#include "Text2ImageWidget.h"
#include "AppConfig.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QFileDialog>
#include <QFile>
#include <QDir>
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QSettings>
#include <QTimer>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCoreApplication>

// ── Construction ─────────────────────────────────────────────────────────────

Text2ImageWidget::Text2ImageWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// ── UI ────────────────────────────────────────────────────────────────────────

void Text2ImageWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Top bar ───────────────────────────────────────────────────────────
    auto* topBar = new QHBoxLayout;
    {
        auto* btn = new QPushButton(LS("Import JSON..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Import JSON...")); });
        connect(btn, &QPushButton::clicked, this, &Text2ImageWidget::onImportJson);
        topBar->addWidget(btn);
    }
    {
        auto* btn = new QPushButton(LS("Export JSON..."));
        retranslates_.push_back([btn]{ btn->setText(LS("Export JSON...")); });
        connect(btn, &QPushButton::clicked, this, &Text2ImageWidget::onExportJson);
        topBar->addWidget(btn);
    }
    btnParseTable_ = new QPushButton(LS("AI Parse Table\xe2\x80\xa6"));
    retranslates_.push_back([this]{ btnParseTable_->setText(LS("AI Parse Table\xe2\x80\xa6")); });
    btnParseTable_->setToolTip(QStringLiteral(
        "Paste an asset description table; AI generates a text prompt for each row."));
    connect(btnParseTable_, &QPushButton::clicked, this, &Text2ImageWidget::onParseTable);
    topBar->addWidget(btnParseTable_);

    lblParseStatus_ = new QLabel;
    lblParseStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    lblParseStatus_->setMinimumWidth(110);
    topBar->addWidget(lblParseStatus_);

    // Separator
    {
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setFixedWidth(2);
        topBar->addWidget(sep);
    }

    btnWakeOllama_ = new QPushButton(LS("Wake Ollama"));
    retranslates_.push_back([this]{ btnWakeOllama_->setText(LS("Wake Ollama")); });
    btnWakeOllama_->setToolTip(QStringLiteral(
        "Check if Ollama is running; start it if not.\n"
        "Ollama always listens on 127.0.0.1:11434."));
    connect(btnWakeOllama_, &QPushButton::clicked, this, &Text2ImageWidget::onWakeOllama);
    topBar->addWidget(btnWakeOllama_);

    lblOllamaStatus_ = new QLabel;
    lblOllamaStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    topBar->addWidget(lblOllamaStatus_);

    btnWakeLlama_ = new QPushButton(LS("Wake LLaMA"));
    retranslates_.push_back([this]{ btnWakeLlama_->setText(LS("Wake LLaMA")); });
    btnWakeLlama_->setToolTip(QStringLiteral(
        "Check if llama-server is running on port 8666; launch run_llama.bat if not.\n"
        "You will be asked to locate run_llama.bat on first use."));
    connect(btnWakeLlama_, &QPushButton::clicked, this, &Text2ImageWidget::onWakeLlama);
    topBar->addWidget(btnWakeLlama_);

    lblLlamaStatus_ = new QLabel;
    lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    topBar->addWidget(lblLlamaStatus_);

    topBar->addStretch();
    root->addLayout(topBar);

    // ── Main splitter ─────────────────────────────────────────────────────
    auto* vSplitter = new QSplitter(Qt::Vertical);

    auto* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setChildrenCollapsible(false);

    // ── Left: item list ───────────────────────────────────────────────────
    auto* listBox = new QGroupBox(LS("Items"));
    retranslates_.push_back([listBox]{ listBox->setTitle(LS("Items")); });
    auto* listLay = new QVBoxLayout(listBox);

    wItems_ = new QListWidget;
    listLay->addWidget(wItems_);

    auto* listBtnRow = new QHBoxLayout;
    {
        auto* btn = new QPushButton(LS("Add"));
        retranslates_.push_back([btn]{ btn->setText(LS("Add")); });
        connect(btn, &QPushButton::clicked, this, &Text2ImageWidget::onAddItem);
        listBtnRow->addWidget(btn);
    }
    {
        auto* btn = new QPushButton(LS("Remove"));
        retranslates_.push_back([btn]{ btn->setText(LS("Remove")); });
        connect(btn, &QPushButton::clicked, this, &Text2ImageWidget::onRemoveItem);
        listBtnRow->addWidget(btn);
    }
    btnRunAll_ = new QPushButton(LS("Run All \xe2\x96\xb6"));
    retranslates_.push_back([this]{ updateButtons(); });
    btnRunAll_->setEnabled(false);
    listBtnRow->addWidget(btnRunAll_);

    btnStop_ = new QPushButton(LS("Stop"));
    retranslates_.push_back([this]{ btnStop_->setText(LS("Stop")); });
    btnStop_->setEnabled(false);
    btnStop_->setFixedWidth(55);
    listBtnRow->addWidget(btnStop_);

    listLay->addLayout(listBtnRow);
    hSplitter->addWidget(listBox);

    connect(wItems_, &QListWidget::currentRowChanged,
            this, [this](int){ onItemSelectionChanged(); });
    connect(btnRunAll_, &QPushButton::clicked, this, &Text2ImageWidget::onRunAll);
    connect(btnStop_,   &QPushButton::clicked, this, &Text2ImageWidget::onStop);

    // ── Right: item detail ────────────────────────────────────────────────
    auto* detailBox = new QGroupBox(LS("Item Detail"));
    retranslates_.push_back([detailBox]{ detailBox->setTitle(LS("Item Detail")); });
    auto* detailLay = new QVBoxLayout(detailBox);

    // Name
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Name:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Name:")); });
        lbl->setFixedWidth(90);
        row->addWidget(lbl);
        edtName_ = new QLineEdit;
        edtName_->setPlaceholderText(LS("Item name"));
        edtName_->setEnabled(false);
        row->addWidget(edtName_);
        detailLay->addLayout(row);
    }

    // Save filename (derived, read-only)
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Save Filename:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Save Filename:")); });
        lbl->setFixedWidth(90);
        row->addWidget(lbl);
        lblSaveFilename_ = new QLabel;
        lblSaveFilename_->setStyleSheet(
            QStringLiteral("color:#aaa;font-style:italic;"));
        row->addWidget(lblSaveFilename_, 1);
        detailLay->addLayout(row);
    }

    // Canvas size
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(LS("Canvas Size:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Canvas Size:")); });
        lbl->setFixedWidth(90);
        row->addWidget(lbl);
        lblSize_ = new QLabel;
        lblSize_->setStyleSheet(
            QStringLiteral("color:#aaa;font-style:italic;"));
        lblSize_->setToolTip(QStringLiteral(
            "Output canvas size in pixels. Parsed from the asset table.\n"
            "This is passed directly to the ComfyUI workflow (EmptySD3LatentImage node).\n"
            "Shown as '(workflow default)' when not set."));
        row->addWidget(lblSize_, 1);
        detailLay->addLayout(row);
    }

    // Prompt
    {
        auto* lbl = new QLabel(LS("Prompt:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Prompt:")); });
        detailLay->addWidget(lbl);
        edtPrompt_ = new QPlainTextEdit;
        edtPrompt_->setPlaceholderText(LS("Enter text prompt..."));
        edtPrompt_->setEnabled(false);
        detailLay->addWidget(edtPrompt_, 1);
    }

    // Run single
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
    lblOutputPreview_->setMinimumHeight(160);
    lblOutputPreview_->setAlignment(Qt::AlignCenter);
    lblOutputPreview_->setStyleSheet(QStringLiteral(
        "QLabel{background:#1a1a1a;border:1px solid #555;"
        "border-radius:3px;color:#555;}"));
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

    connect(edtName_,   &QLineEdit::textChanged,      this, &Text2ImageWidget::onNameEdited);
    connect(edtPrompt_, &QPlainTextEdit::textChanged,  this, &Text2ImageWidget::onPromptEdited);
    connect(btnRunSingle_, &QPushButton::clicked,      this, &Text2ImageWidget::onRunSingle);
}

