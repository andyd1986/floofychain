// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "utilitydialog.h"

#include "ui_helpmessagedialog.h"
#include "ui_paperwalletdialog.h"

#include "bitcoinunits.h"

#ifdef ENABLE_WALLET
#include "sendcoinsdialog.h"
#include "sendcoinsentry.h"
#include "coincontroldialog.h"
#endif

#include "optionsmodel.h"
#include "bitcoingui.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "intro.h"
#include "paymentrequestplus.h"
#include "guiutil.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "net.h"
#include "utilstrencodings.h"

#include <stdio.h>

#include <QCloseEvent>
#include <QImage>
#include <QVariant>
#include <QPixmap>
#include <QComboBox>
#include <QFontMetrics>
#include <QPrinter>
#include <QPrintDialog>
#include <QMessageBox>
#include <QStringList>
#include <QtGlobal>

#include <QApplication>
#include <QDesktopWidget>
#include <QFont>
#include <QLabel>
#include <QRegExp>
#include <QTextTable>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QInputDialog>

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrintPreviewDialog>
#include <QtPrintSupport/QPrinterInfo>
#include <QPainter>
#include "walletmodel.h"

/** "Help message" or "About" dialog box */
HelpMessageDialog::HelpMessageDialog(QWidget *parent, bool about) :
    QDialog(parent),
    ui(new Ui::HelpMessageDialog)
{
    ui->setupUi(this);

    QString version = tr(PACKAGE_NAME) + " " + tr("version") + " " + QString::fromStdString(FormatFullVersion());
    /* On x86 add a bit specifier to the version so that users can distinguish between
     * 32 and 64 bit builds. On other architectures, 32/64 bit may be more ambiguous.
     */
#if defined(__x86_64__)
    version += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__ )
    version += " " + tr("(%1-bit)").arg(32);
#endif

    if (about)
    {
        setWindowTitle(tr("About %1").arg(tr(PACKAGE_NAME)));

        /// HTML-format the license message from the core
        QString licenseInfo = QString::fromStdString(LicenseInfo());
        QString licenseInfoHTML = licenseInfo;
        // Make URLs clickable
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true); // use non-greedy matching
        licenseInfoHTML.replace(uri, "<a href=\"\\1\">\\1</a>");
        // Replace newlines with HTML breaks
        licenseInfoHTML.replace("\n", "<br>");

        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        text = version + "\n" + licenseInfo;
        // FLOOFY_V4_BLUE_ART_PACK: community mascot artwork above license text.
        const QString floofyV4Art = QStringLiteral(
            "<div style=\"text-align:center; padding:6px;\">"
            "<img src=\":/floofy-v4/floofy-friends\" width=\"390\" />"
            "<p style=\"color:#277DB7; font-weight:bold;\">What a bunch of floof!</p>"
            "</div>");
        ui->aboutMessage->setText(floofyV4Art + version + "<br><br>" + licenseInfoHTML);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
    } else {
        setWindowTitle(tr("Command-line options"));
        QString header = tr("Usage:") + "\n" +
            "  floofy-qt [" + tr("command-line options") + "]                     " + "\n";
        QTextCursor cursor(ui->helpMessage->document());
        cursor.insertText(version);
        cursor.insertBlock();
        cursor.insertText(header);
        cursor.insertBlock();

        std::string strUsage = HelpMessage(HMM_BITCOIN_QT);
        const bool showDebug = GetBoolArg("-help-debug", false);
        strUsage += HelpMessageGroup(tr("UI Options:").toStdString());
        if (showDebug) {
            strUsage += HelpMessageOpt("-allowselfsignedrootcertificates", strprintf("Allow self signed root certificates (default: %u)", DEFAULT_SELFSIGNED_ROOTCERTS));
        }
        strUsage += HelpMessageOpt("-choosedatadir", strprintf(tr("Choose data directory on startup (default: %u)").toStdString(), DEFAULT_CHOOSE_DATADIR));
        strUsage += HelpMessageOpt("-lang=<lang>", tr("Set language, for example \"de_DE\" (default: system locale)").toStdString());
        strUsage += HelpMessageOpt("-min", tr("Start minimized").toStdString());
        strUsage += HelpMessageOpt("-rootcertificates=<file>", tr("Set SSL root certificates for payment request (default: -system-)").toStdString());
        strUsage += HelpMessageOpt("-splash", strprintf(tr("Show splash screen on startup (default: %u)").toStdString(), DEFAULT_SPLASHSCREEN));
        strUsage += HelpMessageOpt("-resetguisettings", tr("Reset all settings changed in the GUI").toStdString());
        if (showDebug) {
            strUsage += HelpMessageOpt("-uiplatform", strprintf("Select platform to customize UI for (one of windows, macosx, other; default: %s)", BitcoinGUI::DEFAULT_UIPLATFORM));
        }
        QString coreOptions = QString::fromStdString(strUsage);
        text = version + "\n" + header + "\n" + coreOptions;

        QTextTableFormat tf;
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellPadding(2);
        QVector<QTextLength> widths;
        widths << QTextLength(QTextLength::PercentageLength, 35);
        widths << QTextLength(QTextLength::PercentageLength, 65);
        tf.setColumnWidthConstraints(widths);

        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);

        Q_FOREACH (const QString &line, coreOptions.split("\n")) {
            if (line.startsWith("  -"))
            {
                cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::PreviousCell);
                cursor.movePosition(QTextCursor::NextRow);
                cursor.insertText(line.trimmed());
                cursor.movePosition(QTextCursor::NextCell);
            } else if (line.startsWith("   ")) {
                cursor.insertText(line.trimmed()+' ');
            } else if (line.size() > 0) {
                //Title of a group
                if (cursor.currentTable())
                    cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::Down);
                cursor.insertText(line.trimmed(), bold);
                cursor.insertTable(1, 2, tf);
            }
        }

        ui->helpMessage->moveCursor(QTextCursor::Start);
        ui->scrollArea->setVisible(false);
        ui->aboutLogo->setVisible(false);
    }
}

