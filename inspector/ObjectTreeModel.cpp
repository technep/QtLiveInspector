#include "ObjectTreeModel.h"

ObjectTreeModel::ObjectTreeModel(QObject *parent) : QAbstractItemModel(parent)
{
    m_root = new TreeNode;
}

ObjectTreeModel::~ObjectTreeModel()
{
    delete m_root;
}

TreeNode *ObjectTreeModel::parseNode(const QJsonObject &obj, TreeNode *parent)
{
    auto *n = new TreeNode;
    n->id = obj[QStringLiteral("id")].toInteger();
    n->name = obj[QStringLiteral("name")].toString();
    n->className = obj[QStringLiteral("className")].toString();
    n->address = obj[QStringLiteral("address")].toString();
    n->isWidget = obj[QStringLiteral("isWidget")].toBool();
    n->isWindow = obj[QStringLiteral("isWindow")].toBool();
    n->isQuickItem = obj[QStringLiteral("isQuickItem")].toBool();
    n->parent = parent;
    const QJsonArray kids = obj[QStringLiteral("children")].toArray();
    n->children.reserve(kids.size());
    for (const QJsonValue &c : kids)
        n->children.append(parseNode(c.toObject(), n));
    return n;
}

void ObjectTreeModel::setTree(const QJsonObject &rootObj)
{
    beginResetModel();
    qDeleteAll(m_root->children);
    m_root->children.clear();
    m_root->children.append(parseNode(rootObj, m_root));
    endResetModel();
}

void ObjectTreeModel::clear()
{
    beginResetModel();
    qDeleteAll(m_root->children);
    m_root->children.clear();
    endResetModel();
}

TreeNode *ObjectTreeModel::nodeFromIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root;
    return static_cast<TreeNode *>(index.internalPointer());
}

TreeNode *ObjectTreeModel::findById(TreeNode *from, qint64 id) const
{
    if (!from)
        return nullptr;
    if (from->id == id)
        return from;
    for (TreeNode *c : from->children) {
        if (TreeNode *hit = findById(c, id))
            return hit;
    }
    return nullptr;
}

QModelIndex ObjectTreeModel::indexForId(qint64 id) const
{
    TreeNode *n = findById(m_root, id);
    if (!n || n == m_root)
        return QModelIndex();
    return createIndex(n->parent->children.indexOf(n), 0, n);
}

QModelIndex ObjectTreeModel::indexForPath(const QList<qint64> &path) const
{
    TreeNode *node = m_root;
    for (qint64 id : path) {
        TreeNode *next = nullptr;
        for (TreeNode *c : node->children) {
            if (c->id == id) {
                next = c;
                break;
            }
        }
        if (!next)
            return QModelIndex();
        node = next;
    }
    if (node == m_root)
        return QModelIndex();
    return createIndex(node->parent->children.indexOf(node), 0, node);
}

QModelIndex ObjectTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= 3)
        return QModelIndex();
    TreeNode *parentNode = nodeFromIndex(parent);
    if (row >= parentNode->children.size())
        return QModelIndex();
    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex ObjectTreeModel::parent(const QModelIndex &index) const
{
    TreeNode *node = nodeFromIndex(index);
    if (!node || node == m_root || node->parent == m_root)
        return QModelIndex();
    TreeNode *p = node->parent;
    return createIndex(p->parent->children.indexOf(p), 0, p);
}

int ObjectTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    return nodeFromIndex(parent)->children.size();
}

int ObjectTreeModel::columnCount(const QModelIndex &) const
{
    return 3;
}

QVariant ObjectTreeModel::data(const QModelIndex &index, int role) const
{
    TreeNode *node = nodeFromIndex(index);
    if (!node || node == m_root)
        return QVariant();

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return node->display();
        case 1: return node->className;
        case 2: return node->address;
        }
    } else if (role == Qt::ToolTipRole && index.column() == 0) {
        QString tip = node->display();
        if (node->isQuickItem)
            tip += QStringLiteral("\nQQuickItem");
        if (node->isWidget)
            tip += QStringLiteral("\nQWidget");
        if (node->isWindow)
            tip += QStringLiteral("\nWindow");
        return tip;
    }
    return QVariant();
}

QVariant ObjectTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return QStringLiteral("Object");
        case 1: return QStringLiteral("Class");
        case 2: return QStringLiteral("Address");
        }
    }
    return QVariant();
}
