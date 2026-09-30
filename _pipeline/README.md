# SkinBuilder Asset Pipeline

Scans game assets, categorises them, links related assets by group, then generates a style plan for batch AI reskinning.

## Pipeline Steps

```
scan → classify → link → plan-style
```

Each step reads the previous step's output and writes back to the same target folder.

---

### Step 1 — scan

Scans a target folder and builds the initial asset inventory.

- Skips non-visual files: `.meta`, `.pac`, `.fnt`, `.prefab`, `.m4a`, `.mp3`, `.wav`, `.ogg`, `.aac`, `.flac`
- Assigns a stable `asset_id` (SHA-1 of file path, first 12 chars)

**Output:** `output/<target_key>/step1_assets_inventory.json`, `step1_scan_report.json`

---

### Step 2 — classify

Classifies each asset by location and extension, then updates status to `classified`.

| Folder | Extension | Type |
|--------|-----------|------|
| `textures/` | `.png .jpg .jpeg .webp` | `static` |
| `spine/` | `.png .jpg .jpeg .webp` | `spine_attachment` |
| `spine/` | `.json .atlas .skel` | `spine_source` |

**Output:** `output/<target_key>/step1_assets_inventory.json` (updated in-place), `step2_classify_report.json`

---

### Step 3 — link

Groups assets by `source_group` so static and Spine variants of the same character are linked together.

- Aligns `character_id` across variants in the same group
- Writes a group index used by the style planner

**Output:**
- `output/<target_key>/step3_assets_linked.json`
- `output/<target_key>/step3_group_index.json`
- `output/<target_key>/step3_link_report.json`

---

### Step 4 — plan-style

Assigns a style profile and seed to each group. All assets in the same `source_group` get the **same seed and profile** to ensure visual consistency across static and Spine variants.

`--batch-id` is **optional** — a timestamped ID is auto-generated when omitted. Provide one explicitly to embed a predictable, human-readable identifier in the output JSON (e.g. `10087batch_demo`).

**Output:**
- `output/<target_key>/step4_assets_style_planned.json` — updated asset manifest with style assignments
- `output/<target_key>/step4_style_plan.json` — per-group render plan consumed by the ComfyUI step
- `output/<target_key>/step4_style_plan_report.json`

---

## Folder Layout

```
<output_folder>/               ← configured via --output-folder (e.g. output/10087/)
  <target_key>/                ← derived from asset root folder name (e.g. game/)
    active_target.json
    step1_assets_inventory.json
    step1_scan_report.json
    step2_classify_report.json
    step3_assets_linked.json
    step3_group_index.json
    step3_link_report.json
    step3b_assets_visual_linked.json   ← optional (visual-link step)
    step3b_visual_match_arrays.json    ← optional
    step3b_visual_link_report.json     ← optional
    step3b_visual_previews/            ← optional
    step4_assets_style_planned.json
    step4_style_plan.json
    step4_style_plan_report.json
_pipeline/
  reports/
    active_target.json         ← shared state used by classify/link/plan-style to find active target
  manifests/
    asset_schema.json
  style_profiles/              ← e.g. default_parameters.json
  tools/                       ← asset_pipeline.py, run_pipeline.py, smoke_test.py
  pipeline_config.json
```

---

## Key Metadata Fields

| Field | Purpose |
|-------|---------|
| `asset_id` | Stable ID derived from file path |
| `asset_type` | `static`, `spine_attachment`, or `spine_source` |
| `source_group` | Consistency anchor — links static + Spine variants |
| `character_id` | Character this asset belongs to |
| `style_profile_id` | Which style profile to apply |
| `status` | Pipeline stage: `discovered` → `classified` → `linked` → `planned` |

---

## How to Run

```powershell
# Run each step individually
python asset_pipeline.py scan "D:\path\to\assets\game"
python asset_pipeline.py classify
python asset_pipeline.py link
python asset_pipeline.py plan-style --style-profile-id default_parameters
python asset_pipeline.py plan-style --style-profile-id default_parameters --batch-id batch_demo  # explicit name

# Or run all steps at once
python asset_pipeline.py all --style-profile-id default_parameters

# Smoke test
python smoke_test.py
```

> After `scan`, subsequent commands automatically use the last scanned target.  
> Pass `--target-folder` explicitly to override.

---

## Next Step

Feed `output/<target_key>/step4_style_plan.json` into the ComfyUI execution backend, which renders the reskinned assets and writes QA results back into the manifest.
