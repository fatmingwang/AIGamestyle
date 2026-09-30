#include "HeadlessRunner.h"
#include "AppConfig.h"
#include "AtlasOps.h"
#include "PipelineCommands.h"
#include "PipelineEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSemaphore>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstdio>
#include <iterator>
#include <thread>

// Headless mode is a persistent TCP server (one JSON command per line in, one or more NDJSON
// events per line out) so a client can run a whole session — set the working directory, launch
// ComfyUI, then run pipeline steps — as a sequence of commands against one shared AppConfig,
// instead of one config-from-scratch process per command. This file only handles the socket
// protocol, session state, and the "[headless] ..." operator-facing stderr log; the actual
// readiness checks and process invocation are shared with the GUI via PipelineEngine
// (PipelineEngine.h). See Doc/Commands_Reference.md §12 for the wire protocol and examples.

namespace {

// ── Tiny "--flag value" / "--bool-flag" argument parser (server startup flags only) ────────

struct ArgMap {
    QMap<QString, QString> values;
    QSet<QString>          flags;

    QString get(const QString& key, const QString& def = QString()) const
    {
        return values.value(key, def);
    }
    bool has(const QString& key) const
    {
        return values.contains(key) || flags.contains(key);
    }
};

ArgMap parseArgs(const QStringList& args, const QSet<QString>& boolFlags)
{
    ArgMap m;
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (!a.startsWith(QStringLiteral("--")))
            continue;
        const QString key = a.mid(2);
        if (boolFlags.contains(key)) {
            m.flags.insert(key);
            continue;
        }
        if (i + 1 < args.size() && !args[i + 1].startsWith(QStringLiteral("--"))) {
            m.values.insert(key, args[++i]);
        } else {
            m.flags.insert(key);
        }
    }
    return m;
}

// Converts one command's JSON "args" object (as sent by the client, e.g.
// {"asset-root": "...", "overwrite": true}) into the same ArgMap shape the commands below
// already expect, so every cmdXxx() function is unchanged from the old CLI-flag world.
ArgMap argMapFromJson(const QJsonObject& args)
{
    ArgMap m;
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.value().isBool()) {
            if (it.value().toBool())
                m.flags.insert(it.key());
        } else {
            m.values.insert(it.key(), it.value().toVariant().toString());
        }
    }
    return m;
}

// Inverse of argMapFromJson() — turns an ArgMap back into the QJsonObject shape
// PipelineEngine::buildText2ImageRequest()/buildImageWithPromptRequest()/
// buildPoseFromPromptRequest() take, so those stay JSON-only/UI-agnostic like the rest of
// PipelineEngine instead of learning about ArgMap.
QJsonObject argMapToJson(const ArgMap& a)
{
    QJsonObject o;
    for (auto it = a.values.constBegin(); it != a.values.constEnd(); ++it)
        o[it.key()] = it.value();
    for (const QString& f : a.flags)
        o[f] = true;
    return o;
}

// ── NDJSON job-progress/result reporter, written straight to the client socket ─────────────
// Every command produces one job_start, zero or more step_start/marker/log/step_end/result
// events, and exactly one job_end with a `code`/`success`/`message` describing the outcome —
// the same shape the old one-shot `--json` mode produced, just delivered over the wire instead
// of this process's stdout, and now on every command (there is no other client to talk to).
struct JsonReporter {
    QTcpSocket* sink = nullptr; // the connected client; every event is written here as one line
    QString     message;        // optional override for job_end's "message"

    void emitRaw(const QJsonObject& o) const
    {
        if (!sink) return;
        sink->write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        sink->write("\n", 1);
        sink->waitForBytesWritten(3000);
    }
    void jobStart(const QString& command) const
    {
        emitRaw(QJsonObject{
            { QStringLiteral("type"),    QStringLiteral("job_start") },
            { QStringLiteral("command"), command },
        });
    }
    void jobEnd(const QString& command, int code, const QString& fallbackMessage) const
    {
        emitRaw(QJsonObject{
            { QStringLiteral("type"),    QStringLiteral("job_end") },
            { QStringLiteral("command"), command },
            { QStringLiteral("code"),    code },
            { QStringLiteral("success"), code == 0 },
            { QStringLiteral("message"), message.isEmpty() ? fallbackMessage : message },
        });
    }
    void stepStart(const QString& step, int index = 1, int total = 1) const
    {
        emitRaw(QJsonObject{
            { QStringLiteral("type"),  QStringLiteral("step_start") },
            { QStringLiteral("step"),  step },
            { QStringLiteral("index"), index },
            { QStringLiteral("total"), total },
        });
    }
    void stepEnd(const QString& step, int exitCode) const
    {
        emitRaw(QJsonObject{
            { QStringLiteral("type"),     QStringLiteral("step_end") },
            { QStringLiteral("step"),     step },
            { QStringLiteral("exitCode"), exitCode },
            { QStringLiteral("success"),  exitCode == 0 },
        });
    }
    void log(const QString& step, const QString& text) const
    {
        emitRaw(QJsonObject{
            { QStringLiteral("type"), QStringLiteral("log") },
            { QStringLiteral("stream"), QStringLiteral("headless") },
            { QStringLiteral("step"), step },
            { QStringLiteral("line"), text },
        });
    }
};

QString describeExitCode(int code)
{
    switch (code) {
    case 0:  return QStringLiteral("Job completed successfully.");
    case 1:  return QStringLiteral("The launched script failed.");
    case 2:  return QStringLiteral("A precondition or configuration problem prevented the job from running.");
    case 3:  return QStringLiteral("The style plan is stale and neither --regenerate nor --continue-stale was given.");
    case 64: return QStringLiteral("Unknown command or usage error.");
    default: return QStringLiteral("Job exited with code %1.").arg(code);
    }
}

// Extra ComfyUI-Batch overrides (--ip-weight/--denoise/--steps), as consumed by
// PipelineEngine::buildStepRequest()'s `extra` parameter — only populated when the flag was
// actually passed, so omitting it still reproduces run_comfyui_batch.py's own defaults exactly.
QJsonObject comfyUiBatchExtra(const ArgMap& a)
{
    QJsonObject extra;
    if (a.values.contains(QStringLiteral("ip-weight")))
        extra[QStringLiteral("ipWeight")] = a.get(QStringLiteral("ip-weight"));
    if (a.values.contains(QStringLiteral("denoise")))
        extra[QStringLiteral("denoise")] = a.get(QStringLiteral("denoise"));
    if (a.values.contains(QStringLiteral("steps")))
        extra[QStringLiteral("steps")] = a.get(QStringLiteral("steps"));
    return extra;
}

