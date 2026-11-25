# ComfyLive

A small C++ / Qt 6 app that turns a ComfyUI graph into a form of parameters and shows the picture as it forms
(ComfyUI's step previews), then the result. It can run as a window, or headless as a TCP server driven by JSON
commands (the same protocol as GameStyleAI's `--headless`, `../Doc/Commands_Reference.md` §12).

## Build

```
build.cmd            (Release; "build.cmd debug" for Debug)
```

It needs Visual Studio with the C++ tools and Qt 6 (msvc2022_64) under `%USERPROFILE%\Qt` (or `set QT_DIR=...`), the
same setup as `../App/BUILD.md`. Output: `build\Release\ComfyLive.exe` with its Qt DLLs next to it.

## Visual Studio (run and debug)

```
make_vs_solution.bat            writes build_vs\ComfyLive.slnx (VS 2026) or .sln (VS 2022)
make_vs_solution.bat open       ... and opens it
```

ComfyLive is the startup project: pick **Debug**, press **F5**. Breakpoints work in every `src/` file. Arguments
(`--start`, `--headless`, `--headless-test --start` ...) go in **Project > Properties > Debugging > Command
Arguments**. With F5, headless and test runs open a console window of their own for their output. Run the script again after adding a source file to
`CMakeLists.txt`.

## The window

- **ComfyUI**: two addresses. **Server** (default `127.0.0.1:8188`) runs every graph except speech. **Speech server**
  (default `127.0.0.1:8189`) runs the Qwen3-TTS speech graphs, from its own venv (see "Speech" below). ComfyLive
  connects to the one the graph needs by itself. **Start local ComfyUI** starts that one hidden from
  `../_comfyui/ComfyUI`, with `--preview-method auto` (without that, ComfyUI sends no previews). Logs:
  `../_comfyui/comfylive_server.log` and `comfylive_speech_server.log`. `ComfyLive.exe --start` starts it when nothing
  answers.
- **ComfyUI is never stopped by itself.** Starting it takes a minute, so it keeps running after ComfyLive closes and
  the next start is instant. Stop it with **Stop ComfyUI** (both servers), `ComfyLive.exe --comfy-stop`, or the
  headless `comfyui-stop`. The option **Stop the ComfyUI I started when closing** (off by default) brings back the old
  way.
- **The GPU is shared.** Before each run ComfyLive asks the other ComfyUI to unload its models (ComfyUI's `/free`), and
  the speech graphs unload their model after each run (`unload_model_after_generate`), so 12 GB is enough for both.
- **Graph**: **Built-in** offers five graphs: SDXL text to image, ACE-Step 1.5 text to music, Stable Audio Open text
  to sound effect, and Qwen3-TTS text to speech (a preset voice, or a voice described in words). **Open graph...**
  loads any graph saved from ComfyUI with **Export (API)**, a settings file written next to a song or sound, or a
  recipe (see "Recipes" below). The UI format (a `"nodes"` array) is refused with that hint. Every input that is not a link
  becomes a field, grouped by node: numbers, text, prompts, on/off switches and lists (the server's model, sampler
  and scheduler names). A seed has a **Random** button. A model name the server lacks is replaced with one it has.
- **Run**: **Generate** (Ctrl+Enter), **Stop**, **Save picture**. With **Live** on, every change starts a new run
  after a short pause (600 ms by default), and a run still in progress is stopped first. **New seed each run** is
  optional.
- The right side shows the picture, the history (the last 40, click one to show it again) and the log. A graph
  that makes audio (`SaveAudio`, `SaveAudioMP3` and the like) gets a song row under the picture with **Play / Stop**,
  **Open in player** and **Save audio...**. A new song plays by itself only when the window is in front. Playback uses
  Windows' own player (MCI). It always handles MP3. **Open in player** is for formats it can't play, such as FLAC on a
  PC without that codec. Songs are kept in `%LOCALAPPDATA%\MyWork\ComfyLive\audio`.

## Music (ACE-Step 1.5)

1. **The model:** `ace_step_1.5_turbo_aio.safetensors` (10 GB, one file holding the model, text encoders and VAE)
   in the `checkpoints` models folder (here `D:\Work\AI\models\checkpoints`). Download it from
   `https://huggingface.co/Comfy-Org/ace_step_1.5_ComfyUI_files/resolve/main/checkpoints/ace_step_1.5_turbo_aio.safetensors`.
2. **Built-in > Text to music (ACE-Step 1.5).** The form shows:
   - **Song:** the style (`tags`), the `lyrics` with `[Verse]`, `[Chorus]` and similar section tags, then `bpm`,
     `duration`, `keyscale`, `language` and `seed`;
   - **Sampler:** 8 steps, cfg 1 (the turbo model's settings).
   `duration` and **Length** `seconds` are one value: changing either changes both.
3. **Generate.** The song is saved as MP3.

**Song helpers** (any ACE-Step graph, in its Song group):

| Control | Does |
|---|---|
| **Singer** | Male, Female, Duet (male + female), Choir, Instrumental. Writes `male vocals`, `female vocals` ... first in `tags` and takes the previous choice out. Instrumental also puts `[Instrumental]` in the lyrics, and picking a singer again brings the lyrics back. |
| **Voice** | Soft, Breathy, Powerful, Falsetto, Raspy, in the same way (`breathy voice` ...). |
| **keyscale ▼ ▲** | The key one semitone down or up (D minor → D# minor). The next Generate makes a new song in that key: the melody changes too. |
| **Transpose** (song row) | The finished song, the same melody, −6 … +6 semitones, same length. It saves a new file next to it (`..._+2st.mp3`) and plays it. Up to ±3 sounds natural. It uses ffmpeg: TOMS' `tools/ffmpeg/ffmpeg.exe` when ComfyLive sits in `TOMS/AIGamestyle`, else `ffmpeg.exe` next to `ComfyLive.exe` or on PATH. |

**Settings file: the same song again.** Every song is saved with a `.json` of the same name beside it:
`ComfyLive_00007.mp3` + `ComfyLive_00007.json`. It goes in ComfyLive's audio folder, beside ComfyUI's own file in
`output/audio` when ComfyUI runs on this PC, and beside the copy made by **Save audio...** or **Transpose**. It holds
the graph exactly as it was sent: every value, both seeds and the model name. A transposed song's file also says
`"transpose": 2` and which song it came from. **Open graph...** with that `.json`, then **Generate**: the same song
again. Opening it turns off **New seed each run**. Headless: `generate` reports it as `settings`, and `load-graph`
takes it. The same model, seed and values give the same song. While the same ComfyUI is still running, it even hands
back the identical file from its cache.

**Parameter help.** Rest the mouse on a parameter marked ⓘ to see what it does to the result. **?** next to
**Built-in** lists every parameter of the graph with that text. It is in the window's language and covers the
sampler, the picture graph and every ACE-Step song input (tags, lyrics, the two seeds, bpm, key, cfg_scale,
temperature, top_p, top_k, min_p ...). Inputs it doesn't know show ComfyUI's own tooltip. The text is in
`src/ParamHelp.cpp`.

The model follows the singer words most of the time, not always: generate again (a new seed) if a take has the
wrong voice. `--song-test` checks the Singer / Voice / key rules without ComfyUI.

Other ACE-Step graphs work too, such as ComfyUI's XL or split-model templates: in ComfyUI use **Workflow > Export
(API)**, then **Open graph...**. Keep a real singer's name out of the tags and use lyrics you have the rights to.

## Sound effects (Stable Audio Open 1.0)

Birdsong, animal sounds, metal hits, footsteps, doors, magic whooshes: short sounds from a prompt.

1. **The models** (in `D:\Work\AI\models`):
   - `checkpoints/stable-audio-open-1.0.safetensors` (about 5 GB): `model.safetensors` from
     `https://huggingface.co/stabilityai/stable-audio-open-1.0`, **renamed to `stable-audio-open-1.0.safetensors`** (one
     file, not a folder; the graph, the recipes and the settings files load it by that exact name). The page is gated:
     sign in with a free account and click **Agree and access repository** (Stability AI Community License, free under
     US$1M yearly revenue). Then either click the download arrow of `model.safetensors` in the browser, or use a read
     token (`hf auth login`, then `hf download stabilityai/stable-audio-open-1.0 model.safetensors`). Step by step:
     TOMS `docs/engine/23_SPEECH_AND_SOUND_EFFECTS.md` §7.1.
   - `text_encoders/t5-base.safetensors` (850 MB): `model.safetensors` from `https://huggingface.co/google-t5/t5-base`, renamed.
2. **Built-in > Text to sound effect (Stable Audio Open).** The default is a dog barking twice, 3 seconds. The form shows:
   - **Sound:** the prompt. Name the source, the action, the material and the recording: `sword striking a steel shield,
     sharp metallic clang, single hit, close`, `small songbird chirping, morning forest`, `wolf howl, distant, night`.
   - **Avoid:** what not to hear (music, speech, noise ...).
   - **Timing** `seconds_total` and **Length** `seconds` are one value: changing either changes both. 1-3 s for a hit
     or a bark, 5-10 s for an ambience; up to 47 s.
   - **Sampler:** 50 steps, cfg 5, `dpmpp_3m_sde_gpu` / `exponential` (the model's own settings).
3. **Generate.** The sound is saved as MP3 (`ComfyLive_sfx_00001_.mp3`) with its settings `.json` beside it, exactly as
   for a song: **Save audio...** copies both, and **Open graph...** with the `.json`, then **Generate**, makes the same
   sound again. **Transpose** works too: a sound a few semitones higher or lower (a smaller or bigger animal).

The model follows the source and the material best; exact counts ("three barks") are hit and miss: try a few seeds.
For the TOMS game, convert the MP3 to OGG (`tools/ffmpeg/ffmpeg.exe -i x.mp3 -c:a libvorbis -q:a 5 x.ogg`).

**Language** (top left): English or 繁體中文. The window switches at once and keeps its connection, graph, values,
picture and log, and the choice is remembered. The first start follows Windows: Chinese Windows starts in 繁體中文.
`--lang=en` or `--lang=zh_TW` picks the language for one run. The Chinese text is a table built into the exe
(`src/Translator.cpp`), so there are no `.qm` files to ship; a text missing from it stays English. The ComfyUI input
names (`seed`, `steps`, `tags` ...), ComfyUI's own log lines and the headless protocol stay English, because those are
what ComfyUI and scripts use.

The settings, the graph and its values are kept between runs (QSettings `MyWork/ComfyLive`).

## Speech (Qwen3-TTS)

A person speaking given sentences, in a chosen language: one line, or a whole speech of several sentences read as one.
It runs Alibaba's Qwen3-TTS through the ComfyUI-Qwen-TTS custom node (Apache-2.0, the models under the Qwen3-TTS
licence). Languages: Chinese, English, Japanese, Korean, German, French, Russian, Portuguese, Spanish, Italian.

| Built-in | Node | The voice |
|---|---|---|
| **Text to speech, preset voice** | `FB_Qwen3TTSCustomVoice` | one of 9 speakers (`speaker`), a tone in `instruct` ("angry, shouting") |
| **Text to speech, voice from a description** | `FB_Qwen3TTSVoiceDesign` | described in words in `instruct` ("an old wizard, deep raspy voice") |

The speakers: Ryan and Aiden (English men), Vivian and Serena (Chinese women), Uncle_fu (an older Chinese man), Dylan
(a Beijing man), Eric (a Sichuan man), Ono_anna (a Japanese woman), Sohee (a Korean woman). Each speaks every
language, best its own.

**Why a second ComfyUI.** The node's model code is made for transformers 4.57. The main ComfyUI's venv has
transformers 5.8, and there the model ignores the text and talks nonsense (2026-11-15, checked with Whisper). The main
venv is left as it is, so the other nodes keep working. Speech runs in its own venv on port 8189, which loads only that
one custom node.

**Setup, once** (all under `../_comfyui`):

1. The node: `ComfyUI/custom_nodes/ComfyUI-Qwen-TTS` (`git clone https://github.com/flybirdxx/ComfyUI-Qwen-TTS`).
2. The models in `D:\Work\AI\models\qwen-tts` (`ComfyUI/models/qwen-tts` is a folder link to it):
   `Qwen3-TTS-Tokenizer-12Hz` (651 MB), `Qwen3-TTS-12Hz-1.7B-CustomVoice` (4.3 GB),
   `Qwen3-TTS-12Hz-1.7B-VoiceDesign` (4.3 GB), each from `hf download Qwen/<name> --local-dir <name>`.
3. The speech venv `ComfyUI/.venv-speech`: Python 3.11, the same torch as the main venv
   (`torch==2.14.1 torchaudio==2.11.0 torchvision==0.29.1` from `https://download.pytorch.org/whl/cu130`), ComfyUI's
   `requirements.txt`, the node's `requirements.txt` without onnxruntime, and `transformers==4.57.3`.

ComfyLive starts it as `.venv-speech\Scripts\python.exe main.py --port 8189 --disable-all-custom-nodes
--whitelist-custom-nodes ComfyUI-Qwen-TTS`. The first run of a voice loads the model (about 20 s); a sentence then
takes a few seconds.

## Qwen Image 2.1 Turbo: a picture from text, and pictures edited

Two built-in graphs for the 7B picture model Qwen Image 2.1 Turbo (8 steps):

- **Built-in > Text to image (Qwen Image 2.1 Turbo)**, `"graph": "qwen"`: a picture from a prompt.
- **Built-in > Edit pictures (Qwen Image 2.1 Turbo)**, `"graph": "qwen-edit"`: a new picture from 1 to 4 pictures of
  this PC and a prompt. The prompt names them `<image1>`, `<image2>` ...: `Keep the character in <image1> unchanged,
  show him from the side holding a bow`, or `put the shirt from <image2> on the character in <image1>`.

**Licence: research and evaluation only.** The model is under the Qwen Research License Agreement
(`https://huggingface.co/abenzerps/Qwen-Image-2.1-Turbo-Quantized/blob/main/LICENSE`): non-commercial use only; a
commercial use needs a separate licence from its maker. Do not put its pictures into a game that is sold until that is
settled. The model card also says the model has no content filter.

1. **The models** (in `D:\Work\AI\models`, 17.3 GB together), all from
   `https://huggingface.co/abenzerps/Qwen-Image-2.1-Turbo-Quantized` (no sign-in; its `SHA256SUMS` lists the hashes):
   - `diffusion_models/qwen-image-2.1-turbo-int8_convrot.safetensors` (7.3 GB);
   - `text_encoders/qwen3vl_8b_int8_convrot.safetensors` (9.4 GB), from the repository's `text_encoders/`;
   - `vae/qwen_image_2.1_vae_bf16.safetensors` (0.7 GB), from its `vae/`.

   No custom node is needed (ComfyUI 0.44 has the Qwen Image 2.1 nodes). They are not in TOMS's
   `tools\setup_ai_art.cmd`, because of the licence.
2. **The form.**
   - **Picture** (or **Edit**): `prompt`; `negative_prompt` does nothing at the Turbo's CFG 1; `resolution` is the size
     the edit's pictures are brought to (about resolution x resolution pixels).
   - **Size** (text to image): `width` and `height`. 1024 x 1024 is the start; the model can do 2048 x 2048, which
     needs more memory and is not tried on 12 GB.
   - **Picture 1** and **Picture 2 to 4 (optional)** (edit): a path, or **Browse...**. Empty optional boxes are left
     out. The result has picture 1's shape.
   - **Steps** holds the Turbo's own eight-step schedule (nine numbers). Leave it: a plain 8-step sampler does not give
     the same result. **Guider** `cfg` stays 1.
3. **Generate.** The picture shows in the view; **Save picture...** saves it.

**Recipes** (`ComfyLive.exe --make=x.json --out=x.png` writes the picture and its settings `x.json`):

```json
{ "graph": "qwen", "prompt": "a young archer boy ..., seen from the side, full body", "width": 832, "height": 1216, "seed": 21 }
{ "graph": "qwen-edit", "images": ["hero.png"], "prompt": "Keep the character in <image1> unchanged ... from the side, holding a bow" }
```

`images` is a list of 1 to 4 paths (a relative one is beside the recipe); `avoid` is the negative prompt; no `seed`:
a new picture each run. Examples: `examples/image_qwen_hero_side.json`, `examples/image_qwen_edit_hero_bow.json`.

**Measured on an RTX 4070 (12 GB):** 832 x 1216 from text in 18 s; an edit of one 832 x 1216 picture in 16 s (both
with the models already loaded once); peak 11.5 GB of VRAM.

### The Turbo as a GGUF file (Q6_K)

**Built-in > Text to image / Edit pictures (Qwen Image 2.1 Turbo Q6_K)**, `"graph": "qwen-gguf"` and `"qwen-gguf-edit"`:
the Turbo graphs with `diffusion_models/qwen-image-2.1-turbo-Q6_K.gguf` (6.3 GB, from the Turbo repository above,
SHA-256 checked) in place of the INT8 file. The same eight steps, form and recipe keys. It needs the GGUF loader fork
named below. In these graphs **Model** `unet_name` lists every `.gguf` in `diffusion_models/`, so another quant is
picked there. Measured on an RTX 4070: 832 x 1216 in about 20 s, an edit in 28 s, peak 11.4 GB: the same speed as the
INT8 file.

### The full model (not Turbo), as a GGUF file

**Built-in > Text to image / Edit pictures (Qwen Image 2.1 Q4_K_M, 25 steps)**, `"graph": "qwen-full"` and
`"qwen-full-edit"`: the same two jobs with the original Qwen Image 2.1 and a plain sampler (**Sampler** `steps`, 25 to
start). The same text encoder, VAE, picture boxes and recipe keys; `seed` and `steps` are the sampler's.

- **The model:** `diffusion_models/qwen-image-2.1-Q4_K_M.gguf` (4.6 GB). Its first publisher's repository is gone (the
  address now leads to the Turbo one); the file here came from the copy
  `https://huggingface.co/0xSojalSec/Qwen-Image-2.1-Uncensored-GGUF`, SHA-256 `833439e9...b4b2f8` as that copy lists.
  Its card says it is the original weights quantized, with nothing changed ("uncensored" there means the model has no
  content filter). The same research-only licence.
- **The GGUF loader must be the fork `https://github.com/leejet/ComfyUI-GGUF`** (2026-11-13 or newer) in
  `custom_nodes/ComfyUI-GGUF`: city96's loader of January 2026 answers "Unknown model architecture". HyperFlow H3 Flash
  works with the fork too. The loader that was replaced is kept in `_comfyui/custom_nodes_backup/`.
- **Measured on an RTX 4070 (12 GB):** 832 x 1216 from text in 60 s, an edit in 59 s (25 steps), peak 11.5 GB: about
  three times the Turbo's time. Another quant: put its `.gguf` in `diffusion_models/` and pick it in **Model**.

The two workflow files `examples/image_qwen_image_2_1_t2i_gguf.json` and `examples/Qwen image 2_1_multipPictures.json`
are in ComfyUI's editor format (open them in ComfyUI itself); they are the full model's graphs, which the two
`qwen-full` entries above do in ComfyLive.

## Video (Wan 2.2 TI2V-5B)

A short video of one character moving, from a prompt. It is the first step of TOMS's character animation from video
(TOMS `docs/engine/24_VIDEO_TO_SKELETON.md`): the later tools read the frames, so the frames are the main result and
the MP4 is for looking at.

1. **The models** (in `D:\Work\AI\models`, 17 GB together; TOMS `tools\setup_ai_art.cmd` downloads them). All are
   Apache 2.0 and need no sign-in. No custom node is needed: ComfyUI has the Wan nodes.
   - `diffusion_models/wan2.2_ti2v_5B_fp16.safetensors` (10.0 GB) and `vae/wan2.2_vae.safetensors` (1.4 GB), from
     `https://huggingface.co/Comfy-Org/Wan_2.2_ComfyUI_Repackaged` (`split_files/`);
   - `text_encoders/umt5_xxl_fp8_e4m3fn_scaled.safetensors` (6.7 GB), from
     `https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged` (`split_files/text_encoders/`).
2. **Built-in > Text to video (Wan 2.2 TI2V-5B).** The form shows:
   - **Video:** the prompt. Say who it is, then what the body does, step by step and in order: `stands facing right,
     raises the bow, draws the string back to the cheek, holds for a moment, releases the arrow`. The default prompt
     ends with the words that keep the video readable for the later tools: one character, the whole body in the frame,
     a side view, a camera that does not move, a plain grey background. Keep them.
   - **Avoid:** what not to see (camera movement, several characters, a busy background ...).
   - **Size and length:** `width` and `height` (1280 x 704 is the model's own size; 704 x 1280 for a tall picture) and
     `length` in frames. The model makes 24 frames a second and takes 4n+1 frames: 49 is 2 s, 73 is 3 s, 121 is 5 s.
   - **Sampler:** 20 steps, cfg 5, `uni_pc` / `simple`, and **Model shift** 8 (ComfyUI's own template for this model).
3. **Generate.** While it runs the view shows the sampler's previews. When it is done the frames are read from ComfyUI
   and the video plays in a loop. Under it: **Pause / Play**, one frame back and on, **Open frames folder** and
   **Save video...**.
   - The frames are kept as `frame_0001.png`, `frame_0002.png` ... in `<name>_frames`, with `<name>.mp4` (made by
     ffmpeg, the same one Transpose uses) and the settings `<name>.json` beside the folder. **Save video...** copies
     all three wherever you want. Without ffmpeg there is no MP4; the frames are still kept.
   - **Open graph...** with the `.json`, then **Generate**, makes the same video again.
   - **Save picture...** saves the frame on screen.

With text only the video is a new person each time: only the prompt describes the look. To get the same character
in every clip, give a reference picture (below).

### A reference picture: the video starts as your picture

**Built-in > Picture to video (Wan 2.2 TI2V-5B)** or **Picture to video (HyperFlow H3 Flash Q3_K_M)**. The form has
one more box, **Reference picture**: type the file's path or click **Browse...**. The picture is the video's first
frame, so the video shows that character, in that pose, on that background.

- **The picture:** one character, the whole body, a little room around it for the motion, a plain background. The
  pose in the picture is the pose every clip starts from, so a ready stance is the most useful.
- **The size follows the picture.** Choosing a picture sets `width` and `height` to its shape (about as many pixels as
  before, in steps of 32), so nothing is cropped or stretched. You can still change them.
- **The prompt says only what moves:** `the boy raises the bow, draws the string back and releases the arrow`. The
  look comes from the picture. The built-in prompt ends with the words that hold the camera and the background still.
- ComfyLive sends the picture to ComfyUI's `input` folder itself (as `comfylive_<id>_<name>`). The settings file keeps
  your own path, so opening it makes the same video again.
- **In a recipe:** add `"image"` to a `video` or `video-h3` recipe (a path; a relative one is beside the recipe):

  ```json
  { "graph": "video", "image": "hero_ready.png", "prompt": "the boy breathes calmly, the cape sways", "seconds": 3 }
  ```

- **Measured on an RTX 4070 (12 GB)** with an 832 x 1216 picture, 73 frames: Wan at 800 x 1184 took 261 s; HyperFlow
  at 512 x 736 took 65 s. Both kept the face, the clothes and the colours through the clip.

### Reference pictures to video (HyperFlow): the character in a new pose

**Built-in > Reference pictures to video (HyperFlow H3 Flash)**, `"graph": "video-h3-ref"`. The pictures are
**references, not the first frame**: the video shows that character doing what the prompt says, in a new pose, framing
and background. With **First frame to video** the video starts as the picture exactly as it is.

- **Picture 1**, and **Picture 2 and 3 (optional)**: a path or **Browse...**. The prompt names them `<Picture 1>`,
  `<Picture 2>`: `<Picture 1> seen from the side, raises a wooden bow, draws the string and releases the arrow`, or
  `<Picture 1> walks through the scene of <Picture 2>`.
- **The model** is its own file: `diffusion_models/minimax_h3_ref2va_hyperflow_flash_14b-Q4_K_S.gguf` (8.1 GB, from
  the same repository, folder `ref2va_gguf/`). The text encoder and VAEs are the same; this graph also loads the audio VAE.
- `width` and `height` are yours to set (the picture does not set them): 832 x 480 for a wide view, 480 x 832 for a
  tall figure. `ref_image_size` `match` is the fast setting; `max` keeps more of the picture's detail and is slower.
- In a recipe: `"images": ["hero.png"]` (1 to 3). Example: `examples/video_h3_ref_hero_bow.json`.
- **Measured on an RTX 4070 (12 GB):** one reference picture, 832 x 480, 73 frames, background removed: 88 s, peak
  11.6 GB. The hero of the concept picture, seen from the side, drew and released the bow.

The HyperFlow text and first-frame entries now load **Q4_K_M** (`minimax_h3_fl2va_hyperflow_flash_14b-Q4_K_M.gguf`,
9.1 GB) by default; Q3_K_M is still in the **Model** list. With Q4_K_M and Remove background on: text, 832 x 480,
73 frames in 84 s; first frame, 512 x 736, 73 frames in 101 s; peak 11.9 GB.

### Remove background: frames with only the character

All four video graphs have a **Remove background** box in the form, **on by default**. With it on, every frame is a
PNG with only the character and a transparent background (the ground shadow goes too), ready to be packed into an
atlas and played as a picture sequence. Turn it off to keep the video's own background.

- It is the Inspyrenet background-removal node (`comfyui-inspyrenet-rembg`, the custom node TOMS's art scripts use),
  run on each frame after the video is made. It added about 40 s to a 73-frame clip at 512 x 736 on an RTX 4070.
- The MP4 has no transparency (H.264): it shows the character on grey. The window shows the frames on its dark view.
- In a recipe: `"remove-background": false` turns it off.
- The choice is kept in the settings file with the rest of the graph.

### Video with sound: LTX-2 (`"graph": "video-ltx"`)

A recipe with `"graph": "video-ltx"` makes a short video **with its soundtrack** using LTX-2 19B distilled
(Lightricks), in 8 steps at 768 x 512 and 25 frames a second. Unlike the other video graphs it ends in ComfyUI's
`SaveVideo`: ComfyUI writes the MP4 (picture and sound), and ComfyLive downloads that file to `out`, with its
settings file beside it. There are no frames and ffmpeg is not used.

```json
{ "graph": "video-ltx", "prompt": "a knight in silver armor swings a sword at a wooden training dummy",
  "sound": "a sharp metallic swoosh, then a loud wooden thud, the knight grunts", "seconds": 4 }
```

- **Keys:**
  - `prompt` is what is seen.
  - `sound` is what is heard. It is added to the prompt as "Sound: ...".
  - `avoid`, `width`, `height` and `seed`.
  - `seconds` is turned into 8n+1 frames: 4 s is 97 frames. It sets the picture's and the sound's length together.
  - No fixed-camera ending is added to the prompt, and there is no background removal.
- **Describe the sound plainly.** With no clear sound description, or at 24 frames a second, the soundtrack came out
  almost silent.
- **The models** on the ComfyUI server: `checkpoints/ltx-2-19b-distilled.safetensors` (the video and audio VAE are
  inside it) and `text_encoders/gemma_3_12B_it.safetensors`, from `https://huggingface.co/Lightricks/LTX-2`.
- **Memory and time,** measured on an RTX 4060 Ti 16 GB with 34 GB of RAM:
  - 768 x 512 for 4 s takes 66 to 90 s and peaks at 15.5 GB.
  - Bigger or longer clips may run out of memory.
- **Licence:** LTX-2 Community License Agreement. Read it before using the output in a product.
- **Any graph that ends in SaveVideo** now works the same way: an MP4, WebM, MOV or MKV that ComfyUI saves is
  downloaded as a file (a `video` event with `path`), not read as a picture.

### A second video model: HyperFlow H3 Flash (Q3_K_M)

**Built-in > Text to video (HyperFlow H3 Flash Q3_K_M)**, or `"graph": "video-h3"` in a recipe
(`examples/video_h3_hero_bow_attack.json`). It is Video Rebirth's 6-step version of MiniMax H3, a 14B model, as a GGUF
file. The frames, the MP4, the settings file and the window's controls are the same as for Wan.

- **Licence: read it before using anything it makes.** The weights are under the MiniMax H3 Community License
  Agreement (`https://huggingface.co/VideoRebirthAI/hyperflow-h3-flash/blob/main/LICENSE`), accepted by running the
  model. It does not cover use in the USA, the European Union, the United Kingdom or South Korea, and it forbids
  using, distributing or showing the model's outputs there. A commercial product has to show "MiniMax H3" in its
  interface. Wan 2.2 (Apache 2.0) has none of these limits.
- **Custom nodes** (in `ComfyUI/custom_nodes/`, loaded when ComfyUI starts):
  `https://github.com/city96/ComfyUI-GGUF` and `https://github.com/Video-Rebirth/ComfyUI-HyperFlow-Flash`, plus
  `pip install "gguf>=0.13.0"` in ComfyUI's `.venv`. ComfyUI 0.43 or newer (it has the MiniMax H3 nodes).
- **The models** (28.7 GB together; not in `tools\setup_ai_art.cmd`, because of the licence):
  - `diffusion_models/minimax_h3_fl2va_hyperflow_flash_14b-Q3_K_M.gguf` (7.2 GB), from
    `https://huggingface.co/VideoRebirthAI/hyperflow-h3-flash` (`fl2va_gguf/`);
  - `text_encoders/qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors` (15.7 GB), `vae/minimax_h3_video_vae_fp16.safetensors`
    (5.2 GB) and `vae/minimax_h3_audio_vae_fp32.safetensors` (0.6 GB, not used by this graph), from
    `https://huggingface.co/Comfy-Org/MiniMax-H3`.
- **The form:** **Video** has the prompt, `width`, `height` and `length` (17n+5 frames at 24 a second: 56, 73, 90, 107,
  124 ...; the model was trained on 124 and more, shorter works but is outside that). There is no **Avoid**: the model
  samples without a negative prompt. **Steps** is 4, 6 or 8. **Model** `weights_in`: `auto` keeps the weights that do
  not fit on the GPU in the file on a fast NVMe drive (`disk`), which is what a 12 GB card needs.
- **Measured on an RTX 4070 (12 GB), 64 GB of RAM, models on NVMe, 6 steps:** 640 x 352, 56 frames: 35 s, peak
  11.2 GB of VRAM. 832 x 480, 73 frames: 61 s, peak 10.5 GB. Larger sizes are not tried yet; the publisher measured
  only 24 GB and 32 GB cards.
- Another quant: put its `.gguf` in `diffusion_models/` and pick it in **Model** `unet_name`.

## Headless mode

```
ComfyLive.exe --headless [--port=8766] [--host=127.0.0.1] [--server=127.0.0.1:8188]
                         [--graph=graph_api.json] [--comfy-root=...] [--start]
```

There is no window. The TCP server listens on `--port` (default **8766**, so it can run beside GameStyleAI's 8765).
A client sends one JSON command per line:

```json
{"command": "generate", "args": {"params": {"steps": 12, "text": "a red dragon"}, "out": "D:/out/dragon.png"}}
```

It gets NDJSON events back:

- `{"type":"job_start","command":...}` first;
- then `result`, `log`, and for `generate` also `queued`, `progress`, `preview` and `image`;
- `{"type":"job_end","command","code","success","message"}` last.

Commands run one after another. The graph and its values stay for the next command and the next connection.

| Command | Args | Does |
|---|---|---|
| `comfyui-status` | `server`? or `which`? (`"main"`, `"speech"`) | Is a ComfyUI answering? result `{reachable, server, connected}`; code 1 if not |
| `comfyui-launch` | `which`? (`"main"`, `"speech"`), `comfy-root`?, `custom-nodes`? (true) | Starts the local ComfyUI hidden (nothing if one answers) and ends once it is up. It keeps running after ComfyLive exits |
| `comfyui-stop` | `which`? (`"main"`, `"speech"`, `"all"` = default) or `server`? | Stops the local ComfyUI(s), whoever started them (only a python process on that port). result `{stopped}` |
| `connect` | `server`? | Reads the node list and opens the WebSocket. result `{server, nodeTypes}` |
| `get-params` | | result `{graph, params: [{key, node, input, group, kind, value, choices?, min?, max?}]}` |
| `set-params` | `params: {"<node>/<input>": v}` or `{"<input>": v}` | A bare input name sets every input of that name (`"steps"`). Unknown names and values outside the server's list are refused (code 2) |
| `load-graph` | `path`, or `graph` (API-format object) + `name`? | Replaces the graph |
| `save-graph` | `path` | Saves the graph with its current values |
| `reset-graph` | `graph`? `"image"` (default), `"music"`, `"sfx"`, `"speech"` or `"voice"` | A built-in graph: SDXL text to image, ACE-Step 1.5 text to music, Stable Audio Open sound effect, Qwen3-TTS preset voice, Qwen3-TTS voice from a description |
| `make` (or `song`) | `path`, `out`?, `random-seed`? | One song, sound or speech from a JSON file in one command (see "Recipes" below). Starts the ComfyUI it needs when nothing answers. Events as for `generate` |
| `generate` | `params`?, `random-seed`?, `out`?, `preview-dir`? | Runs the graph. Events: `queued {promptId}`, `progress {value,max}`, `preview {index,width,height,path?}`, `image {file,width,height,path?}` or `audio {file,format,bytes,path?}`, then result `{seconds, previews, output}`. For audio, `out` takes the real format's extension (`song.wav` from an MP3 graph is written as `song.mp3`) |
| `transpose` | `path`, `semitones` (−12…12), `out`? | Transposes an audio file with ffmpeg (same melody, same length). result `{path}`; default out `<name>_+Nst.<ext>` |
| `stop` | | Stops the running generate. It is answered at once, beside it |
| `exit` / `quit` | | Closes this connection; the server keeps running |
| `shutdown` | | Ends the headless server (the ComfyUIs keep running) |

`get-params` and `set-params` connect first when needed; without a ComfyUI they end with code 4 ("send
comfyui-launch first"). `generate` and `make` start the ComfyUI they need. A speech graph goes to the speech server
(`--speech-server=127.0.0.1:8189`), every other graph to `--server`.

Codes:

| Code | Meaning |
|---|---|
| 0 | ok |
| 1 | not reachable / failed to start |
| 2 | bad command or args |
| 4 | no ComfyUI |
| 5 | ComfyUI refused the graph |
| 6 | no image or audio made |
| 7 | ComfyUI failed |
| 8 | stopped |

A ComfyUI that the headless server starts keeps running after it exits (`--stop-comfy-on-exit` for the old way).
`comfyui-stop` or `ComfyLive.exe --comfy-stop[=main|speech|all]` stops it.

## Recipes: one file, one sound

```
ComfyLive.exe --make=recipe.json [--out=x.mp3] [--random-seed] [--stop-comfy-on-exit] [--server=...] [--speech-server=...]
```

This makes one song, sound, speech or video and exits: 0 when the file was written, otherwise the codes above. It uses the
ComfyUI that answers, or starts the one the graph needs and leaves it running. `--song=` is the same option. The `make`
command of `--headless` does the same thing. Without `--out`, the file goes next to the JSON file as
`<name>_<date-time>.mp3`, with its settings file `<name>_<date-time>.json` beside it. An `out` whose settings file would
overwrite the input file is refused (code 2). In the window, **Open graph...** opens the same files.

The file can be one of three kinds:

- **A settings file:** the `.json` written next to every song, sound or speech. The same graph, values and seed: the
  same result again. Add `--random-seed` for a new take.
- **An API graph** from ComfyUI's **Export (API)**.
- **A recipe:** a short description for a built-in graph. `"graph"` picks it: `"sfx"`, `"speech"`, `"voice"`,
  `"music"` (or no `"graph"`: a song file), `"image"`, or a video graph (`"video"`, `"video-h3"`, `"video-ltx"` ...). [examples/](examples/) has many to try:

  ```json
  {"graph": "sfx", "prompt": "a steel sword striking a metal shield, sharp metallic clang, single hit", "seconds": 1.5}
  {"graph": "speech", "text": ["People of the valley, hear me.", "The hero is already climbing."],
   "language": "English", "speaker": "Ryan", "style": "a proud old king, slow and solemn"}
  {"graph": "voice", "text": "So, another climber.", "language": "English",
   "voice": "An old male wizard, deep and slightly raspy voice, speaking slowly"}
  ```

  | Graph | Keys |
  |---|---|
  | `sfx` | `prompt`, `avoid`, `seconds` (both length inputs), `seed`, `steps`, `cfg` |
  | `speech` | `text`, `language`, `speaker`, `style` (= `instruct`), `seed`, `temperature`, `model_choice` |
  | `voice` | `text`, `language`, `voice` (= `instruct`), `seed`, `temperature` |
  | `music` | `tags`, `lyrics`, `duration`, `bpm`, `keyscale`, `language`, `seed`, ... |
  | `image` | `prompt`, `avoid`, `width`, `height`, `steps`, `seed`, ... |

  The song file [examples/quiet_flame_song.json](examples/quiet_flame_song.json):

  ```json
  {
      "name": "Quiet Flame",
      "tags": ["1970s Philadelphia soul ballad", "deep raspy baritone male lead", "lush string section"],
      "lyrics": ["[Verse 1]", "Every evening when the lights turn low", "..."],
      "duration": 289, "bpm": 64, "keyscale": "Bb major", "language": "en"
  }
  ```

  Rules for a recipe:
  - Any input name of the graph works (`get-params` lists them), and so does `"<node>/<input>"`. A `"params": {}`
    object holds more of them. An unknown name is refused, naming it.
  - An array is joined: `tags` with ", ", a speech's `text` with a space (one reading), anything else line by line.
  - Without `"seed"`, each run is a new take. With one, the same file gives the same result.
  - Keys starting with `_` are notes and are ignored. `out` and `random-seed` in the file are defaults that the command
    line overrides.

### A video recipe

```json
{
    "graph": "video",
    "prompt": "a young archer in a blue tunic stands facing right, draws a wooden bow and releases the arrow",
    "seconds": 3,
    "seed": 102
}
```

```
ComfyLive.exe --make=examples\video_hero_bow_attack.json --out=D:\out\hero_bow_attack.mp4
```

This writes `D:\out\hero_bow_attack_frames\frame_0001.png` ... (73 frames for 3 s), `hero_bow_attack.mp4` and
`hero_bow_attack.json`, and prints the three paths. Without `--out` they go next to the recipe as
`<name>_<date-time>`. The exit code is 0 when the frames were written; a missing ffmpeg only leaves out the MP4
(`no MP4: ...` is printed, the code is still 0).

- `"graph": "video-h3"` is HyperFlow H3 Flash instead of Wan: the same keys, without `avoid`, and `seconds` is
  rounded up to 17n+5 frames.
- `prompt` (or `text`): what happens. ComfyLive adds the ending that keeps one whole character, a fixed camera and a
  plain background (`Workflow::videoPromptSuffix`). `"raw-prompt": true` sends the prompt exactly as written.
- `avoid`: the negative prompt; leave it out to keep the built-in one.
- `seconds`: turned into frames (24 a second, rounded to 4n+1). `length` gives the frames directly.
- `image`: a reference picture as the first frame (see "A reference picture"); without `width` and `height` the
  video takes the picture's shape.
- `remove-background`: `false` keeps the video's background (on by default: transparent frames).
- `width`, `height`, `steps`, `cfg`, `seed`: as in the form. No `seed`: a new video each run.

In `--headless`, `make` (or `reset-graph {"graph": "video"}`, `set-params`, `generate {"out": "x.mp4"}`) sends one
event when the frames are in:

```json
{"type": "video", "path": "D:\\out\\x.mp4", "frames": "D:\\out\\x_frames", "count": 73, "fps": 24, "width": 1280,
 "height": 704, "settings": "D:\\out\\x.json"}
```

`mp4Error` replaces `path` when ffmpeg could not make the MP4. Three recipes for the TOMS hero are in `examples/`:
`video_hero_bow_idle`, `video_hero_bow_attack`, `video_hero_bow_hurt`.

## Checks (nothing appears on the screen)

```
ComfyLive.exe --headless-test [--start] [--music] [--out=headless_test.png]
```

This runs the server and a built-in client in one process. Without a ComfyUI it checks only the protocol (status, an
unknown command). With one, or with `--start`, it also runs connect, get-params, a refused and an accepted
set-params, and a real 768², 8-step generate. `--music` adds the ACE-Step graph: a 20-second song saved as
`headless_test_song.mp3`, then transposed +2 (`headless_test_song_+2st.mp3`). It also runs `song` with a 15-second song
file and then with that song's settings file, and checks that both runs give the same song. It prints `ALL CHECKS PASSED` (exit 0) or `SOME CHECKS FAILED`
(exit 1).

```
ComfyLive.exe --selftest [--start] [--steps=8] [--out=result.png] [--server=...]
ComfyLive.exe --screenshot=window.png [--start] [--generate] [--steps=8] [--lang=zh_TW] [--switch-lang=zh_TW]
```

```
ComfyLive.exe --video-test [--out=video_test.mp4]
```

`--video-test` needs no ComfyUI and no model: it checks the video recipe's rules (seconds to frames, the prompt's
ending, `raw-prompt`), writes 12 drawn frames as PNG files, reads them back, makes an MP4 of them and checks its
settings file. It prints `ALL CHECKS PASSED` (exit 0) or `SOME CHECKS FAILED` (exit 1).

`--selftest` runs the built-in graph once. `--screenshot` draws the window off screen, after one generation if you
add `--generate`. These modes use Qt's `offscreen` platform, with fonts from `%WINDIR%\Fonts`.

## Files

| File | |
|---|---|
| `src/ComfyClient` | HTTP (`/object_info`, `/prompt`, `/interrupt`, `/queue`, `/view`) plus the WebSocket events and previews |
| `src/MiniWebSocket` | a small RFC 6455 client on QTcpSocket (this Qt has no WebSockets module) |
| `src/ComfyProcess` | starts the local ComfyUIs hidden (main, speech), and stops one by its port |
| `src/Workflow` | the API-format graph, its parameters, the built-in graphs (image, music, sfx, speech, voice, video, video-h3, qwen, qwen-edit), recipes |
| `src/MainWindow`, `src/ImageView` | the window |
| `src/AudioPlayer` | plays a song with Windows' MCI (winmm) |
| `src/ParamHelp` | what each parameter does (hover, the ? list), English and 繁體中文 |
| `src/SongTools` | Singer / Voice tags, key steps, Transpose (ffmpeg) |
| `src/VideoTools` | a video's frames as numbered PNG files, and an MP4 of them (ffmpeg) |
| `src/Translator` | the English / 繁體中文 switch: the Traditional Chinese table |
| `src/HeadlessServer` | `--headless`, `--headless-test` and `--make` |
| `examples/` | recipes: a song, 14 sound effects (`sfx_*`), 8 speeches (`speech_*`, `voice_*`), 4 videos (`video_*`) |
