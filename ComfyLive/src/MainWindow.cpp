#include "MainWindow.h"

#include "AudioPlayer.h"
#include "ImageView.h"
#include "ParamHelp.h"
#include "SongTools.h"
#include "Translator.h"
#include "VideoTools.h"
#include "VramLog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QGuiApplication>
#include <QMenu>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#ifndef COMFYLIVE_DEFAULT_COMFY_ROOT
#define COMFYLIVE_DEFAULT_COMFY_ROOT "../_comfyui/ComfyUI"
#endif

namespace {
// Stored with the GameStyleAI app's organisation name (Windows: HKCU\Software\MyWork\ComfyLive).
#define COMFYLIVE_SETTINGS(name) QSettings name(QStringLiteral("MyWork"), QStringLiteral("ComfyLive"))
QString keyOf(const ParamDef& p) { return p.nodeId + QLatin1Char('/') + p.input; }
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , workflow_(Workflow::textToImage())
{
    buildUi();
    loadSettings();
    liveTimer_.setSingleShot(true);
    connect(&liveTimer_, &QTimer::timeout, this, &MainWindow::generate);
    toastTimer_.setSingleShot(true);
    connect(&toastTimer_, &QTimer::timeout, this, [this] {
        if (toast_)
            toast_->hide();
    });
    vramTimer_.setInterval(1200);
    connect(&vramTimer_, &QTimer::timeout, this, [this] {
        client_.gpuMemory([this](bool ok, double total, double used) {
            if (ok && vramTimer_.isActive()) {
                vramTotalGB_ = total;
                vramPeakGB_ = qMax(vramPeakGB_, used);
            }
        });
    });
    connect(&frameTimer_, &QTimer::timeout, this, [this] {
        if (!frames_.isEmpty())
            showFrame((frameIndex_ + 1) % int(frames_.size()));
    });

    connect(&client_, &ComfyClient::logLine, this, &MainWindow::log);
    speechServer_.setLaunch(QStringLiteral(".venv-speech"), {QStringLiteral("ComfyUI-Qwen-TTS")},
                            QStringLiteral("comfylive_speech_server.log"));
    for (ComfyProcess* p : {&server_, &speechServer_}) {
        connect(p, &ComfyProcess::logLine, this, &MainWindow::log);
        connect(p, &ComfyProcess::up, this, [this] {
            startedServer_ = true;
            client_.connectToServer(targetServer());
        });
        connect(p, &ComfyProcess::failed, this, [this](const QString& why) {
            setStatus(tr("ComfyUI did not start"));
            log(why);
            startServerBtn_->setEnabled(true);
            QMessageBox::warning(this, tr("ComfyLive"), why);
        });
    }
    connect(&client_, &ComfyClient::connected, this, [this] {
        setStatus(tr("Connected to %1").arg(client_.server()));
        connectBtn_->setText(tr("Reconnect"));
        startServerBtn_->setEnabled(true);
        for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
            log(line);
        buildParams();
        generateBtn_->setEnabled(true);
        emit ready();
        if (generateWhenConnected_) {   // Generate asked for while switching to the graph's ComfyUI
            generateWhenConnected_ = false;
            generate();
        }
    });
    connect(&client_, &ComfyClient::connectionFailed, this, [this](const QString& why) {
        generateWhenConnected_ = false;
        setStatus(tr("No ComfyUI at %1: press Start local ComfyUI, or start it yourself").arg(client_.server()));
        log(tr("cannot reach %1: %2").arg(client_.server(), why));
        startServerBtn_->setEnabled(true);
    });
    connect(&client_, &ComfyClient::disconnected, this, [this] { setStatus(tr("Connection lost: retrying ...")); });
    connect(&client_, &ComfyClient::queued, this, [this](const QString& id, int) {
        currentId_ = id;
        pending_ << id;
        if (nextIsVideo_)
            videoIds_.insert(id, nextFps_);
        frameTimer_.stop();   // the view is the new run's now (its previews)
        vramPeakGB_ = 0;
        vramTimer_.start();
        previews_ = 0;
        runClock_.start();
        progress_->setValue(0);
        stopBtn_->setEnabled(true);
        view_->setCaption(tr("queued ..."));
    });
    connect(&client_, &ComfyClient::promptRejected, this, [this](const QString& why) {
        log(tr("ComfyUI refused the graph: ") + why);
        view_->setCaption(tr("refused: see the log"));
        emit runDone(false, why);
    });
    connect(&client_, &ComfyClient::progress, this, [this](const QString& id, int value, int max) {
        if (id != currentId_)
            return;
        progress_->setMaximum(std::max(1, max));
        progress_->setValue(value);
        view_->setCaption(tr("step %1 / %2").arg(value).arg(max));
    });
    connect(&client_, &ComfyClient::preview, this, [this](const QImage& image) {
        if (currentId_.isEmpty() || !pending_.contains(currentId_))
            return;
        previews_++;
        view_->setImage(image);
    });
    connect(&client_, &ComfyClient::imageReady, this, &MainWindow::addImage);
    connect(&client_, &ComfyClient::imageSetComing, this, [this](const QString& id, int count) {
        if (id == currentId_)
            view_->setCaption(videoIds_.contains(id) ? tr("reading %1 frames ...").arg(count) : tr("reading %1 pictures ...").arg(count));
    });
    connect(&client_, &ComfyClient::imageSetReady, this, [this](const QString& id, const QList<QImage>& images, const QStringList& files) {
        if (videoIds_.contains(id)) {
            const bool ok = !images.isEmpty();
            if (ok)
                addVideo(id, images, files);
            else
                log(tr("the video's frames could not be read"));
            videoIds_.remove(id);
            if (doneAfterVideo_ == id) {
                doneAfterVideo_.clear();
                emit runDone(ok, QString());
            }
            return;
        }
        for (qsizetype i = 0; i < images.size(); ++i)   // a batch of pictures
            addImage(id, images[i], files[i]);
    });
    connect(&client_, &ComfyClient::audioReady, this, [this](const QString& id, const QByteArray& bytes, const QString& file) {
        // kept in ComfyLive's own folder, so the history can play it again and "Save audio..." can copy it
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/audio");
        QDir().mkpath(dir);
        const QString path = QDir(dir).filePath(QFileInfo(file).fileName());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            log(tr("could not write %1").arg(QDir::toNativeSeparators(path)));
            return;
        }
        f.close();
        addAudioToHistory(path, file);
        log(tr("audio: %1 (%2 KB)").arg(QDir::toNativeSeparators(path)).arg(bytes.size() / 1024));
        if (QFileInfo(QDir(comfyRootEdit_->text()).filePath(file)).isFile())
            log(tr("ComfyUI's own file: %1").arg(comfyFilePath(file)));
        // its settings beside it: Open graph... with them and Generate makes this song again
        QString why;
        const QJsonObject sent = client_.sentGraph(id);
        if (!sent.isEmpty() && Workflow::writeSettings(path, sent, workflow_.name(), {{QStringLiteral("comfyFile"), file}}, &why)) {
            log(tr("settings: %1").arg(QDir::toNativeSeparators(Workflow::settingsPathFor(path))));
            // and beside ComfyUI's own file, when that ComfyUI is on this PC ("output/audio/x.mp3" under its folder)
            const QString original = QDir(comfyRootEdit_->text()).filePath(file);
            if (QFileInfo(original).isFile() && Workflow::writeSettings(original, sent, workflow_.name(), {{QStringLiteral("comfyFile"), file}}, nullptr))
                log(tr("settings: %1").arg(QDir::toNativeSeparators(Workflow::settingsPathFor(original))));
        }
        else if (!sent.isEmpty())
            log(tr("could not write %1").arg(Workflow::settingsPathFor(path)) + QStringLiteral(": ") + why);
        if (id == currentId_)
            showAudio(path, file, true);
    });
    connect(&client_, &ComfyClient::finished, this, [this](const QString& id) {
        if (!pending_.removeAll(id))
            return;
        if (id == currentId_) {
            const double s = runClock_.elapsed() / 1000.0;
            progress_->setValue(progress_->maximum());
            log(tr("done in %1 s, %2 previews").arg(s, 0, 'f', 1).arg(previews_));
            recordRun(false);
            if (videoIds_.contains(id)) {   // its frames are still on their way: the run is done when they are shown
                doneAfterVideo_ = id;
            } else {
                if (frames_.isEmpty())
                    view_->setCaption(tr("done in %1 s").arg(s, 0, 'f', 1));
                emit runDone(true, QString());
            }
        }
        stopBtn_->setEnabled(!pending_.isEmpty());
    });
    connect(&client_, &ComfyClient::failed, this, [this](const QString& id, const QString& why) {
        pending_.removeAll(id);
        stopBtn_->setEnabled(!pending_.isEmpty());
        log(tr("ComfyUI failed: ") + why);
        view_->setCaption(tr("failed: see the log"));
        if (id == currentId_)
            recordRun(true);
        if (id == currentId_)
            emit runDone(false, why);
    });
    connect(&client_, &ComfyClient::interrupted, this, [this](const QString& id) {
        pending_.removeAll(id);
        stopBtn_->setEnabled(!pending_.isEmpty());
    });
}

