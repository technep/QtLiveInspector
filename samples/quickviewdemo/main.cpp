#include <QApplication>
#include <QQuickStyle>
#include <QQuickView>

// quickviewdemo — same QML surface as quickdemo, but hosted in a plain
// QQuickView (no ApplicationWindow). Exercises the probe against the
// QQuickWindow-under-QApplication combination: quick overlay input
// visualization, window screenshots, and a scene without a QML Window type.
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");

    QQuickView view;
    view.setObjectName("quickViewWindow");
    view.setSource(QUrl(QStringLiteral("qrc:/QuickViewDemo/Main.qml")));
    if (!view.rootObject()) {
        qFatal("quickviewdemo: failed to load QML (check the qrc import path)");
    }
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(680, 520);
    view.show();
    return app.exec();
}
