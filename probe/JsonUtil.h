#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>

class QObject;
class QMetaProperty;

namespace JsonUtil {

// Convert an arbitrary QVariant to a compact JSON representation.
// QObject pointers become {"object": "<id>", "className": ..., "objectName": ...}.
QJsonValue variantToJson(const QVariant &v, int depth = 0);

// Value of one meta property (enum-aware).
QJsonValue propertyValueJson(QObject *obj, const QMetaProperty &prop);

// Full property list of an object: meta properties + dynamic properties.
QJsonArray propertiesJson(QObject *obj);

// Write a property from a JSON value (scalar conversion, enum by name or value).
bool setPropertyFromJson(QObject *obj, const QString &name, const QJsonValue &value,
                         QString *error);

} // namespace JsonUtil