// ── Config mutation: the same field set the GUI's syncUiToConfig()/AppConfig cover ─────────
// Applies whichever fields are present in `a` onto `cfg`, leaving the rest untouched — used
// both to seed the server's default per-connection config at startup (from --config/CLI flags)
// and, per-connection, by the `set-config` command to mutate a session already in progress.

void applyArgsToConfig(AppConfig& cfg, const ArgMap& a)
{
    if (a.values.contains(QStringLiteral("project-root")))     cfg.projectRoot     = a.get(QStringLiteral("project-root"));
    if (a.values.contains(QStringLiteral("output-folder")))    cfg.outputFolder    = a.get(QStringLiteral("output-folder"));
    if (a.values.contains(QStringLiteral("asset-root")))       cfg.assetRoot       = a.get(QStringLiteral("asset-root"));
    if (a.values.contains(QStringLiteral("hero-reference")))   cfg.heroReference   = a.get(QStringLiteral("hero-reference"));
    if (a.values.contains(QStringLiteral("positive-prompt")))  cfg.positivePrompt  = a.get(QStringLiteral("positive-prompt"));
    if (a.values.contains(QStringLiteral("negative-prompt")))  cfg.negativePrompt  = a.get(QStringLiteral("negative-prompt"));
    if (a.values.contains(QStringLiteral("style-profile-id"))) cfg.styleProfileId  = a.get(QStringLiteral("style-profile-id"));
    if (a.values.contains(QStringLiteral("batch-id")))         cfg.batchId         = a.get(QStringLiteral("batch-id"));
    if (a.values.contains(QStringLiteral("comfyui-server")))   cfg.comfyuiServer   = a.get(QStringLiteral("comfyui-server"));
    if (a.values.contains(QStringLiteral("python-exe")))       cfg.pythonExe       = a.get(QStringLiteral("python-exe"));
    if (a.values.contains(QStringLiteral("category")))         cfg.category        = a.get(QStringLiteral("category"));
    if (a.values.contains(QStringLiteral("ip-weight")))        cfg.ipAdapterWeight = a.get(QStringLiteral("ip-weight")).toDouble();
    if (a.values.contains(QStringLiteral("denoise")))          cfg.denoise         = a.get(QStringLiteral("denoise")).toDouble();
    if (a.values.contains(QStringLiteral("steps")))            cfg.steps           = a.get(QStringLiteral("steps")).toInt();
    if (a.has(QStringLiteral("include-spine")))                cfg.includeSpine    = true;
}

// Built once at server startup from --config <file> / CLI flags; used as the starting point
// for every new connection's session config (each connection then gets its own copy to mutate).
AppConfig buildSeedConfig(const ArgMap& a)
{
    AppConfig cfg;

    if (a.values.contains(QStringLiteral("config"))) {
        QFile f(a.get(QStringLiteral("config")));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            cfg = PipelineEngine::configFromJson(o, cfg);
        } else {
            fprintf(stderr, "[headless] WARNING: could not open --config file: %s\n",
                    qPrintable(a.get(QStringLiteral("config"))));
        }
    }

    applyArgsToConfig(cfg, a);
    cfg.resolveRelativePaths();
    return cfg;
}

// ── Non-interactive equivalents of MainWindow's confirmation dialogs ───────
// The GUI pops a QMessageBox when the output folder isn't empty, or when the existing
// style_plan.json looks stale. Headless mode has no one to click a button, so both gates
// require an explicit flag instead — but the underlying verdict (is it actually stale? empty?)
// comes from PipelineEngine, the exact same JSON the GUI's dialogs are driven by.

bool confirmOutputFolderHeadless(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QJsonObject state = PipelineEngine::checkOutputFolderState(cfg);
    if (!state.value(QStringLiteral("applicable")).toBool(true)) return true;
    if (state.value(QStringLiteral("empty")).toBool()) return true;

    const QString targetFolder = state.value(QStringLiteral("targetFolder")).toString();
    const int entryCount       = state.value(QStringLiteral("entryCount")).toInt();

    if (a.has(QStringLiteral("clean"))) {
        if (!QDir(targetFolder).removeRecursively()) {
            const QString msg = QStringLiteral("Failed to delete output folder: %1").arg(targetFolder);
            fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
            jr.message = msg;
            return false;
        }
        fprintf(stderr, "[headless] Output folder deleted: %s\n", qPrintable(targetFolder));
        return true;
    }
    if (a.has(QStringLiteral("overwrite"))) {
        fprintf(stderr, "[headless] WARNING: output folder not empty, writing into it anyway: %s\n",
                qPrintable(targetFolder));
        return true;
    }

    const QString msg = QStringLiteral(
        "Output folder already has %1 item(s): %2. Pass --overwrite to write into it anyway, "
        "or --clean to delete it first.").arg(entryCount).arg(targetFolder);
    fprintf(stderr,
        "[headless] ERROR: output folder already has %d item(s): %s\n"
        "             Pass --overwrite to write into it anyway, or --clean to delete it first.\n",
        entryCount, qPrintable(targetFolder));
    jr.message = msg;
    return false;
}

enum class FreshResult { Fresh, ContinueAnyway, NeedRegenerate, Abort };

FreshResult checkStylePlanFreshHeadless(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QJsonObject state = PipelineEngine::checkStylePlanState(cfg);
    if (!state.value(QStringLiteral("applicable")).toBool(true)) return FreshResult::Fresh;
    if (state.value(QStringLiteral("fresh")).toBool()) return FreshResult::Fresh;

    const QString reason = state.value(QStringLiteral("reason")).toString();
    fprintf(stderr, "[headless] WARNING: stale style_plan detected: %s\n", qPrintable(reason));

    if (a.has(QStringLiteral("regenerate")))     return FreshResult::NeedRegenerate;
    if (a.has(QStringLiteral("continue-stale"))) return FreshResult::ContinueAnyway;

    fprintf(stderr,
        "[headless] ERROR: style_plan.json is outdated. Pass --regenerate to re-run "
        "scan->classify->link->plan-style first, or --continue-stale to proceed anyway.\n");
    jr.message = QStringLiteral("style_plan.json is outdated: %1").arg(reason.split('\n').first());
    return FreshResult::Abort;
}

