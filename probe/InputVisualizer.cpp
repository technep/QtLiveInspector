#include "InputVisualizer.h"
#include "QuickTouchOverlay.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QPainter>
#include <QWidget>
#include <QWindow>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QSGImageNode>
#include <QSGOpacityNode>
#include <QSGTexture>
#include <QtMath>

class TouchOverlayWidget : public QWidget
{
public:
    explicit TouchOverlayWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("__qtinspector_touch_overlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setPoints(const QList<TouchPointRecord> &pts)
    {
        m_points = pts;
        update();
    }

    // Null image = circle mode. The overlay keeps its own copy so the
    // visualizer can die before the widget teardown without dangling.
    void setMonkeyImage(const QImage &img) { m_monkey = img; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        for (const auto &pt : std::as_const(m_points)) {
            const bool held = pt.held && !pt.released;
            QColor strokeColor = held
                ? QColor(255, 152, 0, int(255 * pt.opacity))
                : QColor(0, 162, 255, int(255 * pt.opacity));
            QColor fillColor = held
                ? QColor(255, 152, 0, int(80 * pt.opacity))
                : QColor(0, 162, 255, int(60 * pt.opacity));

            const QPointF c = mapFromGlobal(pt.pos);
            if (!m_monkey.isNull() && pt.radius > 0.5) {
                if (held) { // orange halo behind the monkey = press-and-hold
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(255, 152, 0, int(120 * pt.opacity)));
                    p.drawEllipse(c, pt.radius * 1.15, pt.radius * 1.15);
                }
                p.setOpacity(pt.opacity);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                const qreal r = pt.radius;
                p.drawImage(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r),
                            m_monkey);
                p.setOpacity(1.0);
                continue;
            }
            p.setPen(QPen(strokeColor, 2.5));
            p.setBrush(fillColor);
            p.drawEllipse(c, pt.radius, pt.radius);
        }
    }

private:
    QList<TouchPointRecord> m_points;
    QImage m_monkey;
};

// The overlay class itself lives in QuickTouchOverlay.h so tests can
// assert the scene->local coordinate contract of setPoints().

