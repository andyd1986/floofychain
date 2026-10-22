// Copyright (c) 2026 The FloofyChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sparkleoverlay.h"

#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRadialGradient>

#include <cmath>
#include <cstdint>

namespace {
static const int SPARKLE_COUNT = 17;
static const int FRAME_INTERVAL_MS = 85; // ~12 fps; deliberately low overhead

// Stable, non-cryptographic positions -- no use of the wallet RNG.
qreal nextFraction(uint32_t& state)
{
    state = 1664525u * state + 1013904223u;
    return static_cast<qreal>((state >> 8) & 0xffffu) / 65535.0;
}
}

SparkleOverlay::SparkleOverlay(QWidget* parentHost)
    : QWidget(parentHost),
      host(parentHost),
      sparkling(false)
{
    // Clicks, touch and wheel input always reach the original controls.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setObjectName("floofySparkles");

    uint32_t seed = 0xF100F123u;
    stars.reserve(SPARKLE_COUNT);
    for (int i = 0; i < SPARKLE_COUNT; ++i) {
        Star star;
        const qreal u = nextFraction(seed);
        const qreal v = nextFraction(seed);
        // Prefer the margins: shimmer is visible without obscuring text.
        switch (i % 4) {
        case 0: star.position = QPointF(0.06 + u * 0.88, 0.035 + v * 0.08); break;
        case 1: star.position = QPointF(0.06 + u * 0.88, 0.87 + v * 0.10); break;
        case 2: star.position = QPointF(0.025 + u * 0.07, 0.16 + v * 0.67); break;
        default: star.position = QPointF(0.89 + u * 0.08, 0.16 + v * 0.67); break;
        }
        star.radius = 2.2 + nextFraction(seed) * 2.0;
        star.phase = nextFraction(seed) * 6.283185307179586;
        star.speed = 0.7 + nextFraction(seed) * 1.15;
        star.tint = i % 3;
        stars.append(star);
    }

    timer.setInterval(FRAME_INTERVAL_MS);
    timer.setTimerType(Qt::CoarseTimer);
    connect(&timer, &QTimer::timeout, this, [this]() {
        if (sparkling && isVisible() && window() &&
            window()->isVisible() && !window()->isMinimized()) {
            update();
        }
    });
    elapsed.start();

    alignToHost();
    if (host)
        host->installEventFilter(this);
    hide();
}

SparkleOverlay::~SparkleOverlay()
{
    timer.stop();
    if (host)
        host->removeEventFilter(this);
}

void SparkleOverlay::alignToHost()
{
    if (host)
        setGeometry(host->rect());
}

void SparkleOverlay::setSparkling(bool enabled)
{
    sparkling = enabled;
    if (sparkling) {
        alignToHost();
        show();
        if (!timer.isActive())
            timer.start();
        update();
    } else {
        timer.stop();
        hide();
    }
}

bool SparkleOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == host && event->type() == QEvent::Resize)
        alignToHost();
    return QWidget::eventFilter(watched, event);
}

void SparkleOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!sparkling || width() < 180 || height() < 140)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // Compute phase from elapsed time instead of accumulating frames, so
    // minimizing the GUI never causes a burst of catch-up animations.
    const qreal seconds = static_cast<qreal>(elapsed.elapsed()) / 1000.0;
    for (const Star& star : stars) {
        const qreal wave = 0.5 + 0.5 * std::sin(seconds * star.speed + star.phase);
        const qreal intensity = wave * wave * wave;
        if (intensity < 0.045)
            continue;

        const QPointF center(star.position.x() * width(),
                             star.position.y() * height());
        const qreal radius = star.radius * (0.7 + 0.5 * wave);
        const qreal glowRadius = radius * 4.4;

        const QColor pastel = star.tint == 0 ? QColor(182, 169, 255) :
                              star.tint == 1 ? QColor(161, 221, 255) :
                                               QColor(255, 207, 244);
        QRadialGradient glow(center, glowRadius);
        QColor middle = pastel;
        middle.setAlpha(static_cast<int>(67 * intensity));
        QColor fade = pastel;
        fade.setAlpha(0);
        glow.setColorAt(0.0, QColor(255, 255, 255,
                                  static_cast<int>(135 * intensity)));
        glow.setColorAt(0.32, middle);
        glow.setColorAt(1.0, fade);
        painter.setBrush(QBrush(glow));
        painter.drawEllipse(center, glowRadius, glowRadius);

        // Small four-point star with a bright white core.
        const qreal outer = radius * 1.65;
        const qreal inner = radius * 0.27;
        QPainterPath shape;
        shape.moveTo(center.x(), center.y() - outer);
        shape.lineTo(center.x() + inner, center.y() - inner);
        shape.lineTo(center.x() + outer, center.y());
        shape.lineTo(center.x() + inner, center.y() + inner);
        shape.lineTo(center.x(), center.y() + outer);
        shape.lineTo(center.x() - inner, center.y() + inner);
        shape.lineTo(center.x() - outer, center.y());
        shape.lineTo(center.x() - inner, center.y() - inner);
        shape.closeSubpath();
        QColor ink = pastel.lighter(160);
        ink.setAlpha(static_cast<int>(220 * intensity));
        painter.fillPath(shape, ink);
        painter.setBrush(QColor(255, 255, 255,
                                static_cast<int>(220 * intensity)));
        painter.drawEllipse(center, radius * 0.29, radius * 0.29);
    }
}
