#include "Workflow.h"

#include <QDateTime>
#include <QDir>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

QJsonObject node(const QString& cls, const QString& title, const QJsonObject& inputs)
{
    QJsonObject n;
    n[QStringLiteral("class_type")] = cls;
    n[QStringLiteral("inputs")] = inputs;
    n[QStringLiteral("_meta")] = QJsonObject{{QStringLiteral("title"), title}};
    return n;
}

QJsonArray link(const char* nodeId, int slot)
{
    return QJsonArray{QString::fromLatin1(nodeId), slot};
}

// The input's spec in /object_info: ["INT", {min, max, ...}], [[choices], {...}] or ["COMBO", {"options": [...]}].
QJsonArray specOf(const QJsonObject& objectInfo, const QString& cls, const QString& input)
{
    const QJsonObject in = objectInfo.value(cls).toObject().value(QStringLiteral("input")).toObject();
    for (const char* part : {"required", "optional"}) {
        const QJsonValue v = in.value(QLatin1String(part)).toObject().value(input);
        if (v.isArray())
            return v.toArray();
    }
    return {};
}

QStringList choicesOf(const QJsonArray& spec)
{
    QStringList out;
    QJsonArray list;
    if (!spec.isEmpty() && spec[0].isArray())
        list = spec[0].toArray();
    else if (!spec.isEmpty() && spec[0].toString() == QStringLiteral("COMBO"))
        list = spec.size() > 1 ? spec[1].toObject().value(QStringLiteral("options")).toArray() : QJsonArray();
    for (const QJsonValue& v : list)
        out << (v.isString() ? v.toString() : QString::number(v.toDouble()));
    return out;
}

bool isLink(const QJsonValue& v)
{
    return v.isArray() && v.toArray().size() == 2 && v.toArray()[0].isString();
}

}  // namespace

