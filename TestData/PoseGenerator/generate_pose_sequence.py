"""
Pose Sequence Generator
========================
Generates a numbered sequence of OpenPose-format skeleton images (COCO-18
keypoints, rendered in ControlNet's canonical rainbow stick-figure
convention) for any animation described by a small JSON keyframe file --
no reference video, motion capture, or manual joint-dragging required, and
no new Python code for a new animation.

Feed the output directly into ControlNet's "openpose" model as the
per-frame conditioning image for Stage 3 of the sprite pipeline
(see Doc/Sprite_Animation_To_Atlas_Pipeline.md).

Data-driven design
-------------------
Every animation is a JSON file under poses_library/ describing a handful
of keyframes (angle in degrees, or pixel offset, at a given point 0..1
through the action) for a fixed set of channels:

  pelvis_dx, pelvis_dy                          -- root offset in pixels
  r_hip_deg,       l_hip_deg                    -- thigh angle from vertical
  r_knee_flex_deg, l_knee_flex_deg              -- knee bend, independent of hip angle
  r_shoulder_deg,  l_shoulder_deg                -- upper-arm angle from vertical
  r_elbow_flex_deg, l_elbow_flex_deg            -- added forearm bend

Missing channels default to 0 (straight limb, no offset). Channels are
smoothstep-interpolated between keyframes, so 4-7 keyframe points already
give a smooth curve. "loop": true means the animation repeats forever
(frame N+1 wraps back to frame 1's t=0); "loop": false means it's a single
action (frame 1 = t=0 exactly, last frame = t=1.0 exactly).

Adding a brand-new animation (climb, swim, crawl, punch, ...) means writing
a new poses_library/<name>.json -- no changes to this script. That JSON is
plain structured data, so it's also the kind of thing an LLM can generate
directly from a text description of the motion.

Usage:
  python generate_pose_sequence.py --animation walk --output-dir ./poses
  python generate_pose_sequence.py --animation jump --frames 8 --output-dir ./poses
  python generate_pose_sequence.py --animation poses_library/punch.json --facing left --output-dir ./poses
"""

import argparse
import json
import math
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

LIBRARY_DIR = Path(__file__).parent / "poses_library"

# ---------------------------------------------------------------------------
# COCO-18 keypoint layout (OpenPose body format used by ControlNet-openpose)
# ---------------------------------------------------------------------------
(NOSE, NECK, R_SHO, R_ELB, R_WRI, L_SHO, L_ELB, L_WRI,
 R_HIP, R_KNEE, R_ANK, L_HIP, L_KNEE, L_ANK, R_EYE, L_EYE, R_EAR, L_EAR) = range(18)

# Bone list (child, parent) pairs, matching the standard OpenPose/ControlNet
# annotator topology (lllyasviel/ControlNet annotator/openpose/util.py),
# expressed here with COCO-18 indices instead of the original 1-indexed form.
BONES = [
    (NECK, R_SHO), (NECK, L_SHO),
    (R_SHO, R_ELB), (R_ELB, R_WRI),
    (L_SHO, L_ELB), (L_ELB, L_WRI),
    (NECK, R_HIP), (R_HIP, R_KNEE), (R_KNEE, R_ANK),
    (NECK, L_HIP), (L_HIP, L_KNEE), (L_KNEE, L_ANK),
    (NECK, NOSE),
    (NOSE, R_EYE), (R_EYE, R_EAR),
    (NOSE, L_EYE), (L_EYE, L_EAR),
    (R_SHO, R_EAR), (L_SHO, L_EAR),
]

# Same 18-color rainbow palette used by the reference ControlNet openpose
# renderer, indexed by keypoint (not by bone) so every joint has a fixed,
# recognizable color across frames.
KEYPOINT_COLORS = [
    (255, 0, 0), (255, 85, 0), (255, 170, 0), (255, 255, 0), (170, 255, 0),
    (85, 255, 0), (0, 255, 0), (0, 255, 85), (0, 255, 170), (0, 255, 255),
    (0, 170, 255), (0, 85, 255), (0, 0, 255), (85, 0, 255), (170, 0, 255),
    (255, 0, 255), (255, 0, 170), (255, 0, 85),
]

JOINT_RADIUS = 4
STICK_WIDTH = 4
BONE_BRIGHTNESS = 0.6  # bones are drawn dimmer than joints, matching the
                        # look ControlNet-openpose was trained on.

CHANNELS = [
    "pelvis_dx", "pelvis_dy",
    "r_hip_deg", "r_knee_flex_deg", "l_hip_deg", "l_knee_flex_deg",
    "r_shoulder_deg", "r_elbow_flex_deg", "l_shoulder_deg", "l_elbow_flex_deg",
]


