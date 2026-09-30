"""
ImageWithPrompt Runner
======================
Generates a NEW image from a style reference image + text prompt
using ComfyUI with IPAdapter.

Unlike the Reskin pipeline (which re-styles an existing source image),
this workflow starts from an empty latent — the reference only guides
the visual style, while the prompt drives the content.

Generation pipeline:
  EmptyLatentImage (1024×1024) → KSampler → VAEDecode
  → ImageScale (downscale to reference dimensions)
  → InspyrenetRembg (remove background → restore alpha)
  → SaveImage

Usage examples
--------------
# Use a category from categories.json:
python run_imagewithprompt.py --reference W1.png --category symbol_high --output-dir ./out

# Provide prompts directly:
python run_imagewithprompt.py --reference W1.png \\
    --positive "gold dragon coin, cartoon game icon, glowing, vibrant" \\
    --negative "blurry, realistic, low quality" \\
    --output-dir ./out

# Full control:
python run_imagewithprompt.py --reference ref.png --category character \\
    --seed 1234 --steps 25 --ip-weight 0.8 \\
    --output-name hero_portrait --output-dir ./out
"""

import argparse
import json
import sys
import uuid
import urllib.request
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

WORKFLOW_FILE   = Path(__file__).parent / "ImageWithPromptWorkFlow.json"
CATEGORIES_FILE = Path(__file__).parent / "categories.json"

# ── Node titles (must match _meta.title in ImageWithPromptWorkFlow.json) ──────

NODE_REFERENCE = "2.Load Reference Image"
NODE_POSITIVE  = "3.CLIP Text Encode Positive"
NODE_NEGATIVE  = "4.CLIP Text Encode Negative"
NODE_LATENT    = "8.Empty Latent Image"
NODE_KSAMPLER  = "9.KSampler"
NODE_DOWNSCALE = "12.Downscale to Source Size"
NODE_SAVE      = "14.Save Image"

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_categories() -> dict:
    raw = load_json(CATEGORIES_FILE)
    return {k: (v["positive"], v["negative"]) for k, v in raw.items() if not k.startswith("_")}


def resolve_category(name: str, categories: dict) -> str:
    if name in categories:
        return name
    for key in categories:
        if key == "default":
            continue
        if key.lower() in name.lower():
            return key
    return "default"


def get_image_size(filepath: str) -> tuple[int, int]:
    """Return (width, height) of an image using PIL (always available in ComfyUI)."""
    from PIL import Image
    with Image.open(filepath) as img:
        return img.size  # (width, height)


def sdxl_resolution(w: int, h: int) -> tuple[int, int]:
    """
    Scale (w, h) to ~1 MP while preserving aspect ratio, rounded to multiples of 64.
    This gives SDXL the correct canvas shape so the subject fills the whole frame.
    """
    aspect = w / h
    nh = int((1024 * 1024 / aspect) ** 0.5)
    nw = int(nh * aspect)
    nw = max(64, round(nw / 64) * 64)
    nh = max(64, round(nh / 64) * 64)
    return nw, nh


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


# ── Core ──────────────────────────────────────────────────────────────────────

