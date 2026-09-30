#include "PoseGeneratorWidget.h"
#include "AppConfig.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFont>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QDateTime>

// ── Construction ─────────────────────────────────────────────────────────────

PoseGeneratorWidget::PoseGeneratorWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// Returns the canonical default API URL for a given backend key.
static QString posegenBackendDefaultUrl(const QString& b)
{
    if (b == QStringLiteral("ollama"))  return QStringLiteral("http://localhost:11434");
    if (b == QStringLiteral("openai"))  return QStringLiteral("https://api.openai.com/v1");
    if (b == QStringLiteral("llama"))   return QStringLiteral("http://127.0.0.1:8666/v1");
    return QStringLiteral("https://api.anthropic.com");
}

static QString posegenBackendUrlKey(const QString& b)
{
    return QStringLiteral("poseGen/apiUrl/") + b;
}

// ── UI ────────────────────────────────────────────────────────────────────────

void PoseGeneratorWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Shared top row: Name / Subject Type ──────────────────────────────
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Name:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Name:")); });
        lbl->setFixedWidth(60);
        row->addWidget(lbl);
        edtName_ = new QLineEdit;
        edtName_->setPlaceholderText(QStringLiteral("e.g. climb, BirdFly"));
        row->addWidget(edtName_, 1);

        auto* lblSubject = new QLabel(LS("Subject:"));
        retranslates_.push_back([lblSubject]{ lblSubject->setText(LS("Subject:")); });
        row->addWidget(lblSubject);
        cbxSubjectType_ = new QComboBox;
        cbxSubjectType_->addItem(LS("Human"),            QStringLiteral("human"));
        cbxSubjectType_->addItem(LS("2-Legged Animal"),  QStringLiteral("biped_animal"));
        cbxSubjectType_->addItem(LS("4-Legged Animal"),  QStringLiteral("quadruped"));
        cbxSubjectType_->addItem(LS("Bird"),             QStringLiteral("bird"));
        cbxSubjectType_->addItem(LS("Fish"),             QStringLiteral("fish"));
        cbxSubjectType_->addItem(LS("Other Creature"),   QStringLiteral("other"));
        retranslates_.push_back([this]{
            cbxSubjectType_->setItemText(0, LS("Human"));
            cbxSubjectType_->setItemText(1, LS("2-Legged Animal"));
            cbxSubjectType_->setItemText(2, LS("4-Legged Animal"));
            cbxSubjectType_->setItemText(3, LS("Bird"));
            cbxSubjectType_->setItemText(4, LS("Fish"));
            cbxSubjectType_->setItemText(5, LS("Other Creature"));
        });
        cbxSubjectType_->setToolTip(QStringLiteral(
            "Human / 2-Legged Animal: AI writes pose-skeleton data for ControlNet "
            "(the human-biped rig can approximate a two-legged animal's gait).\n"
            "4-Legged Animal / Bird / Fish / Other: switches to the Creature Animator "
            "below (reference image + motion prompt -> AI video), since a skeleton "
            "built for two arms and two legs cannot represent those body plans."));
        row->addWidget(cbxSubjectType_);
        row->addStretch();
        root->addLayout(row);
        connect(cbxSubjectType_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PoseGeneratorWidget::onSubjectTypeChanged);
    }
    // ── Shared top row: Output Dir ────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Output Dir:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Output Dir:")); });
        lbl->setFixedWidth(60);
        row->addWidget(lbl);
        edtOutputDir_ = new QLineEdit;
        row->addWidget(edtOutputDir_, 1);
        auto* btnBrowse = new QPushButton(LS("Browse\xe2\x80\xa6"));
        retranslates_.push_back([btnBrowse]{ btnBrowse->setText(LS("Browse\xe2\x80\xa6")); });
        row->addWidget(btnBrowse);
        root->addLayout(row);
        connect(btnBrowse, &QPushButton::clicked, this, &PoseGeneratorWidget::onBrowseOutputDir);
    }

    // ══════════════════════════════════════════════════════════════════════
    // Skeleton pipeline section (Human / 2-Legged Animal)
    // ══════════════════════════════════════════════════════════════════════
    skeletonSection_ = new QWidget;
    auto* skelLay = new QVBoxLayout(skeletonSection_);
    skelLay->setContentsMargins(0, 0, 0, 0);
    skelLay->setSpacing(4);

    // ── Backend row ───────────────────────────────────────────────────────
    auto* backendRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Backend:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Backend:")); });
        lbl->setFixedWidth(64);
        backendRow->addWidget(lbl);
    }
    cbxBackend_ = new QComboBox;
    cbxBackend_->addItem(QStringLiteral("Anthropic (Claude)"),   QStringLiteral("anthropic"));
    cbxBackend_->addItem(QStringLiteral("Ollama (local, free)"), QStringLiteral("ollama"));
    cbxBackend_->addItem(QStringLiteral("OpenAI (gpt-4o-mini)"), QStringLiteral("openai"));
    cbxBackend_->addItem(QStringLiteral("LLaMA.cpp (local)"),    QStringLiteral("llama"));
    cbxBackend_->setFixedWidth(175);
    backendRow->addWidget(cbxBackend_);
    backendRow->addSpacing(8);
    {
        auto* lbl = new QLabel(LS("API URL:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("API URL:")); });
        lbl->setFixedWidth(56);
        backendRow->addWidget(lbl);
    }
    edtUrl_ = new QLineEdit;
    edtUrl_->setToolTip(QStringLiteral(
        "API server address. Edit the IP/domain to point to your server.\n"
        "Ollama native:     http://localhost:11434\n"
        "OpenAI-compat:     http://localhost:11434/v1\n"
        "Anthropic default: https://api.anthropic.com"));
    backendRow->addWidget(edtUrl_, 1);
    btnTest_ = new QPushButton(LS("Test"));
    retranslates_.push_back([this]{ btnTest_->setText(LS("Test")); });
    btnTest_->setFixedWidth(48);
    backendRow->addWidget(btnTest_);
    lblTestStatus_ = new QLabel;
    lblTestStatus_->setMinimumWidth(90);
    lblTestStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    backendRow->addWidget(lblTestStatus_);
    skelLay->addLayout(backendRow);

    auto loadSavedUrl = [this](int) {
        const QString b = cbxBackend_->currentData().toString();
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        const QString saved = s.value(posegenBackendUrlKey(b), QString()).toString();
        edtUrl_->setText(saved.isEmpty() ? posegenBackendDefaultUrl(b) : saved);
        lblTestStatus_->clear();
        lblTestStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    };
    connect(cbxBackend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, loadSavedUrl);
    loadSavedUrl(0);
    connect(btnTest_, &QPushButton::clicked, this, &PoseGeneratorWidget::onTestBackendUrl);

    // ── Describe the motion ───────────────────────────────────────────────
    auto* describeBox = new QGroupBox(LS("Describe the Motion"));
    retranslates_.push_back([describeBox]{ describeBox->setTitle(LS("Describe the Motion")); });
    auto* describeLay = new QVBoxLayout(describeBox);

    {
        auto* row = new QHBoxLayout;
        chkLoop_ = new QCheckBox(LS("Loops"));
        retranslates_.push_back([this]{ chkLoop_->setText(LS("Loops")); });
        chkLoop_->setChecked(true);
        chkLoop_->setToolTip(QStringLiteral(
            "Checked: the animation repeats forever (walk, idle, ...).\n"
            "Unchecked: a one-shot action (jump, shoot, punch, ...)."));
        row->addWidget(chkLoop_);

        auto* lblHint = new QLabel(LS("Hint frames:"));
        retranslates_.push_back([lblHint]{ lblHint->setText(LS("Hint frames:")); });
        row->addWidget(lblHint);
        spnHintFrames_ = new QSpinBox;
        spnHintFrames_->setRange(2, 32);
        spnHintFrames_->setValue(6);
        spnHintFrames_->setToolTip(QStringLiteral(
            "Rough frame-count hint sent to the AI. The final render frame\n"
            "count is set separately below and can differ."));
        row->addWidget(spnHintFrames_);
        row->addStretch();
        describeLay->addLayout(row);
    }
    {
        auto* lbl = new QLabel(LS("Description:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Description:")); });
        describeLay->addWidget(lbl);
        edtDescription_ = new QPlainTextEdit;
        edtDescription_->setPlaceholderText(QStringLiteral(
            "e.g. a person climbing a ladder, reaching up with alternating hands "
            "while the opposite knee lifts each rung"));
        edtDescription_->setFixedHeight(64);
        describeLay->addWidget(edtDescription_);
    }
    {
        auto* row = new QHBoxLayout;
        btnGenerate_ = new QPushButton(LS("Generate Pose Data (AI) \xe2\x96\xb6"));
        retranslates_.push_back([this]{ btnGenerate_->setText(LS("Generate Pose Data (AI) \xe2\x96\xb6")); });
        row->addWidget(btnGenerate_);
        lblGenerateStatus_ = new QLabel;
        lblGenerateStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblGenerateStatus_);
        row->addStretch();
        describeLay->addLayout(row);
    }
    skelLay->addWidget(describeBox);
    connect(btnGenerate_, &QPushButton::clicked, this, &PoseGeneratorWidget::onGeneratePoseData);

    // ── Pose JSON (editable) ──────────────────────────────────────────────
    auto* jsonBox = new QGroupBox(LS("Pose JSON (editable)"));
    retranslates_.push_back([jsonBox]{ jsonBox->setTitle(LS("Pose JSON (editable)")); });
    auto* jsonLay = new QVBoxLayout(jsonBox);
    {
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        edtJson_ = new QPlainTextEdit;
        edtJson_->setFont(mono);
        edtJson_->setPlaceholderText(QStringLiteral(
            "(generated JSON will appear here \xe2\x80\x94 you can edit it by hand before rendering)"));
        jsonLay->addWidget(edtJson_, 1);
    }
    {
        auto* row = new QHBoxLayout;
        btnLoadJson_ = new QPushButton(LS("Load JSON\xe2\x80\xa6"));
        retranslates_.push_back([this]{ btnLoadJson_->setText(LS("Load JSON\xe2\x80\xa6")); });
        row->addWidget(btnLoadJson_);
        btnSaveJson_ = new QPushButton(LS("Save to Library"));
        retranslates_.push_back([this]{ btnSaveJson_->setText(LS("Save to Library")); });
        row->addWidget(btnSaveJson_);
        lblJsonPath_ = new QLabel;
        lblJsonPath_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblJsonPath_, 1);
        jsonLay->addLayout(row);
    }
    skelLay->addWidget(jsonBox, 1);
    connect(btnLoadJson_, &QPushButton::clicked, this, &PoseGeneratorWidget::onLoadJson);
    connect(btnSaveJson_, &QPushButton::clicked, this, &PoseGeneratorWidget::onSaveJson);

    // ── Render frames ─────────────────────────────────────────────────────
    auto* renderBox = new QGroupBox(LS("Render Frames"));
    retranslates_.push_back([renderBox]{ renderBox->setTitle(LS("Render Frames")); });
    auto* renderLay = new QVBoxLayout(renderBox);
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Facing:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Facing:")); });
        row->addWidget(lbl);
        cbxFacing_ = new QComboBox;
        cbxFacing_->addItem(LS("Right"), QStringLiteral("right"));
        cbxFacing_->addItem(LS("Left"),  QStringLiteral("left"));
        retranslates_.push_back([this]{
            cbxFacing_->setItemText(0, LS("Right"));
            cbxFacing_->setItemText(1, LS("Left"));
        });
        row->addWidget(cbxFacing_);

        auto* lblFrames = new QLabel(LS("Frames:"));
        retranslates_.push_back([lblFrames]{ lblFrames->setText(LS("Frames:")); });
        row->addWidget(lblFrames);
        spnRenderFrames_ = new QSpinBox;
        spnRenderFrames_->setRange(0, 64);
        spnRenderFrames_->setSpecialValueText(LS("(animation default)"));
        retranslates_.push_back([this]{ spnRenderFrames_->setSpecialValueText(LS("(animation default)")); });
        row->addWidget(spnRenderFrames_);
        row->addStretch();
        renderLay->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        btnGeneratePictures_ = new QPushButton(LS("Generate Pictures \xe2\x96\xb6"));
        retranslates_.push_back([this]{ btnGeneratePictures_->setText(LS("Generate Pictures \xe2\x96\xb6")); });
        row->addWidget(btnGeneratePictures_);
        lblRenderStatus_ = new QLabel;
        lblRenderStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblRenderStatus_);
        row->addStretch();
        renderLay->addLayout(row);
    }
    skelLay->addWidget(renderBox);
    connect(btnGeneratePictures_, &QPushButton::clicked,
            this, &PoseGeneratorWidget::onGeneratePictures);

    root->addWidget(skeletonSection_, 1);

    // ══════════════════════════════════════════════════════════════════════
    // Creature pipeline section (4-Legged Animal / Bird / Fish / Other)
    // ══════════════════════════════════════════════════════════════════════
    creatureSection_ = new QWidget;
    auto* creatureLay = new QVBoxLayout(creatureSection_);
    creatureLay->setContentsMargins(0, 0, 0, 0);
    creatureLay->setSpacing(4);

    auto* creatureBox = new QGroupBox(LS("Creature Animator (image-to-video, no skeleton)"));
    retranslates_.push_back([creatureBox]{
        creatureBox->setTitle(LS("Creature Animator (image-to-video, no skeleton)")); });
    auto* cLay = new QVBoxLayout(creatureBox);

    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Reference Image:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Reference Image:")); });
        lbl->setFixedWidth(110);
        row->addWidget(lbl);
        edtRefImage_ = new QLineEdit;
        edtRefImage_->setPlaceholderText(QStringLiteral(
            "a still image of the creature (generate one via Text to Image first)"));
        row->addWidget(edtRefImage_, 1);
        auto* btnBrowseRef = new QPushButton(LS("Browse\xe2\x80\xa6"));
        retranslates_.push_back([btnBrowseRef]{ btnBrowseRef->setText(LS("Browse\xe2\x80\xa6")); });
        row->addWidget(btnBrowseRef);
        cLay->addLayout(row);
        connect(btnBrowseRef, &QPushButton::clicked, this, &PoseGeneratorWidget::onBrowseRefImage);
    }
    {
        auto* lbl = new QLabel(LS("Motion Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Motion Prompt:")); });
        cLay->addWidget(lbl);
        edtMotionPrompt_ = new QPlainTextEdit;
        edtMotionPrompt_->setPlaceholderText(QStringLiteral(
            "e.g. a short fat bird flapping its wings, flying in place, loop"));
        edtMotionPrompt_->setFixedHeight(48);
        cLay->addWidget(edtMotionPrompt_);
    }
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Negative Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Negative Prompt:")); });
        lbl->setFixedWidth(110);
        row->addWidget(lbl);
        edtNegativePrompt_ = new QLineEdit(
            QStringLiteral("static, blurry, distorted, extra limbs, deformed"));
        row->addWidget(edtNegativePrompt_, 1);
        cLay->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        auto* lbl1 = new QLabel(LS("Duration:"));
        retranslates_.push_back([lbl1]{ lbl1->setText(LS("Duration:")); });
        row->addWidget(lbl1);
        cbxDuration_ = new QComboBox;
        cbxDuration_->addItem(QStringLiteral("5s"), QStringLiteral("5"));
        cbxDuration_->addItem(QStringLiteral("10s"), QStringLiteral("10"));
        cbxDuration_->setToolTip(QStringLiteral(
            "Kling's minimum is 5s regardless of how short a loop you want to keep --\n"
            "you'll always decode more raw frames than the ones you select below."));
        row->addWidget(cbxDuration_);

        auto* lbl2 = new QLabel(LS("Aspect:"));
        retranslates_.push_back([lbl2]{ lbl2->setText(LS("Aspect:")); });
        row->addWidget(lbl2);
        cbxAspectRatio_ = new QComboBox;
        cbxAspectRatio_->addItem(QStringLiteral("1:1"),  QStringLiteral("1:1"));
        cbxAspectRatio_->addItem(QStringLiteral("16:9"), QStringLiteral("16:9"));
        cbxAspectRatio_->addItem(QStringLiteral("9:16"), QStringLiteral("9:16"));
        cbxAspectRatio_->setToolTip(QStringLiteral(
            "Kling only supports these three ratios -- crop after if you need an\n"
            "odd sprite canvas size."));
        row->addWidget(cbxAspectRatio_);

        auto* lbl3 = new QLabel(LS("Model:"));
        retranslates_.push_back([lbl3]{ lbl3->setText(LS("Model:")); });
        row->addWidget(lbl3);
        cbxKlingModel_ = new QComboBox;
        cbxKlingModel_->addItem(QStringLiteral("kling-v2-master"), QStringLiteral("kling-v2-master"));
        cbxKlingModel_->addItem(QStringLiteral("kling-v2-1-master"), QStringLiteral("kling-v2-1-master"));
        cbxKlingModel_->addItem(QStringLiteral("kling-v2-5-turbo"), QStringLiteral("kling-v2-5-turbo"));
        cbxKlingModel_->addItem(QStringLiteral("kling-v1-6"), QStringLiteral("kling-v1-6"));
        cbxKlingModel_->addItem(QStringLiteral("kling-v1"), QStringLiteral("kling-v1"));
        row->addWidget(cbxKlingModel_);

        auto* lbl4 = new QLabel(LS("Mode:"));
        retranslates_.push_back([lbl4]{ lbl4->setText(LS("Mode:")); });
        row->addWidget(lbl4);
        cbxKlingMode_ = new QComboBox;
        cbxKlingMode_->addItem(QStringLiteral("std"), QStringLiteral("std"));
        cbxKlingMode_->addItem(QStringLiteral("pro"), QStringLiteral("pro"));
        cbxKlingMode_->setToolTip(QStringLiteral(
            "\"pro\" costs more but generally tracks motion prompts better."));
        row->addWidget(cbxKlingMode_);

        auto* lblFrames = new QLabel(LS("Frames:"));
        retranslates_.push_back([lblFrames]{ lblFrames->setText(LS("Frames:")); });
        row->addWidget(lblFrames);
        spnCreatureFrames_ = new QSpinBox;
        spnCreatureFrames_->setRange(2, 32);
        spnCreatureFrames_->setValue(8);
        spnCreatureFrames_->setToolTip(QStringLiteral(
            "How many evenly-spaced frames to keep from the decoded clip."));
        row->addWidget(spnCreatureFrames_);
        row->addStretch();
        cLay->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        btnGenerateAnimation_ = new QPushButton(LS("Generate Animation (Video AI) \xe2\x96\xb6"));
        retranslates_.push_back([this]{
            btnGenerateAnimation_->setText(LS("Generate Animation (Video AI) \xe2\x96\xb6")); });
        btnGenerateAnimation_->setToolTip(QStringLiteral(
            "Billed through your comfy.org account credits (Kling image-to-video)."));
        row->addWidget(btnGenerateAnimation_);
        lblCreatureStatus_ = new QLabel;
        lblCreatureStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblCreatureStatus_);
        row->addStretch();
        cLay->addLayout(row);
        connect(btnGenerateAnimation_, &QPushButton::clicked,
                this, &PoseGeneratorWidget::onGenerateAnimation);
    }

    creatureLay->addWidget(creatureBox, 1);
    root->addWidget(creatureSection_, 1);

    // ── Shared preview + log ──────────────────────────────────────────────
    lblPreview_ = new QLabel;
    lblPreview_->setMinimumHeight(120);
    lblPreview_->setAlignment(Qt::AlignCenter);
    lblPreview_->setStyleSheet(QStringLiteral(
        "QLabel{background:#1a1a1a;border:1px solid #555;"
        "border-radius:3px;color:#555;}"));
    lblPreview_->setText(QStringLiteral("Preview"));
    root->addWidget(lblPreview_, 1);

    auto* logBox = new QGroupBox(LS("Log"));
    retranslates_.push_back([logBox]{ logBox->setTitle(LS("Log")); });
    auto* logLay = new QVBoxLayout(logBox);
    {
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        logView_ = new QPlainTextEdit;
        logView_->setReadOnly(true);
        logView_->setMaximumBlockCount(3000);
        logView_->setFont(mono);
        logView_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:#1e1e1e;color:#d4d4d4;}"));
        logLay->addWidget(logView_);
    }
    {
        auto* row = new QHBoxLayout;
        row->addStretch();
        auto* btn = new QPushButton(LS("Clear"));
        retranslates_.push_back([btn]{ btn->setText(LS("Clear")); });
        connect(btn, &QPushButton::clicked, this, [this]{ logView_->clear(); });
        row->addWidget(btn);
        logLay->addLayout(row);
    }
    root->addWidget(logBox, 1);

    onSubjectTypeChanged(cbxSubjectType_->currentIndex());
    updateButtons();
}

