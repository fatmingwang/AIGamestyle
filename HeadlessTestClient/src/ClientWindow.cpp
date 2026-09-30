#include "ClientWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonValue>
#include <QDateTime>

namespace {

struct CommandInfo {
    QString help;          // shown in the Help dialog
    QString argsTemplate;  // auto-filled into the args field when this command is selected
};

// Mirrors the command table in Doc/Commands_Reference.md §12 / App/tools/headless_client.py —
// keep these three in sync if the server's command set changes. `argsTemplate` pre-fills the
// args field with the expected "key=" shape so a required key is never just forgotten (the
// server only reports a missing arg by name, e.g. set-working-dir's "requires ... path").
const QMap<QString, CommandInfo> kCommandInfo = {
    { QStringLiteral("set-working-dir"),
      { QStringLiteral("chdir the server + set Project Root for this session."),
        QStringLiteral("path=") } },
    { QStringLiteral("set-config"),
      { QStringLiteral("Merge fields into this session's config. Any of: project-root, "
                       "output-folder, asset-root, hero-reference, positive-prompt, "
                       "negative-prompt, style-profile-id, batch-id, comfyui-server, "
                       "python-exe, category, ip-weight, denoise, steps, include-spine."),
        QStringLiteral("comfyui-server=") } },
    { QStringLiteral("get-config"),
      { QStringLiteral("No args. Returns this session's current config as JSON."),
        QString() } },
    { QStringLiteral("check"),
      { QStringLiteral("No args. Readiness check (git/python/ComfyUI/venv/models/port)."),
        QString() } },
    { QStringLiteral("comfyui-status"),
      { QStringLiteral("No args. job_end.success is true/false for reachable/unreachable."),
        QString() } },
    { QStringLiteral("comfyui-launch"),
      { QStringLiteral("Start ComfyUI if not already reachable, then poll for it."),
        QStringLiteral("wait-timeout=60") } },
    { QStringLiteral("scan"),
      { QStringLiteral("Scan the asset root into the pipeline inventory."),
        QStringLiteral("asset-root=") } },
    { QStringLiteral("classify"),
      { QStringLiteral("No args. Classifies assets from the last scan."),
        QString() } },
    { QStringLiteral("link"),
      { QStringLiteral("No args. Links/groups assets from the last classify."),
        QString() } },
    { QStringLiteral("plan-style"),
      { QStringLiteral("Build the style batch plan."),
        QStringLiteral("style-profile-id=default_parameters batch-id=") } },
    { QStringLiteral("comfyui-batch"),
      { QStringLiteral("Run the style-plan batch through ComfyUI. overwrite/clean gate a "
                       "non-empty output folder; regenerate/continue-stale gate a stale plan."),
        QStringLiteral("overwrite=true") } },
    { QStringLiteral("simple-batch"),
      { QStringLiteral("Reskin the asset root directly, bypassing scan/classify/link/plan-style."),
        QStringLiteral("asset-root=") } },
    { QStringLiteral("all"),
      { QStringLiteral("scan -> classify -> link -> plan-style -> comfyui-batch, stops on the "
                       "first failure. Takes the same gate flags as scan + comfyui-batch."),
        QStringLiteral("overwrite=true") } },
    { QStringLiteral("text2image"),
      { QStringLiteral("Generate a new image straight from a prompt, no source asset. "
                       "Optional: output-name, output-dir, width, height."),
        QStringLiteral("prompt=") } },
    { QStringLiteral("imagewithprompt"),
      { QStringLiteral("Prompt-guided variant of one reference image. positive-prompt or "
                       "category; optional: negative-prompt, output-dir, output-name, seed, "
                       "steps, cfg, denoise, ip-weight."),
        QStringLiteral("reference=") } },
    { QStringLiteral("pose-from-prompt"),
      { QStringLiteral("Generate pose/skeleton JSON from a text description. Optional: "
                       "subject-type (human|biped_animal), loop, frames, backend, api-url, output."),
        QStringLiteral("name= description=") } },
    { QStringLiteral("atlas-extract"),
      { QStringLiteral("Unpack a Spine atlas into individual sprite PNGs. Optional: scale (default 1.0)."),
        QStringLiteral("atlas-path=") } },
    { QStringLiteral("atlas-merge"),
      { QStringLiteral("Pack sprite PNGs (from atlas-extract's output) back into the atlas. "
                       "Optional: scale, merge-mode=separate|single, conflict=fail|replace|rename."),
        QStringLiteral("atlas-path=") } },
    { QStringLiteral("nas-upload"),
      { QStringLiteral("robocopy a local output folder to a NAS target. "
                       "Optional: conflict=fail|overwrite|rename (default fail)."),
        QStringLiteral("source= target=") } },
    { QStringLiteral("exit"),
      { QStringLiteral("No args. Closes the connection; the server sends no reply."),
        QString() } },
};

// QMap<QString,CommandInfo> above is key-sorted, which would scramble the combo box (session
// commands first, then pipeline commands in pipeline order) — so the display/selection order is
// spelled out explicitly here instead, and kCommandInfo is used purely as a by-name lookup.
const QStringList kCommands = {
    QStringLiteral("set-working-dir"),
    QStringLiteral("set-config"),
    QStringLiteral("get-config"),
    QStringLiteral("check"),
    QStringLiteral("comfyui-status"),
    QStringLiteral("comfyui-launch"),
    QStringLiteral("scan"),
    QStringLiteral("classify"),
    QStringLiteral("link"),
    QStringLiteral("plan-style"),
    QStringLiteral("comfyui-batch"),
    QStringLiteral("simple-batch"),
    QStringLiteral("all"),
    QStringLiteral("text2image"),
    QStringLiteral("imagewithprompt"),
    QStringLiteral("pose-from-prompt"),
    QStringLiteral("atlas-extract"),
    QStringLiteral("atlas-merge"),
    QStringLiteral("nas-upload"),
    QStringLiteral("exit"),
};

// "key=value key2=value2 ..." -> {"key":"value", "key2":<inferred type>}. A value wrapped in
// double quotes may contain spaces (path="a fantasy sword"); true/false/numbers are sent as
// their JSON type so flags like overwrite/include-spine and numeric fields like steps work.
// A bare word with no "=" (e.g. a path typed without its "path=" prefix) is silently not a
// valid arg — `dropped`, if given, collects those tokens so the caller can warn about them
// instead of the server just failing with a generic "requires ..." error and no clue why.
QJsonObject parseArgsLine(const QString& line, QStringList* dropped = nullptr)
{
    QStringList tokens;
    QString token;
    bool inQuotes = false;

    for (const QChar c : line) {
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if (c.isSpace() && !inQuotes) {
            if (!token.isEmpty()) { tokens << token; token.clear(); }
            continue;
        }
        token += c;
    }
    if (!token.isEmpty()) tokens << token;

    QJsonObject args;
    for (const QString& tok : tokens) {
        const int eq = tok.indexOf(QLatin1Char('='));
        if (eq < 0) {
            if (dropped) *dropped << tok;
            continue;
        }
        const QString key = tok.left(eq);
        const QString val = tok.mid(eq + 1);

        if (val == QStringLiteral("true"))       { args[key] = true;  continue; }
        if (val == QStringLiteral("false"))      { args[key] = false; continue; }
        bool ok = false;
        const int i = val.toInt(&ok);
        if (ok) { args[key] = i; continue; }
        const double d = val.toDouble(&ok);
        if (ok) { args[key] = d; continue; }
        args[key] = val;
    }
    return args;
}

} // namespace

