// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mempoolpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "sync.h"
#include "txmempool.h"
#include "validation.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct PendingTx
{
    QString txid;
    QString received;
    QString bytes;
    QString fee;
    QString rate;
    QString inputsOutputs;
};

class MempoolReadWorker : public QThread
{
public:
    explicit MempoolReadWorker(int limit, QObject* parent = nullptr)
        : QThread(parent), rowLimit(limit), count(0), memoryBytes(0) {}

    std::vector<PendingTx> rows;
    QString feeTotal;
    QString error;
    int count;
    quint64 memoryBytes;

protected:
    void run() override
    {
        try {
            CAmount sumFees = 0;
            {
                LOCK(mempool.cs);
                count = static_cast<int>(mempool.mapTx.size());
                memoryBytes = static_cast<quint64>(mempool.DynamicMemoryUsage());

                // Sum ALL unconfirmed transaction fees, not just displayed rows.
                for (const CTxMemPoolEntry& entry : mempool.mapTx) {
                    const CAmount fee = entry.GetFee();
                    if (fee < 0 || fee > std::numeric_limits<CAmount>::max() - sumFees)
                        throw std::runtime_error("Mempool fee aggregate overflow");
                    sumFees += fee;
                }

                const auto& byTime = mempool.mapTx.get<entry_time>();
                for (auto it = byTime.rbegin();
                     it != byTime.rend() && static_cast<int>(rows.size()) < rowLimit; ++it) {
                    const CTransaction& tx = it->GetTx();
                    const size_t vsize = it->GetTxSize();
                    PendingTx row;
                    row.txid = QString::fromStdString(tx.GetHash().GetHex());
                    row.received = QDateTime::fromTime_t(
                        static_cast<uint>(it->GetTime())).toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
                    row.bytes = QString::number(static_cast<qulonglong>(vsize));
                    row.fee = BitcoinUnits::format(
                        BitcoinUnits::BTC, it->GetFee(), false,
                        BitcoinUnits::separatorAlways);
                    row.rate = QString::number(
                        static_cast<double>(it->GetFee()) /
                            static_cast<double>(std::max<size_t>(vsize, 1)), 'f', 2);
                    row.inputsOutputs = QString("%1 / %2")
                        .arg(tx.vin.size()).arg(tx.vout.size());
                    rows.push_back(row);
                }
            }
            feeTotal = BitcoinUnits::formatWithUnit(
                BitcoinUnits::BTC, sumFees, false,
                BitcoinUnits::separatorAlways);
        } catch (const std::exception& ex) {
            error = QString::fromUtf8(ex.what());
        } catch (...) {
            error = QStringLiteral("Unable to take a mempool snapshot");
        }
    }

private:
    int rowLimit;
};

QFrame* metricCard(const QString& title, QLabel*& number, QWidget* parent, bool compact)
{
    QFrame* card = new QFrame(parent);
    card->setObjectName("mempoolMetricCard");
    if (compact)
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QBoxLayout* metricLayout = compact
        ? static_cast<QBoxLayout*>(new QHBoxLayout(card))
        : static_cast<QBoxLayout*>(new QVBoxLayout(card));
    metricLayout->setContentsMargins(compact ? 5 : 8, compact ? 2 : 5,
                                     compact ? 5 : 8, compact ? 2 : 5);
    metricLayout->setSpacing(compact ? 4 : 2);
    QLabel* caption = new QLabel(title, card);
    caption->setObjectName("mempoolMetricCaption");
    number = new QLabel(QStringLiteral("—"), card);
    number->setObjectName("mempoolMetricNumber");
    number->setTextInteractionFlags(Qt::TextSelectableByMouse);
    number->setWordWrap(true);
    metricLayout->addWidget(caption);
    metricLayout->addWidget(number);
    if (compact) metricLayout->addStretch();
    return card;
}
} // namespace

