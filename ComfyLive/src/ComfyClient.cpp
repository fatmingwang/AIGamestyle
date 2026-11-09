#include "ComfyClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUuid>

ComfyClient::ComfyClient(QObject* parent)
    : QObject(parent)
    , clientId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    reconnect_.setSingleShot(true);
    reconnect_.setInterval(3000);
    connect(&reconnect_, &QTimer::timeout, this, &ComfyClient::openSocket);
    connect(&socket_, &MiniWebSocket::opened, this, [this] {
        const bool first = !ready_;
        ready_ = true;
        emit logLine(QStringLiteral("WebSocket open"));
        if (first)
            emit connected();
    });
    connect(&socket_, &MiniWebSocket::closed, this, [this] {
        if (ready_) {
            ready_ = false;
            emit disconnected();
            emit logLine(QStringLiteral("WebSocket closed"));
        }
        if (wanted_)
            reconnect_.start();
    });
    connect(&socket_, &MiniWebSocket::textMessage, this, &ComfyClient::onSocketText);
    connect(&socket_, &MiniWebSocket::binaryMessage, this, &ComfyClient::onSocketBinary);
}

ComfyClient::~ComfyClient()
{
    // Members go in reverse order: reconnect_ before socket_. Stop listening to the socket first, so its closing
    // cannot start the timer that is already destroyed (the crash on closing the window), nor emit to the window.
    wanted_ = false;
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    reconnect_.stop();
}

QUrl ComfyClient::url(const QString& path) const
{
    return QUrl(QStringLiteral("http://") + server_ + path);
}

void ComfyClient::ping(const std::function<void(bool)>& done)
{
    QNetworkRequest request(url(QStringLiteral("/system_stats")));
    request.setTransferTimeout(2000);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError);
    });
}

void ComfyClient::connectToServer(const QString& server)
{
    disconnectFromServer();
    server_ = server.trimmed();
    wanted_ = true;
    emit logLine(QStringLiteral("reading the node list from ") + server_);
    QNetworkRequest request(url(QStringLiteral("/object_info")));
    request.setTransferTimeout(30000);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            wanted_ = false;
            emit connectionFailed(reply->errorString());
            return;
        }
        objectInfo_ = QJsonDocument::fromJson(reply->readAll()).object();
        emit logLine(QStringLiteral("%1 node types").arg(objectInfo_.size()));
        openSocket();
    });
}

void ComfyClient::disconnectFromServer()
{
    wanted_ = false;
    reconnect_.stop();
    socket_.close();
    ready_ = false;
}

void ComfyClient::openSocket()
{
    if (!wanted_)
        return;
    const QUrl u = url(QString());
    socket_.open(u.host(), quint16(u.port(8188)), QStringLiteral("/ws?clientId=") + clientId_);
}

void ComfyClient::queuePrompt(const QJsonObject& graph)
{
    QJsonObject body;
    body[QStringLiteral("prompt")] = graph;
    body[QStringLiteral("client_id")] = clientId_;
    QNetworkRequest request(url(QStringLiteral("/prompt")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = net_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, graph] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QJsonObject answer = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || !answer.contains(QStringLiteral("prompt_id"))) {
            // a 400 carries what is wrong, node by node
            QString why = answer.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            const QJsonObject nodeErrors = answer.value(QStringLiteral("node_errors")).toObject();
            for (auto it = nodeErrors.begin(); it != nodeErrors.end(); ++it) {
                const QJsonObject node = it.value().toObject();
                for (const QJsonValue& e : node.value(QStringLiteral("errors")).toArray()) {
                    const QJsonObject eo = e.toObject();
                    why += QStringLiteral("\n  node %1 (%2): %3 %4").arg(it.key(), node.value(QStringLiteral("class_type")).toString(),
                                                                        eo.value(QStringLiteral("message")).toString(),
                                                                        eo.value(QStringLiteral("details")).toString());
                }
            }
            if (why.isEmpty())
                why = reply->errorString() + QStringLiteral(" ") + QString::fromUtf8(data.left(300));
            emit promptRejected(why);
            return;
        }
        const QString id = answer.value(QStringLiteral("prompt_id")).toString();
        sent_.insert(id, graph);
        sentOrder_ << id;
        while (sentOrder_.size() > 50)
            sent_.remove(sentOrder_.takeFirst());
        emit queued(id, answer.value(QStringLiteral("number")).toInt());
    });
}

