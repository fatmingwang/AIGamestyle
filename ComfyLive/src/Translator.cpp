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
        {QStringLiteral("A built-in graph: SDXL text to image, or ACE-Step 1.5 text to music"), QStringLiteral("內建流程：SDXL 文字生圖，或 ACE-Step 1.5 文字生音樂")},
        {QStringLiteral("Text to image (SDXL)"), QStringLiteral("文字生圖（SDXL）")},
        {QStringLiteral("Text to music (ACE-Step 1.5)"), QStringLiteral("文字生音樂（ACE-Step 1.5）")},
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
        {QStringLiteral("Open the song in Windows' own player (for a format ComfyLive cannot play)"), QStringLiteral("用 Windows 的播放器開啟歌曲（ComfyLive 不能播放的格式）")},
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
        {QStringLiteral("Save the song"), QStringLiteral("儲存歌曲")},
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
        {QStringLiteral("ComfyUI API graph or song settings (*.json)"), QStringLiteral("ComfyUI API 流程或歌曲設定 (*.json)")},
        {QStringLiteral("a song's settings: the same values and seed; Generate makes that song again (New seed each run is off)"), QStringLiteral("歌曲設定：相同的數值和種子；按「生成」會再做出同一首歌（已關閉「每次換新種子」）")},
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
