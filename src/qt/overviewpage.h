// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OVERVIEWPAGE_H
#define BITCOIN_QT_OVERVIEWPAGE_H

#include "amount.h"

#include <QWidget>
#include <QDateTime>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;
class QThread;
class QLabel;
class QPushButton;
class MempoolPage;

namespace Ui {
    class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);
    void UpdateTip();

public Q_SLOTS:
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;

    TxViewDelegate *txdelegate;
    std::unique_ptr<TransactionFilterProxy> filter;
    QThread* networkStatsWorker;
    QLabel* networkSupplyLabel;
    QLabel* networkBurnedLabel;
    QLabel* nextRewardLabel;
    QLabel* nextRewardHeightLabel;
    QLabel* nextMiningTypeLabel;
    QLabel* nextNativeDifficultyLabel;
    QLabel* nextAuxpowDifficultyLabel;
    QLabel* nextMiningBalanceLabel;
    QLabel* networkStatusLabel;
    MempoolPage* compactMempoolPage;
    QThread* richListWorker;
    QPushButton* richListRefreshButton;
    QLabel* richListRankLabel;
    QLabel* richListDetailsLabel;
    qint64 lastNetworkStatsRefresh;

private Q_SLOTS:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
    void refreshNetworkStats();
    void updateNextReward();
    void onNetworkBlockChanged(int count, const QDateTime& blockDate, double verificationProgress, bool header);
    void networkStatsReady(const QString& supply, const QString& burned, const QString& detail);
    void networkStatsError(const QString& reason);
    void networkStatsFinished();
    void refreshRichListRank();
    void richListReady(const QString& ranking, const QString& details,
                       const QString& tooltip);
    void richListError(const QString& message);
    void richListFinished();
};

#endif // BITCOIN_QT_OVERVIEWPAGE_H
