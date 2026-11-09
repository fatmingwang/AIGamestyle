#pragma once
#include "MiniWebSocket.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>

// ComfyUI's server API, as the browser front end uses it:
//   GET  /object_info          every node class and its inputs (types, ranges, choices)
//   POST /prompt               queue a graph (API format) -> prompt_id
//   POST /interrupt, /queue    stop the running prompt, drop queued ones
//   GET  /view                 an output image
//   ws   /ws?clientId=...      status, progress, executing / executed, and binary preview images
// Previews only come when ComfyUI was started with --preview-method (ComfyLive starts it with "auto").
class ComfyClient : public QObject
{
    Q_OBJECT
public:
    explicit ComfyClient(QObject* parent = nullptr);

    // "127.0.0.1:8188". Reads /object_info, then opens the WebSocket; it reconnects by itself while wanted.
    void connectToServer(const QString& server);
    void disconnectFromServer();
    void setServer(const QString& server) { server_ = server.trimmed(); }   // for ping() before connecting
    bool isReady() const { return ready_; }
    QString server() const { return server_; }
    const QJsonObject& objectInfo() const { return objectInfo_; }

    void queuePrompt(const QJsonObject& graph);
    void interrupt();
    void deleteQueued(const QStringList& promptIds);
    void ping(const std::function<void(bool)>& done);   // GET /system_stats: is a server there at all?

signals:
    void connected();                                   // object info read and the WebSocket open
    void connectionFailed(const QString& why);
    void disconnected();
    void queued(const QString& promptId, int number);
    void promptRejected(const QString& why);            // /prompt said no (a bad value, a missing model ...)
    void queueRemaining(int count);
    void executing(const QString& promptId, const QString& nodeId);
    void progress(const QString& promptId, int value, int max);
    void preview(const QImage& image);
    void imageReady(const QString& promptId, const QImage& image, const QString& fileName);
    void audioReady(const QString& promptId, const QByteArray& bytes, const QString& fileName);   // the file as saved (mp3, flac ...)
    void finished(const QString& promptId);
    void failed(const QString& promptId, const QString& why);
    void interrupted(const QString& promptId);
    void logLine(const QString& text);

private:
    QUrl url(const QString& path) const;
    void openSocket();
    void onSocketText(const QString& text);
    void onSocketBinary(const QByteArray& data);
    void fetchImage(const QString& promptId, const QJsonObject& ref);
    void fetchAudio(const QString& promptId, const QJsonObject& ref);
    QNetworkReply* fetchFile(const QJsonObject& ref, QString* shown);

    QNetworkAccessManager net_;
    MiniWebSocket socket_;
    QTimer reconnect_;
    QString server_;
    QString clientId_;
    QJsonObject objectInfo_;
    bool wanted_ = false;
    bool ready_ = false;
};
