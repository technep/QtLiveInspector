#pragma once

#include "InputVisualizer.h"

#include <QQuickItem>

class QSGNode;

// Lightweight scene-graph touch overlay: one indexed triangle list per
// circle with per-vertex alpha feathering — the polygon silhouette is
// hidden by the edge gradient, so circles read as smooth without any
// texture, QPainter pass, or full-window render target. Suitable for
// embedded targets.
//
// Declared in a header (implementation in InputVisualizer.cpp) so tests
// can assert the coordinate contract: setPoints() receives points in THIS
// ITEM's local coordinates — InputVisualizer maps scene coordinates
// through the item chain (pushPoints), which is what keeps the marker
// under the finger when the scene root is rotated.
class QuickTouchOverlay : public QQuickItem
{
public:
    explicit QuickTouchOverlay(QQuickItem *parent = nullptr);

    void setPoints(const QList<TouchPointRecord> &pts);
    // Null image = circle mode. Own copy: the item can outlive the
    // visualizer during application teardown.
    void setMonkeyImage(const QImage &img);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    QList<TouchPointRecord> m_points;
    QImage m_monkey;
};