HelpMessageDialog::~HelpMessageDialog()
{
    delete ui;
}

void HelpMessageDialog::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    fprintf(stdout, "%s\n", qPrintable(text));
}

void HelpMessageDialog::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}

void HelpMessageDialog::on_okButton_accepted()
{
    close();
}

/** "PaperWallet" dialog box */
PaperWalletDialog::PaperWalletDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PaperWalletDialog)
{
    // FLOOFY_V4_PAPERWALLET_10_PAGES_6_PER_PAGE_V6
    // Keep preview scrollable while all print controls remain visible.
    ui->setupUi(this);
    ui->buttonBox->addButton(tr("Close"), QDialogButtonBox::RejectRole);

    ui->paperScroll->setWidgetResizable(false);
    ui->paperTemplate->setScaledContents(true);
    ui->addressQRCode->setScaledContents(false);
    ui->privateKeyQRCode->setScaledContents(false);
    ui->addressQRCode->setAlignment(Qt::AlignCenter);
    ui->privateKeyQRCode->setAlignment(Qt::AlignCenter);

    QFont mono("DejaVu Sans Mono");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPixelSize(12);
    mono.setBold(true);
    ui->addressText->setFont(mono);
    ui->privateKeyText->setFont(mono);
    ui->addressText->setWordWrap(true);
    ui->privateKeyText->setWordWrap(true);
    ui->addressText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->privateKeyText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->walletsPerPageCombo->setCurrentIndex(1); // 2 different wallets / A4 sheet

    const QRect available = QApplication::desktop()->availableGeometry(this);
    const int width = qMax(410, qMin(590, available.width()-48));
    const int height = qMax(390, qMin(650, available.height()-80));
    resize(width, height);
    move(available.center() - rect().center());
}

void PaperWalletDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    // FIXME: This cannot be the right way of doing something on open
    if (_clientModel && _clientModel->getNetworkActive()) {
        QMessageBox::critical(this, tr("Warning: Network Activity Detected"), tr("It is recommended to disconnect from the internet before printing paper wallets. Even though paper wallets are generated on your local computer, it is still possible to unknowingly have malware that transmits your screen to a remote location. It is also recommended to print to a local printer vs a network printer since that network traffic can be monitored. Some advanced printers also store copies of each printed document. Proceed with caution relative to the amount of value you plan to store on each address."), QMessageBox::Ok, QMessageBox::Ok);
    }
}

