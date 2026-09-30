#pragma once
#include <QString>

// Shared "last browsed directory" — persists to QSettings.
// Pass the full result path (file or folder) to set(); get() returns the dir.
class LastDir
{
public:
    static QString get();
    static void    set(const QString& pathOrDir);
};
