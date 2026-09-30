"""
Creature Animation Generator
=============================
For any non-human/non-biped creature (bird, fish, dragon, ...) where the
skeleton-driven ControlNet approach in TestData/PoseGenerator/ doesn't apply
-- ControlNet's "openpose" model only understands human COCO-18 skeletons,
and there's no way to express a bird's body plan (wings attached to the
body, no elbows/wrists) on that rig at all.

Instead this asks Kling (image-to-video) to animate one reference image
directly from a text motion prompt, decodes every frame of the resulting
clip, removes the background locally (InspyrenetRembg, already installed),
picks N evenly-spaced frames, and packs them into a sprite-sheet + atlas --
the same Stage 4/5 output shape as the human pipeline, just reached by a
completely different Stage 1-3.

Everything up through background removal happens in a single ComfyUI graph:
    LoadImage(reference) -> KlingImage2VideoNode -> GetVideoComponents
        -> InspyrenetRembg -> SaveImage (every decoded frame)
Frame selection and atlas packing happen locally afterward.

Cost note: KlingImage2VideoNode is a paid cloud API node billed through your
comfy.org account credits (auth_token_comfy_org) -- same billing model as
the Gemini/Recraft nodes used elsewhere in this project. Minimum duration is
5 seconds, so every call decodes a lot more raw frames than you'll keep.

Usage:
    python generate_creature_animation.py \\
        --reference-image ./bird_ref.png \\
        --motion-prompt "a short fat bird flapping its wings, flying in place, loop" \\
        --name BirdFly --frames 8 --output-dir ./output

stdout protocol:
    [PROGRESS] <message>
    [DONE] <absolute_path_to_atlas_json>
    [ERROR] <message>
"""

import argparse
import json
import os
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent
sys.path.insert(0, str(ROOT))

from TestData.ComgyUIPythonCommonFunction import (
    COMFYUI_SERVER,
    queue_prompt,
    wait_for_completion,
    download_outputs,
)

import urllib.request


def upload_image(filepath: str) -> str:
    """Upload an image to ComfyUI's input folder and return the server filename."""
    filename = Path(filepath).name
    boundary = uuid.uuid4().hex
    with open(filepath, "rb") as f:
        file_data = f.read()
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="image"; filename="{filename}"\r\n'
        f"Content-Type: image/png\r\n\r\n"
    ).encode() + file_data + f"\r\n--{boundary}--\r\n".encode()
    req = urllib.request.Request(
        f"http://{COMFYUI_SERVER}/upload/image",
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
    )
    resp = urllib.request.urlopen(req)
    return json.loads(resp.read())["name"]


def build_workflow(uploaded_ref, name, motion_prompt, negative_prompt,
                   model_name, cfg_scale, mode, aspect_ratio, duration):
    return {
        "1": {
            "inputs": {"image": uploaded_ref, "upload": "image"},
            "class_type": "LoadImage",
            "_meta": {"title": "Reference Image"},
        },
        "2": {
            "inputs": {
                "start_frame": ["1", 0],
                "prompt": motion_prompt,
                "negative_prompt": negative_prompt,
                "model_name": model_name,
                "cfg_scale": cfg_scale,
                "mode": mode,
                "aspect_ratio": aspect_ratio,
                "duration": duration,
            },
            "class_type": "KlingImage2VideoNode",
            "_meta": {"title": "Kling Image2Video"},
        },
        "3": {
            "inputs": {"video": ["2", 0]},
            "class_type": "GetVideoComponents",
            "_meta": {"title": "Get Video Components"},
        },
        "4": {
            "inputs": {"image": ["3", 0], "torchscript_jit": "default"},
            "class_type": "InspyrenetRembg",
            "_meta": {"title": "Remove Background"},
        },
        "5": {
            "inputs": {"images": ["4", 0], "filename_prefix": f"{name}_raw"},
            "class_type": "SaveImage",
            "_meta": {"title": "Save Frames"},
        },
    }


def pick_evenly_spaced(paths, count):
    """Select `count` evenly-spaced items from a sorted list, endpoints included."""
    n = len(paths)
    if n <= count:
        return paths
    if count == 1:
        return [paths[0]]
    indices = [round(i * (n - 1) / (count - 1)) for i in range(count)]
    # De-dupe while preserving order, in case count is close to n.
    seen = set()
    result = []
    for idx in indices:
        if idx not in seen:
            seen.add(idx)
            result.append(paths[idx])
    return result


