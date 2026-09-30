#include "PipelineEngine.h"
#include "LogParser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>

namespace PipelineEngine {

QJsonObject configToJson(const AppConfig& cfg)
{
    QJsonObject o;
    o[QStringLiteral("projectRoot")]     = cfg.projectRoot;
    o[QStringLiteral("outputFolder")]    = cfg.outputFolder;
    o[QStringLiteral("assetRoot")]       = cfg.assetRoot;
    o[QStringLiteral("heroReference")]   = cfg.heroReference;
    o[QStringLiteral("positivePrompt")]  = cfg.positivePrompt;
    o[QStringLiteral("negativePrompt")]  = cfg.negativePrompt;
    o[QStringLiteral("styleProfileId")]  = cfg.styleProfileId;
    o[QStringLiteral("batchId")]         = cfg.batchId;
    o[QStringLiteral("comfyuiServer")]   = cfg.comfyuiServer;
    o[QStringLiteral("category")]        = cfg.category;
    o[QStringLiteral("pythonExe")]       = cfg.pythonExe;
    o[QStringLiteral("ipAdapterWeight")] = cfg.ipAdapterWeight;
    o[QStringLiteral("denoise")]         = cfg.denoise;
    o[QStringLiteral("steps")]           = cfg.steps;
    o[QStringLiteral("fullPipeline")]    = cfg.fullPipeline;
    o[QStringLiteral("includeSpine")]    = cfg.includeSpine;
    return o;
}

AppConfig configFromJson(const QJsonObject& o, AppConfig cfg)
{
    if (o.contains(QStringLiteral("projectRoot")))     cfg.projectRoot     = o[QStringLiteral("projectRoot")].toString();
    if (o.contains(QStringLiteral("outputFolder")))    cfg.outputFolder    = o[QStringLiteral("outputFolder")].toString();
    if (o.contains(QStringLiteral("assetRoot")))       cfg.assetRoot       = o[QStringLiteral("assetRoot")].toString();
    if (o.contains(QStringLiteral("heroReference")))   cfg.heroReference   = o[QStringLiteral("heroReference")].toString();
    if (o.contains(QStringLiteral("positivePrompt")))  cfg.positivePrompt  = o[QStringLiteral("positivePrompt")].toString();
    if (o.contains(QStringLiteral("negativePrompt")))  cfg.negativePrompt  = o[QStringLiteral("negativePrompt")].toString();
    if (o.contains(QStringLiteral("styleProfileId")))  cfg.styleProfileId  = o[QStringLiteral("styleProfileId")].toString();
    if (o.contains(QStringLiteral("batchId")))         cfg.batchId         = o[QStringLiteral("batchId")].toString();
    if (o.contains(QStringLiteral("comfyuiServer")))   cfg.comfyuiServer   = o[QStringLiteral("comfyuiServer")].toString();
    if (o.contains(QStringLiteral("category")))        cfg.category        = o[QStringLiteral("category")].toString();
    if (o.contains(QStringLiteral("pythonExe")))       cfg.pythonExe       = o[QStringLiteral("pythonExe")].toString();
    if (o.contains(QStringLiteral("ipAdapterWeight"))) cfg.ipAdapterWeight = o[QStringLiteral("ipAdapterWeight")].toDouble();
    if (o.contains(QStringLiteral("denoise")))         cfg.denoise         = o[QStringLiteral("denoise")].toDouble();
    if (o.contains(QStringLiteral("steps")))           cfg.steps           = o[QStringLiteral("steps")].toInt();
    if (o.contains(QStringLiteral("fullPipeline")))    cfg.fullPipeline    = o[QStringLiteral("fullPipeline")].toBool();
    if (o.contains(QStringLiteral("includeSpine")))    cfg.includeSpine    = o[QStringLiteral("includeSpine")].toBool();
    return cfg;
}

QJsonObject checkOutputFolderState(const AppConfig& cfg)
{
    QJsonObject result;

    if (cfg.outputFolder.isEmpty() || cfg.assetRoot.isEmpty()) {
        result[QStringLiteral("applicable")] = false;
        result[QStringLiteral("empty")]      = true;
        result[QStringLiteral("entryCount")] = 0;
        return result;
    }

    const QString targetKey    = QFileInfo(cfg.assetRoot).fileName();
    const QString targetFolder = QDir::cleanPath(cfg.outputFolder + QStringLiteral("/") + targetKey);

    result[QStringLiteral("applicable")]   = true;
    result[QStringLiteral("targetFolder")] = targetFolder;

    QDir dir(targetFolder);
    if (!dir.exists()) {
        result[QStringLiteral("empty")]      = true;
        result[QStringLiteral("entryCount")] = 0;
        return result;
    }

    const QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    result[QStringLiteral("empty")]      = entries.isEmpty();
    result[QStringLiteral("entryCount")] = int(entries.count());
    return result;
}

QJsonObject checkStylePlanState(const AppConfig& cfg)
{
    QJsonObject result;

    if (cfg.assetRoot.isEmpty() || cfg.batchId.isEmpty()) {
        result[QStringLiteral("applicable")] = false;
        result[QStringLiteral("fresh")]      = true;
        return result;
    }

    const QString targetKey = QFileInfo(cfg.assetRoot).fileName();
    const QString planPath  = QDir::cleanPath(
        cfg.outputFolder + QStringLiteral("/") + targetKey + QStringLiteral("/step4_style_plan.json"));

    result[QStringLiteral("applicable")] = true;
    result[QStringLiteral("planPath")]   = planPath;

    QFile f(planPath);
    if (!f.exists()) {
        result[QStringLiteral("planExists")] = false;
        result[QStringLiteral("fresh")]      = true;
        return result;
    }
    result[QStringLiteral("planExists")] = true;

    QString planRoot;
    if (f.open(QIODevice::ReadOnly)) {
        planRoot = QJsonDocument::fromJson(f.readAll())
                       .object().value(QStringLiteral("scanned_asset_root")).toString();
        f.close();
    }
    result[QStringLiteral("planRoot")] = planRoot;

    bool stale = false;
    QString reason;
    if (planRoot.isEmpty()) {
        stale  = true;
        reason = QStringLiteral(
            "The style_plan.json at:\n  %1\n\n"
            "was generated with an older version of the pipeline and does not\n"
            "record which asset folder it was built from.").arg(planPath);
    } else {
        const QString canonCurrent = QDir(cfg.assetRoot).canonicalPath();
        const QString canonPlan    = QDir(planRoot).canonicalPath();
        if (canonCurrent != canonPlan) {
            stale  = true;
            reason = QStringLiteral(
                "The style_plan.json was generated for:\n  %1\n\n"
                "but the current Asset Root is:\n  %2")
                    .arg(planRoot, cfg.assetRoot);
        }
    }

    result[QStringLiteral("fresh")]  = !stale;
    result[QStringLiteral("reason")] = reason;
    return result;
}

QJsonObject buildStepRequest(StepIndex idx, const AppConfig& cfg, const QString& projectRootIn,
                              const QJsonObject& extra)
{
    const QString projectRoot = QDir::cleanPath(projectRootIn);
    LaunchSpec spec = buildStep(idx, cfg, projectRoot);

    QJsonArray args;
    args.append(QStringLiteral("-u"));
    args.append(spec.script);
    for (const QString& a : spec.args)
        args.append(a);

    if (idx == StepIndex::ComfyUiBatch) {
        if (extra.contains(QStringLiteral("ipWeight"))) {
            args.append(QStringLiteral("--ip-weight"));
            args.append(extra[QStringLiteral("ipWeight")].toString());
        }
        if (extra.contains(QStringLiteral("denoise"))) {
            args.append(QStringLiteral("--denoise"));
            args.append(extra[QStringLiteral("denoise")].toString());
        }
        if (extra.contains(QStringLiteral("steps"))) {
            args.append(QStringLiteral("--steps"));
            args.append(extra[QStringLiteral("steps")].toString());
        }
    }

    QJsonObject env;
    env[QStringLiteral("COMFYUI_SERVER")]   = cfg.comfyuiServer;
    env[QStringLiteral("PYTHONIOENCODING")] = QStringLiteral("utf-8");

    QJsonObject req;
    req[QStringLiteral("step")]             = stepName(idx);
    req[QStringLiteral("program")]          = cfg.pythonExe;
    req[QStringLiteral("args")]             = args;
    req[QStringLiteral("workingDirectory")] = projectRoot;
    req[QStringLiteral("env")]              = env;
    return req;
}

void splitHostPort(const QString& hostPort, QString& host, QString& port)
{
    const int colon = hostPort.lastIndexOf(':');
    host = (colon > 0) ? hostPort.left(colon)    : hostPort;
    port = (colon > 0) ? hostPort.mid(colon + 1) : QStringLiteral("8188");
}

bool comfyuiReachable(const AppConfig& cfg)
{
    QString addr, port;
    splitHostPort(cfg.comfyuiServer, addr, port);

    QProcess probe;
    probe.start(cfg.pythonExe, {
        QStringLiteral("-c"),
        QStringLiteral("import socket,sys;"
                       "s=socket.socket();"
                       "s.settimeout(1.5);"
                       "r=s.connect_ex(('%1',%2));"
                       "s.close();"
                       "sys.exit(r)").arg(addr, port)
    });
    probe.waitForFinished(2500);
    return probe.exitCode() == 0;
}

QJsonObject buildComfyUiLaunchRequest(const AppConfig& cfg)
{
    QJsonObject req;
    const QString projectRoot = QDir::cleanPath(cfg.projectRoot);

#ifdef Q_OS_WIN
    const QString batPath = QDir::cleanPath(
        projectRoot + QStringLiteral("/_pipeline/tools/start_comfyui_with_flags.bat"));
    if (!QFile::exists(batPath)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("Batch file not found: %1").arg(batPath);
        return req;
    }
    req[QStringLiteral("program")] = QStringLiteral("cmd.exe");
    req[QStringLiteral("args")] = QJsonArray{
        QStringLiteral("/c"), QDir::toNativeSeparators(batPath),
        QStringLiteral("--enable-manager"), QStringLiteral("--listen"), QStringLiteral("0.0.0.0")
    };
    req[QStringLiteral("workingDirectory")] = projectRoot;
#else
    const QString comfyDir   = QDir::cleanPath(projectRoot + QStringLiteral("/_comfyui/ComfyUI"));
    const QString venvPython = QDir::cleanPath(comfyDir + QStringLiteral("/.venv/bin/python"));
    const QString mainPy     = QDir::cleanPath(comfyDir + QStringLiteral("/main.py"));
    if (!QFile::exists(mainPy) || !QFile::exists(venvPython)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("ComfyUI (or its venv) not found under %1").arg(comfyDir);
        return req;
    }
    QString addr, port;
    splitHostPort(cfg.comfyuiServer, addr, port);
    req[QStringLiteral("program")] = venvPython;
    req[QStringLiteral("args")] = QJsonArray{
        mainPy, QStringLiteral("--listen"), QStringLiteral("0.0.0.0"),
        QStringLiteral("--port"), port, QStringLiteral("--enable-manager")
    };
    req[QStringLiteral("workingDirectory")] = comfyDir;
#endif
    req[QStringLiteral("ok")] = true;
    return req;
}

QJsonObject buildText2ImageRequest(const AppConfig& cfg, const QJsonObject& args)
{
    QJsonObject req;
    const QString projectRoot = QDir::cleanPath(cfg.projectRoot);

    const QString prompt = args.value(QStringLiteral("prompt")).toString().trimmed();
    if (prompt.isEmpty()) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("text2image requires args.prompt.");
        return req;
    }

