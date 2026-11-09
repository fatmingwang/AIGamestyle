#include "HeadlessServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace {

QJsonValue kindName(ParamDef::Kind k)
{
    switch (k) {
    case ParamDef::Kind::Int: return QStringLiteral("int");
    case ParamDef::Kind::Float: return QStringLiteral("float");
    case ParamDef::Kind::Line: return QStringLiteral("string");
    case ParamDef::Kind::Text: return QStringLiteral("text");
    case ParamDef::Kind::Bool: return QStringLiteral("bool");
    case ParamDef::Kind::Combo: return QStringLiteral("choice");
    }
    return QStringLiteral("string");
}

}  // namespace

HeadlessServer::HeadlessServer(const Options& options, QObject* parent)
    : QObject(parent)
    , options_(options)
    , workflow_(Workflow::textToImage())
{
    if (!options_.graphPath.isEmpty()) {
        QString why;
        Workflow w;
        if (w.load(options_.graphPath, &why))
            workflow_ = w;
        else
            emit log(QStringLiteral("cannot load %1: %2 (using the built-in graph)").arg(options_.graphPath, why));
    }
    client_.setServer(options_.comfyServer);
    connect(&tcp_, &QTcpServer::newConnection, this, &HeadlessServer::onNewConnection);
    connect(&client_, &ComfyClient::logLine, this, &HeadlessServer::log);
    connect(&process_, &ComfyProcess::logLine, this, &HeadlessServer::log);

    // ComfyUI's answers, for the command that is running
    connect(&process_, &ComfyProcess::up, this, [this] {
        if (busy_ && job_.command == QStringLiteral("comfyui-launch")) {
            result({{QStringLiteral("started"), true}});
            end(0, QStringLiteral("ComfyUI is up"));
        }
    });
    connect(&process_, &ComfyProcess::failed, this, [this](const QString& why) {
        if (busy_ && job_.command == QStringLiteral("comfyui-launch"))
            end(1, why);
    });
    connect(&client_, &ComfyClient::connected, this, [this] {
        for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
            emit log(line);
        if (afterConnect_) {
            auto then = std::move(afterConnect_);
            afterConnect_ = nullptr;
            then();
        }
    });
    connect(&client_, &ComfyClient::connectionFailed, this, [this](const QString& why) {
        if (afterConnect_) {
            afterConnect_ = nullptr;
            end(4, QStringLiteral("cannot connect to ComfyUI at %1: %2").arg(client_.server(), why));
        }
    });
    connect(&client_, &ComfyClient::queued, this, [this](const QString& id, int number) {
        if (job_.command != QStringLiteral("generate"))
            return;
        promptId_ = id;
        send({{QStringLiteral("type"), QStringLiteral("queued")}, {QStringLiteral("promptId"), id}, {QStringLiteral("number"), number}});
    });
    connect(&client_, &ComfyClient::promptRejected, this, [this](const QString& why) {
        if (busy_ && job_.command == QStringLiteral("generate"))
            end(5, QStringLiteral("ComfyUI refused the graph: ") + why);
    });
    connect(&client_, &ComfyClient::progress, this, [this](const QString& id, int value, int max) {
        if (id == promptId_ && busy_)
            send({{QStringLiteral("type"), QStringLiteral("progress")}, {QStringLiteral("value"), value}, {QStringLiteral("max"), max}});
    });
    connect(&client_, &ComfyClient::preview, this, [this](const QImage& image) {
        if (promptId_.isEmpty() || !busy_ || job_.command != QStringLiteral("generate"))
            return;
        ++previews_;
        QJsonObject e{{QStringLiteral("type"), QStringLiteral("preview")}, {QStringLiteral("index"), previews_},
                      {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}};
        if (!previewDir_.isEmpty()) {
            const QString path = QDir(previewDir_).filePath(QStringLiteral("preview_%1.jpg").arg(previews_, 3, 10, QLatin1Char('0')));
            if (image.save(path))
                e[QStringLiteral("path")] = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        }
        send(e);
    });
    connect(&client_, &ComfyClient::imageReady, this, [this](const QString& id, const QImage& image, const QString& file) {
        if (id != promptId_ || !busy_)
            return;
        QJsonObject e{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("file"), file},
                      {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}};
        if (!outPath_.isEmpty()) {
            if (image.save(outPath_))
                e[QStringLiteral("path")] = QDir::toNativeSeparators(QFileInfo(outPath_).absoluteFilePath());
            else
                e[QStringLiteral("error")] = QStringLiteral("could not write ") + outPath_;
        }
        gotImage_ = true;
        send(e);
    });
    connect(&client_, &ComfyClient::audioReady, this, [this](const QString& id, const QByteArray& bytes, const QString& file) {
        if (id != promptId_ || !busy_)
            return;
        QJsonObject e{{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("file"), file},
                      {QStringLiteral("bytes"), bytes.size()}, {QStringLiteral("format"), QFileInfo(file).suffix()}};
        if (!outPath_.isEmpty()) {
            // the bytes as ComfyUI saved them: "out" gets the real format's extension (song.wav from an mp3 graph -> song.mp3)
            QString path = outPath_;
            const QString suffix = QFileInfo(file).suffix();
            if (!suffix.isEmpty() && QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0)
                path = QFileInfo(path).path() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QLatin1Char('.') + suffix;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size())
                e[QStringLiteral("path")] = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
            else
                e[QStringLiteral("error")] = QStringLiteral("could not write ") + path;
        }
        gotImage_ = true;
        send(e);
    });
    connect(&client_, &ComfyClient::finished, this, [this](const QString& id) {
        if (id != promptId_ || !busy_)
            return;
        const double seconds = clock_.elapsed() / 1000.0;
        // the image's download may still be on its way: end once it is in, or after a moment
        auto finishJob = [this, seconds] {
            if (!busy_ || job_.command != QStringLiteral("generate"))
                return;
            result({{QStringLiteral("seconds"), seconds}, {QStringLiteral("previews"), previews_}, {QStringLiteral("output"), gotImage_}});
            end(gotImage_ ? 0 : 6, gotImage_ ? QStringLiteral("done")
                                             : QStringLiteral("the graph made no image or audio (does it end in SaveImage, PreviewImage or SaveAudio?)"));
        };
        QTimer::singleShot(gotImage_ ? 0 : 1500, this, finishJob);
    });
    connect(&client_, &ComfyClient::failed, this, [this](const QString& id, const QString& why) {
        if (id == promptId_ && busy_)
            end(7, QStringLiteral("ComfyUI failed: ") + why);
    });
    connect(&client_, &ComfyClient::interrupted, this, [this](const QString& id) {
        if (id == promptId_ && busy_ && job_.command == QStringLiteral("generate"))
            end(8, QStringLiteral("stopped"));
    });
}

