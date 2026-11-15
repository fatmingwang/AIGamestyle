#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

// One editable input of the graph: a literal value on a node (a link to another node is not a parameter).
struct ParamDef
{
    enum class Kind { Int, Float, Line, Text, Bool, Combo };
    QString nodeId;
    QString input;
    QString group;          // "Positive (CLIPTextEncode)"
    QString classType;      // "CLIPTextEncode"
    QString tooltip;        // the server's own tooltip for this input, when it has one
    Kind kind = Kind::Line;
    QJsonValue value;
    QStringList choices;    // Combo
    double minimum = -1e15;
    double maximum = 1e15;
    double step = 1.0;
    int decimals = 0;
    bool isSeed() const { return kind == Kind::Int && (input == QStringLiteral("seed") || input == QStringLiteral("noise_seed")); }
};

// A ComfyUI graph in its API format (what "Save (API)" writes: {"<node id>": {"class_type", "inputs", "_meta"}}) and the
// parameters it has. The types, ranges and choices come from the server's /object_info when it knows the node class;
// otherwise from the value itself.
class Workflow
{
public:
    static Workflow textToImage();   // the built-in SDXL text-to-image graph
    static Workflow textToMusic();   // the built-in ACE-Step 1.5 song graph (tags + lyrics -> MP3)
    static Workflow textToSoundEffect();   // the built-in Stable Audio Open 1.0 sound graph (a prompt -> MP3; a dog bark)
    static Workflow textToSpeech();        // Qwen3-TTS (custom node): a preset speaker says the text -> MP3
    static Workflow textToSpeechDesigned();   // Qwen3-TTS: a voice described in words says the text -> MP3
    // The built-in graphs by id: image, music, sfx, speech, voice (a recipe's "graph", headless reset-graph).
    static QStringList builtInIds();
    static bool builtIn(const QString& id, Workflow* out);

    // load(): an API graph, a settings file (written next to every song or sound) or a recipe.
    bool load(const QString& path, QString* why);
    bool save(const QString& path, QString* why) const;
    bool fromJson(const QJsonObject& graph, QString* why);   // also a song's settings file (it holds the graph)
    bool fromSettings() const { return fromSettings_; }    // loaded from a song's settings file: same values, same seed

    // A recipe: a short file for one built-in graph, {"graph": "sfx", "prompt": "...", "seconds": 2, "seed"?: 7, ...}
    // (README.md "Recipes"). Every other key is an input name of that graph (all inputs of that name), "<node>/<input>",
    // or a friendly name (sfx/image: prompt, avoid; voice: voice; speech: style); "params" {} holds more. An array is
    // joined: tags with ", ", a speech's sentences with " ", else lines. No "graph": a song file (music). No seed: a new
    // one each run (recipeRandomSeed). "out" and "random-seed" are defaults for the run.
    static bool isRecipe(const QJsonObject& o);
    bool fromRecipe(const QJsonObject& recipe, QString* why);
    bool loadedFromRecipe() const { return fromRecipe_; }
    bool recipeRandomSeed() const { return recipeRandomSeed_; }
    QString recipeOut() const { return recipeOut_; }

    // A song's settings file: <song>.json next to the song, {"comfylive": "settings", "graph": {the graph exactly as it
    // was sent, seed and all}, "audio", "created", ...extra}. Open graph... (or load-graph) with it and Generate makes
    // the same song again.
    static QString settingsPathFor(const QString& audioPath);
    static bool writeSettings(const QString& audioPath, const QJsonObject& graph, const QString& name, const QJsonObject& extra,
                              QString* why);
    // The settings of `from` written for `to` too (a copy, a transposed song), with `extra` added. False: `from` had none.
    static bool copySettings(const QString& from, const QString& to, const QJsonObject& extra);

    const QJsonObject& graph() const { return graph_; }
    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }

    QList<ParamDef> params(const QJsonObject& objectInfo) const;
    // Also keeps tied inputs equal: an ACE-Step encoder's "duration" and the empty song latent's "seconds" are one
    // length (the ComfyUI template ties them), so setting either sets both.
    void setValue(const QString& nodeId, const QString& input, const QJsonValue& value);
    QJsonValue value(const QString& nodeId, const QString& input) const;
    // A Qwen3-TTS node is in the graph: it runs on the speech ComfyUI (its own venv with transformers 4.57.3, port 8189),
    // not the main one.
    bool needsSpeechServer() const;

    // A checkpoint / model name the server does not have: the first it does have of the same family (ACE-Step, Stable
    // Audio, T5, else SDXL). A sound or song model with none of its family stays. Returns what was changed, for the log.
    QStringList fixMissingChoices(const QJsonObject& objectInfo);

private:
    QJsonObject graph_;
    QString name_;
    bool fromSettings_ = false;
    bool fromRecipe_ = false;
    bool recipeRandomSeed_ = false;
    QString recipeOut_;
};
