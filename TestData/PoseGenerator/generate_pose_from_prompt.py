"""
generate_pose_from_prompt.py
=============================
Asks an AI backend to write a keyframe pose-animation JSON -- the same
format generate_pose_sequence.py consumes -- from a plain-text description
of a motion (e.g. "a person climbing a ladder, one rung at a time").

This is what makes adding a brand-new animation not require writing any
code: describe the motion in English, get back a JSON file, hand that
JSON straight to generate_pose_sequence.py.

Usage:
    python generate_pose_from_prompt.py --name climb \
        --description "a person climbing a ladder, reaching up with alternating hands" \
        --loop false --frames 6 --backend anthropic --output ./poses_library/climb.json

stdout protocol (read by the Qt app):
    [PROGRESS] <message>
    [DONE] <absolute_path_to_output_json>
    [ERROR] <message>
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from generate_pose_sequence import CHANNELS  # noqa: E402  (shared source of truth)

DEFAULT_MODELS = {
    "ollama":    "llama3.2",
    "anthropic": "claude-haiku-4-5-20251001",
    "openai":    "gpt-4o-mini",
    "llama":     "default",
}


# ── Prompt builder ────────────────────────────────────────────────────────────

def build_system_prompt(subject_type: str) -> str:
    base = (
        "You are a 2D character-animation engine. You describe a motion as keyframe "
        "curves for a fixed set of joint-angle channels on a simple BIPED rig "
        "(one head, two arms with shoulder+elbow, two legs with hip+knee), viewed "
        "from the side. This rig has no wings, tail, beak, fins, or any body part "
        "other than what a two-legged, two-armed body has -- it cannot represent a "
        "quadruped, bird, fish, vehicle, or any subject with a different body plan, "
        "no matter how the channels are tuned."
    )
    if subject_type == "biped_animal":
        scope = (
            "The requested subject is a two-legged animal (kangaroo, ostrich, bipedal "
            "dinosaur, etc.) -- that is IN SCOPE. Map its two legs onto r/l_hip_deg + "
            "r/l_knee_flex_deg for the main locomotion. Map its forelimbs/folded "
            "wings/short arms onto r/l_shoulder_deg + r/l_elbow_flex_deg, but keep "
            "their motion small since most two-legged animals don't swing forelimbs "
            "the way a human arm swings. Use pelvis_dx/pelvis_dy for tail-assisted "
            "body lean and bob. If the subject is instead a quadruped, bird in "
            "flight, fish, or anything else that isn't fundamentally two-legged, "
            "refuse by returning ONLY this JSON: "
            '{"error": "non_humanoid_subject", "reason": "<one sentence>"} '
            "instead of forcing it onto the rig."
        )
    else:
        scope = (
            "If the requested subject is not a human/humanoid character performing "
            "an action (walking, jumping, punching, climbing, dancing, etc. all "
            "count -- a two-legged animal, quadruped, bird, fish, or object does "
            "not), refuse by returning ONLY this JSON: "
            '{"error": "non_humanoid_subject", "reason": "<one sentence>"} '
            "instead of forcing it onto the rig."
        )
    return (
        f"{base} {scope} You never invent new channel names and you never add "
        "prose -- only the requested JSON object."
    )

CHANNEL_DOC = """\
Channels (all optional; a channel not mentioned defaults to a straight/neutral limb):
  pelvis_dx, pelvis_dy   -- root offset in pixels. Positive dx = forward in the
                            facing direction. Negative dy = up, positive dy = down.
  r_hip_deg,  l_hip_deg  -- thigh angle from vertical, degrees. Positive = swung
                            forward in the facing direction, negative = swung back.
  r_knee_flex_deg, l_knee_flex_deg -- knee bend, degrees, independent of hip angle.
                            0 = straight leg. Larger = more bent (heel drawn back/up).
                            Always >= 0.
  r_shoulder_deg, l_shoulder_deg -- upper-arm angle from vertical, degrees. Positive
                            = swung forward, negative = swung back. Values past 90
                            swing the arm up toward overhead.
  r_elbow_flex_deg, l_elbow_flex_deg -- added forearm bend, degrees, independent of
                            shoulder angle. 0 = straight arm. Always >= 0.
