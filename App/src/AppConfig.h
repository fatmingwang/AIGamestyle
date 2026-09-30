#pragma once
#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// App-wide identity/branding constants, centralized so each only needs to change in one place.
//
// GS_ORG_NAME is used for both QCoreApplication::setOrganizationName() (main.cpp) and every
// QSettings("<org>", "GameStyleAI") construction across the widgets — it determines where
// settings are stored (Windows: HKCU\Software\<org>\GameStyleAI; Linux/macOS:
// ~/.config/<org>/GameStyleAI.conf). Changing this value changes where the app looks for
// previously saved settings, so existing users' saved settings will not carry over.
inline const QString GS_ORG_NAME = QStringLiteral("MyWork");

// Default host pre-filled into the Resource Replace tab's NAS fields (ResourceReplaceWidget) —
// purely a starting point the user edits in the UI, not a hardcoded destination.
inline const QString GS_DEFAULT_NAS_HOST = QStringLiteral("127.0.0.1");

// Use relative-friendly defaults. Runtime will resolve these against the
// application directory (expand `~` as home). This avoids hard-coded
// absolute paths while remaining portable across platforms.
#ifdef Q_OS_WIN
    #define GS_DEFAULT_PYTHON "python"
    #define GS_DEFAULT_PROJECT_ROOT "."
    #define GS_DEFAULT_OUTPUT "./output/10020"
    #define GS_DEFAULT_ASSET "./assets/game"
#else
    #define GS_DEFAULT_PYTHON "python3"
    #define GS_DEFAULT_PROJECT_ROOT "."
    #define GS_DEFAULT_OUTPUT "./output/10020"
    #define GS_DEFAULT_ASSET "./assets/game"
#endif

struct AppConfig {
    QString projectRoot   = QStringLiteral(GS_DEFAULT_PROJECT_ROOT);
    QString outputFolder  = QStringLiteral(GS_DEFAULT_OUTPUT);
    QString assetRoot     = QStringLiteral(GS_DEFAULT_ASSET);
    QString heroReference   = QStringLiteral("");   // empty = use hero_reference from style profile JSON
    QString positivePrompt  = QStringLiteral("");   // empty = use reskin_categories.json
    QString negativePrompt  = QStringLiteral("");
    QString styleProfileId  = QStringLiteral("default_parameters");
    QString batchId         = QStringLiteral("batch_demo");
    QString comfyuiServer   = QStringLiteral("127.0.0.1:8188");
    QString category        = QStringLiteral("default");
    QString pythonExe       = QStringLiteral(GS_DEFAULT_PYTHON);
    double  ipAdapterWeight = 0.75;
    double  denoise         = 0.80;
    int     steps           = 30;
    bool    fullPipeline    = true;
    bool    includeSpine    = false;

    AppConfig();

    // Resolve relative paths (expand '~' and make absolute relative to
    // `QCoreApplication::applicationDirPath()`); call after load() as well.
    void resolveRelativePaths();

    void load(QSettings& s);
    void save(QSettings& s) const;
};