ClientWindow::ClientWindow(QWidget* parent)
    : QMainWindow(parent)
{
    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected,          this, &ClientWindow::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected,       this, &ClientWindow::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead,          this, &ClientWindow::onSocketReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &ClientWindow::onSocketError);

    buildUi();
    setWindowTitle(QStringLiteral("GameStyleAI Headless Test Client"));
    resize(820, 580);
}

void ClientWindow::buildUi()
{
    auto* central = new QWidget;
    auto* mainLayout = new QVBoxLayout(central);

    // ── Connection group ─────────────────────────────────────────────────────
    auto* connGroup = new QGroupBox(QStringLiteral("Connection"));
    auto* connLayout = new QHBoxLayout(connGroup);
    connLayout->addWidget(new QLabel(QStringLiteral("Host:")));
    edtHost_ = new QLineEdit(QStringLiteral("127.0.0.1"));
    edtHost_->setFixedWidth(120);
    connLayout->addWidget(edtHost_);
    connLayout->addWidget(new QLabel(QStringLiteral("Port:")));
    edtPort_ = new QLineEdit(QStringLiteral("8765"));
    edtPort_->setFixedWidth(70);
    connLayout->addWidget(edtPort_);
    btnConnect_ = new QPushButton(QStringLiteral("Connect"));
    connect(btnConnect_, &QPushButton::clicked, this, &ClientWindow::onConnectClicked);
    connLayout->addWidget(btnConnect_);
    lblStatus_ = new QLabel(QStringLiteral("Disconnected"));
    lblStatus_->setStyleSheet(QStringLiteral("color:#e57373;"));
    connLayout->addWidget(lblStatus_);
    connLayout->addStretch();
    mainLayout->addWidget(connGroup);

    // ── Command group ────────────────────────────────────────────────────────
    auto* cmdGroup = new QGroupBox(QStringLiteral("Command"));
    auto* cmdLayout = new QHBoxLayout(cmdGroup);
    cboCommand_ = new QComboBox;
    cboCommand_->addItems(kCommands);
    connect(cboCommand_, &QComboBox::currentTextChanged, this, &ClientWindow::onCommandChanged);
    cmdLayout->addWidget(cboCommand_);
    edtArgs_ = new QLineEdit;
    edtArgs_->setPlaceholderText(
        QStringLiteral("args as key=value key2=value2 ...  e.g. path=D:/Work/GamestyleAI"));
    connect(edtArgs_, &QLineEdit::returnPressed, this, &ClientWindow::onSendClicked);
    cmdLayout->addWidget(edtArgs_, 1);
    btnBrowse_ = new QPushButton(QStringLiteral("Browse..."));
    btnBrowse_->setToolTip(QStringLiteral("Pick a directory and fill it in as path=<dir> "
                                          "(for set-working-dir)"));
    connect(btnBrowse_, &QPushButton::clicked, this, &ClientWindow::onBrowseClicked);
    cmdLayout->addWidget(btnBrowse_);
    btnHelp_ = new QPushButton(QStringLiteral("Help"));
    btnHelp_->setToolTip(QStringLiteral("Show every command's args and what it does"));
    connect(btnHelp_, &QPushButton::clicked, this, &ClientWindow::onHelpClicked);
    cmdLayout->addWidget(btnHelp_);
    btnSend_ = new QPushButton(QStringLiteral("Send"));
    btnSend_->setEnabled(false);
    connect(btnSend_, &QPushButton::clicked, this, &ClientWindow::onSendClicked);
    cmdLayout->addWidget(btnSend_);
    mainLayout->addWidget(cmdGroup);

    // ── Log group ────────────────────────────────────────────────────────────
    auto* logGroup = new QGroupBox(QStringLiteral("Log"));
    auto* logLayout = new QVBoxLayout(logGroup);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(5000);
    logView_->setStyleSheet(QStringLiteral(
        "background-color:#1e1e1e; color:#dcdcdc; font-family:Consolas,monospace;"));
    logLayout->addWidget(logView_);
    btnClearLog_ = new QPushButton(QStringLiteral("Clear Log"));
    connect(btnClearLog_, &QPushButton::clicked, logView_, &QPlainTextEdit::clear);
    auto* clearRow = new QHBoxLayout;
    clearRow->addStretch();
    clearRow->addWidget(btnClearLog_);
    logLayout->addLayout(clearRow);
    mainLayout->addWidget(logGroup, 1);

    setCentralWidget(central);

    // addItems() above selected the first command before onCommandChanged was connected, so
    // the args field wasn't auto-filled for it — do that one now.
    onCommandChanged(cboCommand_->currentText());
}

