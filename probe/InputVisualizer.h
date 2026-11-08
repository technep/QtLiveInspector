#pragma once

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QList>
#include <QTimer>
#include <QPointer>

class QQuickPaintedItem;
class QWidget;
class QuickTouchOverlay;

struct TouchPointRecord {
    QPointF pos;  // overlay coords: scene position (Qt Quick) / global (widgets)
    qreal radius;
    qreal opacity;
    int id;
    qint64 pressedAt = 0;   // steady-clock ms, for press-and-hold detection
    qint64 releasedAt = 0;  // steady-clock ms, for the release animation
    bool held = false;      // press-and-hold threshold reached
    bool released = false;  // animating out (expanding ring + fade)
};

// Global event filter that visualizes mouse clicks and touch events in the target app.
class InputVisualizer : public QObject
{
    Q_OBJECT
public:
    explicit InputVisualizer(QObject *parent = nullptr);
    ~InputVisualizer() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Default the visualizer tracks only the FIRST active touch (cheapest,
    // and the common case for embedded UIs). setMultiTouch(true) tracks
    // every finger independently.
    void setMultiTouch(bool multi) { m_multiTouch = multi; }
    bool isMultiTouch() const { return m_multiTouch; }

    // Fun mode for qli_monkey.py stress runs: draw a procedurally generated
    // monkey head instead of the circle. Same press/hold/release animation.
    void setMonkeyMode(bool monkey);
    bool isMonkeyMode() const { return m_monkeyMode; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void animate();

private:
    void ensureOverlay();
    void ensureQuickOverlay();
    void pushPoints();
    // State-machine entry points, called from the event filter:
    // trackPoint adds a touch (or moves an existing one — one circle per
    // touch id, following the finger), releasePoint starts its fade-out.
    void trackPoint(const QPointF &globalPos, int id);
    void releasePoint(int id);

    bool m_enabled = false;
    bool m_multiTouch = false;
    bool m_monkeyMode = false; // monkey icon instead of the circle
    QImage m_monkeyImage;      // generated lazily on first enable
    int m_activeId = -1;      // single-touch mode: the one tracked touch id
    QList<TouchPointRecord> m_points;
    QTimer m_animTimer;
    QPointer<QWidget> m_widgetOverlay;
    QPointer<QuickTouchOverlay> m_quickOverlay;
};
