# ComfyLive Discord 機器人

[English](README.md)

在 Discord 傳訊息給機器人：它會顯示選單，你選擇要產生什麼並填寫表單，它再用 [ComfyLive](../ComfyLive/README.md)
產生圖片、音效、語音、音樂或影片，並回覆它產生的檔案（MP4、MP3 或 PNG）。

```
Discord 選單 → 表單 ──► 機器人（本資料夾）──► ComfyLive --headless（TCP 8766）──► ComfyUI（IP:8188）
     ▲                                                                              │
     └──────── 回覆結果檔案 ◄── jobs/<job>/（配方＋結果）◄──────────────────────────┘
```

## 1. 需求

- **Node.js 22.4 以上。** 不需要 `npm install`：機器人只用 Node 內建的功能（WebSocket、fetch、zlib）。
- **已建置的 ComfyLive**，`ComfyLive/build/Release/ComfyLive.exe`（見 [ComfyLive/README.md](../ComfyLive/README.md)「Build」）。
  若 8766 埠沒有回應，機器人會以 `--headless` 啟動它，並讓它持續執行。
- **一台 ComfyLive 連得到的 ComfyUI 伺服器**（`comfyLive.comfyServer`）。
- **影片需要 ffmpeg**：`ffmpeg.exe` 放在 `ComfyLive.exe` 旁邊，或在 PATH 上。沒有它，影片只會以影格回傳
  （`*_frames/frame_0001.png ...`），沒有 MP4。Video 的模型（Wan 2.2）不會產生聲音。
  **Video + sound**（LTX-2）在這裡不需要 ffmpeg：MP4 由 ComfyUI 產生；伺服器需要
  `checkpoints/ltx-2-19b-distilled.safetensors` 和 `text_encoders/gemma_3_12B_it.safetensors`。
- **機器人要和 ComfyLive 在同一台電腦上執行。** ComfyLive 把結果寫進工作資料夾，機器人再從那裡傳送。

## 2. 設定步驟

1. 把 `config.example.json` 複製成 `config.json` 並填寫（第 4 節）。`config.json` 含有機器人 token，所以已列入
   git 忽略清單。token 也可以改由環境變數 `DISCORD_BOT_TOKEN` 提供。
2. Discord Developer Portal → 你的應用程式 → **Bot**：不需要另外開啟任何設定。機器人只讀私訊和 @提及它的訊息，
   這兩種訊息的內容不需要特權的 Message Content intent，Discord 就會送來。
3. 機器人必須在伺服器裡，並能在機器人頻道讀取、發送訊息及**附加檔案**。
4. 啟動：雙擊 `start_discord_bot.bat`，或執行 `node src/main.mjs`（用 `--config=<檔案>` 指定其他設定檔）。

## 3. 使用方式

**私訊**機器人（任何文字），或在機器人頻道（`channelId` / `commandChannelIds`）**@提及**它。

1. **選單。** 機器人回覆一排按鈕：🖼️ Image · 🔊 Sound effect · 🗣️ Speech · 🎙️ Voice · 🎵 Music ·
   🎬 Video · 🎞️ Video + sound · 📊 Status · ❔ Text commands。
2. **表單。** 按下按鈕會開啟該圖譜的表單。欄位留空即使用圖譜的預設值。表單會記住你上次填的內容（種子除外）。

   | 表單 | 欄位 |
   |---|---|
   | Image | 要畫什麼 · 避免 · 尺寸（`1024x1024`）· 步數 · 種子 |
   | Sound effect | 聲音描述 · 避免 · 長度（秒）· 步數 · 種子 |
   | Speech | 要說的話 · 說話者（Ryan、Aiden、Vivian、Serena、Uncle_fu、Dylan、Eric、Ono_anna、Sohee）· 語言 · 語氣 · 種子 |
   | Voice | 要說的話 · 聲音描述 · 語言 · temperature · 種子 |
   | Music | 風格標籤 · 歌詞 · 長度（秒）· BPM · 調性 |
   | Video | 畫面內容 · 避免 · 長度（秒）· 尺寸 · 種子（無聲） |
   | Video + sound | 看到什麼 · 聽到什麼 · 長度（1 到 6 秒）· 尺寸（`768x512`）· 種子 |

   **Video + sound** 會產生一個含音軌的 MP4（LTX-2，4 秒約需 1 到 2 分鐘）。聲音要寫得明確
   （「loud hammer strikes on metal, a fire roars」）：聲音欄位含糊或留空，影片幾乎會沒有聲音。
   尺寸乘以長度超過約 768x512、4.5 秒時，表單會拒絕，因為那是伺服器 16 GB 顯示卡容得下的量。

