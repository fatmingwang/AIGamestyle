#include "MainWindow.h"
#include "LastDir.h"
#include "PipelineCommands.h"
#include "PipelineEngine.h"
#include "LogParser.h"
#include "ResourceReplaceWidget.h"
#include "AtlasWidget.h"
#include "SymbolGroupWidget.h"
#include "GroupBuilderWidget.h"
#include "LangManager.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QSplitter>
#include <QScrollBar>
#include <QFileDialog>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTime>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QStatusBar>
#include <QFrame>
#include <QFont>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTabWidget>
#include <QToolBar>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ── Construction / destruction ───────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , settings_(GS_ORG_NAME, QStringLiteral("GameStyleAI"))
{
    // Restore saved language before building UI
    int savedLang = settings_.value(QStringLiteral("lang"), 0).toInt();
    LangManager::instance().setLang(savedLang == 1 ? AppLang::ZH_TW : AppLang::EN);

    config_.load(settings_);
    buildUi();
    syncConfigToUi();

    connect(&process_, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onStdout);
    connect(&process_, &QProcess::readyReadStandardError,
            this, &MainWindow::onStderr);
    connect(&process_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    // ── groupProcess_ signals ─────────────────────────────────────────────
    connect(&groupProcess_, &QProcess::readyReadStandardOutput,
            this, [this]{
                groupProcess_.setReadChannel(QProcess::StandardOutput);
                while (groupProcess_.canReadLine())
                    appendGroupLog(QString::fromUtf8(groupProcess_.readLine()).trimmed());
            });
    connect(&groupProcess_, &QProcess::readyReadStandardError,
            this, [this]{
                groupProcess_.setReadChannel(QProcess::StandardError);
                while (groupProcess_.canReadLine())
                    appendGroupLog(QString::fromUtf8(groupProcess_.readLine()).trimmed(),
                                   QColor(255, 140, 0));
            });
    connect(&groupProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onGroupItemFinished);
    connect(&groupProcess_, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError err) {
                if (err == QProcess::FailedToStart) {
                    appendGroupLog(QStringLiteral("[Group Run] ERROR: failed to start Python — check pythonExe path."),
                                   QColor(244, 67, 54));
                    if (symGroupWidget_) symGroupWidget_->setRunEnabled(true);
                }
            });

    setWindowTitle(LS("GameStyleAI \xe2\x80\x94 Batch Reskin Tool"));
    resize(1200, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (state_ == AppState::RUNNING) {
        process_.kill();
        process_.waitForFinished(2000);
    }
    syncUiToConfig();
    config_.save(settings_);
    settings_.sync();
    event->accept();
}

// ── UI construction ──────────────────────────────────────────────────────────

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    // ── Language toolbar ─────────────────────────────────────────────────────
    {
        auto* toolbar = new QToolBar(this);
        toolbar->setMovable(false);
        toolbar->setFloatable(false);
        addToolBar(Qt::TopToolBarArea, toolbar);

        auto* lblLang = new QLabel(LS("Language:"));
        retranslates_.push_back([lblLang]{ lblLang->setText(LS("Language:")); });
        toolbar->addWidget(lblLang);

        auto* cbxLang = new QComboBox;
        cbxLang->addItem(QStringLiteral("English"));
        cbxLang->addItem(QStringLiteral("\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"));
        int savedLang = settings_.value(QStringLiteral("lang"), 0).toInt();
        cbxLang->setCurrentIndex(savedLang);
        toolbar->addWidget(cbxLang);

        connect(cbxLang, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx){
                    LangManager::instance().setLang(idx == 1 ? AppLang::ZH_TW : AppLang::EN);
                    settings_.setValue(QStringLiteral("lang"), idx);
                    settings_.sync();
                });
    }

    // Tab widget
    tabWidget_ = new QTabWidget;

    // ── Tab 1: Pipeline ───────────────────────────────────────────────────
    auto* pipelineTab = new QWidget;
    auto* pipelineLayout = new QVBoxLayout(pipelineTab);
    pipelineLayout->setContentsMargins(0, 4, 0, 0);
    pipelineLayout->setSpacing(4);

    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch();
        auto* btn = new QPushButton(LS("?  Help"));
        retranslates_.push_back([btn]{ btn->setText(LS("?  Help")); });
        btn->setFixedWidth(80);
        btn->setToolTip(QStringLiteral("How to use the Pipeline tab"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2a6496;color:white;border-radius:4px;"
            "font-weight:bold;padding:3px 8px;border:none;}"
            "QPushButton:hover{background:#337ab7;}"));
        connect(btn, &QPushButton::clicked, this, [this]{
            QMessageBox mb(this);
            mb.setWindowTitle(LS("Pipeline -- Help"));
            mb.setIcon(QMessageBox::Information);
            mb.setText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.pipeline.text")
                : QStringLiteral(
                    "Runs the AI style-transfer pipeline on game asset images.\n"
                    "\n"
                    "QUICK START\n"
                    "  1. Set Project Root to the root of your gamestyleai folder.\n"
                    "  2. Set Source Folder to the images to re-style.\n"
                    "  3. Set Output Folder for the styled results.\n"
                    "  4. Set Asset Root (sub-folder key, e.g. game).\n"
                    "  5. Pick a Hero Reference image (defines the target art style).\n"
                    "  6. Make sure ComfyUI is running -- click Launch to start it.\n"
                    "  7. Click Run All."));
            mb.setDetailedText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.pipeline.details")
                : QStringLiteral(
                    "FIELDS\n"
                    "  Project Root   -- Root of the project; locates pipeline scripts.\n"
                    "  Source Folder  -- Original images to process.\n"
                    "  Output Folder  -- Where styled results are written.\n"
                    "  Asset Root     -- Sub-folder key used by the pipeline.\n"
                    "  Hero Reference -- Style reference image used by IPAdapter.\n"
                    "  ComfyUI URL    -- Server address (default: 127.0.0.1:8188).\n"
                    "                   Click Launch to start ComfyUI if not running.\n"
                    "  Style Profile  -- Profile ID in style_plan.json (blank = default).\n"
                    "  Batch ID       -- Unique run name; folder key under _pipeline/batches/.\n"
                    "  Mode           -- Full Pipeline (5 steps) or Simple Batch (direct).\n"
                    "  Category       -- Loads default prompts from reskin_categories.json.\n"
                    "  Include Spine  -- Also re-style Spine atlas PNG textures.\n"
                    "  IPAdapter Wt   -- Style strength 0.0-1.0 (higher = closer to ref).\n"
                    "  Denoise        -- Change amount (higher = more creative/different).\n"
                    "  Steps          -- Diffusion steps (20-30 is typical).\n"
                    "\n"
                    "PIPELINE STEPS (Full Pipeline mode)\n"
                    "  Scan -> Classify -> Link -> Plan Style -> ComfyUI Batch\n"
                    "\n"
                    "REQUIREMENTS\n"
                    "  - Python 3.9+ in PATH (or configured in project settings)\n"
                    "  - ComfyUI running with an IPAdapter workflow loaded\n"
                    "  - pip install requests Pillow\n"
                    "\n"
                    "COMMON ISSUES\n"
                    "  'ComfyUI not reachable'   -- Click Launch, or start ComfyUI manually.\n"
                    "  'style_plan not found'    -- Use Run All (not Run Step) to build first.\n"
                    "  'Python not found'        -- Ensure python.exe is in system PATH.\n"
                    "  'Stale style plan' dialog -- Click Regenerate to rebuild from scratch.\n"
                    "  Output folder not empty   -- Choose Replace or New Folder (timestamped)."));
            mb.exec();
        });
        hdr->addWidget(btn);
        pipelineLayout->addLayout(hdr);
    }

    pipelineLayout->addWidget(buildConfigPanel());

    auto* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->addWidget(buildPipelinePanel());
    vSplitter->addWidget(buildLogPanel());
    vSplitter->setStretchFactor(0, 2);
    vSplitter->setStretchFactor(1, 3);
    pipelineLayout->addWidget(vSplitter, 1);

    tabWidget_->addTab(pipelineTab, LS("Pipeline"));

    // ── Tab 2: Symbol Groups ──────────────────────────────────────────────
    symGroupWidget_ = new SymbolGroupWidget;
    connect(symGroupWidget_, &SymbolGroupWidget::applyGroupRequested,
            this, &MainWindow::onApplyGroup);
    connect(symGroupWidget_, &SymbolGroupWidget::runGroupsRequested,
            this, &MainWindow::onRunGroups);
    tabWidget_->addTab(symGroupWidget_, LS("Symbol Groups"));

    // ── Tab 3: Group Builder ──────────────────────────────────────────────
    groupBuilder_ = new GroupBuilderWidget;
    groupBuilder_->setPythonExe(config_.pythonExe);
    groupBuilder_->setProjectRoot(config_.projectRoot);
    connect(groupBuilder_, &GroupBuilderWidget::groupsReadyToLoad,
            this, [this](const QList<SymbolGroup>& groups) {
                symGroupWidget_->loadGroups(groups);
                tabWidget_->setCurrentWidget(symGroupWidget_);
            });
    tabWidget_->addTab(groupBuilder_, LS("Group Builder"));

    // ── Tab 4: Online Resource Replace ───────────────────────────────────
    resourceWidget_ = new ResourceReplaceWidget;
    tabWidget_->addTab(resourceWidget_, LS("Online Resource Replace"));

    // ── Tab 5: Atlas Tools ────────────────────────────────────────────────
    atlasWidget_ = new AtlasWidget;
    tabWidget_->addTab(atlasWidget_, LS("Atlas Tools"));

    // ── Tab 6: Image With Prompt ──────────────────────────────────────────
    iwpWidget_ = new ImageWithPromptWidget;
    tabWidget_->addTab(iwpWidget_, LS("Image With Prompt"));

    // ── Tab 7: Text to Image ──────────────────────────────────────────────
    t2iWidget_ = new Text2ImageWidget;
    tabWidget_->addTab(t2iWidget_, LS("Text to Image"));

    // ── Tab 8: Pose Generator ─────────────────────────────────────────────
    poseGenWidget_ = new PoseGeneratorWidget;
    tabWidget_->addTab(poseGenWidget_, LS("Pose Generator"));

    mainLayout->addWidget(tabWidget_, 1);

    // Status bar
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setFixedWidth(200);
    statusLabel_ = new QLabel(QStringLiteral("Idle"));
    elapsedLabel_ = new QLabel(QStringLiteral(""));
    elapsedLabel_->setMinimumWidth(90);
    elapsedLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(elapsedLabel_);
    statusBar()->addPermanentWidget(progressBar_);

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(1000);
    connect(tickTimer_, &QTimer::timeout, this, &MainWindow::onElapsedTick);

    // ── Connect LangManager to retranslateUi ──────────────────────────────
    connect(&LangManager::instance(), &LangManager::langChanged,
            this, &MainWindow::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            symGroupWidget_, &SymbolGroupWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            groupBuilder_, &GroupBuilderWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            resourceWidget_, &ResourceReplaceWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            atlasWidget_, &AtlasWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            iwpWidget_, &ImageWithPromptWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            t2iWidget_, &Text2ImageWidget::retranslateUi);
    connect(&LangManager::instance(), &LangManager::langChanged,
            poseGenWidget_, &PoseGeneratorWidget::retranslateUi);
}

