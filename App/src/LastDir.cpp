#include "LastDir.h"
#include <QSettings>
#include <QFileInfo>
#include <QDir>

QString LastDir::get()
{
    return QSettings().value(QStringLiteral("ui/lastDir"), QDir::homePath()).toString();
}

void LastDir::set(const QString& pathOrDir)
{
    if (pathOrDir.isEmpty()) return;
    QFileInfo fi(pathOrDir);
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    QSettings().setValue(QStringLiteral("ui/lastDir"), dir);
}
