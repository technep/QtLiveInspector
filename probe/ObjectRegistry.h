#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

// Assigns stable numeric ids to QObjects and tracks liveness via QPointer.
// Id 0 is reserved for the virtual "Application" root node.
class ObjectRegistry
{
public:
    static ObjectRegistry &instance()
    {
        static ObjectRegistry inst;
        return inst;
    }

    qint64 idForObject(QObject *obj)
    {
        if (!obj)
            return -1;
        auto it = m_objToId.constFind(obj);
        if (it != m_objToId.constEnd())
            return it.value();
        const qint64 id = m_next++;
        m_objToId.insert(obj, id);
        m_idToObj.insert(id, QPointer<QObject>(obj));
        return id;
    }

    // Returns nullptr when the id is unknown or the object has been deleted.
    QObject *objectForId(qint64 id) const
    {
        auto it = m_idToObj.constFind(id);
        if (it == m_idToObj.constEnd())
            return nullptr;
        return it.value().data();
    }

    // Drop entries whose objects have been destroyed.
    void prune()
    {
        for (auto it = m_idToObj.begin(); it != m_idToObj.end();) {
            if (it.value().isNull())
                it = m_idToObj.erase(it);
            else
                ++it;
        }
        // Rebuild the forward map without stale pointers.
        m_objToId.clear();
        for (auto it = m_idToObj.constBegin(); it != m_idToObj.constEnd(); ++it)
            m_objToId.insert(it.value().data(), it.key());
    }

private:
    ObjectRegistry() = default;
    QHash<QObject *, qint64> m_objToId;
    QHash<qint64, QPointer<QObject>> m_idToObj;
    qint64 m_next = 1;
};
