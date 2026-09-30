#include "AtlasWidget.h"
#include "AtlasOps.h"
#include "AppConfig.h"
#include "LangManager.h"
#include "LastDir.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QTime>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>

static const QString ATLAS_SETTINGS_GROUP = QStringLiteral("AtlasTools");

AtlasWidget::AtlasWidget(QWidget* parent)
    : QWidget(parent)
    , settings_(GS_ORG_NAME, QStringLiteral("GameStyleAI"))
{
    buildUi();
    settings_.beginGroup(ATLAS_SETTINGS_GROUP);
    edtAtlasFile_->setText(settings_.value(QStringLiteral("lastAtlasFile")).toString());
    settings_.endGroup();
}

// ── UI ────────────────────────────────────────────────────────────────────────

void AtlasWidget::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 16, 24, 16);
    mainLayout->setSpacing(12);

    lblTitle_ = new QLabel(LS("Atlas Tools"));
    retranslates_.push_back([this]{ lblTitle_->setText(LS("Atlas Tools")); });
    QFont tf = lblTitle_->font();
    tf.setPointSize(16);
    tf.setBold(true);
    lblTitle_->setFont(tf);
    lblTitle_->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblTitle_);

    // Help button
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch();
        auto* btn = new QPushButton(LS("?  Help"));
        retranslates_.push_back([btn]{ btn->setText(LS("?  Help")); });
        btn->setFixedWidth(80);
        btn->setToolTip(QStringLiteral("How to use the Atlas Tools tab"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2a6496;color:white;border-radius:4px;"
            "font-weight:bold;padding:3px 8px;border:none;}"
            "QPushButton:hover{background:#337ab7;}"));
        connect(btn, &QPushButton::clicked, this, [this]{
            QMessageBox mb(this);
            mb.setWindowTitle(LS("Atlas Tools -- Help"));
            mb.setIcon(QMessageBox::Information);
            mb.setText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.atlas.text")
                : QStringLiteral(
                    "Extracts individual sprites from a Spine .atlas texture sheet\n"
                    "and merges them back after editing (e.g. after AI re-styling).\n"
                    "\n"
                    "WORKFLOW\n"
                    "  Extract  ->  edit individual sprites  ->  Merge Back\n"
                    "\n"
                    "STEP 1 -- Extract\n"
                    "  1. Browse to a Spine .atlas file.\n"
                    "  2. Optionally set a Scale (1.0 = original size).\n"
                    "  3. Click Extract.\n"
                    "  Each sprite is saved as a PNG in an 'atlas/' sub-folder\n"
                    "  next to the original texture file.\n"
                    "\n"
                    "STEP 2 -- Edit sprites\n"
                    "  Process the extracted PNGs with the Pipeline or Symbol Groups tab.\n"
                    "  Keep the same filenames -- Merge Back matches by filename.\n"
                    "\n"
                    "STEP 3 -- Merge Back\n"
                    "  1. Select the same .atlas file.\n"
                    "  2. Click Merge Back.\n"
                    "  Sprites from atlas/ are composited back into the atlas texture."));
            mb.setDetailedText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.atlas.details")
                : QStringLiteral(
                    "SCALE FIELD\n"
                    "  1.0  -- original size; output files have no suffix.\n"
                    "  0.5  -- half size; output goes to atlas_0.5x/ + _0.5x.atlas\n"
                    "  2.0  -- double size; output goes to atlas_2x/ + _2x.atlas\n"
                    "\n"
                    "OUTPUT FILES\n"
                    "  Extract:    <atlas_dir>/atlas/<sprite_name>.png  (one per sprite)\n"
                    "  Merge Back: <atlas_dir>/atlas_<scale>/<texture>.png  (full sheet)\n"
                    "              <atlas_dir>/<atlas_name>_<scale>.atlas   (rescaled .atlas)\n"
                    "\n"
                    "COMMON ISSUES\n"
                    "  'Cannot open atlas file'   -- Check the path; file must be readable.\n"
                    "  Blank sprites after extract -- The region has size 0 in the atlas.\n"
                    "  Merge output looks wrong   -- Do not resize or rename extracted PNGs\n"
                    "                               before merging.\n"
                    "  Missing sprites in merge   -- Make sure the atlas/ sub-folder exists\n"
                    "                               and contains the expected PNGs."));
            mb.exec();
        });
        hdr->addWidget(btn);
        mainLayout->addLayout(hdr);
    }

    // ── File picker ──────────────────────────────────────────────────────
    grpAtlasFiles_ = new QGroupBox(LS("Atlas File"));
    retranslates_.push_back([this]{ grpAtlasFiles_->setTitle(LS("Atlas File")); });
    grpAtlasFiles_->setStyleSheet(QStringLiteral(
        "QGroupBox { border: 2px solid #0d6efd; border-radius: 6px; margin-top: 6px; padding-top: 6px; }"
        "QGroupBox::title { color: #0d6efd; font-weight: bold; }"));
    auto* fileGrid = new QGridLayout(grpAtlasFiles_);
    fileGrid->setColumnStretch(1, 1);

    lblAtlasFile_ = new QLabel(LS("Atlas File:"));
    retranslates_.push_back([this]{ lblAtlasFile_->setText(LS("Atlas File:")); });
    fileGrid->addWidget(lblAtlasFile_, 0, 0);
    edtAtlasFile_ = new QLineEdit;
    edtAtlasFile_->setPlaceholderText(LS("Select a .atlas file"));
    fileGrid->addWidget(edtAtlasFile_, 0, 1);
    btnBrowse_ = new QPushButton(LS("Browse..."));
    retranslates_.push_back([this]{ btnBrowse_->setText(LS("Browse...")); });
    btnBrowse_->setFixedWidth(80);
    connect(btnBrowse_, &QPushButton::clicked, this, &AtlasWidget::onBrowseAtlas);
    fileGrid->addWidget(btnBrowse_, 0, 2);

    lblScale_ = new QLabel(LS("Scale:"));
    retranslates_.push_back([this]{ lblScale_->setText(LS("Scale:")); });
    fileGrid->addWidget(lblScale_, 1, 0);
    spnScale_ = new QDoubleSpinBox;
    spnScale_->setRange(0.1, 4.0);
    spnScale_->setSingleStep(0.25);
    spnScale_->setDecimals(2);
    spnScale_->setValue(1.0);
    spnScale_->setSuffix(QStringLiteral("x"));
    spnScale_->setFixedWidth(90);
    spnScale_->setToolTip(QStringLiteral(
        "1.0 = original size\n"
        "0.5 = half size -> atlas_0.5x/ folder + _0.5x.atlas\n"
        "2.0 = double size -> atlas_2x/ folder + _2x.atlas"));
    fileGrid->addWidget(spnScale_, 1, 1, Qt::AlignLeft);

    mainLayout->addWidget(grpAtlasFiles_);

    // ── Action buttons ───────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(15);
    btnRow->addStretch();

    btnExtract_ = new QPushButton(LS("Extract"));
    retranslates_.push_back([this]{ btnExtract_->setText(LS("Extract")); });
    btnExtract_->setMinimumWidth(190);
    btnExtract_->setMinimumHeight(42);
    btnExtract_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #0d6efd; color: white; font-weight: bold; font-size: 13px;"
        "  padding: 8px 20px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background: #0a58ca; }"
        "QPushButton:disabled { background: #6c757d; }"));
    connect(btnExtract_, &QPushButton::clicked, this, &AtlasWidget::onExtract);

    btnMerge_ = new QPushButton(LS("Merge Back"));
    retranslates_.push_back([this]{ btnMerge_->setText(LS("Merge Back")); });
    btnMerge_->setMinimumWidth(210);
    btnMerge_->setMinimumHeight(42);
    btnMerge_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #fd7e14; color: white; font-weight: bold; font-size: 13px;"
        "  padding: 8px 20px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background: #e96b0c; }"
        "QPushButton:disabled { background: #6c757d; }"));
    connect(btnMerge_, &QPushButton::clicked, this, &AtlasWidget::onMerge);

    btnRow->addWidget(btnExtract_);
    btnRow->addWidget(btnMerge_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // ── Description labels ───────────────────────────────────────────────
    auto* descLabel = new QLabel(
        LS("Extract: unpack atlas texture to atlas/ sub-folder (preserves orig size and alpha)\n"
           "Merge Back: read images from atlas/ sub-folder and composite into atlas texture"));
    retranslates_.push_back([descLabel]{
        descLabel->setText(
            LS("Extract: unpack atlas texture to atlas/ sub-folder (preserves orig size and alpha)\n"
               "Merge Back: read images from atlas/ sub-folder and composite into atlas texture"));
    });
    descLabel->setStyleSheet(QStringLiteral("color: #6c757d; font-size: 11px;"));
    descLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(descLabel);

    // ── Log ──────────────────────────────────────────────────────────────
    auto* logGroup = new QGroupBox(QStringLiteral("Log"));
    auto* logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(6, 4, 6, 4);
    logLayout->setSpacing(4);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(3000);
    {
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        logView_->setFont(mono);
    }
    logView_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #1e1e1e; color: #d4d4d4; border: none; }"));
    logLayout->addWidget(logView_);

    auto* logBtnRow = new QHBoxLayout;
    logBtnRow->addStretch();
    auto* btnClear = new QPushButton(LS("Clear Log"));
    retranslates_.push_back([btnClear]{ btnClear->setText(LS("Clear Log")); });
    connect(btnClear, &QPushButton::clicked, logView_, &QPlainTextEdit::clear);
    logBtnRow->addWidget(btnClear);
    logLayout->addLayout(logBtnRow);

    mainLayout->addWidget(logGroup, 1);
}

