#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include "media/IMediaSource.h"

namespace view {
class Controller : public QObject {
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Playing,
        Paused
    };

    explicit Controller(QObject *parent = nullptr);
    ~Controller();

    void openFile(const QString &filePath);
    void pauseOrResume();
    void stop();

    void setState(State state);
    State state() const;

private:
    State m_state{State::Stopped};
    mutable QMutex m_stateMutex;

    std::unique_ptr<media::IMediaSource> m_mediaSource{nullptr};
};

} // namespace view