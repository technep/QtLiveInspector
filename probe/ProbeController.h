#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

class QTcpServer;
class QTcpSocket;
class Highlighter;
class Picker;
class NotifyRelay;

// Runs inside the target application (GUI thread) and serves the
// newline-delimited JSON protocol described in protocol.txt.
class ProbeController : public QObject
{
    Q_OBJECT
public:
    // Entry point for all injection methods. Safe to call multiple times and
    // before QCoreApplication exists (waits for the application object).
    static void boot();

    static ProbeController *instance();

    // Start the TCP server. Must be called in the GUI thread.
    void start();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    explicit ProbeController(QObject *parent = nullptr);
    ~ProbeController() override;

    void handleRequest(QTcpSocket *client, const QJsonObject &req);
    // Resolve the target object of a request: either {"node": <id>} or
    // {"objectName": "<name>"} (unique match required).
    QObject *resolveObject(const QJsonObject &req, QString *error);
    bool invokeMethod(QObject *obj, const QString &method, const QJsonArray &args,
                      QString *error);
    bool clickObject(QObject *obj, QString *error);
    void send(QTcpSocket *client, const QJsonObject &obj);
    void broadcastEvent(const QJsonObject &ev);
    void emitPropertyChanged(QObject *obj, const QString &propertyName);

    static ProbeController *s_instance;

    QTcpServer *m_server = nullptr;
    QList<QTcpSocket *> m_clients;
    QHash<QTcpSocket *, QByteArray> m_recvBuffers;
    Highlighter *m_highlighter = nullptr;
    Picker *m_picker = nullptr;
    NotifyRelay *m_relay = nullptr;
    QString m_token;
    bool m_started = false;
};