MempoolPage::MempoolPage(bool isCompact, QWidget* parent)
    : QWidget(parent),
      compact(isCompact),
      clientModel(nullptr),
      worker(nullptr),
      timer(new QTimer(this)),
      countValue(nullptr),
      memoryValue(nullptr),
      feesValue(nullptr),
      statusValue(nullptr),
      peersValue(nullptr),
      detailsValue(nullptr),
      table(nullptr),
      search(nullptr),
      expandButton(nullptr),
      copyButton(nullptr)
{
    setObjectName(compact ? "mempoolCompact" : "mempoolFullPage");
    if (compact)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(compact ? 5 : 10, compact ? 3 : 8,
                            compact ? 5 : 10, compact ? 3 : 8);
    main->setSpacing(compact ? 2 : 5);

    QHBoxLayout* titleBar = new QHBoxLayout();
    QLabel* title = new QLabel(compact ? tr("Network mempool")
                                      : tr("FloofyChain Network Mempool"), this);
    title->setObjectName(compact ? "mempoolCompactHeading" : "mempoolPageHeading");
    titleBar->addWidget(title);
    if (!compact)
        titleBar->addStretch();
    QLabel* live = new QLabel(compact ? tr("● LIVE") : tr("● NETWORK OBSERVED"), this);
    live->setObjectName("mempoolLiveBadge");
    live->setToolTip(tr("All network transactions received by this node, not only this wallet. Other peers may have different transactions."));
    titleBar->addWidget(live);
    if (!compact)
        main->addLayout(titleBar);

    if (!compact) {
        QLabel* introduction = new QLabel(
            tr("Network-wide unconfirmed activity observed by your FloofyChain node "
               "(not limited to this wallet). Every node holds its own mempool, "
               "so this is a network-observed view rather than a guaranteed global total. "
               "Updated every five seconds."),
            this);
        introduction->setWordWrap(true);
        main->addWidget(introduction);
    }

    QHBoxLayout* cards = new QHBoxLayout();
    cards->setSpacing(compact ? 3 : 6);
    cards->addWidget(metricCard(compact ? tr("TXs") : tr("Network TXs seen"),
                                countValue, this, compact), 1);
    cards->addWidget(metricCard(compact ? tr("Fees") : tr("Pending fees"),
                                feesValue, this, compact), 1);
    // All three figures remain visible in the compact Overview and full page.
    cards->addWidget(metricCard(compact ? tr("Memory") : tr("Mempool memory"),
                                memoryValue, this, compact), 1);
    if (compact) {
        titleBar->addLayout(cards, 1);
        main->addLayout(titleBar);
    } else {
        main->addLayout(cards);
    }

    QHBoxLayout* controls = new QHBoxLayout();
    statusValue = new QLabel(tr("Waiting for network mempool data"), this);
    peersValue = new QLabel(tr("Peers: —"), this);
    peersValue->setObjectName("mempoolPeers");
    peersValue->setToolTip(tr("Connected P2P peers; this node observes transactions relayed across the network."));
    statusValue->setObjectName("mempoolStatus");
    statusValue->setWordWrap(!compact);
    controls->addWidget(statusValue, 1);
    controls->addWidget(peersValue);
    if (compact) {
        expandButton = new QPushButton(tr("Recent ▾"), this);
        expandButton->setObjectName("mempoolExpandButton");
        controls->addWidget(expandButton);
        connect(expandButton, SIGNAL(clicked()), this, SLOT(toggleRecent()));
    } else {
        search = new QLineEdit(this);
        search->setPlaceholderText(tr("Filter by transaction ID…"));
        search->setClearButtonEnabled(true);
        controls->addWidget(search);
        connect(search, SIGNAL(textChanged(QString)), this, SLOT(updateSearch(QString)));
    }
    main->addLayout(controls);

    table = new QTableWidget(0, 6, this);
    table->setObjectName("mempoolTable");
    table->setHorizontalHeaderLabels(QStringList()
        << tr("Received") << tr("Transaction ID") << tr("vBytes")
        << tr("Fee (FLOOF)") << tr("sat/vB") << tr("Inputs / outputs"));
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setShowGrid(false);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->setSortingEnabled(false); // Entries are newest first by default.
    table->setToolTip(tr("Double-click a transaction to copy its full TXID."));
    connect(table, SIGNAL(cellClicked(int,int)), this, SLOT(showDetails(int,int)));
    connect(table, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(copySelectedTx()));
    if (compact) {
        table->setColumnHidden(4, true);
        table->setColumnHidden(5, true);
        table->setMaximumHeight(114);
        table->verticalHeader()->setDefaultSectionSize(21);
        table->hide();
    }
    main->addWidget(table, compact ? 0 : 1);

    if (!compact) {
        detailsValue = new QLabel(
            tr("Select a transaction to see its full TXID and entry details."), this);
        detailsValue->setObjectName("mempoolDetails");
        detailsValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailsValue->setWordWrap(true);
        main->addWidget(detailsValue);
        copyButton = new QPushButton(tr("Copy selected TXID"), this);
        copyButton->setEnabled(false);
        connect(copyButton, SIGNAL(clicked()), this, SLOT(copySelectedTx()));
        QHBoxLayout* actions = new QHBoxLayout();
        actions->addWidget(copyButton);
        actions->addStretch();
        main->addLayout(actions);
    }

    timer->setInterval(5000);
    connect(timer, SIGNAL(timeout()), this, SLOT(refresh()));
}

MempoolPage::~MempoolPage()
{
    timer->stop();
    if (worker) {
        worker->wait();
        delete worker;
    }
}

void MempoolPage::setClientModel(ClientModel* model)
{
    if (clientModel)
        disconnect(clientModel, nullptr, this, nullptr);
    clientModel = model;
    timer->stop();
    if (!model) {
        peersValue->setText(tr("Peers: —"));
        statusValue->setText(tr("Local node is not connected."));
        return;
    }

    connect(model, SIGNAL(mempoolSizeChanged(long,size_t)),
            this, SLOT(updateSummary(long,size_t)));
    connect(model, SIGNAL(numConnectionsChanged(int)),
            this, SLOT(updateNetworkPeers(int)));
    updateNetworkPeers(model->getNumConnections());
    updateSummary(model->getMempoolSize(), model->getMempoolDynamicUsage());
    timer->start();
    QTimer::singleShot(300, this, SLOT(refresh()));
}