void MainWindow::addImage(const QString& id, const QImage& image, const QString& file)
{
    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(image.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                     QString());
    item->setData(Qt::UserRole, image);
    item->setToolTip(file);
    addHistoryItem(item, {QDir(comfyRootEdit_->text()).filePath(file)}, id == currentId_);
    if (id == currentId_) {
        hideVideo();
        view_->setImage(image);
        saveImageBtn_->setEnabled(true);
    }
    log(tr("image: %1 (%2 x %3)").arg(comfyFilePath(file)).arg(image.width()).arg(image.height()));
}

void MainWindow::recordRun(bool failed)
{
    vramTimer_.stop();
    VramLog::Record r;
    if (vramPeakGB_ <= 0 || !VramLog::sizeOf(workflow_, &r))
        return;
    r.peakGB = vramPeakGB_;
    r.totalGB = vramTotalGB_;
    r.seconds = runClock_.elapsed() / 1000.0;
    r.failed = failed;
    VramLog::add(r);
    log(tr("peak GPU memory: %1 of %2 GB (%3 x %4 x %5)").arg(r.peakGB, 0, 'f', 1).arg(r.totalGB, 0, 'f', 1).arg(r.width).arg(r.height).arg(r.frames));
}

void MainWindow::showSizeNote()
{
    if (!VramLog::sizeOf(workflow_, nullptr))
        return;
    auto show = [this](double total, double used) {
        const QString text = VramLog::describe(workflow_, total, used);
        if (text.isEmpty() || !toast_)
            return;
        toast_->setText(text);
        toast_->adjustSize();
        toast_->move(12, 12);
        toast_->show();
        toast_->raise();
        toastTimer_.start(9000);
    };
    show(vramTotalGB_, -1);   // at once, then with the card's memory as it is now
    if (client_.isReady())
        client_.gpuMemory([this, show](bool ok, double total, double used) {
            if (!ok)
                return;
            vramTotalGB_ = total;
            show(total, used);
        });
}

QString MainWindow::comfyFilePath(const QString& file) const
{
    // "output/x.png" is under the ComfyUI folder when that ComfyUI is on this PC; else the file is only on the server
    const QString local = QDir(comfyRootEdit_->text()).filePath(file);
    if (QFileInfo(local).isFile())
        return QDir::toNativeSeparators(QFileInfo(local).absoluteFilePath());
    return tr("%1 (on the ComfyUI server %2)").arg(file, client_.server());
}

void MainWindow::addVideo(const QString& id, const QList<QImage>& frames, const QStringList& files)
{
    const double fps = videoIds_.take(id);
    // kept in ComfyLive's own folder, so the history can play it again and "Save video..." can copy it
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/video");
    QDir().mkpath(dir);
    QString base = QFileInfo(files.value(0)).completeBaseName();   // ComfyLive_video_00012_
    while (base.endsWith(QLatin1Char('_')))
        base.chop(1);
    const QString mp4 = QDir(dir).filePath(base + QStringLiteral(".mp4"));
    const QString framesDir = VideoTools::framesDirFor(mp4);
    const QString shown = tr("%1: %2 frames, %3 x %4, %5 s").arg(base).arg(frames.size()).arg(frames.first().width())
                              .arg(frames.first().height()).arg(frames.size() / fps, 0, 'f', 1);
    QString why;
    if (!VideoTools::writeFrames(frames, framesDir, &why)) {
        log(why);
        return;
    }
    log(tr("video: %1").arg(shown));
    log(tr("frames: %1").arg(QDir::toNativeSeparators(framesDir)));
    if (QFileInfo(QDir(comfyRootEdit_->text()).filePath(files.value(0))).isFile())
        log(tr("ComfyUI's own frames: %1 ... (%2 files)").arg(comfyFilePath(files.value(0))).arg(files.size()));
    const QJsonObject sent = client_.sentGraph(id);
    const QJsonObject extra{{QStringLiteral("comfyFile"), files.value(0)}, {QStringLiteral("frames"), QFileInfo(framesDir).fileName()},
                            {QStringLiteral("frameCount"), int(frames.size())}, {QStringLiteral("fps"), fps},
                            {QStringLiteral("width"), frames.first().width()}, {QStringLiteral("height"), frames.first().height()}};
    if (!sent.isEmpty() && Workflow::writeSettings(mp4, sent, workflow_.name(), extra, &why))
        log(tr("settings: %1").arg(QDir::toNativeSeparators(Workflow::settingsPathFor(mp4))));
    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(frames.first().scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                     QStringLiteral("\u25B6 ") + base.right(12));
    item->setData(Qt::UserRole + 2, mp4);
    item->setData(Qt::UserRole + 3, fps);
    item->setToolTip(shown);
    QStringList owned{mp4, Workflow::settingsPathFor(mp4), framesDir};
    for (const QString& f : files)   // ComfyUI's own frames
        owned << QDir(comfyRootEdit_->text()).filePath(f);
    addHistoryItem(item, owned, id == currentId_);
    if (id == currentId_)
        showVideo(mp4, frames, fps, shown);
    COMFYLIVE_SETTINGS(s);
    VideoTools::encode(SongTools::findFfmpeg(s.value(QStringLiteral("ffmpeg")).toString()), framesDir, fps, mp4,
                       [this](bool ok, const QString& message) {
                           log(ok ? tr("video file: %1").arg(QDir::toNativeSeparators(message)) : tr("no MP4 (the frames are kept): %1").arg(message));
                       });
}

void MainWindow::showVideo(const QString& mp4Path, const QList<QImage>& frames, double fps, const QString& shown)
{
    if (player_)
        player_->stop();
    audioRow_->hide();
    audioRow_->setProperty("path", QString());
    frames_ = frames;
    framesFps_ = fps > 0 ? fps : 24.0;
    videoPath_ = mp4Path;
    videoShown_ = shown;
    videoRow_->setVisible(!frames_.isEmpty());
    if (frames_.isEmpty())
        return;
    saveImageBtn_->setEnabled(true);   // the frame on screen
    view_->setCaption(shown);
    showFrame(0);
    frameTimer_.start(qMax(1, int(1000.0 / framesFps_ + 0.5)));
    videoPlayBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    videoPlayBtn_->setText(tr("Pause"));
}

void MainWindow::hideVideo()
{
    frameTimer_.stop();
    frames_.clear();
    videoPath_.clear();
    videoShown_.clear();
    if (videoRow_)
        videoRow_->hide();
}

void MainWindow::showFrame(int index)
{
    if (index < 0 || index >= frames_.size())
        return;
    frameIndex_ = index;
    view_->setImage(frames_[index]);
    videoLabel_->setText(tr("frame %1 / %2   %3 s").arg(index + 1).arg(frames_.size()).arg(index / framesFps_, 0, 'f', 2));
}

