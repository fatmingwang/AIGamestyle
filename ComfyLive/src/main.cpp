// ComfyLive -- the parameters of a ComfyUI graph as a form, and the picture as it forms (README.md).
//
//   ComfyLive.exe                            the window; it connects to a ComfyUI that is already running
//   ComfyLive.exe --start                    ... and starts the local one if nothing answers
//   ComfyLive.exe --lang=zh_TW               the window in Traditional Chinese (en: English); the window's Language box remembers its choice
//
// Checks without a window (nothing flashes on the screen):
//   ComfyLive.exe --selftest [--start] [--steps=8] [--out=result.png] [--server=127.0.0.1:8188]
//       connects, runs the built-in graph once, counts the previews and saves the result; exit 0 = it worked
//   ComfyLive.exe --screenshot=window.png [--start] [--generate] [--steps=8] [--lang=en|zh_TW] [--switch-lang=zh_TW]
//       the window drawn off screen (after one generation with --generate), saved as a picture
//
// Headless (no window; README.md "Headless mode", the same protocol as GameStyleAI's --headless):
//   ComfyLive.exe --headless [--port=8766] [--host=127.0.0.1] [--server=127.0.0.1:8188] [--graph=g.json] [--start]
//       a TCP server: one JSON command per line in, NDJSON events out
//   ComfyLive.exe --headless-test [--start] [--music] [--out=headless_test.png]
//       the server and a built-in client in one process; exit 0 = ALL CHECKS PASSED
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "HeadlessServer.h"
#include "MainWindow.h"
#include "Translator.h"
#include "Workflow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef COMFYLIVE_DEFAULT_COMFY_ROOT
#define COMFYLIVE_DEFAULT_COMFY_ROOT "../_comfyui/ComfyUI"
#endif

