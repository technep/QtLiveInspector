#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonObject>

// Monitors application performance metrics and main thread responsiveness.
class PerfMonitor : public QObject
{
    Q_OBJECT
public:
    explicit PerfMonitor(QObject *parent = nullptr);
    ~PerfMonitor() override;

    void start();
    void stop();

    // Returns performance metrics JSON
    QJsonObject getStats();

    // Check if main thread event loop is idle / responsive
    bool isIdle(double maxDelayMs = 25.0);

private slots:
    void measurePing();

private:
    QTimer m_pingTimer;
    QElapsedTimer m_elapsed;
    qint64 m_lastPingTime = 0;
    double m_eventLoopDelayMs = 0.0;
    int m_frameCount = 0;
    double m_fps = 60.0;
    QElapsedTimer m_fpsTimer;
};