Workflow Workflow::textToImage()
{
    QJsonObject g;
    g[QStringLiteral("4")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("Juggernaut-XL_v9_RunDiffusionPhoto_v2.safetensors")}});
    g[QStringLiteral("6")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Positive prompt"),
                                  {{QStringLiteral("text"), QStringLiteral("a brave young archer in a blue tunic, fantasy RPG character, full body, "
                                                                           "game art, clean lines, soft lighting, plain background")},
                                   {QStringLiteral("clip"), link("4", 1)}});
    g[QStringLiteral("7")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Negative prompt"),
                                  {{QStringLiteral("text"), QStringLiteral("blurry, low quality, extra limbs, text, watermark")},
                                   {QStringLiteral("clip"), link("4", 1)}});
    g[QStringLiteral("5")] = node(QStringLiteral("EmptyLatentImage"), QStringLiteral("Size"),
                                  {{QStringLiteral("width"), 1024}, {QStringLiteral("height"), 1024}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("3")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 20}, {QStringLiteral("cfg"), 6.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("dpmpp_2m")}, {QStringLiteral("scheduler"), QStringLiteral("karras")},
                                   {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("4", 0)},
                                   {QStringLiteral("positive"), link("6", 0)}, {QStringLiteral("negative"), link("7", 0)},
                                   {QStringLiteral("latent_image"), link("5", 0)}});
    g[QStringLiteral("8")] = node(QStringLiteral("VAEDecode"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("3", 0)}, {QStringLiteral("vae"), link("4", 2)}});
    g[QStringLiteral("9")] = node(QStringLiteral("SaveImage"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("ComfyLive")}, {QStringLiteral("images"), link("8", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to image (built-in)");
    return w;
}

Workflow Workflow::textToMusic()
{
    // The same graph as ComfyUI's "ACE-Step 1.5 checkpoint" template (audio_ace_step_1_5_checkpoint.json), saving MP3
    // (Windows plays it without extra codecs). The model: ace_step_1.5_turbo_aio.safetensors in checkpoints/ (README.md).
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("ace_step_1.5_turbo_aio.safetensors")}});
    g[QStringLiteral("2")] = node(QStringLiteral("ModelSamplingAuraFlow"), QStringLiteral("Model shift"),
                                  {{QStringLiteral("shift"), 3.0}, {QStringLiteral("model"), link("1", 0)}});
    g[QStringLiteral("3")] = node(QStringLiteral("TextEncodeAceStepAudio1.5"), QStringLiteral("Song"),
                                  {{QStringLiteral("tags"), QStringLiteral("smooth quiet storm R&B, 1970s Philly soul ballad, high smooth male tenor, "
                                                                           "soft falsetto, breathy intimate delivery, lush strings, soft electric piano, "
                                                                           "gentle bass, subtle drums, warm polished production")},
                                   {QStringLiteral("lyrics"), QStringLiteral("[Verse]\nI woke up in a tower made of stone\nNo name, no road, no way back home\n"
                                                                             "Every door I open shows a face I almost know\n\n"
                                                                             "[Chorus]\nTell me who I was before the night\nGive me back the memory, give me back the light\n"
                                                                             "Floor by floor I climb to find the man I left behind\n\n"
                                                                             "[Outro]\nFloor by floor, I am coming home")},
                                   {QStringLiteral("seed"), 1}, {QStringLiteral("bpm"), 72}, {QStringLiteral("duration"), 60.0},
                                   {QStringLiteral("timesignature"), QStringLiteral("4")}, {QStringLiteral("language"), QStringLiteral("en")},
                                   {QStringLiteral("keyscale"), QStringLiteral("D minor")}, {QStringLiteral("generate_audio_codes"), true},
                                   {QStringLiteral("cfg_scale"), 2.0}, {QStringLiteral("temperature"), 0.85}, {QStringLiteral("top_p"), 0.9},
                                   {QStringLiteral("top_k"), 0}, {QStringLiteral("min_p"), 0.0}, {QStringLiteral("clip"), link("1", 1)}});
    g[QStringLiteral("4")] = node(QStringLiteral("ConditioningZeroOut"), QStringLiteral("Negative"),
                                  {{QStringLiteral("conditioning"), link("3", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("EmptyAceStep1.5LatentAudio"), QStringLiteral("Length"),
                                  {{QStringLiteral("seconds"), 60.0}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("6")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 8}, {QStringLiteral("cfg"), 1.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("euler")}, {QStringLiteral("scheduler"), QStringLiteral("simple")},
                                   {QStringLiteral("denoise"), 1.0}, {QStringLiteral("model"), link("2", 0)},
                                   {QStringLiteral("positive"), link("3", 0)}, {QStringLiteral("negative"), link("4", 0)},
                                   {QStringLiteral("latent_image"), link("5", 0)}});
    g[QStringLiteral("7")] = node(QStringLiteral("VAEDecodeAudio"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("6", 0)}, {QStringLiteral("vae"), link("1", 2)}});
    g[QStringLiteral("8")] = node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("audio/ComfyLive")}, {QStringLiteral("quality"), QStringLiteral("V0")},
                                   {QStringLiteral("audio"), link("7", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to music, ACE-Step 1.5 (built-in)");
    return w;
}

Workflow Workflow::textToSoundEffect()
{
    // ComfyUI's Stable Audio Open 1.0 graph (its "stable audio" example), with the timing conditioning and saving MP3
    // (Windows plays it without extra codecs). The models: stable-audio-open-1.0.safetensors in checkpoints/ and
    // t5-base.safetensors in text_encoders/ (README.md). The default: a dog barking.
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("CheckpointLoaderSimple"), QStringLiteral("Model"),
                                  {{QStringLiteral("ckpt_name"), QStringLiteral("stable-audio-open-1.0.safetensors")}});
    g[QStringLiteral("2")] = node(QStringLiteral("CLIPLoader"), QStringLiteral("Text encoder"),
                                  {{QStringLiteral("clip_name"), QStringLiteral("t5-base.safetensors")},
                                   {QStringLiteral("type"), QStringLiteral("stable_audio")}});
    g[QStringLiteral("3")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Sound"),
                                  {{QStringLiteral("text"), QStringLiteral("a medium-sized dog barking twice, loud and close, outdoors, "
                                                                           "clean field recording, sound effect")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("4")] = node(QStringLiteral("CLIPTextEncode"), QStringLiteral("Avoid"),
                                  {{QStringLiteral("text"), QStringLiteral("music, speech, human voice, background noise, hum, low quality, distorted")},
                                   {QStringLiteral("clip"), link("2", 0)}});
    g[QStringLiteral("5")] = node(QStringLiteral("ConditioningStableAudio"), QStringLiteral("Timing"),
                                  {{QStringLiteral("seconds_start"), 0.0}, {QStringLiteral("seconds_total"), 3.0},
                                   {QStringLiteral("positive"), link("3", 0)}, {QStringLiteral("negative"), link("4", 0)}});
    g[QStringLiteral("6")] = node(QStringLiteral("EmptyLatentAudio"), QStringLiteral("Length"),
                                  {{QStringLiteral("seconds"), 3.0}, {QStringLiteral("batch_size"), 1}});
    g[QStringLiteral("7")] = node(QStringLiteral("KSampler"), QStringLiteral("Sampler"),
                                  {{QStringLiteral("seed"), 1}, {QStringLiteral("steps"), 50}, {QStringLiteral("cfg"), 5.0},
                                   {QStringLiteral("sampler_name"), QStringLiteral("dpmpp_3m_sde_gpu")},
                                   {QStringLiteral("scheduler"), QStringLiteral("exponential")}, {QStringLiteral("denoise"), 1.0},
                                   {QStringLiteral("model"), link("1", 0)}, {QStringLiteral("positive"), link("5", 0)},
                                   {QStringLiteral("negative"), link("5", 1)}, {QStringLiteral("latent_image"), link("6", 0)}});
    g[QStringLiteral("8")] = node(QStringLiteral("VAEDecodeAudio"), QStringLiteral("Decode"),
                                  {{QStringLiteral("samples"), link("7", 0)}, {QStringLiteral("vae"), link("1", 2)}});
    g[QStringLiteral("9")] = node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                                  {{QStringLiteral("filename_prefix"), QStringLiteral("audio/ComfyLive_sfx")}, {QStringLiteral("quality"), QStringLiteral("V0")},
                                   {QStringLiteral("audio"), link("8", 0)}});
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to sound effect, Stable Audio Open (built-in)");
    return w;
}

namespace {

// The Qwen3-TTS node's sampling inputs, the same on both speech nodes (ComfyUI-Qwen-TTS's own defaults).
QJsonObject qwenSampling(QJsonObject in)
{
    in[QStringLiteral("model_choice")] = QStringLiteral("1.7B");
    in[QStringLiteral("device")] = QStringLiteral("auto");
    in[QStringLiteral("precision")] = QStringLiteral("bf16");
    in[QStringLiteral("seed")] = 1;
    in[QStringLiteral("max_new_tokens")] = 2048;
    in[QStringLiteral("top_p")] = 1.0;
    in[QStringLiteral("top_k")] = 50;
    in[QStringLiteral("temperature")] = 0.9;
    in[QStringLiteral("repetition_penalty")] = 1.05;
    in[QStringLiteral("attention")] = QStringLiteral("auto");
    // on: the speech model leaves the GPU after each run (ComfyUI's /free cannot reach it), so the main ComfyUI's
    // picture and music models have the 12 GB again
    in[QStringLiteral("unload_model_after_generate")] = true;
    return in;
}

QJsonObject saveMp3(const char* prefix, const char* from)
{
    return node(QStringLiteral("SaveAudioMP3"), QStringLiteral("Save"),
                {{QStringLiteral("filename_prefix"), QString::fromLatin1(prefix)}, {QStringLiteral("quality"), QStringLiteral("V0")},
                 {QStringLiteral("audio"), link(from, 0)}});
}

}  // namespace

Workflow Workflow::textToSpeech()
{
    // ComfyUI-Qwen-TTS's CustomVoice node (custom_nodes/ComfyUI-Qwen-TTS; models in models/qwen-tts, README.md): one of
    // 9 preset speakers says the text in the chosen language; "instruct" adds a tone ("angry", "whispering").
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("FB_Qwen3TTSCustomVoice"), QStringLiteral("Speech"),
                                  qwenSampling({{QStringLiteral("text"), QStringLiteral("The gate of the tower is open. Climb, young hero, "
                                                                                        "and do not look back.")},
                                                {QStringLiteral("speaker"), QStringLiteral("Ryan")},
                                                {QStringLiteral("language"), QStringLiteral("English")},
                                                {QStringLiteral("instruct"), QString()}}));
    g[QStringLiteral("2")] = saveMp3("audio/ComfyLive_speech", "1");
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to speech, Qwen3-TTS preset voice (built-in)");
    return w;
}

