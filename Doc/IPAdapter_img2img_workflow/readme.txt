LEARN THIS ORDER
1 img2img
2 IPAdapter
3 batch automation
4 ControlNet
5 advanced workflows



1. IPAdapter + img2img workflow

Goal:

Image A = original image/content
Image B = style reference
Output = Image A structure + Image B style

IPAdapter uses an image as visual guidance; img2img works by encoding the input image into latent space and resampling it with denoise below 1.0.


Step 1 — Install needed nodes

In ComfyUI Manager, install:

Manager->Custom Nodes Manager

ComfyUI_IPAdapter_plus


Installing node pack is NOT enough

You ALSO need:

IPAdapter models

AND:

CLIP Vision model
select related template to download model.

IPAdapter model
ip-adapter-plus_sdxl_vit-h.safetensors
models/ipadapter/ip-adapter-plus_sdxl_vit-h.safetensors
CLIP Vision
CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors
models/clip_vision/CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors

Checkpoint
Juggernaut XL or other SDXL checkpoint
Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors
models/checkpoints

VAE
Use checkpoint VAE or SDXL-compatible VAE
models/vae


Then restart ComfyUI. The common IPAdapter Plus custom node pack is cubiq/ComfyUI_IPAdapter_plus.
----------------------------------
choice template
SDXL IPAdapter img2img