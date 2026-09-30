"""
Milestone 5 — ComfyUI Batch Executor

Consumes output/<target_key>/style_plan.json and runs each
static image asset through ComfyUI using settings from the style profile.

Usage:
    python _pipeline/tools/run_comfyui_batch.py --batch-id batch_demo
    python _pipeline/tools/run_comfyui_batch.py --batch-id batch_demo --asset-root game
"""

import argparse
import json
import shutil
import sys
import traceback
import uuid
import urllib.request
import urllib.error
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).parent.parent.parent
sys.path.insert(0, str(ROOT))

from TestData.ComgyUIPythonCommonFunction import (
    COMFYUI_SERVER,
    find_node_by_title,
    queue_prompt,
    wait_for_completion,
    download_outputs,
)

# ── Paths ─────────────────────────────────────────────────────────────────────

PIPELINE_DIR      = ROOT / "_pipeline"
WORKFLOW_FILE     = ROOT / "TestData" / "Reskin" / "ReSkinTestWorkFlow.json"
CATEGORIES_FILE   = ROOT / "TestData" / "Reskin" / "reskin_categories.json"

# ── Node titles (must match _meta.title in the workflow JSON) ──────────────────

NODE_IMAGE_A    = "2.Load Image A(Source)"
NODE_IMAGE_B    = "5.Load Image B(Reference)"
NODE_POSITIVE   = "4.CLIP Text Encode (Prompt) Positive"
NODE_NEGATIVE   = "4.CLIP Text Encode (Prompt) Negative"
NODE_UPSCALE_A  = "2b.Upscale Image A to 1024x1024"
NODE_DOWNSCALE  = "11b.Downscale Output to Source Size"
NODE_KSAMPLER   = "9.KSampler"
NODE_SAVE       = "12.Save Image"

IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".webp"}

# ── Loaders ───────────────────────────────────────────────────────────────────

def load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_style_plan(batch_id: str, asset_root: str, output_dir: str | None = None) -> dict:
    target_key = Path(asset_root).name
    base = Path(output_dir).resolve() if output_dir else ROOT / "output"
    path = base / target_key / "step4_style_plan.json"
    if not path.exists():
        print(f"[run_comfyui_batch] style_plan not found: {path}")
        raise SystemExit(1)
    return load_json(path)


def load_style_profile(profile_id: str) -> dict:
    path = PIPELINE_DIR / "style_profiles" / f"{profile_id}.json"
    if not path.exists():
        fallback = PIPELINE_DIR / "style_profiles" / "default_parameters.json"
        if fallback.exists():
            print(f"[run_comfyui_batch] WARNING: style_profile '{profile_id}' not found, using default_parameters")
            path = fallback
        else:
            print(f"[run_comfyui_batch] style_profile not found: {path}")
            raise SystemExit(1)
    return load_json(path)


def load_categories() -> dict:
    raw = load_json(CATEGORIES_FILE)
    # Convert {"positive": ..., "negative": ...} to (pos, neg) tuples
    return {k: (v["positive"], v["negative"]) for k, v in raw.items() if not k.startswith("_")}

# ── Helpers ───────────────────────────────────────────────────────────────────

def upload_image(filepath: str) -> str:
    """Upload image to ComfyUI input folder and return the server filename."""
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


def read_image_size(filepath: str) -> tuple[int, int]:
    """Return (width, height) from PNG/JPEG header without Pillow."""
    path = Path(filepath)
    with open(path, "rb") as f:
        header = f.read(24)
    if header[:8] == b"\x89PNG\r\n\x1a\n":
        import struct
        w, h = struct.unpack(">II", header[16:24])
        return w, h
    if header[:2] == b"\xff\xd8":  # JPEG
        import struct
        with open(path, "rb") as f:
            f.read(2)
            while True:
                marker = f.read(2)
                if not marker or marker[0:1] != b"\xff":
                    break
                length = struct.unpack(">H", f.read(2))[0]
                if marker[1:2] in (b"\xc0", b"\xc2"):
                    f.read(1)
                    h, w = struct.unpack(">HH", f.read(4))
                    return w, h
                f.read(length - 2)
    try:
        from PIL import Image as PILImage
        with PILImage.open(path) as img:
            return img.size
    except ImportError:
        pass
    raise ValueError(f"Cannot read image size: {filepath}")


