#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>

#include <functional>

// Client side of the newline-delimited JSON protocol. Commands carry a
// monotonically increasing id; responses are dispatched to their callback.
// Server-push messages arrive via the eventReceived() signal.
class RemoteConnection : public QObject
{
    Q_OBJECT
public:
    explicit RemoteConnection(QObject *parent = nullptr);

    void open(const QString &host, quint16 port, const QString &token);
    void close();
    bool isOpen() const;

    using Callback = std::function<void(const QJsonObject &)>;
    qint64 command(const QString &cmd, const QJsonObject &args, Callback cb);

signals:
    void socketConnected();
    void socketDisconnected();
    void socketError(const QString &message);
    void eventReceived(const QJsonObject &ev);

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    QString m_token;
    qint64 m_nextId = 1;
    QHash<qint64, Callback> m_pending;
};