QWidget* MainWindow::buildConfigPanel()
{
    grpConfig_ = new QGroupBox(LS("Configuration"));
    retranslates_.push_back([this]{ grpConfig_->setTitle(LS("Configuration")); });

    auto* grid  = new QGridLayout(grpConfig_);
    grid->setColumnStretch(1, 1);

    // makeBrowse creates a label, line-edit, and Browse button for a row,
    // and registers retranslation callbacks for the label and button.
    auto makeBrowse = [&](QLineEdit*& edit, const char* labelKey, int row,
                          auto slot) {
        auto* lbl = new QLabel(LS(labelKey));
        retranslates_.push_back([lbl, labelKey]{ lbl->setText(LS(labelKey)); });
        grid->addWidget(lbl, row, 0);
        edit = new QLineEdit;
        grid->addWidget(edit, row, 1);
        auto* btn = new QPushButton(LS("Browse..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Browse...")); });
        btn->setFixedWidth(80);
        connect(btn, &QPushButton::clicked, this, slot);
        grid->addWidget(btn, row, 2);
    };

    makeBrowse(edtProjectRoot_, "Project Root:",  0, &MainWindow::onBrowseProjectRoot);
    makeBrowse(edtOutput_,    "Output Folder:",  1, &MainWindow::onBrowseOutput);
    makeBrowse(edtAssetRoot_, "Asset Root:",     2, &MainWindow::onBrowseAssetRoot);

    // Row 4: Working Directory [edit, default "."] [Set] — changes the process's actual
    // current working directory and applies it as the Project Root, independent of
    // whatever working directory the debugger/launcher was started with.
    {
        auto* lbl = new QLabel(LS("Working Directory:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Working Directory:")); });
        grid->addWidget(lbl, 4, 0);
    }
    edtWorkingDir_ = new QLineEdit(QStringLiteral("."));
    grid->addWidget(edtWorkingDir_, 4, 1);
    {
        auto* btnSetWorkingDir = new QPushButton(LS("Set"));
        retranslates_.push_back([btnSetWorkingDir]{ btnSetWorkingDir->setText(LS("Set")); });
        btnSetWorkingDir->setFixedWidth(80);
        connect(btnSetWorkingDir, &QPushButton::clicked, this, &MainWindow::onSetWorkingDirectory);
        grid->addWidget(btnSetWorkingDir, 4, 2);
    }

    // Row 3: Hero Reference (file picker, not folder)
    {
        auto* lbl = new QLabel(LS("Hero Reference:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Hero Reference:")); });
        grid->addWidget(lbl, 3, 0);
    }
    edtHeroRef_ = new QLineEdit;
    edtHeroRef_->setPlaceholderText(LS("(use path from style profile JSON)"));
    grid->addWidget(edtHeroRef_, 3, 1);
    {
        auto* btnHeroRef = new QPushButton(LS("Browse..."));
        retranslates_.push_back([btnHeroRef]{ btnHeroRef->setText(LS("Browse...")); });
        btnHeroRef->setFixedWidth(80);
        connect(btnHeroRef, &QPushButton::clicked, this, &MainWindow::onBrowseHeroRef);
        grid->addWidget(btnHeroRef, 3, 2);
    }

    // Row 5: ComfyUI URL [edit] [Launch]  |  Style Profile
    {
        auto* lbl = new QLabel(LS("ComfyUI URL:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("ComfyUI URL:")); });
        grid->addWidget(lbl, 5, 0);
    }
    auto* comfyUrlRow = new QHBoxLayout;
    comfyUrlRow->setContentsMargins(0, 0, 0, 0);
    comfyUrlRow->setSpacing(4);
    edtComfyUrl_ = new QLineEdit;
    comfyUrlRow->addWidget(edtComfyUrl_);
    btnLaunchComfy_ = new QPushButton(LS("Launch \xe2\x96\xb6"));
    retranslates_.push_back([this]{ btnLaunchComfy_->setText(LS("Launch \xe2\x96\xb6")); });
    btnLaunchComfy_->setFixedWidth(90);
    btnLaunchComfy_->setToolTip(QStringLiteral(
        "Start ComfyUI in a new window\n"
        "Flags: --enable-manager --listen 0.0.0.0"));
    connect(btnLaunchComfy_, &QPushButton::clicked, this, &MainWindow::onLaunchComfyUI);
    comfyUrlRow->addWidget(btnLaunchComfy_);
    grid->addLayout(comfyUrlRow, 5, 1);

    {
        auto* lbl = new QLabel(LS("Style Profile:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Style Profile:")); });
        grid->addWidget(lbl, 5, 2);
    }
    edtStyleProfile_ = new QLineEdit;
    grid->addWidget(edtStyleProfile_, 5, 3);

    // Row 6: Batch ID  |  Mode
    {
        auto* lbl = new QLabel(LS("Batch ID:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Batch ID:")); });
        grid->addWidget(lbl, 6, 0);
    }
    edtBatchId_ = new QLineEdit;
    grid->addWidget(edtBatchId_, 6, 1);

    {
        auto* lbl = new QLabel(LS("Mode:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Mode:")); });
        grid->addWidget(lbl, 6, 2);
    }
    cbxMode_ = new QComboBox;
    cbxMode_->addItem(LS("Full Pipeline"));
    cbxMode_->addItem(LS("Simple Batch"));
    retranslates_.push_back([this]{
        cbxMode_->setItemText(0, LS("Full Pipeline"));
        cbxMode_->setItemText(1, LS("Simple Batch"));
    });
    grid->addWidget(cbxMode_, 6, 3);

    // Row 7: Category  |  Include Spine checkbox
    {
        auto* lbl = new QLabel(LS("Category:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Category:")); });
        grid->addWidget(lbl, 7, 0);
    }
    cbxCategory_ = new QComboBox;
    for (auto& c : { "default", "symbols", "panel", "autospin",
                     "loopshowscore", "background" })
        cbxCategory_->addItem(QString::fromLatin1(c));
    grid->addWidget(cbxCategory_, 7, 1);
    connect(cbxCategory_, &QComboBox::currentTextChanged,
            this, &MainWindow::onCategoryChanged);

    chkIncludeSpine_ = new QCheckBox(LS("Include Spine Textures"));
    retranslates_.push_back([this]{ chkIncludeSpine_->setText(LS("Include Spine Textures")); });
    chkIncludeSpine_->setToolTip(QStringLiteral(
        "When checked, spine atlas PNG textures are also restyled by ComfyUI.\n"
        "When unchecked, only static images are processed."));
    grid->addWidget(chkIncludeSpine_, 7, 2, 1, 2);

    // Row 8: sliders
    auto* sliderRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("IPAdapter Wt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("IPAdapter Wt:")); });
        sliderRow->addWidget(lbl);
    }
    spnIpWeight_ = new QDoubleSpinBox;
    spnIpWeight_->setRange(0.0, 1.0);
    spnIpWeight_->setSingleStep(0.05);
    spnIpWeight_->setDecimals(2);
    sliderRow->addWidget(spnIpWeight_);

    sliderRow->addSpacing(20);
    {
        auto* lbl = new QLabel(LS("Denoise:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Denoise:")); });
        sliderRow->addWidget(lbl);
    }
    spnDenoise_ = new QDoubleSpinBox;
    spnDenoise_->setRange(0.0, 1.0);
    spnDenoise_->setSingleStep(0.05);
    spnDenoise_->setDecimals(2);
    sliderRow->addWidget(spnDenoise_);

    sliderRow->addSpacing(20);
    {
        auto* lbl = new QLabel(LS("Steps:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Steps:")); });
        sliderRow->addWidget(lbl);
    }
    spnSteps_ = new QSpinBox;
    spnSteps_->setRange(1, 100);
    sliderRow->addWidget(spnSteps_);
    sliderRow->addStretch();

    grid->addLayout(sliderRow, 8, 0, 1, 4);

    // Row 9: Positive prompt
    {
        auto* lbl = new QLabel(LS("Positive Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Positive Prompt:")); });
        grid->addWidget(lbl, 9, 0, Qt::AlignTop);
    }
    edtPositive_ = new QPlainTextEdit;
    edtPositive_->setFixedHeight(64);
    edtPositive_->setPlaceholderText(QStringLiteral("(leave empty to use reskin_categories.json per asset type)"));
    grid->addWidget(edtPositive_, 9, 1, 1, 3);

    // Row 10: Negative prompt
    {
        auto* lbl = new QLabel(LS("Negative Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Negative Prompt:")); });
        grid->addWidget(lbl, 10, 0, Qt::AlignTop);
    }
    edtNegative_ = new QPlainTextEdit;
    edtNegative_->setFixedHeight(64);
    edtNegative_->setPlaceholderText(QStringLiteral("(leave empty to use reskin_categories.json per asset type)"));
    grid->addWidget(edtNegative_, 10, 1, 1, 3);

    return grpConfig_;
}

