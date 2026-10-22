// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2022 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "chainparams.h"
#include "rewards.h"
#include "pow.h"
#include "primitives/block.h"
#include "timedata.h"
#include "utiltime.h"
#include "validation.h"
#include "clientmodel.h"
#include "clientversion.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "mempoolpage.h"
#include "rpc/server.h"
#include "univalue.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <exception>
#include <algorithm>
#include <cstdint>

#define DECORATION_SIZE 32
#define NUM_ITEMS 3

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 6;
        int ypad = 2;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        const QString shortAddress = painter->fontMetrics().elidedText(
            address, Qt::ElideRight, addressRect.width());
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter,
                          shortAddress, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
// The existing RPCs scan the UTXO set and historical blocks. They must never
// execute on the Qt GUI thread. The worker runs at most one scan at a time.
class FloofyNetworkStatsWorker : public QThread
{
    Q_OBJECT
public:
    explicit FloofyNetworkStatsWorker(QObject* parent = nullptr) : QThread(parent) {}

Q_SIGNALS:
    void statsReady(QString supply, QString burned, QString detail);
    void statsError(QString reason);

protected:
    void run() override
    {
        try {
            JSONRPCRequest utxoRequest;
            utxoRequest.strMethod = "gettxoutsetinfo";
            utxoRequest.params = UniValue(UniValue::VARR);
            UniValue utxo = tableRPC.execute(utxoRequest);
            const UniValue utxoAmount = find_value(utxo, "total_amount");
            const UniValue utxoHeight = find_value(utxo, "height");
            const UniValue utxoHash = find_value(utxo, "bestblock");
            if (!utxoAmount.isNum() || !utxoHeight.isNum() || !utxoHash.isStr())
                throw std::runtime_error("Incomplete UTXO statistics response");

            JSONRPCRequest burnRequest;
            burnRequest.strMethod = "getburnedtotal";
            burnRequest.params = UniValue(UniValue::VARR);
            UniValue burns = tableRPC.execute(burnRequest);
            const UniValue burnedAmount = find_value(burns, "confirmed_burned");
            const UniValue burnHeight = find_value(burns, "height");
            const UniValue burnHash = find_value(burns, "blockhash");
            if (!burnedAmount.isNum() || !burnHeight.isNum() || !burnHash.isStr())
                throw std::runtime_error("Incomplete burn statistics response");

            // Reject mixed snapshots from a reorganisation or a new block.
            if (utxoHeight.get_int() != burnHeight.get_int() ||
                utxoHash.get_str() != burnHash.get_str())
                throw std::runtime_error("Chain tip changed during scan; refresh to retry");

            const QString supply = BitcoinUnits::formatWithUnit(
                BitcoinUnits::BTC, AmountFromValue(utxoAmount), false,
                BitcoinUnits::separatorAlways);
            const QString burned = BitcoinUnits::formatWithUnit(
                BitcoinUnits::BTC, AmountFromValue(burnedAmount), false,
                BitcoinUnits::separatorAlways);
            Q_EMIT statsReady(supply, burned,
                              QString("Verified at block %1").arg(utxoHeight.get_int()));
        } catch (const UniValue& rpcError) {
            const UniValue message = find_value(rpcError, "message");
            Q_EMIT statsError(message.isStr()
                                  ? QString::fromStdString(message.get_str())
                                  : QStringLiteral("Network statistics RPC failed"));
        } catch (const std::exception& error) {
            Q_EMIT statsError(QString::fromUtf8(error.what()));
        } catch (...) {
            Q_EMIT statsError(QStringLiteral("Unknown statistics error"));
        }
    }
};


