# Reskin System â€” How It Works & Parameter Reference

## How the Pipeline Works (Step by Step)

```
Source Image (PNG/JPG)
        â”‚
        â–¼
1. Upload source image to ComfyUI input folder
        â”‚
        â–¼
1b. Upload reference image to ComfyUI input folder  â† once per batch, auto from hero_reference
        â”‚
        â–¼
2. Upscale source to 1024Ã—1024  (SDXL works best at this resolution)
        â”‚
        â–¼
3. VAE Encode  (convert image to latent space)
        â”‚
        â–¼
4. IPAdapter  (inject reference image style into the diffusion model)
        â”‚
        â–¼
5. KSampler  (diffusion: positive prompt + negative prompt + seed)
        â”‚
        â–¼
6. VAE Decode  (convert latent back to pixels)
        â”‚
        â–¼
7. Downscale back to original source size
        â”‚
        â–¼
8. Save â†’ output/<batch_id>/<original_filename>.png
```

The key idea: the source image provides the **structure** (shape, composition, silhouette),
the reference image provides the **style** (colours, art direction), and the prompts guide
**what the content should look like**.

---

## Parameter Files

### 1. Style Profile â€” `_pipeline/style_profiles/default_parameters.json`

```json
{
  "checkpoint_model":    "put_your_model_name_here.safetensors",
  "hero_reference":      "_pipeline/style_profiles/references/hero_reference.png",
  "ip_adapter_strength": 0.75,
  "sampler": {
    "steps":     30,
    "cfg_scale": 6.5
  }
}
```

| Parameter | Read by batch runner? | What it controls |
|---|---|---|
| `checkpoint_model` | No | Ignored by batch runner. Model is set in the workflow JSON (see Section 4). |
| `hero_reference` | **Yes** | Default reference image. Auto-uploaded to ComfyUI at batch start. Can be overridden by the App UI or `--hero-reference` CLI flag. |
| `ip_adapter_strength` | **Yes** | How much the reference image overrides the source structure. Higher = more stylised, less faithful to original shape. Range: 0.4â€“0.9 |
| `sampler.steps` | **Yes** | Diffusion iterations. More = sharper output, slower. Range: 20â€“40 |
| `sampler.cfg_scale` | **Yes** | How literally the model follows text prompts. Higher = more prompt-driven. Range: 5â€“9 |

**`hero_reference` priority (highest to lowest):**

1. **App UI â€” Hero Reference field** â€” absolute path selected via Browse button
2. **`--hero-reference` CLI flag** â€” for running the script directly from terminal
3. **`hero_reference` in style profile JSON** â€” the default fallback
4. **Workflow JSON hardcoded value** â€” used only if all above are empty or file not found

Path can be absolute (`D:/refs/my_ref.png`) or relative to the project root
(`_pipeline/style_profiles/references/my_ref.png`).
If the file does not exist, a warning is printed and the workflow JSON default is used.

> **`checkpoint_model` is still ignored by the batch runner.** The SD model must be set
> directly in the workflow JSON (node `1.Load Checkpoint` â†’ `ckpt_name`). See Section 4.

---

### 2. Per-Category Prompts â€” `TestData/Reskin/reskin_categories.json`

Controls the **text description** sent to the diffusion model. Each asset group is
matched to a category by its `character_id` in the style plan.

```json
{
  "symbols": {
    "positive": "cartoon game symbol icon, same silhouette and shape, ...",
    "negative": "blurry, low quality, distorted shape, ..."
  },
  "panel":        { "positive": "...", "negative": "..." },
  "autospin":     { "positive": "...", "negative": "..." },
  "loopshowscore":{ "positive": "...", "negative": "..." },
  "background":   { "positive": "...", "negative": "..." },
  "default":      { "positive": "...", "negative": "..." }
}
```

**Matching logic** (`run_comfyui_batch.py â†’ resolve_category`):
1. Exact match on `character_id`
2. Substring match (e.g. `character_id = "autospin_btn"` â†’ matches key `"autospin"`)
3. Falls back to `"default"` if nothing matches

**What to edit:**
- `positive` â€” describe the desired look. Always include phrases like "same silhouette and shape" to stop the model changing the composition.
- `negative` â€” list things to suppress. Common additions: `"text"`, `"letters"`, `"extra fingers"`.

---

### 3. Per-Asset Seed â€” `_pipeline/batches/<batch_id>/style_plan.json`

Each asset group has a fixed `seed` that makes results reproducible.

```json
{
  "source_group": "h2",
  "character_id": "symbols",
  "seed": 3206297175,
  "static_assets": ["textures/symbols/H2.png"]
}
```

| Field | Effect |
|---|---|
| `seed` | Change to get a different variation. Same seed + same settings = identical output every run. |
| `character_id` | Determines which category (and therefore which prompts) this group uses. |

**To regenerate a single asset differently:** change its `seed` to any other integer.

---

### 4. ComfyUI Workflow â€” `TestData/Reskin/ReSkinTestWorkFlow.json`

