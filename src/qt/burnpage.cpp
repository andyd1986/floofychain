// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "burnpage.h"
#include "burn.h" // FLOOF_MIN_BURN_250000
#include "chainparams.h"

#include "bitcoinamountfield.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "rpc/server.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QSet>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <exception>

// RPC may reserve keys, select coins and broadcast; do not block the Qt UI.
// Only results are read on the GUI thread after the worker has finished.
class FloofyBurnWorker : public QThread
{
public:
    FloofyBurnWorker(CAmount amount, const QString& message, bool preview, const QString& address, QObject* parent)
        : QThread(parent), amount_(amount), message_(message), preview_(preview), address_(address) {}

    UniValue result;
    QString error;

protected:
    void run() override
    {
        try {
            JSONRPCRequest request;
            request.strMethod = "burn";
            request.params = UniValue(UniValue::VARR);
            request.params.push_back(ValueFromAmount(amount_));
            request.params.push_back(false); // Never subtract fees from the burn amount.
            request.params.push_back(message_.toStdString());
            request.params.push_back(preview_);
            if (!address_.isEmpty()) request.params.push_back(address_.toStdString());
            result = tableRPC.execute(request);
        } catch (const UniValue& rpcError) {
            const UniValue reason = find_value(rpcError, "message");
            error = reason.isStr()
                ? QString::fromStdString(reason.get_str())
                : QStringLiteral("Burn RPC rejected the transaction");
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = QStringLiteral("Unknown burn RPC failure");
        }
    }

private:
    CAmount amount_;
    QString message_;
    bool preview_;
    QString address_;
};

class FloofyLeaderboardWorker : public QThread
{
public:
    explicit FloofyLeaderboardWorker(int days, QObject* parent) : QThread(parent), days_(days) {}
    UniValue result;
    QString error;
protected:
    void run() override {
        try {
            JSONRPCRequest req;
            req.strMethod = "getburnleaderboard";
            req.params = UniValue(UniValue::VARR);
            req.params.push_back(days_);
            result = tableRPC.execute(req);
        } catch (const UniValue& e) {
            const UniValue msg = find_value(e, "message");
            error = msg.isStr() ? QString::fromStdString(msg.get_str()) : QStringLiteral("RPC failure");
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) { error = QStringLiteral("Unknown leaderboard error"); }
    }
private:
    int days_;
};