void Text2ImageWidget::retranslateUi()
{
    for (auto& fn : retranslates_) fn();
}

// ── Config ────────────────────────────────────────────────────────────────────

void Text2ImageWidget::setRunConfig(const QString& pythonExe,
                                     const QString& projectRoot,
                                     const QString& comfyuiServer)
{
    pythonExe_     = pythonExe;
    projectRoot_   = projectRoot;
    comfyuiServer_ = comfyuiServer;
    updateButtons();
}

// ── Item management ───────────────────────────────────────────────────────────

void Text2ImageWidget::onAddItem()
{
    T2IItem item;
    item.name         = QStringLiteral("New Item %1").arg(items_.size() + 1);
    item.saveFilename = item.name.remove(QLatin1Char(' '));
    items_.append(item);

    auto* lwi = new QListWidgetItem(item.name, wItems_);
    lwi->setForeground(Qt::white);

    wItems_->setCurrentRow(items_.size() - 1);
    updateButtons();
}

void Text2ImageWidget::onRemoveItem()
{
    if (curItem_ < 0 || curItem_ >= items_.size()) return;
    items_.removeAt(curItem_);
    delete wItems_->takeItem(curItem_);
    curItem_ = -1;
    refreshItemDetail(-1);
    updateButtons();
}

// ── Import / Export ───────────────────────────────────────────────────────────

void Text2ImageWidget::onImportJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this, LS("Import Items JSON"), LastDir::get(),
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

    items_.clear();
    wItems_->clear();

    for (const auto& v : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject obj = v.toObject();
        T2IItem item;
        item.name         = obj.value(QStringLiteral("name")).toString();
        item.prompt       = obj.value(QStringLiteral("prompt")).toString();
        item.saveFilename = obj.value(QStringLiteral("save_filename")).toString();
        if (item.saveFilename.isEmpty())
            item.saveFilename = QString(item.name).remove(QLatin1Char(' '));
        item.width  = obj.value(QStringLiteral("width")).toInt(0);
        item.height = obj.value(QStringLiteral("height")).toInt(0);
        items_.append(item);

        auto* lwi = new QListWidgetItem(item.name, wItems_);
        lwi->setForeground(Qt::white);
    }

    appendLog(QStringLiteral("Imported %1 item(s) from: %2")
              .arg(items_.size()).arg(path));
    updateButtons();
    if (!items_.isEmpty()) wItems_->setCurrentRow(0);
}

