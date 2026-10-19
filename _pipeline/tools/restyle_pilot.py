"""Pilot: restyle one game picture with ComfyUI (SDXL img2img + ControlNet canny), keeping its size and alpha.

    python restyle_pilot.py <input.png> <out_dir> --prompt "..." [--denoise 0.45 0.6] [--seed 7]

The picture is put on a neutral background, scaled so its long side is 1024 (SDXL's size), restyled, scaled back to the
exact original size, and given the original alpha back -- so the silhouette, the size and the edges never change (what
a Spine attachment or a symbol slot needs).
"""
import argparse
import io
import json
import time
import urllib.request
import uuid
from pathlib import Path

from PIL import Image

SERVER = "127.0.0.1:8188"
CHECKPOINT = "Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors"
CONTROLNET = "controlnet-union-sdxl-promax.safetensors"
NEGATIVE = ("blurry, low quality, deformed, extra objects, text artifacts, watermark, photo, realistic photo, "
            "dark, muddy colours, cropped")


def http(path, data=None, ctype="application/json"):
    req = urllib.request.Request(f"http://{SERVER}{path}", data=data, headers={"Content-Type": ctype} if data else {})
    with urllib.request.urlopen(req, timeout=600) as r:
        return r.read()


def upload(img: Image.Image, name: str) -> str:
    buf = io.BytesIO()
    img.save(buf, "PNG")
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\n"
            f"Content-Type: image/png\r\n\r\n").encode() + buf.getvalue() + (
            f"\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{boundary}--\r\n").encode()
    out = json.loads(http("/upload/image", body, f"multipart/form-data; boundary={boundary}"))
    return out["name"]


def workflow(image_name, prompt, negative, denoise, seed, control_strength, steps=28, cfg=6.0, reference=None, ref_weight=0.6):
    """reference: an uploaded image whose look the result follows (IP-Adapter), e.g. the restyled static symbol for its
    Spine parts, so every part of one object matches."""
    wf = {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": CHECKPOINT}},
        "2": {"class_type": "LoadImage", "inputs": {"image": image_name}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"text": prompt, "clip": ["1", 1]}},
        "4": {"class_type": "CLIPTextEncode", "inputs": {"text": negative, "clip": ["1", 1]}},
        "5": {"class_type": "VAEEncode", "inputs": {"pixels": ["2", 0], "vae": ["1", 2]}},
        "6": {"class_type": "Canny", "inputs": {"image": ["2", 0], "low_threshold": 0.15, "high_threshold": 0.35}},
        "7": {"class_type": "ControlNetLoader", "inputs": {"control_net_name": CONTROLNET}},
        "8": {"class_type": "SetUnionControlNetType", "inputs": {"control_net": ["7", 0], "type": "canny/lineart/anime_lineart/mlsd"}},
        "9": {"class_type": "ControlNetApplyAdvanced",
              "inputs": {"positive": ["3", 0], "negative": ["4", 0], "control_net": ["8", 0], "image": ["6", 0],
                         "strength": control_strength, "start_percent": 0.0, "end_percent": 0.8, "vae": ["1", 2]}},
        "10": {"class_type": "KSampler",
               "inputs": {"model": ["1", 0], "positive": ["9", 0], "negative": ["9", 1], "latent_image": ["5", 0],
                          "seed": seed, "steps": steps, "cfg": cfg, "sampler_name": "dpmpp_2m_sde", "scheduler": "karras",
                          "denoise": denoise}},
        "11": {"class_type": "VAEDecode", "inputs": {"samples": ["10", 0], "vae": ["1", 2]}},
        "12": {"class_type": "SaveImage", "inputs": {"images": ["11", 0], "filename_prefix": "restyle_pilot"}},
    }
    if reference:
        wf["20"] = {"class_type": "LoadImage", "inputs": {"image": reference}}
        wf["21"] = {"class_type": "CLIPVisionLoader", "inputs": {"clip_name": "CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"}}
        wf["22"] = {"class_type": "IPAdapterModelLoader", "inputs": {"ipadapter_file": "ip-adapter-plus_sdxl_vit-h.safetensors"}}
        wf["23"] = {"class_type": "IPAdapterAdvanced",
                    "inputs": {"model": ["1", 0], "ipadapter": ["22", 0], "image": ["20", 0], "clip_vision": ["21", 0],
                               "weight": ref_weight, "weight_type": "linear", "combine_embeds": "concat", "start_at": 0.0,
                               "end_at": 1.0, "embeds_scaling": "V only"}}
        wf["10"]["inputs"]["model"] = ["23", 0]
    return wf


def run(wf, rgba=False):
    prompt_id = json.loads(http("/prompt", json.dumps({"prompt": wf, "client_id": uuid.uuid4().hex}).encode()))["prompt_id"]
    while True:
        hist = json.loads(http(f"/history/{prompt_id}"))
        if prompt_id in hist:
            h = hist[prompt_id]
            if h.get("status", {}).get("status_str") == "error":
                raise RuntimeError(json.dumps(h["status"], indent=1)[:2000])
            for node in h["outputs"].values():
                for im in node.get("images", []):
                    q = f"/view?filename={im['filename']}&subfolder={im['subfolder']}&type={im['type']}"
                    return Image.open(io.BytesIO(http(q))).convert("RGBA" if rgba else "RGB")
        time.sleep(0.5)


