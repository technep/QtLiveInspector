#include "MainWindow.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QtLiveInspector"));
    QCoreApplication::setOrganizationName(QStringLiteral("QtLiveInspector"));

    MainWindow w;
    w.show();
    return app.exec();
}