void Text2ImageWidget::onExportJson()
{
    if (items_.isEmpty()) {
        QMessageBox::information(this, LS("Export"), LS("No items to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, LS("Export Items JSON"), LastDir::get(),
        QStringLiteral("JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;
    LastDir::set(path);

    QJsonArray arr;
    for (const auto& item : std::as_const(items_)) {
        QJsonObject obj;
        obj[QStringLiteral("name")]          = item.name;
        obj[QStringLiteral("prompt")]        = item.prompt;
        obj[QStringLiteral("save_filename")] = item.saveFilename;
        if (item.width  > 0) obj[QStringLiteral("width")]  = item.width;
        if (item.height > 0) obj[QStringLiteral("height")] = item.height;
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("items")] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, LS("Export Failed"),
            LS("Cannot write file:\n") + path);
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    appendLog(QStringLiteral("Exported to: %1").arg(path));
}

// ── Item detail ───────────────────────────────────────────────────────────────

void Text2ImageWidget::onItemSelectionChanged()
{
    curItem_ = wItems_->currentRow();
    refreshItemDetail(curItem_);
}

void Text2ImageWidget::refreshItemDetail(int row)
{
    const bool valid = (row >= 0 && row < items_.size());
    busy_ = true;

    edtName_->setEnabled(valid);
    edtPrompt_->setEnabled(valid);

    if (valid) {
        const auto& item = items_[row];
        edtName_->setText(item.name);
        lblSaveFilename_->setText(item.saveFilename + QStringLiteral(".png"));
        lblSize_->setText(
            (item.width > 0 && item.height > 0)
            ? QStringLiteral("%1 × %2 px").arg(item.width).arg(item.height)
            : QStringLiteral("(workflow default)"));
        edtPrompt_->setPlainText(item.prompt);

        if (!item.lastOutputPath.isEmpty()) {
            const QPixmap px(item.lastOutputPath);
            if (!px.isNull()) {
                lblOutputPreview_->setText({});
                lblOutputPreview_->setPixmap(
                    px.scaled(lblOutputPreview_->width(),
                              lblOutputPreview_->height(),
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
        edtName_->clear();
        lblSaveFilename_->clear();
        lblSize_->clear();
        edtPrompt_->clear();
        lblOutputPreview_->setPixmap(QPixmap());
        lblOutputPreview_->setText(QStringLiteral("Output preview"));
    }

    busy_ = false;
    updateButtons();
}

void Text2ImageWidget::onNameEdited(const QString& text)
{
    if (busy_ || curItem_ < 0 || curItem_ >= items_.size()) return;
    items_[curItem_].name         = text;
    items_[curItem_].saveFilename = QString(text).remove(QLatin1Char(' '));
    lblSaveFilename_->setText(items_[curItem_].saveFilename + QStringLiteral(".png"));
    if (auto* lwi = wItems_->item(curItem_))
        lwi->setText(text);
}

void Text2ImageWidget::onPromptEdited()
{
    if (busy_ || curItem_ < 0 || curItem_ >= items_.size()) return;
    items_[curItem_].prompt = edtPrompt_->toPlainText();
}

void Text2ImageWidget::updateButtons()
{
    const bool hasItems  = !items_.isEmpty();
    const bool hasSelect = (curItem_ >= 0 && curItem_ < items_.size());
    const bool running   = process_ && process_->state() != QProcess::NotRunning;

    btnRunSingle_->setEnabled(hasSelect && !running);
    btnRunAll_->setEnabled(hasItems && !running);
    btnStop_->setEnabled(running);

    btnRunAll_->setText(hasItems
        ? QStringLiteral("Run All \xe2\x96\xb6 (%1)").arg(items_.size())
        : QStringLiteral("Run All \xe2\x96\xb6"));
}

void Text2ImageWidget::markItemColor(int row, const QColor& color)
{
    if (auto* lwi = wItems_->item(row))
        lwi->setForeground(color);
}

// ── Log ───────────────────────────────────────────────────────────────────────

void Text2ImageWidget::appendLog(const QString& text, const QColor& color)
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

void Text2ImageWidget::onRunSingle()
{
    if (curItem_ < 0 || curItem_ >= items_.size()) return;
    if (process_ && process_->state() != QProcess::NotRunning) return;
    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set."), QColor(255, 80, 80));
        return;
    }

    batchRunning_ = false;
    batchQueue_.clear();
    batchCurrent_ = -1;

    outDir_ = QDir::cleanPath(projectRoot_ + QStringLiteral("/output/Text2Image"));
    appendLog(QStringLiteral("Output \xe2\x86\x92 %1").arg(
                  QDir::toNativeSeparators(outDir_)));
    startRun(curItem_);
}

void Text2ImageWidget::onRunAll()
{
    if (items_.isEmpty()) return;
    if (process_ && process_->state() != QProcess::NotRunning) return;
    if (projectRoot_.isEmpty()) {
        appendLog(LS("ERROR: Project Root not set."), QColor(255, 80, 80));
        return;
    }

    batchRunning_ = true;
    batchQueue_.clear();
    for (int i = 0; i < items_.size(); ++i) batchQueue_.append(i);

    outDir_ = QDir::cleanPath(projectRoot_ + QStringLiteral("/output/Text2Image"));
    QDir().mkpath(outDir_);

    for (int i = 0; i < wItems_->count(); ++i) markItemColor(i, Qt::white);
    appendLog(QStringLiteral("=== Batch run started: %1 items ===").arg(items_.size()),
              QColor(100, 200, 255));
    appendLog(QStringLiteral("Output \xe2\x86\x92 %1").arg(
                  QDir::toNativeSeparators(outDir_)));
    startNextInBatch();
}

void Text2ImageWidget::onStop()
{
    batchQueue_.clear();
    batchRunning_ = false;
    if (process_ && process_->state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("Stopping..."), QColor(255, 140, 0));
        process_->kill();
    }
}

void Text2ImageWidget::startNextInBatch()
{
    if (batchQueue_.isEmpty()) {
        appendLog(QStringLiteral("=== Batch complete ==="), QColor(100, 200, 100));
        batchRunning_ = false;
        updateButtons();
        return;
    }
    batchCurrent_ = batchQueue_.takeFirst();
    startRun(batchCurrent_);
}

void Text2ImageWidget::startRun(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= items_.size()) return;
    const auto& item = items_[itemIndex];

    if (item.prompt.trimmed().isEmpty()) {
        appendLog(QStringLiteral("SKIP [%1]: prompt is empty").arg(item.name),
                  QColor(255, 140, 0));
        if (batchRunning_) { startNextInBatch(); return; }
        updateButtons();
        return;
    }

    const QString scriptPath = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/TestData/Text2Image/run_text2image.py"));

    if (!QFile::exists(scriptPath)) {
        appendLog(QStringLiteral("ERROR: script not found:\n  ") + scriptPath,
                  QColor(255, 80, 80));
        batchQueue_.clear(); batchRunning_ = false;
        updateButtons();
        return;
    }

    QDir().mkpath(outDir_);

    if (!process_) {
        process_ = new QProcess(this);
        connect(process_, &QProcess::readyReadStandardOutput,
                this, &Text2ImageWidget::onProcessReadyRead);
        connect(process_, &QProcess::readyReadStandardError, this, [this]{
            process_->setReadChannel(QProcess::StandardError);
            while (process_->canReadLine()) {
                const QString line = QString::fromUtf8(process_->readLine()).trimmed();
                if (!line.isEmpty()) appendLog(line, QColor(255, 140, 0));
            }
        });
        connect(process_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &Text2ImageWidget::onProcessFinished);
    }

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--prompt")      << item.prompt.trimmed()
         << QStringLiteral("--output-name") << item.saveFilename
         << QStringLiteral("--output-dir")  << QDir::toNativeSeparators(outDir_);
    if (item.width > 0 && item.height > 0) {
        args << QStringLiteral("--width")  << QString::number(item.width)
             << QStringLiteral("--height") << QString::number(item.height);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COMFYUI_SERVER"),    comfyuiServer_);
    env.insert(QStringLiteral("PYTHONIOENCODING"),  QStringLiteral("utf-8"));
    process_->setProcessEnvironment(env);
    process_->setWorkingDirectory(projectRoot_);

    markItemColor(itemIndex, QColor(255, 220, 0));
    if (itemIndex == curItem_) {
        lblOutputPreview_->setPixmap(QPixmap());
        lblOutputPreview_->setText(QStringLiteral("Running\xe2\x80\xa6"));
    }

    appendLog(QStringLiteral("Running [%1/%2]: %3")
              .arg(batchRunning_ ? items_.size() - batchQueue_.size() : 1)
              .arg(batchRunning_ ? items_.size() : 1)
              .arg(item.name));

    process_->start(pythonExe_, args);
    updateButtons();
}

// ── Process I/O ───────────────────────────────────────────────────────────────

void Text2ImageWidget::onProcessReadyRead()
{
    process_->setReadChannel(QProcess::StandardOutput);
    while (process_->canReadLine()) {
        const QString line = QString::fromUtf8(process_->readLine()).trimmed();
        if (!line.isEmpty()) appendLog(line);
    }
}

void Text2ImageWidget::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    onProcessReadyRead();

    const int finishedIdx = batchRunning_ ? batchCurrent_ : curItem_;

    if (exitCode != 0) {
        if (finishedIdx >= 0) markItemColor(finishedIdx, QColor(255, 80, 80));
        appendLog(QStringLiteral("FAILED (exit %1): %2")
                  .arg(exitCode)
                  .arg(finishedIdx >= 0 && finishedIdx < items_.size()
                       ? items_[finishedIdx].name : QString()),
                  QColor(255, 80, 80));
        if (finishedIdx == curItem_)
            lblOutputPreview_->setText(QStringLiteral("Failed"));
        if (batchRunning_) { startNextInBatch(); return; }
        updateButtons();
        return;
    }

    if (finishedIdx >= 0 && finishedIdx < items_.size()) {
        markItemColor(finishedIdx, QColor(100, 200, 100));
        const QString outPath = QDir::cleanPath(
            outDir_ + QStringLiteral("/")
            + items_[finishedIdx].saveFilename + QStringLiteral(".png"));
        items_[finishedIdx].lastOutputPath = outPath;
        appendLog(QStringLiteral("Done: %1").arg(items_[finishedIdx].name),
                  QColor(100, 200, 100));

        if (finishedIdx == curItem_) {
            const QPixmap px(outPath);
            if (!px.isNull()) {
                lblOutputPreview_->setText({});
                lblOutputPreview_->setPixmap(
                    px.scaled(lblOutputPreview_->width(),
                              lblOutputPreview_->height(),
                              Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
    }

    if (batchRunning_) { startNextInBatch(); return; }
    updateButtons();
}

// ── AI Parse Table ────────────────────────────────────────────────────────────

// Returns the canonical default API URL for a given backend key.
static QString backendDefaultUrl(const QString& b)
{
    if (b == QStringLiteral("ollama"))  return QStringLiteral("http://localhost:11434");
    if (b == QStringLiteral("openai"))  return QStringLiteral("https://api.openai.com/v1");
    if (b == QStringLiteral("llama"))   return QStringLiteral("http://127.0.0.1:8666/v1");
    return QStringLiteral("https://api.anthropic.com");
}

// QSettings key for saved URL per backend.
static QString backendUrlKey(const QString& b)
{
    return QStringLiteral("t2i/parseApiUrl/") + b;
}

void Text2ImageWidget::onParseTable()
{
    if (projectRoot_.isEmpty()) {
        QMessageBox::warning(this, LS("Parse Table"),
            LS("Set the Project Root in Configuration first."));
        return;
    }

    // ── Dialog ────────────────────────────────────────────────────────────
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(LS("AI Parse Asset Table"));
    dlg->resize(720, 620);

    auto* lay = new QVBoxLayout(dlg);

    // ── Backend + URL row ─────────────────────────────────────────────────
    auto* backendRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Backend:"));
        lbl->setFixedWidth(64);
        backendRow->addWidget(lbl);
    }
    auto* cbxBackend = new QComboBox;
    cbxBackend->addItem(QStringLiteral("Anthropic (Claude)"),   QStringLiteral("anthropic"));
    cbxBackend->addItem(QStringLiteral("Ollama (local, free)"), QStringLiteral("ollama"));
    cbxBackend->addItem(QStringLiteral("OpenAI (gpt-4o-mini)"), QStringLiteral("openai"));
    cbxBackend->addItem(QStringLiteral("LLaMA.cpp (local)"),    QStringLiteral("llama"));
    cbxBackend->setFixedWidth(175);
    backendRow->addWidget(cbxBackend);
    backendRow->addSpacing(8);
    {
        auto* lbl = new QLabel(LS("API URL:"));
        lbl->setFixedWidth(56);
        backendRow->addWidget(lbl);
    }
    auto* edtUrl = new QLineEdit;
    edtUrl->setToolTip(QStringLiteral(
        "API server address. Edit the IP/domain to point to your server.\n"
        "Ollama native:     http://localhost:11434\n"
        "OpenAI-compat:     http://localhost:11434/v1\n"
        "Anthropic default: https://api.anthropic.com"));
    backendRow->addWidget(edtUrl, 1);

    // Test button + status label
    auto* btnTest = new QPushButton(LS("Test"));
    btnTest->setFixedWidth(48);
    btnTest->setToolTip(QStringLiteral("Check whether the API endpoint is reachable (TCP connect)."));
    backendRow->addWidget(btnTest);
    auto* lblTestStatus = new QLabel;
    lblTestStatus->setMinimumWidth(90);
    lblTestStatus->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    backendRow->addWidget(lblTestStatus);

    lay->addLayout(backendRow);

    // ── Load saved URL (or default) as real text when backend changes ────────
    auto loadSavedUrl = [=](int) {
        const QString b = cbxBackend->currentData().toString();
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        const QString saved = s.value(backendUrlKey(b), QString()).toString();
        edtUrl->setText(saved.isEmpty() ? backendDefaultUrl(b) : saved);
        lblTestStatus->clear();
        lblTestStatus->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    };
    connect(cbxBackend, QOverload<int>::of(&QComboBox::currentIndexChanged),
            dlg, loadSavedUrl);
    loadSavedUrl(0); // seed for initial backend

    // ── Test button ───────────────────────────────────────────────────────
    connect(btnTest, &QPushButton::clicked, dlg, [=] {
        const QString testUrl = edtUrl->text().trimmed();

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

        btnTest->setEnabled(false);
        lblTestStatus->setText(LS("Checking\xe2\x80\xa6"));
        lblTestStatus->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));

        auto* proc = new QProcess(dlg);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CHECK_URL"), testUrl);
        proc->setProcessEnvironment(env);

        connect(proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                dlg, [btnTest, lblTestStatus, proc, testUrl](int code, QProcess::ExitStatus) {
            if (code == 0) {
                lblTestStatus->setText(LS("Reachable \xe2\x9c\x93"));
                lblTestStatus->setStyleSheet(
                    QStringLiteral("color:#4caf50;font-style:italic;"));
            } else {
                lblTestStatus->setText(LS("Unreachable \xe2\x9c\x97"));
                lblTestStatus->setStyleSheet(
                    QStringLiteral("color:#f44336;font-style:italic;"));
            }
            btnTest->setEnabled(true);
            proc->deleteLater();
        });

        proc->start(pythonExe_, { QStringLiteral("-c"),
                                   QString::fromLatin1(kCheckScript) });
    });

    // ── Style reference image row ─────────────────────────────────────────
    QString styleImagePath;   // updated by Browse button, read by Analyze button

    auto* styleRefRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Style ref:"));
        lbl->setFixedWidth(70);
        styleRefRow->addWidget(lbl);
    }
    auto* lblStyleImage = new QLabel(LS("(no image)"));
    lblStyleImage->setStyleSheet(QStringLiteral("color:#888;font-style:italic;"));
    lblStyleImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    lblStyleImage->setMinimumWidth(0);
    styleRefRow->addWidget(lblStyleImage, 1);

    auto* btnBrowseStyle = new QPushButton(LS("Browse\xe2\x80\xa6"));
    btnBrowseStyle->setFixedWidth(80);
    styleRefRow->addWidget(btnBrowseStyle);

    auto* btnAnalyzeStyle = new QPushButton(LS("Analyze \xe2\x96\xb6"));
    btnAnalyzeStyle->setFixedWidth(90);
    btnAnalyzeStyle->setEnabled(false);
    btnAnalyzeStyle->setToolTip(QStringLiteral(
        "Send the selected image to the AI backend and fill the Style Hint field."));
    styleRefRow->addWidget(btnAnalyzeStyle);

    auto* lblAnalyzeStatus = new QLabel;
    lblAnalyzeStatus->setMinimumWidth(100);
    lblAnalyzeStatus->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    styleRefRow->addWidget(lblAnalyzeStatus);

    lay->addLayout(styleRefRow);

    // ── Style hint row ────────────────────────────────────────────────────
    auto* styleRow = new QHBoxLayout;
    {
        auto* lbl = new QLabel(LS("Style hint:"));
        lbl->setFixedWidth(70);
        styleRow->addWidget(lbl);
    }
    auto* edtStyle = new QLineEdit;
    edtStyle->setPlaceholderText(QStringLiteral(
        "e.g. 2D mobile game asset, vibrant flat illustration (optional)"));
    styleRow->addWidget(edtStyle, 1);
    lay->addLayout(styleRow);

    // ── Browse style image ────────────────────────────────────────────────
    connect(btnBrowseStyle, &QPushButton::clicked, dlg, [=, &styleImagePath] {
        const QString path = QFileDialog::getOpenFileName(
            dlg, LS("Select Style Reference Image"), LastDir::get(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All Files (*)"));
        if (path.isEmpty()) return;
        LastDir::set(path);
        styleImagePath = path;
        // Show truncated filename
        lblStyleImage->setText(QFileInfo(path).fileName());
        lblStyleImage->setStyleSheet(QStringLiteral("color:#ddd;font-style:normal;"));
        btnAnalyzeStyle->setEnabled(true);
        lblAnalyzeStatus->clear();
    });

    // ── Analyze style ─────────────────────────────────────────────────────
    connect(btnAnalyzeStyle, &QPushButton::clicked, dlg, [=, &styleImagePath] {
        if (styleImagePath.isEmpty()) return;

        const QString scriptPath = QDir::cleanPath(
            projectRoot_ + QStringLiteral("/TestData/Text2Image/analyze_style_image.py"));
        if (!QFile::exists(scriptPath)) {
            lblAnalyzeStatus->setText(LS("Script not found"));
            lblAnalyzeStatus->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
            return;
        }

        btnAnalyzeStyle->setEnabled(false);
        lblAnalyzeStatus->setText(LS("Analyzing\xe2\x80\xa6"));
        lblAnalyzeStatus->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));

        QStringList args;
        args << QStringLiteral("-u") << scriptPath
             << QStringLiteral("--image")   << QDir::toNativeSeparators(styleImagePath)
             << QStringLiteral("--backend") << cbxBackend->currentData().toString()
             << QStringLiteral("--api-url") << edtUrl->text().trimmed();

        auto* proc = new QProcess(dlg);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        proc->setProcessEnvironment(env);
        proc->setWorkingDirectory(projectRoot_);

        // Stderr → log immediately so the traceback is visible
        connect(proc, &QProcess::readyReadStandardError, dlg,
                [this, proc] {
            proc->setReadChannel(QProcess::StandardError);
            while (proc->canReadLine()) {
                const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                if (!line.isEmpty())
                    appendLog(QStringLiteral("[analyze err] ") + line,
                              QColor(255, 140, 0));
            }
        });

        // Accumulate stdout lines to find [DONE]
        auto* lineBuffer = new QStringList;

        appendLog(QStringLiteral("=== Analyze style: %1 ===")
                  .arg(QFileInfo(styleImagePath).fileName()),
                  QColor(100, 180, 255));

        connect(proc, &QProcess::readyReadStandardOutput, dlg,
                [this, proc, lineBuffer, lblAnalyzeStatus] {
            proc->setReadChannel(QProcess::StandardOutput);
            while (proc->canReadLine()) {
                const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                if (!line.isEmpty()) {
                    lineBuffer->append(line);
                    if (line.startsWith(QStringLiteral("[PROGRESS]"))) {
                        const QString msg = line.mid(10).trimmed();
                        lblAnalyzeStatus->setText(msg);
                        appendLog(msg, QColor(255, 193, 7));
                    } else if (line.startsWith(QStringLiteral("[ERROR]"))) {
                        appendLog(line.mid(7).trimmed(), QColor(255, 80, 80));
                    }
                }
            }
        });

        connect(proc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                dlg, [this, proc, lineBuffer, btnAnalyzeStyle, lblAnalyzeStatus, edtStyle]
                     (int code, QProcess::ExitStatus) {
            // Drain remaining stdout
            proc->setReadChannel(QProcess::StandardOutput);
            while (proc->canReadLine()) {
                const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                if (!line.isEmpty()) lineBuffer->append(line);
            }
            // Drain remaining stderr
            proc->setReadChannel(QProcess::StandardError);
            while (proc->canReadLine()) {
                const QString line = QString::fromUtf8(proc->readLine()).trimmed();
                if (!line.isEmpty())
                    appendLog(QStringLiteral("[analyze err] ") + line,
                              QColor(255, 140, 0));
            }

            if (code == 0) {
                QString result;
                for (const auto& l : std::as_const(*lineBuffer)) {
                    if (l.startsWith(QStringLiteral("[DONE]")))
                        result = l.mid(6).trimmed();
                }
                if (!result.isEmpty()) {
                    edtStyle->setText(result);
                    lblAnalyzeStatus->setText(LS("Done \xe2\x9c\x93"));
                    lblAnalyzeStatus->setStyleSheet(
                        QStringLiteral("color:#4caf50;font-style:italic;"));
                    appendLog(QStringLiteral("Style: %1").arg(result),
                              QColor(100, 200, 100));
                } else {
                    lblAnalyzeStatus->setText(LS("No result"));
                    lblAnalyzeStatus->setStyleSheet(
                        QStringLiteral("color:#f44336;font-style:italic;"));
                    appendLog(QStringLiteral("Analyze style: no [DONE] line in output."),
                              QColor(255, 80, 80));
                }
            } else {
                QString errMsg;
                for (const auto& l : std::as_const(*lineBuffer)) {
                    if (l.startsWith(QStringLiteral("[ERROR]")))
                        errMsg = l.mid(7).trimmed();
                }
                const QString display = errMsg.isEmpty() ? LS("Failed") : errMsg;
                lblAnalyzeStatus->setText(display);
                lblAnalyzeStatus->setStyleSheet(
                    QStringLiteral("color:#f44336;font-style:italic;"));
                appendLog(QStringLiteral("Analyze style failed (exit %1): %2")
                          .arg(code).arg(display),
                          QColor(255, 80, 80));
            }

            btnAnalyzeStyle->setEnabled(true);
            delete lineBuffer;
            proc->deleteLater();
        });

        proc->start(pythonExe_, args);
    });

    // ── Table paste area ──────────────────────────────────────────────────
    lay->addWidget(new QLabel(LS("Paste the asset table below (tab-separated):")));
    auto* edtTable = new QPlainTextEdit;
    edtTable->setFont(QFont(QStringLiteral("Consolas"), 9));
    edtTable->setPlaceholderText(QStringLiteral(
        "檔案名稱\t格式\t尺寸\t估計大小\t說明\n"
        "bg_sky.png\tPNG\t1500x400\t200KB\t天空遠景..."));
    lay->addWidget(edtTable, 1);

    // ── Restore last session values ───────────────────────────────────────
    {
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));

        // Backend
        const int savedIdx = s.value(QStringLiteral("t2i/parseBackend"), 0).toInt();
        if (savedIdx > 0 && savedIdx < cbxBackend->count()) {
            cbxBackend->setCurrentIndex(savedIdx);
            // loadSavedUrl is triggered by currentIndexChanged signal above
        }

        // Style hint
        edtStyle->setText(s.value(QStringLiteral("t2i/parseStyleHint")).toString());

        // Style image path
        const QString savedImg = s.value(QStringLiteral("t2i/parseStyleImagePath")).toString();
        if (!savedImg.isEmpty() && QFile::exists(savedImg)) {
            styleImagePath = savedImg;
            lblStyleImage->setText(QFileInfo(savedImg).fileName());
            lblStyleImage->setStyleSheet(QStringLiteral("color:#ddd;font-style:normal;"));
            btnAnalyzeStyle->setEnabled(true);
        }

        // Table text
        edtTable->setPlainText(s.value(QStringLiteral("t2i/parseTableText")).toString());
    }

    // ── Dialog buttons ────────────────────────────────────────────────────
    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    btnBox->button(QDialogButtonBox::Ok)->setText(LS("Generate Prompts"));
    lay->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) { dlg->deleteLater(); return; }

    const QString tableText = edtTable->toPlainText().trimmed();
    parseBackend_ = cbxBackend->currentData().toString();
    parseApiUrl_  = edtUrl->text().trimmed();
    const QString styleHint = edtStyle->text().trimmed();

    // ── Save session values on accept ─────────────────────────────────────
    {
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        s.setValue(QStringLiteral("t2i/parseBackend"),       cbxBackend->currentIndex());
        s.setValue(QStringLiteral("t2i/parseStyleHint"),     styleHint);
        if (!styleImagePath.isEmpty())
            s.setValue(QStringLiteral("t2i/parseStyleImagePath"), styleImagePath);
        if (!tableText.isEmpty())
            s.setValue(QStringLiteral("t2i/parseTableText"), tableText);
    }

    dlg->deleteLater();

    if (tableText.isEmpty()) {
        QMessageBox::information(this, LS("Parse Table"), LS("Table text is empty."));
        return;
    }

    // Write table to temp file
    const QString tempDir = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/output/Text2Image"));
    QDir().mkpath(tempDir);

    parseTempFile_   = tempDir + QStringLiteral("/_parse_input.tsv");
    parseOutputFile_ = tempDir + QStringLiteral("/_parse_output.json");

    {
        QFile f(parseTempFile_);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, LS("Parse Table"),
                LS("Cannot write temp file:\n") + parseTempFile_);
            return;
        }
        f.write(tableText.toUtf8());
    }

    const QString scriptPath = QDir::cleanPath(
        projectRoot_ + QStringLiteral("/TestData/Text2Image/parse_table_to_items.py"));
    if (!QFile::exists(scriptPath)) {
        QMessageBox::warning(this, LS("Parse Table"),
            LS("Script not found:\n") + scriptPath);
        return;
    }

    QStringList args;
    args << QStringLiteral("-u") << scriptPath
         << QStringLiteral("--table")   << QDir::toNativeSeparators(parseTempFile_)
         << QStringLiteral("--output")  << QDir::toNativeSeparators(parseOutputFile_)
         << QStringLiteral("--backend") << parseBackend_;
    args << QStringLiteral("--api-url") << parseApiUrl_;
    if (!styleHint.isEmpty())
        args << QStringLiteral("--style") << styleHint;

    if (!parseProcess_) {
        parseProcess_ = new QProcess(this);
        connect(parseProcess_, &QProcess::readyReadStandardOutput,
                this, &Text2ImageWidget::onParseProcessReadyRead);
        connect(parseProcess_, &QProcess::readyReadStandardError, this, [this]{
            parseProcess_->setReadChannel(QProcess::StandardError);
            while (parseProcess_->canReadLine()) {
                const QString line = QString::fromUtf8(parseProcess_->readLine()).trimmed();
                if (!line.isEmpty())
                    appendLog(QStringLiteral("[parse err] ") + line, QColor(255, 140, 0));
            }
        });
        connect(parseProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &Text2ImageWidget::onParseProcessFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    parseProcess_->setProcessEnvironment(env);
    parseProcess_->setWorkingDirectory(projectRoot_);

    appendLog(QStringLiteral("=== AI Parse Table: backend=%1 url=%2 ===")
              .arg(parseBackend_,
                   parseApiUrl_.isEmpty() ? backendDefaultUrl(parseBackend_) : parseApiUrl_),
              QColor(100, 180, 255));
    parseProcess_->start(pythonExe_, args);

    // Heartbeat: disable button and show elapsed time every 5 s while LLM is generating
    btnParseTable_->setEnabled(false);
    parseStartSecs_ = QDateTime::currentSecsSinceEpoch();
    lblParseStatus_->setText(LS("Generating\xe2\x80\xa6"));
    lblParseStatus_->setStyleSheet(QStringLiteral("color:#ffb300;font-style:italic;"));

    if (!parseHeartbeat_) {
        parseHeartbeat_ = new QTimer(this);
        connect(parseHeartbeat_, &QTimer::timeout,
                this, &Text2ImageWidget::onParseHeartbeat);
    }
    parseHeartbeat_->start(5000);
}

void Text2ImageWidget::onParseProcessReadyRead()
{
    parseProcess_->setReadChannel(QProcess::StandardOutput);
    while (parseProcess_->canReadLine()) {
        const QString line = QString::fromUtf8(parseProcess_->readLine()).trimmed();
        if (!line.isEmpty()) appendLog(line);
    }
}

void Text2ImageWidget::onParseProcessFinished(int exitCode, QProcess::ExitStatus)
{
    if (parseHeartbeat_) parseHeartbeat_->stop();
    btnParseTable_->setEnabled(true);

    onParseProcessReadyRead();

    if (exitCode != 0) {
        lblParseStatus_->setText(LS("Failed \xe2\x9c\x97"));
        lblParseStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
        appendLog(QStringLiteral("Parse failed (exit %1).").arg(exitCode),
                  QColor(255, 80, 80));
        return;
    }

    // Save the URL only on success so that next time the working URL is pre-filled.
    if (!parseApiUrl_.isEmpty()) {
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        s.setValue(backendUrlKey(parseBackend_), parseApiUrl_);
    }

    // Load the generated JSON and append as items
    QFile f(parseOutputFile_);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("Cannot read output: ") + parseOutputFile_,
                  QColor(255, 80, 80));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray  arr  = root.value(QStringLiteral("items")).toArray();

    int added = 0;
    for (const auto& v : arr) {
        const QJsonObject obj = v.toObject();
        T2IItem item;
        item.name         = obj.value(QStringLiteral("name")).toString();
        item.prompt       = obj.value(QStringLiteral("prompt")).toString();
        item.saveFilename = obj.value(QStringLiteral("save_filename")).toString();
        if (item.saveFilename.isEmpty())
            item.saveFilename = QString(item.name).remove(QLatin1Char(' '));
        item.width  = obj.value(QStringLiteral("width")).toInt(0);
        item.height = obj.value(QStringLiteral("height")).toInt(0);
        if (item.name.isEmpty()) continue;
        items_.append(item);
        auto* lwi = new QListWidgetItem(item.name, wItems_);
        lwi->setForeground(Qt::white);
        ++added;
    }

    appendLog(QStringLiteral("Imported %1 item(s) from AI parse.").arg(added),
              QColor(100, 200, 100));
    lblParseStatus_->setText(LS("Done \xe2\x9c\x93"));
    lblParseStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
    updateButtons();
    if (added > 0 && wItems_->currentRow() < 0)
        wItems_->setCurrentRow(0);

    // Clean up temp input file
    QFile::remove(parseTempFile_);
}

