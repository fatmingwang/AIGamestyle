#include <QApplication>
#include <QCoreApplication>
#include "MainWindow.h"
#include "HeadlessRunner.h"

#ifdef Q_OS_WIN
#include <cstdio>
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

#ifdef QT_DEBUG
    // Visual Studio's "Command Arguments" debugging property is stored in the CMake-generated
    // .vcxproj.user file, which is easy to lose (e.g. a full CMake reconfigure regenerates the
    // project files, same trap as the Working Directory setting — see App/BUILD.md). So that
    // pressing F5 on a Debug build reliably launches headless mode without depending on that IDE
    // setting, default to --headless when the process was started with no arguments at all. This
    // is compiled out entirely in Release (QT_DEBUG isn't defined there — see GameStyleAI.vcxproj's
    // per-config PreprocessorDefinitions), and even in Debug it only applies when truly no
    // arguments were given: passing real ones (via VS's Command Arguments field or an actual
    // command line, e.g. to debug the GUI instead) always overrides this.
    if (args.isEmpty())
        args << QStringLiteral("--headless");
#endif

    const QString mode = args.isEmpty() ? QString() : args.first();
    const bool isHeadless     = (mode == QStringLiteral("--headless"));
    // --headless-test: starts the same server as --headless plus a built-in test client that
    // exercises it and reports PASS/FAIL, then exits — a one-command way to check the server
    // works at all before wiring up a separate client. See HeadlessRunner::runHeadlessSelfTest.
    const bool isHeadlessTest = (mode == QStringLiteral("--headless-test"));

    if (isHeadless || isHeadlessTest) {
#ifdef Q_OS_WIN
        // This binary is built WIN32 (no console) for the GUI's sake. If stdout/stderr were
        // explicitly redirected to a file or pipe by the caller (the normal case for CI/build
        // scripts), a valid handle already exists here — leave it alone.
        if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr) {
            // Launched from an existing console (cmd.exe, PowerShell, a terminal) — attach to
            // it instead of opening a second window. If that fails, there is no console to
            // attach to at all — the Visual Studio "F5" case (devenv.exe has none of its own)
            // and the Explorer double-click case — so allocate a brand new one instead, or the
            // [headless] log has nowhere visible to go.
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
                AllocConsole();
            FILE* f;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
#endif
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("GameStyleAI"));
        app.setOrganizationName(GS_ORG_NAME);

        return isHeadlessTest ? runHeadlessSelfTest(args) : runHeadless(args);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("GameStyleAI"));
    app.setOrganizationName(GS_ORG_NAME);
    app.setStyle(QStringLiteral("Fusion"));

    // Fusion + Windows dark mode produces dark-on-dark QToolTip — force readable colors.
    app.setStyleSheet(QStringLiteral(
        "QToolTip {"
        "  background-color: #3c3f41;"
        "  color: #f0f0f0;"
        "  border: 1px solid #666;"
        "  padding: 4px 6px;"
        "  border-radius: 3px;"
        "}"));

    MainWindow w;
    w.show();

    return app.exec();
}
