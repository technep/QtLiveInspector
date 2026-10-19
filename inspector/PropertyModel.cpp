#include "PropertyModel.h"

#include <QBrush>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

PropertyModel::PropertyModel(QObject *parent) : QAbstractTableModel(parent) { }

void PropertyModel::setProperties(const QJsonArray &props)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(props.size());
    for (const QJsonValue &v : props) {
        const QJsonObject o = v.toObject();
        PropertyRow r;
        r.name = o[QStringLiteral("name")].toString();
        r.type = o[QStringLiteral("type")].toString();
        r.value = o[QStringLiteral("value")];
        r.writable = o[QStringLiteral("writable")].toBool();
        r.notify = o[QStringLiteral("notify")].toBool();
        r.constant = o[QStringLiteral("constant")].toBool();
        r.dynamic = o[QStringLiteral("dynamic")].toBool();
        m_rows.append(r);
    }
    endResetModel();
}

void PropertyModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

bool PropertyModel::updateValue(const QString &name, const QJsonValue &value)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].name == name) {
            m_rows[i].value = value;
            emit dataChanged(index(i, 2), index(i, 2), {Qt::DisplayRole, Qt::EditRole});
            return true;
        }
    }
    return false;
}

QString PropertyModel::displayValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(qint64(d));
        return QString::number(d);
    }
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Array:
        return QStringLiteral("[%1 items]").arg(v.toArray().size());
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("object"))) {
            QString s = o[QStringLiteral("className")].toString();
            const QString n = o[QStringLiteral("objectName")].toString();
            if (!n.isEmpty())
                s += QStringLiteral(" '%1'").arg(n);
            s += QStringLiteral(" [#%1]").arg(o[QStringLiteral("object")].toString());
            return s;
        }
        if (o.value(QStringLiteral("enum")).toBool())
            return o[QStringLiteral("text")].toString(
                QString::number(o[QStringLiteral("value")].toInt()));
        if (o.contains(QStringLiteral("width")) && o.contains(QStringLiteral("x"))) {
            return QStringLiteral("%1,%2 %3x%4")
                .arg(o[QStringLiteral("x")].toDouble())
                .arg(o[QStringLiteral("y")].toDouble())
                .arg(o[QStringLiteral("width")].toDouble())
                .arg(o[QStringLiteral("height")].toDouble());
        }
        if (o.contains(QStringLiteral("width")))
            return QStringLiteral("%1x%2")
                .arg(o[QStringLiteral("width")].toDouble())
                .arg(o[QStringLiteral("height")].toDouble());
        if (o.contains(QStringLiteral("x")))
            return QStringLiteral("%1,%2")
                .arg(o[QStringLiteral("x")].toDouble())
                .arg(o[QStringLiteral("y")].toDouble());
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    }
    return QString();
}

int PropertyModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int PropertyModel::columnCount(const QModelIndex &) const
{
    return 3;
}

QVariant PropertyModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const PropertyRow &r = m_rows.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case 0: return r.name;
        case 1: return r.type;
        case 2: return displayValue(r.value);
        }
    } else if (role == Qt::ToolTipRole) {
        QString tip = r.type;
        if (r.constant)
            tip += QStringLiteral(" [constant]");
        if (r.notify)
            tip += QStringLiteral(" [has NOTIFY]");
        if (r.dynamic)
            tip += QStringLiteral(" [dynamic]");
        if (index.column() == 2)
            tip += QStringLiteral("\n") + displayValue(r.value);
        return tip;
    } else if (role == Qt::ForegroundRole && index.column() == 2 && !r.writable) {
        return QBrush(QColor(120, 120, 120));
    }
    return QVariant();
}

QVariant PropertyModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return QStringLiteral("Name");
        case 1: return QStringLiteral("Type");
        case 2: return QStringLiteral("Value");
        }
    }
    return QVariant();
}

Qt::ItemFlags PropertyModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 2 && index.row() < m_rows.size()) {
        const PropertyRow &r = m_rows.at(index.row());
        // Only scalar values are editable (rects/objects need structured input).
        if (r.writable && !r.constant
            && (r.value.isDouble() || r.value.isString() || r.value.isBool()))
            f |= Qt::ItemIsEditable;
    }
    return f;
}

bool PropertyModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || index.column() != 2 || index.row() >= m_rows.size())
        return false;
    const PropertyRow &r = m_rows.at(index.row());

    const QString text = value.toString().trimmed();
    QJsonValue nv;
    if (r.value.isBool() || text == QLatin1String("true") || text == QLatin1String("false")) {
        nv = (text == QLatin1String("true") || text == QLatin1String("1"));
    } else {
        bool ok = false;
        const double d = text.toDouble(&ok);
        if (ok && r.value.isDouble())
            nv = d;
        else
            nv = text;
    }
    emit valueEdited(r.name, nv);
    return true;
}