namespace {

constexpr qreal kStrokeWidth = 2.5;
constexpr qreal kBaseRadius = 12.0;     // circle size while a touch is active
constexpr int kPopInMs = 120;           // press scale-up (0 -> base radius)
constexpr int kHoldMs = 600;            // press-and-hold threshold
constexpr qreal kReleaseGrowth = 90.0;  // px/s the release ring expands
constexpr qreal kReleaseLifetime = 0.4; // s until the release ring is gone

// A flat-color ring band (triangle strip) — the primitive that renders
// reliably across scene-graph backends. Smooth circles are composited from
// a few concentric bands with stepped alpha; the bands are fixed-pixel
// widths, so they read as ~1px antialiasing at any circle size.
QSGGeometryNode *makeBand(const QPointF &center, qreal rOuter, qreal rInner,
                          int n, const QColor &color)
{
    QSGGeometry *geometry = new QSGGeometry(
        QSGGeometry::defaultAttributes_Point2D(), 2 * (n + 1), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    for (int i = 0; i <= n; ++i) {
        const qreal a = (2.0 * M_PI * i) / n;
        const qreal ca = std::cos(a), sa = std::sin(a);
        v[2 * i].set(float(center.x() + rOuter * ca),
                     float(center.y() + rOuter * sa));
        v[2 * i + 1].set(float(center.x() + rInner * ca),
                         float(center.y() + rInner * sa));
    }
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// One circle: solid fill disc + 3-band stroke (fade-in, solid, fade-out),
// each band a flat-color strip. Adaptive resolution keeps facets under
// ~0.7 px at any radius.
QSGNode *makeCircle(const QPointF &center, qreal radius, qreal opacity,
                    bool held)
{
    const QColor fillSolid = held ? QColor(255, 152, 0, int(80 * opacity))
                                  : QColor(0, 162, 255, int(60 * opacity));
    const QColor fillSoft = held ? QColor(255, 152, 0, int(28 * opacity))
                                 : QColor(0, 162, 255, int(22 * opacity));
    const QColor strokeSolid = held ? QColor(255, 152, 0, int(255 * opacity))
                                    : QColor(0, 162, 255, int(255 * opacity));
    const QColor strokeSoft = held ? QColor(255, 152, 0, int(110 * opacity))
                                   : QColor(0, 162, 255, int(110 * opacity));

    const qreal rOut = radius + kStrokeWidth / 2.0;
    const qreal rIn = radius - kStrokeWidth / 2.0;
    const bool hasStroke = rOut > 1.0 && strokeSolid.alpha() > 3;
    const qreal maxR = (hasStroke ? rOut : radius) + 1.2;
    const int n = qBound(48, int(maxR * 3.0), 128);

    auto *group = new QSGNode;
    // fill: soft outer halo + solid core (a two-step feather)
    const qreal rFillHalo = qMax(radius * 0.55, radius - 1.6);
    group->appendChildNode(
        makeBand(center, radius, qMin(rFillHalo, radius - 0.01), n, fillSoft));
    group->appendChildNode(
        makeBand(center, qMin(rFillHalo, radius), 0.01, n, fillSolid));
    if (hasStroke) {
        const qreal f = 1.1; // fade band width in px
        group->appendChildNode(makeBand(
            center, rOut + f, rOut, n, strokeSoft));
        group->appendChildNode(makeBand(
            center, rOut, qMax(rIn, 0.01), n, strokeSolid));
        group->appendChildNode(makeBand(
            center, qMax(rIn, 0.01), qMax(rIn - f, 0.01), n, strokeSoft));
    }
    return group;
}

// Monkey-mode icon, drawn procedurally once — no asset files, no resource
// compiler, works in a bare LD_PRELOAD lib. 128px, transparent background;
// scaled by pt.radius at draw time just like the circle.
QImage makeMonkeyImage()
{
    QImage img(128, 128, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QColor fur(146, 96, 56);
    const QColor furLight(214, 170, 128);
    const QColor face(222, 184, 138);

    p.setBrush(fur);                       // head
    p.drawEllipse(24, 20, 80, 84);
    p.drawEllipse(10, 40, 28, 28);         // ears
    p.drawEllipse(90, 40, 28, 28);
    p.setBrush(furLight);                  // inner ears
    p.drawEllipse(16, 46, 16, 16);
    p.drawEllipse(96, 46, 16, 16);
    p.setBrush(face);                      // muzzle area
    p.drawEllipse(38, 40, 52, 58);
    p.setBrush(Qt::white);                 // eyes
    p.drawEllipse(48, 56, 14, 14);
    p.drawEllipse(66, 56, 14, 14);
    p.setBrush(Qt::black);
    p.drawEllipse(53, 61, 6, 6);
    p.drawEllipse(71, 61, 6, 6);
    p.setBrush(fur);                       // nose
    p.drawEllipse(58, 70, 12, 8);
    p.setPen(QPen(Qt::black, 2.5));        // smile
    p.setBrush(Qt::NoBrush);
    p.drawArc(52, 68, 24, 18, 200 * 16, 140 * 16);
    return img;
}

} // namespace

QuickTouchOverlay::QuickTouchOverlay(QQuickItem *parent) : QQuickItem(parent)
{
    setObjectName(QStringLiteral("__qtinspector_touch_overlay"));
    // QQuickOverlay (dialogs/popups/dropdowns) sits at z=1000001 —
    // stay above it so ripples are visible over popup overlays.
    setZ(1000000000.0);
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptTouchEvents(false);
    setAcceptHoverEvents(false);
    setFlag(ItemHasContents, true);
}

void QuickTouchOverlay::setPoints(const QList<TouchPointRecord> &pts)
{
    // InputVisualizer delivers points in THIS ITEM's local coordinates
    // (scene coordinates mapped through the item chain in pushPoints —
    // required when the scene root is rotated). GUI thread only.
    m_points = pts;
    update();
}

void QuickTouchOverlay::setMonkeyImage(const QImage &img)
{
    m_monkey = img;
    update();
}

QSGNode *QuickTouchOverlay::updatePaintNode(QSGNode *oldNode,
                                            UpdatePaintNodeData *)
{
    // Rebuild from scratch — a handful of circles is a few hundred floats;
    // far cheaper than any caching logic, and returning nullptr when idle
    // drops the geometry (and GPU memory) entirely.
    delete oldNode;
    if (m_points.isEmpty())
        return nullptr;

    auto *root = new QSGNode;
    for (const auto &pt : std::as_const(m_points)) {
        // pressed/moved: blue, press-and-hold: orange, released: blue ripple-out
        const bool held = pt.held && !pt.released;
        if (!m_monkey.isNull() && pt.radius > 0.5 && window()) {
            // Monkey mode (qli_monkey.py runs): a textured quad scaled by
            // pt.radius, faded via a QSGOpacityNode — the press/hold/release
            // animation is identical to the circle path.
            auto *fade = new QSGOpacityNode;
            fade->setOpacity(pt.opacity);
            if (held) { // orange halo behind the monkey = press-and-hold
                fade->appendChildNode(makeCircle(
                    pt.pos, pt.radius * 1.15, 1.0, true));
            }
            QSGTexture *tex = window()->createTextureFromImage(m_monkey);
            QSGImageNode *img = window()->createImageNode();
            img->setTexture(tex);
            img->setOwnsTexture(true);
            const qreal r = pt.radius;
            img->setRect(QRectF(pt.pos.x() - r, pt.pos.y() - r,
                                2 * r, 2 * r));
            fade->appendChildNode(img);
            root->appendChildNode(fade);
            continue;
        }
        root->appendChildNode(makeCircle(pt.pos, pt.radius, pt.opacity,
                                         held));
    }
    return root;
}

InputVisualizer::InputVisualizer(QObject *parent) : QObject(parent)
{
    // 30 FPS is plenty for the release ripple and hold detection; the timer
    // only runs while something is animating or awaiting hold detection.
    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, &InputVisualizer::animate);
}

InputVisualizer::~InputVisualizer()
{
    if (QCoreApplication::closingDown()) {
        // We are typically parented to the QApplication, so on shutdown this
        // destructor runs from INSIDE ~QApplication (via deleteChildren).
        // Detaching the event filter from the app object mid-destruction
        // segfaults in QObject::removeEventFilter (observed on exit with the
        // standalone inputviz lib), and is pointless — no further events are
        // dispatched. Member cleanup only; overlays are already gone.
        m_animTimer.stop();
        m_points.clear();
        return;
    }
    setEnabled(false);
}

void InputVisualizer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (m_enabled) {
        qApp->installEventFilter(this);
        // The animation timer is started on demand by trackPoint/releasePoint.
    } else {
        qApp->removeEventFilter(this);
        m_animTimer.stop();
        m_points.clear();
        m_activeId = -1;
        if (m_widgetOverlay) {
            m_widgetOverlay->deleteLater();
            m_widgetOverlay = nullptr;
        }
        if (m_quickOverlay) {
            m_quickOverlay->deleteLater();
            m_quickOverlay = nullptr;
        }
    }
}

void InputVisualizer::setMonkeyMode(bool monkey)
{
    if (m_monkeyMode == monkey)
        return;
    m_monkeyMode = monkey;
    if (monkey && m_monkeyImage.isNull())
        m_monkeyImage = makeMonkeyImage();
    if (m_widgetOverlay)
        static_cast<TouchOverlayWidget *>(m_widgetOverlay.data())
            ->setMonkeyImage(monkey ? m_monkeyImage : QImage());
    if (m_quickOverlay)
        m_quickOverlay->setMonkeyImage(monkey ? m_monkeyImage : QImage());
}

void InputVisualizer::trackPoint(const QPointF &globalPos, int id)
{
    if (!m_multiTouch) {
        // First-touch-wins: ignore other fingers while one is tracked.
        if (m_activeId != -1 && id != m_activeId)
            return;
        if (m_activeId == -1)
            m_activeId = id;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto &pt : m_points) {
        if (pt.id == id && !pt.released) {
            pt.pos = globalPos; // the touch moved — relocate the same circle
            pushPoints();
            return;
        }
    }
    if (m_points.size() >= 32) // bound memory/CPU on embedded targets
        m_points.removeFirst();
    TouchPointRecord pt;
    pt.pos = globalPos;
    pt.radius = 0.0; // pop-in: animate() scales it up to kBaseRadius
    pt.opacity = 1.0;
    pt.id = id;
    pt.pressedAt = now;
    m_points.append(pt);
    ensureOverlay();
    pushPoints(); // don't wait for the first timer tick — paint now
    if (!m_animTimer.isActive())
        m_animTimer.start();
}

void InputVisualizer::releasePoint(int id)
{
    if (!m_multiTouch && id != m_activeId)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool found = false;
    for (auto &pt : m_points) {
        if (pt.id == id && !pt.released) {
            pt.released = true;
            pt.releasedAt = now;
            found = true;
        }
    }
    if (!m_multiTouch)
        m_activeId = -1;
    if (found && !m_animTimer.isActive())
        m_animTimer.start();
    if (found)
        pushPoints();
}

void InputVisualizer::pushPoints()
{
    if (m_widgetOverlay && m_widgetOverlay->isVisible()) {
        static_cast<TouchOverlayWidget *>(m_widgetOverlay.data())->setPoints(m_points);
    }

    if (m_quickOverlay) {
        if (QQuickItem *parent = m_quickOverlay->parentItem())
            m_quickOverlay->setSize(parent->size());
        // Points arrive as SCENE coordinates. With an untransformed
        // contentItem that equals overlay-local coordinates — but embedded
        // targets often rotate the scene root (QQuickRootItem) by
        // 90/180/270° for software display rotation, and the overlay
        // inherits that transform. Map each point into the overlay's local
        // system so the marker stays under the physical finger.
        QList<TouchPointRecord> local = m_points;
        for (auto &pt : local)
            pt.pos = m_quickOverlay->mapFromScene(pt.pos);
        m_quickOverlay->setPoints(local);
    }
}

void InputVisualizer::ensureOverlay()
{
    const QWidgetList tops = QApplication::topLevelWidgets();
    QWidget *activeTop = nullptr;
    for (QWidget *w : tops) {
        if (w->isVisible()) {
            activeTop = w;
            break;
        }
    }

    if (!activeTop) {
        // No widgets — a Qt Quick window gets a scene-graph overlay instead.
        ensureQuickOverlay();
        return;
    }

    if (m_quickOverlay)
        m_quickOverlay->setVisible(false);

    if (!m_widgetOverlay || m_widgetOverlay->parentWidget() != activeTop) {
        if (m_widgetOverlay)
            m_widgetOverlay->deleteLater();
        m_widgetOverlay = new TouchOverlayWidget(activeTop);
        static_cast<TouchOverlayWidget *>(m_widgetOverlay.data())
            ->setMonkeyImage(m_monkeyMode ? m_monkeyImage : QImage());
    }
    m_widgetOverlay->setGeometry(activeTop->rect());
    m_widgetOverlay->show();
    m_widgetOverlay->raise();
}

void InputVisualizer::ensureQuickOverlay()
{
    QQuickWindow *activeWindow = nullptr;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (qw && qw->isVisible()) {
            activeWindow = qw;
            break;
        }
    }
    if (!activeWindow)
        return;

