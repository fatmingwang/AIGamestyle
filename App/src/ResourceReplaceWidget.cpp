#include "ResourceReplaceWidget.h"
#include "AppConfig.h"
#include "LangManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QFont>
#include <QTime>
#include <QDateTime>
#include <QDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslConfiguration>

static const QString SETTINGS_GROUP = QStringLiteral("ResourceReplace");

ResourceReplaceWidget::ResourceReplaceWidget(QWidget* parent)
    : QWidget(parent)
    , settings_(GS_ORG_NAME, QStringLiteral("GameStyleAI"))
    , nam_(new QNetworkAccessManager(this))
{
    buildUi();
    loadFormData();
}

// ── UI construction ───────────────────────────────────────────────────────────

void ResourceReplaceWidget::buildUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Scrollable form area ─────────────────────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget;
    scroll->setWidget(inner);

    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    // Title
    lblTitle_ = new QLabel(LS("Online Resource Replace"));
    retranslates_.push_back([this]{ lblTitle_->setText(LS("Online Resource Replace")); });
    QFont titleFont = lblTitle_->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    lblTitle_->setFont(titleFont);
    lblTitle_->setAlignment(Qt::AlignCenter);
    layout->addWidget(lblTitle_);

    // Help button
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch();
        auto* btn = new QPushButton(LS("?  Help"));
        retranslates_.push_back([btn]{ btn->setText(LS("?  Help")); });
        btn->setFixedWidth(80);
        btn->setToolTip(QStringLiteral("How to use this tab"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#2a6496;color:white;border-radius:4px;"
            "font-weight:bold;padding:3px 8px;border:none;}"
            "QPushButton:hover{background:#337ab7;}"));
        connect(btn, &QPushButton::clicked, this, [this]{
            QMessageBox mb(this);
            mb.setWindowTitle(LS("Online Resource Replace -- Help"));
            mb.setIcon(QMessageBox::Information);
            mb.setText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.resource.text")
                : QStringLiteral(
                    "Uploads AI-reskinned art assets to a NAS share and triggers\n"
                    "live replacement on the game server.\n"
                    "\n"
                    "WORKFLOW\n"
                    "  1. Set NAS target folder and click 'Upload Output to NAS'.\n"
                    "  2. Fill in the server address (e.g. https://localhost:3443).\n"
                    "  3. Fill in NAS root, Target folder, Game ID, Platform, Operator.\n"
                    "  4. Select deployment environment (Dev and/or QA).\n"
                    "  5. Click 'Start Replace'."));
            mb.setDetailedText(LangManager::instance().lang() == AppLang::ZH_TW
                ? LangManager::instance().get("help.resource.details")
                : QStringLiteral(
                    "FIELDS\n"
                    "  NAS target folder  -- Where to upload the Output folder results.\n"
                    "  Server address     -- Management API base URL (HTTP or HTTPS).\n"
                    "  NAS root           -- Project root on the NAS.\n"
                    "  Target folder      -- Game asset relative path on NAS.\n"
                    "  Game ID            -- e.g. 10083\n"
                    "  Platform           -- e.g. ap\n"
                    "  Operator name      -- Your name; recorded in the operation log.\n"
                    "  Deployment env     -- Dev (development) and/or QA (test).\n"
                    "\n"
                    "SERVER HELP BUTTON (in Server Settings section)\n"
                    "  Fetches usage notes from the server API. If the server is\n"
                    "  unreachable it falls back to opening the URL in a browser.\n"
                    "\n"
                    "COMMON ISSUES\n"
                    "  SSL certificate error   -- Certificate verification is skipped;\n"
                    "                            this is expected for self-signed certs.\n"
                    "  Connection refused      -- Check server address and that the server\n"
                    "                            is running.\n"
                    "  NAS upload fails        -- Check the NAS path and write permissions.\n"
                    "  Buttons stay disabled   -- A request is in progress; wait for it."));
            mb.exec();
        });
        hdr->addWidget(btn);
        layout->addLayout(hdr);
    }

    // ── Upload to NAS section ────────────────────────────────────────────
    grpNasUpload_ = new QGroupBox(LS("Upload to NAS"));
    retranslates_.push_back([this]{ grpNasUpload_->setTitle(LS("Upload to NAS")); });
    grpNasUpload_->setStyleSheet(QStringLiteral(
        "QGroupBox { border: 2px solid #6f42c1; border-radius: 6px; margin-top: 6px; padding-top: 6px; }"
        "QGroupBox::title { color: #6f42c1; font-weight: bold; }"));
    auto* nasUploadGrid = new QGridLayout(grpNasUpload_);
    nasUploadGrid->setColumnStretch(1, 1);

    {
        auto* lbl = new QLabel(LS("Upload Folder:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Upload Folder:")); });
        nasUploadGrid->addWidget(lbl, 0, 0);
    }
    edtUploadFolder_ = new QLineEdit;
    edtUploadFolder_->setPlaceholderText(LS("Select the local folder to upload to NAS"));
    nasUploadGrid->addWidget(edtUploadFolder_, 0, 1);
    btnBrowseUpload_ = new QPushButton(LS("Browse..."));
    retranslates_.push_back([this]{ btnBrowseUpload_->setText(LS("Browse...")); });
    btnBrowseUpload_->setFixedWidth(80);
    nasUploadGrid->addWidget(btnBrowseUpload_, 0, 2);
    connect(btnBrowseUpload_, &QPushButton::clicked, this, [this]{
        const QString dir = QFileDialog::getExistingDirectory(
            this, LS("Select Upload Folder"),
            edtUploadFolder_->text().isEmpty()
                ? QDir::homePath()
                : edtUploadFolder_->text());
        if (!dir.isEmpty())
            edtUploadFolder_->setText(QDir::toNativeSeparators(dir));
    });

    {
        auto* lbl = new QLabel(LS("NAS Target Folder:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("NAS Target Folder:")); });
        nasUploadGrid->addWidget(lbl, 1, 0);
    }
    edtNasUploadTarget_ = new QLineEdit(
        QStringLiteral(R"(\\%1\project\專案開發區\TempForAIReskin\Work)").arg(GS_DEFAULT_NAS_HOST));
    nasUploadGrid->addWidget(edtNasUploadTarget_, 1, 1);

    btnUploadToNas_ = new QPushButton(LS("Upload Output to NAS"));
    retranslates_.push_back([this]{ btnUploadToNas_->setText(LS("Upload Output to NAS")); });
    btnUploadToNas_->setMinimumWidth(160);
    btnUploadToNas_->setMinimumHeight(32);
    btnUploadToNas_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #6f42c1; color: white; font-weight: bold; font-size: 13px;"
        "  padding: 6px 16px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background: #5a32a3; }"
        "QPushButton:disabled { background: #6c757d; }"));
    nasUploadGrid->addWidget(btnUploadToNas_, 1, 2);
    connect(btnUploadToNas_, &QPushButton::clicked, this, &ResourceReplaceWidget::onUploadToNas);

    layout->addWidget(grpNasUpload_);

    // ── Server section ───────────────────────────────────────────────────
    grpServer_ = new QGroupBox(LS("Server Settings"));
    retranslates_.push_back([this]{ grpServer_->setTitle(LS("Server Settings")); });
    grpServer_->setStyleSheet(QStringLiteral(
        "QGroupBox { border: 2px solid #11998e; border-radius: 6px; margin-top: 6px; padding-top: 6px; }"
        "QGroupBox::title { color: #11998e; font-weight: bold; }"));
    auto* serverGrid = new QGridLayout(grpServer_);
    serverGrid->setColumnStretch(1, 1);
    {
        auto* lbl = new QLabel(LS("Server Address:"));
        retranslates_.push_back([lbl]{ lbl->setText(LS("Server Address:")); });
        serverGrid->addWidget(lbl, 0, 0);
    }
    edtServer_ = new QLineEdit(QStringLiteral("https://localhost:3443"));
    edtServer_->setPlaceholderText(QStringLiteral("e.g. https://192.168.1.100:3443 or localhost:3000"));
    serverGrid->addWidget(edtServer_, 0, 1);

    btnHelp_ = new QPushButton(LS("Info"));
    retranslates_.push_back([this]{ btnHelp_->setText(LS("Info")); });
    btnHelp_->setFixedWidth(60);
    btnHelp_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #17a2b8; color: white; border-radius: 4px; padding: 4px 10px; }"
        "QPushButton:hover { background: #138496; }"
        "QPushButton:disabled { background: #6c757d; }"));
    serverGrid->addWidget(btnHelp_, 0, 2);

    connect(btnHelp_, &QPushButton::clicked, this, [this]() {
        const QString addr = edtServer_->text().trimmed();
        if (addr.isEmpty()) return;

        const QString base = addr.startsWith(QStringLiteral("http"))
                                 ? addr
                                 : QStringLiteral("https://") + addr + QStringLiteral(":3443");
        const QUrl url(base + QStringLiteral("/api/sharedcode"));

        setAllButtonsEnabled(false);
        appendLog(QStringLiteral("GET %1").arg(url.toString()));

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        req.setSslConfiguration(ssl);

        auto* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
            reply->deleteLater();
            setAllButtonsEnabled(true);

            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->error() != QNetworkReply::NoError || status >= 400) {
                appendLog(QStringLiteral("[Info] HTTP %1, opening in browser").arg(status));
                QDesktopServices::openUrl(url);
                return;
            }

            const QString content = QString::fromUtf8(reply->readAll());
            appendLog(QStringLiteral("[Info] Fetched successfully (%1 bytes)").arg(content.size()));

            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(LS("Info"));
            dlg->resize(640, 480);
            dlg->setAttribute(Qt::WA_DeleteOnClose);

            auto* vl = new QVBoxLayout(dlg);
            auto* textEdit = new QPlainTextEdit(content);
            textEdit->setReadOnly(true);
            QFont mono(QStringLiteral("Consolas"), 10);
            mono.setStyleHint(QFont::Monospace);
            textEdit->setFont(mono);
            vl->addWidget(textEdit);

            auto* btnClose = new QPushButton(LS("Close"));
            connect(btnClose, &QPushButton::clicked, dlg, &QDialog::accept);
            vl->addWidget(btnClose, 0, Qt::AlignRight);

            dlg->show();
        });
    });
    layout->addWidget(grpServer_);

    // ── Form fields ──────────────────────────────────────────────────────
    grpForm_ = new QGroupBox(LS("Replace Settings"));
    retranslates_.push_back([this]{ grpForm_->setTitle(LS("Replace Settings")); });
    auto* formGrid  = new QGridLayout(grpForm_);
    formGrid->setColumnStretch(1, 1);
    formGrid->setVerticalSpacing(10);

    // Helper: add a labelled field with a stored label pointer for retranslation
    auto addField = [&](QLineEdit*& edit, QLabel*& lblOut, const char* labelKey,
                        int row, const QString& placeholder, const QString& defaultVal = {}) {
        lblOut = new QLabel(LS(labelKey));
        retranslates_.push_back([lblOut, labelKey]{ lblOut->setText(LS(labelKey)); });
        formGrid->addWidget(lblOut, row, 0);
        edit = new QLineEdit(defaultVal);
        edit->setPlaceholderText(placeholder);
        formGrid->addWidget(edit, row, 1);
    };

    addField(edtNasRoot_,      lblNasRoot_,      "NAS root:",      0,
             QStringLiteral(R"(e.g. \\%1\project\ProjectDev\)").arg(GS_DEFAULT_NAS_HOST),
             QStringLiteral(R"(\\%1\project\專案開發區\)").arg(GS_DEFAULT_NAS_HOST));
    addField(edtTargetFolder_, lblTargetFolder_,  "Target folder:", 1,
             QStringLiteral(R"(e.g. slot10083_dragon\_i18n\slot_10083_dragon)"));
    addField(edtGameId_,       lblGameId_,        "Game ID:",       2,
             QStringLiteral("e.g. 10083"));
    addField(edtPlatform_,     lblPlatform_,      "Platform:",      3,
             QStringLiteral("e.g. ap"), QStringLiteral("ap"));
    addField(edtUserName_,     lblUserName_,      "Operator:",      4,
             QStringLiteral("Enter your name"));

    // Env checkboxes
    lblDeployEnv_ = new QLabel(LS("Deploy Env:"));
    retranslates_.push_back([this]{ lblDeployEnv_->setText(LS("Deploy Env:")); });
    formGrid->addWidget(lblDeployEnv_, 5, 0);
    auto* envWidget = new QWidget;
    auto* envLayout = new QHBoxLayout(envWidget);
    envLayout->setContentsMargins(0, 0, 0, 0);
    envLayout->setSpacing(20);
    chkDev_ = new QCheckBox(LS("Dev (development)"));
    retranslates_.push_back([this]{ chkDev_->setText(LS("Dev (development)")); });
    chkDev_->setChecked(true);
    chkQA_  = new QCheckBox(LS("QA (test)"));
    retranslates_.push_back([this]{ chkQA_->setText(LS("QA (test)")); });
    envLayout->addWidget(chkDev_);
    envLayout->addWidget(chkQA_);
    envLayout->addStretch();
    formGrid->addWidget(envWidget, 5, 1);

    layout->addWidget(grpForm_);

    // ── Action buttons ───────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(15);
    btnRow->addStretch();

    btnReplace_ = new QPushButton(LS("Start Replace"));
    retranslates_.push_back([this]{ btnReplace_->setText(LS("Start Replace")); });
    btnReplace_->setMinimumWidth(130);
    btnReplace_->setMinimumHeight(38);
    btnReplace_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #11998e; color: white; font-weight: bold; font-size: 14px;"
        "  padding: 8px 24px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background: #0d7a6e; }"
        "QPushButton:disabled { background: #6c757d; }"));

    btnCommitPush_ = new QPushButton(QStringLiteral("Commit && Push"));
    btnCommitPush_->setMinimumWidth(150);
    btnCommitPush_->setMinimumHeight(38);
    btnCommitPush_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #dc3545; color: white; font-weight: bold; font-size: 14px;"
        "  padding: 8px 24px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background: #c82333; }"
        "QPushButton:disabled { background: #6c757d; }"));

    btnRow->addWidget(btnReplace_);
    btnRow->addWidget(btnCommitPush_);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    // ── Success frame ────────────────────────────────────────────────────
    successFrame_ = new QFrame;
    successFrame_->setFrameShape(QFrame::StyledPanel);
    successFrame_->setStyleSheet(QStringLiteral(
        "QFrame { background: #d4edda; border: 1px solid #c3e6cb; border-radius: 4px; padding: 4px; }"));
    successFrame_->setVisible(false);
    {
        auto* fl = new QVBoxLayout(successFrame_);
        auto* title = new QLabel(QStringLiteral("\xe2\x9c\x85 Replace succeeded!"));
        title->setStyleSheet(QStringLiteral("font-weight: bold; color: #155724; border: none;"));
        successMsg_ = new QLabel;
        successMsg_->setWordWrap(true);
        successMsg_->setStyleSheet(QStringLiteral("color: #155724; border: none;"));
        fl->addWidget(title);
        fl->addWidget(successMsg_);
    }
    layout->addWidget(successFrame_);

    // ── Error frame ──────────────────────────────────────────────────────
    errorFrame_ = new QFrame;
    errorFrame_->setFrameShape(QFrame::StyledPanel);
    errorFrame_->setStyleSheet(QStringLiteral(
        "QFrame { background: #f8d7da; border: 1px solid #f5c6cb; border-radius: 4px; padding: 4px; }"));
    errorFrame_->setVisible(false);
    {
        auto* fl = new QVBoxLayout(errorFrame_);
        auto* title = new QLabel(QStringLiteral("\xe2\x9d\x8c Replace error!"));
        title->setStyleSheet(QStringLiteral("font-weight: bold; color: #721c24; border: none;"));
        errorMsg_ = new QLabel;
        errorMsg_->setWordWrap(true);
        errorMsg_->setStyleSheet(QStringLiteral("color: #721c24; border: none;"));
        fl->addWidget(title);
        fl->addWidget(errorMsg_);
    }
    layout->addWidget(errorFrame_);

    // ── Output frame ─────────────────────────────────────────────────────
    outputFrame_ = new QFrame;
    outputFrame_->setFrameShape(QFrame::StyledPanel);
    outputFrame_->setStyleSheet(QStringLiteral(
        "QFrame { background: #f8f9fa; border: 1px solid #ddd; border-radius: 4px; padding: 4px; }"));
    outputFrame_->setVisible(false);
    {
        auto* fl = new QVBoxLayout(outputFrame_);
        auto* title = new QLabel(QStringLiteral("\xf0\x9f\x93\x8b Output"));
        title->setStyleSheet(QStringLiteral("font-weight: bold; border: none;"));
        outputContent_ = new QPlainTextEdit;
        outputContent_->setReadOnly(true);
        outputContent_->setMaximumHeight(220);
        QFont mono(QStringLiteral("Consolas"), 9);
        mono.setStyleHint(QFont::Monospace);
        outputContent_->setFont(mono);
        fl->addWidget(title);
        fl->addWidget(outputContent_);
    }
    layout->addWidget(outputFrame_);
    layout->addStretch();

    outerLayout->addWidget(scroll, 1);

    // ── Debug log (outside scroll, always visible) ───────────────────────
    auto* logGroup = new QGroupBox(QStringLiteral("Debug Log"));
    auto* logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(6, 4, 6, 4);
    logLayout->setSpacing(4);

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    logView_->setFixedHeight(140);
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
    auto* btnClearLog = new QPushButton(LS("Clear Log"));
    retranslates_.push_back([btnClearLog]{ btnClearLog->setText(LS("Clear Log")); });
    connect(btnClearLog, &QPushButton::clicked, logView_, &QPlainTextEdit::clear);
    logBtnRow->addWidget(btnClearLog);
    logLayout->addLayout(logBtnRow);

    outerLayout->addWidget(logGroup);

    // ── Signal connections ───────────────────────────────────────────────
    connect(btnReplace_,    &QPushButton::clicked, this, &ResourceReplaceWidget::onStartReplace);
    connect(btnCommitPush_, &QPushButton::clicked, this, &ResourceReplaceWidget::onCommitPush);

    auto save = [this]() { saveFormData(); };
    connect(edtServer_,       &QLineEdit::textChanged, this, save);
    connect(edtNasRoot_,      &QLineEdit::textChanged, this, save);
    connect(edtTargetFolder_, &QLineEdit::textChanged, this, save);
    connect(edtGameId_,       &QLineEdit::textChanged, this, save);
    connect(edtPlatform_,     &QLineEdit::textChanged, this, save);
    connect(edtUserName_,     &QLineEdit::textChanged, this, save);
    connect(chkDev_,             &QCheckBox::toggled,     this, save);
    connect(chkQA_,              &QCheckBox::toggled,     this, save);
    connect(edtUploadFolder_,    &QLineEdit::textChanged, this, save);
    connect(edtNasUploadTarget_, &QLineEdit::textChanged, this, save);
}

