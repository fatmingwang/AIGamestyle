# Reskin tools — setting up a new PC

Everything a Windows PC needs before it can run the reskin tools: `restyle_game.py`, which gives a whole game a new
art style, and `make_sprites.py`, which draws new pixel-art sprites. How the reskin works and how to run it on a game is
in [Reskin_Game_Art.md](Reskin_Game_Art.md).

Traditional Chinese: [Reskin_Setup_New_PC.zh-TW.md](Reskin_Setup_New_PC.zh-TW.md).

Checked on the working PC on 2026-10-18; the versions below are the ones that work.

## 0. Checklist

| # | What | Size | Section |
|---|---|---|---|
| 1 | NVIDIA GPU with 12 GB VRAM (tested: RTX 4070), recent NVIDIA driver | — | 1 |
| 2 | Git, Python 3.11 | — | 2 |
| 3 | ComfyUI (0.21.1) in `AIGamestyle/_comfyui/ComfyUI`, with its own `.venv` and PyTorch 2.6.0 + CUDA 12.4 | ~6 GB with PyTorch | 3 |
| 4 | Two custom nodes: **ComfyUI_IPAdapter_plus** and **ComfyUI-Inspyrenet-Rembg** | small | 4 |
| 5 | Six model files (table in section 5) | ~20 GB | 5 |
| 6 | Pillow and numpy for the Python that runs the tools | small | 2 |
| 7 | The check: `check_reskin_setup.py` says `ready` | — | 7 |

The reskin tools are plain Python scripts that send jobs to the ComfyUI server at `127.0.0.1:8188`; they need no
other install.

## 1. Hardware

- **GPU:** NVIDIA, CUDA-capable. Tested on an RTX 4070 (12 GB): about 12–14 s per picture at 1024 × 1024. Every job
  loads SDXL, ControlNet and IP-Adapter together, so plan on **12 GB VRAM**. An 8 GB card may work with ComfyUI's
  `--lowvram` flag (not tested). It does not run in a usable time on a CPU.
- **Disk:** about 30 GB free: the models (~20 GB), ComfyUI with PyTorch (~6 GB), and room for the output.
- **RAM:** 32 GB recommended (SDXL checkpoints are 7 GB each when loaded).

## 2. Software

| Program | Version that works | Get it |
|---|---|---|
| Git | any recent | https://git-scm.com/download/win |
| Python | **3.11** (3.11.9 here); tick "Add to PATH" | https://www.python.org/downloads/windows/ |
| NVIDIA driver | one that supports CUDA 12.4 or later | https://www.nvidia.com/drivers |

The tools also need **Pillow** and **numpy** in the Python that runs them:

```
python -m pip install pillow numpy
```

The ComfyUI `.venv` (section 3) already has both, so `AIGamestyle\_comfyui\ComfyUI\.venv\Scripts\python.exe` can run
the tools too.

## 3. ComfyUI

**One-click way:** `AIGamestyle\_pipeline\tools\setup_style_pipeline.bat` does all of this:

- clones ComfyUI into `AIGamestyle\_comfyui\ComfyUI`;
- creates its `.venv` and installs `requirements.txt`;
- adds ComfyUI-Manager;
- makes the model folders.

[_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) sections 0.5 and 2 describe it and what to
do when it fails. The first start (`start_comfyui_with_flags.bat`, section 6) installs PyTorch with CUDA 12.4 by
itself.