    if (m_widgetOverlay)
        m_widgetOverlay->setVisible(false);

    QQuickItem *content = activeWindow->contentItem();
    if (!m_quickOverlay) {
        m_quickOverlay = new QuickTouchOverlay(content);
        m_quickOverlay->setMonkeyImage(m_monkeyMode ? m_monkeyImage
                                                    : QImage());
    }
    if (m_quickOverlay->parentItem() != content)
        m_quickOverlay->setParentItem(content);
    m_quickOverlay->setSize(content->size());
    m_quickOverlay->setVisible(true);
}

namespace {

// Position of a mouse/touch event in the OVERLAY's coordinate system.
// For Qt Quick deliveries that is the SCENE position (hit-testing uses it,
// so it is trustworthy) — the overlay item sits at the contentItem origin
// where scene == item-local. Global coordinates cannot be trusted on every
// platform: with multiple screens, QQuickItem::mapFromGlobal routes through
// the screen containing the point, which may not be the window's screen
// (observed: a 323px shift under XWayland).
QPointF eventOverlayPos(QObject *watched, QEvent *event)
{
    // Scene position covers both QQuickWindow and QQuickItem receivers:
    // delivery forwards events to items via sendEvent, so the filter runs
    // more than once per input event — scenePosition is identical for all.
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove: {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (qobject_cast<QQuickItem *>(watched)
            || qobject_cast<QQuickWindow *>(watched))
            return me->scenePosition();
        if (auto *w = qobject_cast<QWidget *>(watched))
            return w->mapToGlobal(me->position().toPoint()); // widget path
        return me->globalPosition();
    }
    default:
        return {};
    }
}

} // namespace

