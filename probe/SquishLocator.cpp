#include "SquishLocator.h"

#include <QCoreApplication>
#include <QQuickItem>
#include <QWidget>

namespace {

QString esc(QString s)
{
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return s;
}

// Symbolic name of an object as used in Squish 'container' references.
QString symbolicName(QObject *o)
{
    QString n = o->objectName();
    if (n.isEmpty())
        n = QString::fromUtf8(o->metaObject()->className());
    return QLatin1Char(':') + n;
}

QString elementFor(QObject *o, QObject *parent)
{
    const QString cls = QString::fromUtf8(o->metaObject()->className());
    QStringList props;

    if (parent)
        props << QStringLiteral("container='%1'").arg(esc(symbolicName(parent)));

    if (!o->objectName().isEmpty()) {
        props << QStringLiteral("objectName='%1'").arg(esc(o->objectName()));
        props << QStringLiteral("type='%1'").arg(cls);
    } else {
        props << QStringLiteral("type='%1'").arg(cls);
        int occurrence = 1;
        if (parent) {
            const QList<QObject *> siblings = parent->children();
            for (QObject *s : siblings) {
                if (s == o)
                    break;
                if (s->objectName().isEmpty()
                    && QLatin1String(s->metaObject()->className()) == cls)
                    ++occurrence;
            }
        }
        if (occurrence > 1)
            props << QStringLiteral("occurrence='%1'").arg(occurrence);
    }

    if (QWidget *w = qobject_cast<QWidget *>(o)) {
        props << QStringLiteral("visible='%1'").arg(w->isVisible() ? 1 : 0);
    } else if (QQuickItem *it = qobject_cast<QQuickItem *>(o)) {
        props << QStringLiteral("visible='%1'")
                     .arg(it->isVisible() ? QStringLiteral("true")
                                          : QStringLiteral("false"));
    }

    return QLatin1Char('{') + props.join(QLatin1Char(' ')) + QLatin1Char('}');
}

} // namespace

QStringList SquishLocator::chainFor(QObject *obj)
{
    QStringList chain;
    if (!obj)
        return chain;

    QList<QObject *> ancestors;
    for (QObject *o = obj; o; o = o->parent())
        ancestors.prepend(o);

    // The application object itself is not part of Squish names.
    if (!ancestors.isEmpty() && qobject_cast<QCoreApplication *>(ancestors.first()))
        ancestors.removeFirst();

    for (int i = 0; i < ancestors.size(); ++i)
        chain << elementFor(ancestors.at(i), i > 0 ? ancestors.at(i - 1) : nullptr);

    return chain;
}

QString SquishLocator::locatorFor(QObject *obj)
{
    const QStringList chain = chainFor(obj);
    return chain.isEmpty() ? QString() : chain.constLast();
}
