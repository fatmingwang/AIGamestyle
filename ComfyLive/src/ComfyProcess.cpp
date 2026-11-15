#include "ComfyProcess.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <vector>
#endif

namespace {
constexpr int kPollMs = 1000;
constexpr int kGiveUpMs = 300000;   // a first start with every add-on can take minutes

#ifdef Q_OS_WIN
// The process listening on TCP port (IPv4, then IPv6), or 0.
DWORD listenerPid(quint16 port)
{
    for (ULONG family : {ULONG(AF_INET), ULONG(AF_INET6)}) {
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        std::vector<char> buffer(size + 1024);
        size = ULONG(buffer.size());
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
            continue;
        if (family == AF_INET) {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
                if (ntohs(u_short(table->table[i].dwLocalPort)) == port)
                    return table->table[i].dwOwningPid;
        } else {
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
                if (ntohs(u_short(table->table[i].dwLocalPort)) == port)
                    return table->table[i].dwOwningPid;
        }
    }
    return 0;
}

QString imageName(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t path[MAX_PATH * 2];
    DWORD n = DWORD(MAX_PATH * 2);
    QString out;
    if (QueryFullProcessImageNameW(h, 0, path, &n))
        out = QFileInfo(QString::fromWCharArray(path, int(n))).fileName();
    CloseHandle(h);
    return out;
}

DWORD parentPid(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    DWORD parent = 0;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (e.th32ProcessID == pid) {
            parent = e.th32ParentProcessID;
            break;
        }
    CloseHandle(snap);
    return parent;
}

bool endProcess(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h)
        return false;
    const bool ok = TerminateProcess(h, 1) != 0;
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    return ok;
}
#endif
}  // namespace

bool ComfyProcess::stopLocal(quint16 port, QString* message)
{
    auto tell = [message](const QString& text) {
        if (message)
            *message = text;
    };
#ifdef Q_OS_WIN
    const DWORD pid = listenerPid(port);
    if (pid == 0) {
        tell(QStringLiteral("no ComfyUI is running on port %1").arg(port));
        return false;
    }
    const QString name = imageName(pid);
    if (!name.startsWith(QStringLiteral("python"), Qt::CaseInsensitive)) {
        tell(QStringLiteral("port %1 belongs to %2 (pid %3), not a ComfyUI python: left running").arg(port).arg(name.isEmpty() ? QStringLiteral("?") : name).arg(pid));
        return false;
    }
    const DWORD launcher = parentPid(pid);   // the venv's python.exe starts the real interpreter as its child
    const bool viaLauncher = launcher != 0 && imageName(launcher).startsWith(QStringLiteral("python"), Qt::CaseInsensitive);
    if (!endProcess(pid)) {
        tell(QStringLiteral("could not stop %1 (pid %2) on port %3").arg(name).arg(pid).arg(port));
        return false;
    }
    if (viaLauncher)
        endProcess(launcher);
    tell(QStringLiteral("ComfyUI stopped (%1, pid %2, port %3)").arg(name).arg(pid).arg(port));
    return true;
#else
    tell(QStringLiteral("stopping a ComfyUI by its port is only built for Windows"));
    return false;
#endif
}

ComfyProcess::ComfyProcess(QObject* parent)
    : QObject(parent)
{
    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, &ComfyProcess::poll);
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (poll_.isActive()) {
            poll_.stop();
            emit failed(QStringLiteral("ComfyUI stopped while starting (exit %1). See %2").arg(code).arg(logPath_));
        }
    });
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}

ComfyProcess::~ComfyProcess()
{
    stop();
}

void ComfyProcess::start(const QString& root, quint16 port, bool customNodes)
{
    if (isRunning())
        return;
    const QDir dir(root);
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("main.py")))) {
        emit failed(QStringLiteral("No ComfyUI in %1 (main.py not found). Set the ComfyUI folder.").arg(QDir::toNativeSeparators(root)));
        return;
    }
#ifdef Q_OS_WIN
    QString python = dir.filePath(venv_ + QStringLiteral("/Scripts/python.exe"));
#else
    QString python = dir.filePath(venv_ + QStringLiteral("/bin/python"));
