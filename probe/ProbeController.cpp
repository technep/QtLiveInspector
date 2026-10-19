#include "ProbeController.h"

#include "Highlighter.h"
#include "JsonUtil.h"
#include "NotifyRelay.h"
#include "ObjectRegistry.h"
#include "ObjectTree.h"
#include "Picker.h"
#include "SquishLocator.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QWidget>
#include <QWindow>

#include <chrono>
#include <mutex>
#include <thread>

#ifndef QLI_PROBE_VERSION
#define QLI_PROBE_VERSION "dev"
#endif

ProbeController *ProbeController::s_instance = nullptr;

ProbeController *ProbeController::instance()
{
    if (!s_instance)
        s_instance = new ProbeController(QCoreApplication::instance());
    return s_instance;
}

void ProbeController::boot()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // The constructor of an LD_PRELOADed library runs before main(), so
        // wait on a helper thread until the QCoreApplication exists, then
        // continue in the GUI thread.
        std::thread([] {
            while (!QCoreApplication::instance())
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            QMetaObject::invokeMethod(
                QCoreApplication::instance(), [] { instance()->start(); },
                Qt::QueuedConnection);
        }).detach();
    });
}

ProbeController::ProbeController(QObject *parent) : QObject(parent) { }

ProbeController::~ProbeController() = default;

void ProbeController::start()
{
    if (m_started)
        return;
    m_started = true;

    quint16 port = 17017;
    bool ok = false;
    const int envPort = qEnvironmentVariableIntValue("QLI_PORT", &ok);
    if (ok && envPort > 0 && envPort <= 65535)
        port = quint16(envPort);
    m_token = QString::fromUtf8(qgetenv("QLI_TOKEN"));

    m_highlighter = new Highlighter(this);
    m_picker = new Picker(this);
    m_relay = new NotifyRelay; // no parent: no Q_OBJECT, plain member lifetime
    m_relay->callback = [this](QObject *obj, const QString &propName) {
        if (QThread::currentThread() != thread()) {
            // Marshal cross-thread notifies back into the GUI thread.
            QPointer<QObject> guard(obj);
            QMetaObject::invokeMethod(
                this,
                [this, guard, propName] {
                    if (guard)
                        emitPropertyChanged(guard.data(), propName);
                },
                Qt::QueuedConnection);
            return;
        }
        emitPropertyChanged(obj, propName);
    };

    connect(m_picker, &Picker::picked, this, [this](QObject *obj) {
        m_highlighter->setTarget(obj);
        ObjectRegistry::instance().idForObject(obj); // ensure id
        QJsonObject ev;
        ev[QStringLiteral("type")] = QStringLiteral("event");
        ev[QStringLiteral("event")] = QStringLiteral("picked");
        ev[QStringLiteral("node")] =
            double(ObjectRegistry::instance().idForObject(obj));
        ev[QStringLiteral("path")] = ObjectTree::pathJson(obj);
        broadcastEvent(ev);
    });
    connect(m_picker, &Picker::stopped, this, [this] {
        QJsonObject ev;
        ev[QStringLiteral("type")] = QStringLiteral("event");
        ev[QStringLiteral("event")] = QStringLiteral("pickerStopped");
        broadcastEvent(ev);
    });

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ProbeController::onNewConnection);

    // Listen on all interfaces so remote machines can connect.
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning("[QtInspector] failed to listen on port %u: %s", port,
                 qPrintable(m_server->errorString()));
        return;
    }
    qInfo("[QtInspector] probe active, listening on port %u (pid %lld)%s", port,
          QCoreApplication::applicationPid(),
          m_token.isEmpty() ? "" : " [token required]");
}

void ProbeController::onNewConnection()
{
    while (QTcpSocket *sock = m_server->nextPendingConnection()) {
        m_clients.append(sock);
        connect(sock, &QTcpSocket::readyRead, this, &ProbeController::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &ProbeController::onSocketDisconnected);
        qInfo("[QtInspector] client connected from %s",
              qPrintable(sock->peerAddress().toString()));
    }
}

void ProbeController::onSocketDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;
    m_clients.removeAll(sock);
    m_recvBuffers.remove(sock);
    sock->deleteLater();
}

void ProbeController::send(QTcpSocket *client, const QJsonObject &obj)
{
    if (client && client->state() == QAbstractSocket::ConnectedState) {
        client->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
        client->flush();
    }
}

void ProbeController::broadcastEvent(const QJsonObject &ev)
{
    const QByteArray line = QJsonDocument(ev).toJson(QJsonDocument::Compact) + '\n';
    for (QTcpSocket *c : std::as_const(m_clients)) {
        if (c->state() == QAbstractSocket::ConnectedState)
            c->write(line);
    }
}

void ProbeController::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock)
        return;

    QByteArray &buffer = m_recvBuffers[sock];
    buffer += sock->readAll();

    int idx = -1;
    while ((idx = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(idx);
        buffer.remove(0, idx + 1);
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject res;
            res[QStringLiteral("id")] = 0.0;
            res[QStringLiteral("type")] = QStringLiteral("response");
            res[QStringLiteral("ok")] = false;
            res[QStringLiteral("error")] = QStringLiteral("malformed JSON");
            send(sock, res);
            continue;
        }
        handleRequest(sock, doc.object());
    }
}