def parse_atlas_png_pages(atlas_path: Path) -> list:
    """Return all PNG page filenames referenced in a Spine .atlas file.

    Atlas format: page filenames appear on lines with no leading whitespace,
    no colon, and an image file extension.  Everything else (attachment names,
    key:value metadata) is ignored.
    """
    pages = []
    try:
        text = atlas_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return pages
    for line in text.splitlines():
        if not line or line[0].isspace() or ":" in line:
            continue
        name = line.strip()
        if Path(name).suffix.lower() in IMAGE_EXTENSIONS:
            pages.append(name)
    return pages


def resolve_category(character_id: str, categories: dict) -> str:
    """Match character_id to a category key (exact, then substring, then default)."""
    if character_id in categories:
        return character_id
    for key in categories:
        if key == "default":
            continue
        if key.lower() in character_id.lower():
            return key
    return "default"

# ── Core executor ─────────────────────────────────────────────────────────────

def run_one_asset(
    asset_path: str,
    seed: int,
    character_id: str,
    profile: dict,
    categories: dict,
    output_dir: str,
    reference_server_name: str | None = None,
    positive_prompt: str | None = None,
    negative_prompt: str | None = None,
    ip_weight: float | None = None,
    denoise: float | None = None,
    steps: int | None = None,
) -> None:
    with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
        workflow = json.load(f)
    workflow = deepcopy(workflow)

    # 1. Upload source image
    print(f"    Uploading: {Path(asset_path).name}")
    server_name = upload_image(asset_path)

    # 2. Set Image A
    img_a_id = find_node_by_title(workflow, NODE_IMAGE_A)
    workflow[img_a_id]["inputs"]["image"] = server_name

    # 2b. Set Image B (reference) if uploaded
    if reference_server_name:
        img_b_id = find_node_by_title(workflow, NODE_IMAGE_B)
        workflow[img_b_id]["inputs"]["image"] = reference_server_name

    # 3. Upscale node — always target SDXL 1024×1024
    upscale_id = find_node_by_title(workflow, NODE_UPSCALE_A)
    workflow[upscale_id]["inputs"]["width"]  = 1024
    workflow[upscale_id]["inputs"]["height"] = 1024

    # 4. Downscale output back to source dimensions
    src_w, src_h = read_image_size(asset_path)
    down_id = find_node_by_title(workflow, NODE_DOWNSCALE)
    workflow[down_id]["inputs"]["width"]  = src_w
    workflow[down_id]["inputs"]["height"] = src_h

    # 5. Prompts — CLI/UI override takes priority over reskin_categories.json
    if positive_prompt or negative_prompt:
        pos = positive_prompt or ""
        neg = negative_prompt or ""
        print(f"    Prompts: [override]  character_id: {character_id}")
    else:
        cat = resolve_category(character_id, categories)
        pos, neg = categories[cat]
        print(f"    Category: [{cat}]  character_id: {character_id}")
    workflow[find_node_by_title(workflow, NODE_POSITIVE)]["inputs"]["text"] = pos
    workflow[find_node_by_title(workflow, NODE_NEGATIVE)]["inputs"]["text"] = neg

    # 6. KSampler — CLI overrides take priority over style profile
    ks_id = find_node_by_title(workflow, NODE_KSAMPLER)
    ks = workflow[ks_id]["inputs"]
    ks["seed"]    = seed
    ks["cfg"]     = profile.get("sampler", {}).get("cfg_scale", 6.5)
    ks["steps"]   = steps   if steps   is not None else profile.get("sampler", {}).get("steps",   20)
    ks["denoise"] = denoise if denoise is not None else profile.get("sampler", {}).get("denoise", 0.80)

    # 7. IPAdapter weight — CLI override takes priority over style profile
    effective_ip = ip_weight if ip_weight is not None else profile.get("ip_adapter_strength", 0.75)
    for nid, node in workflow.items():
        if node.get("class_type") == "IPAdapterAdvanced":
            node["inputs"]["weight"] = effective_ip
            break

    # 8. Output prefix: batch_id/source_group/asset_stem
    stem   = Path(asset_path).stem
    save_id = find_node_by_title(workflow, NODE_SAVE)
    workflow[save_id]["inputs"]["filename_prefix"] = stem

    # 9. Queue and wait
    client_id = str(uuid.uuid4())
    result    = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print(f"    Queued: {prompt_id}")
    wait_for_completion(prompt_id, client_id)
    save_name = stem + Path(asset_path).suffix
    download_outputs(prompt_id, output_dir=output_dir, save_filename=save_name)
    print(f"    Saved → {output_dir}/{save_name}")