BurnPage::BurnPage(QWidget* parent)
    : QWidget(parent),
      clientModel(nullptr),
      walletModel(nullptr),
      amountField(nullptr),
      messageField(nullptr),
      leaderboardAddressField(nullptr),
      leaderboardPeriod(nullptr),
      leaderboardTable(nullptr),
      leaderboardStatus(nullptr),
      leaderboardTimer(nullptr),
      leaderboardWorker(nullptr),
      acknowledgeBox(nullptr),
      availableLabel(nullptr),
      minimumLabel(nullptr),
      previewLabel(nullptr),
      resultLabel(nullptr),
      previewButton(nullptr),
      burnButton(nullptr),
      copyButton(nullptr),
      worker(nullptr),
      commitRequested(false),
      executingCommit(false)
{
    setObjectName("burnPage");
    QVBoxLayout* main = new QVBoxLayout(this);
    main->setContentsMargins(10, 8, 10, 8);
    main->setSpacing(5);

    QLabel* heading = new QLabel(tr("🔥 Burn FLOOF"), this);
    heading->setObjectName("burnPageHeading");
    main->addWidget(heading);

    QLabel* intro = new QLabel(
        tr("Permanently remove FLOOF from circulation using a provably "
           "unspendable OP_RETURN output. Burned FLOOF cannot be recovered."),
        this);
    intro->setWordWrap(true);
    intro->setObjectName("burnPageIntro");
    main->addWidget(intro);

    QFrame* card = new QFrame(this);
    card->setObjectName("burnPageCard");
    QVBoxLayout* form = new QVBoxLayout(card);
    form->setContentsMargins(10, 8, 10, 8);
    form->setSpacing(5);

    availableLabel = new QLabel(tr("Spendable balance: loading"), card);
    availableLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addWidget(availableLabel);

    QLabel* amountTitle = new QLabel(tr("Amount to burn"), card);
    QHBoxLayout* amountRow = new QHBoxLayout();
    amountRow->setSpacing(8);
    amountRow->addWidget(amountTitle);
    amountField = new BitcoinAmountField(card);
    amountField->setDisplayUnit(BitcoinUnits::BTC);
    amountRow->addWidget(amountField, 1);
    form->addLayout(amountRow);

    minimumLabel = new QLabel(tr("Minimum burn: checking network height"), card);
    minimumLabel->setObjectName("burnMinimumLabel");
    form->addWidget(minimumLabel);

    QLabel* messageTitle = new QLabel(tr("Public message (optional)"), card);
    QHBoxLayout* messageRow = new QHBoxLayout();
    messageRow->setSpacing(8);
    messageRow->addWidget(messageTitle);
    messageField = new QLineEdit(card);
    messageField->setMaxLength(48);
    messageField->setPlaceholderText(tr("Up to 48 printable ASCII characters"));
    messageField->setToolTip(tr("This message is written permanently into the blockchain."));
    messageRow->addWidget(messageField, 1);
    form->addLayout(messageRow);

    QLabel* rankingTitle = new QLabel(tr("Verified leaderboard address (optional)"), card);
    form->addWidget(rankingTitle);
    leaderboardAddressField = new QComboBox(card);
    leaderboardAddressField->setToolTip(tr("Only confirmed spendable coins belonging to this address will fund the burn. Your normal signed transaction provides proof of ownership."));
    form->addWidget(leaderboardAddressField);
    refreshLeaderboardAddresses();

    QLabel* feeNotice = new QLabel(
        tr("The exact entered amount is burned. A network transaction fee is "
           "charged separately. Preview shows the estimated total wallet cost."),
        card);
    feeNotice->setWordWrap(true);
    feeNotice->setObjectName("burnFeeNotice");
    form->addWidget(feeNotice);

    acknowledgeBox = new QCheckBox(
        tr("I understand this burn is permanent and irreversible."), card);
    acknowledgeBox->setObjectName("burnAcknowledge");
    form->addWidget(acknowledgeBox);

    QHBoxLayout* actions = new QHBoxLayout();
    previewButton = new QPushButton(tr("Preview burn"), card);
    burnButton = new QPushButton(tr("🔥 Burn FLOOF"), card);
    burnButton->setObjectName("burnSubmitButton");
    actions->addWidget(previewButton);
    actions->addWidget(burnButton);
    actions->addStretch();
    form->addLayout(actions);

    previewLabel = new QLabel(
        tr("Enter an amount and select Preview burn to inspect the fee."), card);
    previewLabel->setObjectName("burnPageNotice");
    previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLabel->setWordWrap(true);
    form->addWidget(previewLabel);

    resultLabel = new QLabel(card);
    resultLabel->setObjectName("burnPageResult");
    resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultLabel->setWordWrap(true);
    resultLabel->setVisible(false);
    form->addWidget(resultLabel);

    copyButton = new QPushButton(tr("Copy transaction ID"), card);
    copyButton->setEnabled(false);
    copyButton->setVisible(false);
    form->addWidget(copyButton);

    main->addWidget(card);

    QFrame* board = new QFrame(this);
    board->setObjectName("burnPageLeaderboard");
    QVBoxLayout* boardLayout = new QVBoxLayout(board);
    QHBoxLayout* boardControls = new QHBoxLayout();
    QLabel* boardTitle = new QLabel(tr("Verified Burn Leaderboard"), board);
    leaderboardPeriod = new QComboBox(board);
    leaderboardPeriod->addItem(tr("All time"), 0);
    leaderboardPeriod->addItem(tr("Last 30 days"), 30);
    leaderboardPeriod->addItem(tr("Last 7 days"), 7);
    QPushButton* refresh = new QPushButton(tr("Refresh"), board);
    boardControls->addWidget(boardTitle);
    boardControls->addStretch();
    boardControls->addWidget(leaderboardPeriod);
    boardControls->addWidget(refresh);
    boardLayout->addLayout(boardControls);
    leaderboardTable = new QTableWidget(0, 6, board);
    leaderboardTable->setHorizontalHeaderLabels(QStringList()
        << tr("Rank") << tr("Address") << tr("FLOOF burned") << tr("Burns")
        << tr("Latest TXID") << tr("Message"));
    leaderboardTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leaderboardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    leaderboardTable->horizontalHeader()->setStretchLastSection(true);
    leaderboardTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    boardLayout->addWidget(leaderboardTable);
    leaderboardStatus = new QLabel(tr("Leaderboard not yet loaded"), board);
    boardLayout->addWidget(leaderboardStatus);
    main->addWidget(board);
    main->addStretch();

    leaderboardTimer = new QTimer(this);
    leaderboardTimer->setInterval(600000);
    connect(leaderboardTimer, SIGNAL(timeout()), this, SLOT(refreshLeaderboard()));
    connect(leaderboardPeriod, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshLeaderboard()));
    connect(refresh, SIGNAL(clicked()), this, SLOT(refreshLeaderboard()));
    leaderboardTimer->start();
    QTimer::singleShot(0, this, SLOT(refreshLeaderboard()));

    connect(previewButton, SIGNAL(clicked()), this, SLOT(previewClicked()));
    connect(burnButton, SIGNAL(clicked()), this, SLOT(burnClicked()));
    connect(copyButton, SIGNAL(clicked()), this, SLOT(copyTransactionId()));
}

