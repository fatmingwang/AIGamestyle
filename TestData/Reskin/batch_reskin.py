import json
import random
import sys
import uuid
import urllib.request
import urllib.error
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from ComgyUIPythonCommonFunction import (
    COMFYUI_SERVER,
    find_node_by_title,
    queue_prompt,
    wait_for_completion,
    download_outputs,
)

# ── Configuration ────────────────────────────────────────────────────────────

WORKFLOW_FILE    = Path(__file__).parent / "ReSkinTestWorkFlow.json"

# Node titles (must match _meta.title in the JSON)
NODE_IMAGE_A     = "2.Load Image A(Source)"
NODE_POSITIVE    = "4.CLIP Text Encode (Prompt) Positive"
NODE_NEGATIVE    = "4.CLIP Text Encode (Prompt) Negative"
NODE_UPSCALE_A   = "2b.Upscale Image A to 1024x1024"
NODE_DOWNSCALE   = "11b.Downscale Output to Source Size"
NODE_KSAMPLER    = "9.KSampler"
NODE_SAVE        = "12.Save Image"

# ── Prompt presets per asset category ────────────────────────────────────────
# Loaded from reskin_categories.json next to this script.
# Key   = folder name (case-insensitive substring match against the image path)
# The FIRST matching category wins; "default" is used when nothing matches.

CATEGORIES_FILE = Path(__file__).parent / "reskin_categories.json"

def _load_categories(path: Path) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)
    except FileNotFoundError:
        print(f"[config] Categories file not found: {path}")
        raise SystemExit(1)
    except json.JSONDecodeError as e:
        print(f"[config] Failed to parse {path}: {e}")
        raise SystemExit(1)
    # Convert {"positive": ..., "negative": ...} dicts to (pos, neg) tuples
    return {k: (v["positive"], v["negative"]) for k, v in raw.items() if not k.startswith("_")}

CATEGORY_PROMPTS = _load_categories(CATEGORIES_FILE)

IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".webp", ".bmp"}

# ── Helpers ───────────────────────────────────────────────────────────────────

def upload_image(filepath: str) -> str:
    """Upload a local image to ComfyUI input folder. Returns the server filename."""
    filename  = Path(filepath).name
    boundary  = uuid.uuid4().hex

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
    try:
        resp = urllib.request.urlopen(req)
        return json.loads(resp.read())["name"]
    except urllib.error.URLError as e:
        print(f"[upload_image] Failed to upload {filename}: {e}")
        raise


def read_image_size(filepath: str) -> tuple[int, int]:
    """Return (width, height) by reading PNG/JPEG header bytes — no Pillow needed."""
    path = Path(filepath)
    with open(path, "rb") as f:
        header = f.read(24)

    if header[:8] == b"\x89PNG\r\n\x1a\n":
        import struct
        w, h = struct.unpack(">II", header[16:24])
        return w, h

    if header[:2] in (b"\xff\xd8",):          # JPEG
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

    # Fallback — try Pillow if available
    try:
        from PIL import Image as PILImage
        with PILImage.open(path) as img:
            return img.size
    except ImportError:
        pass

    raise ValueError(f"Cannot read image size for: {filepath}")


# ── Category detection ────────────────────────────────────────────────────────

def detect_category(image_path: str) -> str:
    """Match folder names in the path against CATEGORY_PROMPTS keys."""
    parts = Path(image_path).parts
    for part in parts:
        for category in CATEGORY_PROMPTS:
            if category == "default":
                continue
            if category.lower() in part.lower():
                return category
    return "default"


# ── Core: run one image through the reskin workflow ───────────────────────────

def reskin_one(source_path: str, output_dir: str) -> None:
    with open(WORKFLOW_FILE, "r", encoding="utf-8") as f:
        workflow = json.load(f)
    workflow = deepcopy(workflow)

    # 1. Upload source image and point Load Image A at it
    print(f"  Uploading {Path(source_path).name} ...")
    server_name = upload_image(source_path)
    img_a_id = find_node_by_title(workflow, NODE_IMAGE_A)
    workflow[img_a_id]["inputs"]["image"] = server_name

    # 2. Set upscale node to SDXL target (fixed 1024×1024)
    upscale_id = find_node_by_title(workflow, NODE_UPSCALE_A)
    workflow[upscale_id]["inputs"]["width"]  = 1024
    workflow[upscale_id]["inputs"]["height"] = 1024

    # 3. Set downscale node to match source image dimensions
    src_w, src_h = read_image_size(source_path)
    down_id = find_node_by_title(workflow, NODE_DOWNSCALE)
    workflow[down_id]["inputs"]["width"]  = src_w
    workflow[down_id]["inputs"]["height"] = src_h

    # 4. Pick prompt based on folder category and inject into workflow
    category = detect_category(source_path)
    pos, neg = CATEGORY_PROMPTS[category]
    print(f"  Category: [{category}]")
    pos_id = find_node_by_title(workflow, NODE_POSITIVE)
    neg_id = find_node_by_title(workflow, NODE_NEGATIVE)
    workflow[pos_id]["inputs"]["text"] = pos
    workflow[neg_id]["inputs"]["text"] = neg

    # 5. Unique seed + output prefix per image
    ks_id = find_node_by_title(workflow, NODE_KSAMPLER)
    workflow[ks_id]["inputs"]["seed"] = random.randint(1, 999_999_999_999_999)

    stem = Path(source_path).stem
    save_id = find_node_by_title(workflow, NODE_SAVE)
    workflow[save_id]["inputs"]["filename_prefix"] = f"reskin_{stem}"

    # 6. Queue and wait
    client_id = str(uuid.uuid4())
    result    = queue_prompt(workflow, client_id)
    prompt_id = result["prompt_id"]
    print(f"  Queued prompt_id: {prompt_id}")

    wait_for_completion(prompt_id, client_id)
    saved = download_outputs(prompt_id, output_dir=output_dir)
    if saved:
        for i, f in enumerate(saved):
            ext = Path(f).suffix
            new_name = f"{stem}{ext}" if len(saved) == 1 else f"{stem}_{i+1}{ext}"
            new_path = Path(output_dir) / new_name
            Path(f).replace(new_path)
            print(f"  Saved → {new_path}")
    else:
        print(f"  WARNING: No files were saved for {stem}. Check ComfyUI logs.")


# ── Batch entry point ─────────────────────────────────────────────────────────

def batch_reskin(source_folder: str, output_dir: str = "output/reskin") -> None:
    src = Path(source_folder)
    if not src.exists():
        print(f"[batch_reskin] Folder not found: {source_folder}")
        raise SystemExit(1)

    images = sorted(p for p in src.rglob("*") if p.suffix.lower() in IMAGE_EXTENSIONS)
    if not images:
        print(f"[batch_reskin] No images found in: {source_folder}")
        raise SystemExit(1)

    print(f"Found {len(images)} image(s) in '{source_folder}'")
    print(f"Output folder: {output_dir}\n")

    failed = []
    for i, img_path in enumerate(images, 1):
        print(f"[{i}/{len(images)}] {img_path.relative_to(src)}")
        try:
            reskin_one(str(img_path), output_dir)
        except Exception as e:
            print(f"  ERROR: {e}")
            failed.append(str(img_path))

    print(f"\n{'─'*50}")
    print(f"Completed: {len(images) - len(failed)}/{len(images)}")
    if failed:
        print("Failed:")
        for f in failed:
            print(f"  {f}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python batch_reskin.py <source_folder> [output_folder]")
        print("Example: python batch_reskin.py assets/warriors output/reskin")
        raise SystemExit(1)

    source = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else "output/reskin"
    batch_reskin(source, output)
