# SkinBuilder Step-by-Step Handbook

This guide is written to be followed in order.

---

## 0) Open terminal at project root

```powershell
cd "<path-to-your-clone>"
```

Replace `<path-to-your-clone>` with wherever you cloned this repository (e.g.
`d:\Work\GamestyleAI`). Every command in this guide below is relative to that folder, so stay in
it (or re-`cd` back to it) unless a step says otherwise.

If this fails, confirm the folder exists in File Explorer first.

---

## 0.5) New computer setup: create Python virtual environment (`.venv`)

Use this section when cloning this repository on another computer.

### 0.5.1 Fast way (recommended): run one-click BAT

```powershell
.\_pipeline\tools\create_venv.bat
```

This creates `.venv`, upgrades `pip`, and prints next steps.

### 0.5.2 Activate `.venv` (PowerShell)

```powershell
.\.venv\Scripts\Activate.ps1
```

### 0.5.3 Verify active interpreter is from `.venv`

```powershell
python -c "import sys; print(sys.executable)"
```

Expected path should include `.venv\Scripts\python.exe`.

### 0.5.4 Manual fallback (if BAT fails)

Check Python is installed:

```powershell
python --version
```

If command is not found, install Python 3.11+ and reopen terminal.

Create `.venv` manually:

```powershell
python -m venv .venv
```

If blocked by execution policy, run once in PowerShell:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Then activate again.

### 0.5.5 Install base dependencies (optional but recommended)

```powershell
python -m pip install --upgrade pip
```

If you have a project `requirements.txt`, install it:

```powershell
python -m pip install -r requirements.txt
```

### 0.5.6 Deactivate when done

```powershell
deactivate
```

---

## 1) Scan & Classify assets

> Important: `scan` now requires a target folder argument and can scan folders on any disk (`D:`, `E:`, etc.).

### 1.0 Pick target folder

Examples:

- In-project folder (relative to the project root from step 0): `.\game\textures`
- External disk folder: `E:\GameArt\MySlot\textures`

### 1.1 Run scan (required target folder)

```powershell
python ".\_pipeline\tools\asset_pipeline.py" scan ".\game\textures"
```

Or scan external disk directly:

```powershell
python ".\_pipeline\tools\asset_pipeline.py" scan "E:\GameArt\MySlot\textures"
```

Expected output includes path to:

- `output/<target_key>/assets_inventory.json`
- and target metadata (`target_key`, `target_folder`)

### 1.2 Run classify

```powershell
python ".\_pipeline\tools\asset_pipeline.py" classify
```

Expected output includes path to:

- `output/<target_key>/assets_inventory.json` (status updated to `classified` in-place)

`classify` automatically uses the latest scanned target from:

- `_pipeline/reports/classify_report.json`

please do
python -m pip install --upgrade pip
python -m pip install sentence-transformers pillow

py asset_pipeline.py link --target-folder ".\assets\game"

py asset_pipeline.py visual-link --threshold 0.80 --target-folder .\assets\game

py asset_pipeline.py render-visual-previews --target-folder ".\assets\game"

### 1.3 Verify file exists

```powershell
Get-Content ".\_pipeline\reports\textures\classify_report.json" -TotalCount 40
```

From the JSON above, copy `manifest` and verify that file:

```powershell
Get-Item ".\output\textures\assets_inventory.json"
```

Optional quick preview:

```powershell
Get-Content ".\output\textures\assets_inventory.json" -TotalCount 40
```

Optional: override target explicitly instead of using active target:

```powershell
python ".\_pipeline\tools\asset_pipeline.py" classify --target-folder "E:\GameArt\MySlot\textures"
```

---

## 2) Install style-generation stack (ComfyUI + checker tools)

### 2.1 Run one-click setup

```powershell
.\_pipeline\tools\setup_style_pipeline.bat
```

What this setup does:

- checks required commands (`git`, `python`, `powershell`)
- clones/updates ComfyUI to `_comfyui/ComfyUI`
- creates virtual environment
- installs Python dependencies
- installs ComfyUI-Manager (if possible)
- creates model directories
- runs readiness checker automatically

### 2.2 If script says FAIL

Read the first `[FAIL]` line and fix it first, then re-run:

```powershell
.\_pipeline\tools\setup_style_pipeline.bat
```

---

## 3) Manual readiness check

Run checker any time (from `_pipeline\tools`, pass `--project-root ../../` to point back at the
project root; or from the project root itself, pass `--project-root "."` as below):

```powershell
python ".\_pipeline\tools\check_style_pipeline.py" --project-root "."
```

### READY criteria

You are ready when:

