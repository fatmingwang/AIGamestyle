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

- **ComfyUI**: the server address (default `127.0.0.1:8188`). **Start local ComfyUI** runs
  `../_comfyui/ComfyUI` hidden, with `--preview-method auto` (without that, ComfyUI sends no previews), and its log
  goes to `../_comfyui/comfylive_server.log`. The ComfyUI that ComfyLive started is stopped when it closes, unless
  you untick that option. `ComfyLive.exe --start` starts it when nothing answers.
- **Graph**: **Built-in** offers two graphs, SDXL text to image or ACE-Step 1.5 text to music. **Open graph...**
  loads any graph saved from ComfyUI with
  **Export (API)**. The UI format (a `"nodes"` array) is refused with that hint. Every input that is not a link
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

**Language** (top left): English or 繁體中文. The window switches at once and keeps its connection, graph, values,
picture and log, and the choice is remembered. The first start follows Windows: Chinese Windows starts in 繁體中文.
`--lang=en` or `--lang=zh_TW` picks the language for one run. The Chinese text is a table built into the exe
(`src/Translator.cpp`), so there are no `.qm` files to ship; a text missing from it stays English. The ComfyUI input
names (`seed`, `steps`, `tags` ...), ComfyUI's own log lines and the headless protocol stay English, because those are
what ComfyUI and scripts use.

The settings, the graph and its values are kept between runs (QSettings `MyWork/ComfyLive`).

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
| `comfyui-status` | `server`? | Is a ComfyUI answering? result `{reachable, server, connected}`; code 1 if not |
| `comfyui-launch` | `comfy-root`?, `custom-nodes`? (true) | Starts the local ComfyUI hidden (nothing if one answers) and ends once it is up |
| `connect` | `server`? | Reads the node list and opens the WebSocket. result `{server, nodeTypes}` |
| `get-params` | | result `{graph, params: [{key, node, input, group, kind, value, choices?, min?, max?}]}` |
| `set-params` | `params: {"<node>/<input>": v}` or `{"<input>": v}` | A bare input name sets every input of that name (`"steps"`). Unknown names and values outside the server's list are refused (code 2) |
| `load-graph` | `path`, or `graph` (API-format object) + `name`? | Replaces the graph |
| `save-graph` | `path` | Saves the graph with its current values |
| `reset-graph` | `graph`? `"image"` (default) or `"music"` | A built-in graph: SDXL text to image, or ACE-Step 1.5 text to music |
| `generate` | `params`?, `random-seed`?, `out`?, `preview-dir`? | Runs the graph. Events: `queued {promptId}`, `progress {value,max}`, `preview {index,width,height,path?}`, `image {file,width,height,path?}` or `audio {file,format,bytes,path?}`, then result `{seconds, previews, output}`. For audio, `out` takes the real format's extension (`song.wav` from an MP3 graph is written as `song.mp3`) |
| `transpose` | `path`, `semitones` (−12…12), `out`? | Transposes an audio file with ffmpeg (same melody, same length). result `{path}`; default out `<name>_+Nst.<ext>` |
| `stop` | | Stops the running generate. It is answered at once, beside it |
| `exit` / `quit` | | Closes this connection; the server keeps running |

`get-params`, `set-params` and `generate` connect first when needed. Without a ComfyUI they end with code 4 ("send
comfyui-launch first").

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

The headless server only stops a ComfyUI that it started itself, and only when it exits.

## Checks (nothing appears on the screen)

```
ComfyLive.exe --headless-test [--start] [--music] [--out=headless_test.png]
```

This runs the server and a built-in client in one process. Without a ComfyUI it checks only the protocol (status, an
unknown command). With one, or with `--start`, it also runs connect, get-params, a refused and an accepted
set-params, and a real 768², 8-step generate. `--music` adds the ACE-Step graph: a 20-second song saved as
`headless_test_song.mp3`, then transposed +2 (`headless_test_song_+2st.mp3`). It prints `ALL CHECKS PASSED` (exit 0) or `SOME CHECKS FAILED`
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
| `src/ComfyProcess` | starts and stops the local ComfyUI hidden |
| `src/Workflow` | the API-format graph, its parameters, the built-in graphs (image, music) |
| `src/MainWindow`, `src/ImageView` | the window |
| `src/AudioPlayer` | plays a song with Windows' MCI (winmm) |
| `src/ParamHelp` | what each parameter does (hover, the ? list), English and 繁體中文 |
| `src/SongTools` | Singer / Voice tags, key steps, Transpose (ffmpeg) |
| `src/Translator` | the English / 繁體中文 switch: the Traditional Chinese table |
| `src/HeadlessServer` | `--headless` and `--headless-test` |
