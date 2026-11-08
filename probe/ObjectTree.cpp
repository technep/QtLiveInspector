#include "ObjectTree.h"

#include "ObjectRegistry.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickItem>
#include <QSet>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <functional>

namespace {

// QObject children, plus QQuickItem visual children that have NO QObject
// parent (e.g. popup/dropdown content attached to the scene via parentItem
// only — invisible to QObject::children()).
QList<QObject *> treeChildren(QObject *obj)
{
    QList<QObject *> children = obj->children();
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        const QList<QQuickItem *> visual = item->childItems();
        for (QQuickItem *c : visual) {
            if (!children.contains(c))
                children.append(c);
        }
    }
    return children;
}

QJsonObject treeRec(QObject *obj, QSet<QObject *> &visited)
{
    visited.insert(obj);
    QJsonObject node = ObjectTree::nodeJson(obj);

    QList<QObject *> children = treeChildren(obj);
    std::sort(children.begin(), children.end(), [](QObject *a, QObject *b) {
        const int c = QString::compare(QString::fromUtf8(a->metaObject()->className()),
                                       QString::fromUtf8(b->metaObject()->className()),
                                       Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
        return a->objectName() < b->objectName();
    });

    QJsonArray kids;
    for (QObject *c : std::as_const(children)) {
        if (!visited.contains(c)) // paranoia guard against reparenting cycles
            kids.append(treeRec(c, visited));
    }
    node[QStringLiteral("children")] = kids;
    return node;
}

} // namespace

QJsonObject ObjectTree::nodeJson(QObject *obj)
{
    QJsonObject n;
    n[QStringLiteral("id")] = double(ObjectRegistry::instance().idForObject(obj));
    n[QStringLiteral("name")] = obj->objectName();
    n[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
    n[QStringLiteral("address")] =
        QStringLiteral("0x%1").arg(quintptr(obj), 0, 16);
    n[QStringLiteral("isWidget")] = obj->isWidgetType();
    n[QStringLiteral("isWindow")] = obj->isWindowType();
    n[QStringLiteral("isQuickItem")] = (qobject_cast<QQuickItem *>(obj) != nullptr);
    return n;
}

QJsonObject ObjectTree::treeJson(QObject *obj)
{
    QSet<QObject *> visited;
    return treeRec(obj, visited);
}

QJsonObject ObjectTree::fullTree()
{
    QJsonObject root;
    root[QStringLiteral("id")] = 0.0;
    root[QStringLiteral("name")] = QStringLiteral("Application");
    root[QStringLiteral("className")] = QString();
    root[QStringLiteral("address")] = QString();
    root[QStringLiteral("isWidget")] = false;
    root[QStringLiteral("isWindow")] = false;
    root[QStringLiteral("isQuickItem")] = false;

    QJsonArray roots;
    QSet<QObject *> added;
    QSet<QObject *> visited;

    auto add = [&](QObject *o) {
        if (o && !added.contains(o)) {
            added.insert(o);
            roots.append(treeRec(o, visited));
        }
    };

    add(QCoreApplication::instance());
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        add(w);
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets)
        add(w);

    root[QStringLiteral("children")] = roots;
    return root;
}

QList<QObject *> ObjectTree::allObjects()
{
    QList<QObject *> roots;
    roots << QCoreApplication::instance();
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        roots << w;
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets)
        roots << w;

    QList<QObject *> out;
    QSet<QObject *> visited;
    std::function<void(QObject *)> walk = [&](QObject *o) {
        if (!o || visited.contains(o))
            return;
        visited.insert(o);
        out << o;
        const QList<QObject *> kids = treeChildren(o);
        for (QObject *c : kids)
            walk(c);
    };
    for (QObject *r : std::as_const(roots))
        walk(r);
    return out;
}

QJsonArray ObjectTree::pathJson(QObject *obj)
{
    QList<qint64> ids;
    for (QObject *o = obj; o; o = o->parent())
        ids.prepend(ObjectRegistry::instance().idForObject(o));

    QJsonArray arr;
    arr.append(0.0); // virtual root
    for (qint64 id : ids)
        arr.append(double(id));
    return arr;
}