QWidget* MainWindow::buildPipelinePanel()
{
    auto* hSplitter = new QSplitter(Qt::Horizontal);

    // Left: step list + buttons
    auto* leftWidget = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    grpPipelineSteps_ = new QGroupBox(LS("Pipeline Steps"));
    retranslates_.push_back([this]{ grpPipelineSteps_->setTitle(LS("Pipeline Steps")); });
    auto* stepsLayout = new QVBoxLayout(grpPipelineSteps_);

    stepList_ = new StepListWidget;
    stepsLayout->addWidget(stepList_);

    auto* btnRow = new QHBoxLayout;
    btnRunAll_  = new QPushButton(LS("Run All"));
    retranslates_.push_back([this]{ btnRunAll_->setText(LS("Run All")); });
    btnRunStep_ = new QPushButton(LS("Run Step"));
    retranslates_.push_back([this]{ btnRunStep_->setText(LS("Run Step")); });
    btnRestart_ = new QPushButton(LS("Restart"));
    retranslates_.push_back([this]{ btnRestart_->setText(LS("Restart")); });
    btnStop_    = new QPushButton(LS("Stop"));
    retranslates_.push_back([this]{ btnStop_->setText(LS("Stop")); });
    btnStop_->setEnabled(false);
    btnRow->addWidget(btnRunAll_);
    btnRow->addWidget(btnRunStep_);
    btnRow->addWidget(btnRestart_);
    btnRow->addWidget(btnStop_);
    stepsLayout->addLayout(btnRow);

    leftLayout->addWidget(grpPipelineSteps_);
    hSplitter->addWidget(leftWidget);

    connect(btnRunAll_,  &QPushButton::clicked, this, &MainWindow::onRunAll);
    connect(btnRunStep_, &QPushButton::clicked, this, &MainWindow::onRunStep);
    connect(btnRestart_, &QPushButton::clicked, this, &MainWindow::onRestart);
    connect(btnStop_,    &QPushButton::clicked, this, &MainWindow::onStop);

    // Right: detail view
    grpStepDetail_ = new QGroupBox(LS("Step Detail"));
    retranslates_.push_back([this]{ grpStepDetail_->setTitle(LS("Step Detail")); });
    auto* detailLayout = new QVBoxLayout(grpStepDetail_);
    detailView_ = new QTextEdit;
    detailView_->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"), 9);
    mono.setStyleHint(QFont::Monospace);
    detailView_->setFont(mono);
    detailLayout->addWidget(detailView_);
    hSplitter->addWidget(grpStepDetail_);

    hSplitter->setStretchFactor(0, 1);
    hSplitter->setStretchFactor(1, 2);

    return hSplitter;
}