void MainWindow::onSaveVideo()
{
    if (videoPath_.isEmpty())
        return;
    COMFYLIVE_SETTINGS(s);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the video"),
                                                      QDir(s.value(QStringLiteral("lastVideoDir")).toString()).filePath(QFileInfo(videoPath_).fileName()),
                                                      tr("Video (*.mp4)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("lastVideoDir"), QFileInfo(path).absolutePath());
    // the frames (what the other tools read) in <name>_frames, the MP4 when ffmpeg made one, and the settings
    QString why;
    const QString framesDir = VideoTools::framesDirFor(path);
    if (!VideoTools::writeFrames(frames_, framesDir, &why)) {
        QMessageBox::warning(this, tr("ComfyLive"), why);
        return;
    }
    log(tr("saved ") + QDir::toNativeSeparators(framesDir));
    if (QFileInfo(videoPath_).isFile()) {
        QFile::remove(path);
        if (QFile::copy(videoPath_, path))
            log(tr("saved ") + QDir::toNativeSeparators(path));
        else
            QMessageBox::warning(this, tr("ComfyLive"), tr("Could not write %1").arg(path));
    } else {
        log(tr("no MP4 was made (ffmpeg): only the frames were saved"));
    }
    if (Workflow::copySettings(videoPath_, path, {{QStringLiteral("frames"), QFileInfo(framesDir).fileName()}}))
        log(tr("saved ") + QDir::toNativeSeparators(Workflow::settingsPathFor(path)));
}

MainWindow::~MainWindow()
{
    // Nothing may reach the window while it is torn down: the child widgets go in creation order (the Play button
    // before the player, the log before ComfyUI's last lines), and a signal into a deleted widget crashes.
    if (player_) {
        player_->disconnect(this);
        player_->blockSignals(true);
        player_->close();
    }
    client_.disconnect(this);
    server_.disconnect(this);
    speechServer_.disconnect(this);
    liveTimer_.stop();
    frameTimer_.stop();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("ComfyLive: ComfyUI in real time"));
    auto* split = new QSplitter(this);

    // ---- left: server, graph, parameters, run ----
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);

    auto* langRow = new QHBoxLayout;
    langRow->addStretch(1);
    langRow->addWidget(new QLabel(tr("Language"), left));
    languageCombo_ = new QComboBox(left);
    for (const QString& code : Language::codes())
        languageCombo_->addItem(Language::displayName(code), code);
    languageCombo_->setCurrentIndex(qMax(0, languageCombo_->findData(Language::current())));
    langRow->addWidget(languageCombo_);
    lv->addLayout(langRow);
    connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const QString code = languageCombo_->currentData().toString();
        if (code == Language::current())
            return;
        COMFYLIVE_SETTINGS(s);
        s.setValue(QStringLiteral("language"), code);
        Language::apply(code);
        QTimer::singleShot(0, this, &MainWindow::rebuildUi);   // not inside the combo's own signal: it goes too
    });

    auto* serverBox = new QGroupBox(tr("ComfyUI"), left);
    auto* sf = new QFormLayout(serverBox);
    serverEdit_ = new QLineEdit(QStringLiteral("127.0.0.1:8188"), serverBox);
    comfyRootEdit_ = new QLineEdit(QDir::cleanPath(QStringLiteral(COMFYLIVE_DEFAULT_COMFY_ROOT)), serverBox);
    comfyRootEdit_->setToolTip(tr("The local ComfyUI folder (main.py and its .venv), for Start"));
    customNodesCheck_ = new QCheckBox(tr("Load custom nodes (slower start; needed by graphs that use them)"), serverBox);
    customNodesCheck_->setChecked(true);
    stopServerCheck_ = new QCheckBox(tr("Stop the ComfyUI I started when closing"), serverBox);
    stopServerCheck_->setToolTip(tr("Off (the default): ComfyUI keeps running after ComfyLive closes, so the next start is "
                                    "instant. Stop it with Stop ComfyUI, or ComfyLive.exe --comfy-stop"));
    stopServerCheck_->setChecked(false);
    auto* buttons = new QHBoxLayout;
    connectBtn_ = new QPushButton(tr("Connect"), serverBox);
    startServerBtn_ = new QPushButton(tr("Start local ComfyUI"), serverBox);
    auto* stopComfyBtn = new QPushButton(tr("Stop ComfyUI"), serverBox);
    stopComfyBtn->setToolTip(tr("Stop the ComfyUIs on this PC (Server and Speech server), whoever started them"));
    buttons->addWidget(connectBtn_);
    buttons->addWidget(startServerBtn_);
    buttons->addWidget(stopComfyBtn);
    connect(stopComfyBtn, &QPushButton::clicked, this, &MainWindow::onStopServerClicked);
    statusLabel_ = new QLabel(tr("Not connected"), serverBox);
    speechServerEdit_ = new QLineEdit(QStringLiteral("127.0.0.1:8189"), serverBox);
    speechServerEdit_->setToolTip(tr("The speech ComfyUI (its own .venv-speech, Qwen3-TTS only): speech graphs run here, "
                                     "every other graph on Server"));
    sf->addRow(tr("Server"), serverEdit_);
    sf->addRow(tr("Speech server"), speechServerEdit_);
    sf->addRow(tr("ComfyUI folder"), comfyRootEdit_);
    sf->addRow(customNodesCheck_);
    sf->addRow(stopServerCheck_);
    sf->addRow(buttons);
    sf->addRow(statusLabel_);
    lv->addWidget(serverBox);

    auto* wfRow = new QHBoxLayout;
    workflowLabel_ = new QLabel(left);
    auto* openBtn = new QPushButton(tr("Open graph..."), left);
    openBtn->setToolTip(tr("A ComfyUI graph saved with Workflow > Export (API)"));
    auto* saveBtn = new QPushButton(tr("Save graph..."), left);
    auto* builtInBtn = new QPushButton(tr("Built-in"), left);
    builtInBtn->setToolTip(tr("A built-in graph: SDXL text to image, ACE-Step 1.5 text to music, Stable Audio Open text to sound effect, Qwen3-TTS text to speech, or Wan 2.2 / HyperFlow H3 Flash text to video"));
    auto* builtInMenu = new QMenu(builtInBtn);
    builtInMenu->addAction(tr("Text to image (SDXL)"), this, [this] { setWorkflow(Workflow::textToImage()); });
    builtInMenu->addAction(tr("Text to music (ACE-Step 1.5)"), this, [this] { setWorkflow(Workflow::textToMusic()); });
    builtInMenu->addAction(tr("Text to sound effect (Stable Audio Open)"), this, [this] { setWorkflow(Workflow::textToSoundEffect()); });
    builtInMenu->addAction(tr("Text to speech, preset voice (Qwen3-TTS)"), this, [this] { setWorkflow(Workflow::textToSpeech()); });
    builtInMenu->addAction(tr("Text to speech, voice from a description (Qwen3-TTS)"), this, [this] { setWorkflow(Workflow::textToSpeechDesigned()); });
    builtInMenu->addAction(tr("Text to video (Wan 2.2 TI2V-5B)"), this, [this] { setWorkflow(Workflow::textToVideo()); });
    builtInMenu->addAction(tr("Text to video (HyperFlow H3 Flash)"), this, [this] { setWorkflow(Workflow::textToVideoHyperFlow()); });
    builtInMenu->addAction(tr("Text to image (Qwen Image 2.1 Turbo)"), this, [this] { setWorkflow(Workflow::qwenTextToImage()); });
    builtInMenu->addAction(tr("Edit pictures (Qwen Image 2.1 Turbo)"), this, [this] { setWorkflow(Workflow::qwenEditImages()); });
    builtInMenu->addAction(tr("Text to image (Qwen Image 2.1 Turbo Q6_K)"), this, [this] { setWorkflow(Workflow::qwenGgufTextToImage()); });
    builtInMenu->addAction(tr("Edit pictures (Qwen Image 2.1 Turbo Q6_K)"), this, [this] { setWorkflow(Workflow::qwenGgufEditImages()); });
    builtInMenu->addAction(tr("Text to image (Qwen Image 2.1 Q4_K_M, 25 steps)"), this, [this] { setWorkflow(Workflow::qwenFullTextToImage()); });
    builtInMenu->addAction(tr("Edit pictures (Qwen Image 2.1 Q4_K_M, 25 steps)"), this, [this] { setWorkflow(Workflow::qwenFullEditImages()); });
    builtInMenu->addAction(tr("First frame to video (Wan 2.2 TI2V-5B)"), this, [this] { setWorkflow(Workflow::imageToVideo()); });
    builtInMenu->addAction(tr("First frame to video (HyperFlow H3 Flash)"), this, [this] { setWorkflow(Workflow::imageToVideoHyperFlow()); });
    builtInMenu->addAction(tr("Reference pictures to video (HyperFlow H3 Flash)"), this, [this] { setWorkflow(Workflow::referenceToVideoHyperFlow()); });
    builtInBtn->setMenu(builtInMenu);
    auto* helpBtn = new QPushButton(QStringLiteral("?"), left);
    helpBtn->setFixedWidth(28);
    helpBtn->setToolTip(tr("What each parameter of this graph does to the result"));
    connect(helpBtn, &QPushButton::clicked, this, &MainWindow::showParamHelp);
    wfRow->addWidget(workflowLabel_, 1);
    wfRow->addWidget(openBtn);
    wfRow->addWidget(saveBtn);
    wfRow->addWidget(builtInBtn);
    wfRow->addWidget(helpBtn);
    lv->addLayout(wfRow);

    paramsArea_ = new QScrollArea(left);
    paramsArea_->setWidgetResizable(true);
    paramsArea_->setWidget(new QLabel(tr("Connect to see the parameters."), paramsArea_));
    lv->addWidget(paramsArea_, 1);

    auto* runBox = new QGroupBox(tr("Run"), left);
    auto* rv = new QVBoxLayout(runBox);
    auto* opts = new QHBoxLayout;
    liveCheck_ = new QCheckBox(tr("Live"), runBox);
    liveCheck_->setToolTip(tr("Generate again by itself after every change (a run that is out of date is stopped)"));
    liveDelaySpin_ = new QSpinBox(runBox);
    liveDelaySpin_->setRange(100, 5000);
    liveDelaySpin_->setSingleStep(100);
    liveDelaySpin_->setValue(600);
    liveDelaySpin_->setSuffix(tr(" ms after a change"));
    randomSeedCheck_ = new QCheckBox(tr("New seed each run"), runBox);
    opts->addWidget(liveCheck_);
    opts->addWidget(liveDelaySpin_);
    opts->addWidget(randomSeedCheck_);
    opts->addStretch();
    auto* runRow = new QHBoxLayout;
    generateBtn_ = new QPushButton(tr("Generate  (Ctrl+Enter)"), runBox);
    generateBtn_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    generateBtn_->setEnabled(false);
    stopBtn_ = new QPushButton(tr("Stop"), runBox);
    stopBtn_->setEnabled(false);
    saveImageBtn_ = new QPushButton(tr("Save picture..."), runBox);
    saveImageBtn_->setEnabled(false);
    runRow->addWidget(generateBtn_, 1);
    runRow->addWidget(stopBtn_);
    runRow->addWidget(saveImageBtn_);
    deleteBtn_ = new QPushButton(tr("Delete result..."), runBox);
    deleteBtn_->setToolTip(tr("Move the files of the result picked in the list (the one on screen) to the Recycle Bin: its picture, or its "
                              "frames, MP4 and settings, or its sound, and ComfyUI's own copy in its output folder"));
    deleteBtn_->setEnabled(false);
    runRow->addWidget(deleteBtn_);
    connect(deleteBtn_, &QPushButton::clicked, this, [this] { deleteCurrentResult(true); });
    progress_ = new QProgressBar(runBox);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    rv->addLayout(opts);
    rv->addLayout(runRow);
    rv->addWidget(progress_);
    lv->addWidget(runBox);

    // ---- right: picture, history, log ----
    auto* right = new QWidget(split);
    auto* rvl = new QVBoxLayout(right);
    view_ = new ImageView(right);
    toast_ = new QLabel(view_);
    toast_->setStyleSheet(QStringLiteral("QLabel { background: rgba(20, 24, 32, 225); color: #f0f0f0; border: 1px solid #5a8fd0; "
                                         "border-radius: 6px; padding: 8px 10px; }"));
    toast_->setTextFormat(Qt::PlainText);
    toast_->hide();
    history_ = new QListWidget(right);
    history_->setViewMode(QListView::IconMode);
    history_->setIconSize(QSize(96, 96));
    history_->setFlow(QListView::LeftToRight);
    history_->setWrapping(false);
    history_->setFixedHeight(122);
    history_->setToolTip(tr("The last results: click one to show it"));
    log_ = new QPlainTextEdit(right);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setFixedHeight(130);
    audioRow_ = new QWidget(right);
    auto* arl = new QHBoxLayout(audioRow_);
    arl->setContentsMargins(0, 0, 0, 0);
    playBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("Play"), audioRow_);
    audioLabel_ = new QLabel(audioRow_);
    openAudioBtn_ = new QPushButton(tr("Open in player"), audioRow_);
    openAudioBtn_->setToolTip(tr("Open the audio in Windows' own player (for a format ComfyLive cannot play)"));
    saveAudioBtn_ = new QPushButton(tr("Save audio..."), audioRow_);
    arl->addWidget(playBtn_);
    arl->addWidget(audioLabel_, 1);
    transposeSpin_ = new QSpinBox(audioRow_);
    transposeSpin_->setRange(-6, 6);
    transposeSpin_->setValue(2);
    transposeSpin_->setSuffix(tr(" semitones"));
    transposeSpin_->setToolTip(tr("Up (+) or down (-). Up to 3 sounds natural; more sounds thin (up) or dull (down)"));
    transposeBtn_ = new QPushButton(tr("Transpose"), audioRow_);
    transposeBtn_->setToolTip(tr("The same song, higher or lower: a new file next to it (ffmpeg)"));
    arl->addWidget(transposeSpin_);
    arl->addWidget(transposeBtn_);
    arl->addWidget(openAudioBtn_);
    arl->addWidget(saveAudioBtn_);
    audioRow_->hide();
    player_ = new AudioPlayer(this);
    videoRow_ = new QWidget(right);
    auto* vrl = new QHBoxLayout(videoRow_);
    vrl->setContentsMargins(0, 0, 0, 0);
    videoPlayBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), tr("Pause"), videoRow_);
    auto* prevFrameBtn = new QPushButton(QStringLiteral("\u25C0"), videoRow_);
    auto* nextFrameBtn = new QPushButton(QStringLiteral("\u25B6"), videoRow_);
    prevFrameBtn->setFixedWidth(32);
    nextFrameBtn->setFixedWidth(32);
    prevFrameBtn->setToolTip(tr("One frame back"));
    nextFrameBtn->setToolTip(tr("One frame on"));
    videoLabel_ = new QLabel(videoRow_);
    openFramesBtn_ = new QPushButton(tr("Open frames folder"), videoRow_);
    openFramesBtn_->setToolTip(tr("The video's frames as PNG files (frame_0001.png ...), and its MP4 beside the folder"));
    saveVideoBtn_ = new QPushButton(tr("Save video..."), videoRow_);
    saveVideoBtn_->setToolTip(tr("The MP4, its frames in <name>_frames and its settings .json, wherever you want"));
    vrl->addWidget(videoPlayBtn_);
    vrl->addWidget(prevFrameBtn);
    vrl->addWidget(nextFrameBtn);
    vrl->addWidget(videoLabel_, 1);
    vrl->addWidget(openFramesBtn_);
    vrl->addWidget(saveVideoBtn_);
    videoRow_->hide();
    connect(videoPlayBtn_, &QPushButton::clicked, this, [this] {
        const bool play = !frameTimer_.isActive();
        if (play)
            frameTimer_.start(qMax(1, int(1000.0 / framesFps_ + 0.5)));
        else
            frameTimer_.stop();
        videoPlayBtn_->setIcon(style()->standardIcon(play ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
        videoPlayBtn_->setText(play ? tr("Pause") : tr("Play"));
    });
    for (auto [button, step] : {std::pair{prevFrameBtn, -1}, std::pair{nextFrameBtn, 1}})
        connect(button, &QPushButton::clicked, this, [this, step] {
            if (frames_.isEmpty())
                return;
            frameTimer_.stop();
            videoPlayBtn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
            videoPlayBtn_->setText(tr("Play"));
            showFrame(int((frameIndex_ + step + frames_.size()) % frames_.size()));
        });
    connect(openFramesBtn_, &QPushButton::clicked, this, [this] {
        if (!videoPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(VideoTools::framesDirFor(videoPath_)));
    });
    connect(saveVideoBtn_, &QPushButton::clicked, this, &MainWindow::onSaveVideo);
    rvl->addWidget(view_, 1);
    rvl->addWidget(audioRow_);
    rvl->addWidget(videoRow_);
    rvl->addWidget(history_);
    rvl->addWidget(log_);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({460, 820});
    setCentralWidget(split);
    resize(1300, 860);

    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(startServerBtn_, &QPushButton::clicked, this, &MainWindow::onStartServerClicked);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::onOpenWorkflow);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveWorkflow);
    connect(generateBtn_, &QPushButton::clicked, this, &MainWindow::generate);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::stopRun);
    connect(saveImageBtn_, &QPushButton::clicked, this, &MainWindow::onSaveImage);
    connect(liveCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && client_.isReady())
            liveTimer_.start(liveDelaySpin_->value());
    });
    connect(playBtn_, &QPushButton::clicked, this, [this] {
        if (player_->isPlaying())
            player_->stop();
        else
            player_->play();
    });
    connect(player_, &AudioPlayer::stateChanged, this, [this](bool playing) {
        playBtn_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
        playBtn_->setText(playing ? tr("Stop") : tr("Play"));
    });
    connect(player_, &AudioPlayer::position, this, [this](int ms, int length) {
        auto mmss = [](int t) { return QStringLiteral("%1:%2").arg(t / 60000).arg((t / 1000) % 60, 2, 10, QLatin1Char('0')); };
        audioLabel_->setText(QStringLiteral("%1   %2 / %3").arg(QFileInfo(player_->path()).fileName(), mmss(ms), mmss(length)));
    });
    connect(openAudioBtn_, &QPushButton::clicked, this, [this] {
        const QString path = audioRow_->property("path").toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(saveAudioBtn_, &QPushButton::clicked, this, &MainWindow::onSaveAudio);
    connect(transposeBtn_, &QPushButton::clicked, this, &MainWindow::onTranspose);
    connect(history_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* now) { deleteBtn_->setEnabled(now != nullptr); });
    connect(history_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString audio = item->data(Qt::UserRole + 1).toString();
        if (!audio.isEmpty()) {
            showAudio(audio, item->toolTip(), true);
            return;
        }
        const QString video = item->data(Qt::UserRole + 2).toString();
        if (!video.isEmpty()) {
            const QList<QImage> frames = VideoTools::readFrames(VideoTools::framesDirFor(video));
            if (frames.isEmpty())
                log(tr("no frames in %1").arg(QDir::toNativeSeparators(VideoTools::framesDirFor(video))));
            else
                showVideo(video, frames, item->data(Qt::UserRole + 3).toDouble(), item->toolTip());
            return;
        }
        hideVideo();
        view_->setImage(item->data(Qt::UserRole).value<QImage>());
        view_->setCaption(item->toolTip());
        saveImageBtn_->setEnabled(true);
    });
}