    const QString scriptPath = QDir::cleanPath(
        projectRoot + QStringLiteral("/TestData/Text2Image/run_text2image.py"));
    if (!QFile::exists(scriptPath)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("Script not found: %1").arg(scriptPath);
        return req;
    }

    const QString outputDir = args.contains(QStringLiteral("output-dir"))
        ? args.value(QStringLiteral("output-dir")).toString()
        : QDir::cleanPath(projectRoot + QStringLiteral("/output/Text2Image"));
    const QString outputName = args.contains(QStringLiteral("output-name"))
        ? args.value(QStringLiteral("output-name")).toString()
        : QStringLiteral("t2i_%1").arg(QDateTime::currentMSecsSinceEpoch());

    QJsonArray a;
    a.append(QStringLiteral("-u"));
    a.append(scriptPath);
    a.append(QStringLiteral("--prompt"));      a.append(prompt);
    a.append(QStringLiteral("--output-name")); a.append(outputName);
    a.append(QStringLiteral("--output-dir"));  a.append(QDir::toNativeSeparators(outputDir));

    const int width  = args.value(QStringLiteral("width")).toInt(0);
    const int height = args.value(QStringLiteral("height")).toInt(0);
    if (width > 0 && height > 0) {
        a.append(QStringLiteral("--width"));  a.append(width);
        a.append(QStringLiteral("--height")); a.append(height);
    }

