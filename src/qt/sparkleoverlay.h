// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FLOOFY_QT_SPARKLEOVERLAY_H
#define FLOOFY_QT_SPARKLEOVERLAY_H

#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>

/**
 * Decorative, mouse-transparent twinkles over the central wallet view.
 * No blockchain, networking, RPC, or wallet data is accessed.
 * No Q_OBJECT is needed: the timer uses a Qt 5 lambda connection.
 */
class SparkleOverlay : public QWidget
{
public:
    explicit SparkleOverlay(QWidget* host);
    ~SparkleOverlay() override;

    void setSparkling(bool enabled);
    bool isSparkling() const { return sparkling; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct Star {
        QPointF position; // normalized [0..1] within the wallet view
        qreal radius;
        qreal phase;
        qreal speed;
        int tint;
    };

    QWidget* host;
    QTimer timer;
    QElapsedTimer elapsed;
    QVector<Star> stars;
    bool sparkling;

    void alignToHost();
};

#endif // FLOOFY_QT_SPARKLEOVERLAY_H
