// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef FLOOFY_QT_MEMPOOLPAGE_H
#define FLOOFY_QT_MEMPOOLPAGE_H

#include <QWidget>
#include <cstddef>

class ClientModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QThread;

/** Read-only view of this node's local mempool.
 *  The same widget powers the compact Overview card and full Mempool page.
 */
class MempoolPage : public QWidget
{
    Q_OBJECT
public:
    explicit MempoolPage(bool compact = false, QWidget* parent = nullptr);
    ~MempoolPage();
    void setClientModel(ClientModel* model);

private Q_SLOTS:
    void refresh();
    void refreshFinished();
    void updateSummary(long count, size_t memoryBytes);
    void updateNetworkPeers(int count);
    void toggleRecent();
    void updateSearch(const QString& query);
    void showDetails(int row, int column);
    void copySelectedTx();

private:
    const bool compact;
    ClientModel* clientModel;
    QThread* worker;
    QTimer* timer;
    QLabel* countValue;
    QLabel* memoryValue;
    QLabel* feesValue;
    QLabel* statusValue;
    QLabel* peersValue;
    QLabel* detailsValue;
    QTableWidget* table;
    QLineEdit* search;
    QPushButton* expandButton;
    QPushButton* copyButton;
};

#endif // FLOOFY_QT_MEMPOOLPAGE_H