def run_generation(
    reference_path: str,
    output_dir: str,
    positive_prompt: str | None = None,
    negative_prompt: str | None = None,
    category: str | None = None,
    width: int = 0,
    height: int = 0,
    seed: int = 42,
    steps: int = 20,
    cfg: float = 7.0,
    denoise: float = 1.0,
    ip_weight: float = 0.7,
    output_name: str | None = None,
) -> str:
    """
    Run one generation and return the saved output path.

    - Generates at 1024×1024 for best SDXL quality.
    - Downscales the output to the reference image's actual dimensions.
    - Generates at an SDXL-friendly resolution that matches the reference aspect ratio
      so the subject fills the whole frame; then downscales to exact reference dimensions.
    - width/height: if > 0, use directly; otherwise read from the reference file via PIL.
    - Priority for prompts:  --positive/--negative  >  --category  >  'default' category
    """
    categories = load_categories()

    # ── Resolve prompts ───────────────────────────────────────────────────────
    if positive_prompt or negative_prompt:
        pos = positive_prompt or ""
        neg = negative_prompt or ""
        print("Prompts: [override]")
    elif category:
        cat_key = resolve_category(category, categories)
        pos, neg = categories[cat_key]
        print(f"Category: [{cat_key}]")
    else:
        pos, neg = categories["default"]
        print("No prompt or category specified — using 'default'.")

    # ── Resolve output dimensions ─────────────────────────────────────────────
    if width > 0 and height > 0:
        ref_w, ref_h = width, height
        print(f"Output size: {ref_w} × {ref_h}  (from --width/--height)")
    else:
        ref_w, ref_h = get_image_size(reference_path)
        print(f"Output size: {ref_w} × {ref_h}  (read from reference image)")

    gen_w, gen_h = sdxl_resolution(ref_w, ref_h)
    print(f"Generation canvas: {gen_w} × {gen_h}  (aspect-matched for SDXL)")

    # ── Upload reference ──────────────────────────────────────────────────────
    print(f"Uploading reference: {Path(reference_path).name}")
    ref_server_name = upload_image(reference_path)
    print(f"Reference uploaded as: {ref_server_name}")

    # ── Patch workflow ────────────────────────────────────────────────────────
    workflow = deepcopy(load_json(WORKFLOW_FILE))

    workflow[find_node_by_title(workflow, NODE_REFERENCE)]["inputs"]["image"] = ref_server_name
    workflow[find_node_by_title(workflow, NODE_POSITIVE)]["inputs"]["text"]   = pos
    workflow[find_node_by_title(workflow, NODE_NEGATIVE)]["inputs"]["text"]   = neg

    # EmptyLatentImage: use aspect-matched SDXL dimensions so the subject fills the frame
    lat_key = find_node_by_title(workflow, NODE_LATENT)
    if lat_key:
        workflow[lat_key]["inputs"]["width"]  = gen_w
        workflow[lat_key]["inputs"]["height"] = gen_h

    # KSampler params
    ks = workflow[find_node_by_title(workflow, NODE_KSAMPLER)]["inputs"]
    ks["seed"]    = seed
    ks["steps"]   = steps
    ks["cfg"]     = cfg
    ks["denoise"] = denoise

    # IPAdapter weight
    for node in workflow.values():
        if node.get("class_type") == "IPAdapterAdvanced":
            node["inputs"]["weight"] = ip_weight
            break

    # Downscale: override GetImageSize node connections with literal pixel dimensions.
    # The workflow already has a GetImageSize node wired into the downscale node for
    # use in the ComfyUI UI — this Python override is belt-and-suspenders for the API path.
    ds_key = find_node_by_title(workflow, NODE_DOWNSCALE)
    if ds_key is None:
        print(f"WARNING: node '{NODE_DOWNSCALE}' not found in workflow — output will not be resized")
    else:
        workflow[ds_key]["inputs"]["width"]  = ref_w
        workflow[ds_key]["inputs"]["height"] = ref_h
        print(f"Downscale node patched: {ref_w} × {ref_h}")

    stem = output_name or Path(reference_path).stem
    workflow[find_node_by_title(workflow, NODE_SAVE)]["inputs"]["filename_prefix"] = stem

    # ── Queue → wait → download ───────────────────────────────────────────────
    client_id = str(uuid.uuid4())
    result    = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print(f"Queued: {prompt_id}")
    wait_for_completion(prompt_id, client_id)

    Path(output_dir).mkdir(parents=True, exist_ok=True)
    save_name = stem + ".png"
    download_outputs(prompt_id, output_dir=str(output_dir), save_filename=save_name)
    out_path = str(Path(output_dir) / save_name)
    print(f"Saved → {out_path}")
    return out_path


# ── CLI ───────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate a new image from a style reference + prompt via ComfyUI IPAdapter"
    )
    parser.add_argument("--reference",
                        required=True,
                        help="Path to the style reference image")
    parser.add_argument("--output-dir",
                        default="./output",
                        help="Directory to save the generated image (default: ./output)")
    parser.add_argument("--output-name",
                        default=None,
                        help="Output filename stem, no extension (default: reference stem)")
    parser.add_argument("--category",
                        default=None,
                        help="Prompt category from categories.json "
                             "(e.g. symbol_high, symbol_wild, background, character)")
    parser.add_argument("--positive-prompt",
                        default=None,
                        help="Positive prompt text — overrides --category")
    parser.add_argument("--negative-prompt",
                        default=None,
                        help="Negative prompt text — overrides --category")
    parser.add_argument("--width",
                        type=int, default=0,
                        help="Output width in pixels (default: read from reference image)")
    parser.add_argument("--height",
                        type=int, default=0,
                        help="Output height in pixels (default: read from reference image)")
    parser.add_argument("--seed",
                        type=int, default=42,
                        help="Sampler seed (default: 42)")
    parser.add_argument("--steps",
                        type=int, default=20,
                        help="Sampler steps (default: 20)")
    parser.add_argument("--cfg",
                        type=float, default=7.0,
                        help="CFG scale (default: 7.0)")
    parser.add_argument("--denoise",
                        type=float, default=1.0,
                        help="Denoise strength — 1.0 = full generation from noise (default: 1.0)")
    parser.add_argument("--ip-weight",
                        type=float, default=0.7,
                        help="IPAdapter style weight 0.0-1.0 (default: 0.7)")

    args = parser.parse_args()

    run_generation(
        reference_path=args.reference,
        output_dir=args.output_dir,
        positive_prompt=args.positive_prompt,
        negative_prompt=args.negative_prompt,
        category=args.category,
        width=args.width,
        height=args.height,
        seed=args.seed,
        steps=args.steps,
        cfg=args.cfg,
        denoise=args.denoise,
        ip_weight=args.ip_weight,
        output_name=args.output_name,
    )