Workflow Workflow::textToSpeechDesigned()
{
    // ComfyUI-Qwen-TTS's VoiceDesign node: the voice described in words ("instruct") instead of a preset speaker.
    QJsonObject g;
    g[QStringLiteral("1")] = node(QStringLiteral("FB_Qwen3TTSVoiceDesign"), QStringLiteral("Speech"),
                                  qwenSampling({{QStringLiteral("text"), QStringLiteral("So, another climber. Many have come before you, "
                                                                                        "and the tower remembers every one of them.")},
                                                {QStringLiteral("instruct"), QStringLiteral("An old male wizard, deep and slightly raspy voice, "
                                                                                            "speaking slowly, calm and a little mysterious.")},
                                                {QStringLiteral("language"), QStringLiteral("English")}}));
    g[QStringLiteral("2")] = saveMp3("audio/ComfyLive_voice", "1");
    Workflow w;
    w.graph_ = g;
    w.name_ = QStringLiteral("Text to speech, Qwen3-TTS voice from a description (built-in)");
    return w;
}

QStringList Workflow::builtInIds()
{
    return {QStringLiteral("image"), QStringLiteral("music"), QStringLiteral("sfx"), QStringLiteral("speech"), QStringLiteral("voice")};
}

bool Workflow::builtIn(const QString& id, Workflow* out)
{
    if (id == QStringLiteral("image"))
        *out = textToImage();
    else if (id == QStringLiteral("music"))
        *out = textToMusic();
    else if (id == QStringLiteral("sfx"))
        *out = textToSoundEffect();
    else if (id == QStringLiteral("speech"))
        *out = textToSpeech();
    else if (id == QStringLiteral("voice"))
        *out = textToSpeechDesigned();
    else
        return false;
    return true;
}