void MainWindow::buildParams()
{
    fields_.clear();
    auto* host = new QWidget;
    auto* v = new QVBoxLayout(host);
    QGroupBox* box = nullptr;
    QFormLayout* form = nullptr;
    QString lastGroup;
    for (const ParamDef& p : workflow_.params(client_.objectInfo())) {
        if (p.group != lastGroup || !box) {
            lastGroup = p.group;
            const qsizetype cut = p.group.indexOf(QStringLiteral("  ("));
            const QString title = cut < 0 ? p.group : p.group.left(cut);
            const QString shown = QCoreApplication::translate("Graph", title.toUtf8().constData()) + (cut < 0 ? QString() : p.group.mid(cut));
            box = new QGroupBox(shown, host);
            form = new QFormLayout(box);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            v->addWidget(box);
        }
        const QString nodeId = p.nodeId, input = p.input;
        if (p.classType.startsWith(QStringLiteral("InspyrenetRembg")) && nodeId == QStringLiteral("30") && workflow_.hasRemoveBackground() &&
            input == QStringLiteral("torchscript_jit")) {
            auto* check = new QCheckBox(tr("on: the frames are PNG files with only the character (transparent background)"), box);
            check->setChecked(workflow_.removeBackground());
            check->setToolTip(tr("Off: the frames keep the video's background. The MP4 always has a grey background."));
            connect(check, &QCheckBox::toggled, this, [this](bool on) {
                workflow_.setRemoveBackground(on);
                onParamChanged();
            });
            form->addRow(check);
        }
        const bool aceSong = workflow_.graph().value(nodeId).toObject().value(QStringLiteral("class_type")).toString()
                                 .startsWith(QStringLiteral("TextEncodeAceStepAudio"));
        if (aceSong && input == QStringLiteral("tags")) {
            // the singer and the voice: words in the tags, put first; a choice replaces the one before
            const QString tagsKey = nodeId + QStringLiteral("/tags"), lyricsKey = nodeId + QStringLiteral("/lyrics");
            auto addList = [&](const QString& label, const QList<SongTools::Choice>& list, bool singer) {
                auto* combo = new QComboBox(box);
                for (const SongTools::Choice& c : list)
                    combo->addItem(QCoreApplication::translate("Song", c.label), c.id);
                combo->setCurrentIndex(qMax(0, combo->findData(SongTools::detectChoice(p.value.toString(), list))));
                const QList<SongTools::Choice>* choices = &list;   // a static list: lives as long as the program
                connect(combo, &QComboBox::activated, this, [this, combo, choices, singer, tagsKey, lyricsKey] {
                    const QString id = combo->currentData().toString();
                    const QString tags = workflow_.value(tagsKey.section(QLatin1Char('/'), 0, 0), QStringLiteral("tags")).toString();
                    setTextField(tagsKey, SongTools::applyChoice(tags, *choices, id));
                    if (!singer)
                        return;
                    const QString lyrics = workflow_.value(lyricsKey.section(QLatin1Char('/'), 0, 0), QStringLiteral("lyrics")).toString();
                    if (id == QStringLiteral("instrumental") && lyrics.trimmed() != QStringLiteral("[Instrumental]")) {
                        instrumentalBackup_ = lyrics;   // given back when a singer is picked again
                        setTextField(lyricsKey, QStringLiteral("[Instrumental]"));
                    } else if (id != QStringLiteral("instrumental") && lyrics.trimmed() == QStringLiteral("[Instrumental]") &&
                               !instrumentalBackup_.isEmpty()) {
                        setTextField(lyricsKey, instrumentalBackup_);
                    }
                });
                form->addRow(label, combo);
            };
            addList(tr("Singer"), SongTools::singers(), true);
            addList(tr("Voice"), SongTools::voices(), false);
        }
        QWidget* editor = nullptr;
        switch (p.kind) {
        case ParamDef::Kind::Int:
        case ParamDef::Kind::Float: {
            auto* spin = new QDoubleSpinBox(box);
            spin->setDecimals(p.kind == ParamDef::Kind::Int ? 0 : p.decimals);
            spin->setRange(p.minimum, p.maximum);
            spin->setSingleStep(p.step);
            spin->setValue(p.value.toDouble());
            spin->setKeyboardTracking(false);   // a change when the typing is done, not at every digit
            const bool isInt = p.kind == ParamDef::Kind::Int;
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, nodeId, input, isInt](double d) {
                workflow_.setValue(nodeId, input, isInt ? QJsonValue(qint64(d)) : QJsonValue(d));
                syncNumberFields();
                if (input == QStringLiteral("width") || input == QStringLiteral("height") || input == QStringLiteral("length") ||
                    input == QStringLiteral("batch_size"))
                    showSizeNote();
                onParamChanged();
            });
            editor = spin;
            break;
        }
        case ParamDef::Kind::Line: {
            auto* line = new QLineEdit(p.value.toString(), box);
            connect(line, &QLineEdit::textEdited, this, [this, nodeId, input](const QString& t) {
                workflow_.setValue(nodeId, input, t);
                onParamChanged();
            });
            editor = line;
            break;
        }
        case ParamDef::Kind::Text: {
            auto* text = new QPlainTextEdit(p.value.toString(), box);
            text->setFixedHeight(76);
            connect(text, &QPlainTextEdit::textChanged, this, [this, text, nodeId, input] {
                workflow_.setValue(nodeId, input, text->toPlainText());
                onParamChanged();
            });
            editor = text;
            break;
        }
        case ParamDef::Kind::Bool: {
            auto* check = new QCheckBox(box);
            check->setChecked(p.value.toBool());
            connect(check, &QCheckBox::toggled, this, [this, nodeId, input](bool on) {
                workflow_.setValue(nodeId, input, on);
                onParamChanged();
            });
            editor = check;
            break;
        }
        case ParamDef::Kind::Combo: {
            if (aceSong && input == QStringLiteral("language")) {   // full names shown, the code kept in the graph
                auto* combo = new QComboBox(box);
                for (const QString& code : p.choices) {
                    combo->addItem(SongTools::languageName(code), code);
                    combo->setItemData(combo->count() - 1, code, Qt::ToolTipRole);
                }
                const QString now = p.value.toString();
                if (combo->findData(now) < 0)
                    combo->addItem(SongTools::languageName(now), now);
                combo->setCurrentIndex(combo->findData(now));
                connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, nodeId, input] {
                    workflow_.setValue(nodeId, input, combo->currentData().toString());
                    onParamChanged();
                });
                editor = combo;
                break;
            }
            auto* combo = new QComboBox(box);
            combo->addItems(p.choices);
            const QString now = p.value.isString() ? p.value.toString() : QString::number(p.value.toDouble());
            if (!p.choices.contains(now))
                combo->addItem(now);
            combo->setCurrentText(now);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(12);
            connect(combo, &QComboBox::currentTextChanged, this, [this, nodeId, input](const QString& t) {
                workflow_.setValue(nodeId, input, t);
                onParamChanged();
            });
            editor = combo;
            break;
        }
        }
        fields_.insert(keyOf(p), editor);
        const QString help = ParamHelp::describe(p.classType, p.input, p.tooltip);
        QWidget* label = nullptr;
        if (!help.isEmpty()) {
            auto* l = new QLabel(p.input + QStringLiteral(" \u24D8"), box);   // circled i: there is help here
            l->setToolTip(QStringLiteral("<p style='white-space:pre-wrap'>%1</p>").arg(help.toHtmlEscaped()));
            l->setCursor(Qt::WhatsThisCursor);
            editor->setToolTip(l->toolTip());
            label = l;
        }
        if (aceSong && input == QStringLiteral("keyscale") && p.kind == ParamDef::Kind::Combo) {   // the key, a semitone at a time
            auto* row = new QWidget(box);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* combo = static_cast<QComboBox*>(editor);
            const QStringList choices = p.choices;
            auto* down = new QPushButton(QStringLiteral("\u25BC"), row);
            auto* up = new QPushButton(QStringLiteral("\u25B2"), row);
            down->setToolTip(tr("Key one semitone down (a new song in that key)"));
            up->setToolTip(tr("Key one semitone up (a new song in that key)"));
            down->setFixedWidth(32);
            up->setFixedWidth(32);
            for (auto [button, step] : {std::pair{down, -1}, std::pair{up, 1}})
                connect(button, &QPushButton::clicked, combo, [combo, choices, step] {
                    const QString next = SongTools::stepKey(combo->currentText(), step, choices);
                    if (!next.isEmpty())
                        combo->setCurrentText(next);
                });
            h->addWidget(editor, 1);
            h->addWidget(down);
            h->addWidget(up);
            if (label)
                form->addRow(label, row);
            else
                form->addRow(p.input, row);
            continue;
        }
        if (workflow_.isLocalPicture(nodeId) && input == QStringLiteral("image") && p.kind == ParamDef::Kind::Line) {
            // the reference picture: a file on this PC, with Browse; the video takes the picture's shape
            auto* row = new QWidget(box);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* line = static_cast<QLineEdit*>(editor);
            line->disconnect(this);   // not at every key: when the path is whole
            const bool firstFrame = workflow_.hasStartImage();
            line->setPlaceholderText(firstFrame ? tr("a picture of the character: whole body, plain background") : tr("a picture file on this PC"));
            auto apply = [this, line, nodeId, firstFrame] {
                const QString path = QDir::fromNativeSeparators(line->text().trimmed());
                if (path == workflow_.value(nodeId, QStringLiteral("image")).toString())
                    return;
                if (firstFrame)
                    workflow_.setStartImage(path);   // the video's width and height follow the picture
                else
                    workflow_.setValue(nodeId, QStringLiteral("image"), path);
                syncNumberFields();
                if (firstFrame)
                    showSizeNote();
                const QImage picture(path);
                if (!picture.isNull()) {
                    hideVideo();
                    view_->setImage(picture);
                    audioRow_->hide();
                    view_->setCaption(tr("reference picture: %1 x %2").arg(picture.width()).arg(picture.height()));
                }
                onParamChanged();
            };
            connect(line, &QLineEdit::editingFinished, this, apply);
            auto* browse = new QPushButton(tr("Browse..."), row);
            connect(browse, &QPushButton::clicked, this, [this, line, apply] {
                COMFYLIVE_SETTINGS(s);
                const QString start = line->text().isEmpty() ? s.value(QStringLiteral("lastPictureDir")).toString() : line->text();
                const QString path = QFileDialog::getOpenFileName(this, tr("The reference picture"), start, tr("Pictures (*.png *.jpg *.jpeg *.webp *.bmp)"));
                if (path.isEmpty())
                    return;
                s.setValue(QStringLiteral("lastPictureDir"), QFileInfo(path).absolutePath());
                line->setText(QDir::toNativeSeparators(path));
                apply();
            });
            h->addWidget(editor, 1);
            h->addWidget(browse);
            form->addRow(tr("picture"), row);
            continue;
        }
        if (p.isSeed()) {   // a seed: a dice button beside it
            auto* row = new QWidget(box);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* dice = new QPushButton(tr("Random"), row);
            auto* spin = static_cast<QDoubleSpinBox*>(editor);
            connect(dice, &QPushButton::clicked, spin, [spin] { spin->setValue(double(QRandomGenerator::global()->bounded(Q_INT64_C(1) << 40))); });
            h->addWidget(editor, 1);
            h->addWidget(dice);
            if (label)
                form->addRow(label, row);
            else
                form->addRow(p.input, row);
        } else if (label) {
            form->addRow(label, editor);
        } else {
            form->addRow(p.input, editor);
        }
    }
    v->addStretch();
    paramsArea_->setWidget(host);
    showSizeNote();   // a graph was opened or picked: what its size costs
    workflowLabel_->setText(tr("Graph: <b>%1</b> (%2 nodes)").arg(workflow_.name().toHtmlEscaped()).arg(workflow_.graph().size()));
}

