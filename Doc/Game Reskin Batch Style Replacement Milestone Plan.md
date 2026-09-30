**Game Reskin Batch Style Replacement Milestone Plan**

*Senior Programmer Roadmap: PyTorch, ComfyUI, One-by-One Testing, CLI Batch Automation, Style Consistency, and End-User UI*

Purpose: define a practical technical roadmap for building a production-ready tool that replaces the visual style of game pictures/assets in batches while keeping output consistent, reviewable, and usable by non-technical end users.

# **Target Outcome**

* A reproducible picture style replacement pipeline for game reskinning.
* A tested ComfyUI workflow for single-image replacement, then automated batch replacement.
* A command-line interface that can process folders, resume failed jobs, and export logs.
* A style consistency layer so all replaced assets follow the same target art direction.
* A simple UI for artists or operators to upload source assets, choose a style preset, preview, batch run, and export results.

# **Recommended Technical Stack**

| **Layer**     | **Recommended Choice**         | **Purpose**                                                                          | **Notes**                                                             |
| ------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------- |
| Learning foundation | Python + PyTorch                     | Understand tensors, model loading, GPU memory, image preprocessing                         | Do not over-study research topics; focus on inference and pipelines.        |
| Workflow engine     | ComfyUI                              | Build repeatable node workflows for image-to-image, IPAdapter, LoRA, ControlNet, upscaling | Use ComfyUI first as a visual lab, then call it through API for automation. |
| Base model          | SDXL first, Flux later               | Start easier, then upgrade quality                                                         | Flux gives better quality but is harder and heavier.                        |
| Style control       | IPAdapter + Style LoRA               | Reference target style and keep art direction consistent                                   | Best practical stack for game reskinning.                                   |
| Structure control   | ControlNet / depth / canny / lineart | Preserve original shapes, icons, sprites, and UI layout                                    | Critical for game assets.                                                   |
| Batch runtime       | Python CLI + ComfyUI API             | Automate folder processing and job tracking                                                | Avoid manual drag-and-drop production work.                                 |
| End-user UI         | Web UI or desktop UI                 | Let artists/operators use presets without editing nodes                                    | Keep advanced settings hidden at first.                                     |

Download necessary models require a lot disk space and time(around 200-500 G, it might take 2-3 days)

# **Milestone Roadmap**

| **Milestone**                     | **Goal**                           | **Senior Programmer Tasks**                                                                                                                                                       | **Deliverables**                                 | **Exit Criteria**                                                              | **Risk**                                      |
| --------------------------------------- | ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------ | --------------------------------------------------- |
| M0: Project Definition                  | Clarify what assets are being reskinned  | Collect sample images: icons, characters, UI, backgrounds, sprites. Define target style references and quality bar. Decide copyright-safe style references.                             | Asset sample set, target style board, success criteria | At least 30 representative source images and 3 target style references approved.     | Unclear art direction causes inconsistent output.   |
| M1: PyTorch Study                       | Understand enough AI runtime basics      | Study image tensors, PIL/OpenCV preprocessing, CUDA device handling, fp16/fp8, batching, memory limits, model loading, deterministic seeds.                                             | Small PyTorch image preprocessing scripts              | Can load image folders, resize/pad, normalize, save outputs, and measure GPU memory. | Over-learning theory delays production.             |
| M2: ComfyUI Basics                      | Operate and debug ComfyUI workflows      | Install ComfyUI, learn checkpoints, LoRA, VAE, sampler, img2img, ControlNet, IPAdapter, queue, output folders, workflow JSON.                                                           | Working SDXL img2img workflow                          | Can run one image through workflow and reproduce same result with seed/settings.     | Node dependency/version conflicts.                  |
| M3: Single Image Style Replacement Test | Replace one picture style manually       | Test source image + target style reference one by one. Compare SDXL vs Flux if hardware allows. Tune denoise, CFG, ControlNet strength, IPAdapter strength, LoRA weight.                | Before/after image report                              | 3-5 asset types pass visual review without breaking shape/layout.                    | Too much denoise destroys game asset identity.      |
| M4: Repeatable Workflow Template        | Convert experiment into stable template  | Clean ComfyUI graph, expose only required inputs, lock model versions, document node versions, create style presets.                                                                    | Versioned ComfyUI workflow JSON                        | Same input/settings produce expected repeatable output.                              | Workflow works only on developer machine.           |
| M5: Command-Line Single Run             | Trigger ComfyUI from code                | Use ComfyUI API from Python. Submit prompt JSON, upload image, wait for output, download result, log metadata.                                                                          | CLI: replace_style --input image.png --style preset    | One command processes one image end-to-end.                                          | API handling and output discovery are brittle.      |
| M6: Batch Replacement Engine            | Process folders safely                   | Add folder scanning, output naming, retries, resume mode, error folder, progress bar, CSV/JSON logs, seed control, GPU memory guard.                                                    | CLI batch tool                                         | Can process 100 images unattended with logs and no overwrites.                       | Long jobs fail without resume/retry.                |
| M7: Style Consistency Pass              | Make all outputs look like the same game | Add style preset configs: model, LoRA, IPAdapter reference, ControlNet mode, resolution rules, denoise range, seed strategy, color correction. Add similarity review sheet.             | Preset system and QA comparison grid                   | Outputs from different source assets share the same target art direction.            | Assets drift into different styles.                 |
| M8: Production QA                       | Validate usability for real game assets  | Test by category: icons, characters, environments, UI. Add manual review status, reject reasons, side-by-side contact sheet, optional Photoshop cleanup notes.                          | QA report + accepted/rejected outputs                  | 80%+ assets pass first or second run for selected category.                          | AI artifacts make outputs unusable without cleanup. |
| M9: End-User UI                         | Let non-programmers operate the pipeline | Build simple UI: choose source folder, choose style preset, preview one image, run batch, pause/resume, show errors, export ZIP/folder. Hide technical parameters behind advanced mode. | MVP UI                                                 | Artist/operator can run batch without editing ComfyUI nodes or command line.         | Too many controls confuse users.                    |
| M10: Deployment and Maintenance         | Make it reliable for team use            | Package environment, freeze model/node versions, write install script, add model path validation, update guide, preset backup, logs.                                                    | Release package + documentation                        | New machine setup can run test batch successfully.                                   | Dependency drift breaks workflows later.            |