// A full rank scan runs only when requested and never on the GUI thread.
// In particular, opening the wallet must not scan the UTXO set.
class FloofyRichListWorker : public QThread
{
    Q_OBJECT
public:
    explicit FloofyRichListWorker(QObject* parent = nullptr) : QThread(parent) {}

Q_SIGNALS:
    void richListReady(QString rank, QString details, QString tooltip);
    void richListError(QString message);

protected:
    void run() override
    {
        try {
            JSONRPCRequest request;
            request.strMethod = "getwalletrichlistrank";
            request.params = UniValue(UniValue::VARR);
            request.fHelp = false;
            const UniValue reply = tableRPC.execute(request);
            const UniValue rank = find_value(reply, "rank");
            const UniValue address = find_value(reply, "address");
            const UniValue balance = find_value(reply, "balance");
            const UniValue walletBalance = find_value(reply, "wallet_balance");
            const UniValue totalAddresses = find_value(reply, "total_addresses");
            const UniValue snapshotHeight = find_value(reply, "height");
            if ((!rank.isNull() && !rank.isNum()) ||
                (!address.isNull() && !address.isStr()) ||
                !balance.isNum() || !walletBalance.isNum() ||
                !totalAddresses.isNum() || !snapshotHeight.isNum())
                throw std::runtime_error("Incomplete rich-list RPC response");

            const QString snapshot = QString("Block #%1")
                .arg(snapshotHeight.get_int());
            if (rank.isNull()) {
                Q_EMIT richListReady(QStringLiteral("Unranked"),
                    QStringLiteral("No funded wallet address · ") + snapshot,
                    QStringLiteral("The wallet has no confirmed, spendable "
                                   "address UTXOs in this snapshot."));
                return;
            }

            const QString formattedBalance = BitcoinUnits::formatWithUnit(
                BitcoinUnits::BTC, AmountFromValue(balance), false,
                BitcoinUnits::separatorAlways);
            const QString formattedWallet = BitcoinUnits::formatWithUnit(
                BitcoinUnits::BTC, AmountFromValue(walletBalance), false,
                BitcoinUnits::separatorAlways);
            const QString displayRank =
                QString("#%1 / %2").arg(rank.get_int64()).arg(totalAddresses.get_int64());

            Q_EMIT richListReady(
                displayRank,
                QString("%1 · %2").arg(formattedBalance, snapshot),
                QString("Richest wallet-owned address: %1\n"
                        "Its confirmed UTXO balance: %2\n"
                        "Confirmed wallet-owned address UTXOs: %3\n"
                        "Ranked funded addresses: %4\n"
                        "UTXO snapshot: %5\n\n"
                        "This is an ADDRESS rank, not a person's or wallet's "
                        "aggregate rank. Only extractable standard addresses "
                        "with confirmed unspent outputs count. "
                        "Explorer rich lists may rank differently.")
                    .arg(QString::fromStdString(address.get_str()))
                    .arg(formattedBalance)
                    .arg(formattedWallet)
                    .arg(totalAddresses.get_int64())
                    .arg(snapshot));
        } catch (const UniValue& rpcError) {
            const UniValue reason = find_value(rpcError, "message");
            Q_EMIT richListError(reason.isStr()
                ? QString::fromStdString(reason.get_str())
                : QStringLiteral("Rich-list RPC failed"));
        } catch (const std::exception& ex) {
            Q_EMIT richListError(QString::fromUtf8(ex.what()));
        } catch (...) {
            Q_EMIT richListError(QStringLiteral("Unexpected rich-list error"));
        }
    }
};