BurnPage::~BurnPage()
{
    if (worker) {
        worker->wait(); // Commit/preview must not outlive its owner.
        delete worker;
    }
    if (leaderboardTimer) leaderboardTimer->stop();
    if (leaderboardWorker) { leaderboardWorker->wait(); delete leaderboardWorker; }
    unlockContext.reset(); // Relock if this action originally unlocked the wallet.
}

void BurnPage::setClientModel(ClientModel* model)
{
    clientModel = model;
    updateAvailable();
}

void BurnPage::setWalletModel(WalletModel* model)
{
    if (walletModel)
        disconnect(walletModel, nullptr, this, nullptr);
    walletModel = model;
    if (walletModel) {
        connect(walletModel,
                SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)),
                this, SLOT(updateAvailable()));
    }
    updateAvailable();
    refreshLeaderboardAddresses();
}

QString BurnPage::formatAmount(CAmount amount) const
{
    return BitcoinUnits::formatWithUnit(
        BitcoinUnits::BTC, amount, false, BitcoinUnits::separatorAlways);
}

void BurnPage::updateAvailable()
{
    availableLabel->setText(walletModel
        ? tr("Spendable balance: %1").arg(formatAmount(walletModel->getBalance()))
        : tr("No wallet loaded"));

    const int nextHeight = clientModel ? clientModel->getNumBlocks() + 1 : 0;
    const Consensus::Params& consensus = Params().GetConsensus(nextHeight);
    if (FloofyMinimumBurnActive(consensus, nextHeight)) {
        minimumLabel->setText(tr("Minimum burn: %1")
            .arg(formatAmount(consensus.nMinimumBurnAmount)));
    } else if (consensus.fEnforceMinBurn) {
        minimumLabel->setText(tr("Minimum burn: %1 (activates at block %2)")
            .arg(formatAmount(consensus.nMinimumBurnAmount))
            .arg(static_cast<int>(consensus.nMinBurnActivationHeight)));
    } else {
        minimumLabel->setText(tr("No burn minimum on this network."));
    }
}

