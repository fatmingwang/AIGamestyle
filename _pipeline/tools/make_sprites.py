"""New 32 x 32 pixel-art sprites for TOMS from descriptions (docs/27_AI_SPRITES.md).

    python make_sprites.py <out_dir> --sprite id "description" [--sprite id2 "description2" ...] [--seed 3]
    python make_sprites.py <out_dir> --plan sprites.json [--only monster_] [--force]

Each sprite is made in three steps, so it matches the art already in the game (32 x 32, flat colours, a dark
outline, a transparent background):

  1. ComfyUI: SDXL base + the pixel-art-xl LoRA draws the description at 1024 x 1024 on a plain background;
  2. Inspyrenet (ComfyUI node) removes the background -> RGBA;
  3. here: cropped to what was drawn, shrunk to fit 30 x 30 inside a 32 x 32 tile (feet on the bottom row for
     creatures and people, centred for things), alpha made hard (no half-transparent pixels, like the originals),
     colours reduced to a small palette, and a 1-pixel dark outline drawn around it.

A category with "base" (doors, keys, stairs, walls, floors) is derived instead: the game's own sprite of that kind is
enlarged, changed by img2img toward the description (ControlNet holds its shape), shrunk back, and given the base's
exact silhouette and outline pixels -- so a new emerald door has the yellow door's shape and sits on the map the same.

The 1024 picture is kept beside the sprite (<id>_big.png) for checking or redoing the shrink.
"""
import argparse
import io
import json
import time
import urllib.request
import uuid
from pathlib import Path

import numpy as np
from PIL import Image

SERVER = "127.0.0.1:8188"
CHECKPOINT = "sd_xl_base_1.0.safetensors"
LORA = "pixel-art-xl.safetensors"
STYLE = ("pixel art, chibi 32x32 retro game sprite, single subject, centered, full figure, big head, chunky simple body, "
         "very few details, thick black outline, flat colors, 4 to 6 colors, cute, plain white background")
NEGATIVE = ("photo, realistic, 3d render, blurry, noise, gradient background, text, letters, watermark, frame, border, "
            "multiple subjects, cropped, scenery, shadow on ground")
OUTLINE = (24, 20, 28)   # the near-black of the game's outlines


def http(path, data=None, ctype="application/json"):
    req = urllib.request.Request(f"http://{SERVER}{path}", data=data, headers={"Content-Type": ctype} if data else {})
    with urllib.request.urlopen(req, timeout=900) as r:
        return r.read()


def workflow(prompt, negative, seed, steps=28, cfg=6.5, reference=None, ref_weight=0.3, cutout=True):
    """reference: an uploaded picture of the game's own sprites (enlarged, nearest), so the new ones follow their look."""
    wf = {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": CHECKPOINT}},
        "2": {"class_type": "LoraLoader", "inputs": {"model": ["1", 0], "clip": ["1", 1], "lora_name": LORA,
                                                       "strength_model": 1.0, "strength_clip": 1.0}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["2", 1]}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": negative, "clip": ["2", 1]}},
        "5": {"class_type": "EmptyLatentImage", "inputs": {"width": 1024, "height": 1024, "batch_size": 1}},
        "6": {"class_type": "KSampler", "inputs": {"model": ["2", 0], "positive": ["3", 0], "negative": ["4", 0],
                                                    "latent_image": ["5", 0], "seed": seed, "steps": steps, "cfg": cfg,
                                                    "sampler_name": "dpmpp_2m", "scheduler": "karras", "denoise": 1.0}},
        "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["1", 2]}},
        "8": {"class_type": "InspyrenetRembg", "inputs": {"image": ["7", 0], "torchscript_jit": "default"}},
        "9": {"class_type": "SaveImage", "inputs": {"images": ["8", 0], "filename_prefix": "toms_sprite"}},
    }
    if reference:
        wf["20"] = {"class_type": "LoadImage", "inputs": {"image": reference}}
        wf["21"] = {"class_type": "CLIPVisionLoader", "inputs": {"clip_name": "CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"}}
        wf["22"] = {"class_type": "IPAdapterModelLoader", "inputs": {"ipadapter_file": "ip-adapter-plus_sdxl_vit-h.safetensors"}}
        wf["23"] = {"class_type": "IPAdapterAdvanced",
                    "inputs": {"model": ["2", 0], "ipadapter": ["22", 0], "image": ["20", 0], "clip_vision": ["21", 0],
                               "weight": ref_weight, "weight_type": "style transfer", "combine_embeds": "concat",
                               "start_at": 0.0, "end_at": 1.0, "embeds_scaling": "V only"}}
        wf["6"]["inputs"]["model"] = ["23", 0]
    if not cutout:   # a tile: the whole picture, no background removal
        del wf["8"]
        wf["9"]["inputs"]["images"] = ["7", 0]
    return wf