# ── Batch runner ──────────────────────────────────────────────────────────────

def run_batch(
    batch_id: str,
    asset_root: str,
    output_dir: str | None = None,
    hero_reference: str | None = None,
    positive_prompt: str | None = None,
    negative_prompt: str | None = None,
    include_spine: bool = False,
    ip_weight: float | None = None,
    denoise: float | None = None,
    steps: int | None = None,
) -> None:
    plan       = load_style_plan(batch_id, asset_root, output_dir)
    profile    = load_style_profile(plan["style_profile_id"])
    categories = load_categories()
    asset_base = Path(asset_root)

    # Detect stale style plan: abort if it was generated for a different asset root.
    plan_root = plan.get("scanned_asset_root")
    if plan_root and Path(plan_root).resolve() != asset_base.resolve():
        print(
            f"\n[ERROR] Style plan mismatch!\n"
            f"  Plan was generated for: {plan_root}\n"
            f"  Current asset root:     {asset_base.resolve()}\n"
            f"\n  The style plan is stale. Re-run steps Scan → Classify → Link → Plan Style\n"
            f"  to regenerate it for the current asset folder, then retry this step.\n",
            flush=True,
        )
        raise SystemExit(1)

    if not output_dir:
        output_dir = str(ROOT / "output" / batch_id)
    print(f"Output dir: {output_dir}")

    # Upload reference image once for the whole batch.
    # Priority: CLI --hero-reference > style profile hero_reference field.
    reference_server_name = None
    hero_ref = hero_reference or profile.get("hero_reference", "")
    if hero_ref:
        hero_ref_path = ROOT / hero_ref
        if hero_ref_path.exists():
            print(f"Uploading reference image: {hero_ref_path.name}")
            reference_server_name = upload_image(str(hero_ref_path))
            print(f"Reference uploaded as: {reference_server_name}")
        else:
            print(f"WARNING: hero_reference not found, using workflow default: {hero_ref_path}")
    else:
        print("hero_reference not set in style profile — using workflow default reference image")

    groups = plan["groups"]
    print(f"Batch: {batch_id}  |  Profile: {plan['style_profile_id']}")
    print(f"Groups: {len(groups)}  |  Asset root: {asset_base}\n")

    # Count total image assets up front for progress reporting
    all_static = [
        a for g in groups
        for a in (g.get("static_assets", []) + (g.get("spine_assets", []) if include_spine else []))
        if Path(a).suffix.lower() in IMAGE_EXTENSIONS
    ]
    grand_total = len(all_static)
    print(f"[GS:STEP_START step=comfyui]", flush=True)

    failed = []
    total_assets = 0

    for g_idx, group in enumerate(groups, 1):
        source_group  = group["source_group"]
        character_id  = group.get("character_id", "default")
        seed          = group["seed"]
        spine_sources = group.get("spine_assets", []) if include_spine else []

        # ── Build the image list ──────────────────────────────────────────
        # Static groups: use asset paths from the plan directly.
        static_image_abs = [
            asset_base / a.lstrip("/")
            for a in group.get("static_assets", [])
            if Path(a).suffix.lower() in IMAGE_EXTENSIONS
        ]

        # Spine groups: parse every .atlas file to discover ALL referenced PNG
        # pages — the scan may have only recorded one PNG even when the atlas
        # references several texture sheets.
        spine_image_abs = []
        spine_companion_abs = []  # .json and .atlas to copy verbatim
        if spine_sources:
            atlas_files = [
                asset_base / a.lstrip("/")
                for a in spine_sources
                if Path(a).suffix.lower() == ".atlas"
            ]
            for atlas_abs in atlas_files:
                pages = parse_atlas_png_pages(atlas_abs)
                if pages:
                    atlas_dir = atlas_abs.parent
                    for pg in pages:
                        spine_image_abs.append(atlas_dir / pg)
                    if len(pages) > 1:
                        print(f"    Atlas {atlas_abs.name}: {len(pages)} PNG page(s) found")
                else:
                    # Atlas unreadable or empty — fall back to scanned PNGs
                    for a in spine_sources:
                        if Path(a).suffix.lower() in IMAGE_EXTENSIONS:
                            spine_image_abs.append(asset_base / a.lstrip("/"))

            # Companion files (.json, .atlas) are copied unchanged
            spine_companion_abs = [
                asset_base / a.lstrip("/")
                for a in spine_sources
                if Path(a).suffix.lower() not in IMAGE_EXTENSIONS
            ]

        all_images = static_image_abs + spine_image_abs

        if not all_images:
            reason = "no images" if include_spine else "no static images"
            print(f"[{g_idx}/{len(groups)}] {source_group} — {reason}, skipping")
            continue

        label = f"{len(all_images)} image(s)"
        if spine_image_abs:
            label += f"  [{len(spine_image_abs)} spine page(s)]"
        print(f"[{g_idx}/{len(groups)}] {source_group}  seed={seed}  ({label})")

        # ── Process images through ComfyUI ────────────────────────────────
        for asset_path in all_images:
            asset_abs = str(asset_path)
            if not asset_path.exists():
                print(f"    SKIP (not found): {asset_abs}")
                failed.append(asset_abs)
                total_assets += 1
                print(f"[GS:PROGRESS step=comfyui current={total_assets} total={grand_total}]", flush=True)
                continue
            try:
                run_one_asset(asset_abs, seed, character_id, profile, categories, output_dir, reference_server_name, positive_prompt, negative_prompt, ip_weight=ip_weight, denoise=denoise, steps=steps)
                total_assets += 1
                print(f"[GS:PROGRESS step=comfyui current={total_assets} total={grand_total}]", flush=True)
            except Exception as e:
                print(f"    ERROR [{asset_path.name}]: {e}")
                print(traceback.format_exc())
                failed.append(asset_abs)

        # ── Copy spine companion files (.json, .atlas) verbatim ───────────
        for src_abs in spine_companion_abs:
            if src_abs.exists():
                dst = Path(output_dir) / src_abs.name
                shutil.copy2(str(src_abs), str(dst))
                print(f"    Copied → {dst.name}")
            else:
                print(f"    SKIP companion (not found): {src_abs}")

    print(f"\n{'─'*50}")
    print(f"Done. Processed: {total_assets} asset(s)")
    if failed:
        print(f"Failed ({len(failed)}):")
        for f in failed:
            print(f"  {f}")
    print(f"[GS:STEP_DONE step=comfyui processed={total_assets} failed={len(failed)}]", flush=True)