def default_rig():
    """Segment lengths / pivot, tuned for a 256x384 canvas. Override any of
    these from the CLI to fit a different canvas size or body proportion."""
    return {
        "canvas_width": 256,
        "canvas_height": 384,
        "pelvis_x": 128,
        "pelvis_y": 210,
        "torso_length": 90,
        "neck_length": 14,
        "head_radius": 22,
        "shoulder_width": 70,
        "hip_width": 46,
        "thigh_length": 95,
        "shin_length": 90,
        "upperarm_length": 70,
        "forearm_length": 65,
    }


def build_arg_parser():
    p = argparse.ArgumentParser(
        description="Generate an OpenPose skeleton sequence from a JSON keyframe animation"
    )
    p.add_argument("--animation", required=True,
                    help=f"Name of a JSON file in {LIBRARY_DIR.name}/ (e.g. 'walk'), or a path to one")
    p.add_argument("--frames", type=int, default=None,
                    help="Frame count (default: the animation's own default_frames)")
    p.add_argument("--facing", choices=["left", "right"], default="right",
                    help="Direction the character faces/moves")
    p.add_argument("--output-dir", required=True)
    p.add_argument("--prefix", default=None,
                    help="Filename prefix (default: pose_<animation>)")

    for key, default in default_rig().items():
        p.add_argument(f"--{key.replace('_', '-')}", type=float, default=default)

    p.add_argument("--make-preview", action="store_true", default=True,
                    help="Also save a single contact-sheet PNG of all frames")
    p.add_argument("--no-preview", dest="make_preview", action="store_false")
    return p


def load_animation(name_or_path):
    path = Path(name_or_path)
    if not (path.is_file() and path.suffix == ".json"):
        path = LIBRARY_DIR / f"{name_or_path}.json"
    if not path.is_file():
        available = sorted(p.stem for p in LIBRARY_DIR.glob("*.json"))
        raise SystemExit(
            f"Animation '{name_or_path}' not found (looked for {path}).\n"
            f"Available in {LIBRARY_DIR.name}/: {', '.join(available)}"
        )
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    data.setdefault("name", path.stem)
    data.setdefault("loop", True)
    data.setdefault("default_frames", 8)
    data.setdefault("channels", {})
    return data


def interp_curve(keyframes, t):
    """Smoothstep-interpolate a sparse list of {"t":.., "v":..} points."""
    if not keyframes:
        return 0.0
    pts = sorted(keyframes, key=lambda k: k["t"])
    if t <= pts[0]["t"]:
        return pts[0]["v"]
    if t >= pts[-1]["t"]:
        return pts[-1]["v"]
    for (t0, v0), (t1, v1) in zip(
        ((p["t"], p["v"]) for p in pts), ((p["t"], p["v"]) for p in pts[1:])
    ):
        if t0 <= t <= t1:
            frac = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            eased = frac * frac * (3.0 - 2.0 * frac)  # smoothstep
            return v0 + (v1 - v0) * eased
    return pts[-1]["v"]


def sample_channels(animation, t):
    curves = animation["channels"]
    return {name: interp_curve(curves.get(name, []), t) for name in CHANNELS}


def rotate(length, angle_rad):
    """Vector of given length pointing at angle_rad from straight-down (0,+1)."""
    return length * math.sin(angle_rad), length * math.cos(angle_rad)


def _head_and_torso(pelvis, cfg, facing):
    """Head/neck/shoulder/hip placement shared by every pose."""
    kp = {}
    neck = (pelvis[0], pelvis[1] - cfg["torso_length"])
    kp[NECK] = neck
    kp[NOSE] = (neck[0] + facing * 6, neck[1] - cfg["neck_length"] - cfg["head_radius"] + 4)
    kp[R_EYE] = (neck[0] + facing * 8, neck[1] - cfg["neck_length"] - cfg["head_radius"])
    kp[L_EYE] = (neck[0] + facing * 3, neck[1] - cfg["neck_length"] - cfg["head_radius"])
    kp[R_EAR] = (neck[0] + facing * 11, neck[1] - cfg["neck_length"] - cfg["head_radius"] + 3)
    kp[L_EAR] = (neck[0] + facing * 5, neck[1] - cfg["neck_length"] - cfg["head_radius"] + 3)
    kp[R_SHO] = (neck[0] + cfg["shoulder_width"] / 2, neck[1])
    kp[L_SHO] = (neck[0] - cfg["shoulder_width"] / 2, neck[1])
    kp[R_HIP] = (pelvis[0] + cfg["hip_width"] / 2, pelvis[1])
    kp[L_HIP] = (pelvis[0] - cfg["hip_width"] / 2, pelvis[1])
    return kp


