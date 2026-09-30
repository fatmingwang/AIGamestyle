#include "SymbolGroupWidget.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QSplitter>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFont>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>

// ── Construction ─────────────────────────────────────────────────────────────

SymbolGroupWidget::SymbolGroupWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// ── UI construction ───────────────────────────────────────────────────────────

void SymbolGroupWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Top bar ───────────────────────────────────────────────────────────
    auto* topBar = new QHBoxLayout;
    auto* btnImport = new QPushButton(LS("Import JSON..."));
    retranslates_.push_back([btnImport]{ btnImport->setText(LS("Import JSON...")); });
    auto* btnExport = new QPushButton(LS("Export JSON..."));
    retranslates_.push_back([btnExport]{ btnExport->setText(LS("Export JSON...")); });
    btnExport->setEnabled(false);
    lblFile_ = new QLabel(LS("(no file loaded)"));
    lblFile_->setStyleSheet(QStringLiteral("color: #aaa; font-style: italic;"));
    topBar->addWidget(btnImport);
    topBar->addWidget(btnExport);
    topBar->addSpacing(12);
    {
        auto* lbl = new QLabel(LS("File:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("File:")); });
        topBar->addWidget(lbl);
    }
    topBar->addWidget(lblFile_);
    topBar->addStretch();
    {
        auto* btn = new QPushButton(LS("?  Help"));
        retranslates_.push_back([btn]{ btn->setText(LS("?  Help")); });
        btn->setFixedWidth(80);
        btn->setToolTip(QStringLiteral("How to use the Symbol Groups tab"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2a6496;color:white;border-radius:4px;"
            "font-weight:bold;padding:3px 8px;border:none;}"
            "QPushButton:hover{background:#337ab7;}"));
        connect(btn, &QPushButton::clicked, this, [this]{
            QMessageBox mb(this);
            mb.setWindowTitle(LS("Symbol Groups -- Help"));
            mb.setIcon(QMessageBox::Information);
            mb.setText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.symgroup.text")
                : QStringLiteral(
                    "Manages groups of game symbols. Each group has its own style\n"
                    "reference image and settings; each symbol has its own AI prompt.\n"
                    "\n"
                    "QUICK START\n"
                    "  1. Click Import JSON to load a symbol_groups file.\n"
                    "     (Generate one using the Group Builder tab.)\n"
                    "  2. Check/uncheck groups in the left panel.\n"
                    "  3. Select a group to adjust Reference, IP Weight, Denoise.\n"
                    "  4. Select an item to view or edit its per-symbol prompts.\n"
                    "  5a. 'Apply -> Pipeline'  -- fills Pipeline tab for manual control.\n"
                    "  5b. 'Run Checked [>]'    -- runs all checked groups with one click."));
            mb.setDetailedText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.symgroup.details")
                : QStringLiteral(
                    "PANELS\n"
                    "  Groups (left)    -- Checkbox list; checked groups run together.\n"
                    "  Group Settings   -- Reference image, IP weight, denoise per group.\n"
                    "  Items            -- Files that belong to the selected group.\n"
                    "  Item Detail      -- Per-symbol positive and negative prompts.\n"
                    "\n"
                    "BUTTONS\n"
                    "  [x] All / [ ] None    -- Select or deselect all groups.\n"
                    "  Apply -> Pipeline     -- Copies this group's settings to the Pipeline tab.\n"
                    "  Run Checked [>] (N)   -- Runs all N checked groups through ComfyUI.\n"
                    "                          Output goes to one timestamped folder.\n"
                    "\n"
                    "COMMON ISSUES\n"
                    "  No groups shown      -- Import a JSON file or build one in Group Builder.\n"
                    "  Run button disabled  -- No checked groups have any items.\n"
                    "  ComfyUI errors       -- Make sure ComfyUI is running (Pipeline -> Launch)."));
            mb.exec();
        });
        topBar->addWidget(btn);
    }
    root->addLayout(topBar);

    connect(btnImport, &QPushButton::clicked, this, &SymbolGroupWidget::onImportJson);
    connect(btnExport, &QPushButton::clicked, this, &SymbolGroupWidget::onExportJson);

    connect(this, &SymbolGroupWidget::applyGroupRequested,
            btnExport, [btnExport]{ btnExport->setEnabled(true); });
    connect(btnImport, &QPushButton::clicked, btnExport,
            [btnExport]{ btnExport->setEnabled(true); });

    // ── 3-column splitter ─────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    // ── Left: Group list ──────────────────────────────────────────────────
    auto* leftBox = new QGroupBox(LS("Groups"));
    retranslates_.push_back([leftBox]{ leftBox->setTitle(LS("Groups")); });
    auto* leftLay = new QVBoxLayout(leftBox);

    auto* checkRow = new QHBoxLayout;
    auto* btnCheckAll   = new QPushButton(LS("\xe2\x98\x91 All"));
    retranslates_.push_back([btnCheckAll]{ btnCheckAll->setText(LS("\xe2\x98\x91 All")); });
    auto* btnUncheckAll = new QPushButton(LS("\xe2\x98\x90 None"));
    retranslates_.push_back([btnUncheckAll]{ btnUncheckAll->setText(LS("\xe2\x98\x90 None")); });
    btnCheckAll->setFixedHeight(22);
    btnUncheckAll->setFixedHeight(22);
    checkRow->addWidget(btnCheckAll);
    checkRow->addWidget(btnUncheckAll);
    leftLay->addLayout(checkRow);

    wGroups_ = new QListWidget;
    wGroups_->setMinimumWidth(120);
    leftLay->addWidget(wGroups_);
    splitter->addWidget(leftBox);

    connect(wGroups_, &QListWidget::currentRowChanged,
            this, &SymbolGroupWidget::onGroupChanged);
    connect(wGroups_, &QListWidget::itemChanged,
            this, [this](QListWidgetItem*) { updateRunButton(); });

    connect(btnCheckAll, &QPushButton::clicked, this, [this] {
        QSignalBlocker blk(wGroups_);
        for (int i = 0; i < wGroups_->count(); ++i)
            wGroups_->item(i)->setCheckState(Qt::Checked);
        updateRunButton();
    });
    connect(btnUncheckAll, &QPushButton::clicked, this, [this] {
        QSignalBlocker blk(wGroups_);
        for (int i = 0; i < wGroups_->count(); ++i)
            wGroups_->item(i)->setCheckState(Qt::Unchecked);
        updateRunButton();
    });

    // ── Center: Group settings + item list ───────────────────────────────
    auto* centerWidget = new QWidget;
    auto* centerLay = new QVBoxLayout(centerWidget);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(4);

    auto* settingsBox = new QGroupBox(LS("Group Settings"));
    retranslates_.push_back([settingsBox]{ settingsBox->setTitle(LS("Group Settings")); });
    auto* grid = new QGridLayout(settingsBox);
    grid->setColumnStretch(1, 1);

    {
        auto* lbl = new QLabel(LS("Reference:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Reference:")); });
        grid->addWidget(lbl, 0, 0);
    }
    edtRef_ = new QLineEdit;
    edtRef_->setPlaceholderText(LS("Path to style reference image"));
    grid->addWidget(edtRef_, 0, 1);
    auto* btnBrowse = new QPushButton(LS("Browse..."));
    retranslates_.push_back([btnBrowse]{ btnBrowse->setText(LS("Browse...")); });
    btnBrowse->setFixedWidth(80);
    grid->addWidget(btnBrowse, 0, 2);

    lblThumb_ = new QLabel;
    lblThumb_->setFixedSize(80, 80);
    lblThumb_->setAlignment(Qt::AlignCenter);
    lblThumb_->setStyleSheet(QStringLiteral(
        "QLabel { background: #2a2a2a; border: 1px solid #555; color: #777; }"));
    lblThumb_->setText(QStringLiteral("No\nimage"));
    grid->addWidget(lblThumb_, 0, 3, 2, 1);

    auto* paramRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("IP Weight:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("IP Weight:")); });
        paramRow->addWidget(lbl);
    }
    spnIp_ = new QDoubleSpinBox;
    spnIp_->setRange(0.0, 1.0);
    spnIp_->setSingleStep(0.05);
    spnIp_->setDecimals(2);
    spnIp_->setValue(0.75);
    paramRow->addWidget(spnIp_);
    paramRow->addSpacing(16);
    {
        auto* lbl = new QLabel(LS("Denoise:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Denoise:")); });
        paramRow->addWidget(lbl);
    }
    spnDen_ = new QDoubleSpinBox;
    spnDen_->setRange(0.0, 1.0);
    spnDen_->setSingleStep(0.05);
    spnDen_->setDecimals(2);
    spnDen_->setValue(0.80);
    paramRow->addWidget(spnDen_);
    paramRow->addStretch();
    grid->addLayout(paramRow, 1, 0, 1, 3);

    // ── Script selector ───────────────────────────────────────────────────
    auto* scriptRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Script:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Script:")); });
        scriptRow->addWidget(lbl);
    }
    cbxScript_ = new QComboBox;
    cbxScript_->addItem(QStringLiteral("Image with Prompt"),
                        QStringLiteral("imagewithprompt"));
    cbxScript_->addItem(QStringLiteral("Batch Reskin"),
                        QStringLiteral("batchreskin"));
    cbxScript_->setToolTip(QStringLiteral(
        "Image with Prompt — run_imagewithprompt.py\n"
        "  Sends each symbol through the ComfyUI image-with-prompt workflow.\n\n"
        "Batch Reskin — batch_reskin.py\n"
        "  Runs the classic batch reskin on the symbol's parent folder."));
    scriptRow->addWidget(cbxScript_);
    scriptRow->addStretch();
    grid->addLayout(scriptRow, 2, 0, 1, 3);

    auto* actionRow = new QHBoxLayout;
    btnApply_ = new QPushButton(LS("Apply \xe2\x86\x92 Pipeline"));
    retranslates_.push_back([this]{ btnApply_->setText(LS("Apply \xe2\x86\x92 Pipeline")); });
    btnApply_->setEnabled(false);
    actionRow->addWidget(btnApply_);

    btnRun_ = new QPushButton(LS("Run Checked \xe2\x96\xb6"));
    retranslates_.push_back([this]{ updateRunButton(); });
    btnRun_->setEnabled(false);
    actionRow->addWidget(btnRun_);
    grid->addLayout(actionRow, 3, 0, 1, 4);

    connect(btnBrowse,  &QPushButton::clicked,       this, &SymbolGroupWidget::onBrowseReference);
    connect(edtRef_,    &QLineEdit::textChanged,      this, &SymbolGroupWidget::onReferenceEdited);
    connect(spnIp_,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SymbolGroupWidget::onIpWeightChanged);
    connect(spnDen_,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SymbolGroupWidget::onDenoiseChanged);

    connect(btnApply_, &QPushButton::clicked, this, [this] {
        if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
        emit applyGroupRequested(groups_[curGroup_]);
    });

    connect(btnRun_, &QPushButton::clicked, this, [this] {
        QList<SymbolGroup> selected;
        for (int i = 0; i < wGroups_->count(); ++i) {
            if (wGroups_->item(i)->checkState() == Qt::Checked && i < groups_.size())
                selected.append(groups_[i]);
        }
        if (selected.isEmpty()) return;
        btnRun_->setEnabled(false);
        emit runGroupsRequested(selected, cbxScript_->currentData().toString());
    });

    centerLay->addWidget(settingsBox);

    auto* itemsBox = new QGroupBox(LS("Items"));
    retranslates_.push_back([itemsBox]{ itemsBox->setTitle(LS("Items")); });
    auto* itemsLay = new QVBoxLayout(itemsBox);
    wItems_ = new QListWidget;
    itemsLay->addWidget(wItems_);
    centerLay->addWidget(itemsBox, 1);

    connect(wItems_, &QListWidget::currentRowChanged,
            this, &SymbolGroupWidget::onItemChanged);

    splitter->addWidget(centerWidget);

    // ── Right: Item detail ────────────────────────────────────────────────
    auto* detailBox = new QGroupBox(LS("Item Detail"));
    retranslates_.push_back([detailBox]{ detailBox->setTitle(LS("Item Detail")); });
    auto* detailLay = new QVBoxLayout(detailBox);

    lblItemFile_ = new QLabel(LS("(select an item)"));
    lblItemFile_->setStyleSheet(QStringLiteral("color: #aaa; font-style: italic;"));
    detailLay->addWidget(lblItemFile_);

    lblItemPreview_ = new QLabel;
    lblItemPreview_->setFixedHeight(120);
    lblItemPreview_->setAlignment(Qt::AlignCenter);
    lblItemPreview_->setStyleSheet(QStringLiteral(
        "background:#1a1a1a;border:1px solid #444;border-radius:3px;color:#666;"));
    lblItemPreview_->setText(QStringLiteral("No preview"));
    detailLay->addWidget(lblItemPreview_);

    {
        auto* lbl = new QLabel(LS("Positive Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Positive Prompt:")); });
        detailLay->addWidget(lbl);
    }
    edtPrompt_ = new QPlainTextEdit;
    edtPrompt_->setPlaceholderText(LS("Custom positive prompt for this symbol..."));
    detailLay->addWidget(edtPrompt_, 3);

    {
        auto* lbl = new QLabel(LS("Negative Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Negative Prompt:")); });
        detailLay->addWidget(lbl);
    }
    edtNeg_ = new QPlainTextEdit;
    edtNeg_->setMaximumHeight(80);
    edtNeg_->setPlaceholderText(LS("Optional negative prompt..."));
    detailLay->addWidget(edtNeg_, 1);

    // ── Run single item ───────────────────────────────────────────────────
    auto* runRow = new QHBoxLayout;
    btnRunItem_ = new QPushButton(LS("Run Item \xe2\x96\xb6"));
    btnRunItem_->setEnabled(false);
    btnRunItem_->setToolTip(QStringLiteral(
        "Run ComfyUI on this single symbol using the group's reference and settings.\n"
        "Requires Project Root to be set in the Pipeline tab."));
    btnStopItem_ = new QPushButton(LS("Stop"));
    btnStopItem_->setEnabled(false);
    btnStopItem_->setFixedWidth(60);
    runRow->addWidget(btnRunItem_);
    runRow->addWidget(btnStopItem_);
    runRow->addStretch();
    detailLay->addLayout(runRow);

    {
        QFont mono(QStringLiteral("Consolas"), 8);
        mono.setStyleHint(QFont::Monospace);
        logItem_ = new QPlainTextEdit;
        logItem_->setReadOnly(true);
        logItem_->setMaximumHeight(80);
        logItem_->setMinimumHeight(40);
        logItem_->setFont(mono);
        logItem_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background:#1a1a1a; color:#d4d4d4; }"));
    }
    detailLay->addWidget(logItem_);

    lblOutputPreview_ = new QLabel;
    lblOutputPreview_->setFixedHeight(120);
    lblOutputPreview_->setAlignment(Qt::AlignCenter);
    lblOutputPreview_->setStyleSheet(QStringLiteral(
        "background:#1a1a1a;border:1px solid #555;border-radius:3px;color:#666;"));
    lblOutputPreview_->setText(QStringLiteral("Output preview"));
    detailLay->addWidget(lblOutputPreview_);

    connect(btnRunItem_,  &QPushButton::clicked, this, &SymbolGroupWidget::onRunSingleItem);
    connect(btnStopItem_, &QPushButton::clicked, this, [this]{
        if (singleRunProcess_) singleRunProcess_->kill();
    });

    connect(edtPrompt_, &QPlainTextEdit::textChanged,
            this, &SymbolGroupWidget::onPromptEdited);
    connect(edtNeg_,    &QPlainTextEdit::textChanged,
            this, &SymbolGroupWidget::onNegPromptEdited);

    splitter->addWidget(detailBox);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 3);

    // ── Vertical splitter: 3-panel area on top, run log on bottom ────────────
    auto* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->addWidget(splitter);

    auto* logBox = new QGroupBox(LS("Run Log"));
    retranslates_.push_back([logBox]{ logBox->setTitle(LS("Run Log")); });
    auto* logBoxLay = new QVBoxLayout(logBox);

    {
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        logGroup_ = new QPlainTextEdit;
        logGroup_->setReadOnly(true);
        logGroup_->setMaximumBlockCount(2000);
        logGroup_->setFont(mono);
        logGroup_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background:#1e1e1e; color:#d4d4d4; }"));
        logBoxLay->addWidget(logGroup_);
    }

    auto* logBtnRow = new QHBoxLayout;
    logBtnRow->addStretch();
    auto* btnClearLog = new QPushButton(LS("Clear"));
    retranslates_.push_back([btnClearLog]{ btnClearLog->setText(LS("Clear")); });
    connect(btnClearLog, &QPushButton::clicked, this, [this]{ logGroup_->clear(); });
    logBtnRow->addWidget(btnClearLog);
    logBoxLay->addLayout(logBtnRow);

    vSplitter->addWidget(logBox);
    vSplitter->setStretchFactor(0, 3);
    vSplitter->setStretchFactor(1, 1);

    root->addWidget(vSplitter, 1);
}

