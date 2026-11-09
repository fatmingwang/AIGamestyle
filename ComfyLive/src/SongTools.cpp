#include "SongTools.h"

#include "Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef COMFYLIVE_DEFAULT_FFMPEG
#define COMFYLIVE_DEFAULT_FFMPEG ""
#endif

namespace SongTools {

const QList<Choice>& singers()
{
    static const QList<Choice> list = {
        {QString(), QT_TRANSLATE_NOOP("Song", "(as in the tags)"), QString()},
        {QStringLiteral("male"), QT_TRANSLATE_NOOP("Song", "Male"), QStringLiteral("male vocals")},
        {QStringLiteral("female"), QT_TRANSLATE_NOOP("Song", "Female"), QStringLiteral("female vocals")},
        {QStringLiteral("duet"), QT_TRANSLATE_NOOP("Song", "Duet (male + female)"), QStringLiteral("male and female duet vocals")},
        {QStringLiteral("choir"), QT_TRANSLATE_NOOP("Song", "Choir"), QStringLiteral("choir vocals")},
        {QStringLiteral("instrumental"), QT_TRANSLATE_NOOP("Song", "Instrumental (no singing)"), QStringLiteral("instrumental")},
    };
    return list;
}

const QList<Choice>& voices()
{
    static const QList<Choice> list = {
        {QString(), QT_TRANSLATE_NOOP("Song", "(as in the tags)"), QString()},
        {QStringLiteral("soft"), QT_TRANSLATE_NOOP("Song", "Soft"), QStringLiteral("soft gentle voice")},
        {QStringLiteral("breathy"), QT_TRANSLATE_NOOP("Song", "Breathy"), QStringLiteral("breathy voice")},
        {QStringLiteral("powerful"), QT_TRANSLATE_NOOP("Song", "Powerful"), QStringLiteral("powerful belting voice")},
        {QStringLiteral("falsetto"), QT_TRANSLATE_NOOP("Song", "Falsetto"), QStringLiteral("soft falsetto")},
        {QStringLiteral("raspy"), QT_TRANSLATE_NOOP("Song", "Raspy"), QStringLiteral("raspy voice")},
    };
    return list;
}

namespace {

QStringList items(const QString& tags)
{
    QStringList out;
    for (const QString& t : tags.split(QLatin1Char(','))) {
        const QString s = t.trimmed();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

bool isListed(const QString& item, const QList<Choice>& list)
{
    for (const Choice& c : list)
        if (!c.tags.isEmpty() && item.compare(c.tags, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

int pitchClass(const QString& root)
{
    static const QHash<QString, int> pc = {
        {QStringLiteral("C"), 0}, {QStringLiteral("C#"), 1}, {QStringLiteral("Db"), 1}, {QStringLiteral("D"), 2},
        {QStringLiteral("D#"), 3}, {QStringLiteral("Eb"), 3}, {QStringLiteral("E"), 4}, {QStringLiteral("F"), 5},
        {QStringLiteral("F#"), 6}, {QStringLiteral("Gb"), 6}, {QStringLiteral("G"), 7}, {QStringLiteral("G#"), 8},
        {QStringLiteral("Ab"), 8}, {QStringLiteral("A"), 9}, {QStringLiteral("A#"), 10}, {QStringLiteral("Bb"), 10},
        {QStringLiteral("B"), 11}};
    return pc.value(root, -1);
}

}  // namespace

QString applyChoice(const QString& tags, const QList<Choice>& list, const QString& id)
{
    QStringList kept;
    for (const QString& item : items(tags))
        if (!isListed(item, list))
            kept << item;
    for (const Choice& c : list)
        if (!id.isEmpty() && c.id == id && !c.tags.isEmpty())
            kept.prepend(c.tags);
    return kept.join(QStringLiteral(", "));
}

QString detectChoice(const QString& tags, const QList<Choice>& list)
{
    for (const QString& item : items(tags))
        for (const Choice& c : list)
            if (!c.tags.isEmpty() && item.compare(c.tags, Qt::CaseInsensitive) == 0)
                return c.id;
    return QString();
}

QString stepKey(const QString& keyscale, int semitones, const QStringList& choices)
{
    const QStringList parts = keyscale.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return QString();
    const int from = pitchClass(parts[0]);
    if (from < 0)
        return QString();
    const int to = ((from + semitones) % 12 + 12) % 12;
    // keep the spelling family: sharps going up (and from a sharp), flats going down (and from a flat)
    const bool preferFlat = parts[0].endsWith(QLatin1Char('b')) || (semitones < 0 && !parts[0].endsWith(QLatin1Char('#')));
    QStringList found;   // the same note spelled both ways (D# / Eb) when it is not a natural one
    for (const QString& c : choices) {
        const QStringList cp = c.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (cp.size() == 2 && cp[1] == parts[1] && pitchClass(cp[0]) == to)
            found << c;
    }
    for (const QString& c : found) {
        const QString root = c.section(QLatin1Char(' '), 0, 0);
        if (root.size() == 1 || root.endsWith(QLatin1Char('b')) == preferFlat)
            return c;
    }
    return found.value(0);
}

QString languageName(const QString& code)
{
    struct Name {
        const char* en;
        const char* zh;
    };
    // the codes TextEncodeAceStepAudio1.5 offers (comfy_extras/nodes_ace.py)
    static const QHash<QString, Name> names = {
        {QStringLiteral("ar"), {"Arabic", "阿拉伯文"}},
        {QStringLiteral("az"), {"Azerbaijani", "亞塞拜然文"}},
        {QStringLiteral("bg"), {"Bulgarian", "保加利亞文"}},
        {QStringLiteral("bn"), {"Bengali", "孟加拉文"}},
        {QStringLiteral("ca"), {"Catalan", "加泰隆尼亞文"}},
        {QStringLiteral("cs"), {"Czech", "捷克文"}},
        {QStringLiteral("da"), {"Danish", "丹麥文"}},
        {QStringLiteral("de"), {"German", "德文"}},
        {QStringLiteral("el"), {"Greek", "希臘文"}},
        {QStringLiteral("en"), {"English", "英文"}},
        {QStringLiteral("es"), {"Spanish", "西班牙文"}},
        {QStringLiteral("fa"), {"Persian", "波斯文"}},
        {QStringLiteral("fi"), {"Finnish", "芬蘭文"}},
        {QStringLiteral("fr"), {"French", "法文"}},
        {QStringLiteral("he"), {"Hebrew", "希伯來文"}},
        {QStringLiteral("hi"), {"Hindi", "印地文"}},
        {QStringLiteral("hr"), {"Croatian", "克羅埃西亞文"}},
        {QStringLiteral("ht"), {"Haitian Creole", "海地克里奧爾文"}},
        {QStringLiteral("hu"), {"Hungarian", "匈牙利文"}},
        {QStringLiteral("id"), {"Indonesian", "印尼文"}},
        {QStringLiteral("is"), {"Icelandic", "冰島文"}},
        {QStringLiteral("it"), {"Italian", "義大利文"}},
        {QStringLiteral("ja"), {"Japanese", "日文"}},
        {QStringLiteral("ko"), {"Korean", "韓文"}},
        {QStringLiteral("la"), {"Latin", "拉丁文"}},
        {QStringLiteral("lt"), {"Lithuanian", "立陶宛文"}},
        {QStringLiteral("ms"), {"Malay", "馬來文"}},
        {QStringLiteral("ne"), {"Nepali", "尼泊爾文"}},
        {QStringLiteral("nl"), {"Dutch", "荷蘭文"}},
        {QStringLiteral("no"), {"Norwegian", "挪威文"}},
        {QStringLiteral("pa"), {"Punjabi", "旁遮普文"}},
        {QStringLiteral("pl"), {"Polish", "波蘭文"}},
        {QStringLiteral("pt"), {"Portuguese", "葡萄牙文"}},
        {QStringLiteral("ro"), {"Romanian", "羅馬尼亞文"}},
        {QStringLiteral("ru"), {"Russian", "俄文"}},
        {QStringLiteral("sa"), {"Sanskrit", "梵文"}},
        {QStringLiteral("sk"), {"Slovak", "斯洛伐克文"}},
        {QStringLiteral("sr"), {"Serbian", "塞爾維亞文"}},
        {QStringLiteral("sv"), {"Swedish", "瑞典文"}},
        {QStringLiteral("sw"), {"Swahili", "史瓦希里文"}},
        {QStringLiteral("ta"), {"Tamil", "坦米爾文"}},
        {QStringLiteral("te"), {"Telugu", "泰盧固文"}},
        {QStringLiteral("th"), {"Thai", "泰文"}},
        {QStringLiteral("tl"), {"Tagalog", "他加祿文"}},
        {QStringLiteral("tr"), {"Turkish", "土耳其文"}},
        {QStringLiteral("uk"), {"Ukrainian", "烏克蘭文"}},
        {QStringLiteral("ur"), {"Urdu", "烏都文"}},
        {QStringLiteral("vi"), {"Vietnamese", "越南文"}},
        {QStringLiteral("yue"), {"Cantonese", "粵語"}},
        {QStringLiteral("zh"), {"Chinese (Mandarin)", "中文（國語）"}},
        {QStringLiteral("unknown"), {"Unknown / other", "未知 / 其他"}},
    };
    const auto it = names.constFind(code);
    if (it == names.constEnd())
        return code;
    return QString::fromUtf8(Language::current() == QStringLiteral("zh_TW") ? it->zh : it->en);
}

QString findFfmpeg(const QString& setting)
{
    QStringList tries;
    if (!setting.isEmpty())
        tries << setting;
    tries << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe"))
          << QStringLiteral(COMFYLIVE_DEFAULT_FFMPEG);
    for (const QString& t : tries)
        if (!t.isEmpty() && QFileInfo(t).isFile())
            return QDir::cleanPath(t);
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString transposedPath(const QString& in, int semitones)
{
    const QFileInfo f(in);
    QString base = f.completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("_[+-]\\d+st$")));   // a transposed song transposed again: from its own name
    return f.dir().filePath(QStringLiteral("%1_%2%3st.%4").arg(base, semitones >= 0 ? QStringLiteral("+") : QString()).arg(semitones).arg(f.suffix()));
}

void transpose(const QString& ffmpeg, const QString& in, int semitones, const QString& out,
               const std::function<void(bool, const QString&)>& done)
{
    if (ffmpeg.isEmpty()) {
        done(false, QCoreApplication::translate("Song", "ffmpeg not found: put ffmpeg.exe next to ComfyLive.exe, or on PATH"));
        return;
    }
    auto hidden = [](QProcess* p) {
#ifdef Q_OS_WIN
        p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#else
        Q_UNUSED(p);
#endif
    };
    // the sample rate first (a quick read of the header): asetrate needs it
    int rate = 48000;
    {
        QProcess probe;
        hidden(&probe);
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-i"), in});
        probe.waitForFinished(10000);
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Audio:.*?(\\d+) Hz")).match(QString::fromUtf8(probe.readAllStandardError()));
        if (m.hasMatch())
            rate = m.captured(1).toInt();
    }
    const double factor = std::pow(2.0, semitones / 12.0);
    const QString filter = QStringLiteral("asetrate=%1,aresample=%2,atempo=%3").arg(rate * factor, 0, 'f', 2).arg(rate).arg(1.0 / factor, 0, 'f', 6);
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                     QStringLiteral("-i"), in, QStringLiteral("-af"), filter};
    const QString suffix = QFileInfo(out).suffix().toLower();
    if (suffix == QStringLiteral("mp3"))
        args << QStringLiteral("-c:a") << QStringLiteral("libmp3lame") << QStringLiteral("-q:a") << QStringLiteral("0");
    args << out;
    auto* p = new QProcess;
    hidden(p);
    QObject::connect(p, &QProcess::finished, p, [p, out, done](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo(out).size() > 0;
        done(ok, ok ? out : (err.isEmpty() ? QStringLiteral("ffmpeg exit %1").arg(code) : err));
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(false, QStringLiteral("could not start ffmpeg"));
    });
    p->start(ffmpeg, args);
}

}  // namespace SongTools
