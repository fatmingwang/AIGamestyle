#include "ImageWithPromptWidget.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QMouseEvent>

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
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSplitter>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPixmap>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QDateTime>
#include <QTime>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFont>
#include <QMessageBox>

// ── Construction ─────────────────────────────────────────────────────────────

ImageWithPromptWidget::ImageWithPromptWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// ── Event filter (shift-range check) ─────────────────────────────────────────

bool ImageWithPromptWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == wItems_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::ShiftModifier)) {
            auto* lwi = wItems_->itemAt(me->pos());
            const int row = lwi ? wItems_->row(lwi) : -1;
            if (row >= 0 && anchorRow_ >= 0) {
                const int from = qMin(anchorRow_, row);
                const int to   = qMax(anchorRow_, row);
                // Toggle to the opposite of the clicked item's current state
                const Qt::CheckState newState =
                    (lwi->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
                for (int i = from; i <= to; ++i)
                    if (auto* item = wItems_->item(i))
                        item->setCheckState(newState);
                return true;  // consume — don't change current-item / detail view
            }
        } else if (me->button() == Qt::LeftButton) {
            auto* lwi = wItems_->itemAt(me->pos());
            if (lwi) anchorRow_ = wItems_->row(lwi);
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── UI construction ───────────────────────────────────────────────────────────

void ImageWithPromptWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Top bar: folder + import/export ──────────────────────────────────
    auto* topBar = new QHBoxLayout;

    {
        auto* lbl = new QLabel(LS("Source Folder:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Source Folder:")); });
        topBar->addWidget(lbl);
    }
    edtFolder_ = new QLineEdit;
    edtFolder_->setReadOnly(true);
    edtFolder_->setPlaceholderText(LS("Select a folder containing PNG files..."));
    topBar->addWidget(edtFolder_, 1);

    {
        auto* btn = new QPushButton(LS("Browse..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Browse...")); });
        btn->setFixedWidth(80);
        connect(btn, &QPushButton::clicked, this, &ImageWithPromptWidget::onBrowseFolder);
        topBar->addWidget(btn);
    }

    topBar->addSpacing(16);

    {
        auto* btn = new QPushButton(LS("Import JSON..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Import JSON...")); });
        connect(btn, &QPushButton::clicked, this, &ImageWithPromptWidget::onImportJson);
        topBar->addWidget(btn);
    }
    {
        auto* btn = new QPushButton(LS("Export JSON..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Export JSON...")); });
        connect(btn, &QPushButton::clicked, this, &ImageWithPromptWidget::onExportJson);
        topBar->addWidget(btn);
    }

    root->addLayout(topBar);

    // ── Parameters group ──────────────────────────────────────────────────
    auto* paramsBox = new QGroupBox(LS("Generation Parameters"));
    retranslates_.push_back([paramsBox]{ paramsBox->setTitle(LS("Generation Parameters")); });
    auto* paramRow = new QHBoxLayout(paramsBox);

    auto addParam = [&](const char* labelKey, QWidget* spinbox) {
        auto* lbl = new QLabel(LS(labelKey));
        retranslates_.push_back([lbl, labelKey]{ lbl->setText(LS(labelKey)); });
        paramRow->addWidget(lbl);
        paramRow->addWidget(spinbox);
        paramRow->addSpacing(8);
    };

    spnIpWeight_ = new QDoubleSpinBox;
    spnIpWeight_->setRange(0.0, 1.0); spnIpWeight_->setSingleStep(0.05);
    spnIpWeight_->setDecimals(2); spnIpWeight_->setValue(0.7);
    spnIpWeight_->setFixedWidth(64);
    spnIpWeight_->setToolTip(QStringLiteral(
        "IPAdapter style weight (0.0 – 1.0)\n"
        "Controls how strongly the reference image drives the visual style.\n"
        "0.0 = ignore reference, 1.0 = heavily mirror reference style.\n"
        "Typical range: 0.6 – 0.85"));
    addParam("IP Weight:", spnIpWeight_);

    spnSteps_ = new QSpinBox;
    spnSteps_->setRange(1, 150); spnSteps_->setValue(20);
    spnSteps_->setFixedWidth(50);
    spnSteps_->setToolTip(QStringLiteral(
        "Sampling steps\n"
        "More steps produce finer detail but take longer.\n"
        "Typical range: 20 – 40. Diminishing returns above 50."));
    addParam("Steps:", spnSteps_);

    spnCfg_ = new QDoubleSpinBox;
    spnCfg_->setRange(1.0, 30.0); spnCfg_->setSingleStep(0.5);
    spnCfg_->setDecimals(1); spnCfg_->setValue(7.0);
    spnCfg_->setFixedWidth(56);
    spnCfg_->setToolTip(QStringLiteral(
        "CFG scale — Classifier-Free Guidance (1.0 – 30.0)\n"
        "How strictly the output follows the text prompt.\n"
        "Low (2–5): creative, loose. High (8–15): rigid, over-saturated.\n"
        "Typical range: 6.0 – 8.0"));
    addParam("CFG:", spnCfg_);

    spnDenoise_ = new QDoubleSpinBox;
    spnDenoise_->setRange(0.0, 1.0); spnDenoise_->setSingleStep(0.05);
    spnDenoise_->setDecimals(2); spnDenoise_->setValue(1.0);
    spnDenoise_->setFixedWidth(64);
    spnDenoise_->setToolTip(QStringLiteral(
        "Denoise strength (0.0 – 1.0)\n"
        "1.0 = generate entirely from noise (full creative freedom).\n"
        "Lower values preserve more of the original latent structure.\n"
        "Keep at 1.0 for this workflow (EmptyLatent has no source image)."));
    addParam("Denoise:", spnDenoise_);

    {
        auto* lbl = new QLabel(LS("Seed:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Seed:")); });
        paramRow->addWidget(lbl);
        spnSeed_ = new QSpinBox;
        spnSeed_->setRange(0, 2000000000);
        spnSeed_->setValue(42);
        spnSeed_->setFixedWidth(90);
        spnSeed_->setToolTip(QStringLiteral(
            "Random seed\n"
            "Same seed + same settings = identical output.\n"
            "Change the seed to get a different variation."));
        paramRow->addWidget(spnSeed_);
        paramRow->addSpacing(4);
    }
    chkAutoSeed_ = new QCheckBox(LS("Auto"));
    retranslates_.push_back([this]{ chkAutoSeed_->setText(LS("Auto")); });
    chkAutoSeed_->setToolTip(QStringLiteral(
        "Auto seed\n"
        "Picks a new random seed before every run so each result is unique."));
    connect(chkAutoSeed_, &QCheckBox::toggled, this,
            [this](bool v){ spnSeed_->setEnabled(!v); });
    paramRow->addWidget(chkAutoSeed_);
    paramRow->addStretch();

    root->addWidget(paramsBox);

    // ── Vertical splitter: main panels + log ─────────────────────────────
    auto* vSplitter = new QSplitter(Qt::Vertical);

    // ── Horizontal splitter: item list | item detail ──────────────────────
    auto* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setChildrenCollapsible(false);

    // Left: Items list
    auto* itemsBox = new QGroupBox(LS("Items"));
    retranslates_.push_back([itemsBox]{ itemsBox->setTitle(LS("Items")); });
    auto* itemsLay = new QVBoxLayout(itemsBox);
    wItems_ = new QListWidget;
    wItems_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#1e1e1e;color:#d4d4d4;}"
        "QListWidget::item:selected{background:#264f78;color:#ffffff;}"));
    itemsLay->addWidget(wItems_);

    // Run All below the list
    auto* runAllRow = new QHBoxLayout;
    btnSelectAll_ = new QPushButton(LS("Select All"));
    retranslates_.push_back([this]{ btnSelectAll_->setText(LS("Select All")); });
    btnSelectAll_->setEnabled(false);
    btnRunAll_ = new QPushButton(LS("Run All \xe2\x96\xb6"));
    retranslates_.push_back([this]{ updateButtons(); });
    btnRunAll_->setEnabled(false);
    btnStop_ = new QPushButton(LS("Stop"));
    retranslates_.push_back([this]{ btnStop_->setText(LS("Stop")); });
    btnStop_->setEnabled(false);
    btnStop_->setFixedWidth(60);
    runAllRow->addWidget(btnSelectAll_);
    runAllRow->addWidget(btnRunAll_);
    runAllRow->addWidget(btnStop_);
    itemsLay->addLayout(runAllRow);

    hSplitter->addWidget(itemsBox);

    wItems_->viewport()->installEventFilter(this);

    connect(wItems_, &QListWidget::currentRowChanged,
            this, [this](int){ onItemSelectionChanged(); });
    connect(wItems_, &QListWidget::itemChanged,
            this, [this](QListWidgetItem*){ updateButtons(); });
    connect(btnSelectAll_, &QPushButton::clicked, this, &ImageWithPromptWidget::onSelectAll);
    connect(btnRunAll_,    &QPushButton::clicked, this, &ImageWithPromptWidget::onRunAll);
    connect(btnStop_,      &QPushButton::clicked, this, &ImageWithPromptWidget::onStop);

    // Right: Item detail
    auto* detailBox = new QGroupBox(LS("Item Detail"));
    retranslates_.push_back([detailBox]{ detailBox->setTitle(LS("Item Detail")); });
    auto* detailLay = new QVBoxLayout(detailBox);

    // Filename + size
    {
        auto* row = new QHBoxLayout;
        lblItemName_ = new QLabel(LS("(select an item)"));
        lblItemName_->setStyleSheet(QStringLiteral("font-weight:bold;"));
        row->addWidget(lblItemName_);
        row->addStretch();
        lblItemSize_ = new QLabel;
        lblItemSize_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblItemSize_);
        detailLay->addLayout(row);
    }

    // Source preview
    lblItemPreview_ = new QLabel;
    lblItemPreview_->setFixedHeight(120);
    lblItemPreview_->setAlignment(Qt::AlignCenter);
    lblItemPreview_->setStyleSheet(QStringLiteral(
        "QLabel{background:#1a1a1a;border:1px solid #444;border-radius:3px;color:#555;}"));
    lblItemPreview_->setText(QStringLiteral("Source preview"));
    detailLay->addWidget(lblItemPreview_);

    // Category
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Category:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Category:")); });
        lbl->setFixedWidth(70);
        row->addWidget(lbl);
        cbxCategory_ = new QComboBox;
        cbxCategory_->setToolTip(QStringLiteral(
            "Preset prompt from categories.json.\n"
            "Positive/Negative below override if non-empty."));
        cbxCategory_->setEnabled(false);
        row->addWidget(cbxCategory_);
        row->addStretch();
        detailLay->addLayout(row);
    }

    // Positive prompt
    {
        auto* lbl = new QLabel(LS("Positive Prompt (override):"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Positive Prompt (override):")); });
        detailLay->addWidget(lbl);
        edtPositive_ = new QPlainTextEdit;
        edtPositive_->setPlaceholderText(LS("Leave empty to use the category preset"));
        edtPositive_->setMaximumHeight(70);
        edtPositive_->setEnabled(false);
        detailLay->addWidget(edtPositive_);
    }

    // Negative prompt
    {
        auto* lbl = new QLabel(LS("Negative Prompt (override):"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Negative Prompt (override):")); });
        detailLay->addWidget(lbl);
        edtNegative_ = new QPlainTextEdit;
        edtNegative_->setPlaceholderText(LS("Leave empty to use the category preset"));
        edtNegative_->setMaximumHeight(50);
        edtNegative_->setEnabled(false);
        detailLay->addWidget(edtNegative_);
    }

    // Run Item button
    {
        auto* row = new QHBoxLayout;
        btnRunSingle_ = new QPushButton(LS("Run Item \xe2\x96\xb6"));
        retranslates_.push_back([this]{ btnRunSingle_->setText(LS("Run Item \xe2\x96\xb6")); });
        btnRunSingle_->setEnabled(false);
        btnRunSingle_->setToolTip(QStringLiteral("Generate output for the selected item only"));
        row->addWidget(btnRunSingle_);
        row->addStretch();
        detailLay->addLayout(row);
    }

    // Output preview
    lblOutputPreview_ = new QLabel;
    lblOutputPreview_->setMinimumHeight(120);
    lblOutputPreview_->setAlignment(Qt::AlignCenter);
    lblOutputPreview_->setStyleSheet(QStringLiteral(
        "QLabel{background:#1a1a1a;border:1px solid #555;border-radius:3px;color:#555;}"));
    lblOutputPreview_->setText(QStringLiteral("Output preview"));
    detailLay->addWidget(lblOutputPreview_, 1);

    hSplitter->addWidget(detailBox);
    hSplitter->setStretchFactor(0, 1);
    hSplitter->setStretchFactor(1, 2);

    vSplitter->addWidget(hSplitter);

    // ── Log ───────────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(LS("Run Log"));
    retranslates_.push_back([logBox]{ logBox->setTitle(LS("Run Log")); });
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
    vSplitter->addWidget(logBox);
    vSplitter->setStretchFactor(0, 3);
    vSplitter->setStretchFactor(1, 1);

    root->addWidget(vSplitter, 1);

    // ── Prompt edit connections ───────────────────────────────────────────
    connect(cbxCategory_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageWithPromptWidget::onCategoryIndexChanged);
    connect(edtPositive_, &QPlainTextEdit::textChanged,
            this, &ImageWithPromptWidget::onPositiveEdited);
    connect(edtNegative_, &QPlainTextEdit::textChanged,
            this, &ImageWithPromptWidget::onNegativeEdited);
    connect(btnRunSingle_, &QPushButton::clicked,
            this, &ImageWithPromptWidget::onRunSingle);
}

void ImageWithPromptWidget::retranslateUi()
{
    for (auto& fn : retranslates_)
        fn();
}

// ── Config ────────────────────────────────────────────────────────────────────

void ImageWithPromptWidget::setRunConfig(const QString& pythonExe,
                                          const QString& projectRoot,
                                          const QString& comfyuiServer)
{
    pythonExe_     = pythonExe;
    comfyuiServer_ = comfyuiServer;
    const bool rootChanged = (projectRoot != projectRoot_);
    projectRoot_   = projectRoot;
    if (rootChanged)
        loadCategories();
    updateButtons();
}

void ImageWithPromptWidget::loadCategories()
{
    categoryKeys_.clear();
    categoryPrompts_.clear();

    const QString catPath = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/TestData/ImageWithPrompt/categories.json"));

    QFile f(catPath);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.key().startsWith(QLatin1Char('_'))) continue;
            categoryKeys_ << it.key();
            const QJsonObject entry = it.value().toObject();
            categoryPrompts_[it.key()] = {
                entry.value(QStringLiteral("positive")).toString(),
                entry.value(QStringLiteral("negative")).toString()
            };
        }
    }

    if (categoryKeys_.isEmpty()) {
        categoryKeys_ << QStringLiteral("symbol_high") << QStringLiteral("symbol_low")
                      << QStringLiteral("symbol_wild") << QStringLiteral("symbol_scatter")
                      << QStringLiteral("background")  << QStringLiteral("character")
                      << QStringLiteral("panel")        << QStringLiteral("button")
                      << QStringLiteral("icon")         << QStringLiteral("default");
    }

    QSignalBlocker blk(cbxCategory_);
    const QString prev = cbxCategory_->currentText();
    cbxCategory_->clear();
    cbxCategory_->addItem(QStringLiteral("(none — use prompt)"), QString());
    for (const auto& k : std::as_const(categoryKeys_))
        cbxCategory_->addItem(k, k);
    const int idx = cbxCategory_->findText(prev);
    cbxCategory_->setCurrentIndex(idx >= 0 ? idx : 0);
}