void MainWindow::onParamChanged()
{
    saveSettings();
    if (liveCheck_->isChecked() && client_.isReady())
        liveTimer_.start(liveDelaySpin_->value());
}

void MainWindow::setLanguage(const QString& code)
{
    languageCombo_->setCurrentIndex(qMax(0, languageCombo_->findData(code)));
}

void MainWindow::rebuildUi()
{
    // what the window shows now, which buildUi() starts again empty
    saveSettings();
    const QString logText = log_->toPlainText();
    QList<QListWidgetItem*> items;
    while (history_->count() > 0)
        items << history_->takeItem(0);
    const QImage image = view_->image();
    const QString caption = view_->caption();
    const QString audioPath = audioRow_->property("path").toString();
    const QString audioShown = audioRow_->property("shown").toString();
    const QString status = statusLabel_->text();
    const int progressMax = progress_->maximum(), progressValue = progress_->value();
    const bool canStop = stopBtn_->isEnabled(), canSave = saveImageBtn_->isEnabled(), canStart = startServerBtn_->isEnabled();
    const QByteArray geometry = saveGeometry();
    const QList<QImage> frames = frames_;
    const QString videoPath = videoPath_, videoShown = videoShown_;
    const double videoFps = framesFps_;
    frameTimer_.stop();
    player_->close();
    delete player_;
    player_ = nullptr;
    fields_.clear();

    buildUi();
    loadSettings();
    restoreGeometry(geometry);
    log_->setPlainText(logText);
    for (QListWidgetItem* item : items)
        history_->addItem(item);
    deleteBtn_->setEnabled(history_->currentItem() != nullptr);
    progress_->setRange(0, progressMax);
    progress_->setValue(progressValue);
    stopBtn_->setEnabled(canStop);
    saveImageBtn_->setEnabled(canSave);
    startServerBtn_->setEnabled(canStart);
    if (!videoPath.isEmpty() && !frames.isEmpty()) {
        showVideo(videoPath, frames, videoFps, videoShown);
    } else if (!audioPath.isEmpty()) {
        showAudio(audioPath, audioShown, false);
    } else {
        view_->setImage(image);
        view_->setCaption(caption);
    }
    if (client_.isReady()) {
        setStatus(tr("Connected to %1").arg(client_.server()));
        connectBtn_->setText(tr("Reconnect"));
        buildParams();
        generateBtn_->setEnabled(true);
    } else {
        setStatus(status);   // a message still on its way (starting, retrying) comes in the new language
    }
}