"""

def build_user_message(name, description, loop, frames):
    loop_note = (
        "This animation LOOPS forever, so the values at t=1.0 must equal the "
        "values at t=0.0 for every channel you use (seamless wrap)."
        if loop else
        "This animation is a ONE-SHOT action: t=0.0 is the rest pose and t=1.0 is "
        "the end of the action. It does not need to loop."
    )
    return (
        f"{CHANNEL_DOC}\n"
        f"Animation name: {name}\n"
        f"Motion description: {description}\n"
        f"{loop_note}\n"
        f"Target frame count: {frames} (used only as a hint for how many keyframes "
        "are worth defining -- you may use more or fewer keyframe points per curve; "
        "they get smoothly interpolated).\n\n"
        "Return ONLY a raw JSON object (no markdown fences, no explanation) shaped like:\n"
        "{\n"
        f'  "name": "{name}",\n'
        f'  "loop": {"true" if loop else "false"},\n'
        f'  "default_frames": {frames},\n'
        '  "channels": {\n'
        '    "r_hip_deg": [{"t": 0.0, "v": 0}, {"t": 0.5, "v": 20}, {"t": 1.0, "v": 0}],\n'
        "    ... only channels from the list above ...\n"
        "  }\n"
        "}\n"
        "Every keyframe list must be sorted by ascending \"t\", with t in [0.0, 1.0]. "
        "Use 3-7 keyframes per channel -- enough to describe the motion's shape, not "
        "one per output frame."
    )


def _fix_json(s: str) -> str:
    s = re.sub(r",(\s*[}\]])", r"\1", s)
    s = re.sub(r"\}(\s*)\{", r"},\1{", s)
    return s


def strip_thinking(text: str) -> str:
    return re.sub(r"<think>.*?</think>", "", text or "", flags=re.DOTALL).strip()


def extract_json_object(text: str) -> dict:
    text = strip_thinking(text)
    if not text:
        raise json.JSONDecodeError(
            "Model returned nothing but hidden <think> tokens (or an empty "
            "response). Try a non-reasoning model, or a backend/model that "
            "supports disabling extended thinking.",
            "", 0)

    text = re.sub(r"^```(?:json)?\s*", "", text)
    text = re.sub(r"\s*```\s*$", "", text)
    text = text.strip()

    m = re.search(r"\{.*\}", text, re.DOTALL)
    candidate = m.group() if m else text

    for attempt in (candidate, _fix_json(candidate)):
        try:
            return json.loads(attempt)
        except json.JSONDecodeError:
            continue

    print(f"[PROGRESS] ERROR: raw model output (first 600 chars):\n{candidate[:600]}",
          flush=True)
    raise json.JSONDecodeError("Cannot parse model response as a JSON object", candidate, 0)


def sanitize_animation(raw: dict, name: str, loop: bool, frames: int) -> dict:
    """Drop unknown channels/malformed keyframes rather than trust the model blindly --
    this is the boundary between AI output and generate_pose_sequence.py's renderer."""
    clean_channels = {}
    for ch_name, keyframes in (raw.get("channels") or {}).items():
        if ch_name not in CHANNELS or not isinstance(keyframes, list):
            continue
        pts = []
        for kf in keyframes:
            try:
                t = max(0.0, min(1.0, float(kf["t"])))
                v = float(kf["v"])
            except (KeyError, TypeError, ValueError):
                continue
            pts.append({"t": t, "v": v})
        if pts:
            clean_channels[ch_name] = sorted(pts, key=lambda p: p["t"])

    return {
        "name": str(raw.get("name") or name),
        "loop": bool(raw.get("loop", loop)),
        "default_frames": int(raw.get("default_frames") or frames),
        "channels": clean_channels,
    }


# ── Backends (same dispatch pattern as parse_table_to_items.py) ──────────────

def call_ollama(user_msg, model, api_url, system_prompt):
    import ollama
    client_kwargs = {}
    if api_url:
        host = api_url.rstrip("/")
        if host.endswith("/v1"):
            host = host[:-3]
        client_kwargs["host"] = host
    client = ollama.Client(**client_kwargs)

    # Reasoning models (Qwen3, DeepSeek-R1, ...) can burn the whole response on
    # hidden <think> tokens and return empty content -- same failure mode the
    # "llama" backend below already guards against. Belt-and-suspenders: ask
    # newer Ollama builds to disable thinking natively, and append the textual
    # "/no_think" marker Qwen3-style models key off (harmless no-op on models
    # that don't recognize it).
    chat_kwargs = dict(model=model, messages=[
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": user_msg + " /no_think"},
    ])
    try:
        resp = client.chat(**chat_kwargs, think=False)
    except TypeError:
        resp = client.chat(**chat_kwargs)
    return resp["message"]["content"]


