# GameStyleAI — Command Reference

> 繁體中文版：[Commands_Reference.zh-TW.md](Commands_Reference.zh-TW.md)

This document inventories every distinct command the GameStyleAI desktop app (`App/`) can
execute, what each one actually runs (subprocess / HTTP call / in-process logic), what must be
true before it can run, and what order things are meant to happen in. It also documents the new
**headless mode** (`GameStyleAI --headless ...`) that runs the core asset pipeline without the Qt
GUI.

Sources: `App/src/*.cpp/.h`, and the Python scripts they invoke under `_pipeline/tools/` and
`TestData/`. File:line references point at the code as of this audit (2026-09-18); re-check line
numbers if the referenced files have since changed.

## Contents

1. [Architecture in one paragraph](#architecture-in-one-paragraph)
2. [Command count](#command-count)
3. [Pipeline tab (the core 5-step pipeline)](#1-pipeline-tab)
4. [Symbol Groups tab](#2-symbol-groups-tab)
5. [Group Builder tab](#3-group-builder-tab)
6. [Image With Prompt tab](#4-image-with-prompt-tab)
7. [Text2Image tab](#5-text2image-tab)
8. [Pose Generator tab](#6-pose-generator-tab)
9. [Atlas tab](#7-atlas-tab)
10. [Resource Replace tab](#8-resource-replace-tab)
11. [Ordering / prerequisite quick reference](#9-ordering--prerequisite-quick-reference)
12. [Known gaps and bugs found during this audit](#10-known-gaps-and-bugs-found-during-this-audit)
13. [Scripts that exist but aren't wired into the GUI](#11-scripts-that-exist-but-arent-wired-into-the-gui)
14. [Headless mode](#12-headless-mode)

---

## Architecture in one paragraph

The GUI itself does almost no work. Every button either (a) launches a Python script as a
`QProcess` with a specific argv and environment, (b) makes a direct HTTP request
(`ResourceReplaceWidget` only), or (c) does pure local file/image manipulation in C++
(`AtlasWidget`, JSON import/export, list editing). The Python scripts are the ones that actually
talk to ComfyUI (upload images, queue prompts over its REST/WebSocket API) or to LLM backends
(Ollama / OpenAI / Anthropic / a local llama.cpp server). `MainWindow` owns three pieces of shared
config that most widgets need — `pythonExe`, `projectRoot`, `comfyuiServer` — and pushes them into
each widget via `setRunConfig()`.

## Command count

| Tab / module | Distinct user-triggered commands |
|---|---|
| Pipeline (MainWindow core) | 9 (Scan, Classify, Link, Plan Style, ComfyUI Batch, Simple Batch, Launch ComfyUI, Run All, Run Step) + Restart/Stop as control actions |
| Symbol Groups | 4 (Import/Export JSON, Apply→Pipeline, Run Item, Run Checked) |
| Group Builder | 7 (Scan, New Group from Selected, Auto-Group by Prefix, Auto-Group by Vision, Check backend, Export JSON, Open in Symbol Groups) |
| Image With Prompt | 5 (Import/Export JSON, Run Item, Run All, Stop) |
| Text2Image | 8 (Import/Export JSON, Run Item, Run All, Parse Table→Generate Prompts, Analyze Style Image, Test backend, Wake Ollama, Wake LLaMA) |
| Pose Generator | 5 (Test backend, Generate Pose Data, Generate Pictures, Generate Animation, Load/Save JSON) |
| Atlas | 2 (Extract, Merge Back) |
| Resource Replace | 4 (Info, Start Replace, Commit && Push, Upload Output to NAS) |
| **Total distinct commands** | **~44**, backed by **17 different Python scripts / inline probes** and **3 HTTP endpoints** |

("Distinct commands" counts user-facing actions that do something beyond pure local list/text
editing; pure UI actions like Browse/Clear/Help are omitted from the count but mentioned in each
section for completeness.)

---

## 1. Pipeline tab

The core 5-step pipeline, `App/src/MainWindow.cpp` + `App/src/PipelineCommands.cpp`.

```
Scan  →  Classify  →  Link  →  Plan Style  →  ComfyUI Batch
(writes active_target.json     (writes step4_style_plan.json,
 + step1_assets_inventory.json)  keyed by scanned_asset_root)
```

`Simple Batch` and `Group Run` (§2) are parallel, independent paths that skip straight to
ComfyUI without the scan/classify/link/plan-style bookkeeping.

| # | Command | Runs | Prerequisite gate |
|---|---|---|---|
| 1 | **Scan** | `python -u _pipeline/tools/asset_pipeline.py --output-folder <out> scan <assetRoot>` | Output-folder-not-empty confirmation (`confirmOutputFolder`, MainWindow.cpp:1222) |
| 2 | **Classify** | `... classify [--target-folder]` (defaults to last-scanned target via `_pipeline/reports/active_target.json`) | Scan must have run — else `ValueError("No active target folder found...")` (asset_pipeline.py:222) |
| 3 | **Link** | `... link [--target-folder]` | needs `step1_assets_inventory.json` from Classify |
| 4 | **Plan Style** | `... plan-style --style-profile-id <id> --batch-id <id>` | needs `step3_assets_linked.json` from Link |
| 5 | **ComfyUI Batch** | `python -u _pipeline/tools/run_comfyui_batch.py --batch-id <id> --asset-root <root> [--output-dir] [--hero-reference] [--positive-prompt] [--negative-prompt] [--include-spine]` | (a) output-folder gate, (b) **style-plan freshness gate** (`checkStylePlanFresh`, MainWindow.cpp:1270): requires `step4_style_plan.json` from Plan Style, whose recorded `scanned_asset_root` must match the current Asset Root — stale plan → Regenerate/Continue/Cancel dialog. (c) ComfyUI server must be reachable — **not checked by the app**, only surfaces as a script-side connection error. |
| 6 | **Simple Batch** ("Batch Reskin") | `python -u TestData/Reskin/batch_reskin.py <assetRoot> <outputFolder>` | output-folder gate only; bypasses scan/classify/link/plan-style entirely; talks to ComfyUI directly; random (not deterministic) seed per image |
| 7 | **Launch ComfyUI** | TCP probe (`python -c "socket connect_ex(...)"`) → if unreachable, Windows: `cmd.exe /c _pipeline/tools/start_comfyui_with_flags.bat --enable-manager --listen 0.0.0.0` in a new console window. Non-Windows: not automated — logs instructions to start it manually. | `.bat` file must exist; ComfyUI + its venv must already be installed under `_comfyui/ComfyUI/.venv` |
| 8 | **Run All** | Chains steps 1→5 automatically, stopping on first non-zero exit (or step 6 alone if Mode = Simple Batch) | — |
| 9 | **Run Step** | Runs only the selected step, no auto-chaining | — |
| — | Restart | Resets the step list to step 0; launches nothing | — |
| — | Stop | Kills the running step's process | — |

**Env vars for every step:** `COMFYUI_SERVER=<comfyuiServer>`, `PYTHONIOENCODING=utf-8`, working
directory = project root. Every script is invoked as `python -u <script> <args>` (unbuffered, so
stdout streams live).

**Structured progress protocol:** `asset_pipeline.py` and `run_comfyui_batch.py` print
`[GS:STEP_START step=...]`, `[GS:PROGRESS step=... current=N total=M]`,
`[GS:STEP_DONE step=...]`, `[GS:STEP_DETAIL step=... json=...]`, `[GS:ERROR step=... msg=...]`
lines that `App/src/LogParser.cpp` parses to drive the progress bar / detail pane. **`batch_reskin.py` does not emit these markers** — its output only ever appears as raw log lines.

### Group Run (a second path through the same two per-item scripts)

Triggered from the Symbol Groups tab's "Run Checked", but the actual subprocess orchestration
lives in `MainWindow::onRunGroups`/`runNextGroupItem` (MainWindow.cpp:875-1001). See §2.4.

---

## 2. Symbol Groups tab

`App/src/SymbolGroupWidget.cpp`. Manages named groups of image files, each with a style
reference + IPAdapter weight/denoise + per-item prompts.

| # | Command | Runs | Prerequisite |
|---|---|---|---|
| — | Import/Export JSON | pure file I/O | — |
| — | Apply → Pipeline | copies fields into the Pipeline tab, switches tabs (no process) | — |
| 1 | **Run Item** (single symbol) | `run_imagewithprompt.py --reference <item> --output-dir <dir> --output-name <base> --ip-weight <w> --denoise <d> [--positive-prompt] [--negative-prompt]` **or** `batch_reskin.py <folderOfItem> <outDir>`, depending on the script-mode combo | `projectRoot` set; script exists; **ComfyUI running** (help text: *"Make sure ComfyUI is running (Pipeline → Launch)"*, SymbolGroupWidget.cpp:114) |
| 2 | **Run Checked ▶ (N)** | Same two scripts, but for every item of every *checked* group, queued and run **sequentially** through `MainWindow`'s single `groupProcess_` (MainWindow.cpp:919-1001). Also writes a synthetic `step4_style_plan.json` per item before running it. | Same as above. **Failures don't stop the queue** — each item logs OK/FAILED independently and the run continues (unlike the main Pipeline, which halts on first failure). |

---

## 3. Group Builder tab

`App/src/GroupBuilderWidget.cpp`. Groups a folder of images either by filename prefix or by
asking a vision-capable LLM to group them.

| # | Command | Runs | Prerequisite |
|---|---|---|---|
| 1 | Scan | local recursive file walk (no process) | Source Folder must exist |
| 2 | New Group from Selected | in-memory | ≥1 checked file |
| 3 | Auto-Group by Prefix | in-memory, groups by leading filename letters | file list non-empty (i.e. Scan ran) |
| 4 | **Auto-Group by Vision** | `python -u _pipeline/tools/auto_group_symbols.py --folder <f> --output <f>/symbol_groups_auto.json --backend {ollama\|openai\|anthropic\|llama} [--api-url] [--model] [--reference] [--style-directive]` | folder scanned; script exists; **no automatic backend-reachability check** — failure only surfaces after launch via stderr/non-zero exit |
| 5 | **Check** (backend reachability) | `python -c "<TCP connect_ex probe of the URL field>"`, env `CHECK_URL=<url>` | URL field non-empty. **Manual/optional** — not required before, and not auto-invoked by, Auto-Group by Vision |
| 6 | Export JSON | local file write | groups non-empty |
| 7 | Open in Symbol Groups | Qt signal to MainWindow, no process | groups non-empty |

Backend → default URL: `ollama` → `http://localhost:11434`, `openai` →
`https://api.openai.com/v1`, `anthropic` → `https://api.anthropic.com`, `llama` (local llama.cpp /
LM Studio, OpenAI-compatible) → `http://127.0.0.1:8666/v1`. `anthropic`/`openai` backends need
`ANTHROPIC_API_KEY`/`OPENAI_API_KEY` in the environment — **not set by the app**.

---

## 4. Image With Prompt tab

`App/src/ImageWithPromptWidget.cpp`. Per-image ComfyUI IPAdapter restyle with per-item
category/prompt overrides.

| # | Command | Runs | Prerequisite |
|---|---|---|---|
| — | Browse / Import / Export JSON | local scan / file I/O | — |
| 1 | **Run Item** | `run_imagewithprompt.py --reference <path> --output-dir <dir> --output-name <base> --seed <n> --steps <n> --cfg <f> --denoise <f> --ip-weight <f> [--positive-prompt \| --category] [--negative-prompt]` | `projectRoot` set; script exists; **ComfyUI reachable** (not probed by the widget — script-side failure only) |
| 2 | **Run All** | Same script per **checked** item, sequential, one shared batch output folder `output/IWP_batch_<timestamp>` | items list non-empty; ≥1 checked item; `projectRoot` set |
| — | Stop | kills the running process, clears the batch queue | — |

---

## 5. Text2Image tab

`App/src/Text2ImageWidget.cpp`. Generates images from a name+prompt list, plus AI helpers to
build that list from a pasted table and/or a style-reference image.

| # | Command | Runs | Prerequisite |
|---|---|---|---|
| — | Add/Remove/Import/Export JSON | local, no process | — |
| 1 | **Run Item** | `run_text2image.py --prompt <p> --output-name <name> --output-dir <dir> [--width --height]` | `projectRoot` set; prompt non-empty (else the item is skipped, not failed); **ComfyUI reachable** (not probed) |
| 2 | **Run All** | Same script, sequential over *every* item (no per-item checkbox in this tab) | items non-empty; `projectRoot` set |
| 3 | **Test** (in the "AI Parse Table" dialog) | `python -c "<TCP probe of the API URL>"` | URL field non-empty |
| 4 | **Analyze ▶** (style image → style hint) | `analyze_style_image.py --image <path> --backend <b> --api-url <url>` | style image chosen; script exists |
| 5 | **Generate Prompts** (parse pasted table) | writes the pasted TSV to a temp file, then `parse_table_to_items.py --table <tmp> --output <out.json> --backend <b> --api-url <url> [--style <hint>]` | table text non-empty; script exists; **chosen backend must be reachable** (Ollama/llama/OpenAI/Anthropic — not pre-checked) |
| 6 | **Wake Ollama** | TCP probe `127.0.0.1:11434`; if down, `ollama serve` (must be on PATH), detached; re-probes after 3s | none — soft-fails with a status message if `ollama` isn't installed |
| 7 | **Wake LLaMA** | TCP probe `127.0.0.1:8666`; if down, locates `run_llama.bat` (saved path → common relative paths → file-picker) and launches `cmd.exe /k <bat>` — an **interactive console** the user must pick a model number in | bat file must be locatable; **cannot be made headless without also automating the batch file's menu** |

Backend defaults for Parse Table/Analyze: `anthropic` → `https://api.anthropic.com`, `ollama` →
`http://localhost:11434` (note: different from the Wake-Ollama button's fixed
`127.0.0.1:11434` target), `openai` → `https://api.openai.com/v1`, `llama` →
`http://127.0.0.1:8666/v1`.

---

## 6. Pose Generator tab

`App/src/PoseGeneratorWidget.cpp`. Two independent sub-pipelines chosen by a Subject-type combo.

| # | Command | Runs | Prerequisite |
|---|---|---|---|
| 1 | Test (backend URL) | TCP probe one-liner | none |
| 2 | **Generate Pose Data (AI)** — skeleton pipeline | `generate_pose_from_prompt.py --name <n> --description <d> --subject-type {human\|biped_animal} --loop <bool> --frames <n> --backend <b> --api-url <url> --output <poses_library>/<n>.json` | `projectRoot`, Name + Description set; script exists; refuses (exit 1) for non-biped subjects, pointing at the Creature pipeline instead |
| — | Load JSON / Save to Library | local file I/O | — |
| 3 | **Generate Pictures** — skeleton render | `generate_pose_sequence.py --animation <json> --facing <l\|r> --output-dir <dir> --prefix pose_<n> [--frames]` | pose JSON present (from #2 or Load JSON); pure local rendering, **no ComfyUI, no AI backend needed** |
| 4 | **Generate Animation (Video AI)** — creature pipeline | `generate_creature_animation.py --reference-image <path> --motion-prompt <t> [--negative-prompt] --name <n> --frames <n> --duration <5\|10> --aspect-ratio <r> --model-name <kling-*> --mode <std\|pro> --output-dir <dir>` | reference image must already exist (e.g. from Text2Image tab); **ComfyUI running** with the Kling video nodes installed **and a comfy.org account/API auth (`auth_token_comfy_org`) already configured inside ComfyUI** — this is a paid, credit-billed cloud node; neither the widget nor the script sets that credential |

---

## 7. Atlas tab

`App/src/AtlasWidget.cpp`. The only tab with **no subprocess/Python at all** — pure in-process
`QImage`/`QPainter` work on Spine/libGDX `.atlas` sprite sheets.

| # | Command | Prerequisite |
|---|---|---|
| **Extract** | unpacks each sprite region in the chosen `.atlas` file into its own PNG under `<atlasDir>/atlas<scaleTag>/`, plus a rescaled companion `.atlas`. Meant to run **before** restyling those PNGs through ComfyUI (Pipeline "Include Spine Textures" or Symbol Groups). |
| **Merge Back** | composites the (possibly restyled) per-sprite PNGs back into the sheet. Requires the matching `atlas<scaleTag>/` folder from a prior Extract at the **same Scale value**; run **after** the ComfyUI restyle step. Missing individual PNGs are logged `[Missing]` and left transparent rather than aborting the merge; per-file overwrite prompts can be declined ("Cancel" → logged as `[Cancel] Skipping merge save`, not an error). |

---

## 8. Resource Replace tab

`App/src/ResourceReplaceWidget.cpp`. The only tab using **HTTP directly** (no CLI scripts). TLS
peer verification is disabled on every request (self-signed cert tolerance) and there is **no
login/auth step anywhere** — every call is an anonymous request.

| # | Command | Call | Prerequisite |
|---|---|---|---|
| 1 | Info | `GET <server>/api/sharedcode` | server field non-empty; falls back to opening the URL in a browser on failure |
| 2 | **Start Replace** | `POST <server>/api/onlineGameResourceReplace` `{nasRoot, targetFolder, gameId, platformName, deployEnv:[...], userName}`, tried against a multi-URL fallback chain (`https://host:3443` → as-typed → `http://...`) | NAS root/target/game ID/user name all set; ≥1 of Dev/QA checked; server field set. **Not** gated on the NAS upload having happened first (workflow convention only) |
| 3 | **Commit && Push** | `POST <server>/api/resourceCommitPush` `{nasRoot, targetFolder, gameId, userName, commitMessage}` | Yes/No confirmation dialog; same field checks as above. Intended to run **after** Start Replace has been verified (convention, not enforced) |
| 4 | **Upload Output to NAS** | `robocopy <outputFolder> <nasTarget> /E /IS /IT` (local/UNC copy, not HTTP) | not already uploading; local output folder exists; NAS target set. Recommended **first** step before Start Replace/Commit && Push (their `nasRoot`/`targetFolder` fields expect the files to already be there) |

Default server address: `https://localhost:3443`. Reachability is discovered reactively (a failed
POST after trying all fallback URLs) — the exact `"Cannot connect to server: %1\n\nPlease check
the server is running."` message comes from `tryUrls()` exhausting its candidate list.

---

## 9. Ordering / prerequisite quick reference

| To run this... | ...you must first have |
|---|---|
| Classify | run Scan (needs `active_target.json` + `step1_assets_inventory.json`) |
| Link | run Classify |
| Plan Style | run Link |
| ComfyUI Batch | run Plan Style **with the same Asset Root** (freshness-checked both client-side and inside `run_comfyui_batch.py` itself); **ComfyUI running** |
| Simple Batch | nothing pipeline-wise, but **ComfyUI running** |
| Symbol Groups: Run Item / Run Checked | **ComfyUI running** (launch it from the Pipeline tab first) |
| Image With Prompt: Run Item/All | **ComfyUI running** |
| Text2Image: Run Item/All | **ComfyUI running** |
| Pose Generator: Generate Pictures | Generate Pose Data (or Load JSON) done first |
| Pose Generator: Generate Animation | a reference image already generated (e.g. via Text2Image); **ComfyUI running with Kling nodes + comfy.org auth configured** |
| Text2Image: Parse Table / Analyze Style with backend=`ollama` | Wake Ollama (or Ollama already running) |
| Text2Image: Parse Table / Analyze Style with backend=`llama` | Wake LLaMA, model chosen in its console, fully loaded |
| Group Builder: Auto-Group by Vision | the chosen backend server reachable (soft requirement — not gated) |
| Atlas: Merge Back | Atlas: Extract, at the **same Scale**, plus (usually) a ComfyUI restyle pass on the extracted PNGs in between |
| Resource Replace: Start Replace / Commit && Push | Upload Output to NAS (workflow convention, not code-enforced) |
| Launch ComfyUI | ComfyUI + its Python venv already installed under `_comfyui/ComfyUI/.venv` (Windows: via `start_comfyui_with_flags.bat`; no automated Linux/macOS launch path in the GUI) |

---

## 10. Known gaps and bugs found during this audit

These are **existing behaviors in the current GUI**, left as-is in the GUI to avoid changing
established behavior, but worth knowing:

1. **`--ip-weight`/`--denoise`/`--steps` are silently dropped for the Pipeline tab's ComfyUI Batch
   step.** `run_comfyui_batch.py` accepts all three (run_comfyui_batch.py:431-433), and the
   Pipeline tab has spin-boxes for all three, but `PipelineCommands::buildStep()`
   (PipelineCommands.cpp:37-51) never forwards them — they're read/written to `QSettings` but only
   actually take effect for **Group Run** (§1's Group Run / §2's Run Item/Checked), not for the
   plain "ComfyUI Batch" pipeline step. *Headless mode (§12) fixes this and forwards them when
   explicitly passed.*
2. **`--hero-reference` is passed to a script that doesn't accept it, in one code path.**
   `MainWindow::runNextGroupItem` (Symbol Groups → Run Checked) passes
   `--hero-reference <group.referencePath>` to `run_imagewithprompt.py` when the group has a
   reference set (MainWindow.cpp:986-988), but that script's argparse has no such flag
   (`run_imagewithprompt.py:250-289`). Any "Run Checked" batch with `imagewithprompt` mode and a
   non-empty group reference will fail that item with an "unrecognized arguments" error — logged
   as FAILED, and the queue moves on to the next item rather than stopping.
3. **Two different Python-side fallback hosts exist for `COMFYUI_SERVER`.** The Qt app's C++
   default is `127.0.0.1:8188`; the shared Python helper
   (`TestData/ComgyUIPythonCommonFunction.py:10`) falls back to `localhost:8188` (a specific
   LAN address) if the env var is ever unset. In normal GUI use the app always sets the env var, so
   this never triggers — but any script run manually/outside the app without `COMFYUI_SERVER` set
   will silently target that LAN host, not localhost.
4. **`batch_reskin.py` emits no `[GS:...]` progress markers**, so Simple Batch runs never populate
   the structured progress bar / detail pane, only the raw log.
5. **ComfyUI reachability is never checked before launching a generation step** (ComfyUI Batch,
   Simple Batch, Image With Prompt, Text2Image, Symbol Groups, Pose Generator's animation step) —
   only the dedicated "Launch ComfyUI" button probes first. All other paths discover an unreachable
   ComfyUI only via the launched script's own connection failure.
6. **`start_comfyui_with_flags.bat`'s own `--listen 127.0.0.1` is overridden by the app-supplied
   `--listen 0.0.0.0`**, since the app's flags are appended after the bat's own hardcoded ones on
   ComfyUI's command line and argparse takes the last value — the effective bind address ends up
   `0.0.0.0`, not the bat's own default.

---

## 11. Scripts that exist but aren't wired into the GUI

Found under `_pipeline/tools/` — useful for automation/dev work, but no button calls them:

| Script | Purpose |
|---|---|
| `asset_pipeline.py visual-link` | CLIP-embedding based static↔spine linking by visual similarity (an alternative/supplement to the `link` step's group-name matching) |
| `asset_pipeline.py render-visual-previews` | Builds merged preview images for visually-linked groups |
| `check_style_pipeline.py` | Readiness check: git/python present, ComfyUI + venv installed, checkpoint/ControlNet/IPAdapter/CLIP-Vision models present, ComfyUI port reachable. **This is what headless mode's `check` command wraps** (§12). |
| `run_pipeline.py` | A minimal `scan+classify+link+plan-style` one-shot wrapper — superseded by `asset_pipeline.py all` and now by headless mode's `all` command |
| `smoke_test.py` | Runs scan→classify→link→plan-style back to back and prints combined JSON — a basic self-test, not part of any tab |
| `comfy_style_runner.py` | A lower-level ComfyUI workflow-patching/queueing helper; not imported by `run_comfyui_batch.py` and not called by the GUI |

---

## 12. Headless mode

`GameStyleAI --headless [--port <n>] [--host <addr>] [--config <path.json>]` starts a
**persistent TCP server** that runs the **core 5-step pipeline** (§1) without any GUI — no
`QApplication`, no window, no `QSettings` dependency on a prior desktop session. It reuses the
exact same `AppConfig`/`PipelineCommands::buildStep()` code the GUI uses, so the argv built for
each Python script is identical to what the GUI would run.

The server does not exit after one command. A client connects over TCP, sends one JSON command
per line, and gets back one or more NDJSON events per command ending in a `job_end` — the
connection then stays open for the next command in the same session. This exists specifically so
a client can run a *sequence* against one shared, mutable config — e.g. set the working
directory, then launch ComfyUI, then run the pipeline — instead of every command starting from
scratch the way the old one-shot CLI did.

Implementation: `App/src/HeadlessRunner.h/.cpp`, wired in from `App/src/main.cpp` — if `argv[1] ==
"--headless"`, the app constructs a `QCoreApplication` (not `QApplication`) and dispatches into
`runHeadless()`, which listens with `QTcpServer` and serves one client connection fully (until it
disconnects) before accepting the next. Everything is synchronous/blocking (`waitForNewConnection`,
`waitForReadyRead`, `QProcess::waitForFinished(-1)`), so no Qt event loop (`app.exec()`) needs to
run. On Windows the binary is still built `WIN32` (no console) for the GUI's sake; headless mode
reattaches to the parent console only when no stdout/stderr handle exists at all, so shell
redirection of its own stderr log (`> log.txt`, CI runners, etc.) is left untouched — the actual
command protocol travels over the TCP socket, not stdout/stdin.

### One engine, two front ends, JSON in between

`App/src/PipelineEngine.h/.cpp` is the single place that knows how to (a) decide whether the
output folder / style plan gates pass, and (b) build the exact program/args/env for a pipeline
step. Both `MainWindow` (GUI) and `HeadlessRunner` (CLI) call the same four functions and get back
plain `QJsonObject` verdicts/requests — neither one re-derives this logic on its own:

| `PipelineEngine` function | Returns (JSON) | GUI does with it | CLI does with it |
|---|---|---|---|
| `checkOutputFolderState(cfg)` | `{applicable, targetFolder, empty, entryCount}` | pops the Delete/Overwrite/Cancel `QMessageBox` when `empty` is false | requires `{"overwrite":true}`/`{"clean":true}` in `args` when `empty` is false, else `job_end.code` 2 |
| `checkStylePlanState(cfg)` | `{applicable, planPath, planExists, planRoot, fresh, reason}` | pops the Regenerate/Continue/Cancel `QMessageBox` when `fresh` is false | requires `{"regenerate":true}`/`{"continue-stale":true}` in `args` when `fresh` is false, else `job_end.code` 3 |
| `buildStepRequest(step, cfg, projectRoot, extra)` | `{step, program, args:[...], workingDirectory, env:{...}}` | copies these fields onto its own async `process_` (so the progress bar / Stop button / live `[GS:...]` marker parsing keep working) | passes the JSON straight to `runStepRequestSync()` |
| `runStepRequestSync(request, onEvent?)` | `{started, exitCode, success, error}` | *(not used — the GUI stays async for live progress)* | runs the step synchronously, printing `[headless] ...` to stderr, and always passes an `onEvent` callback that turns each output line into a `marker`/`log` JSON event sent to the client (see "The NDJSON job-progress/result protocol" below) |

`configToJson`/`configFromJson` give `AppConfig` the same JSON shape on both sides too, which is
what `--config <path.json>` loads.

Net effect: there is exactly one implementation of "is the output folder OK to write into", one of
"is the style plan fresh", and one of "what command line does step X actually run" — the GUI and
the headless CLI each wrap that single JSON-speaking implementation in their own front end
(dialogs vs. flags/exit-codes) rather than maintaining two copies of the same decision.

### Scope

Headless mode covers the Pipeline tab (§1) in full — `check`, `comfyui-status`, `comfyui-launch`,
`scan`, `classify`, `link`, `plan-style`, `comfyui-batch`, `simple-batch`, `all` — plus one
game-asset-generation command from each of Text2Image, Image With Prompt, Pose Generator, Atlas
Tools, and Resource Replace: `text2image`, `imagewithprompt`, `pose-from-prompt`,
`atlas-extract`/`atlas-merge`, and `nas-upload` (see the command table below and
`Doc/Headless_Game_Asset_Generation_Plan.md` for why these six).

The rest of those tabs, and Symbol Groups/Group Builder entirely, remain GUI-only — they're
workflow/editing tools built around a person reviewing images and JSON in the loop (Group
Builder's manual drag-and-drop grouping, Text2Image's "AI Parse Table" / "Wake LLaMA" dialogs,
Resource Replace's registration-review form), not batch automation, and Text2Image's
"Wake LLaMA" specifically requires picking a model number in an interactive console. If
build-server automation for any of those becomes a real need, the per-command inventories in
§2–§8 above already have everything needed to add them the same way the six above were.

### Commands

Every command is sent as one JSON line: `{"command": "<name>", "args": {...}}`. `args` holds the
same flags the old one-shot CLI took, just as JSON object keys/values instead of `--flag value`
(e.g. `{"asset-root": "...", "overwrite": true}`).

| Command | Equivalent to | Notes |
|---|---|---|
| `set-working-dir` | the GUI's Working Directory "Set" button (`MainWindow::onSetWorkingDirectory`) | `args: {"path": <dir>}`; `chdir`s the server process to it and sets it as this session's Project Root |
| `set-config` | the GUI's `syncUiToConfig()` | merges any of `project-root, output-folder, asset-root, hero-reference, positive-prompt, negative-prompt, style-profile-id, batch-id, comfyui-server, python-exe, category, ip-weight, denoise, steps, include-spine` into this session's config; fields not present are left unchanged |
| `get-config` | — | returns this session's current config as JSON (a `result` event with a `config` field) |
| `check` | — (readiness check) | Wraps `_pipeline/tools/check_style_pipeline.py`; result comes back as a `result` event |
| `comfyui-status` | the reachability half of "Launch ▶" | `job_end.success` is `true`/`false` for reachable/unreachable |
| `comfyui-launch` | "Launch ▶" | probes first; if unreachable, launches it (Windows: the existing `.bat`; Linux/macOS: runs `_comfyui/ComfyUI/.venv/bin/python main.py` directly, since there's no `.bat` equivalent on those platforms), then polls up to `args.wait-timeout` seconds (default 60) |
| `scan` | Scan | gated by the output-folder check |
| `classify` | Classify | |
| `link` | Link | |
| `plan-style` | Plan Style | |
| `comfyui-batch` | ComfyUI Batch | gated by output-folder + style-plan-freshness checks |
| `simple-batch` | Simple Batch | gated by the output-folder check |
| `all` | Run All (full pipeline) | chains scan→classify→link→plan-style→comfyui-batch, stops at the first non-zero exit |
| `text2image` | Text2Image tab's "Run" | generates a new image straight from a prompt, no source asset needed — `args: {"prompt": <text>}` required; optional `output-name`, `output-dir` (default `<projectRoot>/output/Text2Image`), `width`+`height` |
| `imagewithprompt` | Image With Prompt tab's "Run Item" | a prompt-guided variant of one reference image — `args: {"reference": <img path>}` required, plus `positive-prompt` or `category`; optional `negative-prompt`, `output-dir` (default `<projectRoot>/output/ImageWithPrompt`), `output-name`, `seed`, `steps`, `cfg`, `denoise`, `ip-weight` (denoise/steps/ip-weight default from the session's config, same knobs `comfyui-batch` uses) |
| `pose-from-prompt` | Pose Generator tab's "Generate Pose Data (AI)" | generates pose/skeleton JSON from a text description via an LLM backend (no ComfyUI) — `args: {"name": <n>, "description": <text>}` required; optional `subject-type` (`human`\|`biped_animal`, default `human`), `loop`, `frames` (default 8), `backend`/`api-url` (default `ollama`), `output` (default `<projectRoot>/TestData/PoseGenerator/poses_library/<name>.json`) |
| `atlas-extract` | Atlas Tools tab's "Extract" | unpacks a Spine `.atlas`'s packed texture(s) into individual sprite PNGs — `args: {"atlas-path": <file.atlas>}` required; optional `scale` (default 1.0) — no subprocess, this is in-process image math (`AtlasOps::extractAtlas`) |
| `atlas-merge` | Atlas Tools tab's "Merge Back" | packs sprite PNGs (from `atlas-extract`'s output folder) back into the atlas texture(s) — `args: {"atlas-path": <file.atlas>}` required; optional `scale`, `merge-mode` (`separate`\|`single`, only matters for multi-page atlases — the GUI's own QMessageBox choice, taken as an arg here), `conflict` (`fail`\|`replace`\|`rename` for an already-existing output file — the GUI's own QMessageBox choice; defaults to `fail`, i.e. never silently overwrite) |
| `nas-upload` | Resource Replace tab's "Upload Output to NAS" | `robocopy`s a local output folder to a NAS target — `args: {"source": <dir>, "target": <dir>}` required; optional `conflict` (`fail`\|`overwrite`\|`rename` when the NAS target already has content — the GUI's own delete/move/cancel dialog, taken as an arg here; defaults to `fail`) |
| `exit` / `quit` | — | closes the connection; no reply is sent |

`set-working-dir`/`set-config`/`get-config` only exist in headless mode's session — they mutate
the connection's in-memory `AppConfig`, they don't touch the GUI's `QSettings` or any file.

`text2image`/`imagewithprompt`/`pose-from-prompt` build their request through `PipelineEngine`
(`buildText2ImageRequest`/`buildImageWithPromptRequest`/`buildPoseFromPromptRequest`, the same
`{program, args, workingDirectory, env}` shape `buildStepRequest`/`buildComfyUiLaunchRequest`
already use) and run it through the same `runStepRequestSync`, so their NDJSON events look
exactly like a pipeline step's (`step_start`/`marker`/`log`/`step_end`), plus a `result` event
carrying `outputPath` on success. `atlas-extract`/`atlas-merge` skip the subprocess machinery
entirely and call `AtlasOps` directly, emitting a `result` event with the extract/merge counts.
See `Doc/Headless_Game_Asset_Generation_Plan.md` for the rationale/design of these six commands.

### Non-interactive confirmation gates

The GUI pops a `QMessageBox` when the output folder isn't empty, or when an existing
`step4_style_plan.json` looks stale. Headless mode has no one to click a button, so both require an
explicit `args` field instead:

- Output folder not empty → pass `"args": {"overwrite": true}` (write into it anyway) or
  `{"clean": true}` (delete it first); otherwise the command fails with `job_end.code` 2 and a
  clear message.
- Stale style plan (only checked for `comfyui-batch`) → pass `{"regenerate": true}` (re-runs
  scan→classify→link→plan-style→comfyui-batch, i.e. delegates to `all`) or
  `{"continue-stale": true}` (proceed with the existing plan anyway); otherwise `job_end.code` 3.

### Startup flags

```
--port <n>              TCP port to listen on. Default 8765.
--host <addr>           Interface to bind. Default 127.0.0.1 (localhost only — the server has no
                         authentication, so don't bind 0.0.0.0 on a machine you don't trust).
--config <path.json>    Seed the default session config for new connections (same field names as
                         AppConfig: projectRoot, outputFolder, assetRoot, heroReference,
                         positivePrompt, negativePrompt, styleProfileId, batchId, comfyuiServer,
                         pythonExe, ipAdapterWeight, denoise, steps, includeSpine). See
                         App/headless_config.example.json.
--<any set-config field> value   Also seeds the default session config, same as sending it via
                         set-config right after connecting (e.g. --project-root, --comfyui-server).
```

### Built-in self-test: `--headless-test`

`GameStyleAI --headless-test [--port <n>] [--host <addr>] [--config <path.json>]` is a
one-process smoke test for when a separate client gets "Connection refused" and you want to
know whether the server itself even works, without juggling two terminals. It starts the exact
same server as `--headless` on a background thread, then connects a built-in client and runs it
through `get-config`, `set-working-dir` (against the server process's own current directory, so
it needs no project checked out), `comfyui-status`, and an unknown-command call (to confirm
errors come back as a clean `job_end` rather than a hang), printing each request/reply plus a
final `ALL CHECKS PASSED` / `SOME CHECKS FAILED` line, and exits with code 0/1 accordingly (2 if
it couldn't even bind the port). It does **not** touch ComfyUI or run any pipeline step, so it
passes even without Python/ComfyUI installed — it only proves the server and wire protocol work.
Implementation: `HeadlessRunner::runHeadlessSelfTest()`, reusing the exact same
`handleClientSession()`/command dispatch the real server uses.

**Deliberately does not read the GUI's `QSettings`** — a headless session should be fully
reproducible from `--config` / startup flags / the commands you send it, independent of whatever
a desktop user last saved.

### Connecting and running a session

Any TCP client works — a script, `nc`/`ncat`, another app. Send one JSON object per line; read
one or more JSON objects per line back, ending in a `job_end` for that command, before sending
the next one. Minimal Python example:

```python
import socket, json

s = socket.create_connection(("127.0.0.1", 8765))
f = s.makefile("rwb")

def send(cmd):
    f.write((json.dumps(cmd) + "\n").encode("utf-8")); f.flush()
    while True:
        obj = json.loads(f.readline())
        print(obj)
        if obj.get("type") == "job_end":
            return obj

send({"command": "set-working-dir", "args": {"path": "D:/Work/GamestyleAI"}})
send({"command": "comfyui-launch", "args": {"wait-timeout": 120}})
send({"command": "scan", "args": {"asset-root": "D:/Work/GamestyleAI/TestData/Reskin/props"}})
send({"command": "classify"})
send({"command": "link"})
send({"command": "plan-style", "args": {"style-profile-id": "default_parameters", "batch-id": "nightly"}})
send({"command": "comfyui-batch", "args": {"overwrite": True}})
send({"command": "exit"})
```

**`App/tools/headless_client.py`** is a ready-to-run reference client (stdlib only, no deps) for
exercising this by hand — connect, send commands one at a time, see exactly what comes back:

```sh
# interactive session (default 127.0.0.1:8765) -- type "help" for the command list
python App/tools/headless_client.py

# one-shot: send a single command and exit; process exit code mirrors job_end.success
python App/tools/headless_client.py comfyui-status
python App/tools/headless_client.py scan asset-root=D:/Work/GamestyleAI/TestData/Reskin/props overwrite=true
```

Exit codes carried in `job_end.code` (same numbers the old one-shot CLI used as its process exit
code): `0` success; `1` the launched Python script itself failed; `2` a precondition/config
problem (bad output folder, missing files, missing `path`); `3` stale style plan with neither
`--regenerate` nor `--continue-stale`; `64` unknown command.

### Example sessions

These use `App/tools/headless_client.py`'s one-shot form (`python App/tools/headless_client.py
<command> key=value ...`) for readability — the same sequence works identically as JSON lines
over the raw socket, or through `HeadlessTestClient.exe`. Each line assumes the server is already
running (`GameStyleAI.exe --headless`) and, aside from the first, that you're reusing the same
client session so `set-working-dir`/`set-config` stick (the one-shot CLI form opens a *new*
connection per call, so in practice you'd send these through one interactive/scripted session
instead — see the Python `send()` example above, or drive `HeadlessTestClient.exe` by hand).

**1. Full pipeline run for a freshly-added asset folder** — scan discovers what's in
`asset-root`, classify/link group it, plan-style writes the batch plan, comfyui-batch restyles
every asset through ComfyUI:

```json
{"command": "set-working-dir", "args": {"path": "D:/Work/GamestyleAI"}}
{"command": "comfyui-launch", "args": {"wait-timeout": 120}}
{"command": "set-config", "args": {"style-profile-id": "default_parameters", "batch-id": "props_v1"}}
{"command": "scan", "args": {"asset-root": "D:/Work/GamestyleAI/TestData/Reskin/props"}}
{"command": "classify"}
{"command": "link"}
{"command": "plan-style"}
{"command": "comfyui-batch", "args": {"overwrite": true}}
```

Or the same thing in one call — `all` chains scan→classify→link→plan-style→comfyui-batch and
stops at the first failure:

```json
{"command": "set-config", "args": {"asset-root": "D:/Work/GamestyleAI/TestData/Reskin/props", "style-profile-id": "default_parameters", "batch-id": "props_v1"}}
{"command": "all", "args": {"overwrite": true}}
```

**2. Give ComfyUI a specific prompt for this batch** — `positive-prompt`/`negative-prompt`
override whatever the style profile's default prompt would have been, for every asset in the
run (this is a *reskin* — it restyles existing source images with ComfyUI's img2img, guided by
the prompt; there's no headless command yet for generating a brand-new image from a prompt with
no source image — see the feature note below):

```json
{"command": "set-config", "args": {
    "positive-prompt": "hand-painted fantasy stone texture, weathered, mossy, painterly",
    "negative-prompt": "photo, 3d render, blurry, low quality",
    "asset-root": "D:/Work/GamestyleAI/TestData/Reskin/props"
}}
{"command": "comfyui-batch", "args": {"continue-stale": true}}
```

(`continue-stale` reuses whatever style plan is already on disk for that asset root instead of
demanding a fresh scan/classify/link/plan-style first — fine when you're just iterating on the
prompt for a set of assets you've already planned once.)

**3. Iterate on generation quality without re-scanning** — tune `denoise`/`ip-weight`/`steps`
and re-run just the ComfyUI step against the existing plan, e.g. to compare a few settings back
to back:

```json
{"command": "comfyui-batch", "args": {"denoise": 0.65, "ip-weight": 0.8, "steps": 40, "continue-stale": true}}
{"command": "comfyui-batch", "args": {"denoise": 0.85, "ip-weight": 0.6, "steps": 40, "continue-stale": true}}
```

**4. A batch/CI-style nightly job** — confirm the environment is ready, make sure ComfyUI is up,
then run everything unattended:

```json
{"command": "check"}
{"command": "comfyui-launch", "args": {"wait-timeout": 180}}
{"command": "set-config", "args": {"asset-root": "D:/Work/GamestyleAI/TestData/Reskin/props", "batch-id": "nightly_20260923"}}
{"command": "all", "args": {"overwrite": true}}
{"command": "exit"}
```

**5. One-off reskin, skipping the pipeline plan entirely** — `simple-batch` restyles a folder
directly with the current config's prompt/style, without scan/classify/link/plan-style at all;
useful for a quick one-shot pass over a small folder:

```json
{"command": "set-config", "args": {"asset-root": "D:/Work/GamestyleAI/TestData/Reskin/small_batch", "positive-prompt": "gritty sci-fi metal paneling"}}
{"command": "simple-batch", "args": {"overwrite": true}}
```

**6. Generate a brand-new game asset from just a prompt, then pack it into a sprite atlas** —
`text2image` needs no source image at all; `atlas-extract`/`atlas-merge` round-trip a Spine
atlas so a freshly-generated (or hand-edited) sprite can be dropped back in:

```json
{"command": "text2image", "args": {"prompt": "2D mobile game icon, health potion, red glass bottle, flat vibrant illustration", "output-name": "icon_health_potion", "width": 512, "height": 512}}
{"command": "atlas-extract", "args": {"atlas-path": "D:/Work/GamestyleAI/TestData/Reskin/props/props.atlas"}}
```

At this point, replace/regenerate whichever sprite PNGs you want under `atlas-extract`'s output
folder (`<atlas dir>/atlas/`, printed in its `result` event), then pack them back in:

```json
{"command": "atlas-merge", "args": {"atlas-path": "D:/Work/GamestyleAI/TestData/Reskin/props/props.atlas", "conflict": "rename"}}
```

**7. A prompt-guided variant of an existing asset, then ship the output to a NAS** —
`imagewithprompt` for a single reference image, `nas-upload` to publish the result:

```json
{"command": "imagewithprompt", "args": {"reference": "D:/Work/GamestyleAI/TestData/Reskin/props/crate.png", "positive-prompt": "same crate, cracked and mossy, abandoned ruins style", "denoise": 0.7}}
{"command": "nas-upload", "args": {"source": "D:/Work/GamestyleAI/output/ImageWithPrompt", "target": "\\\\nas01\\GameAssets\\props_variant_01", "conflict": "rename"}}
```

**8. Generate pose data for a new character from a description:**

```json
{"command": "pose-from-prompt", "args": {"name": "goblin_scout_idle", "description": "a small goblin scout crouching, alert, looking left, holding a dagger", "subject-type": "biped_animal", "frames": 12}}
```

### The NDJSON job-progress/result protocol

Every command gets back one compact JSON object per line — [NDJSON](http://ndjson.org/) —
describing the job as it runs, ending in exactly one `job_end`. **Each line is a complete,
self-contained JSON object — read it line by line, not as one big document.** Each object has a
`"type"` field:

| `type` | Emitted | Fields |
|---|---|---|
| `job_start` | once, immediately | `command` |
| `step_start` | once per pipeline step | `step`, `index`, `total` (1-based position within this job — e.g. step 2 of 5 for `all`) |
| `marker` | for every `[GS:...]` line a script prints | `step`, `marker` (`STEP_START`\|`PROGRESS`\|`STEP_DONE`\|`STEP_DETAIL`\|`ERROR`), plus `scriptStep`/`message`/`current`/`total`/`detail` depending on which marker |
| `log` | for every other line a script prints (not a `[GS:...]` marker) | `step`, `stream` (`stdout`\|`stderr`), `line` |
| `result` | `check` only, once | `command`, `report` (the parsed readiness-check JSON) or `raw` if it didn't parse |
| `step_end` | once per pipeline step | `step`, `exitCode`, `success` |
| `job_end` | **exactly once, always last** | `command`, `code`, `success`, `message` |

**`job_end` is the answer to "what happened to the job"**: `code` is the same integer as the
process's own exit code (0/1/2/3/64, as above — so `success` is just `code == 0`), and `message`
is a one-line human-readable summary (e.g. `"Step 'ComfyUI Batch' failed (exit 1)."`,
`"Pipeline completed successfully (scan, classify, link, plan-style, comfyui-batch)."`,
`"ComfyUI is not reachable."`). A caller that only wants pass/fail can ignore every other line and
just look at the last one.

`marker`/`log` events reuse `App/src/LogParser.cpp` — the exact same parser the GUI's log view
uses for the `[GS:STEP_START/PROGRESS/STEP_DONE/STEP_DETAIL/ERROR ...]` lines `asset_pipeline.py`
and `run_comfyui_batch.py` print (§1) — so a `PROGRESS` marker's `current`/`total` fields are the
same per-asset progress the GUI's progress bar shows. `batch_reskin.py` (Simple Batch) doesn't
print these markers (gap #4), so its lines all come through as plain `log` events instead.

Example — `all` failing partway through:

```json
{"type":"job_start","command":"all"}
{"type":"step_start","step":"Scan","index":1,"total":5}
{"type":"marker","step":"Scan","marker":"STEP_START","scriptStep":"scan"}
{"type":"marker","step":"Scan","marker":"STEP_DONE","scriptStep":"scan","message":"STEP_DONE step=scan assets=74]"}
{"type":"step_end","step":"Scan","exitCode":0,"success":true}
{"type":"step_start","step":"Classify","index":2,"total":5}
{"type":"step_end","step":"Classify","exitCode":0,"success":true}
{"type":"step_start","step":"Link","index":3,"total":5}
{"type":"step_end","step":"Link","exitCode":0,"success":true}
{"type":"step_start","step":"Plan Style","index":4,"total":5}
{"type":"step_end","step":"Plan Style","exitCode":0,"success":true}
{"type":"step_start","step":"ComfyUI Batch","index":5,"total":5}
{"type":"log","step":"ComfyUI Batch","stream":"stderr","line":"Cannot reach ComfyUI at 127.0.0.1:8188"}
{"type":"step_end","step":"ComfyUI Batch","exitCode":1,"success":false}
{"type":"job_end","command":"all","code":1,"success":false,"message":"Step 'ComfyUI Batch' failed (exit 1)."}
```

A calling app can drive a progress UI off `step_start`/`step_end`/`PROGRESS` markers as they
arrive, and decide success/failure the moment it sees `job_end`.

Implementation note: the server captures each launched script's stdout/stderr itself (instead of
forwarding them straight through) so it can parse each line and re-emit it as a `marker`/`log`
event on the client socket — see `PipelineEngine::runStepRequestSync`'s `onEvent` callback
parameter and `HeadlessRunner.cpp`'s `JsonReporter` struct (`JsonReporter::sink` is the connected
`QTcpSocket`; every `[headless] ...` line still also goes to this process's own stderr for an
operator tailing the server's console).