bool InputVisualizer::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled)
        return false;

    // Only the first delivery (to the window/widget, or raw app-level
    // deliveries in tests/exotic paths) is authoritative: Qt Quick re-posts
    // events to items during delivery and may rewrite their positions on
    // the way — tracking those would overwrite the correct position with a
    // mutated one.
    const bool windowLevel = qobject_cast<QQuickWindow *>(watched)
        || qobject_cast<QWidget *>(watched)
        || watched == QCoreApplication::instance();

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        if (windowLevel)
            trackPoint(eventOverlayPos(watched, event), 0);
        break;
    case QEvent::MouseMove: {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (windowLevel && me->buttons() != Qt::NoButton)
            trackPoint(eventOverlayPos(watched, event), 0); // relocate — no trail
        break;
    }
    case QEvent::MouseButtonRelease:
        if (windowLevel)
            releasePoint(0);
        break;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate: {
        if (!windowLevel)
            break;
        const auto *te = static_cast<QTouchEvent *>(event);
        for (const auto &tp : te->points()) {
            if (tp.state() == QEventPoint::Released)
                releasePoint(tp.id());
            else
                trackPoint(tp.scenePosition(), tp.id());
        }
        break;
    }
    case QEvent::TouchEnd:
        for (const auto &pt : std::as_const(m_points))
            releasePoint(pt.id);
        break;
    default:
        break;
    }

    return false; // Do not consume event
}