bool Workflow::fromRecipe(const QJsonObject& o, QString* why)
{
    const QString id = o.value(QStringLiteral("graph")).toString(QStringLiteral("music"));   // an old song file has none
    Workflow w;
    if (!builtIn(id, &w)) {
        if (why)
            *why = QStringLiteral("\"graph\": \"%1\" is not a built-in graph (%2)").arg(id, builtInIds().join(QStringLiteral(", ")));
        return false;
    }
    // friendly names for inputs whose plain name is on two nodes (the sound graph's two prompts)
    QHash<QString, QString> alias;
    if (id == QStringLiteral("sfx")) {
        alias.insert(QStringLiteral("prompt"), QStringLiteral("3/text"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("4/text"));
    } else if (id == QStringLiteral("image")) {
        alias.insert(QStringLiteral("prompt"), QStringLiteral("6/text"));
        alias.insert(QStringLiteral("avoid"), QStringLiteral("7/text"));
    } else if (id == QStringLiteral("voice")) {
        alias.insert(QStringLiteral("voice"), QStringLiteral("1/instruct"));
    } else if (id == QStringLiteral("speech")) {
        alias.insert(QStringLiteral("style"), QStringLiteral("1/instruct"));
    }
    QJsonObject values;
    static const QStringList reserved{QStringLiteral("out"), QStringLiteral("random-seed"), QStringLiteral("params"),
                                      QStringLiteral("name"), QStringLiteral("graph"), QStringLiteral("comfylive")};
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (reserved.contains(it.key()) || it.key().startsWith(QLatin1Char('_')))   // "_comment": notes for people
            continue;
        values[it.key()] = it.value();
    }
    const QJsonObject more = o.value(QStringLiteral("params")).toObject();
    for (auto it = more.begin(); it != more.end(); ++it)
        values[it.key()] = it.value();
    bool hasSeed = false;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QString key = alias.value(it.key(), it.key());
        QJsonValue v = it.value();
        if (v.isArray()) {   // lines: tags with ", ", the sentences of a speech with a space, else one per line
            QStringList parts;
            for (const QJsonValue& line : v.toArray())
                parts << line.toString();
            const QString input = key.section(QLatin1Char('/'), -1);
            const bool speechText = (id == QStringLiteral("speech") || id == QStringLiteral("voice")) && input == QStringLiteral("text");
            v = parts.join(input == QStringLiteral("tags") ? QStringLiteral(", ") : speechText ? QStringLiteral(" ") : QStringLiteral("\n"));
        }
        bool found = false;
        for (auto n = w.graph_.begin(); n != w.graph_.end(); ++n) {
            const QString nodeId = n.key();
            const QJsonObject inputs = n.value().toObject().value(QStringLiteral("inputs")).toObject();
            const QString input = key.contains(QLatin1Char('/')) ? key.section(QLatin1Char('/'), 1) : key;
            if ((key.contains(QLatin1Char('/')) && key.section(QLatin1Char('/'), 0, 0) != nodeId) || !inputs.contains(input) ||
                isLink(inputs.value(input)))
                continue;
            w.setValue(nodeId, input, v);
            found = true;
            hasSeed = hasSeed || input == QStringLiteral("seed") || input == QStringLiteral("noise_seed");
        }
        if (!found) {
            if (why)
                *why = QStringLiteral("no input \"%1\" in the built-in \"%2\" graph").arg(it.key(), id);
            return false;
        }
    }
    *this = w;
    fromRecipe_ = true;
    recipeRandomSeed_ = o.value(QStringLiteral("random-seed")).toBool(!hasSeed);
    recipeOut_ = o.value(QStringLiteral("out")).toString();
    if (o.contains(QStringLiteral("name")))
        name_ = o.value(QStringLiteral("name")).toString();
    return true;
}