The raw ComfyUI node graph. The **SD model** must be set here â€” it is not read from the
style profile.

#### Fields that must be correct for the batch to run

| Node (title) | Field | Current value | What happens if wrong |
|---|---|---|---|
| `1.Load Checkpoint` | `ckpt_name` | `"Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors"` | ComfyUI fails at runtime â€” `execution_error`, batch aborts the asset. |
| `5.Load Image B(Reference)` | `image` | `"W1.png"` | Used only when no `hero_reference` is configured. If the file is not in ComfyUI's input folder â†’ `execution_error`. |

To change the **model**: edit `ckpt_name` in node `1.Load Checkpoint`. The filename must
exactly match a model installed in ComfyUI's `models/checkpoints/` folder.

The **reference image** in this node is now the last-resort fallback only â€” normally the
batch runner uploads and sets it automatically from `hero_reference` (see Section 1).

#### Other tunable fields in the workflow

| Node (title) | Parameters of interest |
|---|---|
| `9.KSampler` | `sampler_name` (`dpmpp_2m_sde`), `scheduler` (`karras`), `denoise` (0.65) |
| `8.IPAdapter Advanced` | `weight_type` (`linear`), `start_at`, `end_at`, `embeds_scaling` |
| `2b.Upscale Image A to 1024x1024` | `upscale_method` (`lanczos`) |
| `11b.Downscale Output to Source Size` | `upscale_method` for the final downscale |

**`denoise` in KSampler** is the most impactful hidden knob:
- Low (0.4â€“0.6): output stays close to the source â€” good for UI elements where layout must be preserved.
- High (0.7â€“0.9): output diverges more â€” good for backgrounds and characters where a full repaint is wanted.

> `denoise` is currently hardcoded in the workflow JSON. To expose it as a configurable
> parameter, add it to the style profile and apply it in `run_comfyui_batch.py â†’ run_one_asset`
> (same pattern as `cfg` and `steps`).

---

### 5. App UI Runtime Overrides

Settings entered in the app before launching. These take priority over the style profile.

| UI Field | Config field (`AppConfig`) | Effect |
|---|---|---|
| Hero Reference | `heroReference` | Path to reference image. Overrides `hero_reference` in style profile. Leave empty to use the profile default. |
| IPAdapter Wt | `ipAdapterWeight` | Overrides `ip_adapter_strength` in profile |
| Steps | `steps` | Overrides `sampler.steps` in profile |
| Denoise | `denoise` | Overrides `denoise` in workflow KSampler |
| Style Profile | `styleProfileId` | Selects which `.json` profile to load |
| Batch ID | `batchId` | Selects which `style_plan.json` to use |

**Hero Reference field behaviour:**
- Click **Browse...** to pick any image file (PNG, JPG, WEBP) from your local machine
- The file is uploaded to ComfyUI automatically when the batch starts â€” no manual browser upload needed
- Leave the field **empty** to fall back to `hero_reference` in the style profile JSON

---

## Where Each Parameter Actually Lives

| What you want to change | Where to edit |
|---|---|
| SD model (checkpoint) | Workflow JSON â†’ node `1.Load Checkpoint` â†’ `ckpt_name` |
| Reference image (default for all batches) | Style profile â†’ `hero_reference` |
| Reference image (this session only) | App UI â†’ Hero Reference field (Browse...) |
| IPAdapter strength | Style profile â†’ `ip_adapter_strength`  /  App UI |
| Steps / CFG | Style profile â†’ `sampler.steps` / `sampler.cfg_scale`  /  App UI |
| Denoise strength | Workflow JSON â†’ node `9.KSampler` â†’ `denoise`  /  App UI |
| Text prompts per asset type | `reskin_categories.json` â†’ `positive` / `negative` |
| Variation for one asset | `style_plan.json` â†’ that group's `seed` |
| Which category an asset uses | `style_plan.json` â†’ that group's `character_id` |

---

## Quick Tuning Guide

| Goal | What to change |
|---|---|
| Different art style | Workflow JSON: swap `ckpt_name` to a different SD model |
| Different reference image (permanent) | Style profile: set `hero_reference` to your image path |
| Different reference image (one session) | App UI: Browse to your image in the Hero Reference field |
| More faithful to original shape | Lower `ip_adapter_strength` (try 0.5) |
| More stylised / less source influence | Raise `ip_adapter_strength` (try 0.85) |
| Better quality / sharper details | Raise `steps` (try 35â€“40) |
| Output too literal / prompt-driven | Lower `cfg_scale` (try 5.0) |
| Output ignores prompts | Raise `cfg_scale` (try 8.0) |
| Full repaint (background, character) | Raise `denoise` in workflow JSON (0.75â€“0.90) |
| Preserve layout (UI panels, buttons) | Lower `denoise` (0.45â€“0.65) |
| Get a different variation of one asset | Change its `seed` in `style_plan.json` |
| Change look of all "symbols" assets | Edit `symbols.positive` in `reskin_categories.json` |