void InputVisualizer::animate()
{
    if (m_points.isEmpty()) {
        m_animTimer.stop(); // idle: no periodic wakeups at all
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool pending = false; // anything that needs a future tick?
    for (int i = m_points.size() - 1; i >= 0; --i) {
        auto &pt = m_points[i];
        if (pt.released) {
            const qreal secs = (now - pt.releasedAt) / 1000.0;
            pt.radius = kBaseRadius + kReleaseGrowth * secs;
            pt.opacity = 1.0 - secs / kReleaseLifetime;
            if (pt.opacity <= 0.0)
                m_points.removeAt(i);
            else
                pending = true;
        } else {
            const qint64 age = now - pt.pressedAt;
            if (age < kPopInMs) {
                // press: scale up from 0, ease-out
                const qreal t = age / qreal(kPopInMs);
                pt.radius = kBaseRadius * (1.0 - (1.0 - t) * (1.0 - t));
                pending = true;
            } else {
                pt.radius = kBaseRadius;
            }
            if (!pt.held) {
                if (age >= kHoldMs)
                    pt.held = true; // one repaint below shows the new color
                else
                    pending = true;
            }
        }
        // held and not released: steady state — no animation needed
    }

    pushPoints();
    if (!pending)
        m_animTimer.stop(); // only held touches left: freeze the timer
}