def build_atlas(frame_paths, name, output_dir):
    """Tile frames into one sprite-sheet PNG + a TexturePacker-style JSON atlas."""
    from PIL import Image

    images = [Image.open(p).convert("RGBA") for p in frame_paths]
    w, h = images[0].size
    sheet = Image.new("RGBA", (w * len(images), h), (0, 0, 0, 0))
    frames_meta = {}
    for i, img in enumerate(images):
        if img.size != (w, h):
            img = img.resize((w, h))
        sheet.paste(img, (i * w, 0))
        frames_meta[f"{name}_{i:02d}"] = {
            "frame": {"x": i * w, "y": 0, "w": w, "h": h},
            "duration": 100,
        }

    sheet_path = Path(output_dir) / f"{name}_atlas.png"
    atlas_path = Path(output_dir) / f"{name}_atlas.json"
    sheet.save(sheet_path)
    with open(atlas_path, "w", encoding="utf-8") as f:
        json.dump({
            "frames": frames_meta,
            "meta": {"image": sheet_path.name, "size": {"w": w * len(images), "h": h}},
        }, f, indent=2)
    return sheet_path, atlas_path


def main():
    parser = argparse.ArgumentParser(
        description="Animate a creature reference image via Kling image-to-video, "
                    "extract frames, and pack them into a sprite atlas."
    )
    parser.add_argument("--reference-image", required=True)
    parser.add_argument("--motion-prompt", required=True)
    parser.add_argument("--negative-prompt",
                        default="static, blurry, distorted, extra limbs, deformed")
    parser.add_argument("--name", default=None,
                        help="Output name (default: reference image filename stem)")
    parser.add_argument("--frames", type=int, default=8,
                        help="Evenly-spaced frames to keep from the decoded clip")
    parser.add_argument("--duration", choices=["5", "10"], default="5",
                        help="Kling clip length in seconds (5 is the minimum and cheaper)")
    parser.add_argument("--model-name", default="kling-v2-master")
    parser.add_argument("--mode", choices=["std", "pro"], default="std")
    parser.add_argument("--aspect-ratio", choices=["16:9", "9:16", "1:1"], default="1:1")
    parser.add_argument("--cfg-scale", type=float, default=0.8)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    ref_path = Path(args.reference_image)
    if not ref_path.exists():
        print(f"[ERROR] Reference image not found: {ref_path}", flush=True)
        sys.exit(1)

    name = args.name or ref_path.stem
    output_dir = Path(args.output_dir)
    raw_dir = output_dir / f"{name}_raw"
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_dir.mkdir(parents=True, exist_ok=True)

    print(f"[PROGRESS] Uploading reference image: {ref_path.name}", flush=True)
    uploaded_ref = upload_image(str(ref_path))

    workflow = build_workflow(
        uploaded_ref, name, args.motion_prompt, args.negative_prompt,
        args.model_name, args.cfg_scale, args.mode, args.aspect_ratio, args.duration,
    )

    client_id = str(uuid.uuid4())
    print(f"[PROGRESS] Requesting {args.duration}s Kling video: \"{args.motion_prompt}\"", flush=True)
    result = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print(f"[PROGRESS] Queued: {prompt_id} (this can take a couple of minutes)", flush=True)
    wait_for_completion(prompt_id, client_id)

    print("[PROGRESS] Downloading decoded, background-removed frames...", flush=True)
    saved = download_outputs(prompt_id, output_dir=str(raw_dir))
    if not saved:
        print("[ERROR] No frames were returned by the workflow.", flush=True)
        sys.exit(1)
    print(f"[PROGRESS] Decoded {len(saved)} raw frame(s) from the clip.", flush=True)

    selected = pick_evenly_spaced(sorted(saved), args.frames)
    print(f"[PROGRESS] Selected {len(selected)} evenly-spaced frame(s).", flush=True)

    sheet_path, atlas_path = build_atlas(selected, name, output_dir)
    print(f"[PROGRESS] Wrote sprite sheet: {sheet_path}", flush=True)
    print(f"[DONE] {atlas_path.resolve()}", flush=True)


if __name__ == "__main__":
    main()