void PoseGeneratorWidget::retranslateUi()
{
    for (auto& fn : retranslates_) fn();
}

bool PoseGeneratorWidget::isSkeletonMode() const
{
    const QString data = cbxSubjectType_->currentData().toString();
    return data == QStringLiteral("human") || data == QStringLiteral("biped_animal");
}

void PoseGeneratorWidget::onSubjectTypeChanged(int)
{
    const bool skeleton = isSkeletonMode();
    skeletonSection_->setVisible(skeleton);
    creatureSection_->setVisible(!skeleton);
    updateButtons();
}

// ── Config ────────────────────────────────────────────────────────────────────

void PoseGeneratorWidget::setRunConfig(const QString& pythonExe,
                                        const QString& projectRoot,
                                        const QString& comfyuiServer)
{
    pythonExe_     = pythonExe;
    projectRoot_   = projectRoot;
    comfyuiServer_ = comfyuiServer;

    if (edtOutputDir_->text().trimmed().isEmpty() && !projectRoot_.isEmpty()) {
        edtOutputDir_->setText(QDir::toNativeSeparators(
            QDir::cleanPath(projectRoot_ + QStringLiteral("/output/PoseGenerator"))));
    }
    updateButtons();
}

QString PoseGeneratorWidget::posesLibraryDir() const
{
    return QDir::cleanPath(projectRoot_
        + QStringLiteral("/TestData/PoseGenerator/poses_library"));
}

