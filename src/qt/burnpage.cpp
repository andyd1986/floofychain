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
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <exception>

// RPC may reserve keys, select coins and broadcast; do not block the Qt UI.
// Only results are read on the GUI thread after the worker has finished.
class FloofyBurnWorker : public QThread
{
public:
    FloofyBurnWorker(CAmount amount, const QString& message, bool preview, QObject* parent)
        : QThread(parent), amount_(amount), message_(message), preview_(preview) {}

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
};

BurnPage::BurnPage(QWidget* parent)
    : QWidget(parent),
      clientModel(nullptr),
      walletModel(nullptr),
      amountField(nullptr),
      messageField(nullptr),
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
    main->addStretch();

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
    worker = new FloofyBurnWorker(amountField->value(), messageField->text(), preview, this);
    connect(worker, SIGNAL(finished()), this, SLOT(workerFinished()));
    worker->start();
}

void BurnPage::finishOperation()
{
    unlockContext.reset();
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
}

void BurnPage::copyTransactionId()
{
    if (!lastTxid.isEmpty())
        QApplication::clipboard()->setText(lastTxid);
}
