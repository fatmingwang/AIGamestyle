#include "LogParser.h"
#include <QRegularExpression>
#include <QJsonDocument>

// Extract value of a key=value pair from a marker string like
// "[GS:STEP_DONE step=scan assets=74]"
static QString extractKey(const QString& body, const QString& key)
{
    // Matches: key=value  where value runs until next space or ]
    QRegularExpression re(key + QStringLiteral("=([^\\s\\]]+)"));
    auto m = re.match(body);
    if (m.hasMatch())
        return m.captured(1);
    return {};
}

// The "json=" value may contain spaces, so we read everything after "json="
// up to the closing "]" at the very end of the line.
static QString extractJson(const QString& body)
{
    int pos = body.indexOf(QStringLiteral("json="));
    if (pos < 0)
        return {};
    // Skip "json="
    QString jsonStr = body.mid(pos + 5);
    // Strip trailing "]" if present
    if (jsonStr.endsWith(']'))
        jsonStr.chop(1);
    return jsonStr;
}

LogEvent parseLine(const QString& line)
{
    LogEvent ev;
    ev.rawLine = line;

    if (!line.startsWith(QStringLiteral("[GS:")))
        return ev;  // plain log line, type == NONE

    // e.g. "[GS:STEP_START step=scan]"
    int colon = line.indexOf(':');
    int close = line.indexOf(']');
    if (colon < 0 || close < 0)
        return ev;

    QString verb = line.mid(colon + 1, line.indexOf(' ', colon) - colon - 1);
    QString body = line.mid(colon + 1);   // everything after "[GS:"

    if (verb == QStringLiteral("STEP_START")) {
        ev.type     = MarkerType::STEP_START;
        ev.stepName = extractKey(body, QStringLiteral("step"));

    } else if (verb == QStringLiteral("STEP_DONE")) {
        ev.type     = MarkerType::STEP_DONE;
        ev.stepName = extractKey(body, QStringLiteral("step"));
        ev.message  = body.trimmed();

    } else if (verb == QStringLiteral("PROGRESS")) {
        ev.type     = MarkerType::PROGRESS;
        ev.stepName = extractKey(body, QStringLiteral("step"));
        ev.current  = extractKey(body, QStringLiteral("current")).toInt();
        ev.total    = extractKey(body, QStringLiteral("total")).toInt();

    } else if (verb == QStringLiteral("STEP_DETAIL")) {
        ev.type     = MarkerType::STEP_DETAIL;
        ev.stepName = extractKey(body, QStringLiteral("step"));
        QString jsonStr = extractJson(body);
        if (!jsonStr.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                ev.detailJson = doc.object();
        }

    } else if (verb == QStringLiteral("ERROR")) {
        ev.type     = MarkerType::GS_ERROR;
        ev.stepName = extractKey(body, QStringLiteral("step"));
        ev.message  = extractKey(body, QStringLiteral("msg"));
    }

    return ev;
}