void PoseGeneratorWidget::updateButtons()
{
    const bool genBusy      = genProcess_      && genProcess_->state()      != QProcess::NotRunning;
    const bool renderBusy   = renderProcess_   && renderProcess_->state()   != QProcess::NotRunning;
    const bool creatureBusy = creatureProcess_ && creatureProcess_->state() != QProcess::NotRunning;
    btnGenerate_->setEnabled(!genBusy);
    btnGeneratePictures_->setEnabled(!renderBusy && !edtJson_->toPlainText().trimmed().isEmpty());
    btnGenerateAnimation_->setEnabled(!creatureBusy);
}

// ── Log ───────────────────────────────────────────────────────────────────────

void PoseGeneratorWidget::appendLog(const QString& text, const QColor& color)
{
    const QString ts  = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString msg = QStringLiteral("[%1] %2").arg(ts, text);

    QTextCharFormat fmt;
    fmt.setForeground(color);
    QTextCursor cursor(logView_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(msg + QLatin1Char('\n'), fmt);
    logView_->verticalScrollBar()->setValue(
        logView_->verticalScrollBar()->maximum());
}

// ── Test backend URL ──────────────────────────────────────────────────────────

void PoseGeneratorWidget::onTestBackendUrl()
{
    const QString testUrl = edtUrl_->text().trimmed();

    static const char* kCheckScript =
        "import socket,sys,os,urllib.parse;"
        "p=urllib.parse.urlparse(os.environ['CHECK_URL']);"
        "h=p.hostname or 'localhost';"
        "port=p.port or(443 if p.scheme=='https' else 80);"
        "s=socket.socket();"
        "s.settimeout(4);"
        "r=s.connect_ex((h,port));"
        "s.close();"
        "sys.exit(r)";

    btnTest_->setEnabled(false);
    lblTestStatus_->setText(LS("Checking\xe2\x80\xa6"));
    lblTestStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));

    auto* proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("CHECK_URL"), testUrl);
    proc->setProcessEnvironment(env);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        if (code == 0) {
            lblTestStatus_->setText(LS("Reachable \xe2\x9c\x93"));
            lblTestStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
        } else {
            lblTestStatus_->setText(LS("Unreachable \xe2\x9c\x97"));
            lblTestStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
        }
        btnTest_->setEnabled(true);
        proc->deleteLater();
    });

    proc->start(pythonExe_, { QStringLiteral("-c"), QString::fromLatin1(kCheckScript) });
}