#include "overviewpage.moc"

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle, this)),
    networkStatsWorker(nullptr),
    lastNetworkStatsRefresh(0),
    richListWorker(nullptr),
    richListRefreshButton(nullptr),
    richListRankLabel(nullptr),
    richListDetailsLabel(nullptr)
{
    ui->setupUi(this);

    // Compact Overview: preserve the existing wallet information, but remove
    // oversized art and use tighter layout spacing in desktop-size windows.
    ui->topLayout->setContentsMargins(4, 4, 4, 4);
    ui->topLayout->setSpacing(0);
    ui->horizontalLayout->setSpacing(14);
    ui->verticalLayout_2->setSpacing(7);
    ui->verticalLayout_3->setSpacing(7);
    ui->verticalLayout_4->setContentsMargins(12, 10, 12, 10);
    ui->verticalLayout->setContentsMargins(12, 10, 12, 10);
    ui->gridLayout->setHorizontalSpacing(10);
    ui->gridLayout->setVerticalSpacing(5);
    ui->label_wallet_bgcoin->hide();
    ui->label_wallet_version_overlay->hide();
    // Keep the tip accessible on hover without reserving several text lines.
    ui->label_tip_header->hide();
    ui->label_tip->setWordWrap(false);
    ui->label_tip->setMaximumHeight(18);
    ui->label_tip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ui->labelWalletStatus->setIconSize(QSize(15, 15));
    ui->labelTransactionsStatus->setIconSize(QSize(15, 15));

    // Network statistics show UTXO totals and provable OP_RETURN burns.
    // They are grouped with the wallet panels inside a single dashboard.
    QFrame* statsPanel = new QFrame(this);
    statsPanel->setObjectName("networkStatsPanel");
    statsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* statsLayout = new QVBoxLayout(statsPanel);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(8);

    QHBoxLayout* heading = new QHBoxLayout();
    QLabel* headingTitle = new QLabel(tr("Network statistics"), statsPanel);
    headingTitle->setObjectName("networkStatsTitle");
    heading->addWidget(headingTitle);
    heading->addStretch();
    QPushButton* refresh = new QPushButton(tr("Refresh"), statsPanel);
    refresh->setObjectName("networkStatsRefresh");
    refresh->setToolTip(tr("Recalculate confirmed supply and burn totals (may take some time)"));
    heading->addWidget(refresh);
    // Put scan status in the heading instead of reserving another full row.
    networkStatusLabel = new QLabel(tr("Waiting for sync"), statsPanel);
    networkStatusLabel->setObjectName("networkStatsStatus");
    networkStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    networkStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    heading->insertWidget(2, networkStatusLabel);
    statsLayout->addLayout(heading);

    QHBoxLayout* cards = new QHBoxLayout();
    cards->setSpacing(10);
    auto makeCard = [this, statsPanel, cards](const QString& title, QLabel*& amount,
                                              const QString& tooltip) {
        QFrame* card = new QFrame(statsPanel);
        card->setObjectName("networkStatsCard");
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        card->setMinimumHeight(80);
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 9, 12, 9);
        cardLayout->setSpacing(5);
        QLabel* cardTitle = new QLabel(title, card);
        cardTitle->setObjectName("networkStatsTitle");
        cardTitle->setWordWrap(true);
        amount = new QLabel(tr("Waiting for sync"), card);
        amount->setObjectName("networkStatsAmount");
        amount->setWordWrap(true);
        amount->setTextInteractionFlags(Qt::TextSelectableByMouse);
        card->setToolTip(tooltip);
        cardLayout->addWidget(cardTitle);
        cardLayout->addWidget(amount);
        cards->addWidget(card, 1);
        return card;
    };
    makeCard(tr("Circulating (UTXO)"), networkSupplyLabel,
             tr("All currently unspent outputs, including immature coinbase outputs. "
                "This is UTXO supply, not a complete economic circulating-supply measure."));
    makeCard(tr("🔥 Provably burned"), networkBurnedLabel,
             tr("Confirmed, provably unspendable OP_RETURN burns only. "
                "Transfers to the legacy burn address are not included."));
    // Keep mining details visible, but use two slim horizontal lines instead
    // of making every network-statistics tile as tall as the reward card.
    QFrame* nextRewardCard = new QFrame(statsPanel);
    nextRewardCard->setObjectName("networkRewardStrip");
    nextRewardCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* rewardLayout = new QVBoxLayout(nextRewardCard);
    rewardLayout->setContentsMargins(12, 10, 12, 10);
    rewardLayout->setSpacing(7);
    QHBoxLayout* rewardSummary = new QHBoxLayout();
    rewardSummary->setSpacing(13);
    QLabel* rewardTitle = new QLabel(tr("⛏️ Next reward"), nextRewardCard);
    rewardTitle->setObjectName("networkStatsTitle");
    rewardSummary->addWidget(rewardTitle);
    nextRewardLabel = new QLabel(tr("Waiting for sync"), nextRewardCard);
    nextRewardLabel->setObjectName("networkStatsAmount");
    nextRewardLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rewardSummary->addWidget(nextRewardLabel);
    nextRewardHeightLabel = new QLabel(tr("Waiting for tip"), nextRewardCard);
    nextRewardHeightLabel->setObjectName("networkNextRewardHeight");
    nextRewardHeightLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rewardSummary->addWidget(nextRewardHeightLabel);
    rewardSummary->addStretch(1);
    rewardLayout->addLayout(rewardSummary);

    const QString difficultyHelp = tr(
        "Difficulty for a next-block candidate of each mining type, calculated "
        "with GetNextWorkRequired(). Lower difficulty is easier. In dual mining "
        "either native PoW or AuxPoW can find the next block.");
    // Two balanced rows: the mining type/rolling balance, then one difficulty
    // for each eligible lane. Avoid one line of densely packed tiny text.
    QGridLayout* rewardMining = new QGridLayout();
    rewardMining->setContentsMargins(0, 0, 0, 0);
    rewardMining->setHorizontalSpacing(18);
    rewardMining->setVerticalSpacing(5);
    nextMiningTypeLabel = new QLabel(tr("Mining: checking…"), nextRewardCard);
    nextMiningTypeLabel->setObjectName("networkNextMiningType");
    nextMiningTypeLabel->setToolTip(difficultyHelp);
    rewardMining->addWidget(nextMiningTypeLabel, 0, 0);
    nextMiningBalanceLabel = new QLabel(tr("Balance: checking…"), nextRewardCard);
    nextMiningBalanceLabel->setObjectName("networkNextMiningBalance");
    nextMiningBalanceLabel->setToolTip(tr(
        "Consensus balancing uses counts from the rolling window of accepted "
        "native and AuxPoW blocks. Both types stay eligible."));
    rewardMining->addWidget(nextMiningBalanceLabel, 0, 1);
    nextNativeDifficultyLabel = new QLabel(tr("PoW diff: —"), nextRewardCard);
    nextNativeDifficultyLabel->setObjectName("networkNextMiningDifficulty");
    nextNativeDifficultyLabel->setToolTip(difficultyHelp);
    rewardMining->addWidget(nextNativeDifficultyLabel, 1, 0);
    nextAuxpowDifficultyLabel = new QLabel(tr("AuxPoW diff: —"), nextRewardCard);
    nextAuxpowDifficultyLabel->setObjectName("networkNextMiningDifficulty");
    nextAuxpowDifficultyLabel->setToolTip(difficultyHelp);
    rewardMining->addWidget(nextAuxpowDifficultyLabel, 1, 1);
    rewardMining->setColumnStretch(0, 1);
    rewardMining->setColumnStretch(1, 1);
    rewardLayout->addLayout(rewardMining);

    // The rich-list snapshot is deliberately manual: scanning all funded
    // network addresses is expensive and should never happen at startup.
    QFrame* rankCard = makeCard(
        tr("🏆 Address rank"), richListRankLabel,
        tr("Ranks this wallet's highest-balance spendable address among "
           "funded standard addresses in the confirmed UTXO set. "
           "Click Check; scans may take some time."));
    richListRankLabel->setText(tr("Not checked"));
    richListDetailsLabel = new QLabel(tr("On-demand, read-only"), rankCard);
    richListDetailsLabel->setObjectName("networkRichListDetails");
    richListDetailsLabel->setWordWrap(false);
    richListDetailsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rankCard->layout()->addWidget(richListDetailsLabel);
    richListRefreshButton = new QPushButton(tr("Check Rank"), rankCard);
    richListRefreshButton->setObjectName("networkRichListRefresh");
    richListRefreshButton->setEnabled(false); // Wallet model not set yet.
    richListRefreshButton->setToolTip(
        tr("Scan the blockchain UTXO set to find your richest address's rank. "
           "This is resource-intensive and only runs when requested."));
    // Place the rank and button side-by-side instead of stacking controls.
    QVBoxLayout* rankLayout = qobject_cast<QVBoxLayout*>(rankCard->layout());
    rankLayout->removeWidget(richListRankLabel);
    QHBoxLayout* rankRow = new QHBoxLayout();
    rankRow->setSpacing(9);
    rankRow->addWidget(richListRankLabel, 1);
    rankRow->addWidget(richListRefreshButton);
    rankLayout->insertLayout(1, rankRow);
    connect(richListRefreshButton, SIGNAL(clicked()),
            this, SLOT(refreshRichListRank()));
    statsLayout->addLayout(cards);
    statsLayout->addWidget(nextRewardCard);

    compactMempoolPage = new MempoolPage(true, this);
    compactMempoolPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    // One unified dashboard fills the Overview viewport rather than sitting
    // inside a narrow, vertically centred canvas with wide empty borders.
    // Its children keep comfortable, natural heights and the whole view can
    // scroll when a smaller window cannot show all sections at once.
    QFrame* dashboard = new QFrame(this);
    dashboard->setObjectName("floofyOverviewDashboard");
    dashboard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout* dashboardLayout = new QVBoxLayout(dashboard);
    dashboardLayout->setContentsMargins(22, 17, 22, 19);
    dashboardLayout->setSpacing(14);
    dashboardLayout->setSizeConstraint(QLayout::SetMinimumSize);

    QHBoxLayout* dashboardHeading = new QHBoxLayout();
    QLabel* dashboardTitle = new QLabel(tr("🐾 FloofyChain dashboard"), dashboard);
    dashboardTitle->setObjectName("floofyOverviewTitle");
    dashboardHeading->addWidget(dashboardTitle);
    dashboardHeading->addStretch();
    QLabel* dashboardHint = new QLabel(tr("Wallet & network overview"), dashboard);
    dashboardHint->setObjectName("floofyOverviewSubtitle");
    dashboardHeading->addWidget(dashboardHint);
    dashboardLayout->addLayout(dashboardHeading);

    // Preserve existing alert behaviour while keeping warnings in the
    // same visual section as the information they describe.
    ui->topLayout->removeWidget(ui->labelAlerts);
    dashboardLayout->addWidget(ui->labelAlerts);

    // Existing Designer layout contains the balances and recent transactions.
    // A nested QLayout is also a QLayoutItem: remove it without deleting,
    // clear its previous QObject parent, then attach it to this new layout.
    ui->topLayout->removeItem(ui->horizontalLayout);
    ui->horizontalLayout->setParent(nullptr);
    dashboardLayout->addLayout(ui->horizontalLayout);
    dashboardLayout->addWidget(statsPanel);
    dashboardLayout->addWidget(compactMempoolPage);
    // Only this stretch receives any remaining height. None of the cards is
    // forced into a giant empty panel when the wallet is maximised.
    dashboardLayout->addStretch(1);

    // Remove orphaned spacers left behind by the original Designer layout.
    for (int i = ui->topLayout->count() - 1; i >= 0; --i) {
        if (ui->topLayout->itemAt(i)->spacerItem())
            delete ui->topLayout->takeAt(i);
    }

    // Use the dashboard itself as the scroll-area widget. With
    // widgetResizable=true it grows to cover *all* visible white space,
    // including at maximised size, and scrolls when the window is smaller.
    QScrollArea* overviewScroll = new QScrollArea(this);
    overviewScroll->setObjectName("floofyOverviewScroll");
    overviewScroll->setFrameShape(QFrame::NoFrame);
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    overviewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    overviewScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    overviewScroll->setWidget(dashboard);
    ui->topLayout->addWidget(overviewScroll, 1);

    connect(refresh, SIGNAL(clicked()), this, SLOT(refreshNetworkStats()));


    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    //set the current version
    //ui->label_wallet_version_overlay->setText(QString::fromStdString(FormatFullVersion()));

    // Preserve the helpful tip on hover; a dedicated text row here
    // used another full line of the limited dashboard height.
    UpdateTip();
    ui->label_4->setToolTip(ui->label_tip->toolTip());
    ui->label_tip->hide();

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 6));
    ui->listTransactions->setMaximumHeight(NUM_ITEMS * (DECORATION_SIZE + 8));
    ui->listTransactions->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
}