void BurnPage::previewClicked()
{
    beginPreview(false);
}

void BurnPage::burnClicked()
{
    beginPreview(true);
}

void BurnPage::beginPreview(bool commitAfterConfirmation)
{
    if (worker || !walletModel) {
        if (!walletModel)
            QMessageBox::warning(this, tr("Burn unavailable"), tr("Load a wallet first."));
        return;
    }
    if (clientModel && clientModel->inInitialBlockDownload()) {
        QMessageBox::warning(this, tr("Node synchronising"),
                             tr("Finish blockchain synchronisation before burning."));
        return;
    }
    if (!amountField->validate() || amountField->value() <= 0) {
        QMessageBox::warning(this, tr("Invalid burn amount"),
                             tr("Enter an amount greater than zero."));
        return;
    }

    const QString message = messageField->text();
    for (const QChar& character : message) {
        const ushort code = character.unicode();
        if (code < 32 || code > 126) {
            QMessageBox::warning(this, tr("Invalid burn message"),
                                 tr("Use printable ASCII characters only (up to 48 bytes)."));
            return;
        }
    }
    if (commitAfterConfirmation && !acknowledgeBox->isChecked()) {
        QMessageBox::warning(this, tr("Confirm irreversible action"),
                             tr("Tick the acknowledgement before submitting a burn."));
        return;
    }

    const CAmount amount = amountField->value();
    // FLOOF_MIN_BURN_250000: display friendly GUI validation before unlock.
    const int nextHeight = clientModel ? clientModel->getNumBlocks() + 1 : 0;
    const Consensus::Params& burnConsensus = Params().GetConsensus(nextHeight);
    if (FloofyBurnAmountBelowMinimum(amount, burnConsensus, nextHeight)) {
        QMessageBox::warning(this, tr("Minimum burn amount not met"),
            tr("Minimum burn amount not met. You must burn at least %1.")
                .arg(formatAmount(burnConsensus.nMinimumBurnAmount)));
        updateAvailable();
        return;
    }
    if (walletModel->getBalance() < amount) {
        QMessageBox::warning(this, tr("Insufficient funds"),
                             tr("The burn amount exceeds your spendable balance. "
                                "The transaction fee also requires funds."));
        return;
    }

    WalletModel::UnlockContext context = walletModel->requestUnlock();
    if (!context.isValid()) {
        previewLabel->setText(tr("Wallet unlock cancelled or failed."));
        return;
    }
    unlockContext.reset(new WalletModel::UnlockContext(context));
    commitRequested = commitAfterConfirmation;
    executingCommit = false;
    leaderboardAddressField->setEnabled(false);
    amountField->setEnabled(false);
    messageField->setEnabled(false);
    acknowledgeBox->setEnabled(false);
    previewButton->setEnabled(false);
    burnButton->setEnabled(false);
    resultLabel->clear();
    resultLabel->setVisible(false);
    lastTxid.clear();
    copyButton->setEnabled(false);
    copyButton->setVisible(false);
    previewLabel->setText(tr("Preparing burn preview; nothing has been broadcast…"));
    beginWorker(true);
}

void BurnPage::beginWorker(bool preview)
{
    worker = new FloofyBurnWorker(amountField->value(), messageField->text(), preview,
                                  leaderboardAddressField->currentData().toString(), this);
    connect(worker, SIGNAL(finished()), this, SLOT(workerFinished()));
    worker->start();
}

void BurnPage::finishOperation()
{
    unlockContext.reset();
    leaderboardAddressField->setEnabled(true);
    amountField->setEnabled(true);
    messageField->setEnabled(true);
    acknowledgeBox->setEnabled(true);
    previewButton->setEnabled(true);
    burnButton->setEnabled(true);
    commitRequested = false;
    executingCommit = false;
    updateAvailable();
}