// ── Generate pose data (AI) ───────────────────────────────────────────────────

void PoseGeneratorWidget::onGeneratePoseData()
{
    if (genProcess_ && genProcess_->state() != QProcess::NotRunning) return;
    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set."), QColor(255, 80, 80));
        return;
    }
    const QString name = edtName_->text().trimmed();
    const QString description = edtDescription_->toPlainText().trimmed();
    if (name.isEmpty() || description.isEmpty()) {
        QMessageBox::information(this, LS("Generate Pose Data"),
            LS("Enter both a name and a description first."));
        return;
    }

    const QString scriptPath = QDir::cleanPath(projectRoot_
        + QStringLiteral("/TestData/PoseGenerator/generate_pose_from_prompt.py"));
    if (!QFile::exists(scriptPath)) {
        appendLog(QStringLiteral("ERROR: script not found:\n  ") + scriptPath,
                  QColor(255, 80, 80));
        return;
    }

    const QString backend = cbxBackend_->currentData().toString();
    const QString apiUrl  = edtUrl_->text().trimmed();
    const QString subjectType = cbxSubjectType_->currentData().toString();
    const QString outputJson = QDir::cleanPath(
        posesLibraryDir() + QStringLiteral("/") + name + QStringLiteral(".json"));

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--name")         << name
         << QStringLiteral("--description")  << description
         << QStringLiteral("--subject-type") << subjectType
         << QStringLiteral("--loop")         << (chkLoop_->isChecked() ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("--frames")       << QString::number(spnHintFrames_->value())
         << QStringLiteral("--backend")      << backend
         << QStringLiteral("--api-url")      << apiUrl
         << QStringLiteral("--output")       << QDir::toNativeSeparators(outputJson);

    if (!genProcess_) {
        genProcess_ = new QProcess(this);
        connect(genProcess_, &QProcess::readyReadStandardOutput,
                this, &PoseGeneratorWidget::onGenProcessReadyRead);
        connect(genProcess_, &QProcess::readyReadStandardError, this, [this]{
            genProcess_->setReadChannel(QProcess::StandardError);
            while (genProcess_->canReadLine()) {
                const QString line = QString::fromUtf8(genProcess_->readLine()).trimmed();
                if (!line.isEmpty()) appendLog(QStringLiteral("[ai err] ") + line, QColor(255, 140, 0));
            }
        });
        connect(genProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &PoseGeneratorWidget::onGenProcessFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    genProcess_->setProcessEnvironment(env);
    genProcess_->setWorkingDirectory(projectRoot_);

    appendLog(QStringLiteral("=== Generate pose data: %1 (backend=%2, subject=%3) ===")
              .arg(name, backend, subjectType), QColor(100, 180, 255));
    lblGenerateStatus_->setText(LS("Generating\xe2\x80\xa6"));
    lblGenerateStatus_->setStyleSheet(QStringLiteral("color:#ffb300;font-style:italic;"));
    btnGenerate_->setEnabled(false);

    genProcess_->start(pythonExe_, args);

    // Local/reasoning models can take minutes; show elapsed time so a long
    // wait doesn't look like a hang.
    genStartSecs_ = QDateTime::currentSecsSinceEpoch();
    if (!genHeartbeat_) {
        genHeartbeat_ = new QTimer(this);
        connect(genHeartbeat_, &QTimer::timeout,
                this, &PoseGeneratorWidget::onGenerateHeartbeat);
    }
    genHeartbeat_->start(5000);
}

void PoseGeneratorWidget::onGenerateHeartbeat()
{
    const qint64 elapsed = QDateTime::currentSecsSinceEpoch() - genStartSecs_;
    const QString timeStr = elapsed >= 60
        ? QStringLiteral("%1m %2s").arg(elapsed / 60).arg(elapsed % 60)
        : QStringLiteral("%1s").arg(elapsed);
    lblGenerateStatus_->setText(QStringLiteral("Generating\xe2\x80\xa6 %1").arg(timeStr));
    appendLog(QStringLiteral("Still generating\xe2\x80\xa6 (%1 elapsed)").arg(timeStr),
              QColor(120, 120, 120));
}

void PoseGeneratorWidget::onGenProcessReadyRead()
{
    genProcess_->setReadChannel(QProcess::StandardOutput);
    while (genProcess_->canReadLine()) {
        const QString line = QString::fromUtf8(genProcess_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QStringLiteral("[PROGRESS]"))) {
            const QString msg = line.mid(10).trimmed();
            lblGenerateStatus_->setText(msg);
            appendLog(msg, QColor(255, 193, 7));
        } else if (line.startsWith(QStringLiteral("[ERROR]"))) {
            appendLog(line.mid(7).trimmed(), QColor(255, 80, 80));
        } else if (line.startsWith(QStringLiteral("[DONE]"))) {
            lastJsonPath_ = line.mid(6).trimmed();
        } else {
            appendLog(line);
        }
    }
}

void PoseGeneratorWidget::onGenProcessFinished(int exitCode, QProcess::ExitStatus)
{
    if (genHeartbeat_) genHeartbeat_->stop();
    onGenProcessReadyRead();
    btnGenerate_->setEnabled(true);

    if (exitCode != 0 || lastJsonPath_.isEmpty()) {
        lblGenerateStatus_->setText(LS("Failed \xe2\x9c\x97"));
        lblGenerateStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
        appendLog(QStringLiteral("Generate pose data failed (exit %1).").arg(exitCode),
                  QColor(255, 80, 80));
        return;
    }

    QFile f(lastJsonPath_);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("Cannot read output: ") + lastJsonPath_, QColor(255, 80, 80));
        return;
    }
    edtJson_->setPlainText(QString::fromUtf8(f.readAll()));
    lblJsonPath_->setText(lastJsonPath_);

    lblGenerateStatus_->setText(LS("Done \xe2\x9c\x93"));
    lblGenerateStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
    appendLog(QStringLiteral("Wrote %1").arg(lastJsonPath_), QColor(100, 200, 100));
    updateButtons();
}

