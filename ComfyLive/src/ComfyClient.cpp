#include "ComfyClient.h"

#include <memory>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
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

void ComfyClient::gpuMemory(const std::function<void(bool, double, double)>& done)
{
    QNetworkRequest request(url(QStringLiteral("/system_stats")));
    request.setTransferTimeout(3000);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const QJsonObject device = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("devices")).toArray().at(0).toObject();
        const double total = device.value(QStringLiteral("vram_total")).toDouble(), free = device.value(QStringLiteral("vram_free")).toDouble();
        const double gb = 1024.0 * 1024.0 * 1024.0;
        done(reply->error() == QNetworkReply::NoError && total > 0, total / gb, (total - free) / gb);
    });
}

void ComfyClient::freeMemory(const QString& server, const std::function<void()>& done)
{
    // POST /free on another ComfyUI (nothing there: nothing to free). Its models leave the GPU before ours load.
    QNetworkRequest request(QUrl(QStringLiteral("http://") + server.trimmed() + QStringLiteral("/free")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(3000);
    QNetworkReply* reply = net_.post(request, QByteArrayLiteral("{\"unload_models\": true, \"free_memory\": true}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, server, done] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            emit logLine(QStringLiteral("asked %1 to free its GPU memory").arg(server));
        done();
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
    // the pictures to upload first: LoadImage nodes that name a file on this PC
    struct Upload { QString nodeId, path; };
    auto todo = std::make_shared<QList<Upload>>();
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        const QJsonObject n = it.value().toObject();
        if (n.value(QStringLiteral("class_type")).toString() != QStringLiteral("LoadImage"))
            continue;
        const QString image = n.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString();
        if (image.trimmed().isEmpty()) {
            emit promptRejected(QStringLiteral("no reference picture: choose one (the Reference picture field, or \"image\" in the recipe)"));
            return;
        }
        if (QFileInfo(image).isAbsolute()) {
            if (!QFileInfo(image).isFile()) {
                emit promptRejected(QStringLiteral("the reference picture is not there: ") + image);
                return;
            }
            todo->append({it.key(), image});
        }
    }
    if (todo->isEmpty()) {
        postPrompt(graph, graph);
        return;
    }
    auto send = std::make_shared<QJsonObject>(graph);
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, todo, send, graph, next] {
        if (todo->isEmpty()) {
            postPrompt(*send, graph);
            *next = nullptr;   // lets go of itself
            return;
        }
        const Upload u = todo->takeFirst();
        QFile f(u.path);
        if (!f.open(QIODevice::ReadOnly)) {
            emit promptRejected(QStringLiteral("could not read the reference picture ") + u.path);
            *next = nullptr;
            return;
        }
        const QByteArray bytes = f.readAll();
        // the same picture gets the same name on the server, another picture of the same file name does not replace it
        const QString name = QStringLiteral("comfylive_%1_%2").arg(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(10)),
                                                                   QFileInfo(u.path).fileName());
        auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart file;
        file.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(QString(name).replace(QLatin1Char('"'), QLatin1Char('_'))));
        file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        file.setBody(bytes);
        multi->append(file);
        for (const auto& [key, value] : {std::pair{"overwrite", "true"}, std::pair{"type", "input"}}) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(QLatin1String(key)));
            part.setBody(value);
            multi->append(part);
        }
        QNetworkReply* reply = net_.post(QNetworkRequest(url(QStringLiteral("/upload/image"))), multi);
        multi->setParent(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, u, send, next] {
            reply->deleteLater();
            const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
            const QString stored = answer.value(QStringLiteral("name")).toString();
            if (reply->error() != QNetworkReply::NoError || stored.isEmpty()) {
                emit promptRejected(QStringLiteral("could not send the reference picture %1 to ComfyUI: %2").arg(u.path, reply->errorString()));
                *next = nullptr;
                return;
            }
            const QString sub = answer.value(QStringLiteral("subfolder")).toString();
            QJsonObject n = send->value(u.nodeId).toObject();
            QJsonObject inputs = n.value(QStringLiteral("inputs")).toObject();
            inputs[QStringLiteral("image")] = sub.isEmpty() ? stored : sub + QLatin1Char('/') + stored;
            n[QStringLiteral("inputs")] = inputs;
            (*send)[u.nodeId] = n;
            emit logLine(QStringLiteral("reference picture sent: %1 (in ComfyUI's input folder as %2)").arg(QDir::toNativeSeparators(u.path), stored));
            if (*next)
                (*next)();
        });
    };
    (*next)();
}

void ComfyClient::postPrompt(const QJsonObject& sendGraph, const QJsonObject& graph)
{
    QJsonObject body;
    body[QStringLiteral("prompt")] = sendGraph;
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
        const QJsonArray images = output.value(QStringLiteral("images")).toArray();
        if (images.size() > 1)
            fetchImageSet(promptId, images);
        else
            for (const QJsonValue& image : images)
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

void ComfyClient::fetchImageSet(const QString& promptId, const QJsonArray& refs)
{
    struct Set {
        QList<QImage> images;
        QStringList files;
        qsizetype left = 0;
    };
    auto set = std::make_shared<Set>();
    set->images.resize(refs.size());
    set->files.resize(refs.size());
    set->left = refs.size();
    emit imageSetComing(promptId, int(refs.size()));
    for (qsizetype i = 0; i < refs.size(); ++i) {
        QString shown;
        QNetworkReply* reply = fetchFile(refs[i].toObject(), &shown);
        set->files[i] = shown;
        connect(reply, &QNetworkReply::finished, this, [this, reply, promptId, shown, set, i] {
            reply->deleteLater();
            QImage image;
            if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll()))
                emit logLine(QStringLiteral("could not read the image ") + shown);
            else
                set->images[i] = image;
            if (--set->left > 0)
                return;
            QList<QImage> images;   // those that were read, in the output's order
            QStringList files;
            for (qsizetype k = 0; k < set->images.size(); ++k)
                if (!set->images[k].isNull()) {
                    images << set->images[k];
                    files << set->files[k];
                }
            emit imageSetReady(promptId, images, files);
        });
    }
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