- no `[FAIL]` lines appear
- ComfyUI exists
- ComfyUI venv exists
- at least one model exists in `_comfyui/ComfyUI/models/checkpoints`

Recommended (not always mandatory):

- ControlNet model exists
- IP-Adapter model exists
- CLIP Vision model exists

---

## 4) Prepare workflow template for API execution

### 4.1 Create template file

```powershell
Copy-Item ".\_pipeline\tools\workflow_api_template.example.json" ".\_pipeline\tools\workflow_api_template.json"
```

### 4.2 Replace placeholder workflow

- Open `.\_pipeline\tools\workflow_api_template.json`
- Replace content with your exported ComfyUI API workflow JSON
- Keep placeholders if your runner uses them:
  - `__INPUT_IMAGE__`
  - `__STYLE_REFERENCE__`
  - `__OUTPUT_PREFIX__`

---

## 5) Check style runner tool is available

```powershell
python ".\_pipeline\tools\comfy_style_runner.py" --help
```

If help text appears, tool is installed correctly.

---

## 6) Start ComfyUI (before generation)

### 6.1 One-click start (recommended)

```powershell
.\_pipeline\tools\start_comfyui.bat
```

ComfyUI will start and listen on `http://127.0.0.1:8188`.

### 6.2 CPU mode fallback (if you see CUDA / torch errors)

If startup fails with `Torch not compiled with CUDA enabled`, use CPU mode instead:

```powershell
.\_pipeline\tools\start_comfyui_cpu.bat
```

This is slower, but it works on machines without CUDA-enabled PyTorch.

### 6.3 Manual start (if BAT fails)

```powershell
cd ".\_comfyui\ComfyUI"
.\.venv\Scripts\Activate.ps1
python main.py --listen 127.0.0.1 --port 8188
or use browser http://127.0.0.1:8188
```

### 6.4 Verify ComfyUI is running

In a new terminal, re-run checker:

```powershell
python ".\_pipeline\tools\check_style_pipeline.py" --project-root "."
```

You should see no connection errors if ComfyUI is running.

---

## 7) Run a single style test

in root folder

python TestData/Reskin/reskin_test.py

python TestData/Text2Image/gsl_start_1.py

python TestData/UpScale/UpScale.py

---

## 8) Run batch test from visual match groups

Make sure visual groups exist first (`visual_match_arrays.json`):

```powershell
python ".\_pipeline\tools\asset_pipeline.py" visual-link --threshold 0.80
```

`visual-link` also auto-uses active target from classify report.
If needed, force a specific target:

```powershell
python ".\_pipeline\tools\asset_pipeline.py" visual-link --threshold 0.80 --target-folder "E:\GameArt\MySlot\textures"
```

Then run batch:

```powershell
python ".\_pipeline\tools\comfy_style_runner.py" batch --mode first-only --output-prefix "style_test_batch"
```

Batch report output:

- `_pipeline/reports/style_generation_report.json`

Per-target pipeline outputs are grouped here:

- all generated files: `output/<target_key>/...`
  - groups: `output/<target_key>/groups/...`
  - batches: `output/<target_key>/batches/<batch_id>/...`
- shared state: `_pipeline/reports/active_target.json`

---

## 9) Common quick fixes

- **`[FAIL] ComfyUI missing`**
  - Run `setup_style_pipeline.bat`
- **`[FAIL] No checkpoint model found`**
  - Put model file (`.safetensors` or `.ckpt`) into:
    - `_comfyui/ComfyUI/models/checkpoints`
- **`[FAIL] Checkpoint name is still a placeholder`**
  - Open `_pipeline/style_profiles/default_parameters.json`
  - Replace `put_your_model_name_here.safetensors` with the real model filename
  - Or run `comfy_style_runner.py` with `--checkpoint <your_model>.safetensors`
- **`Cannot reach ComfyUI at http://127.0.0.1:8188`**
  - Start ComfyUI (`python main.py --listen 127.0.0.1 --port 8188`)
- **`workflow_api_template.json missing`**
  - Copy from `workflow_api_template.example.json`

---

## 10) Recommended run order summary

```powershell
cd "<path-to-your-clone>"
python ".\_pipeline\tools\asset_pipeline.py" scan ".\game\textures"
python ".\_pipeline\tools\asset_pipeline.py" classify
python ".\_pipeline\tools\asset_pipeline.py" link
.\_pipeline\tools\setup_style_pipeline.bat
python ".\_pipeline\tools\check_style_pipeline.py" --project-root "."
```

When checker has no `[FAIL]`, proceed to ComfyUI style generation tests.