// ── Parse heartbeat ───────────────────────────────────────────────────────────

void Text2ImageWidget::onParseHeartbeat()
{
    const qint64 elapsed = QDateTime::currentSecsSinceEpoch() - parseStartSecs_;
    const QString timeStr = elapsed >= 60
        ? QStringLiteral("%1m %2s").arg(elapsed / 60).arg(elapsed % 60)
        : QStringLiteral("%1s").arg(elapsed);
    lblParseStatus_->setText(QStringLiteral("Generating\xe2\x80\xa6 %1").arg(timeStr));
    appendLog(QStringLiteral("Still generating\xe2\x80\xa6 (%1 elapsed)").arg(timeStr),
              QColor(120, 120, 120));
}

// ── Wake Ollama ───────────────────────────────────────────────────────────────

void Text2ImageWidget::onWakeOllama()
{
    const char* kCheckScript =
        "import socket,sys,os,urllib.parse;"
        "p=urllib.parse.urlparse(os.environ['CHECK_URL']);"
        "h=p.hostname or 'localhost';"
        "port=p.port or(443 if p.scheme=='https' else 80);"
        "s=socket.socket();"
        "s.settimeout(3);"
        "r=s.connect_ex((h,port));"
        "s.close();"
        "sys.exit(r)";

    const QString kOllamaUrl = QStringLiteral("http://127.0.0.1:11434");

    btnWakeOllama_->setEnabled(false);
    lblOllamaStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    lblOllamaStatus_->setText(LS("Checking\xe2\x80\xa6"));

    // Helper: run a TCP check and call back with true = reachable
    auto doCheck = [this, kCheckScript, kOllamaUrl](auto&& onDone) {
        auto* proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CHECK_URL"), kOllamaUrl);
        proc->setProcessEnvironment(env);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [proc, onDone](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            onDone(code == 0);
        });
        proc->start(pythonExe_, { QStringLiteral("-c"),
                                   QString::fromLatin1(kCheckScript) });
    };

    // First check: is Ollama already alive?
    doCheck([this, doCheck](bool alive) {
        if (alive) {
            lblOllamaStatus_->setText(LS("Ollama already awake \xe2\x9c\x93"));
            lblOllamaStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
            btnWakeOllama_->setEnabled(true);
            appendLog(QStringLiteral("Ollama: already running at 127.0.0.1:11434"),
                      QColor(100, 200, 100));
            return;
        }

        // Not alive — start ollama serve (detached, runs indefinitely)
        lblOllamaStatus_->setText(LS("Starting Ollama\xe2\x80\xa6"));
        appendLog(QStringLiteral("Ollama: not running — launching 'ollama serve'..."),
                  QColor(255, 193, 7));

        const bool launched = QProcess::startDetached(
            QStringLiteral("ollama"), { QStringLiteral("serve") });

        if (!launched) {
            lblOllamaStatus_->setText(LS("ollama not found \xe2\x9c\x97"));
            lblOllamaStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
            btnWakeOllama_->setEnabled(true);
            appendLog(QStringLiteral("Ollama: could not start — is 'ollama' in PATH?"),
                      QColor(255, 80, 80));
            return;
        }

        // Wait 3 s then re-check
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, timer, doCheck] {
            timer->deleteLater();
            doCheck([this](bool alive2) {
                if (alive2) {
                    lblOllamaStatus_->setText(LS("Ollama awake \xe2\x9c\x93"));
                    lblOllamaStatus_->setStyleSheet(
                        QStringLiteral("color:#4caf50;font-style:italic;"));
                    appendLog(QStringLiteral("Ollama: started successfully."),
                              QColor(100, 200, 100));
                } else {
                    lblOllamaStatus_->setText(LS("Failed to start \xe2\x9c\x97"));
                    lblOllamaStatus_->setStyleSheet(
                        QStringLiteral("color:#f44336;font-style:italic;"));
                    appendLog(QStringLiteral("Ollama: started but port 11434 still unreachable."),
                              QColor(255, 80, 80));
                }
                btnWakeOllama_->setEnabled(true);
            });
        });
        timer->start(3000);
    });
}