void SymbolGroupWidget::retranslateUi()
{
    for (auto& fn : retranslates_)
        fn();
}

void SymbolGroupWidget::appendGroupLog(const QString& text, const QColor& color)
{
    const QString ts  = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString msg = QStringLiteral("[%1] %2").arg(ts, text);

    QTextCharFormat fmt;
    fmt.setForeground(color);

    QTextCursor cursor(logGroup_->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(msg + '\n', fmt);

    logGroup_->verticalScrollBar()->setValue(
        logGroup_->verticalScrollBar()->maximum());
}

// ── JSON I/O ──────────────────────────────────────────────────────────────────

bool SymbolGroupWidget::loadJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"),
            QStringLiteral("Cannot open file:\n") + path);
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"),
            QStringLiteral("File is not a valid JSON object."));
        return false;
    }

    groups_.clear();
    const QJsonArray arr = doc.object().value(QStringLiteral("groups")).toArray();
    for (const auto& gv : arr) {
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
    return true;
}

bool SymbolGroupWidget::saveJson(const QString& path) const
{
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
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(nullptr, QStringLiteral("Export Failed"),
            QStringLiteral("Cannot write file:\n") + path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SymbolGroupWidget::onImportJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Symbol Groups JSON"),
        LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    if (!loadJson(path)) return;

    loadedPath_ = path;
    lblFile_->setText(QFileInfo(path).fileName());
    lblFile_->setStyleSheet(QStringLiteral("color: #ccc;"));

    curGroup_ = -1;
    curItem_  = -1;
    refreshGroupList();
}

void SymbolGroupWidget::onExportJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Symbol Groups JSON"),
        LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    if (saveJson(path)) {
        loadedPath_ = path;
        lblFile_->setText(QFileInfo(path).fileName());
    }
}