#endif
    if (!QFileInfo::exists(python)) {
        if (venv_ != QStringLiteral(".venv")) {   // the speech venv is its own; plain python has the wrong packages
            emit failed(QStringLiteral("No %1 in %2: make it first (README.md \"Speech\").").arg(venv_, QDir::toNativeSeparators(root)));
            return;
        }
        python = QStringLiteral("python");
    }
    QStringList args{QStringLiteral("main.py"), QStringLiteral("--disable-auto-launch"), QStringLiteral("--port"), QString::number(port),
                     QStringLiteral("--preview-method"), QStringLiteral("auto")};
    if (!customNodes)
        args << QStringLiteral("--disable-all-custom-nodes");
    else if (!onlyCustomNodes_.isEmpty())
        args << QStringLiteral("--disable-all-custom-nodes") << QStringLiteral("--whitelist-custom-nodes") << onlyCustomNodes_;
    port_ = port;
    logPath_ = QDir(dir.filePath(QStringLiteral(".."))).absoluteFilePath(logName_);
    process_.setWorkingDirectory(dir.absolutePath());
    // UTF-8 output: with stdout in a log file Python writes the Windows code page, and a node printing an emoji (the
    // Qwen3-TTS one prints ✅ while loading) crashes ComfyUI. The main ComfyUI only escaped it because ComfyUI-Manager
    // switches the output itself; the speech one does not load the Manager.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    process_.setProcessEnvironment(env);
    process_.setStandardOutputFile(logPath_);
    if (keepRunning_) {   // detached: not a child that goes with this program (startDetached has no merged channels)
        process_.setStandardErrorFile(logPath_, QIODevice::Append);
        process_.setProgram(python);
        process_.setArguments(args);
#ifdef Q_OS_WIN
        // A ComfyUI that outlives this program must not hold this program's own stdin/stdout/stderr: a caller reading
        // our output through a pipe (a script, an AI tool, `ComfyLive --make=x | more`) would wait for it forever.
        // Its own log file handles are still inherited.
        for (DWORD which : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
            HANDLE h = GetStdHandle(which);
            if (h && h != INVALID_HANDLE_VALUE)
                SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        }
#endif
        detachedPid_ = 0;
        if (!process_.startDetached(&detachedPid_)) {
            emit failed(QStringLiteral("Could not start %1: %2").arg(python, process_.errorString()));
            return;
        }
    } else {
        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.start(python, args);
        if (!process_.waitForStarted(10000)) {
            emit failed(QStringLiteral("Could not start %1: %2").arg(python, process_.errorString()));
            return;
        }
    }
    emit logLine(QStringLiteral("starting ComfyUI (hidden%1, log: %2) ...")
                     .arg(keepRunning_ ? QStringLiteral(", kept running after ComfyLive exits") : QString(),
                          QDir::toNativeSeparators(logPath_)));
    waitedMs_ = 0;
    poll_.start();
}

void ComfyProcess::stop()
{
    poll_.stop();
    if (!isRunning())
        return;
    process_.kill();
    process_.waitForFinished(5000);
    emit logLine(QStringLiteral("ComfyUI stopped"));
}

void ComfyProcess::poll()
{
    waitedMs_ += kPollMs;
#ifdef Q_OS_WIN
    if (keepRunning_ && detachedPid_ > 0) {   // a detached start: no finished() signal, so look whether it died
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(detachedPid_));
        DWORD code = STILL_ACTIVE;
        if (h) {
            GetExitCodeProcess(h, &code);
            CloseHandle(h);
        }
        if (!h || code != STILL_ACTIVE) {
            poll_.stop();
            detachedPid_ = 0;
            emit failed(QStringLiteral("ComfyUI stopped while starting. See %1").arg(QDir::toNativeSeparators(logPath_)));
            return;
        }
    }
#endif
    if (waitedMs_ > kGiveUpMs) {
        poll_.stop();
        emit failed(QStringLiteral("ComfyUI did not answer within %1 s. See %2").arg(kGiveUpMs / 1000).arg(logPath_));
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/system_stats").arg(port_)));
    request.setTransferTimeout(900);
    QNetworkReply* reply = net_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && poll_.isActive()) {
            poll_.stop();
            emit logLine(QStringLiteral("ComfyUI is up after %1 s").arg(waitedMs_ / 1000));
            emit up();
        }
    });
}
