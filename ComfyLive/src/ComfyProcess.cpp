#include "ComfyProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kPollMs = 1000;
constexpr int kGiveUpMs = 300000;   // a first start with every add-on can take minutes
}

ComfyProcess::ComfyProcess(QObject* parent)
    : QObject(parent)
{
    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, &ComfyProcess::poll);
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (poll_.isActive()) {
            poll_.stop();
            emit failed(QStringLiteral("ComfyUI stopped while starting (exit %1). See %2").arg(code).arg(logPath_));
        }
    });
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}

ComfyProcess::~ComfyProcess()
{
    stop();
}

void ComfyProcess::start(const QString& root, quint16 port, bool customNodes)
{
    if (isRunning())
        return;
    const QDir dir(root);
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("main.py")))) {
        emit failed(QStringLiteral("No ComfyUI in %1 (main.py not found). Set the ComfyUI folder.").arg(QDir::toNativeSeparators(root)));
        return;
    }
#ifdef Q_OS_WIN
    QString python = dir.filePath(QStringLiteral(".venv/Scripts/python.exe"));
#else
    QString python = dir.filePath(QStringLiteral(".venv/bin/python"));
#endif
    if (!QFileInfo::exists(python))
        python = QStringLiteral("python");
    QStringList args{QStringLiteral("main.py"), QStringLiteral("--disable-auto-launch"), QStringLiteral("--port"), QString::number(port),
                     QStringLiteral("--preview-method"), QStringLiteral("auto")};
    if (!customNodes)
        args << QStringLiteral("--disable-all-custom-nodes");
    port_ = port;
    logPath_ = QDir(dir.filePath(QStringLiteral(".."))).absoluteFilePath(QStringLiteral("comfylive_server.log"));
    process_.setWorkingDirectory(dir.absolutePath());
    process_.setProcessChannelMode(QProcess::MergedChannels);
    process_.setStandardOutputFile(logPath_);
    process_.start(python, args);
    if (!process_.waitForStarted(10000)) {
        emit failed(QStringLiteral("Could not start %1: %2").arg(python, process_.errorString()));
        return;
    }
    emit logLine(QStringLiteral("starting ComfyUI (hidden, log: %1) ...").arg(QDir::toNativeSeparators(logPath_)));
    waitedMs_ = 0;
    poll_.start();
}

void ComfyProcess::stop()
{
    poll_.stop();
    if (!isRunning())
        return;
    process_.kill();
    process_.waitForFinished(5000);
    emit logLine(QStringLiteral("ComfyUI stopped"));
}

void ComfyProcess::poll()
{
    waitedMs_ += kPollMs;
    if (waitedMs_ > kGiveUpMs) {
        poll_.stop();
        emit failed(QStringLiteral("ComfyUI did not answer within %1 s. See %2").arg(kGiveUpMs / 1000).arg(logPath_));
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/system_stats").arg(port_)));
    request.setTransferTimeout(900);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && poll_.isActive()) {
            poll_.stop();
            emit logLine(QStringLiteral("ComfyUI is up after %1 s").arg(waitedMs_ / 1000));
            emit up();
        }
    });
}