def call_anthropic(user_msg, model, api_url, system_prompt):
    import anthropic
    client = anthropic.Anthropic()
    response = client.messages.create(
        model=model, max_tokens=4096, system=system_prompt,
        messages=[{"role": "user", "content": user_msg}],
    )
    return response.content[0].text


def call_openai(user_msg, model, api_url, system_prompt):
    from openai import OpenAI
    client_kwargs = {}
    if api_url:
        client_kwargs["base_url"] = api_url
        import os
        if not os.environ.get("OPENAI_API_KEY"):
            client_kwargs["api_key"] = "ollama"
    client = OpenAI(**client_kwargs)
    resp = client.chat.completions.create(
        model=model, max_tokens=4096,
        messages=[
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_msg},
        ],
    )
    return resp.choices[0].message.content


def call_llama(user_msg, model, api_url, system_prompt):
    from openai import OpenAI
    client = OpenAI(base_url=api_url or "http://127.0.0.1:8666/v1", api_key="llama")
    resp = client.chat.completions.create(
        model=model, max_tokens=8192,
        messages=[
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_msg + " /no_think"},
        ],
    )
    return resp.choices[0].message.content or ""


DISPATCH = {
    "ollama": call_ollama,
    "anthropic": call_anthropic,
    "openai": call_openai,
    "llama": call_llama,
}


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Generate a keyframe pose-animation JSON from a text description"
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--description", required=True)
    parser.add_argument("--subject-type", default="human",
                        choices=["human", "biped_animal"],
                        help="human: refuse anything non-humanoid. biped_animal: also "
                             "allow two-legged animals (kangaroo, ostrich, ...), still "
                             "refuse quadrupeds/birds/fish/objects.")
    parser.add_argument("--loop", default="true", choices=["true", "false"])
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--backend", default="anthropic",
                        choices=["ollama", "anthropic", "openai", "llama"])
    parser.add_argument("--model", default=None)
    parser.add_argument("--api-url", default=None)
    parser.add_argument("--output", default=None,
                        help="Output path (default: poses_library/<name>.json)")
    args = parser.parse_args()

    loop = args.loop == "true"
    model = args.model or DEFAULT_MODELS[args.backend]
    api_url = args.api_url or ""
    system_prompt = build_system_prompt(args.subject_type)

    output_path = (Path(args.output) if args.output
                   else Path(__file__).parent / "poses_library" / f"{args.name}.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"[PROGRESS] Backend: {args.backend}  Model: {model}  Subject: {args.subject_type}",
          flush=True)
    print(f"[PROGRESS] Asking AI to design '{args.name}': {args.description}", flush=True)

    user_msg = build_user_message(args.name, args.description, loop, args.frames)

    try:
        raw_text = DISPATCH[args.backend](user_msg, model, api_url, system_prompt)
    except ImportError as e:
        print(f"[ERROR] Missing package for backend '{args.backend}': {e}", flush=True)
        sys.exit(1)
    except Exception as e:
        print(f"[ERROR] AI backend call failed: {e}", flush=True)
        sys.exit(1)

    print("[PROGRESS] Parsing AI response...", flush=True)
    try:
        raw_json = extract_json_object(raw_text)
    except json.JSONDecodeError as e:
        print(f"[ERROR] {e}", flush=True)
        sys.exit(1)

    if isinstance(raw_json, dict) and raw_json.get("error") == "non_humanoid_subject":
        reason = raw_json.get("reason", "the description is not a humanoid character/action.")
        print(f"[ERROR] This rig can only animate a human biped -- {reason} "
              "Use TestData/CreatureAnimation/generate_creature_animation.py instead "
              "(image-to-video, no skeleton needed, works for any creature).", flush=True)
        sys.exit(1)

    animation = sanitize_animation(raw_json, args.name, loop, args.frames)
    if not animation["channels"]:
        print("[ERROR] AI response had no usable channel keyframes.", flush=True)
        sys.exit(1)

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(animation, f, indent=2)

    print(f"[PROGRESS] Wrote {len(animation['channels'])} channel(s).", flush=True)
    print(f"[DONE] {output_path.resolve()}", flush=True)


if __name__ == "__main__":
    main()
