#pragma once
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "Workflow.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QTimer>

class AudioPlayer;
class ImageView;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;

// ComfyLive: the parameters of a ComfyUI graph as a form, sent to ComfyUI, the picture shown as it forms (or the
// song played when the graph makes audio: ACE-Step and other SaveAudio graphs; or the video played in a loop when the
// graph makes one: Wan 2.2, whose frames come back as pictures).
//
//  left   the server (connect, or start the local ComfyUI), the graph (built-in text to image, or any API-format
//         .json), its parameters (made from the graph and the server's node list), Generate / Stop / Live
//  right  the picture (the sampler's previews, then the result), the last results, a log
//
// Live: every change starts a new picture after a short pause; a run that is out of date is stopped.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Connect to the server in the box; when nothing answers and startServer, start the local ComfyUI first.
    void startUp(bool startServer);
    void generate();
    void setSteps(int steps);
    // For --screenshot runs: open this graph, settings or recipe file as Open graph... does. From then on the window
    // saves nothing (the graph and values the user left stay as they were). False: the file could not be read.
    bool openGraphForCheck(const QString& path, QString* why);
    // Delete result: the files of the result picked in the history (the one on screen) go to the Recycle Bin: its
    // picture, or its frames folder, MP4 and settings, or its sound and settings, and ComfyUI's own copy in its output
    // folder when that ComfyUI is on this PC. confirm: ask first, naming the files. Returns what was moved.
    QStringList deleteCurrentResult(bool confirm);
    void setLanguage(const QString& code);   // as if picked in the Language box (--screenshot --switch-lang)   // for --screenshot runs: the sampler's steps, when the graph has them

signals:
    void ready();                           // connected and the form built
    void runDone(bool ok, const QString& note);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void rebuildUi();          // after a language change: the same window, state and all, in the new language
    void buildParams();
    void onParamChanged();
    void onConnectClicked();
    void onStartServerClicked();
    void onStopServerClicked();   // the local ComfyUIs, whoever started them (they are never stopped by themselves)
    void stopLocalServers();
    QString targetServer() const;   // the speech server for a Qwen3-TTS graph, else the main one
    void onOpenWorkflow();
    void onSaveWorkflow();
    void onSaveImage();
    void onSaveAudio();
    void showParamHelp();      // every parameter of the graph and what it changes
    void showAudio(const QString& path, const QString& shown, bool autoPlay);
    void addAudioToHistory(const QString& path, const QString& shown);
    void onTranspose();
    void addImage(const QString& id, const QImage& image, const QString& file);   // one result picture: history, the view
    // A video graph's frames: kept as frame_0001.png ... with their settings in ComfyLive's own folder, an MP4 made of
    // them (ffmpeg), a history item, and played in the view.
    void addVideo(const QString& id, const QList<QImage>& frames, const QStringList& files);
    void showVideo(const QString& mp4Path, const QList<QImage>& frames, double fps, const QString& shown);
    void hideVideo();          // the view shows something else: the frames stop and are let go
    void showFrame(int index);
    void onSaveVideo();
    QString comfyFilePath(const QString& file) const;
    // The note over the view when a size or a length changes (and before a run): the size, the frames, what such a
    // run cost on this PC (VramLog) and the card's memory. It goes away by itself.
    void showSizeNote();
    void recordRun(bool failed);
    // a new result in the history: the files it owns (for Delete result), picked when it is the one on screen
    void addHistoryItem(class QListWidgetItem* item, const QStringList& files, bool onScreen);   // the full path of a file ComfyUI wrote ("output/x.png"), for the log
    void setTextField(const QString& key, const QString& text);   // a text field and the graph, as if typed
    void syncNumberFields();   // after a tied input changed with another (an ACE-Step song's duration and seconds)
    void stopRun();
    void setWorkflow(const Workflow& w);
    void randomizeSeeds();
    void setStatus(const QString& text);
    void log(const QString& text);
    void loadSettings();
    void saveSettings() const;

    ComfyClient   client_;
    ComfyProcess  server_;
    ComfyProcess  speechServer_;            // the speech ComfyUI (.venv-speech, Qwen3-TTS only), port 8189
    Workflow      workflow_;

    class QComboBox* languageCombo_ = nullptr;
    QLineEdit*    serverEdit_ = nullptr;
    QLineEdit*    speechServerEdit_ = nullptr;
    QLineEdit*    comfyRootEdit_ = nullptr;
    QCheckBox*    customNodesCheck_ = nullptr;
    QCheckBox*    stopServerCheck_ = nullptr;
    QPushButton*  connectBtn_ = nullptr;
    QPushButton*  startServerBtn_ = nullptr;
    QLabel*       statusLabel_ = nullptr;
    QLabel*       workflowLabel_ = nullptr;
    QScrollArea*  paramsArea_ = nullptr;
    QCheckBox*    liveCheck_ = nullptr;
    QCheckBox*    randomSeedCheck_ = nullptr;
    QSpinBox*     liveDelaySpin_ = nullptr;
    QPushButton*  generateBtn_ = nullptr;
    QPushButton*  stopBtn_ = nullptr;
    QPushButton*  saveImageBtn_ = nullptr;
    QPushButton*  deleteBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    ImageView*    view_ = nullptr;
    QListWidget*  history_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QWidget*      audioRow_ = nullptr;      // shown once there is a song
    QLabel*       audioLabel_ = nullptr;
    QPushButton*  playBtn_ = nullptr;
    QPushButton*  openAudioBtn_ = nullptr;
    QPushButton*  saveAudioBtn_ = nullptr;
    class QSpinBox* transposeSpin_ = nullptr;
    QPushButton*  transposeBtn_ = nullptr;
    QWidget*      videoRow_ = nullptr;      // shown once there is a video
    QLabel*       videoLabel_ = nullptr;
    QPushButton*  videoPlayBtn_ = nullptr;
    QPushButton*  openFramesBtn_ = nullptr;
    QPushButton*  saveVideoBtn_ = nullptr;
    QList<QImage> frames_;                  // the video being shown
    QTimer        frameTimer_;
    int           frameIndex_ = 0;
    double        framesFps_ = 24.0;
    QString       videoPath_;               // its MP4 (the frames are in <name>_frames beside it)
    QString       videoShown_;
    QHash<QString, double> videoIds_;       // our prompts that make a video -> its frames a second
    QString       doneAfterVideo_;          // the run that ended while its frames were still being read: runDone then
    bool          nextIsVideo_ = false;     // the graph being queued makes a video
    double        nextFps_ = 24.0;
    QLabel*       toast_ = nullptr;         // the size note, over the view
    QTimer        toastTimer_;
    QTimer        vramTimer_;               // reads the GPU memory while a run is on
    double        vramPeakGB_ = 0, vramTotalGB_ = 0;
    QString       instrumentalBackup_;      // the lyrics before Singer = Instrumental replaced them
    AudioPlayer*  player_ = nullptr;

    QHash<QString, QWidget*> fields_;       // "<node id>/<input>" -> its editor (to show a new random seed)
    QTimer        liveTimer_;
    QElapsedTimer runClock_;
    QString       currentId_;               // the prompt being shown
    QStringList   pending_;                 // ours, queued or running
    int           previews_ = 0;
    bool          startedServer_ = false;
    bool          connectWanted_ = false;
    bool          generateWhenConnected_ = false;
    bool          checkRun_ = false;        // openGraphForCheck: saveSettings() does nothing
};