// ── Run one step through the shared engine, with headless-flavored logging ─
// stepIndexInJob/stepCountInJob only affect the JSON step_start event's "index"/"total" fields
// (e.g. "3 of 5") for composite jobs like `all`; standalone step commands pass the defaults.

int runStep(StepIndex idx, const AppConfig& cfg, JsonReporter& jr, const QJsonObject& extra = QJsonObject(),
            int stepIndexInJob = 1, int stepCountInJob = 1)
{
    const QJsonObject request = PipelineEngine::buildStepRequest(idx, cfg, cfg.projectRoot, extra);
    const QString step = stepName(idx);

    QStringList argv;
    for (const QJsonValue& v : request.value(QStringLiteral("args")).toArray())
        argv << v.toString();

    fprintf(stderr, "[headless] --- %s ---\n", qPrintable(step));
    fprintf(stderr, "[headless] Launching: %s %s\n",
            qPrintable(request.value(QStringLiteral("program")).toString()),
            qPrintable(argv.join(QChar(' '))));

    jr.stepStart(step, stepIndexInJob, stepCountInJob);

    const QJsonObject result = PipelineEngine::runStepRequestSync(request, [&](const QJsonObject& ev) {
        QJsonObject tagged = ev;
        tagged[QStringLiteral("step")] = step;
        jr.emitRaw(tagged);
    });

    if (!result.value(QStringLiteral("started")).toBool()) {
        const QString err = result.value(QStringLiteral("error")).toString();
        fprintf(stderr, "[headless] ERROR: failed to start: %s\n", qPrintable(err));
        jr.stepEnd(step, -1);
        jr.message = QStringLiteral("Failed to start step '%1': %2").arg(step, err);
        return -1;
    }
    const int rc = result.value(QStringLiteral("exitCode")).toInt();
    jr.stepEnd(step, rc);
    if (rc != 0) {
        fprintf(stderr, "[headless] Step '%s' FAILED (exit %d)\n", qPrintable(step), rc);
        jr.message = QStringLiteral("Step '%1' failed (exit %2).").arg(step).arg(rc);
    }
    return rc;
}

// Same shape as runStep(), but for a single ad-hoc request (text2image/imagewithprompt/
// pose-from-prompt) that isn't one of the 5 fixed pipeline steps — `request` already carries
// its own "ok"/"error" from PipelineEngine's builder, checked here the same way
// buildComfyUiLaunchRequest()'s callers already do. On success, if the request carries an
// "outputPath" (all three do), emits it as a `result` event so the client knows where to look.
int runAdhocRequest(const QString& label, const QJsonObject& request, JsonReporter& jr)
{
    if (!request.value(QStringLiteral("ok")).toBool()) {
        const QString err = request.value(QStringLiteral("error")).toString();
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(err));
        jr.message = err;
        return 2;
    }

    QStringList argv;
    for (const QJsonValue& v : request.value(QStringLiteral("args")).toArray())
        argv << v.toString();

    fprintf(stderr, "[headless] --- %s ---\n", qPrintable(label));
    fprintf(stderr, "[headless] Launching: %s %s\n",
            qPrintable(request.value(QStringLiteral("program")).toString()),
            qPrintable(argv.join(QChar(' '))));

    jr.stepStart(label);

    const QJsonObject result = PipelineEngine::runStepRequestSync(request, [&](const QJsonObject& ev) {
        QJsonObject tagged = ev;
        tagged[QStringLiteral("step")] = label;
        jr.emitRaw(tagged);
    });

    if (!result.value(QStringLiteral("started")).toBool()) {
        const QString err = result.value(QStringLiteral("error")).toString();
        fprintf(stderr, "[headless] ERROR: failed to start: %s\n", qPrintable(err));
        jr.stepEnd(label, -1);
        jr.message = QStringLiteral("Failed to start '%1': %2").arg(label, err);
        return -1;
    }
    const int rc = result.value(QStringLiteral("exitCode")).toInt();
    jr.stepEnd(label, rc);
    if (rc != 0) {
        fprintf(stderr, "[headless] '%s' FAILED (exit %d)\n", qPrintable(label), rc);
        jr.message = QStringLiteral("'%1' failed (exit %2).").arg(label).arg(rc);
        return rc;
    }

    if (request.contains(QStringLiteral("outputPath"))) {
        jr.emitRaw(QJsonObject{
            { QStringLiteral("type"),       QStringLiteral("result") },
            { QStringLiteral("command"),    label },
            { QStringLiteral("outputPath"), request.value(QStringLiteral("outputPath")) },
        });
    }
    return 0;
}

// ── Subcommands ──────────────────────────────────────────────────────────

int cmdAll(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr); // fwd decl (comfyui-batch can delegate to it)

// Session-state commands — the headless equivalent of MainWindow::onSetWorkingDirectory() /
// syncUiToConfig(): these mutate *this connection's* AppConfig so later commands on the same
// connection (comfyui-launch, scan, comfyui-batch, ...) pick up the change.

int cmdSetWorkingDir(AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QString pathArg = a.get(QStringLiteral("path"));
    if (pathArg.isEmpty()) {
        const QString msg = QStringLiteral("set-working-dir requires {\"args\":{\"path\":<dir>}}.");
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }

    const QString resolved = QDir::cleanPath(QDir(QDir::currentPath()).absoluteFilePath(pathArg));
    if (!QDir(resolved).exists()) {
        const QString msg = QStringLiteral("Working directory does not exist: %1").arg(resolved);
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }

    QDir::setCurrent(resolved); // mirrors the GUI's actual chdir, in case anything still relies on CWD
    cfg.projectRoot = resolved;
    fprintf(stderr, "[headless] Working directory / project root set to: %s\n", qPrintable(resolved));
    jr.message = QStringLiteral("Working directory set to: %1").arg(resolved);
    return 0;
}

int cmdSetConfig(AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    applyArgsToConfig(cfg, a);
    cfg.resolveRelativePaths();
    fprintf(stderr, "[headless] Config updated.\n");
    jr.message = QStringLiteral("Config updated.");
    return 0;
}

int cmdGetConfig(const AppConfig& cfg, JsonReporter& jr)
{
    jr.emitRaw(QJsonObject{
        { QStringLiteral("type"),    QStringLiteral("result") },
        { QStringLiteral("command"), QStringLiteral("get-config") },
        { QStringLiteral("config"),  PipelineEngine::configToJson(cfg) },
    });
    jr.message = QStringLiteral("OK");
    return 0;
}