void SymbolGroupWidget::onGroupChanged(int row)
{
    curGroup_ = row;
    curItem_  = -1;
    refreshGroupDetail(row);
    refreshItemList(row);
    refreshItemDetail(-1, -1);
}

void SymbolGroupWidget::onItemChanged(int row)
{
    curItem_ = row;
    refreshItemDetail(curGroup_, row);
}

void SymbolGroupWidget::onBrowseReference()
{
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Reference Image"),
        LastDir::get(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp);;All Files (*)"));
    if (!path.isEmpty()) {
        LastDir::set(path);
        edtRef_->setText(path);
    }
}

void SymbolGroupWidget::onReferenceEdited(const QString& path)
{
    if (busy_) return;
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_[curGroup_].referencePath = path;
    setThumb(path);
}

void SymbolGroupWidget::onIpWeightChanged(double v)
{
    if (busy_) return;
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_[curGroup_].ipWeight = v;
}

void SymbolGroupWidget::onDenoiseChanged(double v)
{
    if (busy_) return;
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    groups_[curGroup_].denoise = v;
}

void SymbolGroupWidget::onPromptEdited()
{
    if (busy_) return;
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    if (curItem_ < 0 || curItem_ >= groups_[curGroup_].items.size()) return;
    groups_[curGroup_].items[curItem_].prompt = edtPrompt_->toPlainText();
}