void MainWindow::syncNumberFields()
{
    for (const ParamDef& p : workflow_.params(client_.objectInfo())) {
        auto* spin = qobject_cast<QDoubleSpinBox*>(fields_.value(keyOf(p)));
        if (spin && spin->value() != p.value.toDouble()) {
            const QSignalBlocker block(spin);
            spin->setValue(p.value.toDouble());
        }
    }
}

void MainWindow::showAudio(const QString& path, const QString& shown, bool autoPlay)
{
    hideVideo();
    audioRow_->setProperty("path", path);
    audioRow_->setProperty("shown", shown);
    audioRow_->show();
    saveAudioBtn_->setEnabled(true);
    view_->setImage(QImage());
    view_->setCaption(QStringLiteral("\u266A ") + shown);
    QString why;
    if (!player_->load(path, &why)) {
        playBtn_->setEnabled(false);
        audioLabel_->setText(tr("%1: ComfyLive cannot play this format (%2); use Open in player").arg(QFileInfo(path).fileName(), why));
        return;
    }
    playBtn_->setEnabled(true);
    emit player_->position(0, player_->lengthMs());
    // only the window in front plays (never a hidden or off-screen one: --screenshot runs and the like stay silent)
    if (autoPlay && isActiveWindow() && QGuiApplication::platformName() != QStringLiteral("offscreen"))
        player_->play();
}

void MainWindow::showParamHelp()
{
    QString html = QStringLiteral("<h3>%1</h3>").arg(workflow_.name().toHtmlEscaped());
    if (!client_.isReady())
        html += QStringLiteral("<p>%1</p>").arg(tr("Connect to see the parameters.").toHtmlEscaped());
    QString lastGroup;
    for (const ParamDef& p : workflow_.params(client_.objectInfo())) {
        if (p.group != lastGroup) {
            lastGroup = p.group;
            const qsizetype cut = p.group.indexOf(QStringLiteral("  ("));
            const QString title = cut < 0 ? p.group : p.group.left(cut);
            html += QStringLiteral("<h4>%1 <span style='color:gray'>%2</span></h4>")
                        .arg(QCoreApplication::translate("Graph", title.toUtf8().constData()).toHtmlEscaped(), p.classType.toHtmlEscaped());
        }
        const QString help = ParamHelp::describe(p.classType, p.input, p.tooltip);
        html += QStringLiteral("<p><b>%1</b>: %2</p>").arg(p.input.toHtmlEscaped(),
                                                            help.isEmpty() ? QStringLiteral("<i>-</i>") : help.toHtmlEscaped());
    }
    QDialog d(this);
    d.setWindowTitle(tr("Parameters: what they change"));
    auto* v = new QVBoxLayout(&d);
    auto* text = new QTextBrowser(&d);
    text->setHtml(html);
    v->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(buttons);
    d.resize(640, 720);
    d.exec();
}

