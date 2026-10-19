#pragma once

#include <QMetaProperty>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>

// Receives NOTIFY signals of arbitrary properties by connecting them to fake
// slot indexes beyond the meta-object's method table (the same technique
// QSignalSpy uses), then forwards them to a std::function callback.
//
// This class deliberately has no Q_OBJECT macro: its metaObject() is
// QObject's, which keeps the index arithmetic identical between addWatch()
// and qt_metacall().
class NotifyRelay : public QObject
{
public:
    struct Watch
    {
        QPointer<QObject> object;
        QString propertyName;
        QMetaObject::Connection connection;
    };

    std::function<void(QObject *, const QString &)> callback;

    int addWatch(QObject *obj, const QMetaProperty &prop)
    {
        if (!obj || !prop.hasNotifySignal())
            return -1;
        const int fakeIndex = metaObject()->methodCount() + int(m_watches.size());
        QMetaObject::Connection c = QMetaObject::connect(
            obj, prop.notifySignal().methodIndex(), this, fakeIndex, Qt::AutoConnection);
        if (!c)
            return -1;
        m_watches.append({QPointer<QObject>(obj), QString::fromUtf8(prop.name()), c});
        return int(m_watches.size()) - 1;
    }

    void clear()
    {
        for (const Watch &w : m_watches)
            QObject::disconnect(w.connection);
        m_watches.clear();
    }

    int watchCount() const { return int(m_watches.size()); }

protected:
    int qt_metacall(QMetaObject::Call call, int id, void **argv) override
    {
        id = QObject::qt_metacall(call, id, argv);
        if (id < 0)
            return id;
        if (call == QMetaObject::InvokeMetaMethod) {
            if (id < m_watches.size()) {
                const Watch &w = m_watches.at(id);
                if (w.object && callback)
                    callback(w.object.data(), w.propertyName);
                id -= int(m_watches.size());
            }
        } else if (call == QMetaObject::RegisterMethodArgumentMetaType) {
            if (id < m_watches.size()) {
                *reinterpret_cast<QMetaType *>(argv[0]) = QMetaType();
                id -= int(m_watches.size());
            }
        }
        return id;
    }

private:
    QVector<Watch> m_watches;
};
