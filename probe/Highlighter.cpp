#include "Highlighter.h"

#include <QPainter>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QWidget>
#include <QtMath>

class QuickHighlightOverlay : public QQuickPaintedItem
{
public:
    explicit QuickHighlightOverlay(QQuickItem *parent) : QQuickPaintedItem(parent)
    {
        setObjectName(QStringLiteral("__qtinspector_overlay"));
        setZ(1000000.0);
        setOpaquePainting(false);
        setAntialiasing(true);
    }

    void setPulse(qreal p)
    {
        m_pulse = p;
        update();
    }

    void paint(QPainter *p) override
    {
        QColor fill(255, 0, 0);
        fill.setAlphaF(0.10 + 0.08 * m_pulse);
        p->fillRect(boundingRect(), fill);

        QPen pen(QColor(255, 30, 30), 3.0);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(boundingRect().adjusted(1.5, 1.5, -1.5, -1.5));
    }

private:
    qreal m_pulse = 0.0;
};

class WidgetHighlightOverlay : public QWidget
{
public:
    explicit WidgetHighlightOverlay(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("__qtinspector_overlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setPulse(qreal p)
    {
        m_pulse = p;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor fill(255, 0, 0);
        fill.setAlphaF(0.10 + 0.08 * m_pulse);
        p.fillRect(rect(), fill);
        p.setPen(QPen(QColor(255, 30, 30), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }

private:
    qreal m_pulse = 0.0;
};

Highlighter::Highlighter(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, &Highlighter::sync);
}

void Highlighter::setTarget(QObject *target)
{
    if (m_target == target)
        return;
    clearOverlays();
    m_target = target;
    if (target) {
        m_timer.start();
        sync();
    }
}

void Highlighter::clear()
{
    m_timer.stop();
    m_target = nullptr;
    clearOverlays();
}

void Highlighter::clearOverlays()
{
    if (m_quickOverlay)
        delete m_quickOverlay.data();
    if (m_widgetOverlay)
        delete m_widgetOverlay.data();
}

void Highlighter::sync()
{
    m_phase += 0.05;
    if (m_phase > 1.0)
        m_phase = 0.0;
    const qreal pulse = (qSin(m_phase * 2.0 * M_PI) + 1.0) / 2.0;

    QObject *t = m_target.data();
    if (!t) {
        clear();
        return;
    }

    // Highlighting a window highlights its content.
    if (auto *qw = qobject_cast<QQuickWindow *>(t))
        t = qw->contentItem();

    if (auto *item = qobject_cast<QQuickItem *>(t)) {
        if (m_widgetOverlay)
            m_widgetOverlay->setVisible(false);
        QQuickWindow *win = item->window();
        if (!win || !win->isVisible() || !item->isVisible()) {
            if (m_quickOverlay)
                m_quickOverlay->setVisible(false);
            return;
        }
        if (!m_quickOverlay)
            m_quickOverlay = new QuickHighlightOverlay(win->contentItem());
        if (m_quickOverlay->parentItem() != win->contentItem())
            m_quickOverlay->setParentItem(win->contentItem());
        m_quickOverlay->setPosition(item->mapToItem(win->contentItem(), QPointF(0, 0)));
        m_quickOverlay->setSize(item->size());
        m_quickOverlay->setPulse(pulse);
        m_quickOverlay->setVisible(true);
        return;
    }

    if (auto *widget = qobject_cast<QWidget *>(t)) {
        if (m_quickOverlay)
            m_quickOverlay->setVisible(false);
        QWidget *top = widget->window();
        if (!top->isVisible() || !widget->isVisible()) {
            if (m_widgetOverlay)
                m_widgetOverlay->setVisible(false);
            return;
        }
        if (!m_widgetOverlay)
            m_widgetOverlay = new WidgetHighlightOverlay(top);
        if (m_widgetOverlay->parentWidget() != top)
            m_widgetOverlay->setParent(top);
        m_widgetOverlay->setGeometry(QRect(widget->mapTo(top, QPoint(0, 0)), widget->size()));
        m_widgetOverlay->setPulse(pulse);
        m_widgetOverlay->setVisible(true);
        m_widgetOverlay->raise();
        return;
    }

    // Plain QObject without visual representation.
    clearOverlays();
}
