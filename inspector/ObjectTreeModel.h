#pragma once

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonObject>

struct TreeNode
{
    qint64 id = -1;
    QString name;
    QString className;
    QString address;
    bool isWidget = false;
    bool isWindow = false;
    bool isQuickItem = false;
    TreeNode *parent = nullptr;
    QVector<TreeNode *> children;

    ~TreeNode() { qDeleteAll(children); }

    QString display() const
    {
        if (name.isEmpty())
            return className.isEmpty() ? QStringLiteral("?") : className;
        if (className.isEmpty())
            return name;
        return QStringLiteral("%1 (%2)").arg(name, className);
    }
};

// Object tree model fed from the probe's "tree" response.
// Columns: 0 = Object, 1 = Class, 2 = Address.
class ObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit ObjectTreeModel(QObject *parent = nullptr);
    ~ObjectTreeModel() override;

    void setTree(const QJsonObject &rootObj);
    void clear();

    TreeNode *nodeFromIndex(const QModelIndex &index) const;
    QModelIndex indexForId(qint64 id) const;
    QModelIndex indexForPath(const QList<qint64> &path) const;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    TreeNode *parseNode(const QJsonObject &obj, TreeNode *parent);
    TreeNode *findById(TreeNode *from, qint64 id) const;

    TreeNode *m_root; // invisible root, first child is the virtual app node
};
