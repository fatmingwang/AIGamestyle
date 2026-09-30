#include <QApplication>
#include "ClientWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HeadlessTestClient"));
    app.setStyle(QStringLiteral("Fusion"));

    ClientWindow w;
    w.show();
    return app.exec();
}
