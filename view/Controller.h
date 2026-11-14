#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <optional>
#include "media/IMediaSource.h"
#include "media/IMediaFrame.h"
#include "VideoSink.h"
#include "audio_renderer/AudioRenderer.h"

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

    void openFile(const QString &filePath, bool tryHw = true);
    void pauseOrResume();
    void stop();

    void setState(State state);
    State state() const;

    int volume() const { return m_volume; }
    void setVolume(int volume);

signals:
    void stateChanged(State state);

private:
    // 不阻塞，不渲染时返回nullptr，不会返回控制帧
    view::Frame nextVideoFrame();
    view::Frame nextAudioFrame();

    // 根据媒体时间重锚时钟
    void reanchorClock(int64_t mediaUs, QMutexLocker<QMutex>& locker);
    // 获取当前时刻的媒体时间
    int64_t currentMediaUs(QMutexLocker<QMutex>& locker);

    void endOfStream();

    std::optional<view::PixelFormat> enableVideoStream(bool& hw);
    std::optional<view::SampleFormat> enableAudioStream();

private:
    State m_state{State::Stopped};
    // mutable QMutex m_stateMutex;

    VideoSink* m_videoSink{nullptr};

    std::unique_ptr<view::AudioRenderer> m_audioRenderer{nullptr};

    std::unique_ptr<media::IMediaSource> m_mediaSource{nullptr};

    int m_videoStreamIndex{-1};
    int64_t m_videoPlayedUs{0};
    double m_videoFps{0};
    bool m_videoEnded{false};
    media::MediaFramePtr m_pendingVideoFrame{nullptr};

    std::atomic_int m_audioStreamIndex{-1};
    std::atomic_bool m_audioEnded{false};
    int64_t m_audioPlayedUs{0};
    media::MediaFramePtr m_pendingAudioFrame{nullptr};

    struct Clock {
        QMutex mutex;
        int64_t anchorSteadyUs{0}; // 记录媒体时间时的参考时间
        int64_t anchorMediaUs{0};
    } m_clock;

    int m_volume{100};
};

} // namespace view