int cmdCheck(const AppConfig& cfg, const ArgMap&, JsonReporter& jr)
{
    const QString script = QDir::cleanPath(cfg.projectRoot + QStringLiteral("/_pipeline/tools/check_style_pipeline.py"));
    if (!QFile::exists(script)) {
        const QString msg = QStringLiteral("check script not found: %1").arg(script);
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }
    QString addr, port;
    PipelineEngine::splitHostPort(cfg.comfyuiServer, addr, port);

    const QStringList args = { script, QStringLiteral("--project-root"), cfg.projectRoot,
                                QStringLiteral("--comfy-port"), port, QStringLiteral("--json") };

    // check_style_pipeline.py's own --json output is pretty-printed (multi-line), which would
    // break the "one JSON object per line" NDJSON contract if forwarded as-is. Capture it
    // instead and re-emit it compactly as one `result` event.
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(cfg.pythonExe, args);
    proc.waitForFinished(-1);
    const int rc = proc.exitCode();

    const QByteArray out = proc.readAllStandardOutput();
    fputs(out.constData(), stderr); // still visible to a human tailing this process's stderr
    const QByteArray err = proc.readAllStandardError();
    if (!err.isEmpty()) fputs(err.constData(), stderr);

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
    QJsonObject resultEvent{
        { QStringLiteral("type"),    QStringLiteral("result") },
        { QStringLiteral("command"), QStringLiteral("check") },
    };
    if (perr.error == QJsonParseError::NoError && doc.isObject())
        resultEvent[QStringLiteral("report")] = doc.object();
    else
        resultEvent[QStringLiteral("raw")] = QString::fromUtf8(out);
    jr.emitRaw(resultEvent);

    jr.message = rc == 0 ? QStringLiteral("Environment is ready.")
                          : QStringLiteral("Environment is not ready — see the [FAIL] items above.");
    return rc;
}

int cmdComfyuiStatus(const AppConfig& cfg, JsonReporter& jr)
{
    const bool up = PipelineEngine::comfyuiReachable(cfg);
    jr.message = up ? QStringLiteral("ComfyUI is reachable.") : QStringLiteral("ComfyUI is not reachable.");
    return up ? 0 : 1;
}

int cmdComfyuiLaunch(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    if (PipelineEngine::comfyuiReachable(cfg)) {
        fprintf(stderr, "[headless] ComfyUI is already running at %s\n", qPrintable(cfg.comfyuiServer));
        jr.message = QStringLiteral("ComfyUI was already running.");
        return 0;
    }

    const QJsonObject req = PipelineEngine::buildComfyUiLaunchRequest(cfg);
    if (!req.value(QStringLiteral("ok")).toBool()) {
        const QString msg = req.value(QStringLiteral("error")).toString();
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }

    QStringList args;
    for (const QJsonValue& v : req.value(QStringLiteral("args")).toArray())
        args << v.toString();
    const bool started = QProcess::startDetached(
        req.value(QStringLiteral("program")).toString(), args,
        req.value(QStringLiteral("workingDirectory")).toString());

    if (!started) {
        fprintf(stderr, "[headless] ERROR: failed to launch ComfyUI\n");
        jr.message = QStringLiteral("Failed to launch ComfyUI.");
        return 3;
    }

    const int timeoutSec = a.get(QStringLiteral("wait-timeout"), QStringLiteral("60")).toInt();
    fprintf(stderr, "[headless] ComfyUI launch requested, waiting up to %ds for it to come up...\n", timeoutSec);
    jr.log(QStringLiteral("comfyui-launch"), QStringLiteral("launched, waiting up to %1s").arg(timeoutSec));
    for (int waited = 0; waited < timeoutSec; waited += 2) {
        QThread::sleep(2);
        if (PipelineEngine::comfyuiReachable(cfg)) {
            fprintf(stderr, "[headless] ComfyUI is up (after %ds).\n", waited + 2);
            jr.message = QStringLiteral("ComfyUI came up after %1s.").arg(waited + 2);
            return 0;
        }
        jr.log(QStringLiteral("comfyui-launch"), QStringLiteral("still waiting (%1s elapsed)").arg(waited + 2));
    }
    fprintf(stderr, "[headless] WARNING: ComfyUI still not reachable after %ds "
                     "— it may still be loading models; check its log.\n", timeoutSec);
    jr.message = QStringLiteral("ComfyUI still not reachable after %1s — it may still be loading models.")
                     .arg(timeoutSec);
    return 1;
}

int cmdComfyUiBatch(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    if (!confirmOutputFolderHeadless(cfg, a, jr)) return 2;

    const FreshResult fr = checkStylePlanFreshHeadless(cfg, a, jr);
    if (fr == FreshResult::Abort)
        return 3;
    if (fr == FreshResult::NeedRegenerate) {
        fprintf(stderr, "[headless] Regenerating: running the full pipeline "
                         "(scan -> classify -> link -> plan-style -> comfyui-batch)...\n");
        jr.log(QStringLiteral("comfyui-batch"), QStringLiteral("stale plan — regenerating via full pipeline"));
        return cmdAll(cfg, a, jr);
    }

    const int rc = runStep(StepIndex::ComfyUiBatch, cfg, jr, comfyUiBatchExtra(a));
    if (rc == 0) jr.message = QStringLiteral("ComfyUI Batch completed successfully.");
    return rc;
}

int cmdAll(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    if (!confirmOutputFolderHeadless(cfg, a, jr)) return 2;

    const StepIndex steps[] = { StepIndex::Scan, StepIndex::Classify, StepIndex::Link, StepIndex::PlanStyle,
                                 StepIndex::ComfyUiBatch };
    const int total = int(std::size(steps));
    for (int i = 0; i < total; ++i) {
        const QJsonObject extra = (steps[i] == StepIndex::ComfyUiBatch) ? comfyUiBatchExtra(a) : QJsonObject();
        const int rc = runStep(steps[i], cfg, jr, extra, i + 1, total);
        if (rc != 0) return rc;
    }

    fprintf(stderr, "[headless] === Pipeline completed successfully ===\n");
    jr.message = QStringLiteral("Pipeline completed successfully (scan, classify, link, plan-style, comfyui-batch).");
    return 0;
}

