#pragma once

#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonValue>

struct PropertyRow
{
    QString name;
    QString type;
    QJsonValue value;
    bool writable = false;
    bool notify = false;
    bool constant = false;
    bool dynamic = false;
};

// Property table of the currently selected object.
// Columns: 0 = Name, 1 = Type, 2 = Value (editable for writable scalars).
class PropertyModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit PropertyModel(QObject *parent = nullptr);

    void setProperties(const QJsonArray &props);
    void clear();
    bool updateValue(const QString &name, const QJsonValue &value);

    static QString displayValue(const QJsonValue &v);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

signals:
    void valueEdited(const QString &name, const QJsonValue &value);

private:
    QVector<PropertyRow> m_rows;
};