QWidget* MainWindow::buildLogPanel()
{
    grpLog_ = new QGroupBox(LS("Log Output"));
    retranslates_.push_back([this]{ grpLog_->setTitle(LS("Log Output")); });
    auto* layout = new QVBoxLayout(grpLog_);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(5000);
    QFont mono(QStringLiteral("Consolas"), 9);
    mono.setStyleHint(QFont::Monospace);
    logView_->setFont(mono);
    logView_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"));
    layout->addWidget(logView_);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnClearLog_ = new QPushButton(LS("Clear"));
    retranslates_.push_back([this]{ btnClearLog_->setText(LS("Clear")); });
    connect(btnClearLog_, &QPushButton::clicked, this, &MainWindow::onClearLog);
    btnRow->addWidget(btnClearLog_);
    layout->addLayout(btnRow);

    return grpLog_;
}

// ── retranslateUi ─────────────────────────────────────────────────────────────

void MainWindow::retranslateUi()
{
    setWindowTitle(LS("GameStyleAI \xe2\x80\x94 Batch Reskin Tool"));

    if (tabWidget_) {
        tabWidget_->setTabText(0, LS("Pipeline"));
        tabWidget_->setTabText(1, LS("Symbol Groups"));
        tabWidget_->setTabText(2, LS("Group Builder"));
        tabWidget_->setTabText(3, LS("Online Resource Replace"));
        tabWidget_->setTabText(4, LS("Atlas Tools"));
    }

    for (auto& fn : retranslates_)
        fn();
}

// ── Config sync ───────────────────────────────────────────────────────────────

void MainWindow::loadCategoryPrompts(const QString& category)
{
    QString jsonPath = QDir::cleanPath(config_.projectRoot)
                       + QStringLiteral("/TestData/Reskin/reskin_categories.json");
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("WARNING: cannot open reskin_categories.json: %1").arg(jsonPath),
                  QColor(255, 140, 0));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QString key = root.contains(category) ? category : QStringLiteral("default");
    QJsonObject cat = root.value(key).toObject();

    edtPositive_->setPlainText(cat.value(QStringLiteral("positive")).toString());
    edtNegative_->setPlainText(cat.value(QStringLiteral("negative")).toString());
}

void MainWindow::onCategoryChanged(const QString& category)
{
    loadCategoryPrompts(category);
}

void MainWindow::syncConfigToUi()
{
    edtProjectRoot_->setText(config_.projectRoot);
    edtOutput_->setText(config_.outputFolder);
    edtAssetRoot_->setText(config_.assetRoot);
    edtHeroRef_->setText(config_.heroReference);
    edtComfyUrl_->setText(config_.comfyuiServer);
    edtStyleProfile_->setText(config_.styleProfileId);
    edtBatchId_->setText(config_.batchId);
    spnIpWeight_->setValue(config_.ipAdapterWeight);
    spnDenoise_->setValue(config_.denoise);
    spnSteps_->setValue(config_.steps);
    cbxMode_->setCurrentIndex(config_.fullPipeline ? 0 : 1);
    chkIncludeSpine_->setChecked(config_.includeSpine);

    // Set category first (triggers onCategoryChanged which fills prompts from JSON).
    // If the user had saved custom prompts, restore those instead.
    {
        QSignalBlocker blocker(cbxCategory_);
        int catIdx = cbxCategory_->findText(config_.category);
        cbxCategory_->setCurrentIndex(catIdx >= 0 ? catIdx : 0);
    }
    if (config_.positivePrompt.isEmpty() && config_.negativePrompt.isEmpty())
        loadCategoryPrompts(config_.category);
    else {
        edtPositive_->setPlainText(config_.positivePrompt);
        edtNegative_->setPlainText(config_.negativePrompt);
    }

    if (symGroupWidget_)
        symGroupWidget_->setRunConfig(config_.pythonExe,
                                      config_.projectRoot,
                                      config_.comfyuiServer);
    if (iwpWidget_)
        iwpWidget_->setRunConfig(config_.pythonExe,
                                 config_.projectRoot,
                                 config_.comfyuiServer);
    if (t2iWidget_)
        t2iWidget_->setRunConfig(config_.pythonExe,
                                 config_.projectRoot,
                                 config_.comfyuiServer);
    if (poseGenWidget_)
        poseGenWidget_->setRunConfig(config_.pythonExe,
                                      config_.projectRoot,
                                      config_.comfyuiServer);
}

