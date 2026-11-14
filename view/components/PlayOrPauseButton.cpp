#include "PlayOrPauseButton.h"

namespace view {
PlayOrPauseButton::PlayOrPauseButton(QWidget* parent)
    : QPushButton(parent)
{
    setPlaying(false);
    setIcon(m_icon);
    setIconSize(QSize(18, 18));
    setFixedSize(QSize(32, 32));
    setToolTip("Play/Pause");
}

bool PlayOrPauseButton::isPlaying() const
{
    return m_playing;
}

void PlayOrPauseButton::setPlaying(bool playing)
{
    m_playing = playing;
    m_icon = QIcon(m_playing ? ":/icons/pause.svg" : ":/icons/play.svg");
}

} // view namespace