3. **送出（Submit）。** 系統會檢查數值。若有錯（例如尺寸填 `big`），會出現 **✏️ Edit and try again**，
   按下後以你填過的值重新開啟表單。正確的表單會產生一則狀態訊息：排隊中，接著顯示進度條，並附有 **✖️ Stop** 按鈕。
4. **檔案。** 工作完成後，機器人直接回覆結果檔案（MP4、MP3 或 PNG，Discord 會直接播放或顯示）並提及你。
   配方和設定的 `.json` 檔不會傳送，而是留在機器人電腦的工作資料夾裡。只以影格回傳（沒有 MP4）的影片會以
   `<job>_frames.zip` 傳送。狀態訊息隨之刪除。
   若失敗，狀態訊息會變成紅色並顯示原因。

工作依序一次執行一個。執行超過 `jobs.timeoutMin` 的工作會被停止。

**文字指令**（略過選單；選單上的 ❔ 會列出它們）：

| 訊息 | 產生 |
|---|---|
| `image 512x512 steps=20 a cute baby` | 一張圖片；`寬x高` 設定尺寸，`key=value` 可設定圖譜的任何輸入 |
| `sfx seconds=2 …` · `speech speaker=Ryan …` · `voice voice="…" …` · `music duration=60 …` | 與表單相同 |
| `video seconds=3 a young archer draws a bow` ＋ 附加一張圖片 | 以該圖片為第一格的影片（表單無法附加檔案） |
| `video-ltx seconds=4 sound="a loud thud" a knight hits a dummy` | 有聲音的影片，同 Video + sound 表單（不檢查尺寸） |
| `{"graph": "sfx", "prompt": "coins", "seconds": 1.5}` | 整份 JSON 配方（也可以放在 ```` ```json ```` 區塊裡） |
| 附加一個 `.json` | 使用該配方，例如 [ComfyLive/examples](../ComfyLive/examples) 裡的任何檔案；訊息中的 `key=value` 會覆蓋它 |
| `status` · `stop` | 佇列與伺服器狀態 · 停止你的工作並取消你等待中的工作 |

配方的鍵與 ComfyLive 相同（[ComfyLive/README.md](../ComfyLive/README.md)「Recipes」）。配方中的值若等於某個附加檔案的檔名，
會換成該檔案在機器人電腦上的路徑。

## 4. 設定

| 鍵 | 預設值 | 說明 |
|---|---|---|
| `bDevMode` | `false` | `true`：紀錄中加入除錯行（每個 ComfyLive 事件）、狀態訊息顯示 ComfyLive 紀錄、永不刪除工作資料夾 |
| `discord.enabled`、`discord.commandsEnabled` | `true` | 兩者都必須是 `true` |
| `discord.botToken` | | 機器人 token（或 `DISCORD_BOT_TOKEN`） |
| `discord.channelId` | | 機器人頻道；也用來找出檢查身分組所需的伺服器 |
| `discord.commandAllowedUserIds` | `[]` | 可使用機器人的人。清單皆空 = 沒有人 |
| `discord.commandAllowedRoleIds` | `[]` | ……或擁有這些身分組之一的人（私訊也適用） |
| `discord.commandAllowEveryone` | `false` | `true`：任何能聯絡到機器人的人 |
| `discord.commandChannelIds` | `[channelId]` | 會回應 @提及的頻道 |
| `discord.commandGuildId` | 由 `channelId` 查出 | 檢查身分組用的伺服器 |
| `discord.commandAllowDM` | `true` | 回應私訊 |
| `discord.commandInChannel` | `true` | 回應指令頻道中的 @提及 |
| `discord.maxUploadMB` | `10` | 伺服器的上傳上限（沒有加成時為 10 MB） |
| `comfyLive.host`、`comfyLive.port` | `127.0.0.1`、`8766` | ComfyLive headless 伺服器的位址 |
| `comfyLive.autoStart` | `true` | 沒有回應時啟動 ComfyLive |
| `comfyLive.exePath` | `../ComfyLive/build/Release/ComfyLive.exe` | 相對於本資料夾 |
| `comfyLive.comfyServer` | `127.0.0.1:8188` | 啟動 ComfyLive 時的 `--server=` |
| `comfyLive.speechServer` | ComfyLive 預設（`127.0.0.1:8189`） | `--speech-server=`：語音（speech/voice）圖譜用的 ComfyUI |
| `comfyLive.extraArgs` | `[]` | 其他 ComfyLive 參數，例如 `["--comfy-root=C:/ComfyUI"]` |
| `comfyLive.startTimeoutSec` | `60` | 等待啟動中的 ComfyLive 的時間 |
| `jobs.folder` | `./jobs` | 每個工作一個資料夾 |
| `jobs.timeoutMin` | `30` | 超過此時間的工作會被停止 |
| `jobs.maxQueue` | `10` | 超過此數量的等待工作會被拒絕 |
| `jobs.keepDays` | `7` | 刪除更舊的工作資料夾（`0` = 保留） |

ComfyLive 只從命令列取得 `comfyServer`、`speechServer` 和 `extraArgs`。機器人發現 ComfyLive 已在執行時，
會詢問它使用的 ComfyUI 位址（每次機器人啟動時一次）。若與設定不同且 `autoStart` 開啟，機器人會關閉該 ComfyLive，
並以設定中的位址重新啟動。只改 `extraArgs` 不會被偵測到：請關閉 ComfyLive（或送出 `shutdown`）來套用。

## 5. 限制

- **上傳大小。** 超過 `maxUploadMB` 的結果不會傳送，回覆會提供它在機器人電腦上的路徑。
- **批次。** 一次產生多張圖片的圖譜只會回傳最後一張（ComfyLive 的 `out` 保留最後一張）。
- **語音（speech / voice）** 會送到 `speechServer`：另一個裝有 Qwen3-TTS 節點的 ComfyUI（8189 埠）。若工作所用的
  ComfyUI 在另一台電腦上且沒有回應，工作會立即結束，顯示「not running」及啟動方式。
- **Speech 的說話者和語言** 必須在 Qwen3-TTS 的清單內（表單會檢查）。要描述新的聲音（「一個兩歲男孩」），
  請改用 Voice 表單。
- **同一個機器人應用程式、兩個程式。** 若本機器人與建置機（nodejsbuildmachine）使用同一個 token，
  兩者都會收到每一則私訊：建置機會回覆它的選單，本機器人則會開始一項工作。請為本機器人另外建立一個機器人應用程式。
  本機器人不註冊任何斜線指令，所以不會改動建置機的 `/menu` 和 `/build`。

## 6. 檔案

| 檔案 | 用途 |
|---|---|
| `src/main.mjs` | 啟動全部功能 |
| `src/config.mjs` | 以 `config.json` 覆蓋預設值 |
| `src/discordGateway.mjs` | Discord 連線、使用權限、接收訊息 |
| `src/discordRest.mjs` | Discord REST：發送、編輯訊息、上傳檔案 |
| `src/forms.mjs` | 選單、每個圖譜的表單、表單 → 配方、每個人上次填的值 |
| `src/ui.mjs` | 按下按鈕或送出表單後要做的事 |
| `src/request.mjs` | 文字訊息 → 配方、選單、status 或 stop |
| `src/jobs.mjs` | 佇列、工作資料夾、ComfyLive `make`、進度、回覆結果 |
| `src/comfyLive.mjs` | ComfyLive headless 用戶端；需要時啟動 ComfyLive |
| `src/zipWriter.mjs` | 寫出影格 zip（取自建置機的 `src/zipWriter.ts`） |
| `data/formValues.json` | 每個人上次填的表單值（已列入 git 忽略） |
| `logs/` | `bot-YYYYMMDD.log`，以及機器人啟動的 ComfyLive 的 `comfylive.log` |
