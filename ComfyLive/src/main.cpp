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
//                 [--graph=recipe.json]
//       the window drawn off screen (after one generation with --generate), saved as a picture. --graph: with that
//       graph, settings or recipe file, and the window's saved settings left as they are
//
// Headless (no window; README.md "Headless mode", the same protocol as GameStyleAI's --headless):
//   ComfyLive.exe --headless [--port=8766] [--host=127.0.0.1] [--server=127.0.0.1:8188] [--graph=g.json] [--start]
//       a TCP server: one JSON command per line in, NDJSON events out
//   ComfyLive.exe --make=recipe.json [--out=x.mp3] [--random-seed] [--stop-comfy-on-exit] [--server=...] [--comfy-root=...]
//       one song, sound, speech or video from a JSON file (a recipe, a settings file or an API graph; --song= is the same), then
//       exit; starts the local ComfyUI when nothing answers and leaves it running (--stop-comfy-on-exit: stops it);
//       exit 0 = the file was written
//   ComfyLive.exe --comfy-stop[=main|speech|all]    stops the local ComfyUIs (8188 main, 8189 speech; all by default).
//       ComfyLive never stops one by itself. --speech-server=127.0.0.1:8189 moves the speech one
//   ComfyLive.exe --headless-test [--start] [--music] [--out=headless_test.png]
//   ComfyLive.exe --song-test     the song helpers' rules (Singer / Voice tags, key steps); no ComfyUI
//   ComfyLive.exe --video-test [--out=video_test.mp4]
//       the video recipe's rules, and frames -> PNG files -> MP4 (ffmpeg); no ComfyUI
//       the server and a built-in client in one process; exit 0 = ALL CHECKS PASSED
#include "ComfyClient.h"
#include "ComfyProcess.h"
#include "HeadlessServer.h"
#include "SongTools.h"
#include "ParamHelp.h"
#include "MainWindow.h"
#include "Translator.h"
#include "VideoTools.h"
#include "Workflow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
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
    const QString graphFile = optionValue(args, QStringLiteral("--graph"));
    if (!graphFile.isEmpty()) {
        QString why;
        if (!w.openGraphForCheck(graphFile, &why)) {
            say(QStringLiteral("cannot open %1: %2").arg(graphFile, why));
            return 1;
        }
    }
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
    o.speechServer = optionValue(args, QStringLiteral("--speech-server"), o.speechServer);
    o.comfyRoot = optionValue(args, QStringLiteral("--comfy-root"), QStringLiteral(COMFYLIVE_DEFAULT_COMFY_ROOT));
    o.graphPath = optionValue(args, QStringLiteral("--graph"));
    o.startComfy = args.contains(QStringLiteral("--start"));
    o.keepComfy = !args.contains(QStringLiteral("--stop-comfy-on-exit"));   // --keep-comfy: the default now
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