void ClientWindow::setConnectedState(bool connected)
{
    btnSend_->setEnabled(connected);
    btnConnect_->setText(connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    lblStatus_->setText(connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"));
    lblStatus_->setStyleSheet(connected ? QStringLiteral("color:#81c784;")
                                         : QStringLiteral("color:#e57373;"));
    edtHost_->setEnabled(!connected);
    edtPort_->setEnabled(!connected);
}

void ClientWindow::onConnectClicked()
{
    if (socket_->state() == QAbstractSocket::ConnectedState ||
        socket_->state() == QAbstractSocket::ConnectingState) {
        socket_->disconnectFromHost();
        return;
    }

    const QString host = edtHost_->text().trimmed();
    const quint16 port = quint16(edtPort_->text().toUInt());
    appendLog(QStringLiteral("Connecting to %1:%2 ...").arg(host).arg(port), QColor(255, 193, 7));
    socket_->connectToHost(host, port);
}

void ClientWindow::onSocketConnected()
{
    appendLog(QStringLiteral("Connected."), QColor(129, 199, 132));
    setConnectedState(true);
}

void ClientWindow::onSocketDisconnected()
{
    appendLog(QStringLiteral("Disconnected."), QColor(229, 115, 115));
    setConnectedState(false);
}

void ClientWindow::onSocketError(QAbstractSocket::SocketError)
{
    appendLog(QStringLiteral("[ERROR] %1").arg(socket_->errorString()), QColor(244, 67, 54));
    setConnectedState(false);
}

void ClientWindow::onSocketReadyRead()
{
    while (socket_->canReadLine()) {
        const QByteArray line = socket_->readLine().trimmed();
        if (!line.isEmpty())
            handleLine(line);
    }
}

void ClientWindow::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Directory"));
    if (dir.isEmpty()) return;
    // Switch first (which auto-fills args to the "path=" template via onCommandChanged), THEN
    // overwrite args with the actual picked directory — otherwise the auto-fill would clobber it.
    cboCommand_->setCurrentText(QStringLiteral("set-working-dir"));
    edtArgs_->setText(QStringLiteral("path=\"%1\"").arg(dir));
}

void ClientWindow::onCommandChanged(const QString& command)
{
    edtArgs_->setText(kCommandInfo.value(command).argsTemplate);
    edtArgs_->setFocus();
    edtArgs_->end(false); // put the cursor after the "key=" prefix, ready to type the value
}

void ClientWindow::onHelpClicked()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("Command Reference"));
    dlg->resize(640, 480);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dlg);
    auto* text = new QPlainTextEdit(dlg);
    text->setReadOnly(true);
    text->setStyleSheet(QStringLiteral(
        "background-color:#1e1e1e; color:#dcdcdc; font-family:Consolas,monospace;"));

    QString body;
    for (const QString& cmd : kCommands) {
        const CommandInfo& info = kCommandInfo.value(cmd);
        body += QStringLiteral("%1\n  %2\n").arg(cmd, -18).arg(info.help);
        if (!info.argsTemplate.isEmpty())
            body += QStringLiteral("  args template: %1\n").arg(info.argsTemplate);
        body += QStringLiteral("\n");
    }
    body += QStringLiteral(
        "Args format: key=value key2=value2 ...\n"
        "Quote a value containing spaces: positive-prompt=\"a fantasy sword\"\n"
        "true/false and numbers are sent as their JSON type automatically.\n"
        "Selecting a command above auto-fills its args template — the Browse... button does the\n"
        "same for set-working-dir but with an actual picked folder.\n"
        "\n"
        "See Doc/Commands_Reference.md \xc2\xa7" "12 for the full protocol.");
    text->setPlainText(body);
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::close);
    layout->addWidget(buttons);

    dlg->show();
}