void OverviewPage::UpdateTip()
{
    QStringList tips = {
        tr("Never share your wallet.dat file with anyone"),
        tr("For advanced operations, use the console in 'Help' -> 'Debug Window'"),
        tr("Encrypt your wallet with a strong passphrase for maximum security"),
        tr("Make sure to keep your wallet software updated"),
        tr("Backup your private key to recover your coins, using 'File' > 'Backup Wallet'"),
        tr("Always do your own research before using an external cryptocurrency service"),
        tr("Never share your private key with anyone"),
        tr("Who owns the private keys, owns the coins"),
        tr("To see ongoing development and contribute, check out the Floofy Core repository on GitHub"),
        tr("Services that claim to double your floofys are always ponzi schemes")
    };

    int i = rand() % tips.length();
    ui->label_tip->setText(tips[i]);
    ui->label_tip->setToolTip(tips[i]);
    ui->label_tip->setText(fontMetrics().elidedText(
        tips[i], Qt::ElideRight, std::max(180, ui->label_tip->width())));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    // Avoid destroying the receiver while its worker still uses Qt signals.
    if (networkStatsWorker) {
        networkStatsWorker->wait();
        delete networkStatsWorker;
    }
    if (richListWorker) {
        richListWorker->wait();
        delete richListWorker;
    }
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    compactMempoolPage->setClientModel(model);
    updateNextReward();
    if(model)
    {
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)),
                this, SLOT(onNetworkBlockChanged(int,QDateTime,double,bool)));
        QTimer::singleShot(1500, this, SLOT(refreshNetworkStats()));

        // Show warning if this is a prerelease version
        //connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        //updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    richListRefreshButton->setEnabled(model != nullptr && richListWorker == nullptr);
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}


