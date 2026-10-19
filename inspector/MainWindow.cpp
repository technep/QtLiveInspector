#include "MainWindow.h"

#include "ObjectTreeModel.h"
#include "PropertyModel.h"
#include "RemoteConnection.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTime>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QtLiveInspector"));
    resize(1150, 720);

    m_conn = new RemoteConnection(this);
    m_treeModel = new ObjectTreeModel(this);
    m_propModel = new PropertyModel(this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_treeModel);
    m_proxy->setFilterKeyColumn(0); // "name (Class)" covers both
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    auto *central = new QWidget(this);
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(6, 6, 6, 6);

    // --- connection bar ----------------------------------------------------
    auto *top = new QHBoxLayout;
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_hostEdit->setMaximumWidth(160);
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(17017);
    m_portSpin->setMaximumWidth(90);
    m_tokenEdit = new QLineEdit;
    m_tokenEdit->setPlaceholderText(QStringLiteral("token (optional)"));
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_tokenEdit->setMaximumWidth(160);
    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    m_statusLabel = new QLabel(QStringLiteral("disconnected"));
    top->addWidget(new QLabel(QStringLiteral("Host:")));
    top->addWidget(m_hostEdit);
    top->addWidget(new QLabel(QStringLiteral("Port:")));
    top->addWidget(m_portSpin);
    top->addWidget(m_tokenEdit);
    top->addWidget(m_connectBtn);
    top->addWidget(m_statusLabel, 1);
    v->addLayout(top);

    // --- main splitter ------------------------------------------------------
    auto *split = new QSplitter;

    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("Search objectName / class / address…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_treeView = new QTreeView;
    m_treeView->setModel(m_proxy);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setHeaderHidden(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeView->setColumnHidden(2, true); // address available on demand
    ll->addWidget(m_searchEdit);
    ll->addWidget(m_treeView);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    auto *actions = new QHBoxLayout;
    m_pickBtn = new QPushButton(QStringLiteral("Pick"));
    m_pickBtn->setCheckable(true);
    m_pickBtn->setToolTip(QStringLiteral("Click an item in the target application"));
    m_highlightBtn = new QPushButton(QStringLiteral("Highlight"));
    m_highlightBtn->setCheckable(true);
    m_highlightBtn->setToolTip(QStringLiteral("Draw a red rectangle around the selected item"));
    m_locatorBtn = new QPushButton(QStringLiteral("Copy Squish Locator"));
    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"));
    m_autoRefresh = new QCheckBox(QStringLiteral("Auto"));
    actions->addWidget(m_pickBtn);
    actions->addWidget(m_highlightBtn);
    actions->addWidget(m_locatorBtn);
    actions->addStretch(1);
    actions->addWidget(m_autoRefresh);
    actions->addWidget(m_refreshBtn);
    m_propView = new QTableView;
    m_propView->setModel(m_propModel);
    m_propView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_propView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_propView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_propView->verticalHeader()->setVisible(false);
    m_propView->setSelectionBehavior(QAbstractItemView::SelectRows);
    rl->addLayout(actions);
    rl->addWidget(m_propView);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    // --- log ----------------------------------------------------------------
    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(1000);
    m_logView->setFixedHeight(110);
    v->addWidget(m_logView);

    setCentralWidget(central);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);

    // --- wiring --------------------------------------------------------------
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_conn, &RemoteConnection::socketConnected, this, &MainWindow::onConnected);
    connect(m_conn, &RemoteConnection::socketDisconnected, this, &MainWindow::onDisconnected);
    connect(m_conn, &RemoteConnection::socketError, this, &MainWindow::onSocketError);
    connect(m_conn, &RemoteConnection::eventReceived, this, &MainWindow::onEventReceived);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshTree);
    connect(m_autoRefresh, &QCheckBox::toggled, m_refreshTimer, [this](bool on) {
        on ? m_refreshTimer->start() : m_refreshTimer->stop();
    });
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (!m_pickBtn->isChecked())
            refreshTree();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) { onTreeSelectionChanged(); });
    connect(m_pickBtn, &QPushButton::toggled, this, &MainWindow::onPickToggled);
    connect(m_highlightBtn, &QPushButton::toggled, this, &MainWindow::onHighlightToggled);
    connect(m_locatorBtn, &QPushButton::clicked, this, &MainWindow::onCopyLocator);
    connect(m_propModel, &PropertyModel::valueEdited, this,
            [this](const QString &name, const QJsonValue &value) {
                if (m_currentNodeId < 0)
                    return;
                m_conn->command(QStringLiteral("setProperty"),
                                {{QStringLiteral("node"), double(m_currentNodeId)},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("value"), value}},
                                [this, name](const QJsonObject &res) {
                                    if (res[QStringLiteral("ok")].toBool()) {
                                        m_propModel->updateValue(
                                            name, res[QStringLiteral("value")]);
                                        log(QStringLiteral("set %1 ok").arg(name));
                                    } else {
                                        log(QStringLiteral("set %1 failed: %2")
                                                .arg(name, res[QStringLiteral("error")].toString()));
                                    }
                                });
            });

    setUiConnected(false);
}

