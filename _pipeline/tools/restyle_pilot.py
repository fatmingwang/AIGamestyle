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


def run(wf):
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
                    return Image.open(io.BytesIO(http(q))).convert("RGB")
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
    return Image.fromarray(out, "RGBA")


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