// ── Game-asset generation commands ──────────────────────────────────────────
// text2image/imagewithprompt/pose-from-prompt build their request via PipelineEngine (the same
// pattern buildStepRequest()/buildComfyUiLaunchRequest() already use) and run it through the
// shared runAdhocRequest() above; atlas-extract/atlas-merge call the shared AtlasOps functions
// directly since those need no subprocess at all. See Doc/Headless_Game_Asset_Generation_Plan.md.

int cmdText2Image(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QJsonObject req = PipelineEngine::buildText2ImageRequest(cfg, argMapToJson(a));
    const int rc = runAdhocRequest(QStringLiteral("text2image"), req, jr);
    if (rc == 0)
        jr.message = QStringLiteral("Generated: %1").arg(req.value(QStringLiteral("outputPath")).toString());
    return rc;
}

int cmdImageWithPrompt(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QJsonObject req = PipelineEngine::buildImageWithPromptRequest(cfg, argMapToJson(a));
    const int rc = runAdhocRequest(QStringLiteral("imagewithprompt"), req, jr);
    if (rc == 0)
        jr.message = QStringLiteral("Generated: %1").arg(req.value(QStringLiteral("outputPath")).toString());
    return rc;
}

int cmdPoseFromPrompt(const AppConfig& cfg, const ArgMap& a, JsonReporter& jr)
{
    const QJsonObject req = PipelineEngine::buildPoseFromPromptRequest(cfg, argMapToJson(a));
    const int rc = runAdhocRequest(QStringLiteral("pose-from-prompt"), req, jr);
    if (rc == 0)
        jr.message = QStringLiteral("Generated: %1").arg(req.value(QStringLiteral("outputPath")).toString());
    return rc;
}

int cmdAtlasExtract(const ArgMap& a, JsonReporter& jr)
{
    const QString atlasPath = a.get(QStringLiteral("atlas-path"));
    if (atlasPath.isEmpty()) {
        const QString msg = QStringLiteral("atlas-extract requires args.atlas-path.");
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }
    const double scale = a.get(QStringLiteral("scale"), QStringLiteral("1.0")).toDouble();

    QStringList log;
    const AtlasOps::ExtractResult result = AtlasOps::extractAtlas(atlasPath, scale, log);
    for (const QString& line : std::as_const(log)) {
        fprintf(stderr, "[headless] %s\n", qPrintable(line));
        jr.log(QStringLiteral("atlas-extract"), line);
    }

    if (!result.ok) {
        jr.message = result.error;
        return 2;
    }

    jr.emitRaw(QJsonObject{
        { QStringLiteral("type"),      QStringLiteral("result") },
        { QStringLiteral("command"),   QStringLiteral("atlas-extract") },
        { QStringLiteral("outputDir"), result.outputDir },
        { QStringLiteral("saved"),     result.saved },
        { QStringLiteral("skipped"),   result.skipped },
    });
    jr.message = QStringLiteral("Extracted %1 sprite(s) to %2 (%3 skipped).")
                     .arg(result.saved).arg(result.outputDir).arg(result.skipped);
    return 0;
}

int cmdAtlasMerge(const ArgMap& a, JsonReporter& jr)
{
    const QString atlasPath = a.get(QStringLiteral("atlas-path"));
    if (atlasPath.isEmpty()) {
        const QString msg = QStringLiteral("atlas-merge requires args.atlas-path.");
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }
    const double scale = a.get(QStringLiteral("scale"), QStringLiteral("1.0")).toDouble();

    AtlasOps::MergeMode mode = AtlasOps::MergeMode::SeparatePages;
    if (a.get(QStringLiteral("merge-mode")) == QStringLiteral("single"))
        mode = AtlasOps::MergeMode::CombineHorizontal;

    // Defaults to Fail (never silently overwrite) — same "ask before touching an existing file"
    // caution the GUI's own default button (Cancel) applies.
    AtlasOps::SaveConflict conflict = AtlasOps::SaveConflict::Fail;
    const QString conflictArg = a.get(QStringLiteral("conflict"), QStringLiteral("fail"));
    if (conflictArg == QStringLiteral("replace")) conflict = AtlasOps::SaveConflict::Replace;
    else if (conflictArg == QStringLiteral("rename")) conflict = AtlasOps::SaveConflict::RenameMerged;

    QStringList log;
    const AtlasOps::MergeResult result = AtlasOps::mergeAtlas(atlasPath, scale, mode, conflict, log);
    for (const QString& line : std::as_const(log)) {
        fprintf(stderr, "[headless] %s\n", qPrintable(line));
        jr.log(QStringLiteral("atlas-merge"), line);
    }

    if (!result.ok) {
        jr.message = result.error;
        return 2;
    }

    jr.emitRaw(QJsonObject{
        { QStringLiteral("type"),       QStringLiteral("result") },
        { QStringLiteral("command"),    QStringLiteral("atlas-merge") },
        { QStringLiteral("savedFiles"), QJsonArray::fromStringList(result.savedFiles) },
        { QStringLiteral("merged"),     result.merged },
        { QStringLiteral("missing"),    result.missing },
    });
    jr.message = QStringLiteral("Merged %1 sprite(s), %2 missing, %3 file(s) saved.")
                     .arg(result.merged).arg(result.missing).arg(result.savedFiles.size());
    return 0;
}

