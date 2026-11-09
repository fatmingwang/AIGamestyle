#include "MainWindow.h"

#include "AudioPlayer.h"
#include "ImageView.h"
#include "ParamHelp.h"
#include "SongTools.h"
#include "Translator.h"

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

    connect(&client_, &ComfyClient::logLine, this, &MainWindow::log);
    connect(&server_, &ComfyProcess::logLine, this, &MainWindow::log);
    connect(&server_, &ComfyProcess::up, this, [this] {
        startedServer_ = true;
        client_.connectToServer(serverEdit_->text());
    });
    connect(&server_, &ComfyProcess::failed, this, [this](const QString& why) {
        setStatus(tr("ComfyUI did not start"));
        log(why);
        startServerBtn_->setEnabled(true);
        QMessageBox::warning(this, tr("ComfyLive"), why);
    });
    connect(&client_, &ComfyClient::connected, this, [this] {
        setStatus(tr("Connected to %1").arg(client_.server()));
        connectBtn_->setText(tr("Reconnect"));
        startServerBtn_->setEnabled(!server_.isRunning());
        for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
            log(line);
        buildParams();
        generateBtn_->setEnabled(true);
        emit ready();
    });
    connect(&client_, &ComfyClient::connectionFailed, this, [this](const QString& why) {
        setStatus(tr("Not connected"));
        log(tr("cannot reach %1: %2").arg(serverEdit_->text(), why));
        startServerBtn_->setEnabled(true);
    });
    connect(&client_, &ComfyClient::disconnected, this, [this] { setStatus(tr("Connection lost: retrying ...")); });
    connect(&client_, &ComfyClient::queued, this, [this](const QString& id, int) {
        currentId_ = id;
        pending_ << id;
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
    connect(&client_, &ComfyClient::imageReady, this, [this](const QString& id, const QImage& image, const QString& file) {
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(image.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                         QString());
        item->setData(Qt::UserRole, image);
        item->setToolTip(file);
        history_->insertItem(0, item);
        while (history_->count() > 40)
            delete history_->takeItem(history_->count() - 1);
        if (id == currentId_) {
            view_->setImage(image);
            saveImageBtn_->setEnabled(true);
        }
        log(tr("image: %1 (%2 x %3)").arg(file).arg(image.width()).arg(image.height()));
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
        log(tr("audio: %1 (%2 KB)").arg(file).arg(bytes.size() / 1024));
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
            view_->setCaption(tr("done in %1 s").arg(s, 0, 'f', 1));
            log(tr("done in %1 s, %2 previews").arg(s, 0, 'f', 1).arg(previews_));
            emit runDone(true, QString());
        }
        stopBtn_->setEnabled(!pending_.isEmpty());
    });
    connect(&client_, &ComfyClient::failed, this, [this](const QString& id, const QString& why) {
        pending_.removeAll(id);
        stopBtn_->setEnabled(!pending_.isEmpty());
        log(tr("ComfyUI failed: ") + why);
        view_->setCaption(tr("failed: see the log"));
        if (id == currentId_)
            emit runDone(false, why);
    });
    connect(&client_, &ComfyClient::interrupted, this, [this](const QString& id) {
        pending_.removeAll(id);
        stopBtn_->setEnabled(!pending_.isEmpty());
    });
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
    liveTimer_.stop();
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
    stopServerCheck_->setChecked(true);
    auto* buttons = new QHBoxLayout;
    connectBtn_ = new QPushButton(tr("Connect"), serverBox);
    startServerBtn_ = new QPushButton(tr("Start local ComfyUI"), serverBox);
    buttons->addWidget(connectBtn_);
    buttons->addWidget(startServerBtn_);
    statusLabel_ = new QLabel(tr("Not connected"), serverBox);
    sf->addRow(tr("Server"), serverEdit_);
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
    builtInBtn->setToolTip(tr("A built-in graph: SDXL text to image, or ACE-Step 1.5 text to music"));
    auto* builtInMenu = new QMenu(builtInBtn);
    builtInMenu->addAction(tr("Text to image (SDXL)"), this, [this] { setWorkflow(Workflow::textToImage()); });
    builtInMenu->addAction(tr("Text to music (ACE-Step 1.5)"), this, [this] { setWorkflow(Workflow::textToMusic()); });
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
    openAudioBtn_->setToolTip(tr("Open the song in Windows' own player (for a format ComfyLive cannot play)"));
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
    rvl->addWidget(view_, 1);
    rvl->addWidget(audioRow_);
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
    connect(history_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString audio = item->data(Qt::UserRole + 1).toString();
        if (!audio.isEmpty()) {
            showAudio(audio, item->toolTip(), true);
            return;
        }
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
    progress_->setRange(0, progressMax);
    progress_->setValue(progressValue);
    stopBtn_->setEnabled(canStop);
    saveImageBtn_->setEnabled(canSave);
    startServerBtn_->setEnabled(canStart);
    if (!audioPath.isEmpty()) {
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
    history_->insertItem(0, item);
    while (history_->count() > 40)
        delete history_->takeItem(history_->count() - 1);
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
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the song"), QDir(s.value(QStringLiteral("lastAudioDir")).toString()).filePath(QFileInfo(from).fileName()),
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

void MainWindow::generate()
{
    if (!client_.isReady()) {
        log(tr("not connected"));
        return;
    }
    if (randomSeedCheck_->isChecked())
        randomizeSeeds();
    if (!pending_.isEmpty()) {   // what is running or waiting is out of date now
        client_.deleteQueued(pending_);
        client_.interrupt();
        pending_.clear();
    }
    currentId_.clear();
    saveSettings();
    client_.queuePrompt(workflow_.graph());
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
    setStatus(tr("Looking for ComfyUI at %1 ...").arg(serverEdit_->text()));
    client_.setServer(serverEdit_->text());
    client_.ping([this, startServer](bool up) {
        if (up) {
            client_.connectToServer(serverEdit_->text());
        } else if (startServer) {
            onStartServerClicked();
        } else {
            setStatus(tr("No ComfyUI at %1: press Start local ComfyUI, or start it yourself").arg(serverEdit_->text()));
            startServerBtn_->setEnabled(true);
        }
    });
}

void MainWindow::onConnectClicked()
{
    saveSettings();
    setStatus(tr("Connecting ..."));
    client_.connectToServer(serverEdit_->text());
}

void MainWindow::onStartServerClicked()
{
    saveSettings();
    const QUrl u(QStringLiteral("http://") + serverEdit_->text().trimmed());
    if (u.host() != QStringLiteral("127.0.0.1") && u.host() != QStringLiteral("localhost")) {
        QMessageBox::information(this, tr("ComfyLive"), tr("Start only works for a ComfyUI on this PC (127.0.0.1)."));
        return;
    }
    startServerBtn_->setEnabled(false);
    setStatus(tr("Starting ComfyUI: the first start can take a minute ..."));
    server_.start(comfyRootEdit_->text(), quint16(u.port(8188)), customNodesCheck_->isChecked());
}

void MainWindow::setWorkflow(const Workflow& w)
{
    stopRun();
    workflow_ = w;
    if (client_.isReady()) {
        for (const QString& line : workflow_.fixMissingChoices(client_.objectInfo()))
            log(line);
        buildParams();
    } else {
        workflowLabel_->setText(tr("Graph: <b>%1</b>").arg(workflow_.name().toHtmlEscaped()));
    }
    saveSettings();
}

void MainWindow::onOpenWorkflow()
{
    COMFYLIVE_SETTINGS(s);
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a ComfyUI graph (API format)"), s.value(QStringLiteral("lastDir")).toString(),
                                                      tr("ComfyUI API graph or song settings (*.json)"));
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
    if (w.fromSettings()) {
        randomSeedCheck_->setChecked(false);   // the same seed: the same song
        saveSettings();
        log(tr("a song's settings: the same values and seed; Generate makes that song again (New seed each run is off)"));
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
    comfyRootEdit_->setText(s.value(QStringLiteral("comfyRoot"), comfyRootEdit_->text()).toString());
    customNodesCheck_->setChecked(s.value(QStringLiteral("customNodes"), true).toBool());
    stopServerCheck_->setChecked(s.value(QStringLiteral("stopServer"), true).toBool());
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
    COMFYLIVE_SETTINGS(s);
    s.setValue(QStringLiteral("server"), serverEdit_->text());
    s.setValue(QStringLiteral("comfyRoot"), comfyRootEdit_->text());
    s.setValue(QStringLiteral("customNodes"), customNodesCheck_->isChecked());
    s.setValue(QStringLiteral("stopServer"), stopServerCheck_->isChecked());
    s.setValue(QStringLiteral("liveDelayMs"), liveDelaySpin_->value());
    s.setValue(QStringLiteral("randomSeed"), randomSeedCheck_->isChecked());
    s.setValue(QStringLiteral("graph"), QJsonDocument(workflow_.graph()).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("graphName"), workflow_.name());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    COMFYLIVE_SETTINGS(s);
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    if (server_.isRunning() && stopServerCheck_->isChecked())
        server_.stop();
    event->accept();
}