QObject *ProbeController::resolveObject(const QJsonObject &req, QString *error)
{
    auto &reg = ObjectRegistry::instance();

    if (req.contains(QStringLiteral("node"))) {
        QObject *obj = reg.objectForId(req[QStringLiteral("node")].toInteger());
        if (!obj)
            *error = QStringLiteral("object no longer exists");
        return obj;
    }

    const QString name = req[QStringLiteral("objectName")].toString();
    if (name.isEmpty()) {
        *error = QStringLiteral("request needs 'node' or 'objectName'");
        return nullptr;
    }

    QList<QObject *> roots;
    roots << QCoreApplication::instance();
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows)
        roots << w;
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets)
        roots << w;

    QList<QObject *> matches;
    QSet<QObject *> visited;
    for (QObject *root : std::as_const(roots)) {
        if (!root || visited.contains(root))
            continue;
        visited.insert(root);
        if (root->objectName() == name)
            matches << root;
        matches.append(root->findChildren<QObject *>(name));
    }

    if (matches.isEmpty()) {
        *error = QStringLiteral("no object named '%1'").arg(name);
        return nullptr;
    }
    if (matches.size() > 1) {
        QStringList ids;
        for (QObject *m : std::as_const(matches))
            ids << QStringLiteral("%1 (%2)")
                       .arg(reg.idForObject(m))
                       .arg(QString::fromUtf8(m->metaObject()->className()));
        *error = QStringLiteral("objectName '%1' is ambiguous, %2 matches: [%3] "
                                "— address it by \"node\" id instead")
                     .arg(name)
                     .arg(matches.size())
                     .arg(ids.join(QStringLiteral(", ")));
        return nullptr;
    }
    return matches.first();
}

void ProbeController::handleRequest(QTcpSocket *client, const QJsonObject &req)
{
    const qint64 id = req[QStringLiteral("id")].toInteger();
    const QString cmd = req[QStringLiteral("cmd")].toString();

    QJsonObject res;
    res[QStringLiteral("id")] = double(id);
    res[QStringLiteral("type")] = QStringLiteral("response");

    auto reply = [&] {
        res[QStringLiteral("ok")] = true;
        send(client, res);
    };
    auto fail = [&](const QString &msg) {
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("error")] = msg;
        send(client, res);
    };

    if (!m_token.isEmpty()
        && req[QStringLiteral("token")].toString() != m_token) {
        fail(QStringLiteral("unauthorized: invalid or missing token"));
        return;
    }

    auto &reg = ObjectRegistry::instance();

    if (cmd == QLatin1String("hello")) {
        res[QStringLiteral("app")] = QCoreApplication::applicationName().isEmpty()
            ? QCoreApplication::applicationFilePath()
            : QCoreApplication::applicationName();
        res[QStringLiteral("qt")] = QString::fromUtf8(qVersion());
        res[QStringLiteral("pid")] = double(QCoreApplication::applicationPid());
        res[QStringLiteral("probeVersion")] = QStringLiteral(QLI_PROBE_VERSION);
        reply();
    } else if (cmd == QLatin1String("tree")) {
        reg.prune();
        res[QStringLiteral("root")] = ObjectTree::fullTree();
        reply();
    } else if (cmd == QLatin1String("properties")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        res[QStringLiteral("properties")] = JsonUtil::propertiesJson(obj);
        reply();
    } else if (cmd == QLatin1String("setProperty")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        const QString name = req[QStringLiteral("name")].toString();
        if (!JsonUtil::setPropertyFromJson(obj, name, req[QStringLiteral("value")], &error)) {
            fail(error);
            return;
        }
        const QByteArray pn = name.toUtf8();
        const int pidx = obj->metaObject()->indexOfProperty(pn.constData());
        if (pidx >= 0)
            res[QStringLiteral("value")] =
                JsonUtil::propertyValueJson(obj, obj->metaObject()->property(pidx));
        else
            res[QStringLiteral("value")] =
                JsonUtil::variantToJson(obj->property(pn.constData()));
        reply();
    } else if (cmd == QLatin1String("locator")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        res[QStringLiteral("locator")] = SquishLocator::locatorFor(obj);
        QJsonArray chain;
        const QStringList parts = SquishLocator::chainFor(obj);
        for (const QString &p : parts)
            chain.append(p);
        res[QStringLiteral("chain")] = chain;
        res[QStringLiteral("path")] = ObjectTree::pathJson(obj);
        reply();
    } else if (cmd == QLatin1String("highlight")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        m_highlighter->setTarget(obj);
        reply();
    } else if (cmd == QLatin1String("clearHighlight")) {
        m_highlighter->clear();
        reply();
    } else if (cmd == QLatin1String("watch")) {
        m_relay->clear();
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        int count = 0;
        const QMetaObject *mo = obj->metaObject();
        for (int i = 0; i < mo->propertyCount(); ++i) {
            const QMetaProperty prop = mo->property(i);
            if (prop.hasNotifySignal() && m_relay->addWatch(obj, prop) >= 0)
                ++count;
        }
        res[QStringLiteral("watching")] = count;
        reply();
    } else if (cmd == QLatin1String("unwatch")) {
        m_relay->clear();
        reply();
    } else if (cmd == QLatin1String("pickStart")) {
        m_picker->start();
        reply();
    } else if (cmd == QLatin1String("pickStop")) {
        m_picker->stop();
        reply();
    } else if (cmd == QLatin1String("invoke")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        if (!invokeMethod(obj, req[QStringLiteral("method")].toString(),
                          req[QStringLiteral("args")].toArray(), &error)) {
            fail(error);
            return;
        }
        reply();
    } else if (cmd == QLatin1String("click")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        if (!clickObject(obj, &error)) {
            fail(error);
            return;
        }
        reply();
    } else {
        fail(QStringLiteral("unknown command: %1").arg(cmd));
    }
}

