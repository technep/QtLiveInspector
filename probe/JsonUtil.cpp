#include "JsonUtil.h"

#include "ObjectRegistry.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QFont>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QTime>
#include <QUrl>

namespace {

QJsonObject rectJson(double x, double y, double w, double h)
{
    QJsonObject o;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    o[QStringLiteral("width")] = w;
    o[QStringLiteral("height")] = h;
    return o;
}

QJsonObject pointJson(double x, double y)
{
    QJsonObject o;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    return o;
}

QJsonObject sizeJson(double w, double h)
{
    QJsonObject o;
    o[QStringLiteral("width")] = w;
    o[QStringLiteral("height")] = h;
    return o;
}

} // namespace

QJsonValue JsonUtil::variantToJson(const QVariant &v, int depth)
{
    if (!v.isValid())
        return QJsonValue();
    if (depth > 4)
        return QStringLiteral("...");

    const QMetaType mt = v.metaType();

    if (mt.flags() & QMetaType::PointerToQObject) {
        QObject *o = v.value<QObject *>();
        if (!o)
            return QJsonValue();
        QJsonObject obj;
        obj[QStringLiteral("object")] =
            QString::number(ObjectRegistry::instance().idForObject(o));
        obj[QStringLiteral("className")] = QString::fromUtf8(o->metaObject()->className());
        obj[QStringLiteral("objectName")] = o->objectName();
        return obj;
    }

    switch (mt.id()) {
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QJsonArray arr;
        const auto list = v.toList();
        for (const QVariant &e : list)
            arr.append(variantToJson(e, depth + 1));
        return arr;
    }
    case QMetaType::QVariantMap: {
        QJsonObject obj;
        const QVariantMap map = v.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            obj[it.key()] = variantToJson(it.value(), depth + 1);
        return obj;
    }
    case QMetaType::QVariantHash: {
        QJsonObject obj;
        const QVariantHash hash = v.toHash();
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it)
            obj[it.key()] = variantToJson(it.value(), depth + 1);
        return obj;
    }
    case QMetaType::QColor:
        return v.value<QColor>().name(QColor::HexArgb);
    case QMetaType::QRect: {
        const QRect r = v.toRect();
        return rectJson(r.x(), r.y(), r.width(), r.height());
    }
    case QMetaType::QRectF: {
        const QRectF r = v.toRectF();
        return rectJson(r.x(), r.y(), r.width(), r.height());
    }
    case QMetaType::QPoint: {
        const QPoint p = v.toPoint();
        return pointJson(p.x(), p.y());
    }
    case QMetaType::QPointF: {
        const QPointF p = v.toPointF();
        return pointJson(p.x(), p.y());
    }
    case QMetaType::QSize: {
        const QSize s = v.toSize();
        return sizeJson(s.width(), s.height());
    }
    case QMetaType::QSizeF: {
        const QSizeF s = v.toSizeF();
        return sizeJson(s.width(), s.height());
    }
    case QMetaType::QUrl:
        return v.toUrl().toString();
    case QMetaType::QDateTime:
        return v.toDateTime().toString(Qt::ISODateWithMs);
    case QMetaType::QDate:
        return v.toDate().toString(Qt::ISODate);
    case QMetaType::QTime:
        return v.toTime().toString(Qt::ISODateWithMs);
    case QMetaType::QFont:
        return v.value<QFont>().toString();
    default:
        break;
    }

    // Scalars and everything QString-convertible.
    QJsonValue jv = v.toJsonValue();
    if (jv.isNull() && mt.id() != QMetaType::Nullptr)
        jv = QStringLiteral("<%1>").arg(QString::fromUtf8(mt.name()));
    return jv;
}

QJsonValue JsonUtil::propertyValueJson(QObject *obj, const QMetaProperty &prop)
{
    const QVariant val = prop.read(obj);
    if (prop.isEnumType()) {
        const QMetaEnum me = prop.enumerator();
        QJsonObject ev;
        const int iv = val.toInt();
        ev[QStringLiteral("value")] = iv;
        if (prop.isFlagType()) {
            ev[QStringLiteral("text")] =
                QString::fromUtf8(me.valueToKeys(iv).constData());
        } else {
            const char *key = me.valueToKey(iv);
            if (key)
                ev[QStringLiteral("text")] = QString::fromUtf8(key);
        }
        ev[QStringLiteral("enum")] = true;
        return ev;
    }
    return variantToJson(val);
}

QJsonArray JsonUtil::propertiesJson(QObject *obj)
{
    QJsonArray arr;
    if (!obj)
        return arr;

    const QMetaObject *mo = obj->metaObject();
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QMetaProperty prop = mo->property(i);
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromUtf8(prop.name());
        o[QStringLiteral("type")] = QString::fromUtf8(prop.typeName());
        o[QStringLiteral("value")] = propertyValueJson(obj, prop);
        o[QStringLiteral("writable")] = prop.isWritable();
        o[QStringLiteral("notify")] = prop.hasNotifySignal();
        o[QStringLiteral("constant")] = prop.isConstant();
        o[QStringLiteral("dynamic")] = false;
        arr.append(o);
    }

    const QList<QByteArray> dynNames = obj->dynamicPropertyNames();
    for (const QByteArray &n : dynNames) {
        QJsonObject o;
        o[QStringLiteral("name")] = QString::fromUtf8(n);
        const QVariant val = obj->property(n.constData());
        o[QStringLiteral("type")] = QString::fromUtf8(val.metaType().name());
        o[QStringLiteral("value")] = variantToJson(val);
        o[QStringLiteral("writable")] = true;
        o[QStringLiteral("notify")] = false;
        o[QStringLiteral("constant")] = false;
        o[QStringLiteral("dynamic")] = true;
        arr.append(o);
    }

    return arr;
}

bool JsonUtil::setPropertyFromJson(QObject *obj, const QString &name, const QJsonValue &value,
                                   QString *error)
{
    if (!obj) {
        *error = QStringLiteral("object no longer exists");
        return false;
    }

    const QByteArray propName = name.toUtf8();
    const int idx = obj->metaObject()->indexOfProperty(propName.constData());

    if (idx < 0) {
        // Dynamic property?
        if (obj->dynamicPropertyNames().contains(propName)) {
            obj->setProperty(propName.constData(), value.toVariant());
            return true;
        }
        *error = QStringLiteral("no such property: %1").arg(name);
        return false;
    }

    const QMetaProperty prop = obj->metaObject()->property(idx);
    if (!prop.isWritable()) {
        *error = QStringLiteral("property %1 is read-only").arg(name);
        return false;
    }

    QVariant v;
    if (prop.isEnumType()) {
        const QMetaEnum me = prop.enumerator();
        bool ok = false;
        int iv = 0;
        if (value.isString()) {
            const QByteArray s = value.toString().toUtf8();
            if (prop.isFlagType())
                iv = int(me.keysToValue(s.constData(), &ok));
            else
                iv = me.keyToValue(s.constData(), &ok);
        } else {
            iv = value.toInt();
            ok = true;
        }
        if (!ok) {
            *error = QStringLiteral("invalid enum value for %1").arg(name);
            return false;
        }
        v = iv;
    } else {
        v = value.toVariant();
        const QMetaType target = prop.metaType();
        if (v.metaType() != target && !v.convert(target)) {
            *error = QStringLiteral("cannot convert value to %1")
                         .arg(QString::fromUtf8(target.name()));
            return false;
        }
    }

    if (!prop.write(obj, v)) {
        *error = QStringLiteral("writing property %1 failed").arg(name);
        return false;
    }
    return true;
}
