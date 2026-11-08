#pragma once

#include <QObject>
#include <QPointF>

class QQuickItem;

// Application-wide event filter that turns the next mouse click in the target
// application into an object pick: the click is consumed (the app does not
// react to it) and the deepest QQuickItem / QWidget under the cursor is
// reported via the picked() signal. Escape cancels picking.
class Picker : public QObject
{
    Q_OBJECT
public:
    explicit Picker(QObject *parent = nullptr);

    bool isActive() const { return m_active; }

    void start();
    void stop();

signals:
    void picked(QObject *object);
    void stopped();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QQuickItem *deepestItemAt(QQuickItem *item, const QPointF &scenePos);

    bool m_active = false;
};