// ── Load / Save JSON ──────────────────────────────────────────────────────────

void PoseGeneratorWidget::onLoadJson()
{
    const QString startDir = QFileInfo(posesLibraryDir()).exists()
        ? posesLibraryDir() : LastDir::get();
    const QString path = QFileDialog::getOpenFileName(
        this, LS("Load Pose JSON"), startDir,
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, LS("Load JSON"), LS("Cannot open file:\n") + path);
        return;
    }
    edtJson_->setPlainText(QString::fromUtf8(f.readAll()));
    lastJsonPath_ = path;
    lblJsonPath_->setText(path);
    appendLog(QStringLiteral("Loaded: %1").arg(path));
    updateButtons();
}

void PoseGeneratorWidget::onSaveJson()
{
    const QString text = edtJson_->toPlainText();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, LS("Save to Library"),
            LS("The JSON is not valid:\n") + err.errorString());
        return;
    }

    QString name = doc.object().value(QStringLiteral("name")).toString();
    if (name.isEmpty()) name = edtName_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, LS("Save to Library"),
            LS("The JSON has no \"name\" field \xe2\x80\x94 enter a Name above first."));
        return;
    }

    const QString libDir = posesLibraryDir();
    QDir().mkpath(libDir);
    const QString path = QDir::cleanPath(libDir + QStringLiteral("/") + name + QStringLiteral(".json"));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, LS("Save to Library"), LS("Cannot write file:\n") + path);
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    lastJsonPath_ = path;
    lblJsonPath_->setText(path);
    appendLog(QStringLiteral("Saved to library: %1").arg(path), QColor(100, 200, 100));
}