def derive_workflow(image_name, prompt, negative, seed, denoise=0.7, control=0.45, steps=28, cfg=7.0):
    """img2img of an uploaded (enlarged) game sprite with the pixel-art LoRA; ControlNet canny keeps its shape."""
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": CHECKPOINT}},
        "2": {"class_type": "LoraLoader", "inputs": {"model": ["1", 0], "clip": ["1", 1], "lora_name": LORA,
                                                       "strength_model": 1.0, "strength_clip": 1.0}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["2", 1]}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": negative, "clip": ["2", 1]}},
        "5": {"class_type": "LoadImage", "inputs": {"image": image_name}},
        "6": {"class_type": "VAEEncode", "inputs": {"pixels": ["5", 0], "vae": ["1", 2]}},
        "10": {"class_type": "Canny", "inputs": {"image": ["5", 0], "low_threshold": 0.2, "high_threshold": 0.5}},
        "11": {"class_type": "ControlNetLoader", "inputs": {"control_net_name": "controlnet-union-sdxl-promax.safetensors"}},
        "12": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["11", 0], "type": "canny/lineart/anime_lineart/mlsd"}},
        "13": {"class_type": "ControlNetApplyAdvanced",
               "inputs": {"positive": ["3", 0], "negative": ["4", 0], "control_net": ["12", 0], "image": ["10", 0],
                          "strength": control, "start_percent": 0.0, "end_percent": 0.8, "vae": ["1", 2]}},
        "7": {"class_type": "KSampler", "inputs": {"model": ["2", 0], "positive": ["13", 0], "negative": ["13", 1],
                                                    "latent_image": ["6", 0], "seed": seed, "steps": steps, "cfg": cfg,
                                                    "sampler_name": "dpmpp_2m", "scheduler": "karras", "denoise": denoise}},
        "8": {"class_type": "VAEDecode", "inputs": {"samples": ["7", 0], "vae": ["1", 2]}},
        "9": {"class_type": "SaveImage", "inputs": {"images": ["8", 0], "filename_prefix": "toms_derive"}},
    }


