#include "textstatistics.h"
#include "configure.h"

#include <QCache>
#include <QFontMetrics>
#include <QIconEngine>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <algorithm>
#include <cmath>

QString formatShortRate(double bytesPerSecond, bool useBits) {
    const char *units = useBits ? " kmgtp" : " KMGTP";
    const double base = useBits ? 1000.0 : 1024.0;
    double value = std::isfinite(bytesPerSecond) && bytesPerSecond > 0.0 ? bytesPerSecond : 0.0;
    if (useBits)
        value *= 8.0;
    int unit = 0;
    while (value >= base && unit < 5) {
        value /= base;
        ++unit;
    }
    if (value >= 999.5 && unit < 5) {
        value /= base;
        ++unit;
    }
    value = std::min(value, 999.0 + (unit < 5 ? 0.499 : 0.0));
    QString result = QString::number(value, 'f', unit > 0 && value < 9.95 ? 1 : 0);
    if (unit > 0)
        result += QLatin1Char(units[unit]);
    return result.rightJustified(4, QLatin1Char(' '));
}

namespace {
// Three pixels wide, five high; the identifier does not depend on a system font.
const unsigned char digits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

int maximumRateWidth(const QFontMetrics &metrics, bool useBits) {
    QChar widest = QLatin1Char('0');
    for (char c = '1'; c <= '9'; ++c)
        if (metrics.horizontalAdvance(QLatin1Char(c)) > metrics.horizontalAdvance(widest))
            widest = QLatin1Char(c);
    int width = metrics.horizontalAdvance(QString(3, widest));
    const char *units = useBits ? "kmgtp" : "KMGTP";
    for (int unit = 0; unit < 5; ++unit) {
        const char suffix = units[unit];
        width = std::max(width, metrics.horizontalAdvance(QString(3, widest) + QLatin1Char(suffix)));
        width = std::max(width, metrics.horizontalAdvance(QString(widest) + QLatin1Char('.') + widest + QLatin1Char(suffix)));
    }
    return width;
}

void drawRate(QPainter &painter, QRect area, QFont font, const QString &text, int shadowSize, bool useBits) {
    if (area.isEmpty())
        return;
    // Fit the largest size of the chosen family/style that accommodates every
    // short value. Font size stays constant when traffic changes.
    int bestSize = 1;
    for (int pixels = 1; pixels <= area.height(); ++pixels) {
        font.setPixelSize(pixels);
        const QFontMetrics metrics(font);
        if (metrics.height() <= area.height() && maximumRateWidth(metrics, useBits) <= area.width())
            bestSize = pixels;
    }
    font.setPixelSize(bestSize);
    painter.setFont(font);
    painter.save();
    painter.setClipRect(area);
    if (shadowSize > 0) {
        const QPen textPen = painter.pen();
        const QColor color = textPen.color();
        // A thin contrasting halo separates the glyph from the digit behind it.
        painter.setPen(qGray(color.rgb()) < 32 ? Qt::white : Qt::black);
        for (int y = -shadowSize; y <= shadowSize; y += shadowSize)
            for (int x = -shadowSize; x <= shadowSize; x += shadowSize)
                if (x != 0 || y != 0)
                    painter.drawText(area.translated(x, y), Qt::AlignRight | Qt::AlignVCenter, text.trimmed());
        painter.setPen(textPen);
    }
    painter.drawText(area, Qt::AlignRight | Qt::AlignVCenter, text.trimmed());
    painter.restore();
}
}

