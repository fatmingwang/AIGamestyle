#pragma once
#include <QString>
#include <QJsonObject>

enum class MarkerType {
    NONE,
    STEP_START,
    STEP_DONE,
    PROGRESS,
    STEP_DETAIL,
    GS_ERROR
};

struct LogEvent {
    MarkerType  type      = MarkerType::NONE;
    QString     stepName;
    QString     message;
    int         current   = 0;
    int         total     = 0;
    QJsonObject detailJson;
    QString     rawLine;
};

// Parse one stdout line. If it starts with "[GS:" the type is set accordingly;
// otherwise type == NONE and rawLine carries the plain text.
LogEvent parseLine(const QString& line);