namespace {

QString optionValue(const QStringList& args, const QString& name, const QString& fallback = QString())
{
    for (const QString& a : args)
        if (a.startsWith(name + QLatin1Char('=')))
            return a.mid(name.size() + 1);
    return fallback;
}

void say(const QString& text)
{
    std::fprintf(stdout, "%s\n", text.toUtf8().constData());
    std::fflush(stdout);
}

int runSelfTest(const QStringList& args)
{
    const QString server = optionValue(args, QStringLiteral("--server"), QStringLiteral("127.0.0.1:8188"));
    const int steps = optionValue(args, QStringLiteral("--steps"), QStringLiteral("8")).toInt();
    const QString out = optionValue(args, QStringLiteral("--out"), QStringLiteral("comfylive_selftest.png"));
    const bool start = args.contains(QStringLiteral("--start"));

    ComfyClient client;
    ComfyProcess process;
    Workflow workflow = Workflow::textToImage();
    int previews = 0, progressEvents = 0, code = 1;
    QString promptId;
    QElapsedTimer clock;
    QObject::connect(&client, &ComfyClient::logLine, [](const QString& t) { say(QStringLiteral("  ") + t); });
    QObject::connect(&process, &ComfyProcess::logLine, [](const QString& t) { say(QStringLiteral("  ") + t); });
    QObject::connect(&process, &ComfyProcess::up, [&] { client.connectToServer(server); });
    QObject::connect(&process, &ComfyProcess::failed, [&](const QString& why) { say(QStringLiteral("FAIL ") + why); qApp->exit(1); });
    QObject::connect(&client, &ComfyClient::connectionFailed, [&](const QString& why) { say(QStringLiteral("FAIL cannot connect: ") + why); qApp->exit(1); });
    QObject::connect(&client, &ComfyClient::connected, [&] {
        for (const QString& line : workflow.fixMissingChoices(client.objectInfo()))
            say(QStringLiteral("  ") + line);
        const QList<ParamDef> params = workflow.params(client.objectInfo());
        say(QStringLiteral("connected: %1 node types; the built-in graph has %2 parameters").arg(client.objectInfo().size()).arg(params.size()));
        for (const ParamDef& p : params) {
            if (p.input == QStringLiteral("steps"))
                workflow.setValue(p.nodeId, p.input, steps);
            if (p.input == QStringLiteral("width") || p.input == QStringLiteral("height"))
                workflow.setValue(p.nodeId, p.input, 768);
        }
        clock.start();
        client.queuePrompt(workflow.graph());
    });
    QObject::connect(&client, &ComfyClient::queued, [&](const QString& id, int) { promptId = id; say(QStringLiteral("queued ") + id); });
    QObject::connect(&client, &ComfyClient::promptRejected, [&](const QString& why) { say(QStringLiteral("FAIL refused: ") + why); qApp->exit(1); });
    QObject::connect(&client, &ComfyClient::progress, [&](const QString&, int, int) { progressEvents++; });
    QObject::connect(&client, &ComfyClient::preview, [&](const QImage&) { previews++; });
    QObject::connect(&client, &ComfyClient::failed, [&](const QString&, const QString& why) { say(QStringLiteral("FAIL ") + why); qApp->exit(1); });
    QObject::connect(&client, &ComfyClient::imageReady, [&](const QString& id, const QImage& image, const QString& file) {
        if (id != promptId)
            return;
        const bool saved = image.save(out);
        say(QStringLiteral("image %1: %2 x %3, saved %4: %5").arg(file).arg(image.width()).arg(image.height()).arg(out, saved ? "yes" : "NO"));
        code = saved ? 0 : 1;
    });
    QObject::connect(&client, &ComfyClient::finished, [&](const QString& id) {
        if (id != promptId)
            return;
        say(QStringLiteral("done in %1 s: %2 progress events, %3 previews").arg(clock.elapsed() / 1000.0, 0, 'f', 1).arg(progressEvents).arg(previews));
        if (previews == 0)
            say(QStringLiteral("NOTE no previews: was ComfyUI started without --preview-method?"));
        QTimer::singleShot(1500, qApp, [&] { qApp->exit(code); });   // the image's download may still be on its way
    });
    QTimer::singleShot(600000, qApp, [] { say(QStringLiteral("FAIL timed out")); qApp->exit(2); });

    client.setServer(server);
    client.ping([&](bool up) {
        if (up)
            client.connectToServer(server);
        else if (start)
            process.start(QStringLiteral(COMFYLIVE_DEFAULT_COMFY_ROOT), quint16(QUrl(QStringLiteral("http://") + server).port(8188)), true);
        else {
            say(QStringLiteral("FAIL no ComfyUI at %1 (add --start to start the local one)").arg(server));
            qApp->exit(1);
        }
    });
    const int result = qApp->exec();
    process.stop();
    say(result == 0 ? QStringLiteral("PASS") : QStringLiteral("FAILED"));
    return result;
}

int runScreenshot(const QStringList& args)
{
    const QString out = optionValue(args, QStringLiteral("--screenshot"));
    const bool generate = args.contains(QStringLiteral("--generate"));
    {
        QSettings s(QStringLiteral("MyWork"), QStringLiteral("ComfyLive"));
        Language::apply(optionValue(args, QStringLiteral("--lang"), s.value(QStringLiteral("language"), Language::systemDefault()).toString()));
    }
    const int steps = optionValue(args, QStringLiteral("--steps"), QStringLiteral("0")).toInt();
    MainWindow w;
    w.resize(1400, 900);
    w.show();
    auto grab = [&w, out](int code) {
        const bool ok = w.grab().save(out);
        say(QStringLiteral("window saved to %1: %2").arg(out, ok ? "yes" : "NO"));
        qApp->exit(ok ? code : 1);
    };
    QObject::connect(&w, &MainWindow::ready, &w, [&] {
        if (!generate) {
            const QString switchTo = optionValue(args, QStringLiteral("--switch-lang"));
            if (!switchTo.isEmpty())   // the Language box, live: the window rebuilt in place
                QTimer::singleShot(300, &w, [&w, switchTo] { w.setLanguage(switchTo); });
            QTimer::singleShot(1200, &w, [grab] { grab(0); });
            return;
        }
        if (steps > 0)
            w.setSteps(steps);
        w.generate();
    });
    QObject::connect(&w, &MainWindow::runDone, &w, [grab](bool ok, const QString& note) {
        if (!ok)
            say(QStringLiteral("the run failed: ") + note);
        QTimer::singleShot(2000, qApp, [grab, ok] { grab(ok ? 0 : 1); });
    });
    QTimer::singleShot(600000, qApp, [grab] { say(QStringLiteral("timed out")); grab(2); });
    QTimer::singleShot(0, &w, [&w, &args] { w.startUp(args.contains(QStringLiteral("--start"))); });
    const int result = qApp->exec();
    w.close();   // stops a ComfyUI it started
    return result;
}

HeadlessServer::Options headlessOptions(const QStringList& args)
{
    HeadlessServer::Options o;
    o.host = optionValue(args, QStringLiteral("--host"), o.host);
    o.port = quint16(optionValue(args, QStringLiteral("--port"), QString::number(o.port)).toUInt());
    o.comfyServer = optionValue(args, QStringLiteral("--server"), o.comfyServer);
    o.comfyRoot = optionValue(args, QStringLiteral("--comfy-root"), QStringLiteral(COMFYLIVE_DEFAULT_COMFY_ROOT));
    o.graphPath = optionValue(args, QStringLiteral("--graph"));
    o.startComfy = args.contains(QStringLiteral("--start"));
    return o;
}

int runHeadless(const QStringList& args)
{
    HeadlessServer server(headlessOptions(args));
    QObject::connect(&server, &HeadlessServer::log, [](const QString& t) { say(QStringLiteral("[headless] ") + t); });
    QString why;
    if (!server.listen(&why)) {
        say(QStringLiteral("[headless] cannot listen: ") + why);
        return 2;
    }
    return qApp->exec();   // until the process is ended (Ctrl+C, or killed)
}

}  // namespace