void MainWindow::setTextField(const QString& key, const QString& text)
{
    if (auto* edit = qobject_cast<QPlainTextEdit*>(fields_.value(key)))
        edit->setPlainText(text);   // its textChanged puts it in the graph
    else
        workflow_.setValue(key.section(QLatin1Char('/'), 0, 0), key.section(QLatin1Char('/'), 1), text);
}

void MainWindow::addAudioToHistory(const QString& path, const QString& shown)
{
    auto* item = new QListWidgetItem(style()->standardIcon(QStyle::SP_MediaVolume), QFileInfo(path).completeBaseName().right(14));
    item->setData(Qt::UserRole + 1, path);
    item->setToolTip(shown);
    const QString original = QDir(comfyRootEdit_->text()).filePath(shown);   // ComfyUI's own file ("output/audio/x.mp3")
    addHistoryItem(item, {path, Workflow::settingsPathFor(path), original, Workflow::settingsPathFor(original)}, true);
}

void MainWindow::addHistoryItem(QListWidgetItem* item, const QStringList& files, bool onScreen)
{
    item->setData(Qt::UserRole + 5, files);
    history_->insertItem(0, item);
    while (history_->count() > 40)
        delete history_->takeItem(history_->count() - 1);
    if (onScreen || !history_->currentItem())
        history_->setCurrentItem(item);
    if (deleteBtn_)
        deleteBtn_->setEnabled(history_->currentItem() != nullptr);
}

QStringList MainWindow::deleteCurrentResult(bool confirm)
{
    QListWidgetItem* item = history_->currentItem();
    if (!item)
        return {};
    // what is really there (a settings file or ComfyUI's copy may not be); never the same file twice
    QStringList files;
    for (const QString& f : item->data(Qt::UserRole + 5).toStringList()) {
        const QString path = QDir::toNativeSeparators(QFileInfo(f).absoluteFilePath());
        if (QFileInfo::exists(f) && !files.contains(path, Qt::CaseInsensitive))
            files << path;
    }
    if (confirm) {
        QString list = files.mid(0, 8).join(QLatin1Char('\n'));
        if (files.size() > 8)
            list += QLatin1Char('\n') + tr("... and %1 more").arg(files.size() - 8);
        const QString question = files.isEmpty() ? tr("Its files are no longer there. Take this result out of the list?")
                                                 : tr("Move this result's %1 files and folders to the Recycle Bin?\n\n%2").arg(files.size()).arg(list);
        if (QMessageBox::question(this, tr("Delete result"), question, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return {};
    }
    // the players let go of the files first
    const bool onScreen = item->data(Qt::UserRole + 2).toString() == videoPath_ && !videoPath_.isEmpty();
    const bool audioOnScreen = !item->data(Qt::UserRole + 1).toString().isEmpty() &&
                               item->data(Qt::UserRole + 1).toString() == audioRow_->property("path").toString();
    if (onScreen)
        hideVideo();
    if (audioOnScreen) {
        player_->close();
        audioRow_->hide();
        audioRow_->setProperty("path", QString());
    }
    QStringList moved;
    for (const QString& f : files) {
        if (QFile::moveToTrash(f)) {
            moved << f;
            log(tr("moved to the Recycle Bin: %1").arg(f));
        } else {
            log(tr("could not delete %1 (is it open in another program?)").arg(f));
        }
    }
    if (moved.size() == files.size()) {   // all gone: the result leaves the list and the view
        const bool wasPicture = item->data(Qt::UserRole + 1).toString().isEmpty() && item->data(Qt::UserRole + 2).toString().isEmpty();
        delete history_->takeItem(history_->row(item));
        if (onScreen || audioOnScreen || wasPicture) {
            view_->setImage(QImage());
            view_->setCaption(tr("deleted"));
            saveImageBtn_->setEnabled(false);
        }
    }
    deleteBtn_->setEnabled(history_->currentItem() != nullptr);
    return moved;
}

void MainWindow::onTranspose()
{
    const QString in = audioRow_->property("path").toString();
    const int semitones = transposeSpin_->value();
    if (in.isEmpty() || semitones == 0)
        return;
    COMFYLIVE_SETTINGS(s);
    const QString ffmpeg = SongTools::findFfmpeg(s.value(QStringLiteral("ffmpeg")).toString());
    const QString out = SongTools::transposedPath(in, semitones);
    if (player_->path() == out)
        player_->close();   // ffmpeg writes over it
    transposeBtn_->setEnabled(false);
    log(tr("transposing %1 by %2 semitones ...").arg(QFileInfo(in).fileName()).arg(semitones));
    SongTools::transpose(ffmpeg, in, semitones, out, [this, out](bool ok, const QString& message) {
        transposeBtn_->setEnabled(true);
        if (!ok) {
            log(tr("transpose failed: ") + message);
            QMessageBox::warning(this, tr("ComfyLive"), tr("Transpose failed:\n%1").arg(message));
            return;
        }
        addAudioToHistory(out, QFileInfo(out).fileName());
        log(tr("saved ") + QDir::toNativeSeparators(out));
        Workflow::copySettings(audioRow_->property("path").toString(), out,
                               {{QStringLiteral("transpose"), transposeSpin_->value()},
                                {QStringLiteral("transposedFrom"), QFileInfo(audioRow_->property("path").toString()).fileName()}});
        showAudio(out, QFileInfo(out).fileName(), true);
    });
}

void MainWindow::onSaveAudio()
{
    const QString from = audioRow_->property("path").toString();
    if (from.isEmpty())
        return;
    COMFYLIVE_SETTINGS(s);
    const QString suffix = QFileInfo(from).suffix();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the audio"),QDir(s.value(QStringLiteral("lastAudioDir")).toString()).filePath(QFileInfo(from).fileName()),
                                                      tr("Audio (*.%1)").arg(suffix));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("lastAudioDir"), QFileInfo(path).absolutePath());
    QFile::remove(path);
    if (QFile::copy(from, path)) {
        log(tr("saved ") + QDir::toNativeSeparators(path));
        if (Workflow::copySettings(from, path, {}))
            log(tr("saved ") + QDir::toNativeSeparators(Workflow::settingsPathFor(path)));
    }
    else
        QMessageBox::warning(this, tr("ComfyLive"), tr("Could not write %1").arg(path));
}

void MainWindow::randomizeSeeds()
{
    for (const ParamDef& p : workflow_.params(client_.objectInfo())) {
        if (!p.isSeed())
            continue;
        const qint64 seed = QRandomGenerator::global()->bounded(Q_INT64_C(1) << 40);
        workflow_.setValue(p.nodeId, p.input, seed);
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(fields_.value(keyOf(p)))) {
            const QSignalBlocker block(spin);
            spin->setValue(double(seed));
        }
    }
}

QString MainWindow::targetServer() const
{
    return (workflow_.needsSpeechServer() ? speechServerEdit_ : serverEdit_)->text().trimmed();
}

void MainWindow::generate()
{
    if (!client_.isReady() || client_.server() != targetServer()) {
        // the graph's own ComfyUI (the speech one, or the main one): connect, then generate
        generateWhenConnected_ = true;
        setStatus(tr("Connecting to %1 ...").arg(targetServer()));
        client_.connectToServer(targetServer());
        return;
    }
    if (randomSeedCheck_->isChecked())
        randomizeSeeds();
    const QStringList snapped = workflow_.snapToSteps(client_.objectInfo());
    for (const QString& line : snapped)
        log(line);
    if (!snapped.isEmpty()) {
        syncNumberFields();   // the form shows the value that is sent
        showSizeNote();
    }
    if (!pending_.isEmpty()) {   // what is running or waiting is out of date now
        client_.deleteQueued(pending_);
        client_.interrupt();
        pending_.clear();
    }
    currentId_.clear();
    nextIsVideo_ = workflow_.makesVideo();
    nextFps_ = workflow_.videoFps();
    saveSettings();
    // the other ComfyUI lets go of the GPU first (12 GB: not room for a picture or song model and the speech model)
    const QString other = (client_.server() == speechServerEdit_->text().trimmed() ? serverEdit_ : speechServerEdit_)->text().trimmed();
    const QJsonObject graph = workflow_.graphForRun();
    client_.freeMemory(other, [this, graph] { client_.queuePrompt(graph); });
}

void MainWindow::setSteps(int steps)
{
    for (const ParamDef& p : workflow_.params(client_.objectInfo()))
        if (p.input == QStringLiteral("steps"))
            workflow_.setValue(p.nodeId, p.input, steps);
    if (client_.isReady())
        buildParams();
}