void OverviewPage::onNetworkBlockChanged(int, const QDateTime&, double, bool header)
{
    if (header)
        return;
    updateNextReward();
    // Expensive historical scans are rate-limited to once every 15 minutes.
    if (lastNetworkStatsRefresh == 0 ||
        QDateTime::currentMSecsSinceEpoch() - lastNetworkStatsRefresh > 15 * 60 * 1000)
        refreshNetworkStats();
}

// Use the same difficulty-1 scale as getdifficulty() in rpc/blockchain.cpp,
// but convert the NEXT candidate's nBits rather than a previously mined block.
// This display-only helper never participates in consensus validation.
static QString FormatNextMiningDifficulty(uint32_t bits)
{
    const uint32_t mantissa = bits & 0x00ffffffU;
    if (mantissa == 0)
        return QStringLiteral("N/A");

    int exponent = static_cast<int>((bits >> 24) & 0xffU);
    double difficulty = 65535.0 / static_cast<double>(mantissa);
    while (exponent < 29) {
        difficulty *= 256.0;
        ++exponent;
    }
    while (exponent > 29) {
        difficulty /= 256.0;
        --exponent;
    }
    return QString::number(difficulty, 'g', 9);
}

void OverviewPage::updateNextReward()
{
    auto showPending = [this](const QString& message) {
        nextRewardLabel->setText(message);
        nextRewardHeightLabel->setText(tr("Waiting for chain tip"));
        nextMiningTypeLabel->setText(tr("Mining: —"));
        nextNativeDifficultyLabel->setText(tr("PoW diff: —"));
        nextAuxpowDifficultyLabel->setText(tr("AuxPoW diff: —"));
        nextMiningBalanceLabel->setText(tr("Balance: —"));
    };

    if (!clientModel) {
        showPending(tr("Node not connected"));
        return;
    }
    if (clientModel->inInitialBlockDownload()) {
        showPending(tr("Synchronising"));
        return;
    }

    CAmount subsidy = 0;
    int nextHeight = -1;
    unsigned int nativeBits = 0;
    unsigned int auxpowBits = 0;
    bool auxpowAllowed = false;
    bool dualPow = false;
    uint32_t nativeCount = 0;
    uint32_t auxpowCount = 0;

    // Match BlockAssembler::CreateNewBlock and UpdateTime: use the accepted
    // tip, height-aware consensus and a candidate for EACH mining type.
    // The dual-DAA branch uses type-specific previous accepted blocks.
    {
        LOCK(cs_main);
        const CBlockIndex* tip = chainActive.Tip();
        if (tip) {
            nextHeight = tip->nHeight + 1;
            const Consensus::Params& params = Params().GetConsensus(nextHeight);
            subsidy = GetFloofyBlockSubsidy(
                nextHeight, params, tip->GetBlockHash());
            auxpowAllowed = !params.fAllowLegacyBlocks;
            dualPow = params.fFloofyDualPowDifficulty;

            CBlockHeader candidate;
            candidate.SetBaseVersion(4, params.nAuxpowChainId);
            candidate.hashPrevBlock = tip->GetBlockHash();
            // Before adaptive difficulty, the historic minimum-difficulty
            // exception can depend on candidate time, as it does for miners.
            candidate.nTime = static_cast<uint32_t>(
                std::max<int64_t>(tip->GetMedianTimePast() + 1,
                    params.fFloofyAdaptiveDifficulty ? GetTime() : GetAdjustedTime()));
            candidate.SetAuxpowFlag(false);
            nativeBits = GetNextWorkRequired(tip, &candidate, params);
            if (auxpowAllowed) {
                candidate.SetAuxpowFlag(true);
                auxpowBits = GetNextWorkRequired(tip, &candidate, params);
            }
            if (dualPow)
                FloofyGetRollingWindowCounts(tip, params, nativeCount, auxpowCount);
        }
    }

    if (nextHeight < 0) {
        showPending(tr("Tip unavailable"));
        return;
    }

    nextRewardLabel->setText(BitcoinUnits::formatWithUnit(
        BitcoinUnits::BTC, subsidy, false, BitcoinUnits::separatorAlways));
    nextRewardHeightLabel->setText(tr("Block #%1 · + fees").arg(nextHeight));
    nextNativeDifficultyLabel->setText(
        tr("PoW diff: %1").arg(FormatNextMiningDifficulty(nativeBits)));

    if (!auxpowAllowed) {
        nextMiningTypeLabel->setText(tr("Mining: Native PoW only"));
        nextAuxpowDifficultyLabel->setText(tr("AuxPoW diff: not yet active"));
        nextMiningBalanceLabel->setText(tr("Balance: inactive"));
        return;
    }

    nextAuxpowDifficultyLabel->setText(
        tr("AuxPoW diff: %1").arg(FormatNextMiningDifficulty(auxpowBits)));
    nextMiningTypeLabel->setText(tr("Mining: PoW or AuxPoW (either)"));

    if (!dualPow) {
        nextMiningBalanceLabel->setText(tr("Balance: shared difficulty"));
    } else if (nativeCount == auxpowCount) {
        nextMiningBalanceLabel->setText(
            tr("Balance: even (PoW %1 / Aux %2)").arg(nativeCount).arg(auxpowCount));
    } else {
        const QString lane = nativeCount < auxpowCount
            ? tr("PoW") : tr("AuxPoW");
        nextMiningBalanceLabel->setText(
            tr("Balance favours %1 (%2 / %3)")
            .arg(lane).arg(nativeCount).arg(auxpowCount));
    }
}

