#include "VolumeButton.h"

namespace view {
VolumeButton::VolumeButton(QWidget* parent)
    : QPushButton(parent)
{
    setFlat(true);
    setFocusPolicy(Qt::NoFocus);
    setVolume(m_percent);
    setIconSize(QSize(18, 18));
    setFixedSize(QSize(32, 32));
    // flat按钮在部分样式（如Win11的modern windows style）下静止时仍会画一层底色，
    // 与窗口背景不一致；改为彻底透明，悬停/按下用半透明遮罩提供点击反馈
    setStyleSheet(R"(
        QPushButton {
            background: transparent;
            border: none;
        }
        QPushButton:hover {
            background-color: rgba(128, 128, 128, 40);
            border-radius: 4px;
        }
        QPushButton:pressed {
            background-color: rgba(128, 128, 128, 80);
            border-radius: 4px;
        }
    )");
}

void VolumeButton::setVolume(int percent)
{
    m_percent = percent;
    if (m_percent <= 0) {
        m_icon = QIcon(":/icons/volume-mute.svg");
        setToolTip("Unmute");
    } else if (m_percent < 50) {
        m_icon = QIcon(":/icons/volume-down.svg");
        setToolTip("Mute");
    } else {
        m_icon = QIcon(":/icons/volume-up.svg");
        setToolTip("Mute");
    }
    setIcon(m_icon);
}

} // view namespace
