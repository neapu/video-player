#include "PlaybackSlider.h"
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMouseEvent>

namespace view {

PlaybackSlider::PlaybackSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
}

void PlaybackSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && orientation() == Qt::Horizontal) {
        // 先把滑块移到点击位置，再交给基类：基类看到点击落在滑块上，
        // 不再翻页，直接进入拖动状态
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

        const int span = groove.width() - handle.width();
        if (span > 0) {
            const int pos = event->position().toPoint().x() - groove.left() - handle.width() / 2;
            const double ratio = qBound(0.0, static_cast<double>(pos) / span, 1.0);
            const double value = minimum() + ratio * (static_cast<double>(maximum()) - minimum());
            setValue(qRound(value));
        }
    }
    QSlider::mousePressEvent(event);
}

} // namespace view
