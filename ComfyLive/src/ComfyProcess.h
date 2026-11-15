#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Starts the local ComfyUI (AIGamestyle/_comfyui/ComfyUI, with its own .venv) without a console window, waits until it
// answers, and stops it again. It always passes --preview-method auto: without it ComfyUI sends no previews while
// sampling, and ComfyLive could only show the finished picture.
class ComfyProcess : public QObject
{
    Q_OBJECT
public:
    explicit ComfyProcess(QObject* parent = nullptr);
    ~ComfyProcess() override;

    // root: the ComfyUI folder (main.py). customNodes false: --disable-all-custom-nodes (starts faster; plain graphs
    // only). The log goes to <root>/../comfylive_server.log.
    void start(const QString& root, quint16 port, bool customNodes);
    void stop();
    bool isRunning() const { return process_.state() != QProcess::NotRunning; }
    // keep: the next start() runs ComfyUI detached, so it stays up after this program exits (stop() leaves it alone);
    // the next `--song` or window finds it running and starts at once.
    void setKeepRunning(bool keep) { keepRunning_ = keep; }
    // Which ComfyUI this is: its venv under the ComfyUI folder (".venv"; the speech one ".venv-speech"), the only custom
    // nodes it loads (empty: all of them, or none without customNodes), its log file beside the ComfyUI folder.
    void setLaunch(const QString& venv, const QStringList& onlyCustomNodes, const QString& logName)
    {
        venv_ = venv;
        onlyCustomNodes_ = onlyCustomNodes;
        logName_ = logName;
    }
    // Stops the ComfyUI that listens on 127.0.0.1:port, whoever started it (an earlier ComfyLive run that kept it, or a
    // hand-started one): only a python process is ended, with the venv's python.exe launcher above it. True when one was
    // stopped; message says what happened either way.
    static bool stopLocal(quint16 port, QString* message);
    QString logPath() const { return logPath_; }

signals:
    void up();                          // it answers on the port
    void failed(const QString& why);
    void logLine(const QString& text);

private:
    void poll();

    QProcess process_;
    QNetworkAccessManager net_;
    QTimer poll_;
    quint16 port_ = 8188;
    int waitedMs_ = 0;
    bool keepRunning_ = false;
    QString logPath_;
    QString venv_ = QStringLiteral(".venv");
    QStringList onlyCustomNodes_;
    QString logName_ = QStringLiteral("comfylive_server.log");
    qint64 detachedPid_ = 0;   // a kept (detached) ComfyUI while it starts: watched for an early exit
};