    QJsonObject env;
    env[QStringLiteral("COMFYUI_SERVER")]   = cfg.comfyuiServer;
    env[QStringLiteral("PYTHONIOENCODING")] = QStringLiteral("utf-8");

    req[QStringLiteral("ok")]               = true;
    req[QStringLiteral("program")]          = cfg.pythonExe;
    req[QStringLiteral("args")]             = a;
    req[QStringLiteral("workingDirectory")] = projectRoot;
    req[QStringLiteral("env")]              = env;
    req[QStringLiteral("outputPath")]       = QDir::cleanPath(
        outputDir + QStringLiteral("/") + outputName + QStringLiteral(".png"));
    return req;
}

QJsonObject buildImageWithPromptRequest(const AppConfig& cfg, const QJsonObject& args)
{
    QJsonObject req;
    const QString projectRoot = QDir::cleanPath(cfg.projectRoot);

    const QString reference = args.value(QStringLiteral("reference")).toString().trimmed();
    if (reference.isEmpty()) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("imagewithprompt requires args.reference.");
        return req;
    }
    if (!QFile::exists(reference)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("Reference image not found: %1").arg(reference);
        return req;
    }

    const QString scriptPath = QDir::cleanPath(
        projectRoot + QStringLiteral("/TestData/ImageWithPrompt/run_imagewithprompt.py"));
    if (!QFile::exists(scriptPath)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("Script not found: %1").arg(scriptPath);
        return req;
    }

    const QString outputDir = args.contains(QStringLiteral("output-dir"))
        ? args.value(QStringLiteral("output-dir")).toString()
        : QDir::cleanPath(projectRoot + QStringLiteral("/output/ImageWithPrompt"));
    const QString outputName = args.contains(QStringLiteral("output-name"))
        ? args.value(QStringLiteral("output-name")).toString()
        : QFileInfo(reference).baseName();

    const int seed = args.contains(QStringLiteral("seed"))
        ? args.value(QStringLiteral("seed")).toInt()
        : int(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);
    const int steps = args.contains(QStringLiteral("steps"))
        ? args.value(QStringLiteral("steps")).toInt() : cfg.steps;
    const double cfgScale = args.contains(QStringLiteral("cfg"))
        ? args.value(QStringLiteral("cfg")).toDouble() : 7.0;
    const double denoise = args.contains(QStringLiteral("denoise"))
        ? args.value(QStringLiteral("denoise")).toDouble() : cfg.denoise;
    const double ipWeight = args.contains(QStringLiteral("ip-weight"))
        ? args.value(QStringLiteral("ip-weight")).toDouble() : cfg.ipAdapterWeight;

    QJsonArray a;
    a.append(QStringLiteral("-u"));
    a.append(scriptPath);
    a.append(QStringLiteral("--reference"));   a.append(reference);
    a.append(QStringLiteral("--output-dir"));  a.append(QDir::toNativeSeparators(outputDir));
    a.append(QStringLiteral("--output-name")); a.append(outputName);
    a.append(QStringLiteral("--seed"));        a.append(seed);
    a.append(QStringLiteral("--steps"));       a.append(steps);
    a.append(QStringLiteral("--cfg"));         a.append(QString::number(cfgScale, 'f', 1));
    a.append(QStringLiteral("--denoise"));     a.append(QString::number(denoise, 'f', 2));
    a.append(QStringLiteral("--ip-weight"));   a.append(QString::number(ipWeight, 'f', 2));

    const QString positivePrompt = args.value(QStringLiteral("positive-prompt")).toString();
    const QString category       = args.value(QStringLiteral("category")).toString();
    if (!positivePrompt.isEmpty()) {
        a.append(QStringLiteral("--positive-prompt")); a.append(positivePrompt);
    } else if (!category.isEmpty()) {
        a.append(QStringLiteral("--category")); a.append(category);
    }
    const QString negativePrompt = args.value(QStringLiteral("negative-prompt")).toString();
    if (!negativePrompt.isEmpty()) {
        a.append(QStringLiteral("--negative-prompt")); a.append(negativePrompt);
    }

    QJsonObject env;
    env[QStringLiteral("COMFYUI_SERVER")]   = cfg.comfyuiServer;
    env[QStringLiteral("PYTHONIOENCODING")] = QStringLiteral("utf-8");

    req[QStringLiteral("ok")]               = true;
    req[QStringLiteral("program")]          = cfg.pythonExe;
    req[QStringLiteral("args")]             = a;
    req[QStringLiteral("workingDirectory")] = projectRoot;
    req[QStringLiteral("env")]              = env;
    req[QStringLiteral("outputPath")]       = QDir::cleanPath(
        outputDir + QStringLiteral("/") + outputName + QStringLiteral(".png"));
    return req;
}