bool Workflow::isRecipe(const QJsonObject& o)
{
    if (o.value(QStringLiteral("graph")).isString() || o.value(QStringLiteral("comfylive")).toString() == QStringLiteral("recipe"))
        return true;
    if (o.value(QStringLiteral("comfylive")).toString() == QStringLiteral("settings"))
        return false;
    for (auto it = o.begin(); it != o.end(); ++it)   // an API graph: every entry a node
        if (!it.key().startsWith(QLatin1Char('_')) && !it.value().toObject().contains(QStringLiteral("class_type")))
            return !o.contains(QStringLiteral("nodes"));   // the editor's format is not a recipe either
    return false;
}

QString Workflow::settingsPathFor(const QString& audioPath)
{
    const QFileInfo f(audioPath);
    return f.dir().filePath(f.completeBaseName() + QStringLiteral(".json"));
}

bool Workflow::writeSettings(const QString& audioPath, const QJsonObject& graph, const QString& name, const QJsonObject& extra,
                             QString* why)
{
    QJsonObject o = extra;
    o[QStringLiteral("comfylive")] = QStringLiteral("settings");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("audio")] = QFileInfo(audioPath).fileName();
    o[QStringLiteral("created")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o[QStringLiteral("how")] = QStringLiteral("ComfyLive: Open graph... with this file, then Generate: the same song or sound "
                                              "again (New seed each run is turned off for it)");
    o[QStringLiteral("graph")] = graph;
    QFile f(settingsPathFor(audioPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool Workflow::copySettings(const QString& from, const QString& to, const QJsonObject& extra)
{
    QFile in(settingsPathFor(from));
    if (!in.open(QIODevice::ReadOnly))
        return false;
    QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
    if (o.value(QStringLiteral("comfylive")).toString() != QStringLiteral("settings"))
        return false;
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o[it.key()] = it.value();
    o[QStringLiteral("audio")] = QFileInfo(to).fileName();
    QFile out(settingsPathFor(to));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool Workflow::fromJson(const QJsonObject& graphIn, QString* why)
{
    // a song's settings file: the graph inside it
    fromSettings_ = graphIn.value(QStringLiteral("comfylive")).toString() == QStringLiteral("settings") &&
                    graphIn.value(QStringLiteral("graph")).isObject();
    fromRecipe_ = false;
    const QJsonObject graph = fromSettings_ ? graphIn.value(QStringLiteral("graph")).toObject() : graphIn;
    if (graph.contains(QStringLiteral("nodes")) && graph.value(QStringLiteral("nodes")).isArray()) {
        if (why)
            *why = QStringLiteral("This is a ComfyUI workflow in the editor's format. In ComfyUI use Workflow > Export (API), then open that file.");
        return false;
    }
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        if (!it.value().toObject().contains(QStringLiteral("class_type"))) {
            if (why)
                *why = QStringLiteral("Node \"%1\" has no class_type: not an API-format ComfyUI graph.").arg(it.key());
            return false;
        }
    }
    if (graph.isEmpty()) {
        if (why)
            *why = QStringLiteral("The graph is empty.");
        return false;
    }
    graph_ = graph;
    return true;
}

bool Workflow::load(const QString& path, QString* why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        if (why)
            *why = err.errorString();
        return false;
    }
    if (isRecipe(doc.object())) {
        if (!fromRecipe(doc.object(), why))
            return false;
        if (!doc.object().contains(QStringLiteral("name")))
            name_ = QFileInfo(path).completeBaseName();
        return true;
    }
    if (!fromJson(doc.object(), why))
        return false;
    name_ = QFileInfo(path).completeBaseName();
    return true;
}

bool Workflow::save(const QString& path, QString* why) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (why)
            *why = f.errorString();
        return false;
    }
    f.write(QJsonDocument(graph_).toJson(QJsonDocument::Indented));
    return true;
}

