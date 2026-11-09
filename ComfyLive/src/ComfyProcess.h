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
    QString logPath_;
};
