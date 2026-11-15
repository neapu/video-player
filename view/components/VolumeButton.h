#pragma once
#include <QPushButton>

namespace view {
// 音量按钮：按音量档位切换图标（静音/低/高），无边框；
// 点击行为（静音/恢复）由使用者通过 clicked 信号实现
class VolumeButton : public QPushButton {
    Q_OBJECT
public:
    VolumeButton(QWidget* parent = nullptr);

    // 按音量百分比刷新图标与提示：0静音，1~49低，50以上高
    void setVolume(int percent);

private:
    int m_percent{100};
    QIcon m_icon{};
};
} // view namespace