// ── Wake LLaMA ────────────────────────────────────────────────────────────────

void Text2ImageWidget::onWakeLlama()
{
    const char* kCheckScript =
        "import socket,sys,os,urllib.parse;"
        "p=urllib.parse.urlparse(os.environ['CHECK_URL']);"
        "h=p.hostname or 'localhost';"
        "port=p.port or 8666;"
        "s=socket.socket();"
        "s.settimeout(3);"
        "r=s.connect_ex((h,port));"
        "s.close();"
        "sys.exit(r)";

    const QString kLlamaUrl = QStringLiteral("http://127.0.0.1:8666");

    btnWakeLlama_->setEnabled(false);
    lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
    lblLlamaStatus_->setText(LS("Checking\xe2\x80\xa6"));

    auto doCheck = [this, kCheckScript, kLlamaUrl](auto&& onDone) {
        auto* proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("CHECK_URL"), kLlamaUrl);
        proc->setProcessEnvironment(env);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [proc, onDone](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            onDone(code == 0);
        });
        proc->start(pythonExe_, { QStringLiteral("-c"),
                                   QString::fromLatin1(kCheckScript) });
    };

    doCheck([this](bool alive) {
        if (alive) {
            lblLlamaStatus_->setText(LS("LLaMA already awake \xe2\x9c\x93"));
            lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-style:italic;"));
            btnWakeLlama_->setEnabled(true);
            appendLog(QStringLiteral("LLaMA: already running at 127.0.0.1:8666"),
                      QColor(100, 200, 100));
            return;
        }

        // Resolve bat path from QSettings, falling back to a file dialog
        QSettings s(GS_ORG_NAME, QStringLiteral("GameStyleAI"));
        QString batPath = s.value(QStringLiteral("llama/batPath")).toString();

        if (batPath.isEmpty() || !QFileInfo::exists(batPath)) {
            // Try to find run_llama.bat next to the application or in the project tree
            const QString appDir = QCoreApplication::applicationDirPath();
            const QStringList candidates = {
                appDir + QStringLiteral("/run_llama.bat"),
                appDir + QStringLiteral("/../Doc/Local AI Deploy/run_llama.bat"),
                appDir + QStringLiteral("/../../Doc/Local AI Deploy/run_llama.bat"),
                appDir + QStringLiteral("/../../../Doc/Local AI Deploy/run_llama.bat"),
            };
            for (const QString& c : candidates) {
                if (QFileInfo::exists(c)) { batPath = QDir::toNativeSeparators(QFileInfo(c).absoluteFilePath()); break; }
            }
        }

        if (batPath.isEmpty() || !QFileInfo::exists(batPath)) {
            batPath = QFileDialog::getOpenFileName(
                this, LS("Locate run_llama.bat"),
                QDir::homePath(),
                QStringLiteral("Batch files (*.bat);;All files (*)"));
        }

        if (batPath.isEmpty()) {
            lblLlamaStatus_->setText(LS("Cancelled"));
            lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#aaa;font-style:italic;"));
            btnWakeLlama_->setEnabled(true);
            return;
        }

        if (!QFileInfo::exists(batPath)) {
            lblLlamaStatus_->setText(LS("Bat not found \xe2\x9c\x97"));
            lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
            btnWakeLlama_->setEnabled(true);
            appendLog(QStringLiteral("LLaMA: run_llama.bat not found at: ") + batPath,
                      QColor(255, 80, 80));
            return;
        }

        // Persist validated path
        s.setValue(QStringLiteral("llama/batPath"), batPath);

        lblLlamaStatus_->setText(LS("Launching\xe2\x80\xa6 pick model in console"));
        lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#ffb300;font-style:italic;"));
        appendLog(QStringLiteral("LLaMA: launching ") + batPath, QColor(255, 193, 7));

        const bool launched = QProcess::startDetached(
            QStringLiteral("cmd.exe"),
            { QStringLiteral("/k"), batPath });

        if (!launched) {
            lblLlamaStatus_->setText(LS("Failed to launch \xe2\x9c\x97"));
            lblLlamaStatus_->setStyleSheet(QStringLiteral("color:#f44336;font-style:italic;"));
            appendLog(QStringLiteral("LLaMA: could not start cmd.exe with run_llama.bat"),
                      QColor(255, 80, 80));
        }

        btnWakeLlama_->setEnabled(true);
    });
}