QJsonObject buildPoseFromPromptRequest(const AppConfig& cfg, const QJsonObject& args)
{
    QJsonObject req;
    const QString projectRoot = QDir::cleanPath(cfg.projectRoot);

    const QString name        = args.value(QStringLiteral("name")).toString().trimmed();
    const QString description = args.value(QStringLiteral("description")).toString().trimmed();
    if (name.isEmpty() || description.isEmpty()) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral(
            "pose-from-prompt requires args.name and args.description.");
        return req;
    }

    const QString scriptPath = QDir::cleanPath(
        projectRoot + QStringLiteral("/TestData/PoseGenerator/generate_pose_from_prompt.py"));
    if (!QFile::exists(scriptPath)) {
        req[QStringLiteral("ok")]    = false;
        req[QStringLiteral("error")] = QStringLiteral("Script not found: %1").arg(scriptPath);
        return req;
    }

    const QString subjectType = args.contains(QStringLiteral("subject-type"))
        ? args.value(QStringLiteral("subject-type")).toString() : QStringLiteral("human");
    const bool loop = args.value(QStringLiteral("loop")).toBool(false);
    const int frames = args.contains(QStringLiteral("frames"))
        ? args.value(QStringLiteral("frames")).toInt() : 8;
    const QString backend = args.contains(QStringLiteral("backend"))
        ? args.value(QStringLiteral("backend")).toString() : QStringLiteral("ollama");
    const QString apiUrl = args.value(QStringLiteral("api-url")).toString();
    const QString outputJson = args.contains(QStringLiteral("output"))
        ? args.value(QStringLiteral("output")).toString()
        : QDir::cleanPath(projectRoot + QStringLiteral("/TestData/PoseGenerator/poses_library/")
                           + name + QStringLiteral(".json"));

    QJsonArray a;
    a.append(QStringLiteral("-u"));
    a.append(scriptPath);
    a.append(QStringLiteral("--name"));         a.append(name);
    a.append(QStringLiteral("--description"));  a.append(description);
    a.append(QStringLiteral("--subject-type")); a.append(subjectType);
    a.append(QStringLiteral("--loop"));         a.append(loop ? QStringLiteral("true") : QStringLiteral("false"));
    a.append(QStringLiteral("--frames"));       a.append(frames);
    a.append(QStringLiteral("--backend"));      a.append(backend);
    a.append(QStringLiteral("--api-url"));      a.append(apiUrl);
    a.append(QStringLiteral("--output"));       a.append(QDir::toNativeSeparators(outputJson));

    QJsonObject env;
    env[QStringLiteral("PYTHONIOENCODING")] = QStringLiteral("utf-8");

    req[QStringLiteral("ok")]               = true;
    req[QStringLiteral("program")]          = cfg.pythonExe;
    req[QStringLiteral("args")]             = a;
    req[QStringLiteral("workingDirectory")] = projectRoot;
    req[QStringLiteral("env")]              = env;
    req[QStringLiteral("outputPath")]       = outputJson;
    return req;
}

