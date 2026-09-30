#include "GroupBuilderWidget.h"
#include "AppConfig.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QMessageBox>
#include <QMap>
#include <QProcessEnvironment>
#include <QSettings>
#include <QFont>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>

static const QStringList IMAGE_EXTS{
    QStringLiteral("png"), QStringLiteral("jpg"),
    QStringLiteral("jpeg"), QStringLiteral("webp") };

// ── Construction ─────────────────────────────────────────────────────────────

GroupBuilderWidget::GroupBuilderWidget(QWidget* parent) : QWidget(parent)
{
    buildUi();
}

// ── UI construction ───────────────────────────────────────────────────────────

void GroupBuilderWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Top: folder row ───────────────────────────────────────────────────
    auto* folderRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Source Folder:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Source Folder:")); });
        folderRow->addWidget(lbl);
    }
    edtFolder_ = new QLineEdit;
    edtFolder_->setPlaceholderText(LS("Select a folder containing symbol images"));
    folderRow->addWidget(edtFolder_, 1);
    auto* btnBrowseFolder = new QPushButton(LS("Browse..."));
    retranslates_.push_back([btnBrowseFolder]{ btnBrowseFolder->setText(LS("Browse...")); });
    btnBrowseFolder->setFixedWidth(80);
    auto* btnScan = new QPushButton(LS("Scan \xe2\x96\xb6"));
    retranslates_.push_back([btnScan]{ btnScan->setText(LS("Scan \xe2\x96\xb6")); });
    btnScan->setFixedWidth(70);
    folderRow->addWidget(btnBrowseFolder);
    folderRow->addWidget(btnScan);
    {
        auto* btn = new QPushButton(LS("?  Help"));
        retranslates_.push_back([btn]{ btn->setText(LS("?  Help")); });
        btn->setFixedWidth(80);
        btn->setToolTip(QStringLiteral("How to use the Group Builder tab"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2a6496;color:white;border-radius:4px;"
            "font-weight:bold;padding:3px 8px;border:none;}"
            "QPushButton:hover{background:#337ab7;}"));
        connect(btn, &QPushButton::clicked, this, [this]{
            QMessageBox mb(this);
            mb.setWindowTitle(LS("Group Builder -- Help"));
            mb.setIcon(QMessageBox::Information);
            mb.setText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.groupbuilder.text")
                : QStringLiteral(
                    "Scans a folder of symbol images, groups them manually or with AI,\n"
                    "and generates the symbol_groups JSON used by the Symbol Groups tab.\n"
                    "\n"
                    "QUICK START (AI vision grouping)\n"
                    "  1. Set Source Folder and click Scan.\n"
                    "  2. Set Style Reference to a sample of your target art style.\n"
                    "  3. Select backend 'Ollama (local, free)'.\n"
                    "  4. Set the server URL (default: http://localhost:11434).\n"
                    "  5. Click Auto-Group by Vision.\n"
                    "  6. Review groups/prompts; edit as needed.\n"
                    "  7. Click '-> Open in Symbol Groups'."));
            mb.setDetailedText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.groupbuilder.details")
                : QStringLiteral(
                    "GROUPING METHODS\n"
                    "  Auto-Group by Vision   -- AI analyses each image semantically and\n"
                    "                           assigns group types (WILD, High Value,\n"
                    "                           Card Letters, Bonus) with per-image prompts.\n"
                    "  Auto-Group by Prefix   -- Groups images by leading filename letters\n"
                    "                           (e.g. H1.png, H2.png -> group 'H').\n"
                    "  New Group from Selected -- Select images on the left, click this\n"
                    "                           button to add them as a new group.\n"
                    "\n"
                    "VISION BACKENDS\n"
                    "  Ollama (local, free)\n"
                    "    Install:      https://ollama.com\n"
                    "    Pull model:   ollama pull llava\n"
                    "    Start server: ollama serve\n"
                    "    Python pkg:   pip install ollama\n"
                    "    URL format:   http://<host>:11434\n"
                    "\n"
                    "  OpenAI (gpt-4o)\n"
                    "    Set env var:  OPENAI_API_KEY=sk-...\n"
                    "    Python pkg:   pip install openai\n"
                    "\n"
                    "  Anthropic (Claude)\n"
                    "    Set env var:  ANTHROPIC_API_KEY=sk-ant-...\n"
                    "    Python pkg:   pip install anthropic\n"
                    "\n"
                    "COMMON ISSUES\n"
                    "  'ollama package not found'  -- pip install ollama\n"
                    "  'No images found'           -- Folder must have PNG/JPG/WEBP files.\n"
                    "  Empty prompts after vision  -- JSON parse failed; try again or switch\n"
                    "                                 backend.\n"
                    "  Ollama not responding       -- Run: ollama serve  (check the URL).\n"
                    "  Wrong groupings             -- Edit group names/items manually or re-run."));
            mb.exec();
        });
        folderRow->addWidget(btn);
    }
    root->addLayout(folderRow);

    connect(btnBrowseFolder, &QPushButton::clicked, this, &GroupBuilderWidget::onBrowseFolder);
    connect(btnScan,         &QPushButton::clicked, this, &GroupBuilderWidget::onScan);

    // ── Row 2: style reference ────────────────────────────────────────────
    auto* refRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Style Reference:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Style Reference:")); });
        refRow->addWidget(lbl);
    }
    edtStyleRef_ = new QLineEdit;
    edtStyleRef_->setPlaceholderText(LS("Reference image \xe2\x80\x94 defines the target art style (optional)"));
    refRow->addWidget(edtStyleRef_, 1);
    btnBrowseStyleRef_ = new QPushButton(LS("Browse..."));
    retranslates_.push_back([this]{ btnBrowseStyleRef_->setText(LS("Browse...")); });
    btnBrowseStyleRef_->setFixedWidth(80);
    refRow->addWidget(btnBrowseStyleRef_);
    root->addLayout(refRow);

    // ── Row 3: style directive ────────────────────────────────────────────
    auto* directiveRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Style Directive:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Style Directive:")); });
        directiveRow->addWidget(lbl);
    }
    edtStyleDirective_ = new QLineEdit;
    edtStyleDirective_->setPlaceholderText(LS(
        "Describe the transformation, e.g. \xe2\x80\x9cmake humans into animals\xe2\x80\x9d, "
        "\xe2\x80\x9csteampunk style\xe2\x80\x9d, \xe2\x80\x9cunderwater theme\xe2\x80\x9d \xe2\x80\x94 "
        "used by AI to generate prompts"));
    directiveRow->addWidget(edtStyleDirective_, 1);
    root->addLayout(directiveRow);

    // ── Row 4: backend selector + vision button ───────────────────────────
    auto* visionRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Vision Backend:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Vision Backend:")); });
        visionRow->addWidget(lbl);
    }
    cbxBackend_ = new QComboBox;
    cbxBackend_->addItem(LS("Ollama (local, free)"),  QStringLiteral("ollama"));
    cbxBackend_->addItem(QStringLiteral("OpenAI (gpt-4o)"),       QStringLiteral("openai"));
    cbxBackend_->addItem(QStringLiteral("Anthropic (Claude)"),    QStringLiteral("anthropic"));
    cbxBackend_->addItem(QStringLiteral("LLaMA.cpp (local)"),     QStringLiteral("llama"));
    retranslates_.push_back([this]{
        cbxBackend_->setItemText(0, LS("Ollama (local, free)"));
    });
    cbxBackend_->setFixedWidth(160);
    visionRow->addWidget(cbxBackend_);

    {
        auto* lbl = new QLabel(LS("URL:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("URL:")); });
        visionRow->addWidget(lbl);
    }
    cbxApiUrl_ = new QComboBox;
    cbxApiUrl_->setEditable(true);
    cbxApiUrl_->setInsertPolicy(QComboBox::NoInsert);
    cbxApiUrl_->setFixedWidth(230);
    cbxApiUrl_->setToolTip(QStringLiteral(
        "API server address — select from history or type a new one.\n"
        "Ollama native:     http://localhost:11434\n"
        "OpenAI-compat:     http://localhost:11434/v1\n"
        "LM Studio default: http://127.0.0.1:1234/v1\n"
        "Leave empty to use the backend default."));

    {
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        QStringList history = s.value(QStringLiteral("GroupBuilder/urlHistory")).toStringList();
        const QString defaultUrl = QStringLiteral("http://localhost:11434");
        history.removeAll(defaultUrl);
        history.prepend(defaultUrl);
        for (const auto& url : history)
            cbxApiUrl_->addItem(url);
        cbxApiUrl_->setCurrentIndex(0);
    }
    visionRow->addWidget(cbxApiUrl_);

    btnCheckBackend_ = new QPushButton(LS("Check"));
    retranslates_.push_back([this]{ btnCheckBackend_->setText(LS("Check")); });
    btnCheckBackend_->setFixedWidth(54);
    btnCheckBackend_->setToolTip(QStringLiteral("Test whether the vision backend URL is reachable"));
    visionRow->addWidget(btnCheckBackend_);

    auto updateUrlPlaceholder = [this](int) {
        const QString b = cbxBackend_->currentData().toString();
        QString ph;
        if      (b == QStringLiteral("ollama"))    ph = QStringLiteral("http://localhost:11434");
        else if (b == QStringLiteral("openai"))    ph = QStringLiteral("https://api.openai.com/v1");
        else if (b == QStringLiteral("llama"))     ph = QStringLiteral("http://127.0.0.1:8666/v1");
        else                                       ph = QStringLiteral("https://api.anthropic.com");
        cbxApiUrl_->lineEdit()->setPlaceholderText(ph);
    };
    connect(cbxBackend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, updateUrlPlaceholder);

    {
        auto* lbl = new QLabel(LS("Model:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Model:")); });
        visionRow->addWidget(lbl);
    }
    edtModel_ = new QLineEdit;
    edtModel_->setPlaceholderText(QStringLiteral("default  (llava / gpt-4o / claude-sonnet-4-6)"));
    edtModel_->setFixedWidth(200);
    visionRow->addWidget(edtModel_);
    btnVisionGroup_ = new QPushButton(LS("Auto-Group by Vision \xe2\x96\xb6"));
    retranslates_.push_back([this]{ btnVisionGroup_->setText(LS("Auto-Group by Vision \xe2\x96\xb6")); });
    btnVisionGroup_->setToolTip(QStringLiteral(
        "Analyse all scanned images with the selected vision model.\n"
        "Groups them semantically and writes a prompt for each symbol.\n\n"
        "Ollama (free/local): install from ollama.com, then run:\n"
        "  ollama pull llava\n"
        "OpenAI: set OPENAI_API_KEY in your environment.\n"
        "Anthropic: set ANTHROPIC_API_KEY in your environment."));
    visionRow->addWidget(btnVisionGroup_);
    lblVisionStatus_ = new QLabel;
    lblVisionStatus_->setStyleSheet(QStringLiteral("color: #aaa; font-style: italic;"));
    visionRow->addWidget(lblVisionStatus_, 1);
    root->addLayout(visionRow);

    connect(btnBrowseStyleRef_,  &QPushButton::clicked, this, &GroupBuilderWidget::onBrowseStyleRef);
    connect(btnVisionGroup_,     &QPushButton::clicked, this, &GroupBuilderWidget::onVisionGroup);
    connect(btnCheckBackend_,    &QPushButton::clicked, this, &GroupBuilderWidget::onCheckBackend);

    // ── Main 2-panel splitter ─────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    // ── Left: file list ───────────────────────────────────────────────────
    auto* leftBox = new QGroupBox(QStringLiteral("Found Images"));
    auto* leftLay = new QVBoxLayout(leftBox);

    wFiles_ = new QListWidget;
    leftLay->addWidget(wFiles_, 1);

    lblPreview_ = new QLabel;
    lblPreview_->setFixedHeight(120);
    lblPreview_->setAlignment(Qt::AlignCenter);
    lblPreview_->setStyleSheet(QStringLiteral(
        "background:#1a1a1a;border:1px solid #444;border-radius:3px;color:#666;"));
    lblPreview_->setText(QStringLiteral("No preview"));
    leftLay->addWidget(lblPreview_);

    auto* selRow = new QHBoxLayout;
    auto* btnSelAll  = new QPushButton(LS("\xe2\x98\x91 All"));
    retranslates_.push_back([btnSelAll]{ btnSelAll->setText(LS("\xe2\x98\x91 All")); });
    auto* btnSelNone = new QPushButton(LS("\xe2\x98\x90 None"));
    retranslates_.push_back([btnSelNone]{ btnSelNone->setText(LS("\xe2\x98\x90 None")); });
    btnSelAll->setFixedHeight(22);
    btnSelNone->setFixedHeight(22);
    selRow->addWidget(btnSelAll);
    selRow->addWidget(btnSelNone);
    selRow->addStretch();
    leftLay->addLayout(selRow);

    auto* actRow = new QHBoxLayout;
    auto* btnNewGroup  = new QPushButton(LS("New Group from Selected \xe2\x96\xb6"));
    retranslates_.push_back([btnNewGroup]{ btnNewGroup->setText(LS("New Group from Selected \xe2\x96\xb6")); });
    auto* btnAutoGroup = new QPushButton(LS("Auto-Group by Prefix \xe2\x96\xb6"));
    retranslates_.push_back([btnAutoGroup]{ btnAutoGroup->setText(LS("Auto-Group by Prefix \xe2\x96\xb6")); });
    btnAutoGroup->setToolTip(QStringLiteral(
        "Automatically create one group per leading-letter prefix.\n"
        "E.g. H1-H4 -> group \"H\", L1-L4 -> group \"L\", W1 -> group \"W\"."));
    actRow->addWidget(btnNewGroup);
    actRow->addWidget(btnAutoGroup);
    leftLay->addLayout(actRow);

    splitter->addWidget(leftBox);

    connect(wFiles_, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem* cur, QListWidgetItem*) {
        if (!cur) {
            lblPreview_->setPixmap(QPixmap());
            lblPreview_->setText(QStringLiteral("No preview"));
            return;
        }
        const QPixmap px(cur->data(Qt::UserRole).toString());
        if (px.isNull()) {
            lblPreview_->setPixmap(QPixmap());
            lblPreview_->setText(QStringLiteral("No preview"));
            return;
        }
        lblPreview_->setText({});
        lblPreview_->setPixmap(
            px.scaled(lblPreview_->width(), lblPreview_->height(),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    connect(btnSelAll,  &QPushButton::clicked, this, [this]{
        for (int i = 0; i < wFiles_->count(); ++i)
            wFiles_->item(i)->setCheckState(Qt::Checked);
    });
    connect(btnSelNone, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < wFiles_->count(); ++i)
            wFiles_->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(btnNewGroup,  &QPushButton::clicked, this, &GroupBuilderWidget::onNewGroupFromSelected);
    connect(btnAutoGroup, &QPushButton::clicked, this, &GroupBuilderWidget::onAutoGroupByPrefix);

    // ── Right: group editor ───────────────────────────────────────────────
    auto* rightWidget = new QWidget;
    auto* rightLay    = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(4);

    auto* grpListBox = new QGroupBox(LS("Groups"));
    retranslates_.push_back([grpListBox]{ grpListBox->setTitle(LS("Groups")); });
    auto* grpListLay = new QVBoxLayout(grpListBox);
    wGroups_ = new QListWidget;
    grpListLay->addWidget(wGroups_, 1);
    btnDeleteGroup_ = new QPushButton(LS("Delete Group"));
    retranslates_.push_back([this]{ btnDeleteGroup_->setText(LS("Delete Group")); });
    btnDeleteGroup_->setEnabled(false);
    grpListLay->addWidget(btnDeleteGroup_);
    rightLay->addWidget(grpListBox, 1);

    connect(wGroups_, &QListWidget::currentRowChanged,
            this, &GroupBuilderWidget::onGroupRowChanged);
    connect(btnDeleteGroup_, &QPushButton::clicked,
            this, &GroupBuilderWidget::onDeleteGroup);

    auto* detailBox = new QGroupBox(LS("Group Settings"));
    retranslates_.push_back([detailBox]{ detailBox->setTitle(LS("Group Settings")); });
    auto* grid = new QGridLayout(detailBox);
    grid->setColumnStretch(1, 1);

    {
        auto* lbl = new QLabel(LS("Group Name:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Group Name:")); });
        grid->addWidget(lbl, 0, 0);
    }
    edtGroupName_ = new QLineEdit;
    grid->addWidget(edtGroupName_, 0, 1, 1, 2);

    {
        auto* lbl = new QLabel(LS("Reference:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Reference:")); });
        grid->addWidget(lbl, 1, 0);
    }
    edtRef_ = new QLineEdit;
    edtRef_->setPlaceholderText(QStringLiteral("(optional)"));
    grid->addWidget(edtRef_, 1, 1);
    btnBrowseRef_ = new QPushButton(LS("Browse..."));
    retranslates_.push_back([this]{ btnBrowseRef_->setText(LS("Browse...")); });
    btnBrowseRef_->setFixedWidth(70);
    grid->addWidget(btnBrowseRef_, 1, 2);

    auto* paramRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("IP Weight:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("IP Weight:")); });
        paramRow->addWidget(lbl);
    }
    spnIp_ = new QDoubleSpinBox;
    spnIp_->setRange(0.0, 1.0); spnIp_->setSingleStep(0.05); spnIp_->setDecimals(2);
    spnIp_->setValue(0.75);
    paramRow->addWidget(spnIp_);
    paramRow->addSpacing(12);
    {
        auto* lbl = new QLabel(LS("Denoise:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Denoise:")); });
        paramRow->addWidget(lbl);
    }
    spnDen_ = new QDoubleSpinBox;
    spnDen_->setRange(0.0, 1.0); spnDen_->setSingleStep(0.05); spnDen_->setDecimals(2);
    spnDen_->setValue(0.80);
    paramRow->addWidget(spnDen_);
    paramRow->addStretch();
    grid->addLayout(paramRow, 2, 0, 1, 3);

    {
        auto* lbl = new QLabel(LS("Positive Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Positive Prompt:")); });
        grid->addWidget(lbl, 3, 0, Qt::AlignTop);
    }
    edtPrompt_ = new QPlainTextEdit;
    edtPrompt_->setFixedHeight(64);
    edtPrompt_->setPlaceholderText(QStringLiteral(
        "Default prompt applied to every item in this group.\n"
        "You can override per-item later in the Symbol Groups tab."));
    grid->addWidget(edtPrompt_, 3, 1, 1, 2);

    {
        auto* lbl = new QLabel(LS("Negative Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Negative Prompt:")); });
        grid->addWidget(lbl, 4, 0, Qt::AlignTop);
    }
    edtNeg_ = new QPlainTextEdit;
    edtNeg_->setFixedHeight(48);
    edtNeg_->setPlaceholderText(QStringLiteral("Default negative prompt..."));
    grid->addWidget(edtNeg_, 4, 1, 1, 2);

    rightLay->addWidget(detailBox);
    splitter->addWidget(rightWidget);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    // ── Log panel ─────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(LS("Log"));
    retranslates_.push_back([logBox]{ logBox->setTitle(LS("Log")); });
    auto* logLay = new QVBoxLayout(logBox);
    {
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        logView_ = new QPlainTextEdit;
        logView_->setReadOnly(true);
        logView_->setMaximumBlockCount(2000);
        logView_->setFont(mono);
        logView_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:#1e1e1e;color:#d4d4d4;}"));
        logLay->addWidget(logView_);
    }
    {
        auto* row = new QHBoxLayout;
        row->addStretch();
        auto* btnClear = new QPushButton(LS("Clear"));
        retranslates_.push_back([btnClear]{ btnClear->setText(LS("Clear")); });
        connect(btnClear, &QPushButton::clicked, this, [this]{ logView_->clear(); });
        row->addWidget(btnClear);
        logLay->addLayout(row);
    }

    auto* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->addWidget(splitter);
    vSplitter->addWidget(logBox);
    vSplitter->setStretchFactor(0, 3);
    vSplitter->setStretchFactor(1, 1);
    root->addWidget(vSplitter, 1);

    // ── Bottom action row ─────────────────────────────────────────────────
    auto* bottomRow = new QHBoxLayout;
    auto* btnExport = new QPushButton(LS("Export JSON..."));
    retranslates_.push_back([btnExport]{ btnExport->setText(LS("Export JSON...")); });
    btnSend_ = new QPushButton(LS("\xe2\x86\x92 Open in Symbol Groups"));
    retranslates_.push_back([this]{ btnSend_->setText(LS("\xe2\x86\x92 Open in Symbol Groups")); });
    btnSend_->setToolTip(QStringLiteral(
        "Load these groups directly into the Symbol Groups tab\n"
        "for per-item prompt review and editing."));
    btnSend_->setEnabled(false);
    bottomRow->addStretch();
    bottomRow->addWidget(btnExport);
    bottomRow->addWidget(btnSend_);
    root->addLayout(bottomRow);

    connect(edtGroupName_, &QLineEdit::textEdited,
            this, &GroupBuilderWidget::onGroupNameEdited);
    connect(edtRef_,       &QLineEdit::textEdited,
            this, &GroupBuilderWidget::onRefEdited);
    connect(btnBrowseRef_, &QPushButton::clicked,
            this, &GroupBuilderWidget::onBrowseRef);
    connect(spnIp_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){
                if (!busy_ && curGroup_ >= 0 && curGroup_ < groups_.size())
                    groups_[curGroup_].ipWeight = v;
            });
    connect(spnDen_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v){
                if (!busy_ && curGroup_ >= 0 && curGroup_ < groups_.size())
                    groups_[curGroup_].denoise = v;
            });
    connect(edtPrompt_, &QPlainTextEdit::textChanged, this, [this]{
        if (busy_ || curGroup_ < 0 || curGroup_ >= groups_.size()) return;
        const QString p = edtPrompt_->toPlainText();
        for (auto& item : groups_[curGroup_].items) item.prompt = p;
    });
    connect(edtNeg_, &QPlainTextEdit::textChanged, this, [this]{
        if (busy_ || curGroup_ < 0 || curGroup_ >= groups_.size()) return;
        const QString n = edtNeg_->toPlainText();
        for (auto& item : groups_[curGroup_].items) item.negativePrompt = n;
    });

    connect(btnExport, &QPushButton::clicked, this, &GroupBuilderWidget::onExportJson);
    connect(btnSend_,  &QPushButton::clicked, this, &GroupBuilderWidget::onSendToSymbolGroups);

    setDetailEnabled(false);
}

