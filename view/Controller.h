#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <optional>
#include <atomic>
#include <limits>
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

    void openFile(const QString &filePath, media::VideoDecodeMode decodeMode = media::VideoDecodeMode::Hardware, int decodeGpu = -1);
    void pauseOrResume();
    void stop();

    // 跳转到指定播放位置（微秒），Playing/Paused状态下有效；
    // 异步执行：由媒体线程完成实际的seek，首个新位置的帧到来后视为落定
    void seek(int64_t targetUs);

    void setState(State state);
    State state() const;

    int volume() const { return m_volume; }
    void setVolume(int volume);

    int64_t duration() const;

signals:
    void stateChanged(State state);
    void positionChanged(int64_t pos);

private:
    // 不阻塞，不渲染时返回nullptr，不会返回控制帧
    view::Frame nextVideoFrame();
    view::Frame nextAudioFrame();

    // 根据媒体时间重锚时钟
    void reanchorClock(int64_t mediaUs, QMutexLocker<QMutex>& locker);
    // 获取当前时刻的媒体时间
    int64_t currentMediaUs(QMutexLocker<QMutex>& locker);

    void endOfStream();

    // 按解码模式启用视频流；decodeGpu 为 media::IMediaSource::enumerateGpus() 序号（-1 自动）；
    // hw 出参表示是否走 GPU 零拷贝渲染（软解码与硬解软渲染为 false）
    std::optional<view::PixelFormat> enableVideoStream(media::VideoDecodeMode decodeMode, int decodeGpu, bool& hw);
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

    // seek落定标记：GUI线程的seek()置位为目标位置（微秒），SEEK_IDLE表示无未落定的seek；
    // 消费线程（渲染/音频回调）拉帧时读取，丢弃seek前的旧状态与旧帧，
    // 首个新位置的帧（或End帧）到来后清除，视为落定
    static constexpr int64_t SEEK_IDLE = std::numeric_limits<int64_t>::min();
    std::atomic_int64_t m_videoSeekTargetUs{SEEK_IDLE};
    std::atomic_int64_t m_audioSeekTargetUs{SEEK_IDLE};
    // seek代数：每次seek()自增。onSeekComplete从demux线程投递到GUI线程，
    // 可能与新的seek()并发，用代数丢弃过期回调
    std::atomic_uint64_t m_seekSerial{0};
    // seek失败后的恢复标记：失败时管线停留在原位置，目标位置的窗口过滤会一直丢帧，
    // 改为接受下一帧并把时钟重锚到其实际pts，从流当前所在位置继续播放
    std::atomic_bool m_videoRecoverPending{false};
    std::atomic_bool m_audioRecoverPending{false};
    // seek落定前连续丢弃旧音频帧的计数，超限后无条件接受下一帧兜底（仅音频回调线程访问）
    int m_audioSeekDropCount{0};

    struct Clock {
        QMutex mutex;
        int64_t anchorSteadyUs{0}; // 记录媒体时间时的参考时间
        int64_t anchorMediaUs{0};
    } m_clock;

    int m_volume{100};
};

} // namespace view