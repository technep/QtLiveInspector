#include "PerfMonitor.h"
#include "ObjectRegistry.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>
#include <QWidget>

PerfMonitor::PerfMonitor(QObject *parent) : QObject(parent)
{
    m_pingTimer.setInterval(100); // Check loop delay every 100ms
    connect(&m_pingTimer, &QTimer::timeout, this, &PerfMonitor::measurePing);
}

PerfMonitor::~PerfMonitor()
{
    stop();
}

void PerfMonitor::start()
{
    m_elapsed.start();
    m_fpsTimer.start();
    m_lastPingTime = m_elapsed.elapsed();
    m_pingTimer.start();
}

void PerfMonitor::stop()
{
    m_pingTimer.stop();
}

void PerfMonitor::measurePing()
{
    const qint64 now = m_elapsed.elapsed();
    const qint64 delta = now - m_lastPingTime;
    m_lastPingTime = now;

    // Expected interval is 100ms. Any excess time indicates main thread delay.
    const double delay = double(delta - 100);
    m_eventLoopDelayMs = (m_eventLoopDelayMs * 0.7) + (qMax(0.0, delay) * 0.3);

    m_frameCount++;
    if (m_fpsTimer.elapsed() >= 1000) {
        m_fps = (double(m_frameCount) * 1000.0) / double(m_fpsTimer.elapsed());
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

QJsonObject PerfMonitor::getStats()
{
    QJsonObject obj;
    obj[QStringLiteral("fps")] = m_fps;
    obj[QStringLiteral("eventLoopDelayMs")] = m_eventLoopDelayMs;
    obj[QStringLiteral("qobjectCount")] = double(ObjectRegistry::instance().idForObject(QCoreApplication::instance())); // triggers count/prune
    obj[QStringLiteral("windowCount")] = QGuiApplication::topLevelWindows().size();
    obj[QStringLiteral("isIdle")] = isIdle();
    return obj;
}

bool PerfMonitor::isIdle(double maxDelayMs)
{
    return m_eventLoopDelayMs <= maxDelayMs;
}