QList<ParamDef> Workflow::params(const QJsonObject& objectInfo) const
{
    QStringList ids = graph_.keys();
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) {
        bool okA = false, okB = false;
        const double na = a.toDouble(&okA), nb = b.toDouble(&okB);
        if (okA && okB)
            return na < nb;
        return a < b;
    });
    QList<ParamDef> out;
    for (const QString& id : ids) {
        const QJsonObject n = graph_.value(id).toObject();
        const QString cls = n.value(QStringLiteral("class_type")).toString();
        const QString title = n.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("title")).toString(cls);
        const QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
        // the server's order for this class when it knows it (required, then optional); else the file's
        QStringList order;
        const QJsonObject classInfo = objectInfo.value(cls).toObject();
        for (const QJsonValue& v : classInfo.value(QStringLiteral("input_order")).toObject().value(QStringLiteral("required")).toArray())
            order << v.toString();
        for (const QJsonValue& v : classInfo.value(QStringLiteral("input_order")).toObject().value(QStringLiteral("optional")).toArray())
            order << v.toString();
        for (const QString& k : inputs.keys())
            if (!order.contains(k))
                order << k;
        for (const QString& input : order) {
            if (!inputs.contains(input))
                continue;
            const QJsonValue v = inputs.value(input);
            if (isLink(v))
                continue;
            ParamDef p;
            p.nodeId = id;
            p.input = input;
            p.group = title == cls ? cls : title + QStringLiteral("  (") + cls + QStringLiteral(")");
            p.classType = cls;
            p.value = v;
            const QJsonArray spec = specOf(objectInfo, cls, input);
            const QString type = spec.isEmpty() || spec[0].isArray() ? QString() : spec[0].toString();
            const QJsonObject opts = spec.size() > 1 ? spec[1].toObject() : QJsonObject();
            p.tooltip = opts.value(QStringLiteral("tooltip")).toString();
            const QStringList choices = choicesOf(spec);
            if (!choices.isEmpty()) {
                p.kind = ParamDef::Kind::Combo;
                p.choices = choices;
            } else if (type == QStringLiteral("INT")) {
                p.kind = ParamDef::Kind::Int;
                p.minimum = std::max(-1e15, opts.value(QStringLiteral("min")).toDouble(-1e15));
                p.maximum = std::min(1e15, opts.value(QStringLiteral("max")).toDouble(1e15));
                p.step = std::max(1.0, opts.value(QStringLiteral("step")).toDouble(1.0));
            } else if (type == QStringLiteral("FLOAT")) {
                p.kind = ParamDef::Kind::Float;
                p.minimum = opts.value(QStringLiteral("min")).toDouble(-1e9);
                p.maximum = opts.value(QStringLiteral("max")).toDouble(1e9);
                p.step = opts.value(QStringLiteral("step")).toDouble(0.01);
                p.decimals = std::clamp(int(std::ceil(-std::log10(std::max(1e-6, p.step)))), 1, 6);
            } else if (type == QStringLiteral("BOOLEAN")) {
                p.kind = ParamDef::Kind::Bool;
            } else if (type == QStringLiteral("STRING")) {
                p.kind = opts.value(QStringLiteral("multiline")).toBool() ? ParamDef::Kind::Text : ParamDef::Kind::Line;
            } else if (v.isBool()) {   // the server does not know this node: guess from the value
                p.kind = ParamDef::Kind::Bool;
            } else if (v.isDouble()) {
                const double d = v.toDouble();
                p.kind = d == std::floor(d) && std::fabs(d) < 1e15 ? ParamDef::Kind::Int : ParamDef::Kind::Float;
                p.decimals = 3;
                p.step = p.kind == ParamDef::Kind::Int ? 1.0 : 0.05;
            } else if (v.isString()) {
                p.kind = v.toString().size() > 60 || input == QStringLiteral("text") ? ParamDef::Kind::Text : ParamDef::Kind::Line;
            } else {
                continue;   // an object or a list: not editable here
            }
            out << p;
        }
    }
    return out;
}