void SymbolGroupWidget::onNegPromptEdited()
{
    if (busy_) return;
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    if (curItem_ < 0 || curItem_ >= groups_[curGroup_].items.size()) return;
    groups_[curGroup_].items[curItem_].negativePrompt = edtNeg_->toPlainText();
}

// ── UI refresh helpers ────────────────────────────────────────────────────────

void SymbolGroupWidget::refreshGroupList()
{
    QSignalBlocker blk(wGroups_);
    wGroups_->clear();
    for (const auto& g : groups_) {
        auto* it = new QListWidgetItem(g.name, wGroups_);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    if (!groups_.isEmpty())
        wGroups_->setCurrentRow(0);
    updateRunButton();
}

void SymbolGroupWidget::setRunEnabled(bool v)
{
    if (v)
        updateRunButton();
    else
        btnRun_->setEnabled(false);
}

void SymbolGroupWidget::loadGroups(const QList<SymbolGroup>& groups)
{
    groups_    = groups;
    curGroup_  = -1;
    curItem_   = -1;
    loadedPath_.clear();
    lblFile_->setText(LS("(from Group Builder)"));
    lblFile_->setStyleSheet(QStringLiteral("color: #ccc;"));
    refreshGroupList();
}

void SymbolGroupWidget::updateRunButton()
{
    int checked = 0;
    for (int i = 0; i < wGroups_->count(); ++i) {
        if (wGroups_->item(i)->checkState() == Qt::Checked
                && i < groups_.size()
                && !groups_[i].items.isEmpty())
            ++checked;
    }
    btnRun_->setEnabled(checked > 0);
    btnRun_->setText(checked > 1
        ? QStringLiteral("Run Checked ▶ (%1)").arg(checked)
        : QStringLiteral("Run Checked ▶"));
}

void SymbolGroupWidget::refreshGroupDetail(int g)
{
    const bool valid = (g >= 0 && g < groups_.size());
    busy_ = true;
    if (valid) {
        const auto& grp = groups_[g];
        edtRef_->setText(grp.referencePath);
        spnIp_->setValue(grp.ipWeight);
        spnDen_->setValue(grp.denoise);
        setThumb(grp.referencePath);
    } else {
        edtRef_->clear();
        spnIp_->setValue(0.75);
        spnDen_->setValue(0.80);
        lblThumb_->clear();
        lblThumb_->setText(QStringLiteral("No\nimage"));
    }
    btnApply_->setEnabled(valid);
    updateRunButton();
    busy_ = false;
}

void SymbolGroupWidget::refreshItemList(int g)
{
    wItems_->clear();
    if (g < 0 || g >= groups_.size()) return;
    for (const auto& item : groups_[g].items)
        wItems_->addItem(QFileInfo(item.filePath).fileName());
}

void SymbolGroupWidget::refreshItemDetail(int g, int i)
{
    const bool valid = (g >= 0 && g < groups_.size() &&
                        i >= 0 && i < groups_[g].items.size());
    busy_ = true;
    if (valid) {
        const auto& item = groups_[g].items[i];
        lblItemFile_->setText(QFileInfo(item.filePath).fileName());
        lblItemFile_->setStyleSheet(QStringLiteral("font-weight: bold;"));
        edtPrompt_->setPlainText(item.prompt);
        edtNeg_->setPlainText(item.negativePrompt);
        const QPixmap px(item.filePath);
        if (!px.isNull()) {
            lblItemPreview_->setText({});
            lblItemPreview_->setPixmap(
                px.scaled(lblItemPreview_->width(), lblItemPreview_->height(),
                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            lblItemPreview_->setPixmap(QPixmap());
            lblItemPreview_->setText(QStringLiteral("No preview"));
        }
    } else {
        lblItemFile_->setText(LS("(select an item)"));
        lblItemFile_->setStyleSheet(QStringLiteral("color: #aaa; font-style: italic;"));
        edtPrompt_->clear();
        edtNeg_->clear();
        lblItemPreview_->setPixmap(QPixmap());
        lblItemPreview_->setText(QStringLiteral("No preview"));
    }
    busy_ = false;

    const bool running = singleRunProcess_
                         && singleRunProcess_->state() != QProcess::NotRunning;
    btnRunItem_->setEnabled(valid && !running);
    if (!valid) {
        logItem_->clear();
        lblOutputPreview_->setPixmap(QPixmap());
        lblOutputPreview_->setText(QStringLiteral("Output preview"));
    }
}

// ── Single-item run ───────────────────────────────────────────────────────────

void SymbolGroupWidget::setRunConfig(const QString& pythonExe,
                                      const QString& projectRoot,
                                      const QString& comfyuiServer)
{
    pythonExe_     = pythonExe;
    projectRoot_   = projectRoot;
    comfyuiServer_ = comfyuiServer;
}

static void writeSingleItemPlan(const QString& planPath, const SymbolGroup& group,
                                 const SymbolItem& item, const QString& batchId,
                                 const QString& assetRoot)
{
    QJsonObject grp;
    grp[QStringLiteral("source_group")]  = group.name;
    grp[QStringLiteral("character_id")]  = group.name;
    grp[QStringLiteral("seed")]          = 42;
    grp[QStringLiteral("static_assets")] = QJsonArray{ QFileInfo(item.filePath).fileName() };
    grp[QStringLiteral("spine_assets")]  = QJsonArray{};

    QJsonObject root;
    root[QStringLiteral("batch_id")]           = batchId;
    root[QStringLiteral("style_profile_id")]   = QStringLiteral("default");
    root[QStringLiteral("scanned_asset_root")] = QDir::toNativeSeparators(assetRoot);
    root[QStringLiteral("groups")]             = QJsonArray{grp};

    QFile f(planPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

void SymbolGroupWidget::onRunSingleItem()
{
    if (curGroup_ < 0 || curGroup_ >= groups_.size()) return;
    if (curItem_ < 0 || curItem_ >= groups_[curGroup_].items.size()) return;

    const SymbolGroup& group = groups_[curGroup_];
    const SymbolItem&  item  = group.items[curItem_];

    if (projectRoot_.isEmpty()) {
        logItem_->appendPlainText(QStringLiteral(
            "ERROR: Project Root not set — configure it in the Pipeline tab first."));
        return;
    }

    const QString scriptMode = cbxScript_->currentData().toString();

    const QString scriptPath = QDir::cleanPath(
        scriptMode == QStringLiteral("batchreskin")
            ? projectRoot_ + QStringLiteral("/TestData/Reskin/batch_reskin.py")
            : projectRoot_ + QStringLiteral("/TestData/ImageWithPrompt/run_imagewithprompt.py"));
    if (!QFile::exists(scriptPath)) {
        logItem_->appendPlainText(QStringLiteral("ERROR: script not found:\n") + scriptPath);
        return;
    }

    if (singleRunProcess_ && singleRunProcess_->state() != QProcess::NotRunning) {
        logItem_->appendPlainText(QStringLiteral("[busy] A run is already in progress."));
        return;
    }

    if (!singleRunProcess_) {
        singleRunProcess_ = new QProcess(this);
        connect(singleRunProcess_, &QProcess::readyReadStandardOutput,
                this, &SymbolGroupWidget::onSingleRunReadyRead);
        connect(singleRunProcess_, &QProcess::readyReadStandardError, this, [this] {
            singleRunProcess_->setReadChannel(QProcess::StandardError);
            while (singleRunProcess_->canReadLine()) {
                const QString line =
                    QString::fromUtf8(singleRunProcess_->readLine()).trimmed();
                if (!line.isEmpty())
                    logItem_->appendPlainText(line);
            }
            logItem_->verticalScrollBar()->setValue(
                logItem_->verticalScrollBar()->maximum());
        });
        connect(singleRunProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &SymbolGroupWidget::onSingleRunFinished);
    }

    logItem_->clear();
    lblOutputPreview_->setPixmap(QPixmap());
    lblOutputPreview_->setText(QStringLiteral("Running\xe2\x80\xa6"));
    btnRunItem_->setEnabled(false);
    btnStopItem_->setEnabled(true);

    const QString itemPath  = QDir::fromNativeSeparators(item.filePath);
    const QString assetRoot = QFileInfo(itemPath).absolutePath();
    singleRunOutputDir_     = QDir::cleanPath(
                                  projectRoot_ + QStringLiteral("/output/")
                                  + QFileInfo(assetRoot).fileName());

    QDir().mkpath(singleRunOutputDir_);

    QStringList args;
    args << QStringLiteral("-u") << scriptPath;

    if (scriptMode == QStringLiteral("batchreskin")) {
        // batch_reskin.py <sourceFolder> <outputFolder>
        args << QDir::toNativeSeparators(assetRoot)
             << QDir::toNativeSeparators(singleRunOutputDir_);
    } else {
        // run_imagewithprompt.py
        args << QStringLiteral("--reference")   << QDir::toNativeSeparators(itemPath)
             << QStringLiteral("--output-dir")  << QDir::toNativeSeparators(singleRunOutputDir_)
             << QStringLiteral("--output-name") << QFileInfo(itemPath).baseName()
             << QStringLiteral("--ip-weight")   << QString::number(group.ipWeight, 'f', 2)
             << QStringLiteral("--denoise")     << QString::number(group.denoise,   'f', 2);
        if (!item.prompt.isEmpty())
            args << QStringLiteral("--positive-prompt") << item.prompt;
        if (!item.negativePrompt.isEmpty())
            args << QStringLiteral("--negative-prompt") << item.negativePrompt;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COMFYUI_SERVER"), comfyuiServer_);
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    singleRunProcess_->setProcessEnvironment(env);
    singleRunProcess_->setWorkingDirectory(projectRoot_);
    singleRunProcess_->start(pythonExe_, args);

    logItem_->appendPlainText(QStringLiteral("Starting: ") + QFileInfo(itemPath).fileName());
    logItem_->appendPlainText(QStringLiteral("Output \xe2\x86\x92 ") + singleRunOutputDir_);
}

void SymbolGroupWidget::onSingleRunReadyRead()
{
    singleRunProcess_->setReadChannel(QProcess::StandardOutput);
    while (singleRunProcess_->canReadLine()) {
        const QString line =
            QString::fromUtf8(singleRunProcess_->readLine()).trimmed();
        if (!line.isEmpty())
            logItem_->appendPlainText(line);
    }
    logItem_->verticalScrollBar()->setValue(
        logItem_->verticalScrollBar()->maximum());
}

void SymbolGroupWidget::onSingleRunFinished(int exitCode, QProcess::ExitStatus)
{
    onSingleRunReadyRead();

    const bool valid = (curGroup_ >= 0 && curGroup_ < groups_.size()
                        && curItem_ >= 0 && curItem_ < groups_[curGroup_].items.size());
    btnRunItem_->setEnabled(valid);
    btnStopItem_->setEnabled(false);

    if (exitCode != 0) {
        logItem_->appendPlainText(
            QStringLiteral("FAILED (exit %1)").arg(exitCode));
        lblOutputPreview_->setText(QStringLiteral("Failed"));
        return;
    }

    logItem_->appendPlainText(QStringLiteral("Done."));

    // Locate the output image: same filename as the input, written to output dir
    if (!valid) return;
    const QString itemPath   = groups_[curGroup_].items[curItem_].filePath;
    const QString outputPath = QDir::cleanPath(
        singleRunOutputDir_ + QStringLiteral("/") + QFileInfo(itemPath).fileName());

    auto showPixmap = [this](const QString& path) -> bool {
        const QPixmap px(path);
        if (px.isNull()) return false;
        lblOutputPreview_->setText({});
        lblOutputPreview_->setPixmap(
            px.scaled(lblOutputPreview_->width(), lblOutputPreview_->height(),
                      Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return true;
    };

    if (showPixmap(outputPath)) return;

    // Fallback: scan the output dir recursively
    static const QStringList imgFilters{
        QStringLiteral("*.png"), QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"), QStringLiteral("*.webp") };
    QStringList pending;
    pending << singleRunOutputDir_;
    while (!pending.isEmpty()) {
        QDir d(pending.takeLast());
        for (const auto& fn : d.entryList(imgFilters, QDir::Files))
            if (showPixmap(d.absoluteFilePath(fn))) return;
        for (const auto& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            pending << d.absoluteFilePath(sub);
    }
    lblOutputPreview_->setText(QStringLiteral("Output not found"));
}

void SymbolGroupWidget::setThumb(const QString& path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        lblThumb_->clear();
        lblThumb_->setText(QStringLiteral("No\nimage"));
        return;
    }
    const QPixmap px(path);
    if (px.isNull()) {
        lblThumb_->setText(QStringLiteral("Load\nfailed"));
        return;
    }
    lblThumb_->setPixmap(px.scaled(lblThumb_->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