namespace {

// Turns one line of child output into the JSON event handed to runStepRequestSync()'s
// `onEvent` callback, reusing the exact marker grammar App/src/LogParser.cpp already parses
// for the GUI's log view ("[GS:STEP_START ...]", "[GS:PROGRESS ...]", etc.).
QJsonObject lineToEvent(const QString& line, const char* streamName)
{
    const LogEvent ev = parseLine(line);
    if (ev.type == MarkerType::NONE) {
        return QJsonObject{
            {QStringLiteral("type"),   QStringLiteral("log")},
            {QStringLiteral("stream"), QString::fromLatin1(streamName)},
            {QStringLiteral("line"),   line},
        };
    }

    QJsonObject o{
        {QStringLiteral("type"), QStringLiteral("marker")},
    };
    switch (ev.type) {
    case MarkerType::STEP_START:
        o[QStringLiteral("marker")]    = QStringLiteral("STEP_START");
        o[QStringLiteral("scriptStep")] = ev.stepName;
        break;
    case MarkerType::STEP_DONE:
        o[QStringLiteral("marker")]     = QStringLiteral("STEP_DONE");
        o[QStringLiteral("scriptStep")] = ev.stepName;
        o[QStringLiteral("message")]    = ev.message;
        break;
    case MarkerType::PROGRESS:
        o[QStringLiteral("marker")]     = QStringLiteral("PROGRESS");
        o[QStringLiteral("scriptStep")] = ev.stepName;
        o[QStringLiteral("current")]    = ev.current;
        o[QStringLiteral("total")]      = ev.total;
        break;
    case MarkerType::STEP_DETAIL:
        o[QStringLiteral("marker")]     = QStringLiteral("STEP_DETAIL");
        o[QStringLiteral("scriptStep")] = ev.stepName;
        o[QStringLiteral("detail")]     = ev.detailJson;
        break;
    case MarkerType::GS_ERROR:
        o[QStringLiteral("marker")]     = QStringLiteral("ERROR");
        o[QStringLiteral("scriptStep")] = ev.stepName;
        o[QStringLiteral("message")]    = ev.message;
        break;
    default:
        break;
    }
    return o;
}

} // namespace