void MainWindow::syncUiToConfig()
{
    config_.projectRoot     = edtProjectRoot_->text();
    config_.outputFolder    = edtOutput_->text();
    config_.assetRoot       = edtAssetRoot_->text();
    config_.heroReference   = edtHeroRef_->text();
    config_.positivePrompt  = edtPositive_->toPlainText().trimmed();
    config_.negativePrompt  = edtNegative_->toPlainText().trimmed();
    config_.comfyuiServer   = edtComfyUrl_->text();
    config_.styleProfileId  = edtStyleProfile_->text();
    config_.batchId         = edtBatchId_->text();
    config_.ipAdapterWeight = spnIpWeight_->value();
    config_.denoise         = spnDenoise_->value();
    config_.steps           = spnSteps_->value();
    config_.fullPipeline    = (cbxMode_->currentIndex() == 0);
    config_.includeSpine    = chkIncludeSpine_->isChecked();
    config_.category        = cbxCategory_->currentText();

    if (symGroupWidget_)
        symGroupWidget_->setRunConfig(config_.pythonExe,
                                      config_.projectRoot,
                                      config_.comfyuiServer);
    if (iwpWidget_)
        iwpWidget_->setRunConfig(config_.pythonExe,
                                 config_.projectRoot,
                                 config_.comfyuiServer);
    if (t2iWidget_)
        t2iWidget_->setRunConfig(config_.pythonExe,
                                 config_.projectRoot,
                                 config_.comfyuiServer);
    if (poseGenWidget_)
        poseGenWidget_->setRunConfig(config_.pythonExe,
                                      config_.projectRoot,
                                      config_.comfyuiServer);
}

// ── Browse slots ─────────────────────────────────────────────────────────────

void MainWindow::onBrowseProjectRoot()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Project Root"), LastDir::get());
    if (!dir.isEmpty()) { LastDir::set(dir); edtProjectRoot_->setText(dir); }
}


void MainWindow::onSetWorkingDirectory()
{
    const QString entered = edtWorkingDir_->text().trimmed().isEmpty()
                                 ? QStringLiteral(".")
                                 : edtWorkingDir_->text().trimmed();
    const QString resolved = QDir::cleanPath(QDir(QDir::currentPath()).absoluteFilePath(entered));

    if (!QDir(resolved).exists()) {
        appendLog(QStringLiteral("[ERROR] Working directory does not exist: %1").arg(resolved),
                  QColor(244, 67, 54));
        return;
    }
    if (!QDir::setCurrent(resolved)) {
        appendLog(QStringLiteral("[ERROR] Failed to change working directory to: %1").arg(resolved),
                  QColor(244, 67, 54));
        return;
    }

    edtWorkingDir_->setText(resolved);
    edtProjectRoot_->setText(resolved);
    syncUiToConfig();
    loadCategoryPrompts(cbxCategory_->currentText());

    appendLog(QStringLiteral("Working directory changed to: %1").arg(resolved), QColor(76, 175, 80));
}

void MainWindow::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Output Folder"), LastDir::get());
    if (!dir.isEmpty()) { LastDir::set(dir); edtOutput_->setText(dir); }
}

void MainWindow::onBrowseAssetRoot()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Asset Root Folder"), LastDir::get());
    if (!dir.isEmpty()) { LastDir::set(dir); edtAssetRoot_->setText(dir); }
}

void MainWindow::onBrowseHeroRef()
{
    QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Hero Reference Image"),
        LastDir::get(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp);;All Files (*)"));
    if (!file.isEmpty()) { LastDir::set(file); edtHeroRef_->setText(file); }
}

void MainWindow::onLaunchComfyUI()
{
    btnLaunchComfy_->setEnabled(false);
    syncUiToConfig(); // config_ must reflect the current Project Root / ComfyUI URL fields

    // ── Check if ComfyUI is already reachable — same probe headless uses ───────
    if (PipelineEngine::comfyuiReachable(config_)) {
        appendLog(QStringLiteral("ComfyUI is already running at %1 — skipping launch.").arg(config_.comfyuiServer),
                  QColor(76, 175, 80));
        btnLaunchComfy_->setEnabled(true);
        return;
    }

    // ── Same launch command headless builds (PipelineEngine::buildComfyUiLaunchRequest) ──
    const QJsonObject req = PipelineEngine::buildComfyUiLaunchRequest(config_);
    if (!req.value(QStringLiteral("ok")).toBool()) {
        appendLog(QStringLiteral("[ERROR] %1,set working directory to Project root")
                      .arg(req.value(QStringLiteral("error")).toString()),
                  QColor(244, 67, 54));
        btnLaunchComfy_->setEnabled(true);
        return;
    }

    appendLog(QStringLiteral("ComfyUI not reachable — launching now..."),
              QColor(255, 193, 7));

    QStringList args;
    for (const QJsonValue& v : req.value(QStringLiteral("args")).toArray())
        args << v.toString();

    auto* p = new QProcess(this);
    p->setProgram(req.value(QStringLiteral("program")).toString());
    p->setArguments(args);
    p->setWorkingDirectory(req.value(QStringLiteral("workingDirectory")).toString());

#ifdef Q_OS_WIN
    // Give it its own visible console window so the user can watch it start/see errors.
    p->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* cpa) {
            cpa->flags             |= CREATE_NEW_CONSOLE;
            cpa->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        });
#endif

    connect(p, &QProcess::started, this, [this] {
        btnLaunchComfy_->setEnabled(true);
    });
    connect(p, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        btnLaunchComfy_->setEnabled(true);
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            p, &QProcess::deleteLater);

    p->start();

    if (p->waitForStarted(3000))
        appendLog(QStringLiteral("ComfyUI launched (--enable-manager --listen 0.0.0.0)."),
                  QColor(100, 200, 255));
    else {
        appendLog(QStringLiteral("[ERROR] Could not launch ComfyUI: %1")
                      .arg(p->errorString()),
                  QColor(244, 67, 54));
        btnLaunchComfy_->setEnabled(true);
        p->deleteLater();
    }
}

void MainWindow::onApplyGroup(const SymbolGroup& group)
{
    edtHeroRef_->setText(group.referencePath);
    spnIpWeight_->setValue(group.ipWeight);
    spnDenoise_->setValue(group.denoise);

    if (!group.items.isEmpty()) {
        const QString itemDir = QFileInfo(group.items.first().filePath).absolutePath();
        edtAssetRoot_->setText(itemDir);

        const QString& pos = group.items.first().prompt;
        const QString& neg = group.items.first().negativePrompt;
        if (!pos.isEmpty()) edtPositive_->setPlainText(pos);
        if (!neg.isEmpty()) edtNegative_->setPlainText(neg);
    }

    const QString projectRoot = edtProjectRoot_->text().trimmed();
    if (!projectRoot.isEmpty()) {
        const QString slug = group.name.simplified().replace(' ', '_').toLower();
        edtOutput_->setText(QDir::cleanPath(
            projectRoot + QStringLiteral("/output/") + slug));
    }

    edtBatchId_->setText(
        QStringLiteral("group_")
        + group.name.simplified().replace(' ', '_').toLower());

    tabWidget_->setCurrentIndex(0);
    appendLog(QStringLiteral("Applied group '%1' (%2 item(s)) to pipeline.")
                  .arg(group.name).arg(group.items.size()),
              QColor(100, 200, 255));
}

