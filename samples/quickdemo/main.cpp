#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QuickDemo"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("QuickDemo", "Main");
#else
    // Qt 6.2–6.4 has no loadFromModule; load the resource URL directly.
    // Without the QTP0001 policy the module keeps its URI-based resource
    // prefix, so the file lives at qrc:/<URI>/ (same as quickviewdemo).
    engine.load(QUrl(QStringLiteral("qrc:/QuickDemo/Main.qml")));
#endif

    return app.exec();
}
