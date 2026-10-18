"""Check a PC is ready for the reskin tools (Doc/Reskin_Setup_New_PC.md): python check_reskin_setup.py

Asks the running ComfyUI (127.0.0.1:8188) for the nodes and model files restyle_game.py / make_sprites.py use, and
checks this Python has Pillow and numpy. Prints OK / MISSING per item; exit 1 when anything is missing.
"""
import json
import sys
import urllib.request

SERVER = "127.0.0.1:8188"

NODES = {
    "CheckpointLoaderSimple": "ComfyUI core", "LoraLoader": "ComfyUI core", "Canny": "ComfyUI core",
    "ControlNetLoader": "ComfyUI core", "SetUnionControlNetType": "ComfyUI core",
    "ControlNetApplyAdvanced": "ComfyUI core", "CLIPVisionLoader": "ComfyUI core",
    "IPAdapterModelLoader": "custom node ComfyUI_IPAdapter_plus (cubiq)",
    "IPAdapterAdvanced": "custom node ComfyUI_IPAdapter_plus (cubiq)",
    "InspyrenetRembg": "custom node ComfyUI-Inspyrenet-Rembg (john-mnz); only make_sprites.py",
}
MODELS = {
    "checkpoints": ["Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors",   # restyle (restyle_pilot.py)
                    "sd_xl_base_1.0.safetensors"],                         # pixel sprites (make_sprites.py)
    "controlnet": ["controlnet-union-sdxl-promax.safetensors"],
    "ipadapter": ["ip-adapter-plus_sdxl_vit-h.safetensors"],
    "clip_vision": ["CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"],
    "loras": ["pixel-art-xl.safetensors"],                                 # pixel sprites only
}


def get(path):
    with urllib.request.urlopen(f"http://{SERVER}{path}", timeout=30) as r:
        return json.loads(r.read())


def main():
    bad = 0
    for mod in ("PIL", "numpy"):
        try:
            __import__(mod)
            print(f"OK       python package {mod}")
        except ImportError:
            print(f"MISSING  python package {mod}: pip install {'pillow' if mod == 'PIL' else mod}")
            bad += 1
    try:
        stats = get("/system_stats")
    except Exception as e:   # noqa: BLE001
        print(f"MISSING  ComfyUI at {SERVER} ({e}): start it first (Doc/Reskin_Setup_New_PC.md, step 6)")
        return 1
    dev = stats.get("devices", [{}])[0]
    print(f"OK       ComfyUI {stats.get('system', {}).get('comfyui_version', '?')} on {dev.get('name', '?')}, "
          f"{dev.get('vram_total', 0) / 2**30:.1f} GB VRAM")
    info = get("/object_info")
    for node, where in NODES.items():
        ok = node in info
        bad += not ok
        print(f"{'OK      ' if ok else 'MISSING '} node {node}  ({where})")
    for folder, files in MODELS.items():
        try:
            have = set(get(f"/models/{folder}"))
        except Exception:   # noqa: BLE001
            have = set()
        for f in files:
            ok = f in have
            bad += not ok
            print(f"{'OK      ' if ok else 'MISSING '} models/{folder}/{f}")
    print("ready" if not bad else f"{bad} missing")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
