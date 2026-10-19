#include "Picker.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWidget>
#include <QWindow>

Picker::Picker(QObject *parent) : QObject(parent) { }

void Picker::start()
{
    if (m_active)
        return;
    m_active = true;
    qApp->installEventFilter(this);
    QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
}

void Picker::stop()
{
    if (!m_active)
        return;
    m_active = false;
    qApp->removeEventFilter(this);
    QGuiApplication::restoreOverrideCursor();
    emit stopped();
}

QQuickItem *Picker::deepestItemAt(QQuickItem *item, const QPointF &scenePos)
{
    // Later children are on top in Qt Quick stacking order -> scan backwards.
    const QList<QQuickItem *> kids = item->childItems();
    for (auto it = kids.crbegin(); it != kids.crend(); ++it) {
        QQuickItem *c = *it;
        if (!c->isVisible() || c->opacity() <= 0.0)
            continue;
        if (c->objectName().startsWith(QLatin1String("__qtinspector")))
            continue; // never pick our own overlay
        const QPointF local = c->mapFromScene(scenePos);
        if (c->contains(local)) {
            if (QQuickItem *deeper = deepestItemAt(c, scenePos))
                return deeper;
            return c;
        }
    }
    return nullptr;
}

bool Picker::eventFilter(QObject * /*watched*/, QEvent *event)
{
    if (!m_active)
        return false;

    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            stop();
            return true;
        }
        return false;
    }

    if (event->type() != QEvent::MouseButtonPress)
        return false;

    auto *me = static_cast<QMouseEvent *>(event);
    const QPoint globalPos = me->globalPosition().toPoint();

    QObject *found = nullptr;

    // 1) Qt Quick: find the QQuickWindow under the cursor and hit-test items.
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *win : windows) {
        auto *qw = qobject_cast<QQuickWindow *>(win);
        if (!qw || !qw->isVisible() || !qw->geometry().contains(globalPos))
            continue;
        const QPointF scenePos = qw->mapFromGlobal(globalPos);
        if (QQuickItem *item = deepestItemAt(qw->contentItem(), scenePos)) {
            found = item;
            break;
        }
        found = qw; // window itself, no item hit
        break;
    }

    // 2) Qt Widgets fallback.
    if (!found) {
        if (QWidget *w = QApplication::widgetAt(globalPos)) {
            if (w->objectName().startsWith(QLatin1String("__qtinspector")))
                return true; // clicked our own overlay, swallow
            found = w;
        }
    }

    if (found) {
        stop();
        emit picked(found);
    }
    return true; // consume the click so the target app does not react
}