void OverviewPage::refreshNetworkStats()
{
    if (!clientModel || clientModel->inInitialBlockDownload()) {
        networkStatusLabel->setText(tr("Syncing…"));
        networkStatusLabel->setToolTip(tr("Supply and burn figures become available after synchronisation."));
        return;
    }
    if (networkStatsWorker)
        return;

    lastNetworkStatsRefresh = QDateTime::currentMSecsSinceEpoch();
    networkStatusLabel->setText(tr("Checking…"));
    networkStatusLabel->setToolTip(tr("Reading confirmed chain statistics in the background."));
    networkStatsWorker = new FloofyNetworkStatsWorker(this);
    connect(networkStatsWorker, SIGNAL(statsReady(QString,QString,QString)),
            this, SLOT(networkStatsReady(QString,QString,QString)));
    connect(networkStatsWorker, SIGNAL(statsError(QString)),
            this, SLOT(networkStatsError(QString)));
    connect(networkStatsWorker, SIGNAL(finished()),
            this, SLOT(networkStatsFinished()));
    networkStatsWorker->start();
}

void OverviewPage::networkStatsReady(const QString& supply, const QString& burned, const QString& detail)
{
    networkSupplyLabel->setText(supply);
    networkBurnedLabel->setText(burned);
    networkStatusLabel->setText(detail);
    networkStatusLabel->setToolTip(detail + tr(" · UTXO amount includes immature outputs"));
}

