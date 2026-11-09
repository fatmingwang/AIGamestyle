#pragma once
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "Workflow.h"

#include <QElapsedTimer>
#include <QHash>
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
// song played when the graph makes audio: ACE-Step and other SaveAudio graphs).
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
    void onOpenWorkflow();
    void onSaveWorkflow();
    void onSaveImage();
    void onSaveAudio();
    void showParamHelp();      // every parameter of the graph and what it changes
    void showAudio(const QString& path, const QString& shown, bool autoPlay);
    void addAudioToHistory(const QString& path, const QString& shown);
    void onTranspose();
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
    Workflow      workflow_;

    class QComboBox* languageCombo_ = nullptr;
    QLineEdit*    serverEdit_ = nullptr;
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
};