// --video-test: the video recipe's rules, and frames -> frame_0001.png ... -> an MP4 and back (no ComfyUI, no model).
int runVideoTest(const QStringList& args)
{
    int failures = 0;
    auto has = [&failures](const QString& what, bool ok) {
        failures += ok ? 0 : 1;
        say(QStringLiteral("%1 %2").arg(ok ? QStringLiteral("ok  ") : QStringLiteral("FAIL"), what));
    };
    Workflow w;
    QString why;
    has(QStringLiteral("the built-in graphs have \"video\""), Workflow::builtInIds().contains(QStringLiteral("video")));
    const bool loaded = w.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video")}, {QStringLiteral("prompt"), QStringLiteral("a knight swings a sword")},
                                      {QStringLiteral("avoid"), QStringLiteral("blurry")}, {QStringLiteral("seconds"), 3}, {QStringLiteral("seed"), 7}}, &why);
    has(QStringLiteral("a video recipe loads ") + why, loaded);
    has(QStringLiteral("it makes a video at 24 frames a second"), w.makesVideo() && w.videoFps() == 24.0);
    has(QStringLiteral("3 seconds are %1 frames (73)").arg(w.value(QStringLiteral("55"), QStringLiteral("length")).toInt()),
        w.value(QStringLiteral("55"), QStringLiteral("length")).toInt() == 73);
    const QString prompt = w.value(QStringLiteral("6"), QStringLiteral("text")).toString();
    has(QStringLiteral("the prompt keeps its words and gets the fixed-camera, plain-background ending"),
        prompt.startsWith(QStringLiteral("a knight swings a sword, ")) && prompt.endsWith(Workflow::videoPromptSuffix()));
    has(QStringLiteral("avoid goes to the negative prompt only"), w.value(QStringLiteral("7"), QStringLiteral("text")).toString() == QStringLiteral("blurry"));
    has(QStringLiteral("the recipe's seed is kept (no random seed)"),
        w.value(QStringLiteral("3"), QStringLiteral("seed")).toInt() == 7 && !w.recipeRandomSeed());
    Workflow raw;
    has(QStringLiteral("\"raw-prompt\": the prompt as written"),
        raw.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video")}, {QStringLiteral("text"), QStringLiteral("only this")},
                        {QStringLiteral("raw-prompt"), true}}, &why) &&
            raw.value(QStringLiteral("6"), QStringLiteral("text")).toString() == QStringLiteral("only this"));
    Workflow h3;
    const bool h3Loaded = h3.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video-h3")}, {QStringLiteral("prompt"), QStringLiteral("a knight swings a sword")},
                                         {QStringLiteral("seconds"), 5}, {QStringLiteral("seed"), 9}}, &why);
    has(QStringLiteral("a HyperFlow recipe (\"video-h3\") loads ") + (h3Loaded ? QString() : why), h3Loaded);
    has(QStringLiteral("it makes a video at 24 frames a second; 5 seconds are %1 frames (124)").arg(h3.value(QStringLiteral("5"), QStringLiteral("length")).toInt()),
        h3.makesVideo() && h3.videoFps() == 24.0 && h3.value(QStringLiteral("5"), QStringLiteral("length")).toInt() == 124);
    has(QStringLiteral("its prompt gets the same ending, its seed goes to the noise"),
        h3.value(QStringLiteral("5"), QStringLiteral("prompt")).toString() == QStringLiteral("a knight swings a sword, ") + Workflow::videoPromptSuffix() &&
            h3.value(QStringLiteral("8"), QStringLiteral("noise_seed")).toInt() == 9 && !h3.recipeRandomSeed());
    Workflow noAvoid;
    has(QStringLiteral("\"avoid\" is refused for it (no negative prompt)"),
        !noAvoid.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video-h3")}, {QStringLiteral("avoid"), QStringLiteral("blurry")}}, &why));
    // a reference picture: 400 x 600 (2:3) -> the video takes its shape, about as many pixels as before
    const QString refPath = QFileInfo(optionValue(args, QStringLiteral("--out"), QStringLiteral("video_test.mp4"))).absolutePath() + QStringLiteral("/video_test_ref.png");
    QDir().mkpath(QFileInfo(refPath).absolutePath());
    QImage ref(400, 600, QImage::Format_RGB32);
    ref.fill(QColor(200, 200, 200));
    ref.save(refPath);
    Workflow pic;
    const bool picLoaded = pic.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video")}, {QStringLiteral("prompt"), QStringLiteral("draws the bow")},
                                           {QStringLiteral("image"), refPath}, {QStringLiteral("seconds"), 2}}, &why);
    has(QStringLiteral("a video recipe with \"image\" loads ") + (picLoaded ? QString() : why), picLoaded);
    has(QStringLiteral("it has the picture as its first frame"), pic.hasStartImage() && pic.startImage() == refPath && pic.makesVideo());
    const int pw = pic.value(QStringLiteral("55"), QStringLiteral("width")).toInt(), ph = pic.value(QStringLiteral("55"), QStringLiteral("height")).toInt();
    has(QStringLiteral("the video takes the picture's shape: %1 x %2 (768 x 1152)").arg(pw).arg(ph), pw == 768 && ph == 1152);
    has(QStringLiteral("its prompt ends for a picture (the look is not described again)"),
        pic.value(QStringLiteral("6"), QStringLiteral("text")).toString() == QStringLiteral("draws the bow, ") + Workflow::videoPromptSuffixWithImage());
    Workflow picH3;
    has(QStringLiteral("the same for HyperFlow, and \"width\" / \"height\" in the recipe win"),
        picH3.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video-h3")}, {QStringLiteral("image"), refPath}, {QStringLiteral("width"), 480},
                          {QStringLiteral("height"), 704}}, &why) &&
            picH3.hasStartImage() && picH3.value(QStringLiteral("5"), QStringLiteral("width")).toInt() == 480 &&
            picH3.value(QStringLiteral("5"), QStringLiteral("height")).toInt() == 704);
    // Remove background: on by default in all four video graphs; "remove-background": false in a recipe turns it off
    bool allOn = true;
    for (const QString& id : {QStringLiteral("video"), QStringLiteral("video-h3"), QStringLiteral("video-image"), QStringLiteral("video-h3-image")}) {
        Workflow g;
        allOn = allOn && Workflow::builtIn(id, &g) && g.hasRemoveBackground() && g.removeBackground();
    }
    has(QStringLiteral("Remove background is on by default in the four video graphs"), allOn);
    has(QStringLiteral("a recipe has it on unless it says false"), w.removeBackground() && pic.removeBackground());
    Workflow keep;
    has(QStringLiteral("\"remove-background\": false: Save takes the decoded frames"),
        keep.fromRecipe({{QStringLiteral("graph"), QStringLiteral("video")}, {QStringLiteral("remove-background"), false}}, &why) &&
            keep.hasRemoveBackground() && !keep.removeBackground() &&
            keep.value(QStringLiteral("9"), QStringLiteral("images")) == QJsonValue(QJsonArray{QStringLiteral("8"), 0}));
    keep.setRemoveBackground(true);
    has(QStringLiteral("turned on again: Save takes the background-removal node's pictures"),
        keep.removeBackground() && keep.value(QStringLiteral("9"), QStringLiteral("images")) == QJsonValue(QJsonArray{QStringLiteral("30"), 0}));
    has(QStringLiteral("the picture graph has no such option"), !Workflow::textToImage().hasRemoveBackground());
    has(QStringLiteral("a text video graph has no picture"), !w.hasStartImage() && w.startImage().isEmpty());
    has(QStringLiteral("the picture and song graphs make no video"), !Workflow::textToImage().makesVideo() && !Workflow::textToMusic().makesVideo());

    // 12 frames with a bar that moves -> PNG files -> read back -> an MP4
    const QString mp4 = QFileInfo(optionValue(args, QStringLiteral("--out"), QStringLiteral("video_test.mp4"))).absoluteFilePath();
    const QString dir = VideoTools::framesDirFor(mp4);
    QList<QImage> frames;
    for (int i = 0; i < 12; ++i) {
        QImage f(320, 192, QImage::Format_ARGB32);   // transparent but for the bar, as with Remove background
        f.fill(Qt::transparent);
        QPainter p(&f);
        p.fillRect(20 + i * 20, 60, 40, 72, QColor(90, 170, 255));
        p.end();
        frames << f;
    }
    why.clear();
    const bool written = VideoTools::writeFrames(frames, dir, &why);
    has(QStringLiteral("12 frames written to ") + QDir::toNativeSeparators(dir) + QLatin1Char(' ') + why, written);
    const QList<QImage> back = VideoTools::readFrames(dir);
    has(QStringLiteral("12 frames read back, in order (%1)").arg(back.size()),
        back.size() == 12 && back.first().hasAlphaChannel() && qAlpha(back.first().pixel(300, 10)) == 0 && back.first().pixel(30, 90) == frames.first().pixel(30, 90) && back.last().pixel(250, 90) == frames.last().pixel(250, 90) &&
            back.first().pixel(250, 90) != back.last().pixel(250, 90));
    has(QStringLiteral("a shorter take replaces a longer one (5 frames left)"),
        VideoTools::writeFrames(frames.mid(0, 5), dir, &why) && VideoTools::readFrames(dir).size() == 5 && VideoTools::writeFrames(frames, dir, &why));
    QFile::remove(mp4);
    const QString ffmpeg = SongTools::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        say(QStringLiteral("skip the MP4: no ffmpeg (tools\\get_ffmpeg.cmd in TOMS, or ffmpeg.exe next to ComfyLive.exe)"));
        say(failures == 0 ? QStringLiteral("ALL CHECKS PASSED") : QStringLiteral("SOME CHECKS FAILED"));
        return failures == 0 ? 0 : 1;
    }
    VideoTools::encode(ffmpeg, dir, 24.0, mp4, [&](bool ok, const QString& message) {
        has(QStringLiteral("the MP4: %1 (%2 bytes)").arg(QDir::toNativeSeparators(message)).arg(QFileInfo(mp4).size()), ok && QFileInfo(mp4).size() > 500);
        bool named = Workflow::writeSettings(mp4, w.graph(), w.name(), {{QStringLiteral("fps"), 24.0}}, nullptr);
        QFile f(Workflow::settingsPathFor(mp4));
        named = named && f.open(QIODevice::ReadOnly) && f.readAll().contains("\"video\": \"" + QFileInfo(mp4).fileName().toUtf8() + "\"");
        f.close();
        has(QStringLiteral("its settings file names it as \"video\""), named);
        Workflow again;
        has(QStringLiteral("the settings file opens as the same graph"),
            again.load(Workflow::settingsPathFor(mp4), nullptr) && again.fromSettings() && again.graph() == w.graph());
        say(failures == 0 ? QStringLiteral("ALL CHECKS PASSED") : QStringLiteral("SOME CHECKS FAILED"));
        qApp->exit(failures == 0 ? 0 : 1);
    });
    QTimer::singleShot(60000, qApp, [] { say(QStringLiteral("FAIL timed out")); qApp->exit(2); });
    return qApp->exec();
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
    const bool videoTest = args.contains(QStringLiteral("--video-test"));
    const QString songFile = optionValue(args, QStringLiteral("--make"), optionValue(args, QStringLiteral("--song")));
    const bool comfyStop = args.contains(QStringLiteral("--comfy-stop")) || !optionValue(args, QStringLiteral("--comfy-stop")).isEmpty();
    if (selfTest || screenshot || headless || headlessTest || songTest || videoTest || comfyStop || !songFile.isEmpty()) {
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
    if (videoTest)
        return runVideoTest(args);
    if (comfyStop) {   // --comfy-stop[=main|speech|all]: end the local ComfyUIs, then exit (0: at least one was stopped)
        const HeadlessServer::Options o = headlessOptions(args);
        const QString which = optionValue(args, QStringLiteral("--comfy-stop"), QStringLiteral("all"));
        bool any = false;
        for (const QString& server : {o.comfyServer, o.speechServer}) {
            if ((which == QStringLiteral("main") && server != o.comfyServer) || (which == QStringLiteral("speech") && server != o.speechServer))
                continue;
            QString message;
            any = ComfyProcess::stopLocal(quint16(QUrl(QStringLiteral("http://") + server).port(8188)), &message) || any;
            say(message);
        }
        return any ? 0 : 1;
    }
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
