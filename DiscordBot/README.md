# ComfyLive Discord bot

[繁體中文](README.zh-TW.md)

Send the bot a message on Discord: it shows a menu, you pick what to make and fill in a form, and it makes the
picture, sound, speech, music or video with [ComfyLive](../ComfyLive/README.md) and replies with the file it made (MP4, MP3 or PNG).

```
Discord menu → form ──► bot (this folder) ──► ComfyLive --headless (TCP 8766) ──► ComfyUI (IP:8188)
       ▲                                                                              │
       └──────── reply with the result file ◄── jobs/<job>/ (recipe + results) ◄───────┘
```

## 1. Requirements

- **Node.js 22.4 or newer.** No `npm install`: the bot uses only what Node has built in (WebSocket, fetch, zlib).
- **ComfyLive built**, `ComfyLive/build/Release/ComfyLive.exe` (see [ComfyLive/README.md](../ComfyLive/README.md) "Build").
  The bot starts it with `--headless` when nothing answers on port 8766, and leaves it running.
- **A ComfyUI server** that ComfyLive can reach (`comfyLive.comfyServer`).
- **ffmpeg for videos**: `ffmpeg.exe` next to `ComfyLive.exe`, or on PATH. Without it a video comes back as frames
  only (`*_frames/frame_0001.png ...`), with no MP4. The Video model (Wan 2.2) makes no sound.
  **Video + sound** (LTX-2) needs no ffmpeg here: ComfyUI writes its MP4, and the server needs
  `checkpoints/ltx-2-19b-distilled.safetensors` and `text_encoders/gemma_3_12B_it.safetensors`.
- **Run the bot on the same PC as ComfyLive.** ComfyLive writes the results to the job folder, and the bot sends them from there.

## 2. Setup

1. Copy `config.example.json` to `config.json` and fill it in (section 4). `config.json` is git-ignored because it
   holds the bot token. The token can also come from the `DISCORD_BOT_TOKEN` environment variable.
2. Discord Developer Portal → your application → **Bot**: nothing extra to switch on. The bot reads only direct
   messages and messages that @mention it, and Discord sends their text without the privileged Message Content intent.
3. The bot must be in the server, able to read and send messages and **attach files** in the bot channel.
4. Start it: double-click `start_discord_bot.bat`, or `node src/main.mjs` (`--config=<file>` for another config file).

## 3. Using it

Send a **direct message** to the bot (any text), or **@mention** it in the bot channel (`channelId` / `commandChannelIds`).

1. **The menu.** The bot replies with buttons: 🖼️ Image · 🔊 Sound effect · 🗣️ Speech · 🎙️ Voice · 🎵 Music ·
   🎬 Video · 🎞️ Video + sound · 📊 Status · ❔ Text commands.
2. **The form.** A button opens a form with that graph's fields. Leave a field empty to use the graph's default.
   The form remembers what you entered last time (except the seed).

   | Form | Fields |
   |---|---|
   | Image | what to draw · avoid · size (`1024x1024`) · steps · seed |
   | Sound effect | what it sounds like · avoid · length in seconds · steps · seed |
   | Speech | what to say · speaker (Ryan, Aiden, Vivian, Serena, Uncle_fu, Dylan, Eric, Ono_anna, Sohee) · language · tone · seed |
   | Voice | what to say · the voice (a description) · language · temperature · seed |
   | Music | style tags · lyrics · length in seconds · BPM · key |
   | Video | what happens · avoid · length in seconds · size · seed (silent) |
   | Video + sound | what you see · what you hear · length (1 to 6 s) · size (`768x512`) · seed |

   **Video + sound** makes one MP4 with its soundtrack (LTX-2, about 1 to 2 minutes for 4 s). Describe the sound
   plainly ("loud hammer strikes on metal, a fire roars"): a vague or empty sound field gives an almost silent clip.
   The form refuses a size and length over about 768x512 for 4.5 s, which is what fits the server's 16 GB card.

3. **Submit.** The values are checked. A wrong one (e.g. size `big`) gets **✏️ Edit and try again**, which opens
   the form with your values. A correct form gets a status message: queued, then a progress bar, with a **✖️ Stop** button.
4. **The file.** When the job is done, the bot replies with the result itself (the MP4, MP3 or PNG, which Discord
   plays or shows in place) and mentions you. The recipe and settings `.json` files are not sent: they stay in the job
   folder on the bot PC. A video that came back as frames only (no MP4) is sent as `<job>_frames.zip`. The status
   message is removed. If something fails, the status message turns red with the reason.

Jobs run one at a time, in order. A job running longer than `jobs.timeoutMin` is stopped.

**Text commands** (skip the menu; ❔ on the menu lists them):

