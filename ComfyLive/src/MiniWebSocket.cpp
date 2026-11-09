#include "MiniWebSocket.h"

#include <QRandomGenerator>

MiniWebSocket::MiniWebSocket(QObject* parent)
    : QObject(parent)
{
    connect(&socket_, &QTcpSocket::connected, this, &MiniWebSocket::onConnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &MiniWebSocket::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected, this, &MiniWebSocket::finish);
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorText(socket_.errorString());
        if (socket_.state() == QAbstractSocket::UnconnectedState)
            finish();
    });
}

void MiniWebSocket::open(const QString& host, quint16 port, const QString& pathAndQuery)
{
    close();
    host_ = host;
    port_ = port;
    path_ = pathAndQuery;
    buffer_.clear();
    fragment_.clear();
    handshakeDone_ = false;
    open_ = false;
    active_ = true;
    socket_.connectToHost(host, port);
}

void MiniWebSocket::close()
{
    if (!active_)
        return;
    if (open_)
        sendFrame(0x8, QByteArray());   // a polite close; the server drops the connection
    socket_.abort();
    finish();
}

void MiniWebSocket::finish()
{
    if (!active_)
        return;
    active_ = false;
    open_ = false;
    handshakeDone_ = false;
    emit closed();
}

void MiniWebSocket::onConnected()
{
    QByteArray nonce(16, '\0');
    for (char& c : nonce)
        c = char(QRandomGenerator::global()->bounded(256));
    const QByteArray key = nonce.toBase64();
    QByteArray request;
    request += "GET " + path_.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + host_.toUtf8() + ":" + QByteArray::number(port_) + "\r\n";
    request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    socket_.write(request);
}

void MiniWebSocket::onReadyRead()
{
    buffer_ += socket_.readAll();
    if (!handshakeDone_) {
        const qsizetype end = buffer_.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = buffer_.left(end);
        buffer_.remove(0, end + 4);
        if (!head.startsWith("HTTP/1.1 101")) {
            emit errorText(QStringLiteral("the server did not accept the WebSocket: ") + QString::fromUtf8(head.left(80)));
            socket_.abort();
            return;
        }
        handshakeDone_ = true;
        open_ = true;
        emit opened();
    }
    // frames: 2 bytes, an extended length, a mask key (servers do not mask), the payload
    while (buffer_.size() >= 2) {
        const quint8 b0 = quint8(buffer_[0]);
        const quint8 b1 = quint8(buffer_[1]);
        const bool fin = (b0 & 0x80) != 0;
        const quint8 op = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        quint64 len = b1 & 0x7F;
        qsizetype at = 2;
        if (len == 126) {
            if (buffer_.size() < 4)
                return;
            len = (quint64(quint8(buffer_[2])) << 8) | quint8(buffer_[3]);
            at = 4;
        } else if (len == 127) {
            if (buffer_.size() < 10)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | quint8(buffer_[2 + i]);
            at = 10;
        }
        QByteArray mask;
        if (masked) {
            if (buffer_.size() < at + 4)
                return;
            mask = buffer_.mid(at, 4);
            at += 4;
        }
        if (quint64(buffer_.size() - at) < len)
            return;   // the rest of this frame has not arrived yet
        QByteArray payload = buffer_.mid(at, qsizetype(len));
        buffer_.remove(0, at + qsizetype(len));
        if (masked)
            for (qsizetype i = 0; i < payload.size(); ++i)
                payload[i] = char(payload[i] ^ mask[i % 4]);

        switch (op) {
        case 0x0:   // continuation
            fragment_ += payload;
            if (fin) {
                if (fragmentOp_ == 0x1)
                    emit textMessage(QString::fromUtf8(fragment_));
                else
                    emit binaryMessage(fragment_);
                fragment_.clear();
            }
            break;
        case 0x1:
        case 0x2:
            if (!fin) {
                fragment_ = payload;
                fragmentOp_ = op;
            } else if (op == 0x1) {
                emit textMessage(QString::fromUtf8(payload));
            } else {
                emit binaryMessage(payload);
            }
            break;
        case 0x8:   // close
            socket_.disconnectFromHost();
            return;
        case 0x9:   // ping
            sendFrame(0xA, payload);
            break;
        default:    // pong and anything unknown
            break;
        }
    }
}

void MiniWebSocket::sendFrame(quint8 opcode, const QByteArray& payload)
{
    // A client must mask every frame it sends.
    QByteArray frame;
    frame += char(0x80 | opcode);
    if (payload.size() < 126) {
        frame += char(0x80 | payload.size());
    } else {
        frame += char(0x80 | 126);
        frame += char((payload.size() >> 8) & 0xFF);
        frame += char(payload.size() & 0xFF);
    }
    QByteArray mask(4, '\0');
    for (char& c : mask)
        c = char(QRandomGenerator::global()->bounded(256));
    frame += mask;
    for (qsizetype i = 0; i < payload.size(); ++i)
        frame += char(payload[i] ^ mask[i % 4]);
    socket_.write(frame);
}