// ── Folder scan ───────────────────────────────────────────────────────────────

void ImageWithPromptWidget::onBrowseFolder()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, LS("Select Source Folder"), LastDir::get());
    if (path.isEmpty()) return;

    LastDir::set(path);
    folderPath_ = path;
    edtFolder_->setText(path);
    scanFolder(path);
}

void ImageWithPromptWidget::scanFolder(const QString& folderPath)
{
    items_.clear();
    wItems_->clear();
    curItem_ = -1;
    refreshItemDetail(-1);

    static const QStringList imgFilters{
        QStringLiteral("*.png"),  QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"),  QStringLiteral("*.webp"), QStringLiteral("*.tga"),
        QStringLiteral("*.tif"),  QStringLiteral("*.tiff"), QStringLiteral("*.gif"),
        QStringLiteral("*.psd"),  QStringLiteral("*.exr"),  QStringLiteral("*.hdr")
    };

    QStringList allPaths;
    QDirIterator it(folderPath, imgFilters, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        allPaths << it.next();
    allPaths.sort(Qt::CaseInsensitive);

    const QDir baseDir(folderPath);
    for (const auto& absPath : std::as_const(allPaths)) {
        IWPItem item;
        item.filePath = absPath;
        item.category = matchCategory(QFileInfo(absPath).completeBaseName());
        items_.append(item);

        const QString relPath = baseDir.relativeFilePath(absPath);
        auto* lwi = new QListWidgetItem(
            QStringLiteral("%1  [%2]").arg(relPath, item.category.isEmpty()
                ? QStringLiteral("default") : item.category),
            wItems_);
        lwi->setFlags(lwi->flags() | Qt::ItemIsUserCheckable);
        lwi->setCheckState(Qt::Checked);
        lwi->setForeground(Qt::white);
        lwi->setData(Qt::UserRole, absPath);
    }

    appendLog(QStringLiteral("Scanned: %1 file(s) in %2")
              .arg(items_.size()).arg(QDir::toNativeSeparators(folderPath)));

    updateButtons();
    if (!items_.isEmpty())
        wItems_->setCurrentRow(0);
}

QString ImageWithPromptWidget::matchCategory(const QString& stem) const
{
    const QString lower = stem.toLower();
    // exact match first
    for (const auto& key : std::as_const(categoryKeys_))
        if (key == lower) return key;
    // substring match (skip "default")
    for (const auto& key : std::as_const(categoryKeys_)) {
        if (key == QStringLiteral("default")) continue;
        if (lower.contains(key) || key.contains(lower)) return key;
    }
    return QStringLiteral("default");
}

// ── Import / Export ───────────────────────────────────────────────────────────

void ImageWithPromptWidget::onImportJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this, LS("Import Prompts JSON"), LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, LS("Import Failed"),
            LS("Cannot open file:\n") + path);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // Optionally update folder
    const QString folder = root.value(QStringLiteral("folder")).toString();
    if (!folder.isEmpty() && QDir(folder).exists()) {
        folderPath_ = folder;
        edtFolder_->setText(folder);
        scanFolder(folder);
    }

    // Overlay prompt data onto matching items
    const QJsonArray arr = root.value(QStringLiteral("items")).toArray();
    for (const auto& v : arr) {
        const QJsonObject obj = v.toObject();
        const QString filename = obj.value(QStringLiteral("file")).toString();
        for (auto& item : items_) {
            if (QFileInfo(item.filePath).fileName() == filename) {
                item.category = obj.value(QStringLiteral("category")).toString();
                item.positive = obj.value(QStringLiteral("positive")).toString();
                item.negative = obj.value(QStringLiteral("negative")).toString();
                break;
            }
        }
    }

    // Refresh list labels
    for (int i = 0; i < wItems_->count() && i < items_.size(); ++i) {
        const auto& item = items_[i];
        const QString cat = item.category.isEmpty()
            ? QStringLiteral("default") : item.category;
        wItems_->item(i)->setText(
            QStringLiteral("%1  [%2]").arg(QFileInfo(item.filePath).fileName(), cat));
    }

    if (curItem_ >= 0)
        refreshItemDetail(curItem_);

    appendLog(QStringLiteral("Imported prompts from: %1").arg(path));
}