void BurnPage::workerFinished()
{
    if (!worker)
        return;
    FloofyBurnWorker* completed = worker;
    worker = nullptr;
    const QString error = completed->error;
    const UniValue reply = completed->result;
    completed->deleteLater();

    if (!error.isEmpty() || !reply.isObject()) {
        previewLabel->setText(tr("Burn not submitted. Check wallet history if unsure."));
        QMessageBox::warning(this, tr("Burn RPC failed"),
                             error.isEmpty() ? tr("Unexpected burn RPC result") : error);
        finishOperation();
        return;
    }

    if (!executingCommit) {
        const UniValue amount = find_value(reply, "amount");
        const UniValue fee = find_value(reply, "fee");
        const UniValue walletCost = find_value(reply, "wallet_cost");
        const UniValue isPreview = find_value(reply, "preview");
        if (!amount.isNum() || !fee.isNum() || !walletCost.isNum() ||
            !isPreview.isBool() || !isPreview.get_bool()) {
            previewLabel->setText(tr("Unexpected burn preview response."));
            finishOperation();
            return;
        }

        QString text;
        try {
            text = tr("Amount permanently burned: %1\n"
                      "Estimated fee: %2\n"
                      "Estimated total wallet cost: %3\n"
                      "Message: %4")
                .arg(formatAmount(AmountFromValue(amount)))
                .arg(formatAmount(AmountFromValue(fee)))
                .arg(formatAmount(AmountFromValue(walletCost)))
                .arg(messageField->text().isEmpty() ? tr("(none)") : messageField->text());
        } catch (const std::exception& ex) {
            previewLabel->setText(tr("Unable to display burn preview."));
            QMessageBox::warning(this, tr("Burn preview error"), QString::fromUtf8(ex.what()));
            finishOperation();
            return;
        } catch (...) {
            previewLabel->setText(tr("Unable to display burn preview."));
            finishOperation();
            return;
        }
        previewLabel->setText(text);

        if (!commitRequested) {
            finishOperation();
            return;
        }
        QMessageBox confirmation(QMessageBox::Warning,
                                 tr("Confirm permanent FLOOF burn"),
                                 tr("%1\n\nThis operation is IRREVERSIBLE. "
                                    "Your coins cannot be recovered. The fee may change "
                                    "slightly before broadcast.\n\nProceed?").arg(text),
                                 QMessageBox::NoButton, this);
        confirmation.setTextFormat(Qt::PlainText);
        QPushButton* confirmButton = confirmation.addButton(
            tr("Burn permanently"), QMessageBox::AcceptRole);
        confirmation.addButton(QMessageBox::Cancel);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.exec();
        if (confirmation.clickedButton() != confirmButton) {
            previewLabel->setText(tr("Burn cancelled — nothing was broadcast."));
            finishOperation();
            return;
        }

        executingCommit = true;
        previewLabel->setText(tr("Submitting burn transaction to the network…"));
        beginWorker(false);
        return;
    }

    const UniValue committed = find_value(reply, "committed");
    const UniValue txid = find_value(reply, "txid");
    if (!committed.isBool() || !committed.get_bool() || !txid.isStr()) {
        previewLabel->setText(tr("Burn returned an unexpected result. "
                                 "Check transaction history before retrying."));
        finishOperation();
        return;
    }

    lastTxid = QString::fromStdString(txid.get_str());
    previewLabel->setText(tr("Burn transaction submitted! It is pending confirmation."));
    resultLabel->setText(tr("Transaction ID: %1").arg(lastTxid));
    resultLabel->setVisible(true);
    copyButton->setEnabled(true);
    copyButton->setVisible(true);
    acknowledgeBox->setChecked(false);
    amountField->clear();
    messageField->clear();
    finishOperation();
    refreshLeaderboard();
}

void BurnPage::copyTransactionId()
{
    if (!lastTxid.isEmpty())
        QApplication::clipboard()->setText(lastTxid);
}

