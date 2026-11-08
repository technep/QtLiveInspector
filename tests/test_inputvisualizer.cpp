// test_inputvisualizer.cpp — deterministic tests for the press / press-and-
// hold / release state machine of InputVisualizer.
//
// Events are fed straight through QCoreApplication::sendEvent(), which
// passes them by the application-wide event filter installed by
// setEnabled(true) — exactly the path real mouse/touch input takes, but
// without needing a window, a display server, or real input hardware.
// Runs offscreen; part of the standard ctest suite.

#include <QApplication>
#include <QInputDevice>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QTest>

// Test-only access to internals (m_points, m_animTimer). All of
// InputVisualizer.h's own includes are #pragma once, so the macro cannot
// corrupt Qt headers here.
#define private public
#include "InputVisualizer.h"
#undef private

static QMouseEvent mouseEvent(QEvent::Type type, const QPointF &global,
                              Qt::MouseButton button = Qt::LeftButton,
                              Qt::MouseButtons buttons = Qt::LeftButton)
{
    return QMouseEvent(type, global, global, global.toPoint(), button,
                       buttons, Qt::NoModifier);
}

template <typename EV>
static void send(EV ev) // by value: sendEvent needs a non-temporary
{
    QCoreApplication::sendEvent(QCoreApplication::instance(), &ev);
}

static void send(QTouchEvent *ev) // QTouchEvent copy ctor is protected
{
    QCoreApplication::sendEvent(QCoreApplication::instance(), ev);
}