void ImageWithPromptWidget::onExportJson()
{
    if (items_.isEmpty()) {
        QMessageBox::information(this, LS("Export"), LS("No items to export."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, LS("Export Prompts JSON"), LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    QJsonArray arr;
    for (const auto& item : std::as_const(items_)) {
        QJsonObject obj;
        obj[QStringLiteral("file")]     = QFileInfo(item.filePath).fileName();
        obj[QStringLiteral("category")] = item.category;
        if (!item.positive.isEmpty())
            obj[QStringLiteral("positive")] = item.positive;
        if (!item.negative.isEmpty())
            obj[QStringLiteral("negative")] = item.negative;
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("folder")] = folderPath_;
    root[QStringLiteral("items")]  = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, LS("Export Failed"), LS("Cannot write file:\n") + path);
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    appendLog(QStringLiteral("Exported prompts to: %1").arg(path));
}

// ── Item detail ───────────────────────────────────────────────────────────────

void ImageWithPromptWidget::onItemSelectionChanged()
{
    curItem_ = wItems_->currentRow();
    refreshItemDetail(curItem_);
}

void ImageWithPromptWidget::refreshItemDetail(int row)
{
    const bool valid = (row >= 0 && row < items_.size());
    busy_ = true;

    cbxCategory_->setEnabled(valid);
    edtPositive_->setEnabled(valid);
    edtNegative_->setEnabled(valid);

    if (valid) {
        const auto& item = items_[row];
        const QString filename = QFileInfo(item.filePath).fileName();
        lblItemName_->setText(filename);
        lblItemName_->setStyleSheet(QStringLiteral("font-weight:bold;color:#eee;"));

        // Source preview + size
        const QPixmap px(item.filePath);
        if (!px.isNull()) {
            lblItemPreview_->setText({});
            lblItemPreview_->setPixmap(
                px.scaled(lblItemPreview_->width(), lblItemPreview_->height(),
                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
            lblItemSize_->setText(
                QStringLiteral("%1 × %2").arg(px.width()).arg(px.height()));
        } else {
            lblItemPreview_->setPixmap(QPixmap());
            lblItemPreview_->setText(QStringLiteral("No preview"));
            lblItemSize_->clear();
        }

        // Category combo
        const int catIdx = item.category.isEmpty()
            ? 0 : cbxCategory_->findData(item.category);
        cbxCategory_->setCurrentIndex(catIdx >= 0 ? catIdx : 0);

        // Show category default when no custom override is set.
        // busy_ is already true here so the textChanged signal won't save anything.
        const QString catKey = item.category.isEmpty()
            ? QStringLiteral("default") : item.category;
        const auto& def = categoryPrompts_.value(catKey);

        edtPositive_->setPlainText(
            item.positive.isEmpty() ? def.first  : item.positive);
        edtNegative_->setPlainText(
            item.negative.isEmpty() ? def.second : item.negative);

        // Output preview
        if (!item.lastOutputPath.isEmpty()) {
            const QPixmap opx(item.lastOutputPath);
            if (!opx.isNull()) {
                lblOutputPreview_->setText({});
                lblOutputPreview_->setPixmap(
                    opx.scaled(lblOutputPreview_->width(), lblOutputPreview_->height(),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                lblOutputPreview_->setPixmap(QPixmap());
                lblOutputPreview_->setText(QStringLiteral("Output preview"));
            }
        } else {
            lblOutputPreview_->setPixmap(QPixmap());
            lblOutputPreview_->setText(QStringLiteral("Output preview"));
        }
    } else {
        lblItemName_->setText(LS("(select an item)"));
        lblItemName_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
        lblItemSize_->clear();
        lblItemPreview_->setPixmap(QPixmap());
        lblItemPreview_->setText(QStringLiteral("Source preview"));
        {
            QSignalBlocker blk(cbxCategory_);
            cbxCategory_->setCurrentIndex(0);
        }
        edtPositive_->clear();
        edtNegative_->clear();
        lblOutputPreview_->setPixmap(QPixmap());
        lblOutputPreview_->setText(QStringLiteral("Output preview"));
    }

    busy_ = false;
    updateButtons();
}

void ImageWithPromptWidget::onCategoryIndexChanged(int index)
{
    if (busy_ || curItem_ < 0 || curItem_ >= items_.size()) return;

    const QString newCat = (index <= 0) ? QString()
                                        : cbxCategory_->itemData(index).toString();

    // Collect targets: all checked items, or just curItem_ if none are checked.
    QList<int> targets;
    for (int i = 0; i < wItems_->count(); ++i)
        if (wItems_->item(i)->checkState() == Qt::Checked)
            targets.append(i);
    if (targets.isEmpty())
        targets.append(curItem_);

    // Apply category and refresh list labels for all targets.
    for (int i : targets) {
        if (i < 0 || i >= items_.size()) continue;
        items_[i].category = newCat;
        if (auto* lwi = wItems_->item(i)) {
            const QString cat = newCat.isEmpty() ? QStringLiteral("default") : newCat;
            lwi->setText(QStringLiteral("%1  [%2]")
                .arg(QFileInfo(items_[i].filePath).fileName(), cat));
        }
    }

    // Refresh prompt fields for the currently displayed item only.
    // Guard busy_ so textChanged doesn't save the preset as a custom override.
    const auto& item = items_[curItem_];
    const QString catKey = item.category.isEmpty()
        ? QStringLiteral("default") : item.category;
    const auto& def = categoryPrompts_.value(catKey);
    busy_ = true;
    if (item.positive.isEmpty())
        edtPositive_->setPlainText(def.first);
    if (item.negative.isEmpty())
        edtNegative_->setPlainText(def.second);
    busy_ = false;
}

void ImageWithPromptWidget::onPositiveEdited()
{
    if (busy_ || curItem_ < 0 || curItem_ >= items_.size()) return;
    items_[curItem_].positive = edtPositive_->toPlainText();
}

void ImageWithPromptWidget::onNegativeEdited()
{
    if (busy_ || curItem_ < 0 || curItem_ >= items_.size()) return;
    items_[curItem_].negative = edtNegative_->toPlainText();
}

void ImageWithPromptWidget::updateButtons()
{
    const bool hasItems  = !items_.isEmpty();
    const bool hasSelect = (curItem_ >= 0 && curItem_ < items_.size());
    const bool running   = process_ && process_->state() != QProcess::NotRunning;

    int checked = 0;
    for (int i = 0; i < wItems_->count(); ++i)
        if (wItems_->item(i)->checkState() == Qt::Checked)
            ++checked;

    btnSelectAll_->setEnabled(hasItems && !running);
    btnRunSingle_->setEnabled(hasSelect && !running);
    btnRunAll_->setEnabled(checked > 0 && !running);
    btnStop_->setEnabled(running);

    btnRunAll_->setText(checked > 0
        ? QStringLiteral("Run All \xe2\x96\xb6 (%1)").arg(checked)
        : QStringLiteral("Run All \xe2\x96\xb6"));
}

void ImageWithPromptWidget::markItemColor(int row, const QColor& color)
{
    if (auto* lwi = wItems_->item(row))
        lwi->setForeground(color);
}

// ── Log ───────────────────────────────────────────────────────────────────────

void ImageWithPromptWidget::appendLog(const QString& text, const QColor& color)
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

// ── Run logic ─────────────────────────────────────────────────────────────────

void ImageWithPromptWidget::saveRunManifest(const QList<int>& indices)
{
    QJsonObject params;
    params[QStringLiteral("ip_weight")]  = spnIpWeight_->value();
    params[QStringLiteral("steps")]      = spnSteps_->value();
    params[QStringLiteral("cfg")]        = spnCfg_->value();
    params[QStringLiteral("denoise")]    = spnDenoise_->value();
    params[QStringLiteral("seed")]       = spnSeed_->value();
    params[QStringLiteral("auto_seed")]  = chkAutoSeed_->isChecked();

    QJsonArray itemsArr;
    for (int idx : indices) {
        if (idx < 0 || idx >= items_.size()) continue;
        const auto& item = items_[idx];
        QJsonObject obj;
        obj[QStringLiteral("file")]     = QFileInfo(item.filePath).fileName();
        obj[QStringLiteral("category")] = item.category.isEmpty()
                                          ? QStringLiteral("default") : item.category;
        if (!item.positive.isEmpty())
            obj[QStringLiteral("positive")] = item.positive;
        if (!item.negative.isEmpty())
            obj[QStringLiteral("negative")] = item.negative;
        itemsArr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("timestamp")]  =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("output_dir")] = QDir::toNativeSeparators(batchOutDir_);
    root[QStringLiteral("parameters")] = params;
    root[QStringLiteral("items")]      = itemsArr;

    QDir().mkpath(batchOutDir_);
    QFile f(batchOutDir_ + QStringLiteral("/run_params.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ImageWithPromptWidget::onRunSingle()
{
    if (curItem_ < 0 || curItem_ >= items_.size()) return;
    if (process_ && process_->state() != QProcess::NotRunning) return;

    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set — configure it in the Pipeline tab first."),
                  QColor(255, 80, 80));
        return;
    }

    batchRunning_ = false;
    batchQueue_.clear();
    batchCurrent_ = -1;

    batchOutDir_ = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/output/")
        + QDir(folderPath_).dirName());

    appendLog(QStringLiteral("Output \xe2\x86\x92 %1").arg(
                  QDir::toNativeSeparators(batchOutDir_)));
    saveRunManifest({curItem_});
    startRun(curItem_);
}

void ImageWithPromptWidget::onRunAll()
{
    if (items_.isEmpty()) return;
    if (process_ && process_->state() != QProcess::NotRunning) return;

    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set — configure it in the Pipeline tab first."),
                  QColor(255, 80, 80));
        return;
    }

    batchQueue_.clear();
    for (int i = 0; i < wItems_->count(); ++i)
        if (wItems_->item(i)->checkState() == Qt::Checked)
            batchQueue_.append(i);

    if (batchQueue_.isEmpty()) return;
    batchRunning_ = true;

    const QString ts = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    batchOutDir_ = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/output/IWP_batch_") + ts);
    QDir().mkpath(batchOutDir_);

    for (int i = 0; i < wItems_->count(); ++i)
        markItemColor(i, Qt::white);

    saveRunManifest(batchQueue_);

    appendLog(QStringLiteral("=== Batch run started: %1 item(s) ===").arg(batchQueue_.size()),
              QColor(100, 200, 255));
    startNextInBatch();
}

void ImageWithPromptWidget::onSelectAll()
{
    const int total = wItems_->count();
    if (total == 0) return;

    int checked = 0;
    for (int i = 0; i < total; ++i)
        if (wItems_->item(i)->checkState() == Qt::Checked)
            ++checked;

    const Qt::CheckState newState = (checked == total) ? Qt::Unchecked : Qt::Checked;
    for (int i = 0; i < total; ++i)
        wItems_->item(i)->setCheckState(newState);
}

void ImageWithPromptWidget::onStop()
{
    batchQueue_.clear();
    batchRunning_ = false;
    if (process_ && process_->state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("Stopping..."), QColor(255, 140, 0));
        process_->kill();
    }
}

void ImageWithPromptWidget::startNextInBatch()
{
    if (batchQueue_.isEmpty()) {
        appendLog(QStringLiteral("=== Batch run complete ==="),
                  QColor(100, 200, 100));
        batchRunning_ = false;
        updateButtons();
        return;
    }
    batchCurrent_ = batchQueue_.takeFirst();
    startRun(batchCurrent_);
}

void ImageWithPromptWidget::startRun(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= items_.size()) return;
    const auto& item = items_[itemIndex];

    const QString scriptPath = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/TestData/ImageWithPrompt/run_imagewithprompt.py"));

    if (!QFile::exists(scriptPath)) {
        appendLog(QStringLiteral("ERROR: script not found:\n  ") + scriptPath,
                  QColor(255, 80, 80));
        batchQueue_.clear(); batchRunning_ = false;
        updateButtons();
        return;
    }

    QDir().mkpath(batchOutDir_);

    if (!process_) {
        process_ = new QProcess(this);
        connect(process_, &QProcess::readyReadStandardOutput,
                this, &ImageWithPromptWidget::onProcessReadyRead);
        connect(process_, &QProcess::readyReadStandardError, this, [this]{
            process_->setReadChannel(QProcess::StandardError);
            while (process_->canReadLine()) {
                const QString line = QString::fromUtf8(process_->readLine()).trimmed();
                if (!line.isEmpty())
                    appendLog(line, QColor(255, 140, 0));
            }
        });
        connect(process_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ImageWithPromptWidget::onProcessFinished);
    }

    int seed = spnSeed_->value();
    if (chkAutoSeed_->isChecked())
        seed = static_cast<int>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--reference")   << item.filePath
         << QStringLiteral("--output-dir")  << QDir::toNativeSeparators(batchOutDir_)
         << QStringLiteral("--output-name") << QFileInfo(item.filePath).baseName()
         << QStringLiteral("--seed")        << QString::number(seed)
         << QStringLiteral("--steps")       << QString::number(spnSteps_->value())
         << QStringLiteral("--cfg")         << QString::number(spnCfg_->value(), 'f', 1)
         << QStringLiteral("--denoise")     << QString::number(spnDenoise_->value(), 'f', 2)
         << QStringLiteral("--ip-weight")   << QString::number(spnIpWeight_->value(), 'f', 2);

    if (!item.positive.isEmpty())
        args << QStringLiteral("--positive-prompt") << item.positive;
    else if (!item.category.isEmpty())
        args << QStringLiteral("--category") << item.category;

    if (!item.negative.isEmpty())
        args << QStringLiteral("--negative-prompt") << item.negative;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COMFYUI_SERVER"), comfyuiServer_);
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process_->setProcessEnvironment(env);
    process_->setWorkingDirectory(projectRoot_);

    markItemColor(itemIndex, QColor(255, 220, 0));  // yellow = running

    if (itemIndex == curItem_) {
        lblOutputPreview_->setPixmap(QPixmap());
        lblOutputPreview_->setText(QStringLiteral("Running\xe2\x80\xa6"));
    }

    appendLog(QStringLiteral("Running [%1/%2]: %3  [%4]")
              .arg(batchRunning_ ? items_.size() - batchQueue_.size() : 1)
              .arg(batchRunning_ ? items_.size() : 1)
              .arg(QFileInfo(item.filePath).fileName(),
                   item.category.isEmpty() ? QStringLiteral("prompt") : item.category));

    process_->start(pythonExe_, args);
    updateButtons();
}

// ── Process I/O ───────────────────────────────────────────────────────────────

void ImageWithPromptWidget::onProcessReadyRead()
{
    process_->setReadChannel(QProcess::StandardOutput);
    while (process_->canReadLine()) {
        const QString line = QString::fromUtf8(process_->readLine()).trimmed();
        if (!line.isEmpty())
            appendLog(line);
    }
}

void ImageWithPromptWidget::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    onProcessReadyRead();

    const int finishedIdx = batchRunning_ ? batchCurrent_ : curItem_;

    if (exitCode != 0) {
        if (finishedIdx >= 0)
            markItemColor(finishedIdx, QColor(255, 80, 80));
        appendLog(QStringLiteral("FAILED (exit %1): %2")
                  .arg(exitCode)
                  .arg(finishedIdx >= 0 && finishedIdx < items_.size()
                       ? QFileInfo(items_[finishedIdx].filePath).fileName() : QString()),
                  QColor(255, 80, 80));
        if (finishedIdx == curItem_)
            lblOutputPreview_->setText(QStringLiteral("Failed"));
        if (batchRunning_) { startNextInBatch(); return; }
        updateButtons();
        return;
    }

    // Success
    if (finishedIdx >= 0 && finishedIdx < items_.size()) {
        markItemColor(finishedIdx, QColor(100, 200, 100));
        const QString outPath = QDir::cleanPath(
            batchOutDir_ + QStringLiteral("/")
            + QFileInfo(items_[finishedIdx].filePath).baseName()
            + QStringLiteral(".png"));
        items_[finishedIdx].lastOutputPath = outPath;
        appendLog(QStringLiteral("Done: %1").arg(QFileInfo(outPath).fileName()),
                  QColor(100, 200, 100));
        if (finishedIdx == curItem_) {
            const QPixmap opx(outPath);
            if (!opx.isNull()) {
                lblOutputPreview_->setText({});
                lblOutputPreview_->setPixmap(
                    opx.scaled(lblOutputPreview_->width(), lblOutputPreview_->height(),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
    }

    if (batchRunning_) { startNextInBatch(); return; }
    updateButtons();
}