void GroupBuilderWidget::retranslateUi()
{
    for (auto& fn : retranslates_)
        fn();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

QStringList GroupBuilderWidget::checkedFilePaths() const
{
    QStringList out;
    for (int i = 0; i < wFiles_->count(); ++i) {
        auto* it = wFiles_->item(i);
        if (it->checkState() == Qt::Checked)
            out << it->data(Qt::UserRole).toString();
    }
    return out;
}

void GroupBuilderWidget::setDetailEnabled(bool v)
{
    edtGroupName_->setEnabled(v);
    edtRef_->setEnabled(v);
    btnBrowseRef_->setEnabled(v);
    spnIp_->setEnabled(v);
    spnDen_->setEnabled(v);
    edtPrompt_->setEnabled(v);
    edtNeg_->setEnabled(v);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void GroupBuilderWidget::onBrowseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Source Folder"), LastDir::get());
    if (!dir.isEmpty()) {
        LastDir::set(dir);
        edtFolder_->setText(dir);
        onScan();
    }
}

void GroupBuilderWidget::onScan()
{
    const QString folderPath = edtFolder_->text().trimmed();
    if (folderPath.isEmpty()) return;

    QDir rootDir(folderPath);
    if (!rootDir.exists()) return;

    QStringList nameFilters;
    for (const auto& ext : IMAGE_EXTS)
        nameFilters << QStringLiteral("*.") + ext
                    << QStringLiteral("*.") + ext.toUpper();

    // Recursively collect all image files under folderPath
    QStringList absPaths;
    QStringList pending;
    pending << folderPath;
    while (!pending.isEmpty()) {
        QDir cur(pending.takeLast());
        for (const auto& fn : cur.entryList(nameFilters, QDir::Files))
            absPaths << cur.absoluteFilePath(fn);
        for (const auto& sub : cur.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            pending << cur.absoluteFilePath(sub);
    }
    absPaths.sort();

    wFiles_->clear();
    for (const auto& absPath : absPaths) {
        const QString label = rootDir.relativeFilePath(absPath);
        auto* it = new QListWidgetItem(label, wFiles_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        it->setData(Qt::UserRole, absPath);
    }

    groups_.clear();
    curGroup_ = -1;
    refreshGroupList();

    appendLog(QStringLiteral("Scanned %1 image(s) from %2")
              .arg(absPaths.size())
              .arg(QDir::toNativeSeparators(folderPath)));
}

void GroupBuilderWidget::setPythonExe(const QString& exe)   { pythonExe_   = exe;  }
void GroupBuilderWidget::setProjectRoot(const QString& root) { projectRoot_ = root; }

void GroupBuilderWidget::onBrowseStyleRef()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select Style Reference Image"),
        LastDir::get(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp);;All Files (*)"));
    if (!path.isEmpty()) { LastDir::set(path); edtStyleRef_->setText(path); }
}

void GroupBuilderWidget::onVisionGroup()
{
    const QString folder = edtFolder_->text().trimmed();
    if (folder.isEmpty() || wFiles_->count() == 0) {
        QMessageBox::information(this, QStringLiteral("No Images"),
            QStringLiteral("Scan a folder containing symbol images first."));
        return;
    }
    if (analysisProcess_ && analysisProcess_->state() != QProcess::NotRunning)
        return;   // already running

    const QString scriptPath = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/_pipeline/tools/auto_group_symbols.py"));
    if (!QFile::exists(scriptPath)) {
        QMessageBox::warning(this, QStringLiteral("Script Not Found"),
            QStringLiteral("Cannot find:\n") + scriptPath);
        return;
    }

    // Build output path next to the source folder
    const QString outputPath = QDir::cleanPath(folder + QStringLiteral("/symbol_groups_auto.json"));

    const QString backend = cbxBackend_->currentData().toString();
    const QString apiUrl  = cbxApiUrl_->currentText().trimmed();
    const QString model   = edtModel_->text().trimmed();

    // Persist URL to history (max 10 entries)
    if (!apiUrl.isEmpty()) {
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        QStringList history = s.value(QStringLiteral("GroupBuilder/urlHistory")).toStringList();
        history.removeAll(apiUrl);
        history.prepend(apiUrl);
        if (history.size() > 10) history = history.mid(0, 10);
        s.setValue(QStringLiteral("GroupBuilder/urlHistory"), history);

        QSignalBlocker blk(cbxApiUrl_);
        cbxApiUrl_->clear();
        for (const auto& url : history) cbxApiUrl_->addItem(url);
        cbxApiUrl_->setCurrentText(apiUrl);
    }

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--folder")  << folder
         << QStringLiteral("--output")  << outputPath
         << QStringLiteral("--backend") << backend;

    if (!apiUrl.isEmpty())
        args << QStringLiteral("--api-url") << apiUrl;
    if (!model.isEmpty())
        args << QStringLiteral("--model") << model;

    const QString ref = edtStyleRef_->text().trimmed();
    if (!ref.isEmpty() && QFile::exists(ref))
        args << QStringLiteral("--reference") << ref;

    const QString directive = edtStyleDirective_->text().trimmed();
    if (!directive.isEmpty())
        args << QStringLiteral("--style-directive") << directive;

    if (!analysisProcess_) {
        analysisProcess_ = new QProcess(this);
        connect(analysisProcess_, &QProcess::readyReadStandardOutput,
                this, &GroupBuilderWidget::onAnalysisReadyRead);
        connect(analysisProcess_, &QProcess::readyReadStandardError, this, [this] {
            analysisProcess_->setReadChannel(QProcess::StandardError);
            while (analysisProcess_->canReadLine()) {
                const QString line = QString::fromUtf8(analysisProcess_->readLine()).trimmed();
                if (!line.isEmpty()) {
                    lblVisionStatus_->setText(QStringLiteral("ERR: ") + line);
                    appendLog(line, QColor(255, 140, 0));
                }
            }
        });
        connect(analysisProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &GroupBuilderWidget::onAnalysisFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    analysisProcess_->setProcessEnvironment(env);
    analysisProcess_->setWorkingDirectory(projectRoot_);

    btnVisionGroup_->setEnabled(false);
    lblVisionStatus_->setText(QStringLiteral("Starting..."));
    appendLog(QStringLiteral("=== Vision analysis started [%1] ===").arg(backend),
              QColor(100, 200, 255));
    if (!directive.isEmpty())
        appendLog(QStringLiteral("    Style directive: %1").arg(directive),
                  QColor(180, 180, 180));
    analysisProcess_->start(pythonExe_, args);
}

void GroupBuilderWidget::onAnalysisReadyRead()
{
    analysisProcess_->setReadChannel(QProcess::StandardOutput);
    while (analysisProcess_->canReadLine()) {
        const QString line = QString::fromUtf8(analysisProcess_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QStringLiteral("[PROGRESS]"))) {
            const QString msg = line.mid(10).trimmed();
            lblVisionStatus_->setText(msg);
            appendLog(msg, QColor(255, 193, 7));
        } else if (line.startsWith(QStringLiteral("[DONE]"))) {
            const QString jsonPath = line.mid(6).trimmed();
            lblVisionStatus_->setText(QStringLiteral("Loading results..."));
            appendLog(QStringLiteral("Done \xe2\x80\x94 loading %1").arg(jsonPath),
                      QColor(76, 175, 80));
            if (!loadJsonFile(jsonPath))
                lblVisionStatus_->setText(QStringLiteral("Done but failed to load JSON."));
        } else if (line.startsWith(QStringLiteral("[ERROR]"))) {
            const QString msg = line.mid(7).trimmed();
            lblVisionStatus_->setText(QStringLiteral("Error: ") + msg);
            appendLog(QStringLiteral("Error: ") + msg, QColor(244, 67, 54));
        } else {
            appendLog(line);
        }
    }
}

void GroupBuilderWidget::onAnalysisFinished(int exitCode, QProcess::ExitStatus /*status*/)
{
    onAnalysisReadyRead();   // drain remaining output
    btnVisionGroup_->setEnabled(true);
    if (exitCode == 0) {
        appendLog(QStringLiteral("=== Analysis finished ==="), QColor(76, 175, 80));
    } else if (!lblVisionStatus_->text().startsWith(QStringLiteral("Error"))) {
        const QString msg = QStringLiteral("Analysis failed (exit %1).").arg(exitCode);
        lblVisionStatus_->setText(msg);
        appendLog(msg, QColor(244, 67, 54));
    }
}

void GroupBuilderWidget::onCheckBackend()
{
    const QString url = cbxApiUrl_->currentText().trimmed();
    if (url.isEmpty()) {
        lblVisionStatus_->setText(LS("No URL set"));
        lblVisionStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        return;
    }

    btnCheckBackend_->setEnabled(false);
    lblVisionStatus_->setText(LS("Checking\xe2\x80\xa6"));
    lblVisionStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    appendLog(QStringLiteral("Checking backend: %1").arg(url));

    // Resolve host/port from the URL via Python and do a TCP connect.
    // URL is passed via env var to avoid any shell-injection concern.
    static const char* kScript =
        "import socket,sys,os,urllib.parse;"
        "p=urllib.parse.urlparse(os.environ['CHECK_URL']);"
        "h=p.hostname or 'localhost';"
        "port=p.port or(443 if p.scheme=='https' else 80);"
        "s=socket.socket();"
        "s.settimeout(3);"
        "r=s.connect_ex((h,port));"
        "s.close();"
        "sys.exit(r)";

    auto* proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("CHECK_URL"), url);
    proc->setProcessEnvironment(env);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, url](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            lblVisionStatus_->setText(LS("Backend reachable"));
            lblVisionStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
            appendLog(QStringLiteral("Backend reachable: %1").arg(url), QColor(76, 175, 80));
        } else {
            lblVisionStatus_->setText(LS("Unreachable"));
            lblVisionStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
            appendLog(QStringLiteral("Unreachable: %1").arg(url), QColor(244, 67, 54));
        }
        btnCheckBackend_->setEnabled(true);
        proc->deleteLater();
    });

    proc->start(pythonExe_, { QStringLiteral("-c"), QString::fromLatin1(kScript) });
}

