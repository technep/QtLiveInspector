#include "RemoteConnection.h"

#include <QJsonDocument>

RemoteConnection::RemoteConnection(QObject *parent) : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &RemoteConnection::socketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RemoteConnection::socketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RemoteConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit socketError(m_socket->errorString());
    });
}

void RemoteConnection::open(const QString &host, quint16 port, const QString &token)
{
    close();
    m_token = token;
    m_socket->connectToHost(host, port);
}

void RemoteConnection::close()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
    m_pending.clear();
    m_buffer.clear();
}

bool RemoteConnection::isOpen() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

qint64 RemoteConnection::command(const QString &cmd, const QJsonObject &args, Callback cb)
{
    if (!isOpen())
        return -1;
    QJsonObject req = args;
    const qint64 id = m_nextId++;
    req[QStringLiteral("cmd")] = cmd;
    req[QStringLiteral("id")] = double(id);
    if (!m_token.isEmpty())
        req[QStringLiteral("token")] = m_token;
    m_pending.insert(id, std::move(cb));
    m_socket->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    return id;
}

void RemoteConnection::onReadyRead()
{
    m_buffer += m_socket->readAll();
    int idx = -1;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.trimmed().isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        if (obj[QStringLiteral("type")].toString() == QLatin1String("event")) {
            emit eventReceived(obj);
            continue;
        }
        const qint64 id = obj[QStringLiteral("id")].toInteger();
        auto it = m_pending.find(id);
        if (it != m_pending.end()) {
            Callback cb = std::move(it.value());
            m_pending.erase(it);
            if (cb)
                cb(obj);
        }
    }
}