void MainWindow::stopRun()
{
    liveTimer_.stop();
    client_.deleteQueued(pending_);
    client_.interrupt();
    pending_.clear();
    stopBtn_->setEnabled(false);
    view_->setCaption(tr("stopped"));
}

void MainWindow::startUp(bool startServer)
{
    setStatus(tr("Looking for ComfyUI at %1 ...").arg(targetServer()));
    client_.setServer(targetServer());
    client_.ping([this, startServer](bool up) {
        if (up) {
            client_.connectToServer(targetServer());
        } else if (startServer) {
            onStartServerClicked();
        } else {
            setStatus(tr("No ComfyUI at %1: press Start local ComfyUI, or start it yourself").arg(targetServer()));
            startServerBtn_->setEnabled(true);
        }
    });
}

void MainWindow::onConnectClicked()
{
    saveSettings();
    setStatus(tr("Connecting ..."));
    client_.connectToServer(targetServer());
}

void MainWindow::onStartServerClicked()
{
    saveSettings();
    const QUrl u(QStringLiteral("http://") + targetServer());   // the ComfyUI this graph runs on
    if (u.host() != QStringLiteral("127.0.0.1") && u.host() != QStringLiteral("localhost")) {
        QMessageBox::information(this, tr("ComfyLive"), tr("Start only works for a ComfyUI on this PC (127.0.0.1)."));
        return;
    }
    ComfyProcess& process = workflow_.needsSpeechServer() ? speechServer_ : server_;
    startServerBtn_->setEnabled(false);
    process.setKeepRunning(!stopServerCheck_->isChecked());   // kept: started detached, it outlives this window
    setStatus(tr("Starting ComfyUI: the first start can take a minute ..."));
    process.start(comfyRootEdit_->text(), quint16(u.port(8188)), customNodesCheck_->isChecked());
}

void MainWindow::onStopServerClicked()
{
    stopRun();
    client_.disconnectFromServer();
    stopLocalServers();
    startedServer_ = false;
    setStatus(tr("Not connected"));
    connectBtn_->setText(tr("Connect"));
    generateBtn_->setEnabled(false);
    startServerBtn_->setEnabled(true);
}

void MainWindow::stopLocalServers()
{
    // both: the main ComfyUI and the speech one (each only when it is on this PC)
    server_.stop();
    speechServer_.stop();
    for (QLineEdit* edit : {serverEdit_, speechServerEdit_}) {
        const QUrl u(QStringLiteral("http://") + edit->text().trimmed());
        if (u.host() != QStringLiteral("127.0.0.1") && u.host() != QStringLiteral("localhost"))
            continue;
        QString message;
        ComfyProcess::stopLocal(quint16(u.port(8188)), &message);
        log(message);
    }
}

void MainWindow::setWorkflow(const Workflow& w)
{
    stopRun();
    workflow_ = w;
    if (!client_.server().isEmpty() && client_.server() != targetServer()) {   // the graph runs on the other ComfyUI
        log(tr("this graph runs on %1").arg(targetServer()));
        workflowLabel_->setText(tr("Graph: <b>%1</b>").arg(workflow_.name().toHtmlEscaped()));
        client_.connectToServer(targetServer());   // its node list builds the form once connected
    } else if (client_.isReady()) {
        for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
            log(line);
        buildParams();
    } else {
        workflowLabel_->setText(tr("Graph: <b>%1</b>").arg(workflow_.name().toHtmlEscaped()));
    }
    saveSettings();
}

bool MainWindow::openGraphForCheck(const QString& path, QString* why)
{
    Workflow w;
    if (!w.load(path, why))
        return false;
    checkRun_ = true;
    randomSeedCheck_->setChecked(w.loadedFromRecipe() && w.recipeRandomSeed());
    setWorkflow(w);
    return true;
}

void MainWindow::onOpenWorkflow()
{
    COMFYLIVE_SETTINGS(s);
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a ComfyUI graph (API format)"), s.value(QStringLiteral("lastDir")).toString(),
                                                      tr("ComfyUI API graph, a recipe, or a song's, sound's or video's settings (*.json)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    Workflow w;
    QString why;
    if (!w.load(path, &why)) {
        QMessageBox::warning(this, tr("ComfyLive"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), why));
        return;
    }
    setWorkflow(w);
    log(tr("graph: ") + QDir::toNativeSeparators(path));
    if (w.loadedFromRecipe()) {
        if (!w.recipeRandomSeed())
            randomSeedCheck_->setChecked(false);   // the recipe's own seed
        saveSettings();
        log(tr("a recipe for the built-in graph: its values are in the form; Generate makes it"));
    }
    if (w.fromSettings()) {
        randomSeedCheck_->setChecked(false);   // the same seed: the same song
        saveSettings();
        log(tr("saved settings: the same values and seed; Generate makes that song or sound again (New seed each run is off)"));
    }
}

void MainWindow::onSaveWorkflow()
{
    COMFYLIVE_SETTINGS(s);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the graph (API format)"),
                                                      QDir(s.value(QStringLiteral("lastDir")).toString()).filePath(workflow_.name() + QStringLiteral(".json")),
                                                      tr("ComfyUI API graph (*.json)"));
    if (path.isEmpty())
        return;
    QString why;
    if (!workflow_.save(path, &why))
        QMessageBox::warning(this, tr("ComfyLive"), why);
    else
        log(tr("saved ") + QDir::toNativeSeparators(path));
}

void MainWindow::onSaveImage()
{
    if (view_->image().isNull())
        return;
    COMFYLIVE_SETTINGS(s);
    const QString name = QStringLiteral("comfylive_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the picture"), QDir(s.value(QStringLiteral("lastImageDir")).toString()).filePath(name),
                                                      tr("PNG (*.png);;JPEG (*.jpg)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("lastImageDir"), QFileInfo(path).absolutePath());
    if (view_->image().save(path))
        log(tr("saved ") + QDir::toNativeSeparators(path));
    else
        QMessageBox::warning(this, tr("ComfyLive"), tr("Could not write %1").arg(path));
}

void MainWindow::setStatus(const QString& text)
{
    statusLabel_->setText(text);
}

void MainWindow::log(const QString& text)
{
    log_->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + text);
}

void MainWindow::loadSettings()
{
    COMFYLIVE_SETTINGS(s);
    serverEdit_->setText(s.value(QStringLiteral("server"), serverEdit_->text()).toString());
    speechServerEdit_->setText(s.value(QStringLiteral("speechServer"), speechServerEdit_->text()).toString());
    comfyRootEdit_->setText(s.value(QStringLiteral("comfyRoot"), comfyRootEdit_->text()).toString());
    customNodesCheck_->setChecked(s.value(QStringLiteral("customNodes"), true).toBool());
    stopServerCheck_->setChecked(s.value(QStringLiteral("stopServerOnClose"), false).toBool());
    liveDelaySpin_->setValue(s.value(QStringLiteral("liveDelayMs"), 600).toInt());
    randomSeedCheck_->setChecked(s.value(QStringLiteral("randomSeed"), false).toBool());
    const QByteArray graph = s.value(QStringLiteral("graph")).toByteArray();
    if (!graph.isEmpty()) {   // the graph and its values as they were left
        Workflow w;
        if (w.fromJson(QJsonDocument::fromJson(graph).object(), nullptr)) {
            w.setName(s.value(QStringLiteral("graphName"), tr("last graph")).toString());
            workflow_ = w;
        }
    }
    workflowLabel_->setText(tr("Graph: <b>%1</b>").arg(workflow_.name().toHtmlEscaped()));
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
}

void MainWindow::saveSettings() const
{
    if (checkRun_)
        return;
    COMFYLIVE_SETTINGS(s);
    s.setValue(QStringLiteral("server"), serverEdit_->text());
    s.setValue(QStringLiteral("speechServer"), speechServerEdit_->text());
    s.setValue(QStringLiteral("comfyRoot"), comfyRootEdit_->text());
    s.setValue(QStringLiteral("customNodes"), customNodesCheck_->isChecked());
    s.setValue(QStringLiteral("stopServerOnClose"), stopServerCheck_->isChecked());
    s.setValue(QStringLiteral("liveDelayMs"), liveDelaySpin_->value());
    s.setValue(QStringLiteral("randomSeed"), randomSeedCheck_->isChecked());
    s.setValue(QStringLiteral("graph"), QJsonDocument(workflow_.graph()).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("graphName"), workflow_.name());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    COMFYLIVE_SETTINGS(s);
    if (!checkRun_)
        s.setValue(QStringLiteral("geometry"), saveGeometry());
    if (stopServerCheck_->isChecked() && (server_.isRunning() || speechServer_.isRunning() || startedServer_))
        stopLocalServers();
    event->accept();
}