bool GroupBuilderWidget::loadJsonFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    groups_.clear();
    for (const auto& gv : doc.object().value(QStringLiteral("groups")).toArray()) {
        const QJsonObject go = gv.toObject();
        SymbolGroup g;
        g.name          = go.value(QStringLiteral("name")).toString();
        g.referencePath = go.value(QStringLiteral("reference")).toString();
        g.ipWeight      = go.value(QStringLiteral("ip_weight")).toDouble(0.75);
        g.denoise       = go.value(QStringLiteral("denoise")).toDouble(0.80);
        for (const auto& iv : go.value(QStringLiteral("items")).toArray()) {
            const QJsonObject io = iv.toObject();
            SymbolItem item;
            item.filePath       = io.value(QStringLiteral("file")).toString();
            item.prompt         = io.value(QStringLiteral("prompt")).toString();
            item.negativePrompt = io.value(QStringLiteral("negative_prompt")).toString();
            g.items.append(item);
        }
        groups_.append(g);
    }

    curGroup_ = -1;
    refreshGroupList();
    if (!groups_.isEmpty()) {
        wGroups_->setCurrentRow(0);
        lblVisionStatus_->setText(QStringLiteral(
            "%1 group(s) loaded. Edit prompts below, then Export or Send to Symbol Groups.")
                .arg(groups_.size()));
    }
    return true;
}