**By hand** (the same steps, from `AIGamestyle\`):

```
git clone https://github.com/comfyanonymous/ComfyUI.git _comfyui\ComfyUI
cd _comfyui\ComfyUI
python -m venv .venv
.venv\Scripts\python.exe -m pip install --upgrade pip
.venv\Scripts\python.exe -m pip install torch==2.6.0 torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

The working PC runs ComfyUI **0.21.1** (commit `26515acd`, 2026-05-13). A newer ComfyUI should work: the tools use
only standard nodes. If a newer one breaks a node, go back with `git checkout 26515acd`.

Check that the GPU is seen:

```
.venv\Scripts\python.exe -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.get_device_name(0))"
```

It should print `2.6.0+cu124 True NVIDIA ...`.

## 4. Custom nodes

Two nodes are not part of ComfyUI:

| Node | Used for | Repository | Version here |
|---|---|---|---|
| **ComfyUI_IPAdapter_plus** (`IPAdapterModelLoader`, `IPAdapterAdvanced`) | following a reference picture's look: Spine parts follow their object's restyled symbol, sprites follow the game's sprites | https://github.com/cubiq/ComfyUI_IPAdapter_plus | 2.0.0 |
| **ComfyUI-Inspyrenet-Rembg** (`InspyrenetRembg`) | removing the background of new sprites. Only `make_sprites.py` uses it; a restyle does not | https://github.com/john-mnz/ComfyUI-Inspyrenet-Rembg | 1.1.1 |

Install them with ComfyUI-Manager (start ComfyUI with `--enable-manager`, then Manager → Custom Nodes Manager, search
for the name, Install), or by hand:

```
cd AIGamestyle\_comfyui\ComfyUI\custom_nodes
git clone https://github.com/cubiq/ComfyUI_IPAdapter_plus.git comfyui_ipadapter_plus
git clone https://github.com/john-mnz/ComfyUI-Inspyrenet-Rembg.git comfyui-inspyrenet-rembg
..\.venv\Scripts\python.exe -m pip install -r comfyui-inspyrenet-rembg\requirements.txt
```

The Inspyrenet requirements install `transparent-background` (1.3.4 here). On its first use it downloads its model,
`ckpt_base.pth` (367 MB), to `%USERPROFILE%\.transparent-background\`. A PC without internet needs that file copied
there.

Everything else the tools use is a standard ComfyUI node: `CheckpointLoaderSimple`, `LoraLoader`, `Canny`,
`ControlNetLoader`, `SetUnionControlNetType`, `ControlNetApplyAdvanced`, `CLIPVisionLoader`, `KSampler` and the image
loaders and savers.

The other custom nodes in `custom_nodes` (comfyui_controlnet_aux, comfyui-florence2, comfyui-rmbg, comfyui-tensorops,
ComfyUI-Fast-Style-Transfer) belong to older experiments and are **not needed** for the reskin.

## 5. Models

The tools ask for these **exact file names**. Rename a download to the name in the first column.

| File name the tools use | ComfyUI folder | Size (bytes) | Download (Hugging Face) | Used by |
|---|---|---|---|---|
| `Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors` | `checkpoints` | 7 105 348 188 | `RunDiffusion/Juggernaut-XL-v9`, the same file name | restyle |
| `sd_xl_base_1.0.safetensors` | `checkpoints` | 6 938 078 334 | `stabilityai/stable-diffusion-xl-base-1.0`, the same file name | pixel sprites |
| `controlnet-union-sdxl-promax.safetensors` | `controlnet` | 2 513 342 408 | `xinsir/controlnet-union-sdxl-1.0`, file `diffusion_pytorch_model_promax.safetensors` (**rename**) | restyle, derived sprites |
| `ip-adapter-plus_sdxl_vit-h.safetensors` | `ipadapter` | 847 517 512 | `h94/IP-Adapter`, file `sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors` | restyle, sprites |
| `CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors` | `clip_vision` | 2 528 373 448 | `h94/IP-Adapter`, file `models/image_encoder/model.safetensors` (**rename**) | restyle, sprites |
| `pixel-art-xl.safetensors` | `loras` | 170 543 052 | `nerijs/pixel-art-xl`, the same file name | pixel sprites only |

A download link is `https://huggingface.co/<repository>/resolve/main/<file>`, for example
`https://huggingface.co/h94/IP-Adapter/resolve/main/sdxl_models/ip-adapter-plus_sdxl_vit-h.safetensors`. Compare
the size after downloading: a cut-off download fails only when a job runs. Restyling a game alone needs four of
them, about 13 GB: Juggernaut, ControlNet, IP-Adapter and CLIP-ViT-H. SDXL base and the pixel-art LoRA are for new
pixel sprites only.

The names are set at the top of the tools: `CHECKPOINT` and `CONTROLNET` in `_pipeline/tools/restyle_pilot.py` (the
IP-Adapter and CLIP names are in its `workflow()`), and `CHECKPOINT` and `LORA` in `_pipeline/tools/make_sprites.py`.
To use other files, change them there.

**Where the models go.** Either:

- **in ComfyUI's own folders:** `AIGamestyle\_comfyui\ComfyUI\models\<folder>\`; or
- **in a shared folder** (what the working PC does: `D:\Work\AI\models\<folder>\`), named in
  `AIGamestyle\_comfyui\ComfyUI\extra_model_paths.yaml`:

  ```yaml
  qoo_models:
    base_path: D:/Work/AI/models        # change to this PC's folder
    checkpoints: checkpoints
    controlnet: controlnet
    ipadapter: ipadapter
    clip_vision: clip_vision
    loras: loras
  ```

  The file in the repository lists every folder type and has the build machine's path
  (`C:/Users/client/Desktop/AI/models`) in a comment. Set `base_path` to this PC's folder. ComfyUI reads the file when
  it starts.

## 6. Start ComfyUI

Keep it running while the tools work:

```
AIGamestyle\_pipeline\tools\start_comfyui_with_flags.bat
```

It installs or repairs CUDA PyTorch if needed, then serves at http://127.0.0.1:8188. Press Enter at "Extra flags".
[_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) section 6 covers the CPU fallback and a
manual start. A manual start is:

```
cd AIGamestyle\_comfyui\ComfyUI
.venv\Scripts\python.exe main.py --listen 127.0.0.1 --port 8188
```

## 7. Check it

With ComfyUI running:

```
cd AIGamestyle\_pipeline\tools
python check_reskin_setup.py
```

It lists every node and model file the tools use as `OK` or `MISSING` and ends with `ready`. Then try one picture, a
real job of about 15 s:

```
python restyle_pilot.py <any game picture.png> ..\..\output\pilot --prompt "christmas theme, snow, red and gold" --denoise 0.6
```

The result is `output\pilot\<name>_d60.png`, with the same size and alpha as the input.

## 8. What else is already written, and where

| Document | What it has |
|---|---|
| [readme.txt](../readme.txt) | the four setup steps in short |
| [_pipeline/STEP_BY_STEP_HANDBOOK.md](../_pipeline/STEP_BY_STEP_HANDBOOK.md) | the venv, the one-click setup, the readiness check (`check_style_pipeline.py`), starting ComfyUI, CPU mode, common fixes. This is the older scan / classify / batch pipeline, but its setup steps are the same |
| [_pipeline/README.md](../_pipeline/README.md) | the older scan → classify → link → plan-style pipeline (not needed for the reskin tools) |
| [Doc/reskin_parameters.md](reskin_parameters.md) | parameters of the older App / batch reskin (`TestData/Reskin`), not of `restyle_game.py` |
| [Doc/Reskin_Game_Art.md](Reskin_Game_Art.md) | **how the reskin works and how to run it on a game** |

## 9. Common problems

| Symptom | Cause, fix |
|---|---|
| `check_reskin_setup.py`: ComfyUI not reachable | ComfyUI is not started, or is on another port. Start it (section 6). Port 8188 already in use means an old ComfyUI is still running; use that one or close it |
| a job fails with `value_not_in_list` naming a model | the file is missing or has another name. Check section 5's names and `extra_model_paths.yaml`'s `base_path`, then restart ComfyUI |
| `IPAdapterAdvanced` / `InspyrenetRembg` missing | the custom node is not installed, or its import failed. Look in ComfyUI's start log for `IMPORT FAILED` |
| `torch.cuda.is_available()` is False | a CPU PyTorch is installed. Run `start_comfyui_with_flags.bat` (it reinstalls the CUDA build), or the pip line in section 3 |
| out of memory | close other GPU programs. Start ComfyUI with `--lowvram` |
| the first sprite job hangs for minutes | Inspyrenet is downloading `ckpt_base.pth` (section 4) |