// ── Render frames ─────────────────────────────────────────────────────────────

void PoseGeneratorWidget::onBrowseOutputDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, LS("Select Output Directory"), edtOutputDir_->text());
    if (!dir.isEmpty()) edtOutputDir_->setText(QDir::toNativeSeparators(dir));
}

void PoseGeneratorWidget::onGeneratePictures()
{
    if (renderProcess_ && renderProcess_->state() != QProcess::NotRunning) return;
    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set."), QColor(255, 80, 80));
        return;
    }

    const QString jsonText = edtJson_->toPlainText().trimmed();
    if (jsonText.isEmpty()) {
        QMessageBox::information(this, LS("Generate Pictures"),
            LS("Generate or load pose JSON first."));
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, LS("Generate Pictures"),
            LS("The JSON is not valid:\n") + err.errorString());
        return;
    }

    // Render always reads from a file, so make sure the edited text is on disk
    // even if the user tweaked it after generating/loading.
    QString name = doc.object().value(QStringLiteral("name")).toString();
    if (name.isEmpty()) name = edtName_->text().trimmed();
    if (name.isEmpty()) name = QStringLiteral("custom");

    const QString tempDir = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/output/PoseGenerator"));
    QDir().mkpath(tempDir);
    const QString animPath = QDir::cleanPath(tempDir + QStringLiteral("/") + name + QStringLiteral("_edit.json"));
    {
        QFile f(animPath);
        if (!f.open(QIODevice::WriteOnly)) {
            appendLog(QStringLiteral("Cannot write temp animation file:\n  ") + animPath,
                      QColor(255, 80, 80));
            return;
        }
        f.write(doc.toJson(QJsonDocument::Indented));
    }

    const QString scriptPath = QDir::cleanPath(projectRoot_
        + QStringLiteral("/TestData/PoseGenerator/generate_pose_sequence.py"));
    if (!QFile::exists(scriptPath)) {
        appendLog(QStringLiteral("ERROR: script not found:\n  ") + scriptPath,
                  QColor(255, 80, 80));
        return;
    }

    QString outDir = edtOutputDir_->text().trimmed();
    if (outDir.isEmpty()) outDir = tempDir;
    QDir().mkpath(outDir);

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--animation")  << QDir::toNativeSeparators(animPath)
         << QStringLiteral("--facing")     << cbxFacing_->currentData().toString()
         << QStringLiteral("--output-dir") << QDir::toNativeSeparators(outDir)
         << QStringLiteral("--prefix")     << (QStringLiteral("pose_") + name);
    if (spnRenderFrames_->value() > 0)
        args << QStringLiteral("--frames") << QString::number(spnRenderFrames_->value());

    if (!renderProcess_) {
        renderProcess_ = new QProcess(this);
        connect(renderProcess_, &QProcess::readyReadStandardOutput,
                this, &PoseGeneratorWidget::onRenderProcessReadyRead);
        connect(renderProcess_, &QProcess::readyReadStandardError, this, [this]{
            renderProcess_->setReadChannel(QProcess::StandardError);
            while (renderProcess_->canReadLine()) {
                const QString line = QString::fromUtf8(renderProcess_->readLine()).trimmed();
                if (!line.isEmpty()) appendLog(QStringLiteral("[render err] ") + line, QColor(255, 140, 0));
            }
        });
        connect(renderProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &PoseGeneratorWidget::onRenderProcessFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    renderProcess_->setProcessEnvironment(env);
    renderProcess_->setWorkingDirectory(projectRoot_);

    appendLog(QStringLiteral("=== Generate pictures: %1 -> %2 ===")
              .arg(name, QDir::toNativeSeparators(outDir)), QColor(100, 180, 255));
    lblRenderStatus_->setText(LS("Rendering\xe2\x80\xa6"));
    lblRenderStatus_->setStyleSheet(QStringLiteral("color:#ffb300;font-style:italic;"));
    lblPreview_->setPixmap(QPixmap());
    lblPreview_->setText(QStringLiteral("Rendering\xe2\x80\xa6"));
    btnGeneratePictures_->setEnabled(false);

    renderProcess_->start(pythonExe_, args);
}

void PoseGeneratorWidget::onRenderProcessReadyRead()
{
    renderProcess_->setReadChannel(QProcess::StandardOutput);
    while (renderProcess_->canReadLine()) {
        const QString line = QString::fromUtf8(renderProcess_->readLine()).trimmed();
        if (!line.isEmpty()) appendLog(line);
    }
}

void PoseGeneratorWidget::onRenderProcessFinished(int exitCode, QProcess::ExitStatus)
{
    onRenderProcessReadyRead();
    updateButtons();

    if (exitCode != 0) {
        lblRenderStatus_->setText(LS("Failed \xe2\x9c\x97"));
        lblRenderStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
        lblPreview_->setText(QStringLiteral("Failed"));
        appendLog(QStringLiteral("Generate pictures failed (exit %1).").arg(exitCode),
                  QColor(255, 80, 80));
        return;
    }

    lblRenderStatus_->setText(LS("Done \xe2\x9c\x93"));
    lblRenderStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));

    // The renderer names its contact sheet "<prefix>_preview.png"
    QString name = edtName_->text().trimmed();
    {
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(edtJson_->toPlainText().toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QString jsonName = doc.object().value(QStringLiteral("name")).toString();
            if (!jsonName.isEmpty()) name = jsonName;
        }
    }
    const QString previewPath = QDir::cleanPath(
        edtOutputDir_->text().trimmed() + QStringLiteral("/pose_") + name + QStringLiteral("_preview.png"));

    const QPixmap px(previewPath);
    if (!px.isNull()) {
        lblPreview_->setText({});
        lblPreview_->setPixmap(px.scaled(lblPreview_->width(), lblPreview_->height(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblPreview_->setText(QStringLiteral("Done (preview not found)"));
    }
    appendLog(QStringLiteral("Preview: %1").arg(previewPath), QColor(100, 200, 100));
}

// ── Creature animator ─────────────────────────────────────────────────────────

void PoseGeneratorWidget::onBrowseRefImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, LS("Select Reference Image"), LastDir::get(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);
    edtRefImage_->setText(QDir::toNativeSeparators(path));
}

void PoseGeneratorWidget::onGenerateAnimation()
{
    if (creatureProcess_ && creatureProcess_->state() != QProcess::NotRunning) return;
    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set."), QColor(255, 80, 80));
        return;
    }
    const QString name = edtName_->text().trimmed();
    const QString refImage = edtRefImage_->text().trimmed();
    const QString motionPrompt = edtMotionPrompt_->toPlainText().trimmed();
    if (name.isEmpty() || refImage.isEmpty() || motionPrompt.isEmpty()) {
        QMessageBox::information(this, LS("Generate Animation"),
            LS("Enter a Name, a Reference Image, and a Motion Prompt first."));
        return;
    }
    if (!QFile::exists(refImage)) {
        appendLog(QStringLiteral("ERROR: reference image not found: ") + refImage,
                  QColor(255, 80, 80));
        return;
    }

    const QString scriptPath = QDir::cleanPath(projectRoot_
        + QStringLiteral("/TestData/CreatureAnimation/generate_creature_animation.py"));
    if (!QFile::exists(scriptPath)) {
        appendLog(QStringLiteral("ERROR: script not found:\n  ") + scriptPath,
                  QColor(255, 80, 80));
        return;
    }

    QString outDir = edtOutputDir_->text().trimmed();
    if (outDir.isEmpty())
        outDir = QDir::cleanPath(projectRoot_ + QStringLiteral("/output/PoseGenerator"));
    QDir().mkpath(outDir);

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--reference-image") << QDir::toNativeSeparators(refImage)
         << QStringLiteral("--motion-prompt")   << motionPrompt
         << QStringLiteral("--negative-prompt") << edtNegativePrompt_->text().trimmed()
         << QStringLiteral("--name")            << name
         << QStringLiteral("--frames")          << QString::number(spnCreatureFrames_->value())
         << QStringLiteral("--duration")        << cbxDuration_->currentData().toString()
         << QStringLiteral("--aspect-ratio")    << cbxAspectRatio_->currentData().toString()
         << QStringLiteral("--model-name")      << cbxKlingModel_->currentData().toString()
         << QStringLiteral("--mode")            << cbxKlingMode_->currentData().toString()
         << QStringLiteral("--output-dir")      << QDir::toNativeSeparators(outDir);

    if (!creatureProcess_) {
        creatureProcess_ = new QProcess(this);
        connect(creatureProcess_, &QProcess::readyReadStandardOutput,
                this, &PoseGeneratorWidget::onCreatureProcessReadyRead);
        connect(creatureProcess_, &QProcess::readyReadStandardError, this, [this]{
            creatureProcess_->setReadChannel(QProcess::StandardError);
            while (creatureProcess_->canReadLine()) {
                const QString line = QString::fromUtf8(creatureProcess_->readLine()).trimmed();
                if (!line.isEmpty()) appendLog(QStringLiteral("[video err] ") + line, QColor(255, 140, 0));
            }
        });
        connect(creatureProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &PoseGeneratorWidget::onCreatureProcessFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("COMFYUI_SERVER"), comfyuiServer_);
    creatureProcess_->setProcessEnvironment(env);
    creatureProcess_->setWorkingDirectory(projectRoot_);

    appendLog(QStringLiteral("=== Generate animation: %1 (Kling %2/%3, %4s) ===")
              .arg(name, cbxKlingModel_->currentData().toString(),
                   cbxKlingMode_->currentData().toString(),
                   cbxDuration_->currentData().toString()),
              QColor(100, 180, 255));
    lblCreatureStatus_->setText(LS("Requesting video\xe2\x80\xa6"));
    lblCreatureStatus_->setStyleSheet(QStringLiteral("color:#ffb300;font-style:italic;"));
    lblPreview_->setPixmap(QPixmap());
    lblPreview_->setText(QStringLiteral("Requesting video\xe2\x80\xa6"));
    btnGenerateAnimation_->setEnabled(false);

    creatureProcess_->start(pythonExe_, args);

    creatureStartSecs_ = QDateTime::currentSecsSinceEpoch();
    if (!creatureHeartbeat_) {
        creatureHeartbeat_ = new QTimer(this);
        connect(creatureHeartbeat_, &QTimer::timeout,
                this, &PoseGeneratorWidget::onCreatureHeartbeat);
    }
    creatureHeartbeat_->start(5000);
}

void PoseGeneratorWidget::onCreatureHeartbeat()
{
    const qint64 elapsed = QDateTime::currentSecsSinceEpoch() - creatureStartSecs_;
    const QString timeStr = elapsed >= 60
        ? QStringLiteral("%1m %2s").arg(elapsed / 60).arg(elapsed % 60)
        : QStringLiteral("%1s").arg(elapsed);
    lblCreatureStatus_->setText(QStringLiteral("Working\xe2\x80\xa6 %1").arg(timeStr));
    appendLog(QStringLiteral("Still working\xe2\x80\xa6 (%1 elapsed)").arg(timeStr),
              QColor(120, 120, 120));
}

void PoseGeneratorWidget::onCreatureProcessReadyRead()
{
    creatureProcess_->setReadChannel(QProcess::StandardOutput);
    QString donePath;
    while (creatureProcess_->canReadLine()) {
        const QString line = QString::fromUtf8(creatureProcess_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QStringLiteral("[PROGRESS]"))) {
            const QString msg = line.mid(10).trimmed();
            lblCreatureStatus_->setText(msg);
            appendLog(msg, QColor(255, 193, 7));
        } else if (line.startsWith(QStringLiteral("[ERROR]"))) {
            appendLog(line.mid(7).trimmed(), QColor(255, 80, 80));
        } else if (line.startsWith(QStringLiteral("[DONE]"))) {
            donePath = line.mid(6).trimmed();
        } else {
            appendLog(line);
        }
    }
    if (!donePath.isEmpty()) lastJsonPath_ = donePath; // reused as "last atlas json"
}