void OverviewPage::networkStatsError(const QString& reason)
{
    networkStatusLabel->setText(tr("Stats unavailable"));
    networkStatusLabel->setToolTip(reason);
}

void OverviewPage::networkStatsFinished()
{
    if (networkStatsWorker) {
        networkStatsWorker->deleteLater();
        networkStatsWorker = nullptr;
    }
}


void OverviewPage::refreshRichListRank()
{
    if (richListWorker || !walletModel || !clientModel)
        return;
    if (clientModel->inInitialBlockDownload()) {
        richListDetailsLabel->setText(tr("Synchronising — check again after sync"));
        return;
    }

    richListRefreshButton->setEnabled(false);
    richListRankLabel->setText(tr("Scanning…"));
    richListDetailsLabel->setText(tr("Reading confirmed UTXOs in the background"));
    richListWorker = new FloofyRichListWorker(this);
    connect(richListWorker, SIGNAL(richListReady(QString,QString,QString)),
            this, SLOT(richListReady(QString,QString,QString)));
    connect(richListWorker, SIGNAL(richListError(QString)),
            this, SLOT(richListError(QString)));
    connect(richListWorker, SIGNAL(finished()),
            this, SLOT(richListFinished()));
    richListWorker->start();
}

void OverviewPage::richListReady(const QString& ranking,
                                 const QString& details, const QString& tooltip)
{
    richListRankLabel->setText(ranking);
    richListDetailsLabel->setText(details);
    richListRankLabel->setToolTip(tooltip);
    richListDetailsLabel->setToolTip(tooltip);
}

void OverviewPage::richListError(const QString& message)
{
    richListRankLabel->setText(tr("Unavailable"));
    richListDetailsLabel->setText(tr("Scan failed; retry when ready"));
    richListRankLabel->setToolTip(message);
    richListDetailsLabel->setToolTip(message);
}

void OverviewPage::richListFinished()
{
    if (richListWorker) {
        richListWorker->deleteLater();
        richListWorker = nullptr;
    }
    if (richListRefreshButton)
        richListRefreshButton->setEnabled(walletModel && clientModel);
}