void GroupBuilderWidget::onNewGroupFromSelected()
{
    const QStringList paths = checkedFilePaths();
    if (paths.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Files Checked"),
            QStringLiteral("Check at least one image in the left list."));
        return;
    }

    SymbolGroup g;
    g.name     = QStringLiteral("Group %1").arg(groups_.size() + 1);
    g.ipWeight = 0.75;
    g.denoise  = 0.80;
    for (const auto& p : paths) {
        SymbolItem item;
        item.filePath = p;
        g.items.append(item);
    }
    groups_.append(g);
    refreshGroupList();
    wGroups_->setCurrentRow(static_cast<int>(groups_.size()) - 1);
    appendLog(QStringLiteral("Created group '%1' with %2 image(s)")
              .arg(g.name).arg(g.items.size()));
}

void GroupBuilderWidget::onAutoGroupByPrefix()
{
    // Gather every file in the left list (regardless of check state)
    QStringList allPaths;
    for (int i = 0; i < wFiles_->count(); ++i)
        allPaths << wFiles_->item(i)->data(Qt::UserRole).toString();

    if (allPaths.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Images"),
            QStringLiteral("Scan a folder first."));
        return;
    }

    // Group by the leading alphabetical prefix (e.g. H1→H, L2→L, WILD1→WILD)
    QMap<QString, QList<QString>> prefixMap;
    for (const auto& path : allPaths) {
        const QString stem = QFileInfo(path).baseName();
        int i = 0;
        while (i < stem.size() && stem[i].isLetter()) ++i;
        const QString prefix = (i > 0 ? stem.left(i) : stem.left(1)).toUpper();
        prefixMap[prefix].append(path);
    }

    groups_.clear();
    for (auto it = prefixMap.cbegin(); it != prefixMap.cend(); ++it) {
        SymbolGroup g;
        g.name     = it.key();
        g.ipWeight = 0.75;
        g.denoise  = 0.80;
        for (const auto& path : it.value()) {
            SymbolItem item;
            item.filePath = path;
            g.items.append(item);
        }
        groups_.append(g);
    }

    curGroup_ = -1;
    refreshGroupList();
    if (!groups_.isEmpty())
        wGroups_->setCurrentRow(0);
    appendLog(QStringLiteral("Auto-grouped by prefix: %1 group(s) from %2 image(s)")
              .arg(groups_.size()).arg(allPaths.size()));
}