class TestInputVisualizer : public QObject
{
    Q_OBJECT

private slots:
    void pressHoldRelease()
    {
        InputVisualizer viz;
        viz.setEnabled(true);
        QVERIFY(viz.isEnabled());

        // -- press: one tracked point, popping in from radius 0 --------------
        send(mouseEvent(QEvent::MouseButtonPress, {100, 100}));
        QCOMPARE(viz.m_points.size(), 1);
        QCOMPARE(viz.m_points[0].pos, QPointF(100, 100));
        QVERIFY(viz.m_points[0].radius < 12.0); // pop-in started
        QVERIFY(!viz.m_points[0].held);
        QVERIFY(!viz.m_points[0].released);
        QVERIFY(viz.m_animTimer.isActive()); // animating the pop-in

        // pop-in completes at the base radius
        QTRY_VERIFY_WITH_TIMEOUT(viz.m_points[0].radius == 12.0, 2000);

        // -- press-and-hold: after the threshold the point turns held -------
        QTRY_VERIFY_WITH_TIMEOUT(viz.m_points[0].held, 2000);
        // steady state: timer stops again once only held points remain
        QTRY_VERIFY_WITH_TIMEOUT(!viz.m_animTimer.isActive(), 2000);

        // -- release: released flag set, ripple animates out, then gone -----
        send(mouseEvent(QEvent::MouseButtonRelease, {100, 100}, Qt::LeftButton,
                        Qt::NoButton));
        QCOMPARE(viz.m_points.size(), 1);
        QVERIFY(viz.m_points[0].released);
        QVERIFY(viz.m_animTimer.isActive());
        QTRY_VERIFY_WITH_TIMEOUT(viz.m_points.isEmpty(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(!viz.m_animTimer.isActive(), 2000);
    }

    void dragMovesTheCircleWithoutTrail()
    {
        InputVisualizer viz;
        viz.setEnabled(true);

        send(mouseEvent(QEvent::MouseButtonPress, {50, 50}));
        // a drag = move events with the button held: the SAME circle relocates
        for (int i = 1; i <= 5; ++i)
            send(mouseEvent(QEvent::MouseMove, {50.0 + 10 * i, 50}, Qt::NoButton,
                            Qt::LeftButton));
        QCOMPARE(viz.m_points.size(), 1); // no trail, no extra circles
        QCOMPARE(viz.m_points[0].pos, QPointF(100, 50));

        // plain hover (no buttons) must be ignored entirely
        send(mouseEvent(QEvent::MouseMove, {500, 500}, Qt::NoButton,
                        Qt::NoButton));
        QCOMPARE(viz.m_points.size(), 1);
        QCOMPARE(viz.m_points[0].pos, QPointF(100, 50));

        send(mouseEvent(QEvent::MouseButtonRelease, {100, 50}, Qt::LeftButton,
                        Qt::NoButton));
        QTRY_VERIFY_WITH_TIMEOUT(viz.m_points.isEmpty(), 2000);
    }

    void firstTouchOnlyByDefault()
    {
        InputVisualizer viz;
        viz.setEnabled(true);
        QVERIFY(!viz.isMultiTouch());

        QTouchEvent begin(QEvent::TouchBegin,
                          QPointingDevice::primaryPointingDevice(),
                          Qt::NoModifier,
                          {QEventPoint(7, QEventPoint::Pressed,
                                       QPointF(10, 10), QPointF(10, 10))});
        send(&begin);
        QCOMPARE(viz.m_points.size(), 1);
        QCOMPARE(viz.m_points[0].id, 7);

        // second finger down while the first is active: ignored entirely
        QTouchEvent second(QEvent::TouchUpdate,
                           QPointingDevice::primaryPointingDevice(),
                           Qt::NoModifier,
                           {QEventPoint(7, QEventPoint::Stationary,
                                        QPointF(10, 10), QPointF(10, 10)),
                            QEventPoint(8, QEventPoint::Pressed,
                                        QPointF(50, 50), QPointF(50, 50))});
        send(&second);
        QCOMPARE(viz.m_points.size(), 1);
        QCOMPARE(viz.m_points[0].id, 7); // still the first touch

        // first finger lifts; a later press of another id becomes tracked
        QTouchEvent end(QEvent::TouchEnd,
                        QPointingDevice::primaryPointingDevice(),
                        Qt::NoModifier,
                        {QEventPoint(7, QEventPoint::Released,
                                     QPointF(10, 10), QPointF(10, 10))});
        send(&end);
        QTouchEvent later(QEvent::TouchBegin,
                          QPointingDevice::primaryPointingDevice(),
                          Qt::NoModifier,
                          {QEventPoint(8, QEventPoint::Pressed,
                                       QPointF(60, 60), QPointF(60, 60))});
        send(&later);
        QCOMPARE(viz.m_points.size(), 2); // old one released + one new
        QVERIFY(viz.m_points[0].released);
        QCOMPARE(viz.m_points[1].id, 8);
        QVERIFY(!viz.m_points[1].released);
    }

    void multiTouchTracksEachFinger()
    {
        InputVisualizer viz;
        viz.setEnabled(true);
        viz.setMultiTouch(true);
        QVERIFY(viz.isMultiTouch());

        QTouchEvent begin(QEvent::TouchBegin,
                          QPointingDevice::primaryPointingDevice(),
                          Qt::NoModifier,
                          {QEventPoint(1, QEventPoint::Pressed,
                                       QPointF(10, 10), QPointF(10, 10)),
                           QEventPoint(2, QEventPoint::Pressed,
                                       QPointF(20, 20), QPointF(20, 20))});
        send(&begin);
        QCOMPARE(viz.m_points.size(), 2);

        // finger 1 moves, finger 2 stays
        QTouchEvent update(QEvent::TouchUpdate,
                           QPointingDevice::primaryPointingDevice(),
                           Qt::NoModifier,
                           {QEventPoint(1, QEventPoint::Updated,
                                        QPointF(30, 10), QPointF(30, 10)),
                            QEventPoint(2, QEventPoint::Stationary,
                                        QPointF(20, 20), QPointF(20, 20))});
        send(&update);
        QCOMPARE(viz.m_points.size(), 2);
        QCOMPARE(viz.m_points[0].id, 1);
        QCOMPARE(viz.m_points[0].pos, QPointF(30, 10));
        QCOMPARE(viz.m_points[1].id, 2);

        // finger 1 lifts: only that point releases
        QTouchEvent partial(QEvent::TouchUpdate,
                            QPointingDevice::primaryPointingDevice(),
                            Qt::NoModifier,
                            {QEventPoint(1, QEventPoint::Released,
                                         QPointF(30, 10), QPointF(30, 10))});
        send(&partial);
        QCOMPARE(viz.m_points.size(), 2);
        QVERIFY(viz.m_points[0].released);
        QVERIFY(!viz.m_points[1].released);
    }

    void disableClearsEverything()
    {
        InputVisualizer viz;
        viz.setEnabled(true);
        send(mouseEvent(QEvent::MouseButtonPress, {1, 2}));
        QCOMPARE(viz.m_points.size(), 1);
        viz.setEnabled(false);
        QVERIFY(viz.m_points.isEmpty());
        QVERIFY(!viz.m_animTimer.isActive());
    }

    void monkeyModeToggle()
    {
        InputVisualizer viz;
        QVERIFY(!viz.isMonkeyMode());
        viz.setMonkeyMode(true);
        QVERIFY(viz.isMonkeyMode());
        QVERIFY(!viz.m_monkeyImage.isNull()); // icon generated lazily

        // state machine is unaffected by the styling mode
        viz.setEnabled(true);
        send(mouseEvent(QEvent::MouseButtonPress, {50, 50}));
        QCOMPARE(viz.m_points.size(), 1);
        send(mouseEvent(QEvent::MouseButtonRelease, {50, 50}));
        QVERIFY(viz.m_points[0].released);

        viz.setMonkeyMode(false);
        QVERIFY(!viz.isMonkeyMode());
        QVERIFY(viz.m_monkeyImage.isNull()
               || !viz.m_widgetOverlay); // circles again on next paint
    }
};

QTEST_MAIN(TestInputVisualizer) // QApplication (Widgets is linked)
#include "test_inputvisualizer.moc"
