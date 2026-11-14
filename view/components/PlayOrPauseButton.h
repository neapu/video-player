#pragma once
#include <QPushButton>

namespace view {
class PlayOrPauseButton : public QPushButton {
    Q_OBJECT
public:
    PlayOrPauseButton(QWidget* parent = nullptr);

    bool isPlaying() const;
    void setPlaying(bool playing);

private:
    bool m_playing{false};
    QIcon m_icon{};
};
} // view namespace