// robocopy wrapper for uploading a local output folder to a NAS target — the headless
// equivalent of ResourceReplaceWidget's "Upload Output to NAS" button. Its interactive
// "target folder already has content" choice (delete old data / move to a timestamped folder /
// cancel) becomes the `conflict` arg here instead of a QMessageBox.
int cmdNasUpload(const ArgMap& a, JsonReporter& jr)
{
    const QString source = a.get(QStringLiteral("source"));
    const QString target = a.get(QStringLiteral("target"));
    if (source.isEmpty() || target.isEmpty()) {
        const QString msg = QStringLiteral("nas-upload requires args.source and args.target.");
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }
    if (!QDir(source).exists()) {
        const QString msg = QStringLiteral("Source folder does not exist: %1").arg(source);
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }

    // Defaults to fail (never silently delete/move NAS content) — matches the GUI dialog's
    // own default button (Cancel).
    const QString conflict = a.get(QStringLiteral("conflict"), QStringLiteral("fail"));
    QDir targetDir(target);
    const bool targetHasContent = targetDir.exists() &&
        !targetDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();

    if (targetHasContent) {
        if (conflict == QStringLiteral("overwrite")) {
            fprintf(stderr, "[headless] Target not empty, deleting: %s\n", qPrintable(target));
            if (!targetDir.removeRecursively()) {
                const QString msg = QStringLiteral("Cannot delete target folder: %1").arg(target);
                fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
                jr.message = msg;
                return 2;
            }
            QDir().mkpath(target);
        } else if (conflict == QStringLiteral("rename")) {
            const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
            QString parentPath = QDir::toNativeSeparators(target);
            while (parentPath.endsWith('\\') || parentPath.endsWith('/')) parentPath.chop(1);
            const int lastSep = qMax(parentPath.lastIndexOf('\\'), parentPath.lastIndexOf('/'));
            if (lastSep > 2) parentPath = parentPath.left(lastSep);
            const QString timestampPath = parentPath + QStringLiteral("\\") + timestamp;

            fprintf(stderr, "[headless] Target not empty, moving to: %s\n", qPrintable(timestampPath));
            if (!QDir().rename(target, timestampPath)) {
                const QString msg = QStringLiteral("Cannot move existing target: %1 -> %2")
                                         .arg(target, timestampPath);
                fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
                jr.message = msg;
                return 2;
            }
            QDir().mkpath(target);
        } else {
            const QString msg = QStringLiteral(
                "Target folder already has content: %1. Pass args.conflict=\"overwrite\" or "
                "\"rename\" to proceed.").arg(target);
            fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
            jr.message = msg;
            return 2;
        }
    } else if (!targetDir.exists() && !QDir().mkpath(target)) {
        const QString msg = QStringLiteral("Cannot create target folder: %1").arg(target);
        fprintf(stderr, "[headless] ERROR: %s\n", qPrintable(msg));
        jr.message = msg;
        return 2;
    }

    fprintf(stderr, "[headless] Uploading %s -> %s ...\n", qPrintable(source), qPrintable(target));
    jr.log(QStringLiteral("nas-upload"), QStringLiteral("robocopy %1 -> %2").arg(source, target));

    QProcess proc;
    proc.start(QStringLiteral("robocopy"),
               { source, target, QStringLiteral("/E"), QStringLiteral("/IS"), QStringLiteral("/IT") });
    proc.waitForFinished(-1);
    const int rc = proc.exitCode();

    // robocopy: exit < 8 = success (0=nothing to copy, 1=copied, 2-7=partial/extra)
    if (rc < 8) {
        jr.message = QStringLiteral("Upload complete (robocopy exit=%1).").arg(rc);
        return 0;
    }
    fprintf(stderr, "[headless] ERROR: robocopy failed (exit %d)\n", rc);
    jr.message = QStringLiteral("Upload failed, robocopy exit code: %1.").arg(rc);
    return 1;
}

// Dispatches one already-parsed command against the session's persistent AppConfig. Every
// command here writes job_start/.../job_end for itself to jr.sink before returning.
int dispatchCommand(const QString& cmd, const ArgMap& a, AppConfig& cfg, JsonReporter& jr)
{
    if (cmd == QStringLiteral("set-working-dir")) return cmdSetWorkingDir(cfg, a, jr);
    if (cmd == QStringLiteral("set-config"))      return cmdSetConfig(cfg, a, jr);
    if (cmd == QStringLiteral("get-config"))      return cmdGetConfig(cfg, jr);
    if (cmd == QStringLiteral("check"))           return cmdCheck(cfg, a, jr);
    if (cmd == QStringLiteral("comfyui-status"))  return cmdComfyuiStatus(cfg, jr);
    if (cmd == QStringLiteral("comfyui-launch"))  return cmdComfyuiLaunch(cfg, a, jr);
    if (cmd == QStringLiteral("scan"))
        return confirmOutputFolderHeadless(cfg, a, jr) ? runStep(StepIndex::Scan, cfg, jr) : 2;
    if (cmd == QStringLiteral("classify"))        return runStep(StepIndex::Classify, cfg, jr);
    if (cmd == QStringLiteral("link"))            return runStep(StepIndex::Link, cfg, jr);
    if (cmd == QStringLiteral("plan-style"))      return runStep(StepIndex::PlanStyle, cfg, jr);
    if (cmd == QStringLiteral("comfyui-batch"))   return cmdComfyUiBatch(cfg, a, jr);
    if (cmd == QStringLiteral("simple-batch"))
        return confirmOutputFolderHeadless(cfg, a, jr) ? runStep(StepIndex::SimpleBatch, cfg, jr) : 2;
    if (cmd == QStringLiteral("all"))             return cmdAll(cfg, a, jr);
    if (cmd == QStringLiteral("text2image"))      return cmdText2Image(cfg, a, jr);
    if (cmd == QStringLiteral("imagewithprompt")) return cmdImageWithPrompt(cfg, a, jr);
    if (cmd == QStringLiteral("pose-from-prompt")) return cmdPoseFromPrompt(cfg, a, jr);
    if (cmd == QStringLiteral("atlas-extract"))   return cmdAtlasExtract(a, jr);
    if (cmd == QStringLiteral("atlas-merge"))     return cmdAtlasMerge(a, jr);
    if (cmd == QStringLiteral("nas-upload"))      return cmdNasUpload(a, jr);

    const QString msg = QStringLiteral("Unknown command: %1").arg(cmd);
    fprintf(stderr, "[headless] %s\n", qPrintable(msg));
    jr.message = msg;
    return 64;
}

// ── One client connection == one session: its own AppConfig (seeded from server startup,
// then mutated in place by set-working-dir/set-config), served until it sends exit/quit or
// disconnects. The server only serves one session at a time (see runHeadlessServer below).

void handleClientSession(QTcpSocket& socket, AppConfig cfg)
{
    fprintf(stderr, "[headless] Client connected: %s\n", qPrintable(socket.peerAddress().toString()));

    while (socket.state() == QAbstractSocket::ConnectedState) {
        while (!socket.canReadLine()) {
            if (!socket.waitForReadyRead(-1))
                goto disconnected; // remote closed, or a socket error — either way, end the session
        }

        {
            const QByteArray line = socket.readLine().trimmed();
            if (line.isEmpty())
                continue;

            QJsonParseError perr;
            const QJsonObject req = QJsonDocument::fromJson(line, &perr).object();
            if (perr.error != QJsonParseError::NoError) {
                const QJsonObject err{
                    { QStringLiteral("type"),    QStringLiteral("error") },
                    { QStringLiteral("message"), QStringLiteral("Invalid JSON: %1").arg(perr.errorString()) },
                };
                socket.write(QJsonDocument(err).toJson(QJsonDocument::Compact) + '\n');
                socket.waitForBytesWritten(3000);
                continue;
            }

            const QString cmd = req.value(QStringLiteral("command")).toString();
            if (cmd.isEmpty())
                continue;
            if (cmd == QStringLiteral("exit") || cmd == QStringLiteral("quit")) {
                fprintf(stderr, "[headless] Client requested disconnect.\n");
                socket.disconnectFromHost();
                break;
            }

            JsonReporter jr;
            jr.sink = &socket;
            const ArgMap a = argMapFromJson(req.value(QStringLiteral("args")).toObject());

            jr.jobStart(cmd);
            const int rc = dispatchCommand(cmd, a, cfg, jr);
            jr.jobEnd(cmd, rc, describeExitCode(rc));
        }
    }

disconnected:
    fprintf(stderr, "[headless] Client disconnected.\n");
}