void MainWindow::log(const QString &message)
{
    m_logView->appendPlainText(
        QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::setUiConnected(bool connected)
{
    m_hostEdit->setEnabled(!connected);
    m_portSpin->setEnabled(!connected);
    m_tokenEdit->setEnabled(!connected);
    m_connectBtn->setText(connected ? QStringLiteral("Disconnect")
                                    : QStringLiteral("Connect"));
    for (QWidget *w : {static_cast<QWidget *>(m_searchEdit), static_cast<QWidget *>(m_treeView),
                       static_cast<QWidget *>(m_propView), static_cast<QWidget *>(m_pickBtn),
                       static_cast<QWidget *>(m_highlightBtn), static_cast<QWidget *>(m_locatorBtn),
                       static_cast<QWidget *>(m_refreshBtn), static_cast<QWidget *>(m_autoRefresh)})
        w->setEnabled(connected);
    m_statusLabel->setText(connected ? QStringLiteral("connected")
                                     : QStringLiteral("disconnected"));
}

void MainWindow::onConnectClicked()
{
    if (m_conn->isOpen()) {
        m_conn->close();
        return;
    }
    log(QStringLiteral("connecting to %1:%2 …")
            .arg(m_hostEdit->text())
            .arg(m_portSpin->value()));
    m_conn->open(m_hostEdit->text().trimmed(), quint16(m_portSpin->value()),
                 m_tokenEdit->text());
}

void MainWindow::onConnected()
{
    m_conn->command(QStringLiteral("hello"), {}, [this](const QJsonObject &res) {
        if (!res[QStringLiteral("ok")].toBool()) {
            log(QStringLiteral("hello failed: %1")
                    .arg(res[QStringLiteral("error")].toString()));
            m_conn->close();
            return;
        }
        setWindowTitle(QStringLiteral("QtLiveInspector — %1 (Qt %2, pid %3)")
                           .arg(res[QStringLiteral("app")].toString(),
                                res[QStringLiteral("qt")].toString(),
                                QString::number(res[QStringLiteral("pid")].toInteger())));
        log(QStringLiteral("connected to %1 (Qt %2, probe %3)")
                .arg(res[QStringLiteral("app")].toString(),
                     res[QStringLiteral("qt")].toString(),
                     res[QStringLiteral("probeVersion")].toString()));
        setUiConnected(true);
        refreshTree();
    });
}

void MainWindow::onDisconnected()
{
    log(QStringLiteral("disconnected"));
    setUiConnected(false);
    m_treeModel->clear();
    m_propModel->clear();
    m_currentNodeId = -1;
    m_pickBtn->setChecked(false);
    m_highlightBtn->setChecked(false);
    setWindowTitle(QStringLiteral("QtLiveInspector"));
}

void MainWindow::onSocketError(const QString &message)
{
    log(QStringLiteral("socket error: %1").arg(message));
}

QSet<qint64> MainWindow::collectExpandedIds() const
{
    QSet<qint64> ids;
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &p) {
        const int rows = m_proxy->rowCount(p);
        for (int i = 0; i < rows; ++i) {
            const QModelIndex idx = m_proxy->index(i, 0, p);
            if (m_treeView->isExpanded(idx)) {
                if (TreeNode *n = m_treeModel->nodeFromIndex(m_proxy->mapToSource(idx)))
                    ids.insert(n->id);
                walk(idx);
            }
        }
    };
    walk(QModelIndex());
    return ids;
}

void MainWindow::restoreExpandedIds(const QSet<qint64> &ids)
{
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &p) {
        const int rows = m_proxy->rowCount(p);
        for (int i = 0; i < rows; ++i) {
            const QModelIndex idx = m_proxy->index(i, 0, p);
            TreeNode *n = m_treeModel->nodeFromIndex(m_proxy->mapToSource(idx));
            if (n && ids.contains(n->id)) {
                m_treeView->expand(idx);
                walk(idx);
            }
        }
    };
    walk(QModelIndex());
}

void MainWindow::refreshTree()
{
    if (!m_conn->isOpen())
        return;
    const QSet<qint64> expanded = collectExpandedIds();
    const qint64 selected = m_currentNodeId;

    m_conn->command(QStringLiteral("tree"), {}, [this, expanded, selected](const QJsonObject &res) {
        if (!res[QStringLiteral("ok")].toBool()) {
            log(QStringLiteral("tree failed: %1").arg(res[QStringLiteral("error")].toString()));
            return;
        }
        m_treeModel->setTree(res[QStringLiteral("root")].toObject());
        m_treeView->expand(m_proxy->index(0, 0)); // Application root
        restoreExpandedIds(expanded);

        if (selected >= 0) {
            const QModelIndex src = m_treeModel->indexForId(selected);
            if (src.isValid())
                m_treeView->setCurrentIndex(m_proxy->mapFromSource(src));
        }
        if (!m_pendingPath.isEmpty()) {
            const QList<qint64> path = m_pendingPath;
            if (revealPath(path))
                m_pendingPath.clear();
        }
    });
}

