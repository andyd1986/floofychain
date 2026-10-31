// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020-2022 The Floofy Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "splashscreen.h"

#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include <QLinearGradient>
#include <QFont>
#include <QLinearGradient>
#include <QFont>
#include <QRadialGradient>

#include <boost/bind/bind.hpp>

SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0)
{
    // FLOOFY_V4_GROUP_ART_PATCH_2026: original Floofy group art + donations.
    const QSize logicalSize(980, 590);
    qreal ratio = 1.0;
#if QT_VERSION > 0x050100
    ratio = ((QGuiApplication*)QCoreApplication::instance())->devicePixelRatio();
#endif
    pixmap = QPixmap(QSize(qRound(logicalSize.width() * ratio),
                           qRound(logicalSize.height() * ratio)));
#if QT_VERSION > 0x050100
    pixmap.setDevicePixelRatio(ratio);
#endif
    pixmap.fill(Qt::transparent);
    QPainter paint(&pixmap);
    paint.setRenderHint(QPainter::Antialiasing, true);
    paint.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QLinearGradient background(0, 0, 980, 590);
    background.setColorAt(0.0, QColor("#F0FAFF"));
    background.setColorAt(0.58, QColor("#BDE9FF"));
    background.setColorAt(1.0, QColor("#6ABDF4"));
    paint.fillRect(QRect(QPoint(0, 0), logicalSize), background);
    paint.setPen(Qt::NoPen);
    paint.setBrush(QColor(255, 255, 255, 80));
    paint.drawEllipse(QRect(24, 17, 290, 205));
    paint.drawEllipse(QRect(752, 27, 130, 130));

    paint.setBrush(QColor(255, 255, 255, 240));
    paint.setPen(QPen(QColor("#B5DDF6"), 1));
    paint.drawRoundedRect(QRect(28, 25, 924, 510), 24, 24);

    paint.setPen(QColor("#163F64"));
    paint.setFont(QFont("Sans Serif", 31, QFont::Bold));
    paint.drawText(QRect(65, 45, 570, 55), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("FloofyChain"));
    paint.setPen(QColor("#247EBF"));
    paint.setFont(QFont("Sans Serif", 13, QFont::DemiBold));
    paint.drawText(QRect(67, 96, 600, 30), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("V4  |  BLUE & WHITE WALLET"));

    const QString extra = networkStyle->getTitleAddText();
    if (!extra.isEmpty()) {
        paint.setPen(QColor("#C14E71"));
        paint.setFont(QFont("Sans Serif", 12, QFont::Bold));
        paint.drawText(QRect(714, 58, 204, 28), Qt::AlignRight | Qt::AlignVCenter, extra);
    }

    // Shared Floofy-group artwork also used by the tray / app badge.
    const QRect imagePanel(61, 153, 389, 332);
    paint.setBrush(QColor("#E7F7FF"));
    paint.setPen(QPen(QColor("#B7DEF5"), 1));
    paint.drawRoundedRect(imagePanel, 16, 16);
    QPixmap group(":/floofy-v4-group/group");
    if (!group.isNull()) {
        const QPixmap scaled = group.scaled(QSize(367, 319),
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        paint.drawPixmap(61 + (389 - scaled.width()) / 2,
                         155 + (330 - scaled.height()) / 2, scaled);
    }
    paint.setPen(QColor("#356D95"));
    paint.setFont(QFont("Sans Serif", 10));
    paint.drawText(QRect(67, 484, 378, 25), Qt::AlignCenter | Qt::AlignVCenter,
                   tr("Welcome, Floofonaut!"));

    // Real user-supplied donation addresses; no private keys or wallet data.
    const QRect donationCard(471, 153, 446, 349);
    paint.setBrush(QColor("#F6FBFF"));
    paint.setPen(QPen(QColor("#BBDFF5"), 1));
    paint.drawRoundedRect(donationCard, 16, 16);
    paint.setPen(QColor("#17547F"));
    paint.setFont(QFont("Sans Serif", 15, QFont::Bold));
    paint.drawText(QRect(493, 173, 397, 33), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("Support FloofyChain"));
    paint.setFont(QFont("Sans Serif", 10));
    paint.setPen(QColor("#54758C"));
    paint.drawText(QRect(492, 209, 399, 44),
                   Qt::AlignLeft | Qt::TextWordWrap,
                   tr("Donations help with wallet development and network infrastructure."));

    struct DonationRow { const char* coin; const char* address; };
    const DonationRow addresses[] = {
        {"DOGE", "DGpnrvtjnhaF866XKbVUpVAJsBJDKZdr6c"},
        {"LTC", "MCREWjsAVoNzzRAijPWfgRT26LniSBaLH1"},
        {"FLOOF", "FBgtAgpr7LsChn3dggAKSUrNiJhdRHSHAB"},
        {"BTC", "3GUraGiEyvgX58cU45qRme6fdYNPM5vN5i"},
        {"ETH", "0x70Ad65A242CB24cB648A7f81024EC0dA9db61024"}
    };
    for (int i = 0; i < 5; ++i) {
        const int y = 269 + i * 39;
        paint.setPen(QColor("#0A71B8"));
        paint.setFont(QFont("Sans Serif", 10, QFont::Bold));
        paint.drawText(QRect(494, y, 65, 22), Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromLatin1(addresses[i].coin));
        paint.setPen(QColor("#183F60"));
        paint.setFont(QFont("Monospace", 8));
        paint.drawText(QRect(557, y, 347, 22), Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromLatin1(addresses[i].address));
    }
    paint.setPen(QColor("#547A96"));
    paint.setFont(QFont("Sans Serif", 9));
    paint.drawText(QRect(497, 470, 396, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString("Version %1").arg(QString::fromStdString(FormatFullVersion())));

    // Keep the genuine startup message (including progress) visible at all times.
    paint.fillRect(QRect(0, 543, 980, 47), QColor("#17649F"));
    paint.end();

    setWindowTitle(tr(PACKAGE_NAME) + " " + extra);
    resize(logicalSize);
    setFixedSize(logicalSize);
    move(QApplication::desktop()->screenGeometry().center() - rect().center());
    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this,
                                             boost::placeholders::_1,
                                             boost::placeholders::_2));
    connectedWallets.push_back(wallet);
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this,
                                                boost::placeholders::_1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this,
                                                 boost::placeholders::_1,boost::placeholders::_2));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(&SplashScreen::ConnectWallet, this,
                                               boost::placeholders::_1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this,
                                                   boost::placeholders::_1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this,
                                                    boost::placeholders::_1,
                                                    boost::placeholders::_2));
#ifdef ENABLE_WALLET
    Q_FOREACH(CWallet* const & pwallet, connectedWallets) {
        pwallet->ShowProgress.disconnect(boost::bind(ShowProgress, this,
                                                     boost::placeholders::_1,
                                                     boost::placeholders::_2));
    }
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    // FLOOFY_V4_GROUP_ART_PATCH_2026: display real wallet/core startup messages.
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.drawPixmap(0, 0, pixmap);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Sans Serif", 11, QFont::DemiBold));
    const QString status = curMessage.isEmpty()
        ? tr("Starting FloofyChain...") : curMessage;
    painter.drawText(QRect(25, 548, width() - 50, 35),
                     Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
                     status);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
