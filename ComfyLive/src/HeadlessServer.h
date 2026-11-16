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
        QString speechServer = QStringLiteral("127.0.0.1:8189");   // the speech ComfyUI (.venv-speech): Qwen3-TTS graphs
        QString comfyRoot;                            // for comfyui-launch
        QString graphPath;                            // a graph to start with (else the built-in one)
        bool startComfy = false;                      // start the local ComfyUI now if nothing answers
        bool keepComfy = true;                        // a ComfyUI it starts stays running after it exits (false:
                                                      // --stop-comfy-on-exit); comfyui-stop / --comfy-stop end it
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
    void onImage(const QString& file, const QImage& image);   // one picture of the running job: saved to "out", sent
    // The frames of a video graph: frame_0001.png ... in <out>_frames, the settings beside "out", then the MP4 (ffmpeg);
    // done() once the "video" event is sent.
    void onVideoFrames(const QList<QImage>& frames, const QStringList& files, const std::function<void()>& done);
    QJsonArray paramsJson() const;
    bool setParams(const QJsonObject& values, QStringList* changed, QString* why);
    // The ComfyUI the graph runs on: the speech one for a Qwen3-TTS graph, else the main one. routeForGraph() points the
    // client at it (disconnecting from the other); processFor() is the one that starts it.
    QString targetServer() const;
    void routeForGraph();
    ComfyProcess& processFor(const QString& server);

    Options options_;
    QTcpServer tcp_;
    ComfyClient client_;
    ComfyProcess process_;
    ComfyProcess speechProcess_;
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
    bool gotImage_ = false;   // an image, a song or a video came back
    bool videoRun_ = false;   // the graph that was queued makes a video: its pictures are frames
    double videoFps_ = 24.0;
    bool setPending_ = false;             // a set of pictures is still being read (or made into a video)
    std::function<void()> afterSet_;      // the job's end, waiting for it
    QElapsedTimer clock_;
};

// `ComfyLive --headless-test`: the server and a built-in client in one process, like GameStyleAI's --headless-test.
// Protocol checks always; with ComfyUI reachable (or --start), a real generation too. Exit 0 = all passed.
// music: also the built-in ACE-Step 1.5 song graph (a 20 s song saved next to outPath as .mp3).
int runHeadlessTest(const HeadlessServer::Options& options, const QString& outPath, bool music = false);

// `ComfyLive --make=recipe.json [--out=x.mp3] [--random-seed] [--stop-comfy-on-exit]` (--song= is the same): one song,
// sound or speech from a JSON file, then exit. The server and a client in one process, sending the `make` command:
// ComfyUI is used if it answers, else the local one is started and left running for the next run (--stop-comfy-on-exit:
// stopped at the end). Exit = the command's code (0: the file was written).
int runSongFile(const HeadlessServer::Options& options, const QString& songJson, const QString& outPath, bool randomSeed);