def build_pose(values, cfg, facing):
    """The one generic forward-kinematics chain every animation shares.
    `values` is a dict of the CHANNELS sampled at some t (see sample_channels)."""
    pelvis = (
        cfg["pelvis_x"] + facing * values["pelvis_dx"],
        cfg["pelvis_y"] + values["pelvis_dy"],
    )
    kp = _head_and_torso(pelvis, cfg, facing)

    def leg(hip_pos, hip_deg, knee_flex_deg):
        thigh_angle = facing * math.radians(hip_deg)
        # Knee flex is independent of thigh lean, so a knee can bend even
        # when the thigh isn't swinging (e.g. a symmetric crouch).
        shin_angle = thigh_angle - facing * math.radians(knee_flex_deg)
        dx, dy = rotate(cfg["thigh_length"], thigh_angle)
        knee_pos = (hip_pos[0] + dx, hip_pos[1] + dy)
        dx, dy = rotate(cfg["shin_length"], shin_angle)
        ankle_pos = (knee_pos[0] + dx, knee_pos[1] + dy)
        return knee_pos, ankle_pos

    kp[R_KNEE], kp[R_ANK] = leg(kp[R_HIP], values["r_hip_deg"], values["r_knee_flex_deg"])
    kp[L_KNEE], kp[L_ANK] = leg(kp[L_HIP], values["l_hip_deg"], values["l_knee_flex_deg"])

    def arm(sho_pos, sho_deg, elbow_flex_deg):
        shoulder_angle = facing * math.radians(sho_deg)
        forearm_angle = shoulder_angle + facing * math.radians(elbow_flex_deg)
        dx, dy = rotate(cfg["upperarm_length"], shoulder_angle)
        elbow_pos = (sho_pos[0] + dx, sho_pos[1] + dy)
        dx, dy = rotate(cfg["forearm_length"], forearm_angle)
        wrist_pos = (elbow_pos[0] + dx, elbow_pos[1] + dy)
        return elbow_pos, wrist_pos

    kp[R_ELB], kp[R_WRI] = arm(kp[R_SHO], values["r_shoulder_deg"], values["r_elbow_flex_deg"])
    kp[L_ELB], kp[L_WRI] = arm(kp[L_SHO], values["l_shoulder_deg"], values["l_elbow_flex_deg"])

    return kp


def render_skeleton(keypoints, width, height):
    canvas = np.zeros((height, width, 3), dtype=np.uint8)

    for child, parent in BONES:
        if child not in keypoints or parent not in keypoints:
            continue
        x0, y0 = keypoints[child]
        x1, y1 = keypoints[parent]
        mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        length = math.hypot(x1 - x0, y1 - y0)
        angle = math.degrees(math.atan2(y1 - y0, x1 - x0))
        poly = cv2.ellipse2Poly(
            (int(round(mx)), int(round(my))),
            (max(1, int(length / 2)), STICK_WIDTH),
            int(angle), 0, 360, 1,
        )
        color = tuple(int(c * BONE_BRIGHTNESS) for c in KEYPOINT_COLORS[child])
        cv2.fillConvexPoly(canvas, poly, color)

    canvas = (canvas.astype(np.float32) * BONE_BRIGHTNESS).astype(np.uint8)

    for idx, pos in keypoints.items():
        x, y = int(round(pos[0])), int(round(pos[1]))
        cv2.circle(canvas, (x, y), JOINT_RADIUS, KEYPOINT_COLORS[idx], thickness=-1)

    return canvas


def main():
    args = build_arg_parser().parse_args()
    cfg = {key: getattr(args, key) for key in default_rig()}
    facing = 1 if args.facing == "right" else -1

    animation = load_animation(args.animation)
    frames = args.frames or animation["default_frames"]
    looping = animation["loop"]
    prefix = args.prefix or f"pose_{animation['name']}"

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    saved_paths = []
    for i in range(frames):
        t = (i / frames) if looping else (i / max(1, frames - 1))
        values = sample_channels(animation, t)
        keypoints = build_pose(values, cfg, facing)
        canvas = render_skeleton(keypoints, int(cfg["canvas_width"]), int(cfg["canvas_height"]))
        out_path = output_dir / f"{prefix}_{i + 1:02d}.png"
        Image.fromarray(canvas, "RGB").save(out_path)
        saved_paths.append(out_path)
        print(f"Saved {out_path}")

    if args.make_preview and saved_paths:
        strip = np.concatenate([np.array(Image.open(p)) for p in saved_paths], axis=1)
        preview_path = output_dir / f"{prefix}_preview.png"
        Image.fromarray(strip, "RGB").save(preview_path)
        print(f"Saved contact sheet {preview_path}")


if __name__ == "__main__":
    main()
