#pragma once
#include <QSlider>

namespace view {

// 进度条滑块：单击槽道任意位置直接定位到点击处（原生QSlider点击是翻页），
// 定位后交给基类继续处理，进入正常拖动状态，信号（sliderPressed/sliderMoved/sliderReleased）不变
class PlaybackSlider : public QSlider {
    Q_OBJECT
public:
    explicit PlaybackSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
};

} // namespace view
