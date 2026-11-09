#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpSocket>

// A small WebSocket client (RFC 6455) on QTcpSocket: enough for ComfyUI's /ws, which pushes the queue status,
// progress and preview images while a prompt runs. Plain ws:// only (no TLS, no extensions).
//
// Qt's own QtWebSockets module does the same, but it is a separate Qt install; this keeps ComfyLive on the
// Widgets + Network modules the GameStyleAI app already uses.
class MiniWebSocket : public QObject
{
    Q_OBJECT
public:
    explicit MiniWebSocket(QObject* parent = nullptr);

    // pathAndQuery: e.g. "/ws?clientId=..."
    void open(const QString& host, quint16 port, const QString& pathAndQuery);
    void close();
    bool isOpen() const { return open_; }

signals:
    void opened();
    void closed();                           // once per open(), whether it ever opened or not
    void textMessage(const QString& text);
    void binaryMessage(const QByteArray& data);
    void errorText(const QString& text);

private:
    void onConnected();
    void onReadyRead();
    void finish();
    void sendFrame(quint8 opcode, const QByteArray& payload);

    QTcpSocket socket_;
    QByteArray buffer_;
    QByteArray fragment_;                    // a message split over several frames
    quint8     fragmentOp_ = 0;
    QString    host_;
    QString    path_;
    quint16    port_ = 0;
    bool       active_ = false;              // between open() and closed()
    bool       handshakeDone_ = false;
    bool       open_ = false;
};