// Startup flags shared by both entry points (--headless and --headless-test): which port/
// interface to listen on, and the config new connections start from.
struct StartupOptions {
    QHostAddress host = QHostAddress::LocalHost;
    quint16      port = 8765;
    AppConfig    seedCfg;
};

StartupOptions parseStartupOptions(const ArgMap& a, const char* logPrefix)
{
    StartupOptions opt;
    opt.port = quint16(a.get(QStringLiteral("port"), QStringLiteral("8765")).toUShort());
    if (a.values.contains(QStringLiteral("host"))) {
        const QString hostArg = a.get(QStringLiteral("host"));
        if (!opt.host.setAddress(hostArg))
            fprintf(stderr, "%s WARNING: invalid --host '%s', binding to 127.0.0.1 instead\n",
                    logPrefix, qPrintable(hostArg));
    }
    opt.seedCfg = buildSeedConfig(a);
    return opt;
}

int runHeadlessServer(const QHostAddress& host, quint16 port, const AppConfig& seedCfg)
{
    QTcpServer server;
    if (!server.listen(host, port)) {
        fprintf(stderr, "[headless] ERROR: failed to listen on %s:%u: %s\n",
                qPrintable(host.toString()), port, qPrintable(server.errorString()));
        return 2;
    }
    fprintf(stderr,
        "[headless] Server listening on %s:%u\n"
        "[headless] Send one JSON command per line, e.g.:\n"
        "[headless]   {\"command\":\"set-working-dir\",\"args\":{\"path\":\"D:/Work/GamestyleAI\"}}\n"
        "[headless]   {\"command\":\"comfyui-launch\"}\n"
        "[headless] See Doc/Commands_Reference.md \xc2\xa7" "12 for the full protocol.\n",
        qPrintable(host.toString()), port);

    // Serves one client session fully (until it disconnects) before accepting the next — the
    // intended usage is one automation client running a sequential session, not concurrent
    // clients sharing state.
    while (true) {
        if (!server.waitForNewConnection(-1))
            continue;
        QTcpSocket* socket = server.nextPendingConnection();
        if (!socket)
            continue;

        handleClientSession(*socket, seedCfg);

        socket->close();
        delete socket;
    }
}

void printUsage()
{
    fprintf(stderr, "%s",
        "GameStyleAI --headless [--port <n>=8765] [--host <addr>=127.0.0.1] [--config <path.json>] [--flag value ...]\n"
        "\n"
        "Starts a persistent TCP server (does not exit). Connect and send one JSON object per\n"
        "line; each command replies with one or more NDJSON events (job_start, step_start/\n"
        "step_end/log/result, job_end), then the connection stays open for the next command in\n"
        "the same session — e.g. set-working-dir, then comfyui-launch, then scan/comfyui-batch.\n"
        "\n"
        "Commands (send as {\"command\":\"<name>\",\"args\":{...}}):\n"
        "  set-working-dir  {\"path\":<dir>}         chdir + set Project Root for this session.\n"
        "  set-config       {<AppConfig field>: value, ...}   merge fields into this session's config\n"
        "                     (project-root, output-folder, asset-root, hero-reference,\n"
        "                      positive-prompt, negative-prompt, style-profile-id, batch-id,\n"
        "                      comfyui-server, python-exe, category, ip-weight, denoise, steps,\n"
        "                      include-spine).\n"
        "  get-config                                 returns the session's current config as JSON.\n"
        "  check                                       readiness check (git/python/ComfyUI/venv/models/port).\n"
        "  comfyui-status                              reachable/unreachable via job_end.success.\n"
        "  comfyui-launch   {\"wait-timeout\":<s>=60}    start ComfyUI if not already reachable.\n"
        "  scan / classify / link / plan-style / comfyui-batch / simple-batch / all\n"
        "     same flags as the old one-shot CLI, e.g. {\"asset-root\":<dir>,\"overwrite\":true}.\n"
        "  text2image       {\"prompt\":<text>,\"output-name\",\"output-dir\",\"width\",\"height\"}\n"
        "                     generate a new image straight from a prompt, no source asset.\n"
        "  imagewithprompt  {\"reference\":<img>,\"positive-prompt\" or \"category\",\"negative-prompt\",\n"
        "                     \"output-dir\",\"output-name\",\"seed\",\"steps\",\"cfg\",\"denoise\",\"ip-weight\"}\n"
        "                     prompt-guided variant of one reference image.\n"
        "  pose-from-prompt {\"name\":<n>,\"description\":<text>,\"subject-type\",\"loop\",\"frames\",\n"
        "                     \"backend\",\"api-url\",\"output\"}   generate pose/skeleton JSON from a prompt.\n"
        "  atlas-extract    {\"atlas-path\":<file.atlas>,\"scale\"=1.0}\n"
        "                     unpack a Spine atlas into individual sprite PNGs.\n"
        "  atlas-merge      {\"atlas-path\":<file.atlas>,\"scale\"=1.0,\n"
        "                     \"merge-mode\":\"separate\"|\"single\",\"conflict\":\"fail\"|\"replace\"|\"rename\"}\n"
        "                     pack sprite PNGs (from atlas-extract's output) back into the atlas.\n"
        "  nas-upload       {\"source\":<dir>,\"target\":<dir>,\"conflict\":\"fail\"|\"overwrite\"|\"rename\"}\n"
        "                     robocopy a local output folder to a NAS target.\n"
        "  exit / quit                                 closes the connection.\n"
        "\n"
        "Startup flags:\n"
        "  --port <n>            TCP port to listen on (default 8765).\n"
        "  --host <addr>         Interface to bind (default 127.0.0.1 — localhost only).\n"
        "  --config <path.json>  Seed the default session config for new connections.\n"
        "  Any other --flag value pair from the list above also seeds the default config, same\n"
        "  as passing it via set-config on connect.\n"
        "\n"
        "GameStyleAI --headless-test [--port <n>] [--host <addr>] [--config <path.json>]\n"
        "  One-process smoke test: starts this same server on a background thread, connects a\n"
        "  built-in client, runs get-config/set-working-dir/comfyui-status/an-unknown-command\n"
        "  through it, prints ALL CHECKS PASSED or SOME CHECKS FAILED, then exits. Use this to\n"
        "  answer \"is the server even working\" without needing a separate client.\n"
        "\n"
        "See Doc/Commands_Reference.md \xc2\xa7" "12 for the full protocol and examples.\n");
}

} // namespace