namespace {

void setInput(QJsonObject& graph, const QString& nodeId, const QString& input, const QJsonValue& v)
{
    QJsonObject n = graph.value(nodeId).toObject();
    QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
    inputs[input] = v;
    n[QStringLiteral("inputs")] = inputs;
    graph[nodeId] = n;
}

}  // namespace

void Workflow::setValue(const QString& nodeId, const QString& input, const QJsonValue& v)
{
    setInput(graph_, nodeId, input, v);
    // One length, two inputs: an ACE-Step encoder's duration <-> the empty ACE-Step latent's seconds, and Stable Audio's
    // timing seconds_total <-> the empty audio latent's seconds. Only a plain number follows; an input that is a link is
    // the user's own wiring and stays.
    struct Tie { const char* classA; const char* inputA; const char* classB; const char* inputB; };
    static const Tie ties[] = {
        {"TextEncodeAceStepAudio", "duration", "EmptyAceStep", "seconds"},
        {"ConditioningStableAudio", "seconds_total", "EmptyLatentAudio", "seconds"},
    };
    const QString cls = graph_.value(nodeId).toObject().value(QStringLiteral("class_type")).toString();
    QString other, otherClass;
    for (const Tie& t : ties) {
        if (cls.startsWith(QLatin1String(t.classA)) && input == QLatin1String(t.inputA)) {
            other = QLatin1String(t.inputB);
            otherClass = QLatin1String(t.classB);
        } else if (cls.startsWith(QLatin1String(t.classB)) && input == QLatin1String(t.inputB)) {
            other = QLatin1String(t.inputA);
            otherClass = QLatin1String(t.classA);
        }
    }
    if (other.isEmpty())
        return;
    QStringList ids;
    for (auto it = graph_.begin(); it != graph_.end(); ++it) {
        const QJsonObject n = it.value().toObject();
        if (it.key() != nodeId && n.value(QStringLiteral("class_type")).toString().startsWith(otherClass) &&
            n.value(QStringLiteral("inputs")).toObject().value(other).isDouble())
            ids << it.key();
    }
    for (const QString& id : ids)
        setInput(graph_, id, other, v.toDouble());
}