QJsonObject runStepRequestSync(const QJsonObject& request,
                                 const std::function<void(const QJsonObject&)>& onEvent)
{
    QJsonObject result;

    const QString program = request.value(QStringLiteral("program")).toString();
    QStringList args;
    for (const QJsonValue& v : request.value(QStringLiteral("args")).toArray())
        args << v.toString();
    const QString workDir = request.value(QStringLiteral("workingDirectory")).toString();
    const QJsonObject envOverrides = request.value(QStringLiteral("env")).toObject();

    QProcess proc;
    proc.setProcessChannelMode(onEvent ? QProcess::SeparateChannels : QProcess::ForwardedChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = envOverrides.begin(); it != envOverrides.end(); ++it)
        env.insert(it.key(), it.value().toString());
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(workDir);

    proc.start(program, args);
    if (!proc.waitForStarted(5000)) {
        result[QStringLiteral("started")]  = false;
        result[QStringLiteral("success")]  = false;
        result[QStringLiteral("exitCode")] = -1;
        result[QStringLiteral("error")]    = proc.errorString();
        return result;
    }

    if (onEvent) {
        auto drain = [&](QProcess::ProcessChannel channel, const char* streamName) {
            proc.setReadChannel(channel);
            while (proc.canReadLine()) {
                const QString line = QString::fromUtf8(proc.readLine()).trimmed();
                if (!line.isEmpty())
                    onEvent(lineToEvent(line, streamName));
            }
        };
        // No event loop is running (headless mode is fully synchronous), so poll instead of
        // connecting to readyRead* signals.
        while (proc.state() != QProcess::NotRunning) {
            proc.waitForReadyRead(200);
            drain(QProcess::StandardOutput, "stdout");
            drain(QProcess::StandardError, "stderr");
        }
        drain(QProcess::StandardOutput, "stdout");
        drain(QProcess::StandardError, "stderr");
    } else {
        proc.waitForFinished(-1);
    }

    result[QStringLiteral("started")]  = true;
    result[QStringLiteral("exitCode")] = proc.exitCode();
    result[QStringLiteral("success")]  = (proc.exitCode() == 0);
    return result;
}

} // namespace PipelineEngine
