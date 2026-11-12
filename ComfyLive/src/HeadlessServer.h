#pragma once
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "Workflow.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

// `ComfyLive --headless`: the same work as the window, over TCP, with no window (README.md "Headless mode").
//
// The same shape as GameStyleAI's --headless (Doc/Commands_Reference.md §12): a client sends one JSON line per command,
//   {"command": "<name>", "args": {...}}
// and gets NDJSON events back, starting with {"type": "job_start"} and ending with
//   {"type": "job_end", "command", "code", "success", "message"}
// Commands run one after another; the graph and its values stay for the next command and the next connection.
class HeadlessServer : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 8766;                          // GameStyleAI's server is 8765
        QString comfyServer = QStringLiteral("127.0.0.1:8188");
        QString comfyRoot;                            // for comfyui-launch
        QString graphPath;                            // a graph to start with (else the built-in one)
        bool startComfy = false;                      // start the local ComfyUI now if nothing answers
        bool keepComfy = false;                       // a ComfyUI it starts stays running after it exits
    };

    explicit HeadlessServer(const Options& options, QObject* parent = nullptr);
    ~HeadlessServer() override;
    bool listen(QString* why);
    quint16 port() const { return tcp_.serverPort(); }

signals:
    void log(const QString& text);

private:
    struct Job {
        QPointer<QTcpSocket> client;
        QString command;
        QJsonObject args;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void next();                                       // the next queued command, when none is running
    void run(const Job& job);
    void send(const QJsonObject& event);
    void result(const QJsonObject& fields);
    void end(int code, const QString& message);
    void whenConnected(const std::function<void()>& then);   // connect first if needed, else end with code 4
    void whenComfy(const std::function<void()>& then);       // ... and start the local ComfyUI first if nothing answers
    bool generating() const;                                  // the running command makes a picture or a song
    void startGenerate(const QJsonObject& args);              // queue the graph: generate's args (out, preview-dir, random-seed)
    void runSong(const QJsonObject& args);
    QJsonArray paramsJson() const;
    bool setParams(const QJsonObject& values, QStringList* changed, QString* why);

    Options options_;
    QTcpServer tcp_;
    ComfyClient client_;
    ComfyProcess process_;
    Workflow workflow_;
    QList<Job> queue_;
    Job job_;                                          // the running one (command empty: none)
    bool busy_ = false;
    std::function<void()> afterConnect_;
    std::function<void()> afterLaunch_;                // whenComfy: once the ComfyUI it started is up
    // generate
    QString promptId_;
    QString outPath_;
    QString previewDir_;
    int previews_ = 0;
    bool gotImage_ = false;   // an image or a song came back
    QElapsedTimer clock_;
};

// `ComfyLive --headless-test`: the server and a built-in client in one process, like GameStyleAI's --headless-test.
// Protocol checks always; with ComfyUI reachable (or --start), a real generation too. Exit 0 = all passed.
// music: also the built-in ACE-Step 1.5 song graph (a 20 s song saved next to outPath as .mp3).
int runHeadlessTest(const HeadlessServer::Options& options, const QString& outPath, bool music = false);

// `ComfyLive --song=song.json [--out=song.mp3] [--random-seed] [--keep-comfy]`: one song from a JSON file, then exit.
// The server and a client in one process, sending the `song` command: ComfyUI is used if it answers, else the local one
// is started (and stopped again at the end, unless --keep-comfy). Exit = the command's code (0: the song was written).
int runSongFile(const HeadlessServer::Options& options, const QString& songJson, const QString& outPath, bool randomSeed);