void ResourceReplaceWidget::retranslateUi()
{
    for (auto& fn : retranslates_)
        fn();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void ResourceReplaceWidget::setAllButtonsEnabled(bool enabled)
{
    btnHelp_->setEnabled(enabled);
    btnReplace_->setEnabled(enabled);
    btnCommitPush_->setEnabled(enabled);
    btnUploadToNas_->setEnabled(enabled);
}

void ResourceReplaceWidget::appendLog(const QString& msg)
{
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    logView_->appendPlainText(QStringLiteral("[%1] %2").arg(ts, msg));
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void ResourceReplaceWidget::loadFormData()
{
    settings_.beginGroup(SETTINGS_GROUP);
    edtServer_->setText(settings_.value(QStringLiteral("server"),
                         QStringLiteral("https://localhost:3443")).toString());
    const QString defaultNas = QStringLiteral(R"(\\%1\project\專案開發區\)").arg(GS_DEFAULT_NAS_HOST);
    edtNasRoot_->setText(settings_.value(QStringLiteral("nasRoot"), defaultNas).toString());
    edtTargetFolder_->setText(settings_.value(QStringLiteral("targetFolder")).toString());
    edtGameId_->setText(settings_.value(QStringLiteral("gameId")).toString());
    edtPlatform_->setText(settings_.value(QStringLiteral("platform"),
                           QStringLiteral("ap")).toString());
    edtUserName_->setText(settings_.value(QStringLiteral("userName")).toString());
    chkDev_->setChecked(settings_.value(QStringLiteral("envDev"), true).toBool());
    chkQA_->setChecked(settings_.value(QStringLiteral("envQA"), false).toBool());
    edtUploadFolder_->setText(settings_.value(
        QStringLiteral("uploadFolder")).toString());
    edtNasUploadTarget_->setText(settings_.value(
        QStringLiteral("nasUploadTarget"),
        QStringLiteral(R"(\\%1\project\專案開發區\TempForAIReskin\Work)").arg(GS_DEFAULT_NAS_HOST)).toString());
    settings_.endGroup();
}

void ResourceReplaceWidget::saveFormData()
{
    settings_.beginGroup(SETTINGS_GROUP);
    settings_.setValue(QStringLiteral("server"),       edtServer_->text());
    settings_.setValue(QStringLiteral("nasRoot"),      edtNasRoot_->text());
    settings_.setValue(QStringLiteral("targetFolder"), edtTargetFolder_->text());
    settings_.setValue(QStringLiteral("gameId"),       edtGameId_->text());
    settings_.setValue(QStringLiteral("platform"),     edtPlatform_->text());
    settings_.setValue(QStringLiteral("userName"),     edtUserName_->text());
    settings_.setValue(QStringLiteral("envDev"),          chkDev_->isChecked());
    settings_.setValue(QStringLiteral("envQA"),           chkQA_->isChecked());
    settings_.setValue(QStringLiteral("uploadFolder"),    edtUploadFolder_->text());
    settings_.setValue(QStringLiteral("nasUploadTarget"), edtNasUploadTarget_->text());
    settings_.endGroup();
    settings_.sync();
}

void ResourceReplaceWidget::hideMessages()
{
    successFrame_->setVisible(false);
    errorFrame_->setVisible(false);
    outputFrame_->setVisible(false);
}

void ResourceReplaceWidget::showSuccess(const QString& message, const QString& output)
{
    hideMessages();
    successMsg_->setText(message);
    successFrame_->setVisible(true);
    if (!output.isEmpty()) {
        outputContent_->setPlainText(output);
        outputFrame_->setVisible(true);
    }
}

void ResourceReplaceWidget::showError(const QString& message)
{
    hideMessages();
    errorMsg_->setText(message);
    errorFrame_->setVisible(true);
}

bool ResourceReplaceWidget::validateForm()
{
    hideMessages();
    QStringList errors;

    if (edtNasRoot_->text().trimmed().isEmpty())
        errors << LS("Please enter NAS root");
    if (edtTargetFolder_->text().trimmed().isEmpty())
        errors << LS("Please enter target folder");
    if (edtGameId_->text().trimmed().isEmpty())
        errors << LS("Please enter Game ID");
    if (edtUserName_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, LS("Validation Error"),
                             LS("Please enter operator name"));
        edtUserName_->setFocus();
        return false;
    }
    if (!chkDev_->isChecked() && !chkQA_->isChecked())
        errors << LS("Please select at least one deployment environment");

    if (!errors.isEmpty()) {
        showError(errors.join('\n'));
        return false;
    }
    return true;
}

QStringList ResourceReplaceWidget::buildUrls(const QString& serverAddress,
                                              const QString& endpoint) const
{
    QStringList urls;
    if (serverAddress.startsWith(QStringLiteral("http"))) {
        urls << serverAddress + endpoint;
    } else {
        const QStringList parts = serverAddress.split(':');
        if (parts.size() == 2) {
            const QString host = parts[0];
            urls << QStringLiteral("https://") + host + QStringLiteral(":3443") + endpoint;
            urls << QStringLiteral("https://") + serverAddress + endpoint;
            urls << QStringLiteral("http://")  + serverAddress + endpoint;
        } else {
            urls << QStringLiteral("https://") + serverAddress + QStringLiteral(":3443") + endpoint;
            urls << QStringLiteral("https://") + serverAddress + QStringLiteral(":3000") + endpoint;
            urls << QStringLiteral("http://")  + serverAddress + QStringLiteral(":3000") + endpoint;
        }
    }
    return urls;
}

void ResourceReplaceWidget::tryUrls(
    const QStringList& urls, int idx, const QByteArray& body,
    std::function<void(bool, const QJsonObject&, const QString&)> cb)
{
    if (idx >= urls.size()) {
        cb(false, {}, QStringLiteral("All connection attempts failed"));
        return;
    }

    const QUrl url(urls[idx]);
    appendLog(QStringLiteral("Try [%1/%2] POST %3").arg(idx + 1).arg(urls.size()).arg(url.toString()));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(ssl);

    auto* reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, urls, idx, body, cb]() mutable {
        reply->deleteLater();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status < 400) {
            appendLog(QStringLiteral("Response HTTP %1 OK").arg(status));
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            cb(true, doc.object(), {});
        } else {
            appendLog(QStringLiteral("Failed HTTP %1 - %2").arg(status).arg(reply->errorString()));
            tryUrls(urls, idx + 1, body, std::move(cb));
        }
    });
}