def prepare(src: Image.Image, long_side=1024, bg=(128, 128, 128)):
    """On a neutral background, scaled so the long side is long_side, both sides multiples of 8."""
    w, h = src.size
    s = long_side / max(w, h)
    nw, nh = max(8, round(w * s / 8) * 8), max(8, round(h * s / 8) * 8)
    flat = Image.new("RGBA", src.size, bg + (255,))
    flat.alpha_composite(src)
    return flat.convert("RGB").resize((nw, nh), Image.LANCZOS)


def finish(gen: Image.Image, src: Image.Image, bg=(128, 128, 128)) -> Image.Image:
    """Back to the source's exact size, with the source's alpha (silhouette and soft edges kept).

    The picture was restyled over a grey background, so a semi-transparent pixel's colour has that grey mixed in
    (shown = colour * a + grey * (1 - a)). It is taken back out -- colour = (shown - grey * (1 - a)) / a -- or a dark
    see-through button would come out grey and every soft edge would get a grey fringe. Nearly transparent pixels keep
    the source's colour (too little of the new one to recover)."""
    import numpy as np
    shown = np.asarray(gen.resize(src.size, Image.LANCZOS).convert("RGB"), dtype=np.float32)
    s = np.asarray(src, dtype=np.float32)
    a = s[..., 3:4] / 255.0
    grey = np.array(bg, dtype=np.float32)
    with np.errstate(divide="ignore", invalid="ignore"):
        rgb = (shown - grey * (1.0 - a)) / np.maximum(a, 1e-6)
    rgb = np.where(a > 0.08, rgb, s[..., :3])
    out = np.concatenate([np.clip(rgb, 0, 255), s[..., 3:4]], axis=-1).astype(np.uint8)
    return fix_edges(Image.fromarray(out, "RGBA"), src)