void ClientWindow::onSendClicked()
{
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        appendLog(QStringLiteral("[ERROR] Not connected."), QColor(244, 67, 54));
        return;
    }
    const QString command = cboCommand_->currentText().trimmed();
    if (command.isEmpty()) return;

    QStringList dropped;
    const QJsonObject args = parseArgsLine(edtArgs_->text(), &dropped);
    if (!dropped.isEmpty()) {
        appendLog(QStringLiteral("[WARNING] Ignored arg(s) with no '=': %1 — did you forget a "
                                 "key= prefix (e.g. path=%2)?")
                      .arg(dropped.join(QStringLiteral(", ")), dropped.first()),
                  QColor(255, 152, 0));
    }
    sendCommand(command, args);
}

void ClientWindow::sendCommand(const QString& command, const QJsonObject& args)
{
    QJsonObject payload{ { QStringLiteral("command"), command } };
    if (!args.isEmpty())
        payload[QStringLiteral("args")] = args;

    const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    socket_->write(line);

    appendLog(QStringLiteral(">> %1").arg(QString::fromUtf8(line.trimmed())), QColor(100, 181, 246));

    if (command == QStringLiteral("exit") || command == QStringLiteral("quit"))
        socket_->disconnectFromHost();
}

void ClientWindow::handleLine(const QByteArray& line)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QStringLiteral("<< [unparseable] %1").arg(QString::fromUtf8(line)), QColor(255, 152, 0));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("job_start")) {
        appendLog(QStringLiteral("<< job_start  %1").arg(obj.value(QStringLiteral("command")).toString()),
                  QColor(100, 181, 246));
    } else if (type == QStringLiteral("job_end")) {
        const bool ok = obj.value(QStringLiteral("success")).toBool();
        appendLog(QStringLiteral("<< job_end    [%1] code=%2  %3")
                      .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAIL"))
                      .arg(obj.value(QStringLiteral("code")).toInt())
                      .arg(obj.value(QStringLiteral("message")).toString()),
                  ok ? QColor(129, 199, 132) : QColor(244, 67, 54));
    } else if (type == QStringLiteral("step_start")) {
        appendLog(QStringLiteral("<< step_start %1 (%2/%3)")
                      .arg(obj.value(QStringLiteral("step")).toString())
                      .arg(obj.value(QStringLiteral("index")).toInt())
                      .arg(obj.value(QStringLiteral("total")).toInt()),
                  QColor(179, 157, 219));
    } else if (type == QStringLiteral("step_end")) {
        const bool ok = obj.value(QStringLiteral("success")).toBool();
        appendLog(QStringLiteral("<< step_end   [%1] %2 exit=%3")
                      .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAIL"))
                      .arg(obj.value(QStringLiteral("step")).toString())
                      .arg(obj.value(QStringLiteral("exitCode")).toInt()),
                  ok ? QColor(129, 199, 132) : QColor(244, 67, 54));
    } else if (type == QStringLiteral("marker")) {
        appendLog(QStringLiteral("<< marker     %1 step=%2 %3")
                      .arg(obj.value(QStringLiteral("marker")).toString(),
                           obj.value(QStringLiteral("scriptStep")).toString(),
                           obj.value(QStringLiteral("message")).toString()),
                  QColor(224, 224, 224));
    } else if (type == QStringLiteral("log")) {
        appendLog(QStringLiteral("<< log[%1] %2")
                      .arg(obj.value(QStringLiteral("stream")).toString(),
                           obj.value(QStringLiteral("line")).toString()),
                  QColor(158, 158, 158));
    } else if (type == QStringLiteral("result")) {
        QJsonObject rest = obj;
        rest.remove(QStringLiteral("type"));
        appendLog(QStringLiteral("<< result     %1")
                      .arg(QString::fromUtf8(QJsonDocument(rest).toJson(QJsonDocument::Compact))),
                  QColor(77, 208, 225));
    } else if (type == QStringLiteral("error")) {
        appendLog(QStringLiteral("<< error      %1").arg(obj.value(QStringLiteral("message")).toString()),
                  QColor(244, 67, 54));
    } else {
        appendLog(QStringLiteral("<< %1").arg(QString::fromUtf8(line)), QColor(224, 224, 224));
    }
}

void ClientWindow::appendLog(const QString& text, const QColor& color)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logView_->appendHtml(QStringLiteral("<span style='color:#888'>[%1]</span> "
                                        "<span style='color:%2'>%3</span>")
                              .arg(stamp, color.name(), text.toHtmlEscaped()));
}