# **Preferred Development Sequence**

* First build a reliable one-image workflow. Do not start with batch automation before single-image quality is acceptable.
* Start with SDXL + IPAdapter + ControlNet because it is easier to debug. Move to Flux only after the pipeline concept works.
* Use ComfyUI as the visual prototyping environment. Use Python and the ComfyUI API as the production automation layer.
* Create presets per target style instead of asking end users to tune models, strengths, seeds, and denoise values manually.
* Keep original asset shape/layout with ControlNet or masking; style replacement should not redesign the whole asset unless intended.

# **Batch CLI Requirements**

| **Feature** | **Requirement**                                                 | **Example**           |
| ----------------- | --------------------------------------------------------------------- | --------------------------- |
| Input             | Accept file or folder; support PNG/JPG/WebP                           | --input ./source_assets     |
| Preset            | Use named style preset                                                | --preset cyberpunk_ui_v1    |
| Output            | Never overwrite originals                                             | --output ./reskinned_assets |
| Resume            | Skip completed files unless forced                                    | --resume true               |
| Logging           | Write CSV/JSON with source, output, seed, preset, status, time, error | batch_log.csv               |
| Failure handling  | Move failed jobs to retry list                                        | failed_jobs.json            |
| Preview           | Run only first N images for validation                                | --limit 10                  |
| Determinism       | Store model versions, workflow hash, seed, parameters                 | metadata.json               |

# **End-User UI Scope**

* Home screen: select source folder, output folder, and style preset.
* Preview screen: compare original and replaced image before running a full batch.
* Batch screen: progress, current file, success/fail count, pause/resume, cancel safely.
* Review screen: side-by-side gallery with approve/reject/export.
* Advanced mode: denoise, style strength, ControlNet strength, seed, resolution, model selection.
* Preset manager: create, duplicate, lock, import, and export style presets.

# **Definition of Done**

* The pipeline can process a folder of game pictures without manual ComfyUI interaction.
* Each output has traceable metadata: preset, model, workflow version, seed, and source path.
* A non-programmer can run a preview and batch job from the UI.
* Style consistency is measurable through side-by-side review sheets and accepted preset rules.
* The system supports rollback, retry, and reproducible outputs.

# **Suggested Timeline**

| **Phase** | **Duration** | **Main Focus**                  | **Output**                              |
| --------------- | ------------------ | ------------------------------------- | --------------------------------------------- |
| Week 1          | 3-5 days           | PyTorch basics + ComfyUI installation | Single image preprocessing and basic workflow |
| Week 2          | 5 days             | Single image style replacement tests  | Approved workflow direction                   |
| Week 3          | 10 days            | Workflow template + ComfyUI API       | Command-line single image replacement         |
| Week 5          | 10 days            | Batch engine                          | Folder batch replacement with logs            |
| Week 7          | 10 days            | Style consistency and QA              | Preset system and comparison sheets           |
| Week 9+(2 or 6) | 2-6 weeks          | End-user UI and packaging             | Operator-ready MVP                            |

# **Key Warnings**

* Do not rely on prompt-only generation for reskinning; it will not preserve asset structure well enough.
* Do not let every artist manually change parameters; this breaks consistency. Use locked presets.
* Do not batch thousands of images before a small preview set is approved.
* Expect cleanup for text, UI readability, hands, faces, tiny icons, and animation frames.
* Keep model licenses and source style references safe for commercial use.

*End of document*
