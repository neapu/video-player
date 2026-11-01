#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include "media/IMediaSource.h"
#include "media/IMediaFrame.h"
#include "VideoSink.h"

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

    // 裸指针安全声明：只借用，生命周期必须大于Controller对象
    void setVideoSink(VideoSink* sink);

    void openFile(const QString &filePath);
    void pauseOrResume();
    void stop();

    void setState(State state);
    State state() const;

private:
    view::Frame nextVideoFrame();

    // 根据媒体时间重锚时钟
    void reanchorClock(int64_t mediaUs, QMutexLocker<QMutex>& locker);
    // 获取当前时刻的媒体时间
    int64_t currentMediaUs(QMutexLocker<QMutex>& locker);

    void endOfStream();

private:
    State m_state{State::Stopped};
    // mutable QMutex m_stateMutex;

    VideoSink* m_videoSink{nullptr};

    std::unique_ptr<media::IMediaSource> m_mediaSource{nullptr};

    int m_videoStreamIndex{-1};
    int64_t m_lastVideoPts{0};
    double m_videoFps{0};
    bool m_videoEnded{false};
    media::MediaFramePtr m_pendingVideoFrame{nullptr};

    struct Clock {
        QMutex mutex;
        int64_t anchorSteadyUs{0}; // 记录媒体时间时的参考时间
        int64_t anchorMediaUs{0};
    } m_clock;
};

} // namespace view