HeadlessServer::~HeadlessServer()
{
    process_.stop();   // a ComfyUI this server started goes with it
}

bool HeadlessServer::listen(QString* why)
{
    if (!tcp_.listen(QHostAddress(options_.host), options_.port)) {
        if (why)
            *why = tcp_.errorString();
        return false;
    }
    emit log(QStringLiteral("listening on %1:%2 (one JSON command per line)").arg(options_.host).arg(tcp_.serverPort()));
    if (options_.startComfy) {   // start the local ComfyUI now, so the first generate does not wait for it
        client_.ping([this](bool up) {
            if (up)
                client_.connectToServer(options_.comfyServer);
            else
                process_.start(options_.comfyRoot, quint16(QUrl(QStringLiteral("http://") + options_.comfyServer).port(8188)), true);
        });
        connect(&process_, &ComfyProcess::up, this, [this] {
            if (!client_.isReady())
                client_.connectToServer(options_.comfyServer);
        });
    }
    return true;
}

void HeadlessServer::onNewConnection()
{
    while (QTcpSocket* socket = tcp_.nextPendingConnection()) {
        emit log(QStringLiteral("client %1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort()));
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HeadlessServer::onReadyRead(QTcpSocket* socket)
{
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine().trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError err;
        const QJsonObject o = QJsonDocument::fromJson(line, &err).object();
        Job job;
        job.client = socket;
        job.command = o.value(QStringLiteral("command")).toString();
        job.args = o.value(QStringLiteral("args")).toObject();
        if (err.error != QJsonParseError::NoError || job.command.isEmpty()) {
            const QJsonObject bad{{QStringLiteral("type"), QStringLiteral("job_end")}, {QStringLiteral("command"), QString()},
                                  {QStringLiteral("code"), 2}, {QStringLiteral("success"), false},
                                  {QStringLiteral("message"), QStringLiteral("not a command: send {\"command\": \"<name>\", \"args\": {...}} on one line")}};
            socket->write(QJsonDocument(bad).toJson(QJsonDocument::Compact) + "\n");
            continue;
        }
        if (job.command == QStringLiteral("exit") || job.command == QStringLiteral("quit")) {
            socket->disconnectFromHost();
            return;
        }
        if (job.command == QStringLiteral("stop") && busy_) {   // answered at once, beside the running generate
            client_.interrupt();
            client_.deleteQueued(QStringList{promptId_});
            const QJsonObject ok{{QStringLiteral("type"), QStringLiteral("job_end")}, {QStringLiteral("command"), job.command},
                                 {QStringLiteral("code"), 0}, {QStringLiteral("success"), true}, {QStringLiteral("message"), QStringLiteral("stopping")}};
            socket->write(QJsonDocument(ok).toJson(QJsonDocument::Compact) + "\n");
            continue;
        }
        queue_ << job;
    }
    next();
}

void HeadlessServer::next()
{
    while (!busy_ && !queue_.isEmpty()) {
        const Job job = queue_.takeFirst();
        if (!job.client)
            continue;   // its client has gone
        run(job);
    }
}

void HeadlessServer::send(const QJsonObject& event)
{
    if (job_.client)
        job_.client->write(QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n");
}

void HeadlessServer::result(const QJsonObject& fields)
{
    QJsonObject e = fields;
    e[QStringLiteral("type")] = QStringLiteral("result");
    e[QStringLiteral("command")] = job_.command;
    send(e);
}

void HeadlessServer::end(int code, const QString& message)
{
    if (!busy_)
        return;
    send({{QStringLiteral("type"), QStringLiteral("job_end")}, {QStringLiteral("command"), job_.command}, {QStringLiteral("code"), code},
          {QStringLiteral("success"), code == 0}, {QStringLiteral("message"), message}});
    emit log(QStringLiteral("%1: %2 (%3)").arg(job_.command).arg(code).arg(message));
    busy_ = false;
    job_ = Job{};
    promptId_.clear();
    QTimer::singleShot(0, this, &HeadlessServer::next);
}

void HeadlessServer::whenConnected(const std::function<void()>& then)
{
    if (client_.isReady()) {
        then();
        return;
    }
    client_.ping([this, then](bool up) {
        if (!up) {
            end(4, QStringLiteral("no ComfyUI at %1: send comfyui-launch first, or start it yourself").arg(client_.server()));
            return;
        }
        afterConnect_ = then;
        client_.connectToServer(client_.server());
    });
}

QJsonArray HeadlessServer::paramsJson() const
{
    QJsonArray out;
    for (const ParamDef& p : workflow_.params(client_.objectInfo())) {
        QJsonObject o{{QStringLiteral("key"), p.nodeId + QLatin1Char('/') + p.input}, {QStringLiteral("node"), p.nodeId},
                      {QStringLiteral("input"), p.input}, {QStringLiteral("group"), p.group}, {QStringLiteral("kind"), kindName(p.kind)},
                      {QStringLiteral("value"), p.value}};
        if (p.kind == ParamDef::Kind::Combo)
            o[QStringLiteral("choices")] = QJsonArray::fromStringList(p.choices);
        if (p.kind == ParamDef::Kind::Int || p.kind == ParamDef::Kind::Float) {
            o[QStringLiteral("min")] = p.minimum;
            o[QStringLiteral("max")] = p.maximum;
        }
        out << o;
    }
    return out;
}

// values: {"<node id>/<input>": v} for one input, or {"<input>": v} for every input of that name ("steps": 12)
bool HeadlessServer::setParams(const QJsonObject& values, QStringList* changed, QString* why)
{
    const QList<ParamDef> params = workflow_.params(client_.objectInfo());
    for (auto it = values.begin(); it != values.end(); ++it) {
        bool found = false;
        for (const ParamDef& p : params) {
            const QString key = p.nodeId + QLatin1Char('/') + p.input;
            if (it.key() != key && it.key() != p.input)
                continue;
            QJsonValue v = it.value();
            if (p.kind == ParamDef::Kind::Int && v.isDouble())
                v = qint64(v.toDouble());
            if (p.kind == ParamDef::Kind::Combo && !p.choices.isEmpty() && !p.choices.contains(v.toString())) {
                if (why)
                    *why = QStringLiteral("%1: \"%2\" is not one of the server's choices").arg(key, v.toString());
                return false;
            }
            workflow_.setValue(p.nodeId, p.input, v);
            found = true;
            if (changed)
                *changed << key;
        }
        if (!found) {
            if (why)
                *why = QStringLiteral("no parameter \"%1\" in the graph (get-params lists them)").arg(it.key());
            return false;
        }
    }
    return true;
}

void HeadlessServer::run(const Job& job)
{
    job_ = job;
    busy_ = true;
    send({{QStringLiteral("type"), QStringLiteral("job_start")}, {QStringLiteral("command"), job.command}});
    const QJsonObject& a = job.args;
    const QString cmd = job.command;

    if (cmd == QStringLiteral("comfyui-status")) {
        if (a.contains(QStringLiteral("server")))
            client_.setServer(a.value(QStringLiteral("server")).toString());
        client_.ping([this](bool up) {
            result({{QStringLiteral("reachable"), up}, {QStringLiteral("server"), client_.server()}, {QStringLiteral("connected"), client_.isReady()}});
            end(up ? 0 : 1, up ? QStringLiteral("reachable") : QStringLiteral("not reachable"));
        });
    } else if (cmd == QStringLiteral("comfyui-launch")) {
        client_.ping([this, a](bool up) {
            if (up) {
                result({{QStringLiteral("started"), false}});
                end(0, QStringLiteral("already running"));
                return;
            }
            const QUrl u(QStringLiteral("http://") + client_.server());
            process_.start(a.value(QStringLiteral("comfy-root")).toString(options_.comfyRoot), quint16(u.port(8188)),
                           a.value(QStringLiteral("custom-nodes")).toBool(true));
        });
    } else if (cmd == QStringLiteral("connect")) {
        if (a.contains(QStringLiteral("server")))
            client_.setServer(a.value(QStringLiteral("server")).toString());
        client_.disconnectFromServer();
        whenConnected([this] {
            result({{QStringLiteral("server"), client_.server()}, {QStringLiteral("nodeTypes"), client_.objectInfo().size()}});
            end(0, QStringLiteral("connected"));
        });
    } else if (cmd == QStringLiteral("get-params")) {
        whenConnected([this] {
            result({{QStringLiteral("graph"), workflow_.name()}, {QStringLiteral("params"), paramsJson()}});
            end(0, QString());
        });
    } else if (cmd == QStringLiteral("set-params")) {
        whenConnected([this, a] {
            QStringList changed;
            QString why;
            if (!setParams(a.value(QStringLiteral("params")).toObject(), &changed, &why)) {
                end(2, why);
                return;
            }
            result({{QStringLiteral("changed"), QJsonArray::fromStringList(changed)}});
            end(0, QString());
        });
    } else if (cmd == QStringLiteral("load-graph")) {
        Workflow w;
        QString why;
        bool ok = false;
        if (a.contains(QStringLiteral("graph"))) {
            ok = w.fromJson(a.value(QStringLiteral("graph")).toObject(), &why);
            w.setName(a.value(QStringLiteral("name")).toString(QStringLiteral("sent graph")));
        } else {
            ok = w.load(a.value(QStringLiteral("path")).toString(), &why);
        }
        if (!ok) {
            end(2, why);
        } else {
            workflow_ = w;
            if (client_.isReady())
                for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
                    send({{QStringLiteral("type"), QStringLiteral("log")}, {QStringLiteral("text"), line}});
            result({{QStringLiteral("graph"), workflow_.name()}, {QStringLiteral("nodes"), workflow_.graph().size()}});
            end(0, QString());
        }
    } else if (cmd == QStringLiteral("save-graph")) {
        QString why;
        if (workflow_.save(a.value(QStringLiteral("path")).toString(), &why))
            end(0, QString());
        else
            end(2, why);
    } else if (cmd == QStringLiteral("reset-graph")) {
        const QString which = a.value(QStringLiteral("graph")).toString(QStringLiteral("image"));
        if (which != QStringLiteral("image") && which != QStringLiteral("music")) {
            end(2, QStringLiteral("graph: \"image\" (SDXL text to image) or \"music\" (ACE-Step 1.5 text to music)"));
            return;
        }
        workflow_ = which == QStringLiteral("music") ? Workflow::textToMusic() : Workflow::textToImage();
        if (client_.isReady())
            for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
                send({{QStringLiteral("type"), QStringLiteral("log")}, {QStringLiteral("text"), line}});
        result({{QStringLiteral("graph"), workflow_.name()}});
        end(0, workflow_.name());
    } else if (cmd == QStringLiteral("generate")) {
        whenConnected([this, a] {
            QString why;
            if (a.contains(QStringLiteral("params")) && !setParams(a.value(QStringLiteral("params")).toObject(), nullptr, &why)) {
                end(2, why);
                return;
            }
            if (a.value(QStringLiteral("random-seed")).toBool())
                for (const ParamDef& p : workflow_.params(client_.objectInfo()))
                    if (p.isSeed())
                        workflow_.setValue(p.nodeId, p.input, qint64(QRandomGenerator::global()->bounded(Q_INT64_C(1) << 40)));
            outPath_ = a.value(QStringLiteral("out")).toString();
            previewDir_ = a.value(QStringLiteral("preview-dir")).toString();
            if (!previewDir_.isEmpty())
                QDir().mkpath(previewDir_);
            if (!outPath_.isEmpty())
                QDir().mkpath(QFileInfo(outPath_).absolutePath());
            previews_ = 0;
            gotImage_ = false;
            promptId_.clear();
            clock_.start();
            client_.queuePrompt(workflow_.graph());
        });
    } else if (cmd == QStringLiteral("stop")) {
        end(0, QStringLiteral("nothing is running"));
    } else {
        end(2, QStringLiteral("unknown command \"%1\": comfyui-status, comfyui-launch, connect, get-params, set-params, load-graph, "
                              "save-graph, reset-graph, generate, stop, exit").arg(cmd));
    }
}

// ---- --headless-test ----

int runHeadlessTest(const HeadlessServer::Options& options, const QString& outPath, bool music)
{
    auto say = [](const QString& t) {
        std::fprintf(stderr, "[headless-test] %s\n", t.toUtf8().constData());
        std::fflush(stderr);
    };
    HeadlessServer::Options serverOptions = options;
    serverOptions.startComfy = false;   // the test starts it itself, with comfyui-launch
    serverOptions.port = 0;             // any free port
    HeadlessServer server(serverOptions);
    QObject::connect(&server, &HeadlessServer::log, [&say](const QString& t) { say(QStringLiteral("  server: ") + t); });
    QString why;
    if (!server.listen(&why)) {
        say(QStringLiteral("cannot listen: ") + why);
        return 2;
    }
    QTcpSocket socket;
    QList<QJsonObject> steps;   // the commands still to send
    int failures = 0, checks = 0;
    bool comfyUp = false;
    QJsonObject current;
    std::function<void()> sendNext;
    // what each command must end with
    auto expect = [&](const QJsonObject& end) {
        const QString cmd = current.value(QStringLiteral("command")).toString();
        const bool success = end.value(QStringLiteral("success")).toBool();
        ++checks;
        if (current.value(QStringLiteral("_either")).toBool()) {   // comfyui-status: "not reachable" is an answer too
            comfyUp = success;
            say(QStringLiteral("ok   %1: %2").arg(cmd, end.value(QStringLiteral("message")).toString()));
            return;
        }
        const bool ok = success != current.value(QStringLiteral("_expectFail")).toBool();
        if (!ok)
            ++failures;
        say(QStringLiteral("%1 %2: code %3, %4").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), cmd)
                .arg(end.value(QStringLiteral("code")).toInt()).arg(end.value(QStringLiteral("message")).toString()));
    };
    int progressEvents = 0, previewEvents = 0;
    QObject::connect(&socket, &QTcpSocket::readyRead, [&] {
        while (socket.canReadLine()) {
            const QJsonObject e = QJsonDocument::fromJson(socket.readLine()).object();
            const QString type = e.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("progress")) { ++progressEvents; continue; }
            if (type == QStringLiteral("preview")) { ++previewEvents; continue; }
            if (type == QStringLiteral("result") || type == QStringLiteral("image") || type == QStringLiteral("audio") ||
                type == QStringLiteral("queued") || type == QStringLiteral("log")) {
                QJsonObject shown = e;
                if (shown.contains(QStringLiteral("params")))
                    shown[QStringLiteral("params")] = QStringLiteral("%1 parameters").arg(shown.value(QStringLiteral("params")).toArray().size());
                say(QStringLiteral("  <- ") + QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact)).left(300));
            }
            if (type == QStringLiteral("job_end")) {
                expect(e);
                if (current.value(QStringLiteral("command")).toString() == QStringLiteral("generate"))
                    say(QStringLiteral("  %1 progress events, %2 previews").arg(progressEvents).arg(previewEvents));
                sendNext();
            }
        }
    });
    sendNext = [&] {
        // the steps that need ComfyUI only run when it answered (or was started)
        while (!steps.isEmpty() && steps.first().value(QStringLiteral("_needsComfy")).toBool() && !comfyUp) {
            say(QStringLiteral("skip %1: no ComfyUI (add --start to start the local one)").arg(steps.first().value(QStringLiteral("command")).toString()));
            steps.removeFirst();
        }
        if (steps.isEmpty()) {
            say(failures == 0 ? QStringLiteral("ALL CHECKS PASSED (%1)").arg(checks) : QStringLiteral("SOME CHECKS FAILED (%1 of %2)").arg(failures).arg(checks));
            QCoreApplication::exit(failures == 0 ? 0 : 1);
            return;
        }
        current = steps.takeFirst();
        QJsonObject wire{{QStringLiteral("command"), current.value(QStringLiteral("command"))}, {QStringLiteral("args"), current.value(QStringLiteral("args"))}};
        say(QStringLiteral("-> ") + QString::fromUtf8(QJsonDocument(wire).toJson(QJsonDocument::Compact)));
        socket.write(QJsonDocument(wire).toJson(QJsonDocument::Compact) + "\n");
    };
    auto step = [](const QString& cmd, const QJsonObject& args, bool needsComfy = false, bool expectFail = false) {
        return QJsonObject{{QStringLiteral("command"), cmd}, {QStringLiteral("args"), args}, {QStringLiteral("_needsComfy"), needsComfy},
                           {QStringLiteral("_expectFail"), expectFail}};
    };
    if (options.startComfy)
        steps << step(QStringLiteral("comfyui-launch"), {});
    QJsonObject status = step(QStringLiteral("comfyui-status"), {});
    status[QStringLiteral("_either")] = true;
    steps << status
          << step(QStringLiteral("no-such-command"), {}, false, true)
          << step(QStringLiteral("connect"), {}, true)
          << step(QStringLiteral("get-params"), {}, true)
          << step(QStringLiteral("set-params"), {{QStringLiteral("params"), QJsonObject{{QStringLiteral("no_such_input"), 1}}}}, true, true)
          << step(QStringLiteral("set-params"), {{QStringLiteral("params"), QJsonObject{{QStringLiteral("steps"), 8}, {QStringLiteral("width"), 768},
                                                                                       {QStringLiteral("height"), 768}}}}, true)
          << step(QStringLiteral("generate"), {{QStringLiteral("out"), outPath}, {QStringLiteral("random-seed"), true}}, true);
    if (music) {   // the ACE-Step song graph: a short song, saved as .mp3 next to the picture
        const QString songPath = QFileInfo(outPath).path() + QLatin1Char('/') + QFileInfo(outPath).completeBaseName() + QStringLiteral("_song.mp3");
        steps << step(QStringLiteral("reset-graph"), {{QStringLiteral("graph"), QStringLiteral("music")}}, true)
              << step(QStringLiteral("set-params"), {{QStringLiteral("params"), QJsonObject{{QStringLiteral("duration"), 20}}}}, true)
              << step(QStringLiteral("generate"), {{QStringLiteral("out"), songPath}, {QStringLiteral("random-seed"), true}}, true)
              << step(QStringLiteral("reset-graph"), {{QStringLiteral("graph"), QStringLiteral("image")}}, true);
    }
    QObject::connect(&socket, &QTcpSocket::connected, [&] { sendNext(); });
    QTimer::singleShot(600000, [&say] { say(QStringLiteral("timed out")); QCoreApplication::exit(3); });
    socket.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    return QCoreApplication::exec();
}
