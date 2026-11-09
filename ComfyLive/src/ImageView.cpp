#include "ImageView.h"

#include <QPainter>

ImageView::ImageView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 320);
}

void ImageView::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void ImageView::setCaption(const QString& caption)
{
    caption_ = caption;
    update();
}

void ImageView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 28));
    if (!image_.isNull()) {
        const QSize fit = image_.size().scaled(size() - QSize(16, 40), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fit.width()) / 2, (height() - 24 - fit.height()) / 2), fit);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, image_);
    } else if (caption_.isEmpty()) {
        p.setPen(QColor(140, 140, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No picture yet: connect, then Generate (or turn on Live)."));
    } else {   // no picture (a song, or a run that has not shown one yet): the caption in the middle
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() * 1.6);
        p.setFont(f);
        p.setPen(QColor(220, 220, 230));
        p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, caption_);
        return;
    }
    if (!caption_.isEmpty()) {
        p.setPen(QColor(220, 220, 230));
        p.drawText(rect().adjusted(8, 0, -8, -4), Qt::AlignBottom | Qt::AlignHCenter, caption_);
    }
}
