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

This makes one song, sound or speech and exits: 0 when the file was written, otherwise the codes above. It uses the
ComfyUI that answers, or starts the one the graph needs and leaves it running. `--song=` is the same option. The `make`
command of `--headless` does the same thing. Without `--out`, the file goes next to the JSON file as
`<name>_<date-time>.mp3`, with its settings file `<name>_<date-time>.json` beside it. An `out` whose settings file would
overwrite the input file is refused (code 2). In the window, **Open graph...** opens the same files.

The file can be one of three kinds:

- **A settings file:** the `.json` written next to every song, sound or speech. The same graph, values and seed: the
  same result again. Add `--random-seed` for a new take.
- **An API graph** from ComfyUI's **Export (API)**.
- **A recipe:** a short description for a built-in graph. `"graph"` picks it: `"sfx"`, `"speech"`, `"voice"`,
  `"music"` (or no `"graph"`: a song file) or `"image"`. [examples/](examples/) has 22 to try:

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

`--selftest` runs the built-in graph once. `--screenshot` draws the window off screen, after one generation if you
add `--generate`. These modes use Qt's `offscreen` platform, with fonts from `%WINDIR%\Fonts`.

## Files

| File | |
|---|---|
| `src/ComfyClient` | HTTP (`/object_info`, `/prompt`, `/interrupt`, `/queue`, `/view`) plus the WebSocket events and previews |
| `src/MiniWebSocket` | a small RFC 6455 client on QTcpSocket (this Qt has no WebSockets module) |
| `src/ComfyProcess` | starts the local ComfyUIs hidden (main, speech), and stops one by its port |
| `src/Workflow` | the API-format graph, its parameters, the built-in graphs (image, music, sfx, speech, voice), recipes |
| `src/MainWindow`, `src/ImageView` | the window |
| `src/AudioPlayer` | plays a song with Windows' MCI (winmm) |
| `src/ParamHelp` | what each parameter does (hover, the ? list), English and 繁體中文 |
| `src/SongTools` | Singer / Voice tags, key steps, Transpose (ffmpeg) |
| `src/Translator` | the English / 繁體中文 switch: the Traditional Chinese table |
| `src/HeadlessServer` | `--headless`, `--headless-test` and `--make` |
| `examples/` | recipes: a song, 14 sound effects (`sfx_*`), 8 speeches (`speech_*`, `voice_*`) |