bool ProbeController::invokeMethod(QObject *obj, const QString &method,
                                   const QJsonArray &args, QString *error)
{
    const QMetaObject *mo = obj->metaObject();
    const QByteArray name = method.toUtf8();
    const int argc = args.size();

    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.name() != name || m.parameterCount() != argc || argc > 6)
            continue;

        QVariant qargs[6];
        QGenericArgument gargs[6];
        bool convertible = true;
        for (int p = 0; p < argc; ++p) {
            const QMetaType pt = m.parameterMetaType(p);
            QVariant v = args.at(p).toVariant();
            if (v.metaType() != pt && !v.convert(pt)) {
                convertible = false;
                break;
            }
            qargs[p] = v;
            gargs[p] = QGenericArgument(pt.name(), qargs[p].constData());
        }
        if (!convertible)
            continue;

        bool ok = false;
        switch (argc) {
        case 0: ok = m.invoke(obj, Qt::DirectConnection); break;
        case 1: ok = m.invoke(obj, Qt::DirectConnection, gargs[0]); break;
        case 2: ok = m.invoke(obj, Qt::DirectConnection, gargs[0], gargs[1]); break;
        case 3: ok = m.invoke(obj, Qt::DirectConnection, gargs[0], gargs[1], gargs[2]); break;
        case 4: ok = m.invoke(obj, Qt::DirectConnection, gargs[0], gargs[1], gargs[2], gargs[3]); break;
        case 5: ok = m.invoke(obj, Qt::DirectConnection, gargs[0], gargs[1], gargs[2], gargs[3], gargs[4]); break;
        case 6: ok = m.invoke(obj, Qt::DirectConnection, gargs[0], gargs[1], gargs[2], gargs[3], gargs[4], gargs[5]); break;
        }
        if (ok)
            return true;
        *error = QStringLiteral("invoking %1 failed").arg(method);
        return false;
    }
    *error = QStringLiteral("no method '%1' with %2 parameter(s) on %3")
                 .arg(method)
                 .arg(argc)
                 .arg(QString::fromUtf8(mo->className()));
    return false;
}

bool ProbeController::clickObject(QObject *obj, QString *error)
{
    // Widgets: prefer the button's own animated click.
    if (auto *btn = qobject_cast<QAbstractButton *>(obj)) {
        btn->animateClick();
        return true;
    }

    // QML items: synthesize a mouse press+release at the item's center.
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        QQuickWindow *win = item->window();
        if (!win) {
            *error = QStringLiteral("item is not in a window");
            return false;
        }
        const QPointF scenePos = item->mapToScene(item->boundingRect().center());
        const QPoint globalPos = win->mapToGlobal(scenePos.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos, globalPos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos, globalPos,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &release);
        return true;
    }

    // Other widgets: synthesize a mouse press+release at the widget's center.
    if (auto *w = qobject_cast<QWidget *>(obj)) {
        const QPointF center = QRectF(w->rect()).center();
        const QPointF globalPos = w->mapToGlobal(center.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, center, globalPos,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, center, globalPos,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &release);
        return true;
    }

    *error = QStringLiteral("object of type %1 is not clickable")
                 .arg(QString::fromUtf8(obj->metaObject()->className()));
    return false;
}

void ProbeController::emitPropertyChanged(QObject *obj, const QString &propertyName)
{
    const QByteArray pn = propertyName.toUtf8();
    const int idx = obj->metaObject()->indexOfProperty(pn.constData());
    if (idx < 0)
        return;

    QJsonObject ev;
    ev[QStringLiteral("type")] = QStringLiteral("event");
    ev[QStringLiteral("event")] = QStringLiteral("propertyChanged");
    ev[QStringLiteral("node")] = double(ObjectRegistry::instance().idForObject(obj));
    ev[QStringLiteral("property")] = propertyName;
    ev[QStringLiteral("value")] =
        JsonUtil::propertyValueJson(obj, obj->metaObject()->property(idx));
    broadcastEvent(ev);
}