TreeNode *MainWindow::currentNode() const
{
    const QModelIndex idx = m_treeView->currentIndex();
    if (!idx.isValid())
        return nullptr;
    TreeNode *n = m_treeModel->nodeFromIndex(m_proxy->mapToSource(idx));
    return (n && n->id > 0) ? n : nullptr; // id 0 = virtual root
}

void MainWindow::onTreeSelectionChanged()
{
    TreeNode *n = currentNode();
    const qint64 newId = n ? n->id : -1;
    if (newId == m_currentNodeId)
        return;
    m_currentNodeId = newId;

    m_conn->command(QStringLiteral("unwatch"), {}, {});
    if (m_currentNodeId < 0) {
        m_propModel->clear();
        return;
    }

    loadProperties();
    m_conn->command(QStringLiteral("watch"),
                    {{QStringLiteral("node"), double(m_currentNodeId)}}, {});
    if (m_highlightBtn->isChecked())
        m_conn->command(QStringLiteral("highlight"),
                        {{QStringLiteral("node"), double(m_currentNodeId)}}, {});
}

void MainWindow::loadProperties()
{
    if (m_currentNodeId < 0)
        return;
    const qint64 node = m_currentNodeId;
    m_conn->command(QStringLiteral("properties"),
                    {{QStringLiteral("node"), double(node)}},
                    [this, node](const QJsonObject &res) {
                        if (node != m_currentNodeId)
                            return; // selection moved on meanwhile
                        if (res[QStringLiteral("ok")].toBool())
                            m_propModel->setProperties(
                                res[QStringLiteral("properties")].toArray());
                        else
                            m_propModel->clear();
                    });
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    m_proxy->setFilterFixedString(text);
    if (!text.isEmpty())
        m_treeView->expandAll();
}

void MainWindow::onPickToggled(bool checked)
{
    m_conn->command(checked ? QStringLiteral("pickStart") : QStringLiteral("pickStop"),
                    {}, {});
    if (checked)
        log(QStringLiteral("picker active — click an element in the target app (Esc cancels)"));
}

void MainWindow::onHighlightToggled(bool checked)
{
    if (checked && m_currentNodeId >= 0) {
        m_conn->command(QStringLiteral("highlight"),
                        {{QStringLiteral("node"), double(m_currentNodeId)}}, {});
    } else {
        m_conn->command(QStringLiteral("clearHighlight"), {}, {});
    }
}

void MainWindow::onCopyLocator()
{
    if (m_currentNodeId < 0) {
        log(QStringLiteral("no object selected"));
        return;
    }
    m_conn->command(QStringLiteral("locator"),
                    {{QStringLiteral("node"), double(m_currentNodeId)}},
                    [this](const QJsonObject &res) {
                        if (!res[QStringLiteral("ok")].toBool()) {
                            log(QStringLiteral("locator failed: %1")
                                    .arg(res[QStringLiteral("error")].toString()));
                            return;
                        }
                        const QString loc = res[QStringLiteral("locator")].toString();
                        QGuiApplication::clipboard()->setText(loc);
                        log(QStringLiteral("Squish locator copied: %1").arg(loc));
                        statusBar()->showMessage(QStringLiteral("Squish locator copied to clipboard"), 4000);
                    });
}

bool MainWindow::revealPath(const QList<qint64> &path)
{
    const QModelIndex src = m_treeModel->indexForPath(path);
    if (!src.isValid())
        return false;
    const QModelIndex idx = m_proxy->mapFromSource(src);
    if (!idx.isValid())
        return false;
    m_treeView->expand(idx);
    QModelIndex p = idx.parent();
    while (p.isValid()) {
        m_treeView->expand(p);
        p = p.parent();
    }
    m_treeView->setCurrentIndex(idx);
    m_treeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    return true;
}

void MainWindow::onEventReceived(const QJsonObject &ev)
{
    const QString name = ev[QStringLiteral("event")].toString();

    if (name == QLatin1String("propertyChanged")) {
        const qint64 node = ev[QStringLiteral("node")].toInteger();
        if (node == m_currentNodeId) {
            m_propModel->updateValue(ev[QStringLiteral("property")].toString(),
                                     ev[QStringLiteral("value")]);
        }
    } else if (name == QLatin1String("picked")) {
        m_pickBtn->setChecked(false);
        QList<qint64> path;
        const QJsonArray arr = ev[QStringLiteral("path")].toArray();
        for (const QJsonValue &v : arr)
            path.append(v.toInteger());
        log(QStringLiteral("picked node #%1").arg(ev[QStringLiteral("node")].toInteger()));
        if (!revealPath(path)) {
            // The tree may be stale (object created after last refresh).
            m_pendingPath = path;
            refreshTree();
        }
    } else if (name == QLatin1String("pickerStopped")) {
        m_pickBtn->setChecked(false);
    }
}