void PoseGeneratorWidget::onCreatureProcessFinished(int exitCode, QProcess::ExitStatus)
{
    if (creatureHeartbeat_) creatureHeartbeat_->stop();
    onCreatureProcessReadyRead();
    updateButtons();

    if (exitCode != 0) {
        lblCreatureStatus_->setText(LS("Failed \xe2\x9c\x97"));
        lblCreatureStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
        lblPreview_->setText(QStringLiteral("Failed"));
        appendLog(QStringLiteral("Generate animation failed (exit %1).").arg(exitCode),
                  QColor(255, 80, 80));
        return;
    }

    lblCreatureStatus_->setText(LS("Done \xe2\x9c\x93"));
    lblCreatureStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));

    // generate_creature_animation.py names its sheet "<name>_atlas.png"
    const QString name = edtName_->text().trimmed();
    const QString sheetPath = QDir::cleanPath(
        edtOutputDir_->text().trimmed() + QStringLiteral("/") + name + QStringLiteral("_atlas.png"));

    const QPixmap px(sheetPath);
    if (!px.isNull()) {
        lblPreview_->setText({});
        lblPreview_->setPixmap(px.scaled(lblPreview_->width(), lblPreview_->height(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblPreview_->setText(QStringLiteral("Done (sheet not found)"));
    }
    appendLog(QStringLiteral("Sprite sheet: %1").arg(sheetPath), QColor(100, 200, 100));
}
