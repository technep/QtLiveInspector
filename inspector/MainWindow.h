#pragma once

#include <QList>
#include <QMainWindow>
#include <QSet>

class RemoteConnection;
class ObjectTreeModel;
class PropertyModel;
class QSortFilterProxyModel;
class QTreeView;
class QTableView;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTimer;
class QJsonObject;
struct TreeNode;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onConnectClicked();
    void onConnected();
    void onDisconnected();
    void onSocketError(const QString &message);
    void onEventReceived(const QJsonObject &ev);
    void refreshTree();
    void onTreeSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onPickToggled(bool checked);
    void onHighlightToggled(bool checked);
    void onCopyLocator();
    void loadProperties();

private:
    void setUiConnected(bool connected);
    void log(const QString &message);
    bool revealPath(const QList<qint64> &path);
    QSet<qint64> collectExpandedIds() const;
    void restoreExpandedIds(const QSet<qint64> &ids);
    TreeNode *currentNode() const;

    RemoteConnection *m_conn;
    ObjectTreeModel *m_treeModel;
    QSortFilterProxyModel *m_proxy;
    PropertyModel *m_propModel;

    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpin;
    QLineEdit *m_tokenEdit;
    QPushButton *m_connectBtn;
    QLabel *m_statusLabel;

    QLineEdit *m_searchEdit;
    QTreeView *m_treeView;
    QTableView *m_propView;
    QPlainTextEdit *m_logView;

    QPushButton *m_pickBtn;
    QPushButton *m_highlightBtn;
    QPushButton *m_locatorBtn;
    QPushButton *m_refreshBtn;
    QCheckBox *m_autoRefresh;
    QTimer *m_refreshTimer;

    qint64 m_currentNodeId = -1;
    QList<qint64> m_pendingPath; // path to reveal after the next tree refresh
};