# ── CLI ───────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ComfyUI batch executor (Milestone 5)")
    parser.add_argument("--batch-id",        required=True, help="Batch ID, e.g. batch_demo")
    parser.add_argument("--asset-root",      default="game", help="Root folder containing textures/ and spine/")
    parser.add_argument("--output-dir",      default=None, help="Output directory (default: <project_root>/output/<batch_id>)")
    parser.add_argument("--hero-reference",  default=None, help="Override hero reference image path")
    parser.add_argument("--positive-prompt", default=None, help="Override positive prompt for all assets (ignores reskin_categories.json)")
    parser.add_argument("--negative-prompt", default=None, help="Override negative prompt for all assets (ignores reskin_categories.json)")
    parser.add_argument("--include-spine",   action="store_true", help="Also restyle spine texture PNGs (spine_assets), not just static images")
    parser.add_argument("--ip-weight",  type=float, default=None, help="Override IPAdapter weight (0.0-1.0)")
    parser.add_argument("--denoise",    type=float, default=None, help="Override KSampler denoise strength (0.0-1.0)")
    parser.add_argument("--steps",      type=int,   default=None, help="Override KSampler step count")
    args = parser.parse_args()
    run_batch(args.batch_id, args.asset_root, args.output_dir, args.hero_reference, args.positive_prompt, args.negative_prompt, args.include_spine, ip_weight=args.ip_weight, denoise=args.denoise, steps=args.steps)