// ── Start Replace ─────────────────────────────────────────────────────────────

void ResourceReplaceWidget::onStartReplace()
{
    saveFormData();
    if (!validateForm()) return;

    const QString nasRoot      = edtNasRoot_->text().trimmed();
    const QString targetFolder = edtTargetFolder_->text().trimmed();
    const QString gameId       = edtGameId_->text().trimmed();
    const QString platform     = edtPlatform_->text().trimmed().isEmpty()
                                  ? QStringLiteral("ap") : edtPlatform_->text().trimmed();
    const QString userName     = edtUserName_->text().trimmed();
    const QString serverAddr   = edtServer_->text().trimmed();

    if (serverAddr.isEmpty()) { showError(LS("Please enter server address")); return; }

    QStringList deployEnv;
    if (chkDev_->isChecked()) deployEnv << QStringLiteral("dev");
    if (chkQA_->isChecked())  deployEnv << QStringLiteral("QA");

    setAllButtonsEnabled(false);
    btnReplace_->setText(LS("Replacing..."));
    appendLog(QStringLiteral("=== Start Replace gameId=%1 env=%2 ===")
                  .arg(gameId, deployEnv.join(',')));

    QJsonObject data;
    data[QStringLiteral("nasRoot")]      = nasRoot;
    data[QStringLiteral("targetFolder")] = targetFolder;
    data[QStringLiteral("gameId")]       = gameId;
    data[QStringLiteral("platformName")] = platform;
    data[QStringLiteral("deployEnv")]    = QJsonArray::fromStringList(deployEnv);
    data[QStringLiteral("userName")]     = userName;

    const QByteArray body = QJsonDocument(data).toJson(QJsonDocument::Compact);
    const QStringList urls = buildUrls(serverAddr, QStringLiteral("/api/onlineGameResourceReplace"));

    tryUrls(urls, 0, body,
            [this, gameId, targetFolder, deployEnv]
            (bool ok, const QJsonObject& result, const QString& errMsg) {
        setAllButtonsEnabled(true);
        btnReplace_->setText(LS("Start Replace"));

        if (!ok) {
            appendLog(QStringLiteral("[Replace] All attempts failed: %1").arg(errMsg));
            showError(LS("Cannot connect to server: %1\n\nPlease check the server is running.")
                          .arg(errMsg));
            return;
        }

        const QString serverMsg = result[QStringLiteral("message")].toString(QStringLiteral("OK"));
        appendLog(QStringLiteral("[Replace] Success: %1").arg(serverMsg));

        const QString msg = QStringLiteral(
            "Game ID: %1\nTarget folder: %2\nDeploy env: %3\n\nServer response: %4")
                .arg(gameId, targetFolder,
                     deployEnv.join(QStringLiteral(", ")), serverMsg);

        QString output;
        if (result.contains(QStringLiteral("output")))
            output = result[QStringLiteral("output")].toString();
        else if (result.contains(QStringLiteral("details")))
            output = result[QStringLiteral("details")].toString();
        else
            output = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));

        showSuccess(msg, output);
    });
}

