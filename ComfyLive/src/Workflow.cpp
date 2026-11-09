#include "Workflow.h"

#include <QDateTime>
#include <QDir>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

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
    o[QStringLiteral("how")] = QStringLiteral("ComfyLive: Open graph... with this file, then Generate: the same song again "
                                              "(New seed each run is turned off for it)");
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
    // One song length: the ACE-Step encoder's duration <-> the empty ACE-Step latent's seconds. Only a plain number
    // follows; an input that is a link is the user's own wiring and stays.
    const QString cls = graph_.value(nodeId).toObject().value(QStringLiteral("class_type")).toString();
    const bool isEncoder = cls.startsWith(QStringLiteral("TextEncodeAceStepAudio")) && input == QStringLiteral("duration");
    const bool isLatent = cls.startsWith(QStringLiteral("EmptyAceStep")) && input == QStringLiteral("seconds");
    if (!isEncoder && !isLatent)
        return;
    const QString other = isEncoder ? QStringLiteral("seconds") : QStringLiteral("duration");
    const QString otherClass = isEncoder ? QStringLiteral("EmptyAceStep") : QStringLiteral("TextEncodeAceStepAudio");
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
        // the same family as the missing one: an ACE-Step model for an ACE-Step graph, else an SDXL picture model
        const bool ace = p.value.toString().contains(QStringLiteral("ace"), Qt::CaseInsensitive);
        QString pick = p.choices.first();
        for (const QString& c : p.choices)
            if (ace ? c.contains(QStringLiteral("ace"), Qt::CaseInsensitive)
                    : (c.contains(QStringLiteral("Juggernaut"), Qt::CaseInsensitive) || c.contains(QStringLiteral("sd_xl"), Qt::CaseInsensitive))) {
                pick = c;
                break;
            }
        setValue(p.nodeId, p.input, pick);
        changed << QStringLiteral("%1.%2: \"%3\" is not on the server, using \"%4\"").arg(p.group, p.input, p.value.toString(), pick);
    }
    return changed;
}