void AtlasWidget::retranslateUi()
{
    for (auto& fn : retranslates_)
        fn();
}

void AtlasWidget::appendLog(const QString& msg)
{
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    logView_->appendPlainText(QStringLiteral("[%1] %2").arg(ts, msg));
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// ── File browser ──────────────────────────────────────────────────────────────

void AtlasWidget::onBrowseAtlas()
{
    const QString path = QFileDialog::getOpenFileName(
        this, LS("Select Atlas File"), LastDir::get(),
        QStringLiteral("Spine Atlas (*.atlas);;All Files (*)"));
    if (path.isEmpty()) return;

    LastDir::set(path);
    edtAtlasFile_->setText(path);
    settings_.beginGroup(ATLAS_SETTINGS_GROUP);
    settings_.setValue(QStringLiteral("lastAtlasFile"), path);
    settings_.endGroup();
    settings_.sync();
}

// ── Extract ───────────────────────────────────────────────────────────────────
// Logic lives in AtlasOps::extractAtlas() (shared with the headless atlas-extract command);
// this just supplies the atlas path/scale from the UI and shows the result.

void AtlasWidget::onExtract()
{
    const QString atlasPath = edtAtlasFile_->text().trimmed();
    if (atlasPath.isEmpty()) {
        QMessageBox::warning(this, LS("Error"), LS("Please select an Atlas file."));
        return;
    }

    const double scale = spnScale_->value();
    btnExtract_->setEnabled(false);
    btnMerge_->setEnabled(false);

    QStringList log;
    const AtlasOps::ExtractResult result = AtlasOps::extractAtlas(atlasPath, scale, log);
    for (const QString& line : std::as_const(log)) appendLog(line);

    btnExtract_->setEnabled(true);
    btnMerge_->setEnabled(true);

    if (!result.ok) {
        QMessageBox::critical(this, LS("Error"), result.error);
        return;
    }

    QMessageBox mb(this);
    mb.setWindowTitle(LS("Extract Complete"));
    mb.setIcon(QMessageBox::Information);
    mb.setText(LS("Extracted %1 sprites to:\n%2").arg(result.saved).arg(result.outputDir));
    auto* btnOpen = mb.addButton(LS("Open Folder"), QMessageBox::ActionRole);
    mb.addButton(QMessageBox::Ok);
    mb.setDefaultButton(QMessageBox::Ok);
    mb.exec();
    if (mb.clickedButton() == btnOpen)
        QDesktopServices::openUrl(QUrl::fromLocalFile(result.outputDir));
}

// ── Merge Back ────────────────────────────────────────────────────────────────
// Logic lives in AtlasOps::mergeAtlas(); this resolves the two choices it used to ask about
// itself via QMessageBox (multi-page layout, and what to do about an already-existing output
// file) up front and passes them in as plain arguments — the headless atlas-merge command
// supplies the same two choices from its args instead of a dialog.

void AtlasWidget::onMerge()
{
    const QString atlasPath = edtAtlasFile_->text().trimmed();
    if (atlasPath.isEmpty()) {
        QMessageBox::warning(this, LS("Error"), LS("Please select an Atlas file."));
        return;
    }

    const double scale = spnScale_->value();

    // Peek at the page count/texture names to decide which questions are even worth asking —
    // mergeAtlas() parses the (tiny, text) atlas file again itself, which is fine.
    QList<AtlasOps::AtlasPage> pages;
    QStringList peekLog;
    if (!AtlasOps::parseAtlas(atlasPath, pages, peekLog)) {
        QMessageBox::critical(this, LS("Error"), peekLog.join(QStringLiteral("\n")));
        return;
    }

    AtlasOps::MergeMode mode = AtlasOps::MergeMode::SeparatePages;
    if (pages.size() > 1) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(QStringLiteral("Multi-page Atlas"));
        msgBox.setText(QStringLiteral(
            "This Atlas has %1 texture pages. Choose output mode:").arg(pages.size()));
        msgBox.setIcon(QMessageBox::Question);
        auto* btnOne = msgBox.addButton(
            QStringLiteral("Merge into single image (horizontal)"), QMessageBox::AcceptRole);
        auto* btnSep = msgBox.addButton(
            QStringLiteral("Keep original page count (%1 files)").arg(pages.size()),
            QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        msgBox.exec();

        if (msgBox.clickedButton() == btnOne)      mode = AtlasOps::MergeMode::CombineHorizontal;
        else if (msgBox.clickedButton() == btnSep) mode = AtlasOps::MergeMode::SeparatePages;
        else                                        return;
    }

    // Only ask about an existing output file if one would actually be written over — same
    // target-path naming mergeAtlas() itself uses.
    const QDir atlasDir = QFileInfo(atlasPath).absoluteDir();
    const QFileInfo atlasInfo(atlasPath);
    const QString tag = AtlasOps::scaleTag(scale);
    const QString fileTag = tag.isEmpty() ? QStringLiteral("_1x") : tag;
    const bool mergeIntoOne = (mode == AtlasOps::MergeMode::CombineHorizontal) && pages.size() > 1;

    QStringList expectedTargets;
    if (mergeIntoOne) {
        expectedTargets << atlasDir.absoluteFilePath(
            atlasInfo.baseName() + QStringLiteral("_combined") + fileTag + QStringLiteral(".png"));
    } else {
        for (const auto& page : std::as_const(pages)) {
            const QFileInfo fi(page.textureName);
            expectedTargets << atlasDir.absoluteFilePath(
                fi.baseName() + fileTag + QStringLiteral(".") + fi.suffix());
        }
    }

    AtlasOps::SaveConflict conflict = AtlasOps::SaveConflict::Replace;
    const bool anyExists = std::any_of(expectedTargets.cbegin(), expectedTargets.cend(),
                                        [](const QString& p) { return QFile::exists(p); });
    if (anyExists) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(LS("File Already Exists"));
        msgBox.setText(LS("One or more output files already exist:\n%1\n\nChoose an action:")
                           .arg(expectedTargets.join(QStringLiteral("\n"))));
        msgBox.setIcon(QMessageBox::Question);
        auto* btnReplace = msgBox.addButton(LS("Replace"), QMessageBox::AcceptRole);
        auto* btnRename  = msgBox.addButton(LS("Save as ..._merged"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        msgBox.exec();

        if (msgBox.clickedButton() == btnReplace)     conflict = AtlasOps::SaveConflict::Replace;
        else if (msgBox.clickedButton() == btnRename) conflict = AtlasOps::SaveConflict::RenameMerged;
        else                                           return;
    }

    btnExtract_->setEnabled(false);
    btnMerge_->setEnabled(false);

    QStringList log;
    const AtlasOps::MergeResult result = AtlasOps::mergeAtlas(atlasPath, scale, mode, conflict, log);
    for (const QString& line : std::as_const(log)) appendLog(line);

    btnExtract_->setEnabled(true);
    btnMerge_->setEnabled(true);

    if (!result.ok) {
        QMessageBox::critical(this, LS("Error"), result.error);
        return;
    }

    appendLog(QStringLiteral("=== Merge Back complete ==="));
    QMessageBox::information(this, LS("Done"), LS("Merge Back complete!"));
}