void BurnPage::refreshLeaderboardAddresses()
{
    if (!leaderboardAddressField) return;
    const QString previous = leaderboardAddressField->currentData().toString();
    leaderboardAddressField->clear();
    leaderboardAddressField->addItem(tr("Standard burn (no leaderboard attribution)"), QString());
    if (!walletModel) return;
    try {
        JSONRPCRequest request;
        request.strMethod = "listunspent";
        request.params = UniValue(UniValue::VARR);
        request.params.push_back(1);
        const UniValue outputs = tableRPC.execute(request);
        if (!outputs.isArray()) return;
        QSet<QString> known;
        for (const UniValue& coin : outputs.getValues()) {
            const UniValue address = find_value(coin, "address");
            const UniValue spendable = find_value(coin, "spendable");
            if (!address.isStr() || (spendable.isBool() && !spendable.get_bool())) continue;
            const QString a = QString::fromStdString(address.get_str());
            if (known.contains(a)) continue;
            known.insert(a);
            leaderboardAddressField->addItem(a, a);
        }
        const int index = leaderboardAddressField->findData(previous);
        if (index >= 0) leaderboardAddressField->setCurrentIndex(index);
    } catch (...) {
        // The normal burn workflow remains available without an address.
    }
}

void BurnPage::refreshLeaderboard()
{
    if (leaderboardWorker || !leaderboardPeriod || !leaderboardTable) return;
    leaderboardStatus->setText(tr("Checking confirmed burns…"));
    leaderboardWorker = new FloofyLeaderboardWorker(leaderboardPeriod->currentData().toInt(), this);
    connect(leaderboardWorker, SIGNAL(finished()), this, SLOT(leaderboardFinished()));
    leaderboardWorker->start();
}

void BurnPage::leaderboardFinished()
{
    if (!leaderboardWorker) return;
    FloofyLeaderboardWorker* done = leaderboardWorker;
    leaderboardWorker = nullptr;
    const UniValue reply = done->result;
    const QString err = done->error;
    done->deleteLater();
    if (!err.isEmpty() || !reply.isObject()) {
        leaderboardStatus->setText(err.isEmpty() ? tr("Leaderboard unavailable") : err);
        return;
    }
    const UniValue rows = find_value(reply, "leaderboard");
    if (!rows.isArray()) {
        leaderboardStatus->setText(tr("Unexpected leaderboard result"));
        return;
    }
    leaderboardTable->setRowCount(0);
    for (const UniValue& entry : rows.getValues()) {
        if (!entry.isObject()) continue;
        const UniValue rank = find_value(entry, "rank");
        const UniValue addr = find_value(entry, "address");
        const UniValue amount = find_value(entry, "amount");
        const UniValue burns = find_value(entry, "burns");
        const UniValue txid = find_value(entry, "latest_txid");
        const UniValue message = find_value(entry, "latest_message");
        if (!rank.isNum() || !addr.isStr() || !amount.isNum() || !burns.isNum()) continue;
        const int row = leaderboardTable->rowCount();
        leaderboardTable->insertRow(row);
        const QString fields[] = {
            QString::number(rank.get_int()),
            QString::fromStdString(addr.get_str()),
            QString::fromStdString(amount.write()),
            QString::number(burns.get_int()),
            txid.isStr() ? QString::fromStdString(txid.get_str()) : QString(),
            message.isStr() ? QString::fromStdString(message.get_str()) : QString()
        };
        for (int col = 0; col < 6; ++col)
            leaderboardTable->setItem(row, col, new QTableWidgetItem(fields[col]));
    }
    const UniValue height = find_value(reply, "height");
    leaderboardStatus->setText(tr("Verified on-chain • Height %1 • %2 ranked addresses")
        .arg(height.isNum() ? height.get_int() : 0)
        .arg(leaderboardTable->rowCount()));
}
