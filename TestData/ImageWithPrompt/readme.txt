ImageWithPrompt — ComfyUI Template
===================================

PURPOSE
-------
Generate a NEW image using a STYLE REFERENCE + TEXT PROMPT.
Unlike the main reskin pipeline (which re-styles an existing source image),
this workflow starts from an empty latent. The reference image guides only the
visual art style (colours, textures, mood); the text prompt drives the content.

Use this when you want to create a brand-new game asset that matches a
reference art style without copying or recoloring the reference itself.


FILES IN THIS FOLDER
--------------------
  ImageWithPromptWorkFlow.json  — ComfyUI API-format workflow (load directly in ComfyUI)
  categories.json               — Preset prompts for common game asset types
  run_imagewithprompt.py        — CLI helper that patches the workflow and runs it
  readme.txt                    — This file


WORKFLOW NODE OVERVIEW
-----------------------
  1. Load Checkpoint             — Loads the base diffusion model (SDXL Juggernaut by default)
  2. Load Reference Image        — Input: your style reference PNG
  3. CLIP Text Encode Positive   — Input: positive prompt describing the new content
  4. CLIP Text Encode Negative   — Input: negative prompt (what to avoid)
  5. Load CLIP Vision            — Loads the vision encoder for IPAdapter
  6. IPAdapter Model Loader      — Loads the IPAdapter Plus (SDXL) model
  7. IPAdapter Advanced          — Extracts style embedding from the reference
  8. Empty Latent Image          — Creates a blank 1024×1024 latent (full generation)
  9. KSampler                    — Runs diffusion: seed=42, steps=20, cfg=7, denoise=1.0
 10. VAE Decode                  — Decodes latent to pixel image
 11. Save Image                  — Saves output to ComfyUI's output folder


PREREQUISITES
-------------
1. ComfyUI running at http://127.0.0.1:8188
   Set the COMFYUI_SERVER environment variable to override the address.

2. ComfyUI-IPAdapter_plus extension installed:
   https://github.com/cubiq/ComfyUI_IPAdapter_plus

3. Model files (place in ComfyUI's model directories):
   - checkpoints/  Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors
                   (or edit node 1 ckpt_name to your preferred SDXL model)
   - clip_vision/  CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors
   - ipadapter/    ip-adapter-plus_sdxl_vit-h.safetensors


QUICK START — run_imagewithprompt.py
-------------------------------------
From the TestData/ImageWithPrompt directory:

  # Use a preset category (recommended for standard game assets):
  python run_imagewithprompt.py --reference myref.png --category symbol_high --output-dir ./out

  # Provide your own prompt (overrides --category):
  python run_imagewithprompt.py --reference myref.png \
      --positive "gold dragon coin, cartoon game icon, glowing, vibrant" \
      --negative "blurry, realistic, low quality" \
      --output-dir ./out

  # Full control over generation parameters:
  python run_imagewithprompt.py --reference ref.png --category character \
      --width 512 --height 512 --seed 1234 --steps 25 --cfg 7.5 \
      --ip-weight 0.8 --output-name hero_portrait --output-dir ./out


AVAILABLE CATEGORIES (categories.json)
---------------------------------------
  symbol_high     High-value symbols (jewels, crowns, gold)
  symbol_low      Card letter symbols (A K Q J 10)
  symbol_wild     WILD symbols (star, lightning, glowing aura)
  symbol_scatter  SCATTER/BONUS badges
  background      Wide landscape backgrounds
  character       Character portrait icons
  panel           UI panel elements
  button          UI button elements
  icon            General game icons
  default         Generic game asset (fallback)

Add or edit categories in categories.json. The --category flag matches
exact keys or performs a substring match (e.g. "wild" matches "symbol_wild").


PARAMETER REFERENCE
--------------------
  --reference         Path to the style reference image (REQUIRED)
  --output-dir        Output directory (default: ./output)
  --output-name       Output filename stem, no extension (default: reference filename stem)
  --category          Preset prompt category (see list above)
  --positive-prompt   Positive prompt text (overrides --category)
  --negative-prompt   Negative prompt text (overrides --category)
  --width             Output width in pixels (default: 1024)
  --height            Output height in pixels (default: 1024)
  --seed              Sampler seed for reproducibility (default: 42)
  --steps             Sampler steps (default: 20)
  --cfg               CFG scale — higher = stricter prompt adherence (default: 7.0)
  --denoise           Denoise strength: 1.0 = full generation from noise (default: 1.0)
  --ip-weight         IPAdapter style weight 0.0–1.0 (default: 0.7)


TIPS
----
- ip-weight 0.5–0.7  : balanced style influence (recommended starting point)
- ip-weight 0.8–1.0  : strong style transfer, may override prompt content
- ip-weight 0.2–0.4  : subtle style hint, prompt content dominates
- denoise 1.0         : full generation (correct for this workflow — do not lower)
- Use a consistent --seed to compare different references or categories
- For transparent-background game icons, add "white background" to negative prompt
  and cut out the background in post, or use a model trained on transparent images


USING THE WORKFLOW DIRECTLY IN COMFYUI UI
------------------------------------------
1. Open ComfyUI in your browser
2. Drag-and-drop ImageWithPromptWorkFlow.json onto the node canvas
3. Double-click node 2 to upload your reference image
4. Edit nodes 3 and 4 to set your positive/negative prompts
5. Adjust node 8 (Empty Latent Image) for output size
6. Adjust node 7 (IPAdapter Advanced) weight for style strength
7. Click Queue Prompt


DIFFERENCE FROM MAIN RESKIN PIPELINE
--------------------------------------
  Reskin pipeline:     Source image -> VAEEncode -> KSampler (denoise < 1.0) -> output
                       Keeps the structure/layout of the source, changes only the style.

  ImageWithPrompt:     Empty latent -> KSampler (denoise = 1.0) -> output
                       Generates completely new content; style guided only by reference.

Use the reskin pipeline when re-theming existing assets (layout preserved).
Use ImageWithPrompt when creating new assets from scratch in a matching art style.
