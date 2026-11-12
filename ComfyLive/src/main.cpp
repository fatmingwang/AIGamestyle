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
//   ComfyLive.exe --song=song.json [--out=song.mp3] [--random-seed] [--keep-comfy] [--server=...] [--comfy-root=...]
//       one song from a JSON file (a song file or a song's settings file), then exit; starts the local ComfyUI when
//       nothing answers (and stops it at the end, unless --keep-comfy); exit 0 = the song was written
//   ComfyLive.exe --headless-test [--start] [--music] [--out=headless_test.png]
//   ComfyLive.exe --song-test     the song helpers' rules (Singer / Voice tags, key steps); no ComfyUI
//       the server and a built-in client in one process; exit 0 = ALL CHECKS PASSED
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "HeadlessServer.h"
#include "SongTools.h"
#include "ParamHelp.h"
#include "MainWindow.h"
#include "Translator.h"
#include "Workflow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
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
    o.keepComfy = args.contains(QStringLiteral("--keep-comfy"));
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


// --song-test: the song helpers' rules (no ComfyUI): Singer / Voice words in the tags, the key a semitone at a time.
int runSongTest()
{
    int failures = 0;
    auto check = [&failures](const QString& what, const QString& got, const QString& want) {
        const bool ok = got == want;
        failures += ok ? 0 : 1;
        say(QStringLiteral("%1 %2: \"%3\"%4").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what, got,
                                                  ok ? QString() : QStringLiteral(" (want \"%1\")").arg(want)));
    };
    const auto& S = SongTools::singers();
    const auto& V = SongTools::voices();
    QString t = QStringLiteral("smooth R&B, lush strings");
    t = SongTools::applyChoice(t, S, QStringLiteral("female"));
    check(QStringLiteral("female"), t, QStringLiteral("female vocals, smooth R&B, lush strings"));
    t = SongTools::applyChoice(t, S, QStringLiteral("male"));
    check(QStringLiteral("female -> male"), t, QStringLiteral("male vocals, smooth R&B, lush strings"));
    t = SongTools::applyChoice(t, V, QStringLiteral("breathy"));
    check(QStringLiteral("+ breathy"), t, QStringLiteral("breathy voice, male vocals, smooth R&B, lush strings"));
    check(QStringLiteral("detect singer"), SongTools::detectChoice(t, S), QStringLiteral("male"));
    t = SongTools::applyChoice(t, S, QString());
    check(QStringLiteral("singer cleared"), t, QStringLiteral("breathy voice, smooth R&B, lush strings"));
    const QStringList keys = [] {
        QStringList k;
        for (const char* q : {"major", "minor"})
            for (const char* r : {"C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B"})
                k << QStringLiteral("%1 %2").arg(QLatin1String(r), QLatin1String(q));
        return k;
    }();
    check(QStringLiteral("D minor +1"), SongTools::stepKey(QStringLiteral("D minor"), 1, keys), QStringLiteral("D# minor"));
    check(QStringLiteral("D minor -1"), SongTools::stepKey(QStringLiteral("D minor"), -1, keys), QStringLiteral("Db minor"));
    check(QStringLiteral("B major +1"), SongTools::stepKey(QStringLiteral("B major"), 1, keys), QStringLiteral("C major"));
    check(QStringLiteral("C major -1"), SongTools::stepKey(QStringLiteral("C major"), -1, keys), QStringLiteral("B major"));
    check(QStringLiteral("Eb minor +1"), SongTools::stepKey(QStringLiteral("Eb minor"), 1, keys), QStringLiteral("E minor"));
    check(QStringLiteral("transposed name"), QFileInfo(SongTools::transposedPath(QStringLiteral("C:/a/song_+2st.mp3"), -3)).fileName(),
          QStringLiteral("song_-3st.mp3"));
    // parameter help: known in both languages, by node type (the Song's seed is not the sampler's), else ComfyUI's tooltip
    auto has = [&failures](const QString& what, bool ok) {
        failures += ok ? 0 : 1;
        say(QStringLiteral("%1 %2").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what));
    };
    const QString ace = QStringLiteral("TextEncodeAceStepAudio1.5");
    int known = 0;
    for (const char* in : {"tags", "lyrics", "seed", "bpm", "duration", "timesignature", "language", "keyscale",
                           "generate_audio_codes", "cfg_scale", "temperature", "top_p", "top_k", "min_p"})
        known += ParamHelp::describe(ace, QString::fromLatin1(in), QString()).isEmpty() ? 0 : 1;
    has(QStringLiteral("help for all 14 ACE-Step song inputs (%1)").arg(known), known == 14);
    const QString aceSeed = ParamHelp::describe(ace, QStringLiteral("seed"), QString());
    const QString samplerSeed = ParamHelp::describe(QStringLiteral("KSampler"), QStringLiteral("seed"), QString());
    has(QStringLiteral("the Song's seed and the sampler's seed are explained apart"), !aceSeed.isEmpty() && !samplerSeed.isEmpty() && aceSeed != samplerSeed);
    has(QStringLiteral("unknown input: ComfyUI's tooltip"), ParamHelp::describe(QStringLiteral("X"), QStringLiteral("y"), QStringLiteral("tip")) == QStringLiteral("tip"));
    has(QStringLiteral("language names: en -> %1, yue -> %2").arg(SongTools::languageName(QStringLiteral("en")), SongTools::languageName(QStringLiteral("yue"))),
        SongTools::languageName(QStringLiteral("en")) == QStringLiteral("English") && SongTools::languageName(QStringLiteral("yue")) == QStringLiteral("Cantonese"));
    Language::apply(QStringLiteral("zh_TW"));
    has(QStringLiteral("language names in 繁體中文: ja -> %1").arg(SongTools::languageName(QStringLiteral("ja"))),
        SongTools::languageName(QStringLiteral("ja")) == QStringLiteral("日文"));
    const QString zh = ParamHelp::describe(ace, QStringLiteral("top_p"), QString());
    Language::apply(QStringLiteral("en"));
    has(QStringLiteral("top_p in 繁體中文: %1").arg(zh.left(24)), zh.contains(QStringLiteral("機率")));
    say(failures == 0 ? QStringLiteral("ALL CHECKS PASSED") : QStringLiteral("SOME CHECKS FAILED"));
    return failures == 0 ? 0 : 1;
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
    const bool songTest = args.contains(QStringLiteral("--song-test"));
    const QString songFile = optionValue(args, QStringLiteral("--song"));
    if (selfTest || screenshot || headless || headlessTest || songTest || !songFile.isEmpty()) {
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
    if (songTest)
        return runSongTest();
    if (!songFile.isEmpty())
        return runSongFile(headlessOptions(args), songFile, optionValue(args, QStringLiteral("--out")),
                           args.contains(QStringLiteral("--random-seed")));
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