// ── Upload Output to NAS ──────────────────────────────────────────────────────

void ResourceReplaceWidget::onUploadToNas()
{
    if (uploadProcess_ && uploadProcess_->state() != QProcess::NotRunning) {
        QMessageBox::warning(this, LS("Warning"), LS("Upload in progress, please wait..."));
        return;
    }

    // 1. Get upload folder from the UI field
    const QString outputFolder = edtUploadFolder_->text().trimmed();

    if (outputFolder.isEmpty()) {
        QMessageBox::warning(this, LS("Error"),
            LS("Upload Folder is not set. Please select the folder to upload."));
        return;
    }
    if (!QDir(outputFolder).exists()) {
        QMessageBox::warning(this, LS("Error"),
            LS("Upload Folder does not exist:\n%1").arg(outputFolder));
        return;
    }

    // 2. Get NAS target folder
    const QString nasTarget = edtNasUploadTarget_->text().trimmed();
    if (nasTarget.isEmpty()) {
        QMessageBox::warning(this, LS("Error"), LS("Please enter NAS target folder."));
        return;
    }

    // 3. Check if the NAS Work folder already has content
    QDir workDir(nasTarget);
    if (workDir.exists() && !workDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(LS("Target Folder Not Empty"));
        msgBox.setText(LS("NAS target folder already has content:\n%1\n\nChoose an action:").arg(nasTarget));
        msgBox.setIcon(QMessageBox::Warning);
        auto* btnDelete = msgBox.addButton(LS("Delete old data"), QMessageBox::DestructiveRole);
        auto* btnMove   = msgBox.addButton(LS("Move to timestamped folder"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        msgBox.exec();

        if (msgBox.clickedButton() == btnDelete) {
            appendLog(QStringLiteral("[NAS] Deleting old data: %1").arg(nasTarget));
            if (!workDir.removeRecursively()) {
                QMessageBox::critical(this, LS("Error"),
                    LS("Cannot delete folder:\n%1").arg(nasTarget));
                return;
            }
            if (!QDir().mkpath(nasTarget)) {
                QMessageBox::critical(this, LS("Error"),
                    LS("Cannot recreate folder:\n%1").arg(nasTarget));
                return;
            }
            appendLog(QStringLiteral("[NAS] Deleted and recreated folder"));

        } else if (msgBox.clickedButton() == btnMove) {
            // Move Work → TempForAIReskin\yyyyMMdd_HHmmss
            const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
            // Build parent path by stripping the last path component
            QString parentPath = QDir::toNativeSeparators(nasTarget);
            while (parentPath.endsWith('\\') || parentPath.endsWith('/'))
                parentPath.chop(1);
            const int lastSep = qMax(parentPath.lastIndexOf('\\'), parentPath.lastIndexOf('/'));
            if (lastSep > 2)
                parentPath = parentPath.left(lastSep);
            const QString timestampPath = parentPath + QStringLiteral("\\") + timestamp;

            appendLog(QStringLiteral("[NAS] Moving to: %1").arg(timestampPath));
            if (!QDir().rename(nasTarget, timestampPath)) {
                QMessageBox::critical(this, LS("Error"),
                    LS("Cannot move folder.\nSource: %1\nTarget: %2").arg(nasTarget, timestampPath));
                return;
            }
            if (!QDir().mkpath(nasTarget)) {
                QMessageBox::critical(this, LS("Error"),
                    LS("Cannot recreate Work folder:\n%1").arg(nasTarget));
                return;
            }
            appendLog(QStringLiteral("[NAS] Moved, new Work folder created"));

        } else {
            appendLog(QStringLiteral("[NAS] Upload cancelled"));
            return;
        }
    } else if (!workDir.exists()) {
        if (!QDir().mkpath(nasTarget)) {
            QMessageBox::critical(this, LS("Error"),
                LS("Cannot create NAS target folder:\n%1").arg(nasTarget));
            return;
        }
        appendLog(QStringLiteral("[NAS] Created target folder: %1").arg(nasTarget));
    }

    // 4. Run robocopy to copy output folder → NAS target
    setAllButtonsEnabled(false);
    btnUploadToNas_->setText(LS("Uploading..."));
    appendLog(QStringLiteral("=== Start upload %1 -> %2 ===").arg(outputFolder, nasTarget));

    if (!uploadProcess_) {
        uploadProcess_ = new QProcess(this);
        connect(uploadProcess_, &QProcess::readyReadStandardOutput,
                this, &ResourceReplaceWidget::onUploadProcessOutput);
        connect(uploadProcess_, &QProcess::readyReadStandardError,
                this, &ResourceReplaceWidget::onUploadProcessOutput);
        connect(uploadProcess_,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ResourceReplaceWidget::onUploadProcessFinished);
    }

    // /E  = copy subdirs including empty
    // /IS = include same (overwrite even if timestamps match)
    // /IT = include tweaked files
    uploadProcess_->start(QStringLiteral("robocopy"),
        { outputFolder, nasTarget,
          QStringLiteral("/E"), QStringLiteral("/IS"), QStringLiteral("/IT") });
}

void ResourceReplaceWidget::onUploadProcessOutput()
{
    const QString out = QString::fromLocal8Bit(uploadProcess_->readAllStandardOutput());
    if (!out.isEmpty()) {
        for (const QString& line : out.split('\n'))
            if (!line.trimmed().isEmpty())
                appendLog(line.trimmed());
    }
    const QString err = QString::fromLocal8Bit(uploadProcess_->readAllStandardError());
    if (!err.isEmpty()) {
        for (const QString& line : err.split('\n'))
            if (!line.trimmed().isEmpty())
                appendLog(QStringLiteral("[ERR] ") + line.trimmed());
    }
}

void ResourceReplaceWidget::onUploadProcessFinished(int exitCode, QProcess::ExitStatus)
{
    setAllButtonsEnabled(true);
    btnUploadToNas_->setText(LS("Upload Output to NAS"));

    // robocopy: exit < 8 = success (0=nothing to copy, 1=copied, 2-7=partial/extra)
    if (exitCode < 8) {
        appendLog(QStringLiteral("[NAS] Upload complete (robocopy exit=%1)").arg(exitCode));
        showSuccess(LS("Upload complete! Files copied to NAS."));
    } else {
        appendLog(QStringLiteral("[NAS] Upload failed (robocopy exit=%1)").arg(exitCode));
        showError(LS("Upload failed, robocopy exit code: %1\nCheck NAS path and permissions.").arg(exitCode));
    }
}

// ── Commit & Push ─────────────────────────────────────────────────────────────

void ResourceReplaceWidget::onCommitPush()
{
    const auto confirmed = QMessageBox::question(
        this,
        LS("Confirm Commit"),
        LS("Have you tested and confirmed you want to Commit & Push?\n\nThis will commit file updates to the Git repository."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirmed != QMessageBox::Yes) return;

    saveFormData();

    const QString nasRoot      = edtNasRoot_->text().trimmed();
    const QString targetFolder = edtTargetFolder_->text().trimmed();
    const QString gameId       = edtGameId_->text().trimmed();
    const QString userName     = edtUserName_->text().trimmed();
    const QString serverAddr   = edtServer_->text().trimmed();

    if (userName.isEmpty()) {
        QMessageBox::warning(this, LS("Validation Error"),
                             LS("Please enter operator name"));
        edtUserName_->setFocus();
        return;
    }

    QStringList errors;
    if (nasRoot.isEmpty())      errors << LS("Please enter NAS root");
    if (targetFolder.isEmpty()) errors << LS("Please enter target folder");
    if (gameId.isEmpty())       errors << LS("Please enter Game ID");
    if (!errors.isEmpty()) { showError(errors.join('\n')); return; }

    if (serverAddr.isEmpty()) { showError(LS("Please enter server address")); return; }

    setAllButtonsEnabled(false);
    btnCommitPush_->setText(QStringLiteral("Committing..."));
    appendLog(QStringLiteral("=== Commit & Push gameId=%1 user=%2 ===").arg(gameId, userName));

    QJsonObject data;
    data[QStringLiteral("nasRoot")]       = nasRoot;
    data[QStringLiteral("targetFolder")]  = targetFolder;
    data[QStringLiteral("gameId")]        = gameId;
    data[QStringLiteral("userName")]      = userName;
    data[QStringLiteral("commitMessage")] =
        QStringLiteral("[BuildMachine] Update game resources by %1").arg(userName);

    const QByteArray body = QJsonDocument(data).toJson(QJsonDocument::Compact);
    const QStringList urls = buildUrls(serverAddr, QStringLiteral("/api/resourceCommitPush"));

    tryUrls(urls, 0, body,
            [this, gameId](bool ok, const QJsonObject& result, const QString& errMsg) {
        setAllButtonsEnabled(true);
        btnCommitPush_->setText(QStringLiteral("Commit && Push"));

        if (!ok) {
            appendLog(QStringLiteral("[Commit] All attempts failed: %1").arg(errMsg));
            showError(LS("Cannot connect to server: %1\n\nPlease check the server is running.")
                          .arg(errMsg));
            return;
        }

        if (result[QStringLiteral("success")].toBool()) {
            const QString hash = result[QStringLiteral("commitHash")].toString(QStringLiteral("N/A"));
            appendLog(QStringLiteral("[Commit] Success hash=%1").arg(hash));
            const QString msg = QStringLiteral(
                "Game ID: %1\nCommit Hash: %2\nMatched files: %3\nReplaced files: %4\n\n%5")
                    .arg(gameId, hash,
                         QString::number(result[QStringLiteral("matchedFiles")].toInt()),
                         QString::number(result[QStringLiteral("replacedFiles")].toInt()),
                         result[QStringLiteral("message")].toString());
            showSuccess(msg, result[QStringLiteral("output")].toString());
        } else {
            const QString reason = result[QStringLiteral("message")].toString();
            appendLog(QStringLiteral("[Commit] Failed: %1").arg(reason));
            showError(LS("Commit & Push failed: %1").arg(reason));
        }
    });
}