// ── Group run ─────────────────────────────────────────────────────────────────

void MainWindow::onRunGroups(const QList<SymbolGroup>& groups, const QString& scriptMode)
{
    groupScriptMode_ = scriptMode;
    syncUiToConfig();
    groupQueue_.clear();
    for (const auto& g : groups)
        for (const auto& item : g.items)
            groupQueue_.append({g, item});

    groupTotalItems_ = groupQueue_.size();
    groupDoneItems_  = 0;

    if (groupQueue_.isEmpty()) {
        appendGroupLog(QStringLiteral("[Group Run] No items found in selected groups."),
                       QColor(255, 140, 0));
        if (symGroupWidget_) symGroupWidget_->setRunEnabled(true);
        return;
    }

    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString folderName;
    if (groups.size() == 1) {
        folderName = QStringLiteral("group_")
                     + groups.first().name.simplified().replace(' ', '_')
                     + QStringLiteral("_") + ts;
    } else {
        folderName = QStringLiteral("group_run_%1groups_%2")
                         .arg(groups.size()).arg(ts);
    }
    groupOutputBase_ = QDir::cleanPath(config_.projectRoot
                                       + QStringLiteral("/output/") + folderName);

    QStringList names;
    for (const auto& g : groups)
        names << QStringLiteral("'%1'(%2)").arg(g.name).arg(g.items.size());
    appendGroupLog(QStringLiteral("=== Group Run: %1  total %2 item(s) ===")
                       .arg(names.join(QStringLiteral(", "))).arg(groupTotalItems_),
                   QColor(255, 193, 7));
    appendGroupLog(QStringLiteral("    Output \xe2\x86\x92 %1").arg(groupOutputBase_),
                   QColor(180, 180, 180));

    runNextGroupItem();
}

