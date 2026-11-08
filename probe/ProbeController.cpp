#include "ProbeController.h"

#include "Highlighter.h"
#include "JsonUtil.h"
#include "NotifyRelay.h"
#include "ObjectRegistry.h"
#include "ObjectTree.h"
#include "Picker.h"
#include "SquishLocator.h"
#include "InputVisualizer.h"
#include "PerfMonitor.h"

#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QWidget>
#include <QWindow>

#include <chrono>
#include <exception>
#include <mutex>
#include <thread>

#ifndef QLI_PROBE_VERSION
#define QLI_PROBE_VERSION "dev"
#endif

// Wire-protocol version, negotiated in the "hello" handshake. Bump the major
// component for incompatible changes; clients with a different major version
// are rejected with an explicit error instead of failing mysteriously.
// 1.1: added mouseDown / mouseUp / mouseMove.
#define QLI_PROTOCOL_VERSION "1.1"

namespace {

// Commands that only observe the target. In read-only mode (QLI_READONLY=1)
// everything else is rejected before it can touch the application.
bool isReadOnlyCommand(const QString &cmd)
{
    return cmd == QLatin1String("hello")
        || cmd == QLatin1String("tree")
        || cmd == QLatin1String("properties")
        || cmd == QLatin1String("perfStats")
        || cmd == QLatin1String("isIdle")
        || cmd == QLatin1String("screenshot")
        || cmd == QLatin1String("locator");
}

// Center of an item/widget in GLOBAL coordinates, plus an optional offset
// in item/widget coordinates. Uses the app's own transform chain, which is
// the only "global" frame the synthesized events can address reliably
// (external frames may disagree, e.g. under XWayland).
bool objectGlobalPos(QObject *obj, const QPointF &offset, QPointF *global,
                     QString *error)
{
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        QQuickWindow *win = item->window();
        if (!win) {
            *error = QStringLiteral("item is not in a window");
            return false;
        }
        const QPointF scenePos =
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2) + offset);
        *global = win->mapToGlobal(scenePos.toPoint());
        return true;
    }
    if (auto *w = qobject_cast<QWidget *>(obj)) {
        *global = w->mapToGlobal((QRectF(w->rect()).center() + offset).toPoint());
        return true;
    }
    *error = QStringLiteral("object of type %1 cannot receive mouse input")
                 .arg(QString::fromUtf8(obj->metaObject()->className()));
    return false;
}

} // namespace

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

    // Bind address: loopback by default. Exposing the probe on the network
    // requires an explicit opt-in via QLI_HOST ("any" or a specific address).
    QHostAddress bindAddress = QHostAddress::LocalHost;
    const QByteArray envHost = qgetenv("QLI_HOST");
    if (envHost == "any" || envHost == "0.0.0.0") {
        bindAddress = QHostAddress::Any;
    } else if (!envHost.isEmpty()) {
        const QHostAddress parsed(QString::fromUtf8(envHost));
        if (!parsed.isNull())
            bindAddress = parsed;
    }

    // Token: fail-safe. An explicitly set QLI_TOKEN wins (an explicitly empty
    // value selects development no-auth mode); when unset we generate a
    // random token and print it once to the application log.
    if (qEnvironmentVariableIsSet("QLI_TOKEN")) {
        m_token = QString::fromUtf8(qgetenv("QLI_TOKEN"));
    } else {
        QByteArray raw(16, Qt::Uninitialized);
        for (int i = 0; i < raw.size(); ++i)
            raw[i] = char(QRandomGenerator::global()->generate() & 0xFF);
        m_token = QString::fromLatin1(raw.toHex());
    }
    m_readOnly = qgetenv("QLI_READONLY") == QByteArray("1");

    m_highlighter = new Highlighter(this);
    m_visualizer = new InputVisualizer(this);
    m_visualizer->setMultiTouch(
        qgetenv("QLI_INPUT_VIZ_MULTITOUCH") == QByteArray("1"));
    m_visualizer->setMonkeyMode(
        qgetenv("QLI_INPUT_VIZ_MONKEY") == QByteArray("1"));
    // Startup opt-in for the overlay, so the probe honors the same
    // QLI_INPUT_VIZ contract as the standalone inputviz library (where
    // the default is on and "0" disables). The probe stays fail-safe:
    // off unless explicitly requested.
    m_visualizer->setEnabled(qgetenv("QLI_INPUT_VIZ") == QByteArray("1"));
    m_perfMonitor = new PerfMonitor(this);
    m_perfMonitor->start();
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

    if (!m_server->listen(bindAddress, port)) {
        qWarning("[QtInspector] failed to listen on %s:%u: %s",
                 qPrintable(bindAddress.toString()), port,
                 qPrintable(m_server->errorString()));
        return;
    }
    qInfo("[QtInspector] probe active, listening on %s:%u (pid %lld)%s%s",
          qPrintable(bindAddress.toString()), port,
          QCoreApplication::applicationPid(),
          m_token.isEmpty() ? " [WARNING: no auth — development only]"
                            : " [token required]",
          m_readOnly ? " [read-only]" : "");
    if (!m_token.isEmpty())
        qInfo("[QtInspector] auth token: %s", qPrintable(m_token));
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

    // Cap the per-client buffer so a peer that never sends a newline (or
    // claims a huge message) cannot grow memory without bound.
    constexpr int kMaxMessageBytes = 8 * 1024 * 1024; // 8 MB
    QByteArray &buffer = m_recvBuffers[sock];
    buffer += sock->readAll();
    if (buffer.size() > kMaxMessageBytes) {
        qWarning("[QtInspector] client %s exceeded the %d-byte message limit "
                 "— disconnecting", qPrintable(sock->peerAddress().toString()),
                 kMaxMessageBytes);
        sock->disconnectFromHost();
        return;
    }

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
        // The probe lives inside a customer process: a bug must never take the
        // host application down, so every request is handled behind a guard.
        try {
            handleRequest(sock, doc.object());
        } catch (const std::exception &e) {
            QJsonObject res;
            res[QStringLiteral("id")] = doc.object()[QStringLiteral("id")].toDouble();
            res[QStringLiteral("type")] = QStringLiteral("response");
            res[QStringLiteral("ok")] = false;
            res[QStringLiteral("error")] =
                QStringLiteral("internal probe error: %1").arg(QString::fromUtf8(e.what()));
            send(sock, res);
            qWarning("[QtInspector] exception while handling '%s': %s",
                     qPrintable(doc.object()[QStringLiteral("cmd")].toString()),
                     e.what());
        } catch (...) {
            QJsonObject res;
            res[QStringLiteral("id")] = doc.object()[QStringLiteral("id")].toDouble();
            res[QStringLiteral("type")] = QStringLiteral("response");
            res[QStringLiteral("ok")] = false;
            res[QStringLiteral("error")] = QStringLiteral("internal probe error");
            send(sock, res);
            qWarning("[QtInspector] unknown exception while handling '%s'",
                     qPrintable(doc.object()[QStringLiteral("cmd")].toString()));
        }
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

    // Walk both QObject and visual-item parenthood: popup/dropdown content
    // is frequently attached to the scene via parentItem only, which
    // findChildren<>() cannot see.
    QList<QObject *> matches;
    const QList<QObject *> all = ObjectTree::allObjects();
    for (QObject *o : all) {
        if (o->objectName() == name)
            matches << o;
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

    if (m_readOnly && !isReadOnlyCommand(cmd)) {
        fail(QStringLiteral("read-only mode: command '%1' is not permitted")
                 .arg(cmd));
        return;
    }

    // Audit trail for anything that can change the target application.
    if (!isReadOnlyCommand(cmd)) {
        QString target;
        if (req.contains(QStringLiteral("node")))
            target = QStringLiteral(" node=%1")
                         .arg(req[QStringLiteral("node")].toInteger());
        else if (req.contains(QStringLiteral("objectName")))
            target = QStringLiteral(" objectName='%1'")
                         .arg(req[QStringLiteral("objectName")].toString());
        qInfo("[QtInspector] AUDIT %s cmd=%s%s",
              qPrintable(client->peerAddress().toString()), qPrintable(cmd),
              qPrintable(target));
    }

    auto &reg = ObjectRegistry::instance();

    if (cmd == QLatin1String("hello")) {
        res[QStringLiteral("app")] = QCoreApplication::applicationName().isEmpty()
            ? QCoreApplication::applicationFilePath()
            : QCoreApplication::applicationName();
        res[QStringLiteral("qt")] = QString::fromUtf8(qVersion());
        res[QStringLiteral("pid")] = double(QCoreApplication::applicationPid());
        res[QStringLiteral("probeVersion")] = QStringLiteral(QLI_PROBE_VERSION);
        res[QStringLiteral("protocolVersion")] = QStringLiteral(QLI_PROTOCOL_VERSION);
        // Version negotiation: a client that announces an incompatible major
        // version gets an explicit error instead of silent misbehavior.
        if (req.contains(QStringLiteral("protocolVersion"))) {
            const QString wanted = req[QStringLiteral("protocolVersion")].toString();
            const QString wantedMajor = wanted.section(QLatin1Char('.'), 0, 0);
            const QString ourMajor =
                QStringLiteral(QLI_PROTOCOL_VERSION).section(QLatin1Char('.'), 0, 0);
            if (!wantedMajor.isEmpty() && wantedMajor != ourMajor) {
                res[QStringLiteral("ok")] = false;
                res[QStringLiteral("error")] = QStringLiteral(
                    "protocol version mismatch: client speaks %1, probe speaks %2")
                        .arg(wanted, QStringLiteral(QLI_PROTOCOL_VERSION));
                send(client, res);
                return;
            }
        }
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
    } else if (cmd == QLatin1String("mouseDown")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        if (!mousePress(obj, &error)) {
            fail(error);
            return;
        }
        reply();
    } else if (cmd == QLatin1String("mouseUp")) {
        QString error;
        QObject *obj = nullptr;
        if (req.contains(QStringLiteral("node"))
            || req.contains(QStringLiteral("objectName"))) {
            obj = resolveObject(req, &error);
            if (!obj) {
                fail(error);
                return;
            }
        } else {
            // No target: release where the last mouseDown happened.
            obj = m_lastPressTarget.data();
            if (!obj) {
                fail(QStringLiteral("no mouseUp target and no prior mouseDown"));
                return;
            }
        }
        if (!mouseRelease(obj, &error)) {
            fail(error);
            return;
        }
        reply();
    } else if (cmd == QLatin1String("mouseMove")) {
        QString error;
        QPointF globalPos;
        Qt::MouseButtons buttons = Qt::NoButton;
        const QString btnName =
            req[QStringLiteral("buttons")].toString().toLower();
        if (btnName.contains(QLatin1String("left")))
            buttons |= Qt::LeftButton;
        if (btnName.contains(QLatin1String("right")))
            buttons |= Qt::RightButton;
        if (btnName.contains(QLatin1String("middle")))
            buttons |= Qt::MiddleButton;

        if (req.contains(QStringLiteral("node"))
            || req.contains(QStringLiteral("objectName"))) {
            // Item-relative: target center plus (dx,dy) in item coordinates,
            // converted with the app's own transform chain — robust under
            // window systems where external "global" frames disagree with
            // Qt's (e.g. XWayland).
            QObject *obj = resolveObject(req, &error);
            if (!obj) {
                fail(error);
                return;
            }
            const QPointF offset(req[QStringLiteral("dx")].toDouble(),
                                 req[QStringLiteral("dy")].toDouble());
            if (!objectGlobalPos(obj, offset, &globalPos, &error)) {
                fail(error);
                return;
            }
        } else {
            globalPos = QPointF(req[QStringLiteral("x")].toDouble(),
                                req[QStringLiteral("y")].toDouble());
        }
        if (!moveMouse(globalPos, buttons, &error)) {
            fail(error);
            return;
        }
        reply();
    } else if (cmd == QLatin1String("typeText")) {
        QString error;
        QObject *obj = resolveObject(req, &error);
        if (!obj) {
            fail(error);
            return;
        }
        if (!typeText(obj, req[QStringLiteral("text")].toString(), &error)) {
            fail(error);
            return;
        }
        reply();
    } else if (cmd == QLatin1String("setInputVisualizer")) {
        // "enabled" absent = leave the current state untouched, so styling
        // commands like {monkey:true} don't force-enable the visualizer.
        const bool enabled = req.contains(QStringLiteral("enabled"))
            ? req[QStringLiteral("enabled")].toBool()
            : m_visualizer->isEnabled();
        m_visualizer->setEnabled(enabled);
        if (req.contains(QStringLiteral("multiTouch")))
            m_visualizer->setMultiTouch(req[QStringLiteral("multiTouch")].toBool());
        if (req.contains(QStringLiteral("monkey")))
            m_visualizer->setMonkeyMode(req[QStringLiteral("monkey")].toBool());
        res[QStringLiteral("enabled")] = m_visualizer->isEnabled();
        res[QStringLiteral("multiTouch")] = m_visualizer->isMultiTouch();
        res[QStringLiteral("monkey")] = m_visualizer->isMonkeyMode();
        reply();
    } else if (cmd == QLatin1String("perfStats")) {
        const QJsonObject stats = m_perfMonitor->getStats();
        for (auto it = stats.begin(); it != stats.end(); ++it)
            res[it.key()] = it.value();
        reply();
    } else if (cmd == QLatin1String("isIdle")) {
        const double maxDelay = req[QStringLiteral("maxDelayMs")].toDouble(25.0);
        res[QStringLiteral("idle")] = m_perfMonitor->isIdle(maxDelay);
        res[QStringLiteral("eventLoopDelayMs")] = m_perfMonitor->getStats()[QStringLiteral("eventLoopDelayMs")];
        reply();
    } else if (cmd == QLatin1String("screenshot")) {
        // Optional target: a window, an item (its window), or a widget.
        // Without a target, the first visible window/widget is grabbed.
        QObject *target = nullptr;
        if (req.contains(QStringLiteral("node"))
            || req.contains(QStringLiteral("objectName"))) {
            QString error;
            target = resolveObject(req, &error);
            if (!target) {
                fail(error);
                return;
            }
        }

        QQuickWindow *qw = qobject_cast<QQuickWindow *>(target);
        QWidget *wdg = qobject_cast<QWidget *>(target);
        if (!qw)
            if (auto *item = qobject_cast<QQuickItem *>(target))
                qw = item->window();

        if (!qw && !wdg) {
            const QWindowList windows = QGuiApplication::topLevelWindows();
            for (QWindow *w : windows) {
                if (auto *c = qobject_cast<QQuickWindow *>(w); c && c->isVisible()) {
                    qw = c;
                    break;
                }
            }
            if (!qw) {
                const QWidgetList tops = QApplication::topLevelWidgets();
                for (QWidget *w : tops) {
                    if (w->isVisible()) {
                        wdg = w;
                        break;
                    }
                }
            }
        }

        QImage img;
        if (qw)
            img = qw->grabWindow();
        else if (wdg)
            img = wdg->grab().toImage();
        if (img.isNull()) {
            fail(QStringLiteral("no visible window to grab"));
            return;
        }

        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        res[QStringLiteral("png")] = QString::fromLatin1(png.toBase64());
        res[QStringLiteral("width")] = img.width();
        res[QStringLiteral("height")] = img.height();
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

bool ProbeController::mousePress(QObject *obj, QString *error)
{
    const Qt::MouseButton button = Qt::LeftButton;
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        QQuickWindow *win = item->window();
        if (!win) {
            *error = QStringLiteral("item is not in a window");
            return false;
        }
        const QPointF scenePos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const QPoint globalPos = win->mapToGlobal(scenePos.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, scenePos, scenePos, globalPos,
                          button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &press);
        m_lastPressTarget = obj;
        m_lastPressPos = scenePos;
        return true;
    }
    if (auto *w = qobject_cast<QWidget *>(obj)) {
        const QPointF center = QRectF(w->rect()).center();
        const QPointF globalPos = w->mapToGlobal(center.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, center, globalPos,
                          button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &press);
        m_lastPressTarget = obj;
        m_lastPressPos = center;
        return true;
    }
    *error = QStringLiteral("object of type %1 cannot receive mouse input")
                 .arg(QString::fromUtf8(obj->metaObject()->className()));
    return false;
}

bool ProbeController::mouseRelease(QObject *obj, QString *error)
{
    const Qt::MouseButton button = Qt::LeftButton;
    if (auto *item = qobject_cast<QQuickItem *>(obj)) {
        QQuickWindow *win = item->window();
        if (!win) {
            *error = QStringLiteral("item is not in a window");
            return false;
        }
        const QPointF scenePos = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const QPoint globalPos = win->mapToGlobal(scenePos.toPoint());
        QMouseEvent release(QEvent::MouseButtonRelease, scenePos, scenePos,
                            globalPos, button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(win, &release);
        return true;
    }
    if (auto *w = qobject_cast<QWidget *>(obj)) {
        const QPointF center = QRectF(w->rect()).center();
        const QPointF globalPos = w->mapToGlobal(center.toPoint());
        QMouseEvent release(QEvent::MouseButtonRelease, center, globalPos,
                            button, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &release);
        return true;
    }
    *error = QStringLiteral("object of type %1 cannot receive mouse input")
                 .arg(QString::fromUtf8(obj->metaObject()->className()));
    return false;
}

bool ProbeController::moveMouse(const QPointF &globalPos, Qt::MouseButtons buttons,
                                QString *error)
{
    // Deliver to the visible top-level window under the point (falling back
    // to the first visible one) so moves work over popup windows too.
    QWindow *target = nullptr;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows) {
        if (w->isVisible() && w->geometry().contains(globalPos.toPoint())) {
            target = w;
            break;
        }
    }
    if (!target) {
        for (QWindow *w : windows) {
            if (w->isVisible()) {
                target = w;
                break;
            }
        }
    }
    auto *qw = qobject_cast<QQuickWindow *>(target);
    if (!qw) {
        *error = QStringLiteral("no visible window to move the mouse over");
        return false;
    }
    const QPointF scenePos = qw->mapFromGlobal(globalPos);
    QMouseEvent move(QEvent::MouseMove, scenePos, scenePos, globalPos.toPoint(),
                     Qt::NoButton, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(qw, &move);
    return true;
}

bool ProbeController::clickObject(QObject *obj, QString *error)
{
    // Widgets: prefer the button's own animated click.
    if (auto *btn = qobject_cast<QAbstractButton *>(obj)) {
        btn->animateClick();
        return true;
    }

    if (!mousePress(obj, error))
        return false;
    return mouseRelease(obj, error);
}

bool ProbeController::typeText(QObject *obj, const QString &text, QString *error)
{
    // Give the target keyboard focus, then synthesize key press/release pairs.
    auto *item = qobject_cast<QQuickItem *>(obj);
    auto *widget = qobject_cast<QWidget *>(obj);
    QQuickWindow *quickWin = nullptr;

    if (item) {
        item->forceActiveFocus();
        quickWin = item->window();
        if (!quickWin) {
            *error = QStringLiteral("item is not in a window");
            return false;
        }
    } else if (widget) {
        widget->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents(); // let the focus change settle
    } else {
        *error = QStringLiteral("object of type %1 cannot receive key input")
                     .arg(QString::fromUtf8(obj->metaObject()->className()));
        return false;
    }

    for (const QChar ch : text) {
        const ushort uc = ch.unicode();
        int key = Qt::Key_unknown;
        if (uc < 128)
            key = int(uc >= 'a' && uc <= 'z' ? uc - 32 : uc); // Qt::Key_A == 'A' …
        const QString t(ch);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, t);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, t);
        // QQuickWindow::sendEvent is private in Qt 6; send directly to the
        // item/widget — QQuickItem::event() handles key input the same way.
        QCoreApplication::sendEvent(item ? static_cast<QObject *>(item)
                                         : static_cast<QObject *>(widget),
                                    &press);
        QCoreApplication::sendEvent(item ? static_cast<QObject *>(item)
                                         : static_cast<QObject *>(widget),
                                    &release);
    }
    return true;
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