| Message | Makes |
|---|---|
| `image 512x512 steps=20 a cute baby` | a picture; `WxH` sets width and height, `key=value` sets any graph input |
| `sfx seconds=2 …` · `speech speaker=Ryan …` · `voice voice="…" …` · `music duration=60 …` | the same as the forms |
| `video seconds=3 a young archer draws a bow` + an attached picture | a video whose first frame is the picture (forms cannot take files) |
| `video-ltx seconds=4 sound="a loud thud" a knight hits a dummy` | a video with sound, as the Video + sound form (no size check) |
| `{"graph": "sfx", "prompt": "coins", "seconds": 1.5}` | a whole recipe as JSON (also inside a ```` ```json ```` block) |
| an attached `.json` | that recipe, e.g. any file from [ComfyLive/examples](../ComfyLive/examples); `key=value` in the message goes on top |
| `status` · `stop` | queue and server state · stop your job and cancel your waiting ones |

Recipe keys are ComfyLive's ([ComfyLive/README.md](../ComfyLive/README.md) "Recipes"). A recipe value equal to an
attached file's name becomes that file's path on the bot PC.

## 4. Config

| Key | Default | Meaning |
|---|---|---|
| `bDevMode` | `false` | `true`: debug lines in the log (every ComfyLive event), ComfyLive log lines in the status message, job folders never deleted |
| `discord.enabled`, `discord.commandsEnabled` | `true` | both must be `true` |
| `discord.botToken` | | the bot token (or `DISCORD_BOT_TOKEN`) |
| `discord.channelId` | | the bot channel; also used to find the server for role checks |
| `discord.commandAllowedUserIds` | `[]` | who may use the bot. Empty lists = nobody |
| `discord.commandAllowedRoleIds` | `[]` | ...or anyone with one of these roles (DMs too) |
| `discord.commandAllowEveryone` | `false` | `true`: anyone who can reach the bot |
| `discord.commandChannelIds` | `[channelId]` | channels where @mentions are answered |
| `discord.commandGuildId` | from `channelId` | the server for role checks |
| `discord.commandAllowDM` | `true` | answer direct messages |
| `discord.commandInChannel` | `true` | answer @mentions in the command channel(s) |
| `discord.maxUploadMB` | `10` | the server's upload limit (10 MB without boosts) |
| `comfyLive.host`, `comfyLive.port` | `127.0.0.1`, `8766` | where ComfyLive's headless server listens |
| `comfyLive.autoStart` | `true` | start ComfyLive when nothing answers |
| `comfyLive.exePath` | `../ComfyLive/build/Release/ComfyLive.exe` | relative to this folder |
| `comfyLive.comfyServer` | `127.0.0.1:8188` | `--server=` for the ComfyLive it starts |
| `comfyLive.speechServer` | ComfyLive's default (`127.0.0.1:8189`) | `--speech-server=`: the ComfyUI for speech/voice graphs |
| `comfyLive.extraArgs` | `[]` | more ComfyLive options, e.g. `["--comfy-root=C:/ComfyUI"]` |
| `comfyLive.startTimeoutSec` | `60` | how long to wait for a started ComfyLive |
| `jobs.folder` | `./jobs` | one folder per job |
| `jobs.timeoutMin` | `30` | a longer job is stopped |
| `jobs.maxQueue` | `10` | more waiting jobs are refused |
| `jobs.keepDays` | `7` | older job folders are deleted (`0` = keep) |

ComfyLive only takes `comfyServer`, `speechServer` and `extraArgs` on its command line. When the bot finds a
ComfyLive already running, it asks which ComfyUI addresses it uses (once per bot start). If they differ from the
config and `autoStart` is on, the bot shuts that ComfyLive down and starts it again with the config's addresses.
A change to `extraArgs` alone is not detected: close ComfyLive (or send it `shutdown`) to apply it.

## 5. Limits

- **Upload size.** A result over `maxUploadMB` is not sent: the reply gives its path on the bot PC.
- **Batches.** A graph that makes several pictures in one run returns only the last one (ComfyLive's `out` keeps the last).
- **Speech and voice** go to `speechServer`: a separate ComfyUI with the Qwen3-TTS nodes (port 8189). If the
  ComfyUI for a job is on another PC and does not answer, the job ends at once with "not running" and how to start it.
- **Speech speakers and languages** must be on Qwen3-TTS's lists (the form checks them). To describe a new voice
  ("a 2 year old boy"), use the Voice form.
- **One bot application, two programs.** If this bot uses the same token as the build machine (nodejsbuildmachine),
  both get every DM: the build machine answers with its menu and this bot starts a job. Use a separate bot
  application for this bot. This bot registers no slash commands, so it never changes the build machine's `/menu` and `/build`.

## 6. Files

| File | What it does |
|---|---|
| `src/main.mjs` | Starts everything |
| `src/config.mjs` | Loads `config.json` over the defaults |
| `src/discordGateway.mjs` | Discord connection, who may use the bot, messages in |
| `src/discordRest.mjs` | Discord REST: messages, edits, file uploads |
| `src/forms.mjs` | The menu, one form per graph, form → recipe, each person's last values |
| `src/ui.mjs` | What a button click or a submitted form does |
| `src/request.mjs` | A text message → a recipe, the menu, status or stop |
| `src/jobs.mjs` | Queue, job folder, ComfyLive `make`, progress, reply with the result |
| `src/comfyLive.mjs` | ComfyLive headless client; starts ComfyLive when needed |
| `src/zipWriter.mjs` | Writes the frames zip (from the build machine's `src/zipWriter.ts`) |
| `data/formValues.json` | Each person's last form values (git-ignored) |
| `logs/` | `bot-YYYYMMDD.log`, and `comfylive.log` for a ComfyLive the bot started |