def fix_edges(out: Image.Image, src: Image.Image, solid=250, radius=4) -> Image.Image:
    """Smooth, dark-rimmed soft edges like the original's.

    The see-through edge pixels cannot be trusted after the restyle: recovering their colour divides by their alpha,
    which blows small errors up to white on nearly transparent pixels (a jagged white fringe on flags and the wheel's
    rim), and the model drew no rim there anyway (the silhouette sat on grey). So every pixel that is not solid in the
    original takes the colour of the nearest solid pixel of the restyled picture, darkened or lightened as much as
    the original's edge pixel was against its own nearest solid pixel -- the new colour with the old rim. A pixel
    farther than `radius` from any solid one is a see-through area of its own (glass, a veil): it keeps its restyled
    colour when it is fairly opaque, the original's when it is faint (where the recovery is unreliable). Alpha is
    never touched.
    """
    import numpy as np
    o = np.asarray(out.convert("RGBA"), dtype=np.float32)
    s = np.asarray(src.convert("RGBA"), dtype=np.float32)
    alpha = s[..., 3]
    known = alpha >= solid
    if known.all() or not known.any():
        return out
    near_out = np.where(known[..., None], o[..., :3], 0)    # nearest solid colour, restyled
    near_src = np.where(known[..., None], s[..., :3], 0)    # ... and in the original
    have = known.copy()
    h, w = alpha.shape
    for _ in range(radius):   # grow the solid colours outward one pixel per round (8 neighbours)
        if have.all():
            break
        grow = np.zeros_like(have)
        acc_o = np.zeros_like(near_out)
        acc_s = np.zeros_like(near_src)
        cnt = np.zeros((h, w), dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                sh = np.zeros_like(have)
                so = np.zeros_like(near_out)
                ss = np.zeros_like(near_src)
                ys, yd = (slice(0, h - dy), slice(dy, h)) if dy >= 0 else (slice(-dy, h), slice(0, h + dy))
                xs, xd = (slice(0, w - dx), slice(dx, w)) if dx >= 0 else (slice(-dx, w), slice(0, w + dx))
                sh[yd, xd] = have[ys, xs]
                so[yd, xd] = near_out[ys, xs]
                ss[yd, xd] = near_src[ys, xs]
                take = sh & ~have
                acc_o[take] += so[take]
                acc_s[take] += ss[take]
                cnt[take] += 1
                grow |= take
        near_out[grow] = acc_o[grow] / cnt[grow][:, None]
        near_src[grow] = acc_s[grow] / cnt[grow][:, None]
        have |= grow
    lum = lambda c: c[..., 0] * 0.299 + c[..., 1] * 0.587 + c[..., 2] * 0.114
    ratio = np.clip(lum(s[..., :3]) / np.maximum(lum(near_src), 1.0), 0.0, 1.3)[..., None]
    edge = (~known) & (alpha > 0) & have
    rgb = o[..., :3].copy()
    rgb[edge] = np.clip(near_out * ratio, 0, 255)[edge]
    faint = (~known) & (alpha > 0) & (alpha < 100) & ~have
    rgb[faint] = s[..., :3][faint]
    res = np.concatenate([rgb, s[..., 3:4]], axis=-1).astype(np.uint8)
    return Image.fromarray(res, "RGBA")


def is_premultiplied(img: Image.Image) -> bool:
    """True when the picture's colours are premultiplied by its alpha (a Spine page packed with "premultiply alpha":
    no colour above its alpha, nothing in fully transparent pixels). Such a picture must be read with
    unpremultiply() and written with premultiply(), or the game adds the stray colour to the screen (a ghost of a
    removed skirt, glowing part edges). A picture with no soft pixels cannot tell, and counts as not premultiplied."""
    import numpy as np
    a = np.asarray(img.convert("RGBA"), dtype=np.int32)
    al, mx = a[..., 3], a[..., :3].max(-1)
    soft = (al > 0) & (al < 250)
    if soft.sum() < 200:
        return False
    return bool(((mx > al + 2) & (al > 0)).sum() <= soft.sum() * 0.002 and ((al == 0) & (mx > 2)).sum() <= (al == 0).sum() * 0.002)


def premultiply(img: Image.Image) -> Image.Image:
    import numpy as np
    a = np.asarray(img.convert("RGBA"), dtype=np.float32)
    a[..., :3] *= a[..., 3:4] / 255.0
    return Image.fromarray(np.clip(np.round(a), 0, 255).astype(np.uint8), "RGBA")


def unpremultiply(img: Image.Image) -> Image.Image:
    import numpy as np
    a = np.asarray(img.convert("RGBA"), dtype=np.float32)
    al = a[..., 3:4]
    a[..., :3] = np.where(al > 0, a[..., :3] * 255.0 / np.maximum(al, 1), 0)
    return Image.fromarray(np.clip(np.round(a), 0, 255).astype(np.uint8), "RGBA")


def keep_skin(out: Image.Image, src: Image.Image, min_area=200) -> Image.Image:
    """The original's bare skin back in a restyled part (a plan part's "keep_skin": true).

    A part can hold skin and clothes together -- a character's torso is her neck, shoulders and dress top -- and the
    restyle paints the skin in the new clothes' colour (a white neck under the face). Skin is told by colour in the
    original (warm hue 8-40 degrees, saturation 0.18-0.82, not dark: shaded skin reaches 0.78, while a red
    bodice next to it is near 0 degrees and around 0.92); only the largest skin areas (at least min_area
    pixels, and the biggest one plus any other at least a quarter of it) count, so a gold button, embroidery or a
    shaded fold of the same hue stays restyled. The edge is feathered so there is
    no seam. Alpha is never touched.
    """
    import numpy as np
    from PIL import ImageFilter
    o = np.asarray(out.convert("RGBA"), dtype=np.float32)
    s = np.asarray(src.convert("RGBA"), dtype=np.float32)
    rgb = s[..., :3] / 255.0
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = mx - mn + 1e-6
    hue = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    sat = d / (mx + 1e-6)
    skin = (hue >= 8) & (hue <= 40) & (sat >= 0.18) & (sat <= 0.82) & (mx >= 0.35) & (s[..., 3] > 10) & (r > g) & (g >= b * 0.9)
    # connected skin areas (4-neighbour flood fill); small ones are trim of a skin-like colour, not skin
    h, w = skin.shape
    seen = np.zeros_like(skin)
    areas = []
    for y0, x0 in zip(*np.nonzero(skin)):
        if seen[y0, x0]:
            continue
        stack, area = [(y0, x0)], []
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            area.append((y, x))
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < h and 0 <= nx < w and skin[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        if len(area) >= min_area:
            areas.append(area)
    if not areas:
        return out
    # the skin is the biggest area, plus any other at least a quarter of it (two bare arms); a smaller skin-coloured
    # patch is shading on the clothes (a red bodice's orange tip)
    biggest = max(len(a) for a in areas)
    keep = np.zeros_like(skin)
    for area in areas:
        if len(area) * 4 >= biggest:
            ys, xs = zip(*area)
            keep[list(ys), list(xs)] = True
    m = np.asarray(Image.fromarray((keep * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3))
                   .filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32)[..., None] / 255.0
    res = o.copy()
    res[..., :3] = o[..., :3] * (1 - m) + s[..., :3] * m
    res[..., 3] = s[..., 3]
    return Image.fromarray(np.clip(res, 0, 255).astype(np.uint8), "RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("out_dir")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--negative", default=NEGATIVE)
    ap.add_argument("--denoise", type=float, nargs="+", default=[0.5])
    ap.add_argument("--control", type=float, default=0.6)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    src = Image.open(a.input).convert("RGBA")
    name = upload(prepare(src), "pilot_" + Path(a.input).name)
    out_dir = Path(a.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for d in a.denoise:
        t = time.time()
        gen = run(workflow(name, a.prompt, a.negative, d, a.seed, a.control))
        out = finish(gen, src)
        p = out_dir / f"{Path(a.input).stem}_d{int(d * 100)}.png"
        out.save(p)
        print(f"{p}  {out.size}  {time.time() - t:.1f}s")


if __name__ == "__main__":
    main()