int runHeadless(const QStringList& argsIn)
{
    QStringList args = argsIn;
    args.removeAll(QStringLiteral("--headless"));

    static const QSet<QString> boolFlags = {
        QStringLiteral("include-spine"), QStringLiteral("help"),
    };
    const ArgMap a = parseArgs(args, boolFlags);

    if (a.has(QStringLiteral("help"))) {
        printUsage();
        return 0;
    }

    const StartupOptions opt = parseStartupOptions(a, "[headless]");
    return runHeadlessServer(opt.host, opt.port, opt.seedCfg);
}

int runHeadlessSelfTest(const QStringList& argsIn)
{
    QStringList args = argsIn;
    args.removeAll(QStringLiteral("--headless-test"));

    static const QSet<QString> boolFlags = {
        QStringLiteral("include-spine"), QStringLiteral("help"),
    };
    const ArgMap a = parseArgs(args, boolFlags);

    if (a.has(QStringLiteral("help"))) {
        printUsage();
        return 0;
    }

    const StartupOptions opt = parseStartupOptions(a, "[headless-test]");

    // Start the real server on a background thread, exactly like runHeadlessServer(), except we
    // capture `opt` BY VALUE into the lambda: this thread is detach()ed and outlives this
    // function's stack frame (the whole process exits right after the checks below finish), so
    // it must own its own copy rather than hold a dangling reference to a local variable.
    QSemaphore listening(0);
    bool serverOk = false;
    std::thread serverThread([opt, &listening, &serverOk] {
        QTcpServer server;
        if (!server.listen(opt.host, opt.port)) {
            fprintf(stderr, "[headless-test] ERROR: failed to listen on %s:%u: %s\n",
                    qPrintable(opt.host.toString()), opt.port, qPrintable(server.errorString()));
            listening.release();
            return;
        }
        serverOk = true;
        fprintf(stderr, "[headless-test] Built-in server listening on %s:%u\n",
                qPrintable(opt.host.toString()), opt.port);
        listening.release();

        while (true) {
            if (!server.waitForNewConnection(-1))
                continue;
            QTcpSocket* socket = server.nextPendingConnection();
            if (!socket)
                continue;
            handleClientSession(*socket, opt.seedCfg);
            socket->close();
            delete socket;
        }
    });
    serverThread.detach();

    listening.acquire();
    if (!serverOk)
        return 2;

    fprintf(stderr, "[headless-test] Connecting a built-in test client to %s:%u ...\n",
            qPrintable(opt.host.toString()), opt.port);

    QTcpSocket client;
    client.connectToHost(opt.host, opt.port);
    if (!client.waitForConnected(5000)) {
        fprintf(stderr, "[headless-test] ERROR: could not connect: %s\n", qPrintable(client.errorString()));
        return 2;
    }
    fprintf(stderr, "[headless-test] Connected.\n\n");

    bool allOk = true;

    // Sends one command and waits for its job_end (exit/quit gets no reply, by protocol design,
    // so don't wait for one). Every non-exit send that never gets a reply within 15s is itself a
    // failure — the point of this test is to catch exactly that class of bug.
    auto sendAndWait = [&](const QString& command, const QJsonObject& cmdArgs = QJsonObject()) -> QJsonObject {
        QJsonObject payload{ { QStringLiteral("command"), command } };
        if (!cmdArgs.isEmpty())
            payload[QStringLiteral("args")] = cmdArgs;
        const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
        fprintf(stderr, "[headless-test] >> %s\n", line.trimmed().constData());
        client.write(line);
        client.waitForBytesWritten(3000);

        if (command == QStringLiteral("exit") || command == QStringLiteral("quit"))
            return QJsonObject();

        while (true) {
            if (!client.canReadLine()) {
                if (!client.waitForReadyRead(15000)) {
                    fprintf(stderr, "[headless-test] ERROR: timed out waiting for a reply to '%s'\n",
                            qPrintable(command));
                    allOk = false;
                    return QJsonObject();
                }
                continue;
            }
            const QByteArray raw = client.readLine().trimmed();
            if (raw.isEmpty())
                continue;
            fprintf(stderr, "[headless-test] << %s\n", raw.constData());
            const QJsonObject obj = QJsonDocument::fromJson(raw).object();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("job_end"))
                return obj;
        }
    };

    auto check = [&](const char* label, bool condition) {
        fprintf(stderr, "[headless-test] %s %s\n\n", condition ? "PASS:" : "FAIL:", label);
        if (!condition) allOk = false;
    };

    QJsonObject r;

    r = sendAndWait(QStringLiteral("get-config"));
    check("get-config succeeds", r.value(QStringLiteral("success")).toBool());

    r = sendAndWait(QStringLiteral("set-working-dir"),
                     QJsonObject{ { QStringLiteral("path"), QDir::currentPath() } });
    check("set-working-dir accepts the server process's own current directory",
          r.value(QStringLiteral("success")).toBool());

    r = sendAndWait(QStringLiteral("comfyui-status"));
    check("comfyui-status replies with a job_end (reachable or not — either is fine here)",
          r.value(QStringLiteral("type")).toString() == QStringLiteral("job_end"));

    r = sendAndWait(QStringLiteral("this-command-does-not-exist"));
    check("an unknown command fails cleanly with code 64, instead of hanging or crashing",
          !r.value(QStringLiteral("success")).toBool() && r.value(QStringLiteral("code")).toInt() == 64);

    sendAndWait(QStringLiteral("exit"));
    client.disconnectFromHost();

    fprintf(stderr, "[headless-test] %s\n", allOk ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allOk ? 0 : 1;
}
