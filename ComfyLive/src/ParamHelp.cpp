#include "ParamHelp.h"

#include "Translator.h"

#include <QHash>
#include <utility>

namespace {

struct Text {
    const char* en;
    const char* zh;
};

// "<class prefix>/<input>" first (the same name means different things on different nodes), then "<input>" alone.
// Each text: what it is, and what changing it does to the result.
const QHash<QString, Text>& table()
{
    static const QHash<QString, Text> t = {
        // ---- ACE-Step 1.5: the song (the language model plans the song, then the sampler renders the sound) ----
        {QStringLiteral("TextEncodeAceStep/tags"),
         {"The style of the song: genre, era, instruments, mood, singer and voice, as comma-separated words. The biggest "
          "influence on how the song sounds. Words first in the list weigh a little more.",
          "歌曲的風格：曲風、年代、樂器、情緒、歌手和嗓音，用逗號分隔。對歌曲聽起來如何影響最大。排在前面的詞份量稍重。"}},
        {QStringLiteral("TextEncodeAceStep/lyrics"),
         {"The words to sing, with section tags on their own lines: [Verse], [Chorus], [Bridge], [Outro]. The tags shape "
          "the song's structure. Only [Instrumental] for no singing.",
          "要唱的歌詞，段落標籤各自一行：[Verse]（主歌）、[Chorus]（副歌）、[Bridge]（橋段）、[Outro]（尾奏）。標籤決定歌曲結構。"
          "只寫 [Instrumental] 就是純音樂。"}},
        {QStringLiteral("TextEncodeAceStep/seed"),
         {"The planner's dice: which melody, arrangement and structure it picks for these tags and lyrics. Same seed + "
          "same values = the same song; a new seed = a different take.",
          "編曲規劃的骰子：決定這些標籤和歌詞會得到哪一種旋律、編曲和結構。相同種子加相同數值 = 同一首歌；換種子 = 另一個版本。"}},
        {QStringLiteral("TextEncodeAceStep/bpm"),
         {"Tempo in beats per minute. 60-80 slow ballad, 90-110 relaxed groove, 120-140 upbeat pop/dance.",
          "速度（每分鐘拍數）。60-80 慢歌，90-110 輕鬆律動，120-140 輕快流行或舞曲。"}},
        {QStringLiteral("TextEncodeAceStep/duration"),
         {"Length of the song in seconds (the Length node follows it). Longer takes more time and memory; the lyrics should "
          "fit the length.",
          "歌曲長度（秒），「長度」節點會跟著改。越長越花時間和記憶體；歌詞要配合長度。"}},
        {QStringLiteral("TextEncodeAceStep/timesignature"),
         {"Beats per bar: 4 for most pop, 3 for a waltz feel, 6 for a swaying 6/8 ballad, 2 for marches.",
          "每小節拍數：多數流行歌用 4，華爾滋感用 3，搖擺的 6/8 慢歌用 6，進行曲用 2。"}},
        {QStringLiteral("TextEncodeAceStep/language"),
         {"The language of the lyrics, so they are pronounced right (en English, zh Chinese, ja Japanese ...).",
          "歌詞的語言，讓發音正確（en 英文、zh 中文、ja 日文…）。"}},
        {QStringLiteral("TextEncodeAceStep/keyscale"),
         {"The key. Higher keys sit higher in the voice; minor keys sound sadder or darker, major keys brighter. A new key "
          "makes a new song: to keep a melody, use Transpose on the finished song.",
          "調性。調越高，人聲越高；小調較哀傷或陰暗，大調較明亮。換調會生成新歌；想保留旋律，請對完成的歌用「移調」。"}},
        {QStringLiteral("TextEncodeAceStep/generate_audio_codes"),
         {"On: the language model plans the song first (melody, structure). Better songs, a little slower. Turn off only "
          "when you give a reference audio instead.",
          "開啟：語言模型先規劃歌曲（旋律、結構）。歌曲較好，稍慢。只有提供參考音訊時才關掉。"}},
        {QStringLiteral("TextEncodeAceStep/cfg_scale"),
         {"How strictly the planner follows the tags and lyrics. Higher (2.5-4): closer to the words, less variety, can "
          "sound forced. Lower (1-1.5): freer, more surprising. 2 is the usual.",
          "規劃時遵守標籤和歌詞的程度。較高（2.5-4）：更貼近文字、變化少、可能生硬。較低（1-1.5）：較自由、較多驚喜。常用 2。"}},
        {QStringLiteral("TextEncodeAceStep/temperature"),
         {"How adventurous the planner is when choosing each next note. Higher (1.0+): more creative, can wander or break "
          "the structure. Lower (0.6-0.8): safer, more predictable, can get repetitive.",
          "規劃時選下一個音的冒險程度。較高（1.0 以上）：更有創意，可能離題或打亂結構。較低（0.6-0.8）：較保守可預期，可能重複。"}},
        {QStringLiteral("TextEncodeAceStep/top_p"),
         {"Only the most likely choices that add up to this share are considered (nucleus sampling). 0.9: normal. Lower "
          "(0.7): safer and plainer. 1.0: everything allowed, more variety and more risk.",
          "只考慮機率加總到這個比例內的最可能選項（nucleus 取樣）。0.9：一般。較低（0.7）：較穩、較平淡。1.0：全部都可，變化多、風險高。"}},
        {QStringLiteral("TextEncodeAceStep/top_k"),
         {"Only the K most likely choices are considered at each step. 0 = off (top_p decides). Small numbers (10-50) "
          "make the song safer and plainer.",
          "每一步只考慮最可能的前 K 個選項。0 = 關閉（由 top_p 決定）。小的數字（10-50）讓歌曲較穩、較平淡。"}},
        {QStringLiteral("TextEncodeAceStep/min_p"),
         {"Drops choices less likely than this fraction of the best one. 0 = off. 0.02-0.1 removes odd notes while keeping "
          "variety.",
          "去掉機率低於最佳選項這個比例的選項。0 = 關閉。0.02-0.1 可去掉怪音，同時保留變化。"}},
        {QStringLiteral("EmptyAceStep/seconds"),
         {"The length of the song to render, in seconds (kept equal to the Song's duration).",
          "要生成的歌曲長度（秒），和「歌曲」的 duration 保持相同。"}},
        {QStringLiteral("EmptyAceStep/batch_size"),
         {"How many takes at once. More takes need more memory and time.", "一次生成幾個版本。越多越花記憶體和時間。"}},
        {QStringLiteral("ModelSamplingAuraFlow/shift"),
         {"How the sampler spends its steps between the overall shape and the fine detail. ACE-Step uses 3. Higher: more "
          "effort on the overall structure; lower: more on detail. Leave it unless you experiment.",
          "取樣器把步數花在整體輪廓或細節的比例。ACE-Step 用 3。較高：多花在整體結構；較低：多花在細節。除非實驗，否則別改。"}},
        // ---- the sampler (pictures and songs) ----
        {QStringLiteral("KSampler/seed"),
         {"The starting noise. Same seed + same values = the same result; another seed = another variation. For a song "
          "this changes the sound's fine detail; the Song's own seed changes the melody.",
          "起始雜訊。相同種子加相同數值 = 相同結果；換種子 = 另一種變化。對歌曲而言這只改聲音細節；旋律由「歌曲」自己的種子決定。"}},
        {QStringLiteral("KSampler/steps"),
         {"How many refining steps. More: more detail and fewer flaws, slower, until it stops improving (20-30 for "
          "pictures). Turbo models are made for few steps (8): more can make them worse.",
          "精修的步數。越多：細節越多、瑕疵越少、越慢，到某個程度就不再進步（圖片約 20-30）。Turbo 模型為少步數（8）設計，加多反而可能變差。"}},
        {QStringLiteral("KSampler/cfg"),
         {"How strongly the result follows the prompt. Higher: closer to the words, more contrast, can look burnt or "
          "sound harsh. Lower: softer and freer. Pictures 5-8; turbo and ACE-Step models use 1.",
          "結果遵從提示詞的強度。較高：更貼近文字、對比更強，可能過曝或刺耳。較低：較柔和自由。圖片 5-8；Turbo 和 ACE-Step 模型用 1。"}},
        {QStringLiteral("KSampler/sampler_name"),
         {"The method that removes the noise step by step. euler: simple and steady; dpmpp_2m: sharp in few steps; "
          "*_ancestral: adds new noise each step, more variety and never fully settles. Use what the model suggests.",
          "逐步去除雜訊的方法。euler：簡單穩定；dpmpp_2m：少步數也清晰；*_ancestral：每步加新雜訊，變化多、不會完全收斂。用模型建議的。"}},
        {QStringLiteral("KSampler/scheduler"),
         {"How the noise is shared out over the steps. karras: more steps spent on fine detail (good for pictures); "
          "simple / normal: even. Use what the model suggests.",
          "雜訊如何分配到各步。karras：較多步數花在細節（適合圖片）；simple / normal：平均。用模型建議的。"}},
        {QStringLiteral("KSampler/denoise"),
         {"How much is made new. 1.0: everything from scratch. Below 1 (with an input picture or sound): keeps that much "
          "of it, 0.3 small changes, 0.7 big changes.",
          "重新生成的比例。1.0：完全從頭生成。小於 1（有輸入圖片或聲音時）：保留部分原樣，0.3 小改，0.7 大改。"}},
        // ---- pictures ----
        {QStringLiteral("CheckpointLoaderSimple/ckpt_name"),
         {"The model file: decides the whole look (photo, anime, painting) or, for ACE-Step, the music model. A different "
          "model needs its own settings.",
          "模型檔：決定整體風格（照片、動畫、繪畫），ACE-Step 則是音樂模型。換模型通常要換一套設定。"}},
        {QStringLiteral("CLIPTextEncode/text"),
         {"The prompt: what to draw (in the positive box) or what to avoid (in the negative box). Concrete words work "
          "best: subject, style, light, colours.",
          "提示詞：要畫什麼（正向）或要避免什麼（負向）。具體的詞最有效：主體、風格、光線、顏色。"}},
        {QStringLiteral("EmptyLatentImage/width"),
         {"Picture width in pixels (multiples of 64). SDXL is made for about 1024 x 1024 in total; far from it gives "
          "stretched or doubled subjects. Bigger is slower.",
          "圖片寬度（像素，64 的倍數）。SDXL 以約 1024 x 1024 的總量為準；差太多會拉長或主體重複。越大越慢。"}},
        {QStringLiteral("EmptyLatentImage/height"),
         {"Picture height in pixels (multiples of 64); see width.", "圖片高度（像素，64 的倍數）；見寬度。"}},
        {QStringLiteral("EmptyLatentImage/batch_size"),
         {"How many pictures at once. More pictures need more memory and time.", "一次生成幾張。越多越花記憶體和時間。"}},
        // ---- saving ----
        {QStringLiteral("filename_prefix"),
         {"The start of the saved file's name, and a sub-folder if it has a slash (audio/songs/x). It does not change the "
          "result.",
          "存檔名稱的開頭；有斜線就是子資料夾（audio/songs/x）。不影響結果。"}},
        {QStringLiteral("SaveAudio/quality"),
         {"MP3 quality: V0 best variable rate (recommended), 320k largest, 128k smallest.",
          "MP3 品質：V0 最佳可變位元率（建議），320k 最大，128k 最小。"}},
    };
    return t;
}

}  // namespace

namespace ParamHelp {

QString describe(const QString& classType, const QString& input, const QString& comfyTooltip)
{
    const auto& t = table();
    const bool zh = Language::current() == QStringLiteral("zh_TW");
    // the most specific class prefix that has this input: "TextEncodeAceStepAudio1.5" matches "TextEncodeAceStep"
    const Text* found = nullptr;
    int best = -1;
    for (auto it = t.begin(); it != t.end(); ++it) {
        const QString& key = it.key();
        const qsizetype slash = key.indexOf(QLatin1Char('/'));
        const QString prefix = slash < 0 ? QString() : key.left(slash);
        const QString name = slash < 0 ? key : key.mid(slash + 1);
        if (name != input || !classType.startsWith(prefix))
            continue;
        if (int(prefix.size()) > best) {
            best = int(prefix.size());
            found = &it.value();
        }
    }
    if (found)
        return QString::fromUtf8(zh ? found->zh : found->en);
    return comfyTooltip;
}

}  // namespace ParamHelp