def derive_finish(big: Image.Image, base: Image.Image, colors=8) -> Image.Image:
    """Back to 32 x 32 with the base's exact silhouette; its near-black outline pixels stay near-black."""
    from PIL import ImageEnhance
    small = big.convert("RGB").resize(base.size, Image.BOX)
    small = ImageEnhance.Contrast(ImageEnhance.Color(small).enhance(1.55)).enhance(1.12)
    q = np.asarray(small.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")).copy()
    b = np.asarray(base.convert("RGBA"))
    outline = (b[..., :3].astype(int).sum(-1) < 150) & (b[..., 3] > 0)
    q[outline] = b[..., :3][outline]
    return Image.fromarray(np.concatenate([q, b[..., 3:4]], axis=-1).astype(np.uint8), "RGBA")


def game_reference(names=None) -> Image.Image:
    """The game's own sprites, 8x with nearest, on white: the look new sprites follow."""
    d = Path(__file__).resolve().parents[3] / "assets" / "media" / "sprites"
    files = [d / f"{n}.png" for n in names] if names else sorted(d.glob("*.png"))
    files = [f for f in files if f.exists()]
    cols = 4
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 256, rows * 256), (255, 255, 255))
    for i, f in enumerate(files):
        im = Image.open(f).convert("RGBA").resize((256, 256), Image.NEAREST)
        bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
        bg.alpha_composite(im)
        sheet.paste(bg.convert("RGB"), ((i % cols) * 256, (i // cols) * 256))
    return sheet


def upload(img: Image.Image, name: str) -> str:
    buf = io.BytesIO()
    img.save(buf, "PNG")
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\n"
            f"Content-Type: image/png\r\n\r\n").encode() + buf.getvalue() + (
            f"\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{boundary}--\r\n").encode()
    return json.loads(http("/upload/image", body, f"multipart/form-data; boundary={boundary}"))["name"]


def run(wf) -> Image.Image:
    pid = json.loads(http("/prompt", json.dumps({"prompt": wf, "client_id": uuid.uuid4().hex}).encode()))["prompt_id"]
    while True:
        h = json.loads(http(f"/history/{pid}"))
        if pid in h:
            if h[pid].get("status", {}).get("status_str") == "error":
                raise RuntimeError(json.dumps(h[pid]["status"])[:1500])
            for node in h[pid]["outputs"].values():
                for im in node.get("images", []):
                    q = f"/view?filename={im['filename']}&subfolder={im['subfolder']}&type={im['type']}"
                    return Image.open(io.BytesIO(http(q))).convert("RGBA")
        time.sleep(0.5)


def to_sprite(big: Image.Image, anchor="bottom", colors=8, box=30, tile=32, fill=False) -> Image.Image:
    """The drawn subject -> a 32 x 32 sprite: fit into box x box, hard alpha, a small palette, a dark outline."""
    a = np.asarray(big)[..., 3]
    ys, xs = np.nonzero(a > 40)
    if len(xs) == 0:
        return Image.new("RGBA", (tile, tile), (0, 0, 0, 0))
    crop = big.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    w, h = crop.size
    s = (box - 2) / max(w, h)                       # room for the outline
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    if fill:                                        # a tile thing (a door, stairs): the whole square
        nw = nh = box - 2
    small = crop.resize((nw, nh), Image.BOX)
    # shrinking blends neighbouring colours: give back the bold, flat look of the game's sprites
    from PIL import ImageEnhance
    rgb_small = ImageEnhance.Contrast(ImageEnhance.Color(small.convert("RGB")).enhance(1.35)).enhance(1.15)
    small = Image.merge("RGBA", (*rgb_small.split(), small.getchannel("A")))
    # hard alpha
    arr = np.asarray(small).copy()
    solid = arr[..., 3] >= 110
    # a small palette for the solid pixels (median cut over them only)
    rgb = Image.fromarray(arr[..., :3])
    q = rgb.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
    arr[..., :3] = np.asarray(q)
    arr[..., 3] = np.where(solid, 255, 0)
    out = Image.new("RGBA", (tile, tile), (0, 0, 0, 0))
    x0 = (tile - nw) // 2
    y0 = tile - 1 - nh if anchor == "bottom" else (tile - nh) // 2
    out.paste(Image.fromarray(arr), (x0, y0))
    # the outline: transparent pixels next to the subject become near-black
    o = np.asarray(out).copy()
    al = o[..., 3] > 0
    ring = np.zeros_like(al)
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        ring |= np.roll(np.roll(al, dy, 0), dx, 1)
    ring &= ~al
    o[ring] = OUTLINE + (255,)
    return Image.fromarray(o)


def to_tile(big: Image.Image, colors=8, tile=32) -> Image.Image:
    """A wall or floor tile: the whole picture shrunk to 32 x 32, opaque, bold, a small palette, no outline."""
    from PIL import ImageEnhance
    small = big.convert("RGB").resize((tile, tile), Image.BOX)
    small = ImageEnhance.Contrast(ImageEnhance.Color(small).enhance(1.25)).enhance(1.1)
    return small.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGBA")


def make(out_dir: Path, sid: str, desc: str, seed: int, anchor: str, style: str, force=False, reference=None, mode="sprite",
         negative_extra="", base=None, denoise=0.7):
    sprite = out_dir / f"{sid}.png"
    if sprite.exists() and not force:
        return False
    t = time.time()
    if base:   # derived from the game's own sprite of this kind: its shape kept
        bimg = Image.open(Path(__file__).resolve().parents[3] / "assets" / "media" / "sprites" / f"{base}.png").convert("RGBA")
        flat = Image.new("RGBA", bimg.size, (255, 255, 255, 255))
        flat.alpha_composite(bimg)
        # grey first: the base's own strong colours would win over the description (an "emerald" door stayed yellow)
        grey = flat.convert("L").convert("RGB")
        name = upload(grey.resize((1024, 1024), Image.NEAREST), f"base_{base}_grey.png")
        neg = NEGATIVE + (", " + negative_extra if negative_extra else "")
        big = run(derive_workflow(name, f"{style}, {desc}", neg, seed, denoise=denoise))
        big.save(out_dir / f"{sid}_big.png")
        derive_finish(big, bimg).save(sprite)
        print(f"{sid:<28} {time.time() - t:.1f}s  (from {base})")
        return True
    tile = mode == "tile"
    neg = (NEGATIVE.replace("gradient background, ", "") if tile else NEGATIVE) + (", " + negative_extra if negative_extra else "")
    big = run(workflow(f"{style}, {desc}", neg, seed,
                       reference=None if tile else reference, cutout=not tile))
    big.save(out_dir / f"{sid}_big.png")
    (to_tile(big) if tile else to_sprite(big, anchor, fill=mode == "fill")).save(sprite)
    print(f"{sid:<28} {time.time() - t:.1f}s")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--sprite", nargs=2, action="append", metavar=("ID", "DESC"), default=[])
    ap.add_argument("--plan")
    ap.add_argument("--only", default="")
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--anchor", default="bottom")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--no-reference", action="store_true", help="without the game's sprites as the style reference")
    a = ap.parse_args()
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(sid, d, a.anchor, STYLE, "sprite", None, "", None, 0.7) for sid, d in a.sprite]
    if a.plan:
        plan = json.loads(Path(a.plan).read_text(encoding="utf-8"))
        for cat in plan["categories"]:
            for sid, d in cat["sprites"].items():
                base = cat.get("base")
                if isinstance(base, dict):   # per sprite: which game sprite it is derived from (stairs up / down)
                    base = next((v for k, v in base.items() if sid.startswith(k)), None)
                jobs.append((sid, d, cat.get("anchor", "bottom"), cat.get("style", STYLE), cat.get("mode", "sprite"),
                             tuple(cat["reference"]) if cat.get("reference") else None, cat.get("negative", ""),
                             base, cat.get("denoise", 0.7)))
    refs = {}   # per category: the game's sprites of that kind (characters for creatures, items for items ...)
    for i, (sid, d, anchor, style, mode, ref_names, neg, base, denoise) in enumerate(jobs):
        if a.only and a.only not in sid:
            continue
        ref = None
        if not a.no_reference:
            if ref_names not in refs:
                tag = "_".join(ref_names[:2]) if ref_names else "all"
                refs[ref_names] = upload(game_reference(list(ref_names) if ref_names else None), f"toms_ref_{tag}.png")
            ref = refs[ref_names]
        make(out, sid, d, a.seed + i * 7919, anchor, style, a.force, reference=ref, mode=mode, negative_extra=neg,
             base=base, denoise=denoise)


if __name__ == "__main__":
    main()
