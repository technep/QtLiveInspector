#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QuickDemo"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // Qt >= 6.5: load through the qml module registered by qt_add_qml_module.
    engine.loadFromModule("QuickDemo", "Main");
    // On Qt 6.2–6.4 use the resource URL of the module instead:
    // engine.load(QUrl(QStringLiteral("qrc:/qt/qml/QuickDemo/Main.qml")));

    return app.exec();
}