void MainWindow::runNextGroupItem()
{
    if (groupQueue_.isEmpty()) {
        appendGroupLog(QStringLiteral("=== Group Run complete: %1 item(s) processed. Output \xe2\x86\x92 %2 ===")
                           .arg(groupTotalItems_).arg(groupOutputBase_),
                       QColor(76, 175, 80));
        if (symGroupWidget_) symGroupWidget_->setRunEnabled(true);
        return;
    }

    auto [group, item] = groupQueue_.takeFirst();
    ++groupDoneItems_;

    const QString itemPath    = QDir::fromNativeSeparators(item.filePath);
    const QString assetRoot   = QFileInfo(itemPath).absolutePath();
    const QString assetRootNm = QFileInfo(assetRoot).fileName();

    const QString batchId = QStringLiteral("grp_%1_%2_%3")
                                .arg(group.name.simplified().replace(' ', '_'))
                                .arg(groupDoneItems_)
                                .arg(QDateTime::currentDateTime().toString(
                                    QStringLiteral("yyyyMMddHHmmss")));

    const QString planDir = QDir::cleanPath(
        groupOutputBase_ + QStringLiteral("/") + assetRootNm);

    if (!QDir().mkpath(planDir)) {
        appendGroupLog(QStringLiteral("[Group Run] ERROR: cannot create plan dir: %1").arg(planDir),
                       QColor(244, 67, 54));
        if (symGroupWidget_) symGroupWidget_->setRunEnabled(true);
        return;
    }

    const QString planPath = planDir + QStringLiteral("/step4_style_plan.json");
    writeStylePlanJson(planPath, group, item, batchId, assetRoot);

    appendGroupLog(QStringLiteral("[Group Run] Item %1/%2 [%3]: %4")
                       .arg(groupDoneItems_).arg(groupTotalItems_)
                       .arg(group.name)
                       .arg(QFileInfo(itemPath).fileName()),
                   QColor(100, 200, 255));

    const QString itemOutputDir = QDir::cleanPath(
        groupOutputBase_ + QStringLiteral("/") + assetRootNm);

    const bool isBatchReskin =
        (groupScriptMode_ == QStringLiteral("batchreskin"));

    const QString scriptPath = QDir::cleanPath(
        isBatchReskin
            ? config_.projectRoot + QStringLiteral("/TestData/Reskin/batch_reskin.py")
            : config_.projectRoot + QStringLiteral("/TestData/ImageWithPrompt/run_imagewithprompt.py"));

    QStringList args;
    args << QStringLiteral("-u") << scriptPath;

    if (isBatchReskin) {
        // batch_reskin.py <sourceFolder> <outputFolder>
        args << QDir::toNativeSeparators(assetRoot)
             << QDir::toNativeSeparators(itemOutputDir);
    } else {
        // run_imagewithprompt.py
        args << QStringLiteral("--reference")   << QDir::toNativeSeparators(itemPath)
             << QStringLiteral("--output-dir")  << QDir::toNativeSeparators(itemOutputDir)
             << QStringLiteral("--output-name") << QFileInfo(itemPath).baseName()
             << QStringLiteral("--ip-weight")   << QString::number(group.ipWeight, 'f', 2)
             << QStringLiteral("--denoise")     << QString::number(group.denoise,   'f', 2);
        if (!group.referencePath.isEmpty())
            args << QStringLiteral("--hero-reference")
                 << QDir::toNativeSeparators(group.referencePath);
        if (!item.prompt.isEmpty())
            args << QStringLiteral("--positive-prompt") << item.prompt;
        if (!item.negativePrompt.isEmpty())
            args << QStringLiteral("--negative-prompt") << item.negativePrompt;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COMFYUI_SERVER"), config_.comfyuiServer);
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    groupProcess_.setProcessEnvironment(env);
    groupProcess_.setWorkingDirectory(QDir::cleanPath(config_.projectRoot));
    groupProcess_.start(config_.pythonExe, args);
}

void MainWindow::writeStylePlanJson(const QString& planPath, const SymbolGroup& group,
                                     const SymbolItem& item, const QString& batchId,
                                     const QString& assetRoot) const
{
    const QString profileId = config_.styleProfileId.isEmpty()
                                  ? QStringLiteral("default")
                                  : config_.styleProfileId;

    QJsonObject grp;
    grp[QStringLiteral("source_group")]  = group.name;
    grp[QStringLiteral("character_id")]  = group.name;
    grp[QStringLiteral("seed")]          = 42;
    grp[QStringLiteral("static_assets")] = QJsonArray{QFileInfo(item.filePath).fileName()};
    grp[QStringLiteral("spine_assets")]  = QJsonArray{};

    QJsonObject root;
    root[QStringLiteral("batch_id")]           = batchId;
    root[QStringLiteral("style_profile_id")]   = profileId;
    root[QStringLiteral("scanned_asset_root")] = QDir::toNativeSeparators(assetRoot);
    root[QStringLiteral("groups")]             = QJsonArray{grp};

    QFile f(planPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

void MainWindow::onGroupItemFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    groupProcess_.setReadChannel(QProcess::StandardOutput);
    while (groupProcess_.canReadLine())
        appendGroupLog(QString::fromUtf8(groupProcess_.readLine()).trimmed());
    groupProcess_.setReadChannel(QProcess::StandardError);
    while (groupProcess_.canReadLine())
        appendGroupLog(QString::fromUtf8(groupProcess_.readLine()).trimmed(),
                       QColor(255, 140, 0));

    const bool ok = (exitCode == 0);
    appendGroupLog(QStringLiteral("[Group Run] Item %1/%2 %3 (exit %4)")
                       .arg(groupDoneItems_).arg(groupTotalItems_)
                       .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                       .arg(exitCode),
                   ok ? QColor(76, 175, 80) : QColor(244, 67, 54));

    runNextGroupItem();
}

// ── Pipeline control ──────────────────────────────────────────────────────────

void MainWindow::onRunAll()
{
    syncUiToConfig();
    stepList_->resetAll();
    detailView_->clear();
    runAll_      = true;
    currentStep_ = 0;
    if (!config_.fullPipeline) {
        launchStep(StepIndex::SimpleBatch);
        return;
    }
    launchStep(StepIndex::Scan);
}

void MainWindow::onRunStep()
{
    syncUiToConfig();
    int idx = stepList_->currentRow();
    if (idx < 0) idx = 0;
    runAll_ = false;
    currentStep_ = idx;
    launchStep(static_cast<StepIndex>(idx));
}

void MainWindow::onRestart()
{
    currentStep_ = 0;
    stepList_->setCurrentRow(0);
    stepList_->resetAll();
    appendLog(QStringLiteral("=== Steps reset to beginning ==="), QColor(180, 180, 180));
}

void MainWindow::onStop()
{
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
    }
}

void MainWindow::onClearLog()
{
    logView_->clear();
}

// ── Process I/O ───────────────────────────────────────────────────────────────

void MainWindow::onStdout()
{
    process_.setReadChannel(QProcess::StandardOutput);
    while (process_.canReadLine()) {
        QString line = QString::fromUtf8(process_.readLine()).trimmed();
        LogEvent ev  = parseLine(line);

        switch (ev.type) {
        case MarkerType::STEP_START:
            appendLog(QStringLiteral("[%1] started").arg(ev.stepName.toUpper()),
                      QColor(255, 193, 7));
            break;

        case MarkerType::STEP_DONE:
            appendLog(QStringLiteral("[%1] done  — %2")
                          .arg(ev.stepName.toUpper(), ev.message),
                      QColor(76, 175, 80));
            break;

        case MarkerType::PROGRESS:
            stepList_->setStepProgress(currentStep_, ev.current, ev.total);
            if (ev.total > 0) {
                progressBar_->setValue(ev.current * 100 / ev.total);
            }
            appendLog(QStringLiteral("[%1] %2/%3")
                          .arg(ev.stepName.toUpper())
                          .arg(ev.current).arg(ev.total),
                      QColor(100, 200, 255));
            break;

        case MarkerType::STEP_DETAIL: {
            QJsonDocument doc(ev.detailJson);
            detailView_->setPlainText(
                QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
            break;
        }

        case MarkerType::GS_ERROR:
            appendLog(QStringLiteral("[ERROR][%1] %2")
                          .arg(ev.stepName.toUpper(), ev.message),
                      QColor(244, 67, 54));
            break;

        default:
            if (!line.isEmpty())
                appendLog(line);
            break;
        }
    }
}

void MainWindow::onStderr()
{
    process_.setReadChannel(QProcess::StandardError);
    while (process_.canReadLine()) {
        QString line = QString::fromUtf8(process_.readLine()).trimmed();
        if (!line.isEmpty())
            appendLog(line, QColor(255, 140, 0));
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    onStdout();
    onStderr();

    bool success = (exitCode == 0);
    setStepDone(static_cast<StepIndex>(currentStep_), success);

    if (!success) {
        setState(AppState::ERROR_STATE);
        return;
    }

    if (runAll_ && config_.fullPipeline && currentStep_ < STEP_COUNT - 1) {
        ++currentStep_;
        launchStep(static_cast<StepIndex>(currentStep_));
    } else {
        if (!runAll_ && currentStep_ < STEP_COUNT - 1)
            stepList_->setCurrentRow(currentStep_ + 1);
        setState(AppState::ALL_DONE);
    }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void MainWindow::appendLog(const QString& text, const QColor& color)
{
    QString ts  = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QString msg = QStringLiteral("[%1] %2").arg(ts, text);

    QTextCharFormat fmt;
    fmt.setForeground(color);

    auto* cursor = new QTextCursor(logView_->document());
    cursor->movePosition(QTextCursor::End);
    cursor->insertText(msg + '\n', fmt);
    delete cursor;

    logView_->verticalScrollBar()->setValue(
        logView_->verticalScrollBar()->maximum());
}

void MainWindow::appendGroupLog(const QString& text, const QColor& color)
{
    appendLog(text, color);
    if (symGroupWidget_)
        symGroupWidget_->appendGroupLog(text, color);
}

void MainWindow::setStepRunning(StepIndex index)
{
    stepList_->setStepStatus(static_cast<int>(index), StepStatus::RUNNING);
    statusLabel_->setText(
        QStringLiteral("Running Step %1/%2 — %3")
            .arg(static_cast<int>(index) + 1).arg(STEP_COUNT).arg(stepName(index)));
    progressBar_->setValue(0);
}

void MainWindow::setStepDone(StepIndex index, bool success)
{
    stepList_->setStepStatus(static_cast<int>(index),
        success ? StepStatus::SUCCESS : StepStatus::FAILED);
}

bool MainWindow::confirmOutputFolder()
{
    // Shared with headless mode via PipelineEngine, so both apply the exact same "is the
    // output folder non-empty" verdict — this function only adds the interactive dialog on top.
    const QJsonObject state = PipelineEngine::checkOutputFolderState(config_);
    if (!state.value(QStringLiteral("applicable")).toBool(true))
        return true;
    if (state.value(QStringLiteral("empty")).toBool())
        return true;

    const QString targetFolder = state.value(QStringLiteral("targetFolder")).toString();
    const int entryCount       = state.value(QStringLiteral("entryCount")).toInt();

    QMessageBox dlg(this);
    dlg.setWindowTitle(QStringLiteral("Output Folder Not Empty"));
    dlg.setIcon(QMessageBox::Warning);
    dlg.setText(QStringLiteral("The output folder already contains files:"));
    dlg.setInformativeText(targetFolder);
    dlg.setDetailedText(
        QStringLiteral("%1 item(s) found in:\n%2\n\nChoose how to proceed:")
            .arg(entryCount)
            .arg(targetFolder));

    QPushButton* btnDelete    = dlg.addButton(QStringLiteral("Delete"),    QMessageBox::DestructiveRole);
    QPushButton* btnOverwrite = dlg.addButton(QStringLiteral("Overwrite"), QMessageBox::AcceptRole);
    dlg.addButton(QMessageBox::Cancel);
    dlg.setDefaultButton(btnOverwrite);

    dlg.exec();

    if (dlg.clickedButton() == btnDelete) {
        if (!QDir(targetFolder).removeRecursively()) {
            appendLog(QStringLiteral("[ERROR] Failed to delete output folder: %1").arg(targetFolder),
                      QColor(244, 67, 54));
            return false;
        }
        appendLog(QStringLiteral("Output folder deleted: %1").arg(targetFolder), QColor(180, 180, 180));
        return true;
    }
    if (dlg.clickedButton() == btnOverwrite) {
        return true;
    }
    return false;
}

bool MainWindow::checkStylePlanFresh()
{
    // Shared with headless mode via PipelineEngine, so both apply the exact same "does the
    // style plan match the current Asset Root" verdict — this function only adds the dialog.
    const QJsonObject state = PipelineEngine::checkStylePlanState(config_);
    if (!state.value(QStringLiteral("applicable")).toBool(true))
        return true;
    if (state.value(QStringLiteral("fresh")).toBool())
        return true;

    const QString reason = state.value(QStringLiteral("reason")).toString();

    appendLog(QStringLiteral("[WARNING] Stale style_plan detected: %1").arg(reason.split('\n').first()),
              QColor(255, 140, 0));

    QMessageBox dlg(this);
    dlg.setWindowTitle(QStringLiteral("Style Plan is Outdated"));
    dlg.setIcon(QMessageBox::Warning);
    dlg.setText(QStringLiteral("The existing style_plan.json is outdated."));
    dlg.setInformativeText(
        reason + QStringLiteral(
            "\n\nChoose how to proceed:"));

    auto* btnRegen    = dlg.addButton(
        QStringLiteral("Regenerate  (re-run Scan \xe2\x86\x92 Classify \xe2\x86\x92 Link \xe2\x86\x92 Plan Style \xe2\x86\x92 ComfyUI)"),
        QMessageBox::AcceptRole);
    auto* btnContinue = dlg.addButton(
        QStringLiteral("Continue with existing plan  (may have missing files)"),
        QMessageBox::DestructiveRole);
    dlg.addButton(QMessageBox::Cancel);
    dlg.setDefaultButton(btnRegen);
    dlg.exec();

    if (dlg.clickedButton() == btnRegen) {
        appendLog(QStringLiteral("=== Regenerating: starting from step 0 ==="), QColor(255, 193, 7));
        stepList_->resetAll();
        detailView_->clear();
        runAll_      = true;
        currentStep_ = 0;
        launchStep(StepIndex::Scan);
        return false;
    }
    if (dlg.clickedButton() == btnContinue)
        return true;

    return false;
}

void MainWindow::launchStep(StepIndex index)
{
    syncUiToConfig();
    config_.save(settings_);
    settings_.sync();

    if ((index == StepIndex::Scan || index == StepIndex::ComfyUiBatch || index == StepIndex::SimpleBatch)
            && !confirmOutputFolder()) {
        setState(AppState::IDLE);
        return;
    }

    if (index == StepIndex::ComfyUiBatch && !checkStylePlanFresh())
        return;

    // Shared with headless mode via PipelineEngine: this builds byte-for-byte the same
    // program/args/env that headless mode's equivalent session command would run. MainWindow
    // just applies those fields onto its own async process_ member so the progress bar, live
    // marker parsing, and Stop button keep working during the run.
    const QString projectRoot = QDir::cleanPath(config_.projectRoot);
    const QJsonObject request = PipelineEngine::buildStepRequest(index, config_, projectRoot);

    QStringList fullArgs;
    for (const QJsonValue& v : request.value(QStringLiteral("args")).toArray())
        fullArgs << v.toString();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QJsonObject envOverrides = request.value(QStringLiteral("env")).toObject();
    for (auto it = envOverrides.begin(); it != envOverrides.end(); ++it)
        env.insert(it.key(), it.value().toString());
    process_.setProcessEnvironment(env);
    process_.setWorkingDirectory(request.value(QStringLiteral("workingDirectory")).toString());

    const QString program = request.value(QStringLiteral("program")).toString();

    appendLog(QStringLiteral("Launching: %1 %2")
                  .arg(program, fullArgs.join(' ')),
              QColor(180, 180, 180));

    setState(AppState::RUNNING);
    setStepRunning(index);
    process_.start(program, fullArgs);
}

void MainWindow::setState(AppState s)
{
    state_ = s;
    bool running = (s == AppState::RUNNING);
    btnRunAll_->setEnabled(!running);
    btnRunStep_->setEnabled(!running);
    btnRestart_->setEnabled(!running);
    btnStop_->setEnabled(running);

    switch (s) {
    case AppState::IDLE:
        tickTimer_->stop();
        statusLabel_->setText(QStringLiteral("Idle"));
        progressBar_->setValue(0);
        elapsedLabel_->setText(QStringLiteral(""));
        break;
    case AppState::RUNNING:
        runTimer_.start();
        tickTimer_->start();
        elapsedLabel_->setText(QStringLiteral("\xe2\x8f\xb1 00:00:00"));
        break;
    case AppState::ALL_DONE:
        tickTimer_->stop();
        onElapsedTick();
        statusLabel_->setText(QStringLiteral("Done"));
        progressBar_->setValue(100);
        appendLog(QStringLiteral("=== Pipeline completed ==="), QColor(76, 175, 80));
        if (runAll_ || currentStep_ == STEP_COUNT - 1) {
            auto btn = QMessageBox::information(
                this,
                QStringLiteral("Pipeline Finished"),
                QStringLiteral("All steps completed successfully.\n\nOpen output folder?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (btn == QMessageBox::Yes && !config_.outputFolder.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(config_.outputFolder));
        }
        break;
    case AppState::ERROR_STATE:
        tickTimer_->stop();
        onElapsedTick();
        statusLabel_->setText(QStringLiteral("Error — check log"));
        appendLog(QStringLiteral("=== Step failed ==="), QColor(244, 67, 54));
        break;
    }
}

void MainWindow::onElapsedTick()
{
    qint64 secs = runTimer_.elapsed() / 1000;
    qint64 h = secs / 3600;
    qint64 m = (secs % 3600) / 60;
    qint64 s = secs % 60;
    elapsedLabel_->setText(
        QStringLiteral("\xe2\x8f\xb1 %1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
}

void MainWindow::updateStatusBar()
{
    // Called from various places; state label is already updated in setState().
}