bool Workflow::needsSpeechServer() const
{
    for (auto it = graph_.begin(); it != graph_.end(); ++it)
        if (it.value().toObject().value(QStringLiteral("class_type")).toString().startsWith(QStringLiteral("FB_Qwen3TTS")))
            return true;
    return false;
}

QJsonValue Workflow::value(const QString& nodeId, const QString& input) const
{
    return graph_.value(nodeId).toObject().value(QStringLiteral("inputs")).toObject().value(input);
}

QStringList Workflow::fixMissingChoices(const QJsonObject& objectInfo)
{
    QStringList changed;
    for (const ParamDef& p : params(objectInfo)) {
        if (p.kind != ParamDef::Kind::Combo || p.choices.isEmpty() || p.choices.contains(p.value.toString()))
            continue;
        if (p.classType.startsWith(QStringLiteral("FB_Qwen3TTS")))
            continue;   // the speech node's lists are its own (speakers, languages, sizes), never a model file
        // the same family as the missing one: an ACE-Step model for an ACE-Step graph, a Stable Audio model or a T5
        // encoder for a sound graph, else an SDXL picture model. A sound model with nothing of its family on the server
        // stays as it is (ComfyUI then names the missing file): another family's model would only fail later.
        const QString was = p.value.toString();
        auto has = [](const QString& s, const char* word) { return s.contains(QLatin1String(word), Qt::CaseInsensitive); };
        auto isStableAudio = [&](const QString& s) { return has(s, "stable") && has(s, "audio"); };
        QStringList family;
        bool keepWhenNone = true;
        if (has(was, "ace"))
            family = p.choices.filter(QStringLiteral("ace"), Qt::CaseInsensitive);
        else if (isStableAudio(was))
            std::copy_if(p.choices.begin(), p.choices.end(), std::back_inserter(family), isStableAudio);
        else if (has(was, "t5"))
            family = p.choices.filter(QStringLiteral("t5"), Qt::CaseInsensitive);
        else {
            keepWhenNone = false;
            for (const QString& c : p.choices)
                if (has(c, "Juggernaut") || has(c, "sd_xl"))
                    family << c;
        }
        if (family.isEmpty() && keepWhenNone) {
            changed << QStringLiteral("%1.%2: \"%3\" is not on the server: download it (README.md)").arg(p.group, p.input, was);
            continue;
        }
        const QString pick = family.isEmpty() ? p.choices.first() : family.first();
        setValue(p.nodeId, p.input, pick);
        changed << QStringLiteral("%1.%2: \"%3\" is not on the server, using \"%4\"").arg(p.group, p.input, p.value.toString(), pick);
    }
    return changed;
}