void MempoolPage::updateSummary(long count, size_t bytes)
{
    const QString countText = QLocale().toString(static_cast<qlonglong>(count));
    const QString memoryText = tr("%1 MiB").arg(
        QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 2));
    if (countValue->text() != countText)
        countValue->setText(countText);
    // Defensive guard for incomplete widget initialization.
    if (memoryValue && memoryValue->text() != memoryText)
        memoryValue->setText(memoryText);
}

void MempoolPage::updateNetworkPeers(int count)
{
    peersValue->setText(tr("Peers: %1").arg(count));
    peersValue->setToolTip(count == 0
        ? tr("No connected P2P peers. Only locally known unconfirmed transactions can appear.")
        : tr("Connected network peers. Transactions listed are not limited to this wallet."));
}

void MempoolPage::refresh()
{
    if (!clientModel || worker)
        return;
    worker = new MempoolReadWorker(compact ? 6 : 100, this);
    connect(worker, SIGNAL(finished()), this, SLOT(refreshFinished()));
    worker->start();
}

void MempoolPage::refreshFinished()
{
    if (!worker)
        return;
    MempoolReadWorker* snapshot = static_cast<MempoolReadWorker*>(worker);
    worker = nullptr;
    const QString error = snapshot->error;

    if (error.isEmpty()) {
        updateSummary(snapshot->count, static_cast<size_t>(snapshot->memoryBytes));
        feesValue->setText(snapshot->feeTotal);
        const std::vector<PendingTx>& pending = snapshot->rows;
        const QString selection = table->currentRow() >= 0 && table->item(table->currentRow(), 1)
                                ? table->item(table->currentRow(), 1)->data(Qt::UserRole).toString()
                                : QString();
        table->setUpdatesEnabled(false);
        table->setRowCount(static_cast<int>(pending.size()));
        int selectedRow = -1;
        for (size_t i = 0; i < pending.size(); ++i) {
            const int row = static_cast<int>(i);
            const PendingTx& info = pending[i];
            QTableWidgetItem* received = new QTableWidgetItem(info.received);
            QTableWidgetItem* txid = new QTableWidgetItem(info.txid.left(14) +
                QStringLiteral("…") + info.txid.right(10));
            txid->setData(Qt::UserRole, info.txid);
            txid->setToolTip(info.txid);
            QTableWidgetItem* bytes = new QTableWidgetItem(info.bytes);
            QTableWidgetItem* fee = new QTableWidgetItem(info.fee);
            QTableWidgetItem* rate = new QTableWidgetItem(info.rate);
            QTableWidgetItem* inputOutput = new QTableWidgetItem(info.inputsOutputs);
            fee->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rate->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, 0, received);
            table->setItem(row, 1, txid);
            table->setItem(row, 2, bytes);
            table->setItem(row, 3, fee);
            table->setItem(row, 4, rate);
            table->setItem(row, 5, inputOutput);
            if (info.txid == selection)
                selectedRow = row;
        }
        table->setUpdatesEnabled(true);
        if (selectedRow >= 0)
            table->selectRow(selectedRow);
        else if (copyButton) {
            copyButton->setEnabled(false);
            detailsValue->setText(tr("Select a transaction to see its full TXID and entry details."));
        }
        if (search)
            updateSearch(search->text());
        statusValue->setText(compact
            ? tr("Updated %1").arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
            : tr("Network observed · %1 · newest %2")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                .arg(pending.size()));
        if (selectedRow >= 0)
            showDetails(selectedRow, 1);
    } else {
        statusValue->setText(tr("Mempool read error: %1").arg(error));
    }
    snapshot->deleteLater();
}

void MempoolPage::toggleRecent()
{
    const bool show = !table->isVisible();
    table->setVisible(show);
    expandButton->setText(show ? tr("Hide ▴") : tr("Recent ▾"));
}

void MempoolPage::updateSearch(const QString& query)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* txid = table->item(row, 1);
        const bool matches = txid && txid->data(Qt::UserRole).toString()
                                          .contains(query, Qt::CaseInsensitive);
        table->setRowHidden(row, !matches);
    }
}

void MempoolPage::showDetails(int row, int)
{
    if (!detailsValue)
        return;
    const QTableWidgetItem* txid = table->item(row, 1);
    if (!txid) return;
    detailsValue->setText(tr("TXID: %1\nReceived: %2\nSize: %3 vB"
                             " · Fee: %4 FLOOF · Fee rate: %5 sat/vB · Inputs / outputs: %6")
        .arg(txid->data(Qt::UserRole).toString())
        .arg(table->item(row, 0)->text())
        .arg(table->item(row, 2)->text())
        .arg(table->item(row, 3)->text())
        .arg(table->item(row, 4)->text())
        .arg(table->item(row, 5)->text()));
    copyButton->setEnabled(true);
}

void MempoolPage::copySelectedTx()
{
    const int row = table->currentRow();
    const QTableWidgetItem* txid = row >= 0 ? table->item(row, 1) : nullptr;
    if (!txid) return;
    QApplication::clipboard()->setText(txid->data(Qt::UserRole).toString());
    statusValue->setText(tr("TXID copied to clipboard."));
}
