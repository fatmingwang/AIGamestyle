#include "VramLog.h"

#include "Workflow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <cmath>

namespace VramLog {

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/vram_records.json");
}

QList<Record> load()
{
    QList<Record> out;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("runs")).toArray()) {
        const QJsonObject o = v.toObject();
        Record r;
        r.model = o.value(QStringLiteral("model")).toString();
        r.width = o.value(QStringLiteral("width")).toInt();
        r.height = o.value(QStringLiteral("height")).toInt();
        r.frames = qMax(1, o.value(QStringLiteral("frames")).toInt(1));
        r.peakGB = o.value(QStringLiteral("peakGB")).toDouble();
        r.totalGB = o.value(QStringLiteral("totalGB")).toDouble();
        r.seconds = o.value(QStringLiteral("seconds")).toDouble();
        r.failed = o.value(QStringLiteral("failed")).toBool();
        r.when = o.value(QStringLiteral("when")).toString();
        out << r;
    }
    return out;
}

void add(const Record& rIn)
{
    Record r = rIn;
    r.when = QDateTime::currentDateTime().toString(Qt::ISODate);
    QList<Record> all = load();
    all.removeIf([&r](const Record& o) { return o.model == r.model && o.width == r.width && o.height == r.height && o.frames == r.frames; });
    all << r;
    while (all.size() > 400)
        all.removeFirst();
    QJsonArray runs;
    for (const Record& o : all)
        runs << QJsonObject{{QStringLiteral("model"), o.model}, {QStringLiteral("width"), o.width}, {QStringLiteral("height"), o.height},
                            {QStringLiteral("frames"), o.frames}, {QStringLiteral("peakGB"), std::round(o.peakGB * 100) / 100},
                            {QStringLiteral("totalGB"), std::round(o.totalGB * 100) / 100}, {QStringLiteral("seconds"), std::round(o.seconds * 10) / 10},
                            {QStringLiteral("failed"), o.failed}, {QStringLiteral("when"), o.when}};
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("runs"), runs}}).toJson(QJsonDocument::Indented));
}

bool sizeOf(const Workflow& w, Record* out)
{
    const QJsonObject g = w.graph();
    Record r;
    bool found = false;
    for (auto it = g.begin(); it != g.end(); ++it) {
        const QJsonObject n = it.value().toObject();
        const QJsonObject in = n.value(QStringLiteral("inputs")).toObject();
        const QString cls = n.value(QStringLiteral("class_type")).toString();
        for (const char* key : {"unet_name", "ckpt_name"})
            if (r.model.isEmpty() && in.value(QLatin1String(key)).isString())
                r.model = in.value(QLatin1String(key)).toString();
        if (!in.value(QStringLiteral("width")).isDouble() || !in.value(QStringLiteral("height")).isDouble())
            continue;
        // the node that makes the latent (a video node before any other)
        const bool video = cls.contains(QStringLiteral("Video"));
        if (found && !video)
            continue;
        found = true;
        r.width = in.value(QStringLiteral("width")).toInt();
        r.height = in.value(QStringLiteral("height")).toInt();
        r.frames = w.makesVideo() ? qMax(1, in.value(QStringLiteral("length")).toInt(1)) : qMax(1, in.value(QStringLiteral("batch_size")).toInt(1));
    }
    if (found && out)
        *out = r;
    return found;
}

QString describe(const Workflow& w, double totalGB, double usedNowGB)
{
    auto tr = [](const char* s) { return QCoreApplication::translate("Vram", s); };
    Record now;
    if (!sizeOf(w, &now))
        return {};
    const bool video = w.makesVideo();
    QString text = video ? tr("%1 x %2, %3 frames (%4 s)").arg(now.width).arg(now.height).arg(now.frames).arg(now.frames / w.videoFps(), 0, 'f', 1)
                         : (now.frames > 1 ? tr("%1 x %2, %3 pictures").arg(now.width).arg(now.height).arg(now.frames)
                                           : tr("%1 x %2").arg(now.width).arg(now.height));
    const double cost = double(now.width) * now.height * now.frames;
    auto line = [&tr](const Record& r) {
        return (r.failed ? tr("%1 x %2 x %3: FAILED at %4 of %5 GB") : tr("%1 x %2 x %3: peak %4 of %5 GB, %6 s"))
            .arg(r.width).arg(r.height).arg(r.frames).arg(r.peakGB, 0, 'f', 1).arg(r.totalGB, 0, 'f', 1).arg(r.seconds, 0, 'f', 0);
    };
    // the runs of this model on this PC
    const Record* same = nullptr;
    const Record* nearest = nullptr;
    const Record* largestOk = nullptr;
    const Record* smallestFailed = nullptr;
    const QList<Record> all = load();
    auto loadOf = [](const Record& r) { return double(r.width) * r.height * r.frames; };
    for (const Record& r : all) {
        if (r.model != now.model)
            continue;
        if (r.width == now.width && r.height == now.height && r.frames == now.frames)
            same = &r;
        if (!nearest || std::fabs(std::log(loadOf(r) / cost)) < std::fabs(std::log(loadOf(*nearest) / cost)))
            nearest = &r;
        if (!r.failed && (!largestOk || loadOf(r) > loadOf(*largestOk)))
            largestOk = &r;
        if (r.failed && (!smallestFailed || loadOf(r) < loadOf(*smallestFailed)))
            smallestFailed = &r;
    }
    if (same)
        text += QLatin1Char('\n') + tr("This size was run: ") + line(*same);
    else if (nearest)
        text += QLatin1Char('\n') + tr("Not run at this size yet. Nearest run: ") + line(*nearest);
    else
        text += QLatin1Char('\n') + tr("This model was not run on this PC yet: the first run measures it.");
    if (largestOk && largestOk != same && (same || largestOk != nearest)) {
        text += QLatin1Char('\n') + tr("Largest that worked: ") + line(*largestOk);
        }
    if (largestOk && largestOk != same) {
        const double ratio = cost / loadOf(*largestOk);
        if (ratio > 1.05)
            text += QLatin1Char('\n') + tr("This is %1 times that: untested, it may be slow or fail.").arg(ratio, 0, 'f', 1);
    }
    if (smallestFailed) {
        text += QLatin1Char('\n') + tr("Smallest that failed: ") + line(*smallestFailed);
        if (cost >= loadOf(*smallestFailed))
            text += QLatin1Char('\n') + tr("This size is as large or larger: expect it to fail.");
    }
    if (totalGB > 0)
        text += QLatin1Char('\n') + (usedNowGB >= 0 ? tr("Card: %1 GB, %2 GB in use now").arg(totalGB, 0, 'f', 1).arg(usedNowGB, 0, 'f', 1)
                                                    : tr("Card: %1 GB").arg(totalGB, 0, 'f', 1));
    return text;
}

}  // namespace VramLog
