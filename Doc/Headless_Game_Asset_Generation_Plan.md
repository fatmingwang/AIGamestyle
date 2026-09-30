# Plan: Headless Game-Asset Generation

Status: **implemented** (all 6 proposed commands: `text2image`, `atlas-extract`, `atlas-merge`,
`imagewithprompt`, `pose-from-prompt`, `nas-upload`). This document is kept as the design
rationale — see `Doc/Commands_Reference.md` §12 for the actual command reference/protocol, and
§4 below for where each command's code lives.

## 1. Why

Headless mode (`Doc/Commands_Reference.md` §12) already covers the Pipeline tab: scan an
existing asset folder, classify/link/plan it, then restyle every asset through ComfyUI
(`comfyui-batch`/`simple-batch`/`all`). That's a *reskin* flow — it needs a source image for
every asset already sitting on disk.

It does **not** cover generating a brand-new asset from just a text prompt, generating pose/
animation data, or packing results into a sprite atlas ready for a game engine — those all exist
today, but only as GUI-only tabs (Text2Image, Pose Generator, Atlas). The goal is to expose the
already-working parts of those tabs as headless commands, so a script can drive "prompt in,
game-ready asset out" the same way it already drives the reskin pipeline.

## 2. What's already there (confirmed by reading the widget source, not guessed)

Every GUI-only tab's "do the work" button already shells out to a plain, argparse-driven Python
script — the same shape as the 5 pipeline steps `PipelineCommands.cpp`/`PipelineEngine.cpp`
already wrap. That's what makes this cheap: no new subprocess-launching machinery, just new
`buildStepRequest`-style request builders and new command names in `HeadlessRunner.cpp`.

| Tab | Script(s) it calls | Input | Output |
|---|---|---|---|
| Text2Image | `TestData/Text2Image/run_text2image.py` | `--prompt`, optional `--width`/`--height`, `COMFYUI_SERVER` env | one PNG at `--output-dir/--output-name` |
| Image With Prompt / Symbol Group | `run_imagewithprompt.py`, `batch_reskin.py` | reference image + prompt, or a source/output folder pair | variant image(s) |
| Pose Generator | `generate_pose_from_prompt.py`, `generate_pose_sequence.py`, `generate_creature_animation.py` | prompt or pose JSON | pose JSON, rendered frames, or a sprite sheet |
| Atlas | *(no script — in-process C++ image math)* | a Spine `.atlas` + PNG | extracted/merged sprite PNGs |
| Resource Replace (NAS upload) | `robocopy` (native, no Python) | output folder + NAS target | files copied to NAS |

Two things are GUI-only *by nature*, not just "not wired up yet," and are out of scope here:
Group Builder's manual drag-and-drop grouping, and Resource Replace's registration-review form.

## 3. Proposed new headless commands

Same session-command shape as the existing ones (`{"command": "...", "args": {...}}`, an
NDJSON `job_start`/.../`job_end` reply), added to `HeadlessRunner.cpp`'s command dispatch:

| # | Command | Wraps | Key args | Notes |
|---|---|---|---|---|
| 1 | `text2image` | `run_text2image.py` | `prompt` (required), `width`, `height`, `output-name` | No source asset needed — the actual "generate a new game asset from a prompt" gap. |
| 2 | `atlas-extract` | in-process `AtlasWidget` logic | `atlas-path`, `output-dir` | Pulls sprites out of a Spine atlas as individual PNGs. |
| 3 | `atlas-merge` | in-process `AtlasWidget` logic | `atlas-path`, `sprites-dir`, `scale` | Packs edited/generated sprites back into an atlas — the "ready for the engine" step after generation. |
| 4 | `imagewithprompt` | `run_imagewithprompt.py` | `reference`, `prompt`, `output-dir` | Prompt-guided variant of one reference image (a single "asset with a twist," not a full batch). |
| 5 | `pose-from-prompt` | `generate_pose_from_prompt.py` | `prompt`, `subject-type`, `output` | |
| 6 | `nas-upload` | `robocopy` wrapper | `source`, `target`, `conflict` (`overwrite`\|`rename`\|`fail`) | Replaces the interactive delete/rename/cancel dialog with an explicit flag — needs a decision on the default (see §5). |

`#1`–`#3` are the ones that directly serve "generate game assets for a project": prompt → image
→ packed atlas, fully unattended. `#4`–`#6` round out the rest of what's already script-backed.

## 4. How each gets wired in (pattern, not new architecture)

For the script-backed ones (`text2image`, `imagewithprompt`, `pose-from-prompt`): a new
`PipelineEngine::build<X>Request(cfg, args)` function that returns the same
`{program, args:[...], workingDirectory}` shape `buildStepRequest()` already returns, executed
through the same `runStepRequestSync()` both the GUI and headless mode already share — this is
the identical pattern used for `comfyui-launch` (`PipelineEngine::buildComfyUiLaunchRequest`).

For the atlas commands: no subprocess at all. `AtlasWidget`'s parse/extract/merge logic (it used
to be three private methods plus the bodies of `onExtract()`/`onMerge()`) was lifted into a new
`App/src/AtlasOps.h/.cpp` — pure functions with no `QWidget`/`QMessageBox` dependency, taking the
GUI's two interactive choices (multi-page layout, save conflict) as plain enum arguments instead
of asking via a dialog. `AtlasWidget` now calls `AtlasOps::extractAtlas()`/`mergeAtlas()` after
resolving those two choices itself via `QMessageBox`; `HeadlessRunner.cpp`'s `atlas-extract`/
`atlas-merge` commands call the same two functions with the choices taken from `args` instead.
Verified byte-identical output to the pre-refactor logic on a synthetic test atlas.

For `nas-upload`: a straight `QProcess::startDetached`/synchronous `robocopy` call, gated by the
`conflict` arg instead of a `QMessageBox`.

## 5. How the open decisions were resolved

- **Scope** — all six were implemented in one pass, per the request.
- **Naming** — kept as proposed: `text2image`, `imagewithprompt`, `pose-from-prompt`,
  `atlas-extract`, `atlas-merge`, `nas-upload`.
- **`nas-upload`'s default conflict policy** — defaults to `"fail"` (report and stop), matching
  the GUI dialog's own default button (Cancel) rather than silently touching NAS content;
  `"overwrite"` and `"rename"` are opt-in via `args.conflict`. `atlas-merge`'s equivalent
  `args.conflict` for an already-existing output file follows the same default.

## 6. Explicitly out of scope

- Group Builder's manual drag-and-drop grouping (no script backs it — direct UI data editing).
- Resource Replace's NAS *registration* form (POSTs Game ID/Platform/etc. for human review before
  submitting) — could be scripted too if wanted, but is a separate, smaller follow-up from the
  upload itself.
