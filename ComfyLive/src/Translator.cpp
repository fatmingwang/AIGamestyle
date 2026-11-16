#include "Translator.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QTranslator>

namespace {

// English source text (exactly as written in tr()) -> Traditional Chinese. Keep the %1 %2 markers.
const QHash<QString, QString>& zhTw()
{
    static const QHash<QString, QString> table = {
        // window
        {QStringLiteral("ComfyLive: ComfyUI in real time"), QStringLiteral("ComfyLive：即時 ComfyUI")},
        {QStringLiteral("Language"), QStringLiteral("語言")},
        {QStringLiteral("ComfyUI"), QStringLiteral("ComfyUI")},
        {QStringLiteral("Server"), QStringLiteral("伺服器")},
        {QStringLiteral("ComfyUI folder"), QStringLiteral("ComfyUI 資料夾")},
        {QStringLiteral("The local ComfyUI folder (main.py and its .venv), for Start"), QStringLiteral("本機 ComfyUI 資料夾（main.py 和它的 .venv），「啟動」時使用")},
        {QStringLiteral("Load custom nodes (slower start; needed by graphs that use them)"), QStringLiteral("載入自訂節點（啟動較慢；用到它們的流程需要）")},
        {QStringLiteral("Stop the ComfyUI I started when closing"), QStringLiteral("關閉時停止我啟動的 ComfyUI")},
        {QStringLiteral("Connect"), QStringLiteral("連線")},
        {QStringLiteral("Reconnect"), QStringLiteral("重新連線")},
        {QStringLiteral("Start local ComfyUI"), QStringLiteral("啟動本機 ComfyUI")},
        {QStringLiteral("Open graph..."), QStringLiteral("開啟流程…")},
        {QStringLiteral("A ComfyUI graph saved with Workflow > Export (API)"), QStringLiteral("用 ComfyUI 的「工作流程 > 匯出（API）」存的流程")},
        {QStringLiteral("Save graph..."), QStringLiteral("儲存流程…")},
        {QStringLiteral("Built-in"), QStringLiteral("內建")},
        {QStringLiteral("A built-in graph: SDXL text to image, ACE-Step 1.5 text to music, Stable Audio Open text to sound effect, or Qwen3-TTS text to speech"), QStringLiteral("內建流程：SDXL 文字生圖、ACE-Step 1.5 文字生音樂、Stable Audio Open 文字生音效，或 Qwen3-TTS 文字轉語音")},
        {QStringLiteral("A built-in graph: SDXL text to image, ACE-Step 1.5 text to music, Stable Audio Open text to sound effect, Qwen3-TTS text to speech, or Wan 2.2 / HyperFlow H3 Flash text to video"), QStringLiteral("內建流程：SDXL 文字生圖、ACE-Step 1.5 文字生音樂、Stable Audio Open 文字生音效、Qwen3-TTS 文字轉語音，或 Wan 2.2 / HyperFlow H3 Flash 文字生影片")},
        {QStringLiteral("Picture to video (Wan 2.2 TI2V-5B)"), QStringLiteral("圖片生影片（Wan 2.2 TI2V-5B）")},
        {QStringLiteral("Picture to video (HyperFlow H3 Flash Q3_K_M)"), QStringLiteral("圖片生影片（HyperFlow H3 Flash Q3_K_M）")},
        {QStringLiteral("Reference picture"), QStringLiteral("參考圖片")},
        {QStringLiteral("Delete result..."), QStringLiteral("刪除結果…")},
        {QStringLiteral("Delete result"), QStringLiteral("刪除結果")},
        {QStringLiteral("Move the files of the result picked in the list (the one on screen) to the Recycle Bin: its picture, or its frames, MP4 and settings, or its sound, and ComfyUI's own copy in its output folder"), QStringLiteral("把清單中選取的結果（畫面上的那個）的檔案移到資源回收筒：它的圖片，或影格、MP4 和設定，或聲音，以及 ComfyUI 輸出資料夾裡的那一份")},
        {QStringLiteral("... and %1 more"), QStringLiteral("…還有 %1 個")},
        {QStringLiteral("Its files are no longer there. Take this result out of the list?"), QStringLiteral("它的檔案已經不在了。要把這個結果從清單移除嗎？")},
        {QStringLiteral("Move this result's %1 files and folders to the Recycle Bin?\n\n%2"), QStringLiteral("要把這個結果的 %1 個檔案和資料夾移到資源回收筒嗎？\n\n%2")},
        {QStringLiteral("moved to the Recycle Bin: %1"), QStringLiteral("已移到資源回收筒：%1")},
        {QStringLiteral("could not delete %1 (is it open in another program?)"), QStringLiteral("無法刪除 %1（是不是被別的程式開著？）")},
        {QStringLiteral("deleted"), QStringLiteral("已刪除")},
        {QStringLiteral("peak GPU memory: %1 of %2 GB (%3 x %4 x %5)"), QStringLiteral("GPU 記憶體峰值：%1 / %2 GB（%3 x %4 x %5）")},
        {QStringLiteral("%1 x %2, %3 frames (%4 s)"), QStringLiteral("%1 x %2，%3 格（%4 秒）")},
        {QStringLiteral("%1 x %2, %3 pictures"), QStringLiteral("%1 x %2，%3 張")},
        {QStringLiteral("%1 x %2 x %3: FAILED at %4 of %5 GB"), QStringLiteral("%1 x %2 x %3：失敗，當時 %4 / %5 GB")},
        {QStringLiteral("%1 x %2 x %3: peak %4 of %5 GB, %6 s"), QStringLiteral("%1 x %2 x %3：峰值 %4 / %5 GB，%6 秒")},
        {QStringLiteral("This size was run: "), QStringLiteral("這個大小跑過：")},
        {QStringLiteral("Not run at this size yet. Nearest run: "), QStringLiteral("這個大小還沒跑過。最接近的一次：")},
        {QStringLiteral("This model was not run on this PC yet: the first run measures it."), QStringLiteral("這個模型還沒在這台電腦上跑過：第一次執行就會量到。")},
        {QStringLiteral("Largest that worked: "), QStringLiteral("成功過的最大值：")},
        {QStringLiteral("This is %1 times that: untested, it may be slow or fail."), QStringLiteral("這是它的 %1 倍：沒有試過，可能很慢或失敗。")},
        {QStringLiteral("Smallest that failed: "), QStringLiteral("失敗過的最小值：")},
        {QStringLiteral("This size is as large or larger: expect it to fail."), QStringLiteral("這個大小和它一樣大或更大：預期會失敗。")},
        {QStringLiteral("Card: %1 GB, %2 GB in use now"), QStringLiteral("顯示卡：%1 GB，目前使用 %2 GB")},
        {QStringLiteral("Card: %1 GB"), QStringLiteral("顯示卡：%1 GB")},
        {QStringLiteral("ComfyUI's own frames: %1 ... (%2 files)"), QStringLiteral("ComfyUI 自己的影格：%1…（%2 個檔案）")},
        {QStringLiteral("ComfyUI's own file: %1"), QStringLiteral("ComfyUI 自己的檔案：%1")},
        {QStringLiteral("%1 (on the ComfyUI server %2)"), QStringLiteral("%1（在 ComfyUI 伺服器 %2 上）")},
        {QStringLiteral("Text to image (Qwen Image 2.1 Turbo)"), QStringLiteral("文字生圖（Qwen Image 2.1 Turbo）")},
        {QStringLiteral("Text to image (Qwen Image 2.1 Q4_K_M, 25 steps)"), QStringLiteral("文字生圖（Qwen Image 2.1 Q4_K_M，25 步）")},
        {QStringLiteral("Edit pictures (Qwen Image 2.1 Q4_K_M, 25 steps)"), QStringLiteral("編輯圖片（Qwen Image 2.1 Q4_K_M，25 步）")},
        {QStringLiteral("Text to image (Qwen Image 2.1 Turbo Q6_K)"), QStringLiteral("文字生圖（Qwen Image 2.1 Turbo Q6_K）")},
        {QStringLiteral("Edit pictures (Qwen Image 2.1 Turbo Q6_K)"), QStringLiteral("編輯圖片（Qwen Image 2.1 Turbo Q6_K）")},
        {QStringLiteral("Edit pictures (Qwen Image 2.1 Turbo)"), QStringLiteral("編輯圖片（Qwen Image 2.1 Turbo）")},
        {QStringLiteral("Picture"), QStringLiteral("圖片")},
        {QStringLiteral("Edit"), QStringLiteral("編輯")},
        {QStringLiteral("Cache"), QStringLiteral("快取")},
        {QStringLiteral("Picture 1"), QStringLiteral("圖片 1")},
        {QStringLiteral("Picture 2 (optional)"), QStringLiteral("圖片 2（可不填）")},
        {QStringLiteral("Picture 3 (optional)"), QStringLiteral("圖片 3（可不填）")},
        {QStringLiteral("Picture 4 (optional)"), QStringLiteral("圖片 4（可不填）")},
        {QStringLiteral("a picture file on this PC"), QStringLiteral("這台電腦上的圖片檔")},
        {QStringLiteral("Remove background"), QStringLiteral("移除背景")},
        {QStringLiteral("on: the frames are PNG files with only the character (transparent background)"), QStringLiteral("開啟：影格是只有角色的 PNG 檔（背景透明）")},
        {QStringLiteral("Off: the frames keep the video's background. The MP4 always has a grey background."), QStringLiteral("關閉：影格保留影片的背景。MP4 一律是灰色背景。")},
        {QStringLiteral("picture"), QStringLiteral("圖片")},
        {QStringLiteral("a picture of the character: whole body, plain background"), QStringLiteral("角色的圖片：全身、單色背景")},
        {QStringLiteral("reference picture: %1 x %2"), QStringLiteral("參考圖片：%1 x %2")},
        {QStringLiteral("Browse..."), QStringLiteral("瀏覽…")},
        {QStringLiteral("The reference picture"), QStringLiteral("參考圖片")},
        {QStringLiteral("Pictures (*.png *.jpg *.jpeg *.webp *.bmp)"), QStringLiteral("圖片 (*.png *.jpg *.jpeg *.webp *.bmp)")},
        {QStringLiteral("Text to video (HyperFlow H3 Flash)"), QStringLiteral("文字生影片（HyperFlow H3 Flash）")},
        {QStringLiteral("First frame to video (Wan 2.2 TI2V-5B)"), QStringLiteral("第一格生影片（Wan 2.2 TI2V-5B）")},
        {QStringLiteral("First frame to video (HyperFlow H3 Flash)"), QStringLiteral("第一格生影片（HyperFlow H3 Flash）")},
        {QStringLiteral("Reference pictures to video (HyperFlow H3 Flash)"), QStringLiteral("參考圖片生影片（HyperFlow H3 Flash）")},
        {QStringLiteral("Audio VAE"), QStringLiteral("音訊 VAE")},
        {QStringLiteral("Steps"), QStringLiteral("步數")},
        {QStringLiteral("Seed"), QStringLiteral("種子")},
        {QStringLiteral("Guider"), QStringLiteral("引導")},
        {QStringLiteral("Sample"), QStringLiteral("取樣")},
        {QStringLiteral("Text to video (Wan 2.2 TI2V-5B)"), QStringLiteral("文字生影片（Wan 2.2 TI2V-5B）")},
        {QStringLiteral("Video"), QStringLiteral("影片")},
        {QStringLiteral("Size and length"), QStringLiteral("大小和長度")},
        {QStringLiteral("VAE"), QStringLiteral("VAE")},
        {QStringLiteral("Pause"), QStringLiteral("暫停")},
        {QStringLiteral("One frame back"), QStringLiteral("上一格")},
        {QStringLiteral("One frame on"), QStringLiteral("下一格")},
        {QStringLiteral("Open frames folder"), QStringLiteral("開啟影格資料夾")},
        {QStringLiteral("The video's frames as PNG files (frame_0001.png ...), and its MP4 beside the folder"), QStringLiteral("影片的影格 PNG 檔（frame_0001.png…），MP4 在資料夾旁邊")},
        {QStringLiteral("Save video..."), QStringLiteral("儲存影片…")},
        {QStringLiteral("The MP4, its frames in <name>_frames and its settings .json, wherever you want"), QStringLiteral("把 MP4、<名稱>_frames 裡的影格和設定 .json 存到你要的地方")},
        {QStringLiteral("Save the video"), QStringLiteral("儲存影片")},
        {QStringLiteral("Video (*.mp4)"), QStringLiteral("影片 (*.mp4)")},
        {QStringLiteral("reading %1 frames ..."), QStringLiteral("正在讀取 %1 格…")},
        {QStringLiteral("reading %1 pictures ..."), QStringLiteral("正在讀取 %1 張圖…")},
        {QStringLiteral("%1: %2 frames, %3 x %4, %5 s"), QStringLiteral("%1：%2 格，%3 x %4，%5 秒")},
        {QStringLiteral("video: %1"), QStringLiteral("影片：%1")},
        {QStringLiteral("frames: %1"), QStringLiteral("影格：%1")},
        {QStringLiteral("video file: %1"), QStringLiteral("影片檔：%1")},
        {QStringLiteral("no MP4 (the frames are kept): %1"), QStringLiteral("沒有做出 MP4（影格已保留）：%1")},
        {QStringLiteral("frame %1 / %2   %3 s"), QStringLiteral("第 %1 / %2 格   %3 秒")},
        {QStringLiteral("the video's frames could not be read"), QStringLiteral("無法讀取影片的影格")},
        {QStringLiteral("no frames in %1"), QStringLiteral("%1 裡沒有影格")},
        {QStringLiteral("no MP4 was made (ffmpeg): only the frames were saved"), QStringLiteral("沒有做出 MP4（ffmpeg）：只儲存了影格")},
        {QStringLiteral("ComfyUI API graph, a recipe, or a song's, sound's or video's settings (*.json)"), QStringLiteral("ComfyUI API 流程、配方，或歌曲、音效、影片的設定 (*.json)")},
        {QStringLiteral("Text to speech, preset voice (Qwen3-TTS)"), QStringLiteral("文字轉語音，預設聲音（Qwen3-TTS）")},
        {QStringLiteral("Text to speech, voice from a description (Qwen3-TTS)"), QStringLiteral("文字轉語音，用文字描述聲音（Qwen3-TTS）")},
        {QStringLiteral("Stop ComfyUI"), QStringLiteral("停止 ComfyUI")},
        {QStringLiteral("Stop the ComfyUIs on this PC (Server and Speech server), whoever started them"), QStringLiteral("停止本機的 ComfyUI（伺服器和語音伺服器，不論是誰啟動的）")},
        {QStringLiteral("Speech server"), QStringLiteral("語音伺服器")},
        {QStringLiteral("The speech ComfyUI (its own .venv-speech, Qwen3-TTS only): speech graphs run here, every other graph on Server"), QStringLiteral("語音用的 ComfyUI（自己的 .venv-speech，只載入 Qwen3-TTS）：語音流程在這裡執行，其他流程在「伺服器」")},
        {QStringLiteral("Connecting to %1 ..."), QStringLiteral("正在連線到 %1…")},
        {QStringLiteral("this graph runs on %1"), QStringLiteral("這個流程在 %1 執行")},
        {QStringLiteral("Off (the default): ComfyUI keeps running after ComfyLive closes, so the next start is instant. Stop it with Stop ComfyUI, or ComfyLive.exe --comfy-stop"), QStringLiteral("關閉（預設）：ComfyLive 關閉後 ComfyUI 繼續執行，下次開啟馬上可用。要停止請按「停止 ComfyUI」，或執行 ComfyLive.exe --comfy-stop")},
        {QStringLiteral("Stop only works for a ComfyUI on this PC (127.0.0.1)."), QStringLiteral("「停止」只能用於本機（127.0.0.1）的 ComfyUI。")},
        {QStringLiteral("a recipe for the built-in graph: its values are in the form; Generate makes it"), QStringLiteral("內建流程的配方：數值已填入表單；按「生成」即可做出")},
        {QStringLiteral("Speech"), QStringLiteral("語音")},
        {QStringLiteral("Text to image (SDXL)"), QStringLiteral("文字生圖（SDXL）")},
        {QStringLiteral("Text to music (ACE-Step 1.5)"), QStringLiteral("文字生音樂（ACE-Step 1.5）")},
        {QStringLiteral("Text to sound effect (Stable Audio Open)"), QStringLiteral("文字生音效（Stable Audio Open）")},
        {QStringLiteral("Connect to see the parameters."), QStringLiteral("連線後才會顯示參數。")},
        {QStringLiteral("Run"), QStringLiteral("執行")},
        {QStringLiteral("Live"), QStringLiteral("即時")},
        {QStringLiteral("Generate again by itself after every change (a run that is out of date is stopped)"), QStringLiteral("每次修改後自動重新生成（過時的執行會被停止）")},
        {QStringLiteral(" ms after a change"), QStringLiteral(" 毫秒（修改後）")},
        {QStringLiteral("New seed each run"), QStringLiteral("每次換新種子")},
        {QStringLiteral("Generate  (Ctrl+Enter)"), QStringLiteral("生成  (Ctrl+Enter)")},
        {QStringLiteral("Stop"), QStringLiteral("停止")},
        {QStringLiteral("Save picture..."), QStringLiteral("儲存圖片…")},
        {QStringLiteral("The last results: click one to show it"), QStringLiteral("最近的結果：點一下顯示")},
        {QStringLiteral("Play"), QStringLiteral("播放")},
        {QStringLiteral("Open in player"), QStringLiteral("用播放器開啟")},
        {QStringLiteral("Open the audio in Windows' own player (for a format ComfyLive cannot play)"), QStringLiteral("用 Windows 的播放器開啟音訊（ComfyLive 不能播放的格式）")},
        {QStringLiteral("Save audio..."), QStringLiteral("儲存音訊…")},
        {QStringLiteral("Random"), QStringLiteral("隨機")},
        {QStringLiteral("Graph: <b>%1</b> (%2 nodes)"), QStringLiteral("流程：<b>%1</b>（%2 個節點）")},
        {QStringLiteral("Graph: <b>%1</b>"), QStringLiteral("流程：<b>%1</b>")},
        {QStringLiteral("last graph"), QStringLiteral("上次的流程")},
        {QStringLiteral("No picture yet: connect, then Generate (or turn on Live)."), QStringLiteral("還沒有圖片：先連線，再按「生成」（或開啟「即時」）。")},
        // status and progress
        {QStringLiteral("ComfyUI did not start"), QStringLiteral("ComfyUI 沒有啟動")},
        {QStringLiteral("Connected to %1"), QStringLiteral("已連線到 %1")},
        {QStringLiteral("Not connected"), QStringLiteral("未連線")},
        {QStringLiteral("Connection lost: retrying ..."), QStringLiteral("連線中斷：重試中…")},
        {QStringLiteral("Looking for ComfyUI at %1 ..."), QStringLiteral("正在 %1 尋找 ComfyUI…")},
        {QStringLiteral("No ComfyUI at %1: press Start local ComfyUI, or start it yourself"), QStringLiteral("%1 沒有 ComfyUI：按「啟動本機 ComfyUI」，或自行啟動")},
        {QStringLiteral("Connecting ..."), QStringLiteral("連線中…")},
        {QStringLiteral("Starting ComfyUI: the first start can take a minute ..."), QStringLiteral("正在啟動 ComfyUI：第一次啟動可能要一分鐘…")},
        {QStringLiteral("queued ..."), QStringLiteral("排隊中…")},
        {QStringLiteral("refused: see the log"), QStringLiteral("被拒絕：請看紀錄")},
        {QStringLiteral("step %1 / %2"), QStringLiteral("第 %1 / %2 步")},
        {QStringLiteral("done in %1 s"), QStringLiteral("完成，%1 秒")},
        {QStringLiteral("failed: see the log"), QStringLiteral("失敗：請看紀錄")},
        // log lines written by the window
        {QStringLiteral("cannot reach %1: %2"), QStringLiteral("無法連到 %1：%2")},
        {QStringLiteral("ComfyUI refused the graph: "), QStringLiteral("ComfyUI 拒絕了這個流程：")},
        {QStringLiteral("image: %1 (%2 x %3)"), QStringLiteral("圖片：%1（%2 x %3）")},
        {QStringLiteral("audio: %1 (%2 KB)"), QStringLiteral("音訊：%1（%2 KB）")},
        {QStringLiteral("could not write %1"), QStringLiteral("無法寫入 %1")},
        {QStringLiteral("done in %1 s, %2 previews"), QStringLiteral("完成，%1 秒，%2 張預覽")},
        {QStringLiteral("ComfyUI failed: "), QStringLiteral("ComfyUI 失敗：")},
        {QStringLiteral("saved "), QStringLiteral("已儲存 ")},
        {QStringLiteral("not connected"), QStringLiteral("未連線")},
        {QStringLiteral("stopped"), QStringLiteral("已停止")},
        {QStringLiteral("graph: "), QStringLiteral("流程：")},
        // dialogs
        {QStringLiteral("ComfyLive"), QStringLiteral("ComfyLive")},
        {QStringLiteral("Could not write %1"), QStringLiteral("無法寫入 %1")},
        {QStringLiteral("Could not open %1:\n%2"), QStringLiteral("無法開啟 %1：\n%2")},
        {QStringLiteral("Start only works for a ComfyUI on this PC (127.0.0.1)."), QStringLiteral("「啟動」只適用於這台電腦上的 ComfyUI（127.0.0.1）。")},
        {QStringLiteral("Open a ComfyUI graph (API format)"), QStringLiteral("開啟 ComfyUI 流程（API 格式）")},
        {QStringLiteral("Save the graph (API format)"), QStringLiteral("儲存流程（API 格式）")},
        {QStringLiteral("ComfyUI API graph (*.json)"), QStringLiteral("ComfyUI API 流程 (*.json)")},
        {QStringLiteral("Save the picture"), QStringLiteral("儲存圖片")},
        {QStringLiteral("PNG (*.png);;JPEG (*.jpg)"), QStringLiteral("PNG (*.png);;JPEG (*.jpg)")},
        {QStringLiteral("Save the audio"), QStringLiteral("儲存音訊")},
        {QStringLiteral("Audio (*.%1)"), QStringLiteral("音訊 (*.%1)")},
        {QStringLiteral("%1: ComfyLive cannot play this format (%2); use Open in player"), QStringLiteral("%1：ComfyLive 不能播放這個格式（%2）；請用「用播放器開啟」")},
        // song helpers (ACE-Step graphs)
        {QStringLiteral("Singer"), QStringLiteral("歌手")},
        {QStringLiteral("Voice"), QStringLiteral("嗓音")},
        {QStringLiteral("(as in the tags)"), QStringLiteral("（照標籤）")},
        {QStringLiteral("Male"), QStringLiteral("男聲")},
        {QStringLiteral("Female"), QStringLiteral("女聲")},
        {QStringLiteral("Duet (male + female)"), QStringLiteral("男女對唱")},
        {QStringLiteral("Choir"), QStringLiteral("合唱")},
        {QStringLiteral("Instrumental (no singing)"), QStringLiteral("純音樂（不唱）")},
        {QStringLiteral("Soft"), QStringLiteral("輕柔")},
        {QStringLiteral("Breathy"), QStringLiteral("氣聲")},
        {QStringLiteral("Powerful"), QStringLiteral("有力")},
        {QStringLiteral("Falsetto"), QStringLiteral("假聲")},
        {QStringLiteral("Raspy"), QStringLiteral("沙啞")},
        {QStringLiteral("Key one semitone down (a new song in that key)"), QStringLiteral("調降半音（用新調重新生成）")},
        {QStringLiteral("Key one semitone up (a new song in that key)"), QStringLiteral("調升半音（用新調重新生成）")},
        {QStringLiteral(" semitones"), QStringLiteral(" 半音")},
        {QStringLiteral("Up (+) or down (-). Up to 3 sounds natural; more sounds thin (up) or dull (down)"), QStringLiteral("升（+）或降（-）。3 以內聽起來自然；更多會變尖（升）或變悶（降）")},
        {QStringLiteral("Transpose"), QStringLiteral("移調")},
        {QStringLiteral("The same song, higher or lower: a new file next to it (ffmpeg)"), QStringLiteral("同一首歌，變高或變低：在旁邊存成新檔（ffmpeg）")},
        {QStringLiteral("transposing %1 by %2 semitones ..."), QStringLiteral("正在把 %1 移調 %2 個半音…")},
        {QStringLiteral("transpose failed: "), QStringLiteral("移調失敗：")},
        {QStringLiteral("Transpose failed:\n%1"), QStringLiteral("移調失敗：\n%1")},
        {QStringLiteral("ffmpeg not found: put ffmpeg.exe next to ComfyLive.exe, or on PATH"), QStringLiteral("找不到 ffmpeg：把 ffmpeg.exe 放在 ComfyLive.exe 旁邊，或加到 PATH")},
        {QStringLiteral("settings: %1"), QStringLiteral("設定：%1")},
        {QStringLiteral("ComfyUI API graph, or a song's or sound's settings (*.json)"), QStringLiteral("ComfyUI API 流程，或歌曲、音效的設定 (*.json)")},
        {QStringLiteral("saved settings: the same values and seed; Generate makes that song or sound again (New seed each run is off)"), QStringLiteral("已存的設定：相同的數值和種子；按「生成」會再做出同一首歌或同一個音效（已關閉「每次換新種子」）")},
        {QStringLiteral("What each parameter of this graph does to the result"), QStringLiteral("這個流程的每個參數如何影響結果")},
        {QStringLiteral("Parameters: what they change"), QStringLiteral("參數：會改變什麼")},
        // the built-in graphs' node titles (group boxes)
        {QStringLiteral("Model"), QStringLiteral("模型")},
        {QStringLiteral("Model shift"), QStringLiteral("模型位移")},
        {QStringLiteral("Positive prompt"), QStringLiteral("正向提示詞")},
        {QStringLiteral("Negative prompt"), QStringLiteral("負向提示詞")},
        {QStringLiteral("Negative"), QStringLiteral("負向")},
        {QStringLiteral("Size"), QStringLiteral("尺寸")},
        {QStringLiteral("Sampler"), QStringLiteral("取樣器")},
        {QStringLiteral("Decode"), QStringLiteral("解碼")},
        {QStringLiteral("Save"), QStringLiteral("儲存")},
        {QStringLiteral("Song"), QStringLiteral("歌曲")},
        {QStringLiteral("Length"), QStringLiteral("長度")},
        {QStringLiteral("Text encoder"), QStringLiteral("文字編碼器")},
        {QStringLiteral("Sound"), QStringLiteral("音效")},
        {QStringLiteral("Avoid"), QStringLiteral("避免")},
        {QStringLiteral("Timing"), QStringLiteral("時間")},
    };
    return table;
}

class ZhTwTranslator : public QTranslator
{
public:
    QString translate(const char* /*context*/, const char* sourceText, const char* /*disambiguation*/, int /*n*/) const override
    {
        return zhTw().value(QString::fromUtf8(sourceText));   // empty: not in the table, Qt keeps the English
    }
    bool isEmpty() const override { return false; }
};

QString g_current = QStringLiteral("en");
ZhTwTranslator* g_translator = nullptr;

}  // namespace

namespace Language {

QStringList codes()
{
    return {QStringLiteral("en"), QStringLiteral("zh_TW")};
}

QString displayName(const QString& code)
{
    return code == QStringLiteral("zh_TW") ? QStringLiteral("繁體中文") : QStringLiteral("English");
}

QString systemDefault()
{
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh_TW") : QStringLiteral("en");
}

QString current()
{
    return g_current;
}

void apply(const QString& code)
{
    g_current = codes().contains(code) ? code : QStringLiteral("en");
    if (!g_translator)
        g_translator = new ZhTwTranslator;
    QCoreApplication::removeTranslator(g_translator);
    if (g_current == QStringLiteral("zh_TW"))
        QCoreApplication::installTranslator(g_translator);
}

}  // namespace Language
