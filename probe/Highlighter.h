#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QuickHighlightOverlay;
class WidgetHighlightOverlay;

// Paints a pulsing red rectangle on top of a target QObject.
//  - QQuickItem / QQuickWindow: overlay item inside the scene (QQuickPaintedItem)
//  - QWidget: translucent child widget painting a red frame
// A timer keeps the overlay glued to the target while it moves/resizes.
class Highlighter : public QObject
{
    Q_OBJECT
public:
    explicit Highlighter(QObject *parent = nullptr);

    void setTarget(QObject *target);
    void clear();

private slots:
    void sync();

private:
    void clearOverlays();

    QPointer<QObject> m_target;
    QPointer<QuickHighlightOverlay> m_quickOverlay;
    QPointer<WidgetHighlightOverlay> m_widgetOverlay;
    QTimer m_timer;
    qreal m_phase = 0.0;
};