void GroupBuilderWidget::onGroupRowChanged(int row)
{
    curGroup_ = row;
    refreshGroupDetail(row);
}

void GroupBuilderWidget::onDeleteGroup()
{
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_.removeAt(curGroup_);
    curGroup_ = -1;
    refreshGroupList();
    setDetailEnabled(false);
}

void GroupBuilderWidget::onGroupNameEdited(const QString& name)
{
    if (busy_ || curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_[curGroup_].name = name;
    // Update the list item text without re-running refreshGroupList
    busy_ = true;
    if (wGroups_->currentItem())
        wGroups_->currentItem()->setText(
            QStringLiteral("%1  (%2 items)").arg(name).arg(groups_[curGroup_].items.size()));
    busy_ = false;
}

void GroupBuilderWidget::onRefEdited(const QString& path)
{
    if (busy_ || curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_[curGroup_].referencePath = path;
}

void GroupBuilderWidget::onBrowseRef()
{
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select Reference Image"),
        LastDir::get(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp);;All Files (*)"));
    if (!path.isEmpty()) {
        LastDir::set(path);
        edtRef_->setText(path);
        groups_[curGroup_].referencePath = path;
    }
}

void GroupBuilderWidget::onExportJson()
{
    if (groups_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Nothing to Export"),
            QStringLiteral("Create at least one group first."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Symbol Groups JSON"),
        LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    QJsonArray arr;
    for (const auto& g : groups_) {
        QJsonObject go;
        go[QStringLiteral("name")]      = g.name;
        go[QStringLiteral("reference")] = g.referencePath;
        go[QStringLiteral("ip_weight")] = g.ipWeight;
        go[QStringLiteral("denoise")]   = g.denoise;

        QJsonArray items;
        for (const auto& item : g.items) {
            QJsonObject io;
            io[QStringLiteral("file")]   = item.filePath;
            io[QStringLiteral("prompt")] = item.prompt;
            if (!item.negativePrompt.isEmpty())
                io[QStringLiteral("negative_prompt")] = item.negativePrompt;
            items.append(io);
        }
        go[QStringLiteral("items")] = items;
        arr.append(go);
    }

    QJsonObject root;
    root[QStringLiteral("groups")] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"),
            QStringLiteral("Cannot write to:\n") + path);
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, QStringLiteral("Exported"),
        QStringLiteral("Saved %1 group(s) to:\n%2").arg(groups_.size()).arg(path));
}

void GroupBuilderWidget::onSendToSymbolGroups()
{
    if (groups_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Nothing to Send"),
            QStringLiteral("Create at least one group first."));
        return;
    }
    emit groupsReadyToLoad(groups_);
}

// ── UI refresh ────────────────────────────────────────────────────────────────

void GroupBuilderWidget::refreshGroupList()
{
    wGroups_->clear();
    for (const auto& g : groups_)
        wGroups_->addItem(
            QStringLiteral("%1  (%2 items)").arg(g.name).arg(g.items.size()));

    const bool hasGroups = !groups_.isEmpty();
    btnDeleteGroup_->setEnabled(hasGroups);
    btnSend_->setEnabled(hasGroups);

    if (!hasGroups) {
        setDetailEnabled(false);
        refreshGroupDetail(-1);
    }
}

// ── Log ───────────────────────────────────────────────────────────────────────

void GroupBuilderWidget::appendLog(const QString& text, const QColor& color)
{
    const QString ts  = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString msg = QStringLiteral("[%1] %2").arg(ts, text);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    QTextCursor cursor(logView_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(msg + QLatin1Char('\n'), fmt);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void GroupBuilderWidget::refreshGroupDetail(int g)
{
    const bool valid = (g >= 0 && g < groups_.size());
    setDetailEnabled(valid);
    busy_ = true;
    if (valid) {
        const auto& grp = groups_[g];
        edtGroupName_->setText(grp.name);
        edtRef_->setText(grp.referencePath);
        spnIp_->setValue(grp.ipWeight);
        spnDen_->setValue(grp.denoise);
        // Show first item's prompt as the group-wide default
        edtPrompt_->setPlainText(grp.items.isEmpty() ? QString() : grp.items.first().prompt);
        edtNeg_->setPlainText(grp.items.isEmpty()    ? QString() : grp.items.first().negativePrompt);
    } else {
        edtGroupName_->clear();
        edtRef_->clear();
        spnIp_->setValue(0.75);
        spnDen_->setValue(0.80);
        edtPrompt_->clear();
        edtNeg_->clear();
    }
    busy_ = false;
}