void PaperWalletDialog::setModel(WalletModel *model)
{
    RandAddSeed();
    this->model = model;
    this->on_getNewAddress_clicked();
}

PaperWalletDialog::~PaperWalletDialog()
{
    delete ui;
}

void PaperWalletDialog::on_getNewAddress_clicked()
{
    // FLOOFY_V4_PAPERWALLET_10_PAGES_6_PER_PAGE_V6
    // Preserve the original offline paper-key creation path.
    CKey privKey;
    privKey.MakeNewKey(true);
    const CPubKey pubkey = privKey.GetPubKey();
    CBitcoinAddress pubkeyhash;
    pubkeyhash.Set(pubkey.GetID());
    const std::string myPrivKey = CBitcoinSecret(privKey).ToString();
    const std::string myPubKey = HexStr(pubkey.begin(), pubkey.end());
    const std::string myAddress = pubkeyhash.ToString();

    const QString address = QString::fromStdString(myAddress);
    const QString wif = QString::fromStdString(myPrivKey);
    ui->addressText->setProperty("floofyRawAddress", address);
    ui->privateKeyText->setProperty("floofyRawWif", wif);
    // Split at fixed character boundaries for legible printed text. The
    // underlying raw strings above never lose any characters.
    const int aSplit=(address.length()+1)/2;
    const int kSplit=(wif.length()+1)/2;
    ui->addressText->setText(address.left(aSplit)+QStringLiteral("\n")+address.mid(aSplit));
    ui->privateKeyText->setText(wif.left(kSplit)+QStringLiteral("\n")+wif.mid(kSplit));
    ui->publicKey->setPlainText(QString::fromStdString(myPubKey));

    ui->addressQRCode->clear();
    ui->privateKeyQRCode->clear();
    ui->addressQRCode->setProperty("floofyRawQr", QVariant());
    ui->privateKeyQRCode->setProperty("floofyRawQr", QVariant());

#ifdef USE_QRCODE
    // Four-module white quiet zones. Keep the unscaled QImage for the printer
    // and use the same exact QR payload in the screen preview.
    auto generateQR=[](QLabel* target, const std::string& contents)->bool {
        QRcode *qr=QRcode_encodeString(contents.c_str(),0,QR_ECLEVEL_M,QR_MODE_8,1);
        if (!qr || qr->width<=0) {
            if (qr) QRcode_free(qr);
            target->setText(QObject::tr("QR unavailable"));
            return false;
        }
        const int border=4;
        const int modules=qr->width;
        QImage image(modules+2*border,modules+2*border,QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y=0;y<modules;++y)
            for (int x=0;x<modules;++x)
                image.setPixel(x+border,y+border,
                    (qr->data[y*modules+x]&1)?qRgb(0,0,0):qRgb(255,255,255));
        QRcode_free(qr);
        target->setProperty("floofyRawQr", QVariant::fromValue(image));
        target->setPixmap(QPixmap::fromImage(image).scaled(
            target->size(),Qt::KeepAspectRatio,Qt::FastTransformation));
        return true;
    };
    if (!generateQR(ui->addressQRCode,myAddress) ||
        !generateQR(ui->privateKeyQRCode,myPrivKey)) {
        ui->addressQRCode->clear();
        ui->privateKeyQRCode->clear();
        ui->addressQRCode->setProperty("floofyRawQr", QVariant());
        ui->privateKeyQRCode->setProperty("floofyRawQr", QVariant());
    }
#else
    // Printing is refused when the required QR-code feature is not compiled.
    ui->addressQRCode->setText(tr("QR support disabled"));
    ui->privateKeyQRCode->setText(tr("QR support disabled"));
#endif
}