int main(int argc, char* argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const bool selfTest = args.contains(QStringLiteral("--selftest"));
    const bool screenshot = !optionValue(args, QStringLiteral("--screenshot")).isEmpty();
    const bool headless = args.contains(QStringLiteral("--headless"));
    const bool headlessTest = args.contains(QStringLiteral("--headless-test"));
    if (selfTest || screenshot || headless || headlessTest) {
        qputenv("QT_QPA_PLATFORM", "offscreen");   // nothing on the screen
#ifdef Q_OS_WIN
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))   // the offscreen platform finds no fonts by itself
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
#endif
#ifdef Q_OS_WIN
        // a WIN32 program: output already redirected (a file, a pipe) stays as it is; else the console it was started from
        // (no console at all, e.g. F5 in Visual Studio or a double-click: a console of its own, so the output is seen)
        if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr && (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
#endif
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ComfyLive"));
    app.setOrganizationName(QStringLiteral("MyWork"));
    if (selfTest)
        return runSelfTest(args);
    if (screenshot)
        return runScreenshot(args);
    if (headlessTest)
        return runHeadlessTest(headlessOptions(args), optionValue(args, QStringLiteral("--out"), QStringLiteral("headless_test.png")),
                               args.contains(QStringLiteral("--music")));
    if (headless)
        return runHeadless(args);

    {   // the window's language: --lang=en|zh_TW, else the last one chosen, else the system's
        QSettings s(QStringLiteral("MyWork"), QStringLiteral("ComfyLive"));
        Language::apply(optionValue(args, QStringLiteral("--lang"), s.value(QStringLiteral("language"), Language::systemDefault()).toString()));
    }
    MainWindow w;
    w.show();
    QTimer::singleShot(0, &w, [&w, &args] { w.startUp(args.contains(QStringLiteral("--start"))); });
    return app.exec();
}
