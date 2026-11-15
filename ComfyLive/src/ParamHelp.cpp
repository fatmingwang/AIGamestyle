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
        // ---- Stable Audio Open: sound effects ----
        {QStringLiteral("CLIPLoader/clip_name"),
         {"The text encoder file. Stable Audio Open uses t5-base.safetensors (in text_encoders).",
          "文字編碼器檔。Stable Audio Open 用 t5-base.safetensors（放在 text_encoders）。"}},
        {QStringLiteral("CLIPLoader/type"),
         {"Which model family the encoder is for: stable_audio for Stable Audio Open. The wrong one gives noise or an error.",
          "編碼器屬於哪個模型家族：Stable Audio Open 用 stable_audio。選錯會得到雜音或錯誤。"}},
        {QStringLiteral("ConditioningStableAudio/seconds_total"),
         {"The length the model is told the sound has, in seconds (kept equal to Length's seconds). A short value (1-3) "
          "gives one tight hit or bark; a longer one (5-10) lets the sound repeat or carry an ambience.",
          "告訴模型這段聲音多長（秒），和「長度」的 seconds 保持相同。短（1-3）：一聲短促的撞擊或吠叫；長（5-10）：聲音會重複或帶環境聲。"}},
        {QStringLiteral("ConditioningStableAudio/seconds_start"),
         {"Where in a longer recording the sound starts, in seconds. Leave 0 for a sound effect.",
          "聲音從一段較長錄音的第幾秒開始。音效請保持 0。"}},
        {QStringLiteral("EmptyLatentAudio/seconds"),
         {"The length of the sound to render, in seconds (kept equal to Timing's seconds_total). Up to 47 s; short is fast.",
          "要生成的聲音長度（秒），和「時間」的 seconds_total 保持相同。最長 47 秒；越短越快。"}},
        {QStringLiteral("EmptyLatentAudio/batch_size"),
         {"How many takes at once. More takes need more memory and time.", "一次生成幾個版本。越多越花記憶體和時間。"}},
        // ---- Qwen3-TTS: speech (ComfyUI-Qwen-TTS custom node) ----
        {QStringLiteral("FB_Qwen3TTS/text"),
         {"What is said. One sentence, or several: they are read as one speech, with natural pauses at full stops. Write "
          "it in the chosen language; numbers read best written out.",
          "要說的話。一句或好幾句：會當成一段話念出，句號處自然停頓。用所選的語言寫；數字寫成文字念得最好。"}},
        {QStringLiteral("FB_Qwen3TTSCustomVoice/speaker"),
         {"The preset voice. Ryan, Aiden: English men. Vivian, Serena: Chinese women. Uncle_fu: an older Chinese man. "
          "Dylan: a Beijing man. Eric: a Sichuan man. Ono_anna: a Japanese woman. Sohee: a Korean woman. Each speaks every "
          "language, best its own.",
          "預設聲音。Ryan、Aiden：英語男聲。Vivian、Serena：中文女聲。Uncle_fu：中文年長男聲。Dylan：北京男聲。Eric：四川男聲。"
          "Ono_anna：日語女聲。Sohee：韓語女聲。每個聲音都能說各種語言，自己的母語最自然。"}},
        {QStringLiteral("FB_Qwen3TTSCustomVoice/instruct"),
         {"Optional: how to say it (\"angry, shouting\", \"whispering, scared\", \"cheerful and fast\"). Empty: a neutral read.",
          "可留空：怎麼說（\"angry, shouting\"、\"whispering, scared\"、\"cheerful and fast\"）。留空：平穩地念。"}},
        {QStringLiteral("FB_Qwen3TTSVoiceDesign/instruct"),
         {"The voice, described in words: who (age, man or woman, character), the timbre (deep, bright, raspy), the pace and "
          "the mood. \"A young girl, bright and energetic voice, speaking fast and excited.\"",
          "用文字描述聲音：是誰（年齡、男女、角色）、音色（低沉、明亮、沙啞）、速度和情緒。例：\"A young girl, bright and energetic voice, speaking fast and excited.\""}},
        {QStringLiteral("FB_Qwen3TTS/language"),
         {"The language of the text. Auto guesses it; naming it gives the right accent, above all for short lines.",
          "文字的語言。Auto 會自己猜；指定語言發音較準，短句尤其如此。"}},
        {QStringLiteral("FB_Qwen3TTS/seed"),
         {"The take. Same seed + same values = the same reading; another seed = another reading (pace, stress, breath).",
          "版本。相同種子加相同數值 = 相同的念法；換種子 = 另一種念法（速度、重音、換氣）。"}},
        {QStringLiteral("FB_Qwen3TTS/model_choice"),
         {"The model size: 1.7B sounds best (about 4 GB of video memory); 0.6B is faster and plainer. Voice design needs 1.7B.",
          "模型大小：1.7B 最好聽（約 4 GB 顯示記憶體）；0.6B 較快、較平淡。用文字描述聲音必須用 1.7B。"}},
        {QStringLiteral("FB_Qwen3TTS/temperature"),
         {"How lively the reading is. Higher (1.0+): more expressive, can slur; lower (0.6): steady and flat. 0.9 is usual.",
          "念法的活潑程度。較高（1.0 以上）：較有表情，可能含糊；較低（0.6）：平穩平淡。常用 0.9。"}},
        {QStringLiteral("FB_Qwen3TTS/max_new_tokens"),
         {"The longest it may speak: 12 tokens a second, so 2048 is about 170 s. Raise it only for a very long speech.",
          "最多能說多長：每秒 12 個 token，2048 約 170 秒。只有很長的演講才需要加大。"}},
        {QStringLiteral("FB_Qwen3TTS/unload_model_after_generate"),
         {"On: the model leaves video memory after each run (slower next time, frees memory for pictures or music).",
          "開啟：每次生成後把模型移出顯示記憶體（下次較慢，但可空出記憶體給圖片或音樂）。"}},
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
         {"The prompt: what to make (the positive box) or what to avoid (the negative box). Concrete words work best. "
          "A picture: subject, style, light, colours. A sound: the source, the action, the material and the recording "
          "(\"sword striking a steel shield, sharp metallic clang, single hit, close\").",
          "提示詞：要生成什麼（正向）或要避免什麼（負向）。具體的詞最有效。圖片：主體、風格、光線、顏色。"
          "音效：聲源、動作、材質和錄音方式（\"sword striking a steel shield, sharp metallic clang, single hit, close\"）。"}},
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