QImage renderTextStatistics(const ViewOptions &options, const QString &upload,
                           const QString &download, int size) {
    if (size < 8)
        return QImage();
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    if (options.mTextTransparentBackground)
        image.fill(Qt::transparent);
    else
        image.fill(QColor(options.mTextBackgroundColor));
    QPainter painter(&image);
    QRect uploadArea(0, 0, size, size / 2);
    QRect downloadArea(0, size / 2, size, size - size / 2);
    const int scale = std::max(1, size / 16);
    const bool right = options.mTextDigitPosition == 1 || options.mTextDigitPosition == 3;
    const bool bottom = options.mTextDigitPosition >= 2;
    const QPoint origin(right ? size - 3 * scale : 0, bottom ? size - 5 * scale : 0);
    // Both traffic rows use the full icon width, regardless of digit position.

    const int digit = std::clamp(options.mTextDigit, 0, 9);
    if (options.mTextShowDigit)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 3; ++x)
                if (digits[digit][y] & (1 << (2 - x)))
                    painter.fillRect(origin.x() + x * scale, origin.y() + y * scale,
                                     scale, scale, QColor(options.mTextDigitColor));

    painter.setRenderHint(QPainter::TextAntialiasing);
    const int shadowSize = options.mTextShadow ? scale : 0;
    painter.setPen(QColor(options.mTextUploadColor));
    drawRate(painter, uploadArea, options.mTextFont, upload, shadowSize, options.mTextUseBits);
    painter.setPen(QColor(options.mTextDownloadColor));
    drawRate(painter, downloadArea, options.mTextFont, download, shadowSize, options.mTextUseBits);
    return image;
}

namespace {
class TextStatisticsIconEngine final : public QIconEngine {
public:
    TextStatisticsIconEngine(const ViewOptions &options, const QString &upload,
                             const QString &download)
        : mOptions(options), mUpload(upload), mDownload(download) {}

    QIconEngine *clone() const override {
        return new TextStatisticsIconEngine(mOptions, mUpload, mDownload);
    }

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override {
        const int side = std::min(size.width(), size.height());
        return side >= 8 ? QSize(side, side) : QSize();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode, QIcon::State) override {
        return renderedPixmap(std::min(size.width(), size.height()));
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override {
        const int side = std::min(rect.width(), rect.height());
        const qreal ratio = painter->device()->devicePixelRatioF();
        const QPixmap image = renderedPixmap(qRound(side * ratio));
        const QRect target(rect.x() + (rect.width() - side) / 2,
                           rect.y() + (rect.height() - side) / 2, side, side);
        painter->drawPixmap(target, image);
    }

    // D-Bus tray backends export a list of bitmaps instead of asking for one
    // size. Keep their existing choices; these do not limit pixmap requests.
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const
#endif
        override {
        return {QSize(16, 16), QSize(22, 22), QSize(32, 32),
                QSize(44, 44), QSize(48, 48), QSize(64, 64)};
    }

    void virtual_hook(int id, void *data) override {
        if (id == QIconEngine::IsNullHook) {
            *static_cast<bool *>(data) = false;
        } else if (id == QIconEngine::ScaledPixmapHook) {
            auto *argument = static_cast<QIconEngine::ScaledPixmapArgument *>(data);
            QSize pixels = argument->size;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            // Qt 6.8 switched this argument from physical to logical pixels.
            pixels *= argument->scale;
#endif
            argument->pixmap = renderedPixmap(std::min(pixels.width(), pixels.height()));
            argument->pixmap.setDevicePixelRatio(argument->scale);
        } else {
            QIconEngine::virtual_hook(id, data);
        }
    }

private:
    QPixmap renderedPixmap(int side) {
        if (side < 8)
            return QPixmap();
        if (const QPixmap *cached = mPixmaps.object(side))
            return *cached;
        const QPixmap image = QPixmap::fromImage(
            renderTextStatistics(mOptions, mUpload, mDownload, side));
        const qint64 pixels = qint64(side) * side;
        if (!image.isNull() && pixels <= mPixmaps.maxCost())
            mPixmaps.insert(side, new QPixmap(image), int(pixels));
        return image;
    }

    // Each icon owns an immutable snapshot. New text or settings create a new
    // engine, so cached images cannot outlive the values they depict.
    const ViewOptions mOptions;
    const QString mUpload;
    const QString mDownload;
    // Cost is in pixels: at most 256 KiB of 32-bit pixel data per engine.
    // Cached pixels have DPR 1; the caller sets DPR on its returned copy.
    QCache<int, QPixmap> mPixmaps{64 * 1024};
};
}

QIcon textStatisticsIcon(const ViewOptions &options, const QString &upload,
                         const QString &download) {
    return QIcon(new TextStatisticsIconEngine(options, upload, download));
}
