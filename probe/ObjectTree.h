#pragma once

#include <QJsonArray>
#include <QJsonObject>

class QObject;

namespace ObjectTree {

// Flat summary of one object: {id, name, className, address, isWidget, isWindow, isQuickItem}
QJsonObject nodeJson(QObject *obj);

// Recursive tree of one object (children included).
QJsonObject treeJson(QObject *obj);

// The virtual root (id 0): children are the QCoreApplication object plus all
// top-level windows and widgets of the target process.
QJsonObject fullTree();

// Id path from the virtual root down to obj: [0, id1, id2, ...].
QJsonArray pathJson(QObject *obj);

} // namespace ObjectTree
