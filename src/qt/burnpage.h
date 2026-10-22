// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef FLOOFY_QT_BURNPAGE_H
#define FLOOFY_QT_BURNPAGE_H

#include "walletmodel.h"
#include <QWidget>
#include <memory>

class BitcoinAmountField;
class ClientModel;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class FloofyBurnWorker;

/** Preview, confirm, and broadcast provable burns using the existing RPC. */
class BurnPage : public QWidget
{
    Q_OBJECT
public:
    explicit BurnPage(QWidget* parent = nullptr);
    ~BurnPage();

    void setClientModel(ClientModel* model);
    void setWalletModel(WalletModel* model);

private Q_SLOTS:
    void previewClicked();
    void burnClicked();
    void workerFinished();
    void updateAvailable();
    void copyTransactionId();

private:
    void beginPreview(bool commitAfterConfirmation);
    void beginWorker(bool preview);
    void finishOperation();
    QString formatAmount(CAmount amount) const;

    ClientModel* clientModel;
    WalletModel* walletModel;
    BitcoinAmountField* amountField;
    QLineEdit* messageField;
    QCheckBox* acknowledgeBox;
    QLabel* availableLabel;
    QLabel* minimumLabel; // FLOOF_MIN_BURN_250000
    QLabel* previewLabel;
    QLabel* resultLabel;
    QPushButton* previewButton;
    QPushButton* burnButton;
    QPushButton* copyButton;
    FloofyBurnWorker* worker;
    std::unique_ptr<WalletModel::UnlockContext> unlockContext;
    bool commitRequested;
    bool executingCommit;
    QString lastTxid;
};

#endif // FLOOFY_QT_BURNPAGE_H