void ComfyClient::interrupt()
{
    QNetworkRequest request(url(QStringLiteral("/interrupt")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = net_.post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyClient::deleteQueued(const QStringList& promptIds)
{
    if (promptIds.isEmpty())
        return;
    QJsonObject body;
    body[QStringLiteral("delete")] = QJsonArray::fromStringList(promptIds);
    QNetworkRequest request(url(QStringLiteral("/queue")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = net_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyClient::onSocketText(const QString& text)
{
    const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = message.value(QStringLiteral("type")).toString();
    const QJsonObject data = message.value(QStringLiteral("data")).toObject();
    const QString promptId = data.value(QStringLiteral("prompt_id")).toString();
    if (type == QStringLiteral("status")) {
        emit queueRemaining(data.value(QStringLiteral("status")).toObject().value(QStringLiteral("exec_info")).toObject()
                                .value(QStringLiteral("queue_remaining")).toInt());
    } else if (type == QStringLiteral("progress")) {
        emit progress(promptId, data.value(QStringLiteral("value")).toInt(), data.value(QStringLiteral("max")).toInt());
    } else if (type == QStringLiteral("executing")) {
        const QJsonValue node = data.value(QStringLiteral("node"));
        if (node.isNull())
            emit finished(promptId);   // the older servers' "all done"
        else
            emit executing(promptId, node.toString());
    } else if (type == QStringLiteral("executed")) {
        const QJsonObject output = data.value(QStringLiteral("output")).toObject();
        for (const QJsonValue& image : output.value(QStringLiteral("images")).toArray())
            fetchImage(promptId, image.toObject());
        for (const QJsonValue& audio : output.value(QStringLiteral("audio")).toArray())
            fetchAudio(promptId, audio.toObject());
    } else if (type == QStringLiteral("execution_error")) {
        emit failed(promptId, data.value(QStringLiteral("node_type")).toString() + QStringLiteral(": ") +
                                  data.value(QStringLiteral("exception_message")).toString().trimmed());
    } else if (type == QStringLiteral("execution_interrupted")) {
        emit interrupted(promptId);
    }
}

void ComfyClient::onSocketBinary(const QByteArray& data)
{
    // [event type, 4 bytes big-endian][...]. 1: a preview: [image format, 4 bytes][JPEG / PNG]. 4: a preview with
    // metadata: [metadata length, 4 bytes][JSON][image]. Anything else: look for the image's own signature.
    if (data.size() < 8)
        return;
    auto be32 = [&data](qsizetype at) {
        return (quint32(quint8(data[at])) << 24) | (quint32(quint8(data[at + 1])) << 16) | (quint32(quint8(data[at + 2])) << 8) |
               quint32(quint8(data[at + 3]));
    };
    const quint32 event = be32(0);
    QByteArray bytes;
    if (event == 1) {
        bytes = data.mid(8);
    } else if (event == 4) {
        const quint32 meta = be32(4);
        bytes = data.mid(8 + qsizetype(meta));
    } else {
        qsizetype at = data.indexOf(QByteArray("\xFF\xD8\xFF", 3));
        if (at < 0)
            at = data.indexOf(QByteArray("\x89PNG", 4));
        if (at < 0)
            return;
        bytes = data.mid(at);
    }
    QImage image;
    if (image.loadFromData(bytes))
        emit preview(image);
}

QNetworkReply* ComfyClient::fetchFile(const QJsonObject& ref, QString* shown)
{
    QUrl u = url(QStringLiteral("/view"));
    QUrlQuery query;
    const QString fileName = ref.value(QStringLiteral("filename")).toString();
    const QString subfolder = ref.value(QStringLiteral("subfolder")).toString();
    query.addQueryItem(QStringLiteral("filename"), fileName);
    query.addQueryItem(QStringLiteral("subfolder"), subfolder);
    query.addQueryItem(QStringLiteral("type"), ref.value(QStringLiteral("type")).toString());
    u.setQuery(query);
    *shown = ref.value(QStringLiteral("type")).toString() + QStringLiteral("/") +
             (subfolder.isEmpty() ? QString() : subfolder + QStringLiteral("/")) + fileName;
    return net_.get(QNetworkRequest(u));
}

void ComfyClient::fetchImage(const QString& promptId, const QJsonObject& ref)
{
    QString shown;
    QNetworkReply* reply = fetchFile(ref, &shown);
    connect(reply, &QNetworkReply::finished, this, [this, reply, promptId, shown] {
        reply->deleteLater();
        QImage image;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll())) {
            emit logLine(QStringLiteral("could not read the image ") + shown);
            return;
        }
        emit imageReady(promptId, image, shown);
    });
}

void ComfyClient::fetchAudio(const QString& promptId, const QJsonObject& ref)
{
    QString shown;
    QNetworkReply* reply = fetchFile(ref, &shown);
    connect(reply, &QNetworkReply::finished, this, [this, reply, promptId, shown] {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || bytes.isEmpty()) {
            emit logLine(QStringLiteral("could not read the audio ") + shown);
            return;
        }
        emit audioReady(promptId, bytes, shown);
    });
}