void PaperWalletDialog::on_printButton_clicked()
{
    // FLOOFY_V4_PAPERWALLET_10_PAGES_6_PER_PAGE_V6
    // Dogecoin/Floofy-style page count is separate from wallets/page.
    // Example: 5 A4 pages x 4 wallets/page = 20 unique paper wallets.
    const int pageCount = ui->walletCount->currentIndex()+1; // 1..10
    const int walletsPerPage = ui->walletsPerPageCombo->currentIndex()+1; // 1..6
    if (pageCount<1 || pageCount>10 || walletsPerPage<1 || walletsPerPage>6) {
        QMessageBox::critical(this,tr("Paper wallet settings"),
            tr("Select 1 to 10 A4 pages and 1 to 6 wallets per page."));
        return;
    }
    const int totalWallets = pageCount * walletsPerPage;

    const int columns = walletsPerPage<=2 ? 1 : 2;
    const int rows = (walletsPerPage+columns-1)/columns;
    const qreal maxMM = (walletsPerPage==1)?155.0:
                        (walletsPerPage==2)?127.0:
                        (walletsPerPage<=4)?94.0:89.0;

    auto fullAddress=[&]()->QString {
        return ui->addressText->property("floofyRawAddress").toString();
    };
    auto validWallet=[&]()->bool {
        const QImage publicQR=ui->addressQRCode->property("floofyRawQr").value<QImage>();
        const QImage privateQR=ui->privateKeyQRCode->property("floofyRawQr").value<QImage>();
        return !fullAddress().isEmpty() &&
            !ui->privateKeyText->property("floofyRawWif").toString().isEmpty() &&
            !publicQR.isNull() && !privateQR.isNull();
    };
    if (!validWallet()) {
        QMessageBox::critical(this,tr("Paper wallet unavailable"),
            tr("The address, private key, or QR code is unavailable. "
               "Build the wallet with QR code support."));
        return;
    }
    const QPixmap templateImage(QStringLiteral(":/icons/paper_wallet"));
    if (templateImage.isNull()) {
        QMessageBox::critical(this,tr("Paper wallet missing"),
            tr("Circular paper-wallet image not found in Qt resources."));
        return;
    }

    if (walletsPerPage>=5 && QMessageBox::warning(this,
        tr("Small paper wallets"),
        tr("Five or six circles per A4 sheet produce smaller QR codes and "
           "private-key lettering. Verify every code with an offline scanner "
           "before transferring FLOOF. Continue?"),
        QMessageBox::Yes | QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
        return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::A4);
    printer.setOrientation(QPrinter::Portrait);
    printer.setFullPage(true);
    QPrintDialog dialog(&printer,this);
    dialog.setPrintRange(QAbstractPrintDialog::AllPages);
    if (dialog.exec()!=QDialog::Accepted) return;

    // Do not let a print-dialog paper/orientation change silently clip circles.
    printer.setPageSize(QPrinter::A4);
    printer.setOrientation(QPrinter::Portrait);
    printer.setFullPage(true);
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::critical(this,tr("Printing Error"),
            tr("Unable to initialise the selected printer."));
        return;
    }
    const QRect page=printer.pageRect();
    const qreal ppm=static_cast<qreal>(printer.resolution())/25.4;
    const qreal margin=8.0*ppm;
    const qreal gap=5.0*ppm;
    const qreal maxW=page.width()-2*margin-(columns-1)*gap;
    const qreal maxH=page.height()-2*margin-(rows-1)*gap;
    const qreal diameter=qMin(maxMM*ppm,qMin(maxW/columns,maxH/rows));
    if (diameter<68.0*ppm) {
        painter.end();
        QMessageBox::critical(this,tr("Printing Error"),
            tr("Selected A4 printable area is too small for this layout."));
        return;
    }
    const qreal gridW=columns*diameter+(columns-1)*gap;
    const qreal gridH=rows*diameter+(rows-1)*gap;
    const qreal startX=(page.width()-gridW)/2.0;
    const qreal startY=(page.height()-gridH)/2.0;
    const qreal logicalSize=1000.0; // The static PNG is larger, printed losslessly.
    const qreal scale=diameter/logicalSize;

    auto drawQR=[&](QLabel* label,const QRectF& zone) {
        const QImage raw=label->property("floofyRawQr").value<QImage>();
        painter.setRenderHint(QPainter::SmoothPixmapTransform,false);
        // Four white QR modules are encoded in raw; nearest-neighbour output.
        painter.drawImage(zone,raw);
    };
    auto drawText=[&](const QString& content,const QRectF& zone,
                      int pixels,bool strong) {
        QFont mono(strong?QStringLiteral("Sans Serif"):
                          QStringLiteral("DejaVu Sans Mono"));
        mono.setStyleHint(strong?QFont::SansSerif:QFont::TypeWriter);
        mono.setBold(true);
        mono.setPixelSize(pixels);
        // Fit the longest exact address/WIF line before painting. Never
        // truncate a private key if a platform's monospace font differs.
        const QStringList lines=content.split(QChar('\n'));
        while (mono.pixelSize()>16) {
            QFontMetrics metrics(mono);
            bool fits=true;
            for (const QString& line : lines) {
                if (metrics.width(line)>zone.width()-8) {fits=false;break;}
            }
            if (fits) break;
            mono.setPixelSize(mono.pixelSize()-1);
        }
        painter.setFont(mono);
        painter.setPen(QColor("#123A5E"));
        painter.setRenderHint(QPainter::TextAntialiasing,true);
        painter.drawText(zone,Qt::AlignLeft|Qt::AlignVCenter|Qt::TextWordWrap,content);
    };
    QList<QString> recipientPubKeyHashes;
    // Each page is filled completely with a separate new keypair in each slot.
    for (int sheet=0;sheet<pageCount;++sheet) {
        if (sheet>0 && !printer.newPage()) {
            painter.end();
            QMessageBox::critical(this,tr("Printing Error"),
                tr("Unable to advance to the next A4 sheet."));
            return;
        }
        for (int slot=0;slot<walletsPerPage;++slot) {
            if (!validWallet()) {
                painter.end();
                QMessageBox::critical(this,tr("Printing Error"),
                    tr("Unable to generate a complete paper wallet."));
                return;
            }
            const int row=slot/columns;
            const int column=slot%columns;
            const int countInRow=qMin(columns,walletsPerPage-row*columns);
            const qreal rowOffset=(columns-countInRow)*(diameter+gap)/2.0;
            painter.save();
            painter.translate(startX+rowOffset+column*(diameter+gap),
                              startY+row*(diameter+gap));
            painter.scale(scale,scale);
            painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
            painter.drawPixmap(QRectF(0,0,1000,1000),templateImage,
                QRectF(0,0,templateImage.width(),templateImage.height()));
            // These positions are the enlarged white panels in the bundled PNG.
            drawQR(ui->addressQRCode,QRectF(184,140,245,245));
            drawText(tr("PUBLIC ADDRESS"),QRectF(461,196,385,27),22,true);
            drawText(ui->addressText->text(),QRectF(461,229,385,94),26,false);
            drawText(tr("PRIVATE KEY - KEEP SECRET"),QRectF(180,731,363,27),20,true);
            drawText(ui->privateKeyText->text(),QRectF(180,765,363,92),24,false);
            drawQR(ui->privateKeyQRCode,QRectF(572,675,212,212));
            painter.restore();

            // Pass the FULL original receiving address into the wallet's
            // unchanged optional post-print funding/transfer routine.
            recipientPubKeyHashes.append(fullAddress());
            const int processed=sheet*walletsPerPage+slot+1;
            if (processed<totalWallets)
                this->on_getNewAddress_clicked();
        }
    }
    if (!painter.end()) {
        QMessageBox::critical(this,tr("Printing Error"),
            tr("Printer did not finish. Verify all wallets before funding."));
        return;
    }

#ifdef ENABLE_WALLET
    QStringList formatted;

    WalletModelTransaction* tx;
    while (true) {
        bool ok;

        // Ask for an amount to send to each paper wallet. It might be better to try to use the BitcoinAmountField, but this works fine.
        double amountInput = QInputDialog::getDouble(this, tr("Load Paper Wallets"), tr("The paper wallet printing process has begun.<br/>Please wait for the wallets to print completely and verify that everything printed correctly.<br/>Check for misalignments, ink bleeding, smears, or anything else that could make the private keys unreadable.<br/>Now, enter the number of FLOOF you wish to send to each wallet:"), 0, 0, 2147483647, 8, &ok);

        if (!ok) {
            return;
        }


        WalletModel::UnlockContext ctx(this->model->requestUnlock());
        if (!ctx.isValid()) {
            return;
        }

        QList<SendCoinsRecipient> recipients;
        quint64 amount = (quint64)(amountInput * COIN);
        Q_FOREACH (const QString& dest, recipientPubKeyHashes) {
            recipients.append(SendCoinsRecipient(dest, tr("Paper wallet %1").arg(dest), amount, ""));
            formatted.append(tr("<b>%1</b> to Paper Wallet <span style='font-family: monospace;'>%2</span>").arg(QString::number(amountInput, 'f', 8), GUIUtil::HtmlEscape(dest)));
        }

        tx = new WalletModelTransaction(recipients);

        WalletModel::SendCoinsReturn prepareStatus;
        if (this->model->getOptionsModel()->getCoinControlFeatures()) // coin control enabled
            prepareStatus = this->model->prepareTransaction(*tx, CoinControlDialog::coinControl);
        else
            prepareStatus = this->model->prepareTransaction(*tx);

        if (prepareStatus.status == WalletModel::InvalidAddress) {
            QMessageBox::critical(this, tr("Send Coins"), tr("The recipient address is not valid, please recheck."), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::InvalidAmount) {
            QMessageBox::critical(this, tr("Send Coins"), tr("The amount to pay must be larger than 0"), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::AmountExceedsBalance) {
            QMessageBox::critical(this, tr("Send Coins"), tr("The amount exceeds your balance."), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::AmountWithFeeExceedsBalance) {
            QMessageBox::critical(this, tr("Send Coins"), tr("The total exceeds your balance when the transaction fee is included"), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::DuplicateAddress) {
            QMessageBox::critical(this, tr("Send Coins"), tr("Duplicate address found, can only send to each address once per send operation."), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::TransactionCreationFailed) {
            QMessageBox::critical(this, tr("Send Coins"), tr("Transaction creation failed!"), QMessageBox::Ok, QMessageBox::Ok);
        } else if (prepareStatus.status == WalletModel::OK) {
            break;
        } else {
            delete tx;
            return;
        }
    }

    // Stolen from sendcoinsdialog.cpp
    qint64 txFee = tx->getTransactionFee();
    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    qint64 totalAmount = tx->getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    Q_FOREACH (BitcoinUnits::Unit u, BitcoinUnits::availableUnits()) {
        if (u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatWithUnit(u, totalAmount));
    }

    questionString.append(tr("Total Amount %1 (= %2)")
                              .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
                              .arg(alternativeUnits.join(" " + tr("or") + " ")));

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"), questionString.arg(formatted.join("<br />")), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) {
        delete tx;
        return;
    }

    WalletModel::SendCoinsReturn sendStatus = this->model->sendCoins(*tx);

    if (sendStatus.status == WalletModel::TransactionCommitFailed) {
        QMessageBox::critical(this, tr("Send Coins"), tr("The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here."), QMessageBox::Ok, QMessageBox::Ok);
    }
    delete tx;
#endif
    return;
}

/** "Shutdown" window */
ShutdownWindow::ShutdownWindow(QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(new QLabel(
        tr("%1 is shutting down...").arg(tr(PACKAGE_NAME)) + "<br /><br />" +
        tr("Do not shut down the computer until this window disappears.")));
    setLayout(layout);
}

QWidget *ShutdownWindow::showShutdownWindow(BitcoinGUI *window)
{
    if (!window)
        return nullptr;

    // Show a simple window indicating shutdown status
    QWidget *shutdownWindow = new ShutdownWindow();
    shutdownWindow->setWindowTitle(window->windowTitle());

    // Center shutdown window at where main window was
    const QPoint global = window->mapToGlobal(window->rect().center());
    shutdownWindow->move(global.x() - shutdownWindow->width() / 2, global.y() - shutdownWindow->height() / 2);
    shutdownWindow->show();
    return shutdownWindow;
}

void ShutdownWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
}
