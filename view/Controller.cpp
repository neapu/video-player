#include "Controller.h"
#include "LogManager.h"
#include <QDebug>

namespace {
constexpr int64_t MIN_INT64 = std::numeric_limits<int64_t>::min();

int64_t steadyClockUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

view::YuvFrame mediaFrameToViewFrame(const media::MediaFramePtr& frame)
{
    view::YuvFrame f{};
    auto frameFormat = frame->format();
    if (frameFormat.pixelFormat != media::PixelFormat::Yuv420p) {
        return f;
    }
    f.width = frameFormat.width;
    f.height = frameFormat.height;
    f.yStride = frame->lineSize(0);
    f.uStride = frame->lineSize(1);
    f.vStride = frame->lineSize(2);
    f.y = QByteArray(reinterpret_cast<const char*>(frame->data(0)), f.yStride * f.height);
    f.u = QByteArray(reinterpret_cast<const char*>(frame->data(1)), f.uStride * f.height / 2);
    f.v = QByteArray(reinterpret_cast<const char*>(frame->data(2)), f.vStride * f.height / 2);
    return f;
}

}

namespace view {

Controller::Controller(QObject* parent) : QObject(parent) {}

Controller::~Controller() {}

void Controller::setVideoSink(VideoSink* sink)
{
    m_videoSink = sink;
}

void Controller::openFile(const QString& filePath)
{
    if (state() != State::Stopped) {
        stop();
    }

    if (!m_videoSink) {
        qCritical() << "No video sink set";
        return;
    }

    using namespace media;
    IMediaSource::Options opts;
    opts.source = filePath.toStdString();
    opts.logDir = LogManager::mediaLogPath();
    opts.instanceName = "media";
    opts.onRunningError = [this](media::RunningError err) {
        // 目前只有一种错误 ReadFailed
        // 下层已经重试过了，本层直接走结束流程
        // 投递到GUI线程
        QMetaObject::invokeMethod(this, [this]{ stop(); }, Qt::QueuedConnection);
    };
    opts.onSeekComplete = [](media::SeekError err) {
        // Handle seek complete
    };

    auto mediaSourceExp = IMediaSource::create(opts);
    if (!mediaSourceExp) {
        auto err = mediaSourceExp.error();
        qCritical() << "Failed to create media source:" << QString::fromStdString(media::errToString(err));
        return;
    }
    m_mediaSource = std::move(mediaSourceExp.value());

    m_videoStreamIndex = -1;
    // 先测试视频
    auto streamsInfo = m_mediaSource->streamsInfo();
    int videoWidth{0};
    int videoHeight{0};
    for (auto& info : streamsInfo) {
        if (info.type == media::StreamType::Video && m_videoStreamIndex == -1) {
            m_videoStreamIndex = info.index;
            m_videoFps = info.frameRate.den > 0 ? (double)info.frameRate.num / info.frameRate.den : 0;
            videoWidth = info.width;
            videoHeight = info.height;
        }
    }

    if (m_videoStreamIndex == -1) { // 现在先测视频，先不管纯音频
        qCritical() << "No video stream found";
        return;
    }

    if (m_videoFps == 0) {
        qWarning() << "Video FPS is 0";
        m_videoFps = 15;
    }

    auto enableExp = m_mediaSource->enableVideoStream(m_videoStreamIndex, {media::PixelFormat::Yuv420p});
    if (!enableExp) {
        auto err = enableExp.error();
        qCritical() << "Failed to enable video stream. " << QString::fromStdString(media::errToString(err));
        return;
    }
    if (enableExp.value() != media::PixelFormat::Yuv420p) {
        qCritical() << "Unexpected pixel format";
        return;
    }

    auto startError = m_mediaSource->start();
    if (startError != media::StartError::None) {
        qCritical() << "Failed to start media source. " << QString::fromStdString(media::errToString(startError));
        return;
    }

    {
        // 时钟重锚到0，表示从这一刻开始播放
        QMutexLocker locker(&m_clock.mutex);
        reanchorClock(0, locker);
    }
    m_lastVideoPts = 0;
    m_videoEnded = false;
    m_pendingVideoFrame.reset();

    VideoSink::Options videoSinkOpts{};
    videoSinkOpts.format = PixelFormat::Yuv420p;
    videoSinkOpts.width = videoWidth;
    videoSinkOpts.height = videoHeight;
    videoSinkOpts.frameSource = [this] { return nextVideoFrame(); };
    m_videoSink->start(videoSinkOpts);

    setState(State::Playing);
}

void Controller::pauseOrResume()
{
    // Implement pause or resume logic here
}

void Controller::stop()
{
    if (state() == State::Stopped) {
        return;
    }

    m_videoSink->stop();
    m_mediaSource->stop();
    m_videoStreamIndex = -1; // 置为-1后，nextVideoFrame不会再访问m_mediaSource
    m_mediaSource.reset();

    setState(State::Stopped);
}

void Controller::setState(State state)
{
    // 目前对状态的访问都在GUI线程，先不加锁，以后看情况加
    // QMutexLocker lock(&m_stateMutex);
    m_state = state;
}

Controller::State Controller::state() const
{
    // QMutexLocker lock(&m_stateMutex);
    return m_state;
}

view::Frame Controller::nextVideoFrame()
{
    if (m_videoStreamIndex == -1) {
        return YuvFrame{};
    }

    while (true) {
        if (!m_pendingVideoFrame) {
            m_pendingVideoFrame = m_mediaSource->nextFrame(m_videoStreamIndex);
        }

        if (!m_pendingVideoFrame) {
            // 严重错误，mediaSource不应返回空指针
            qCritical() << "Got null video frame";
            return YuvFrame{};
        }

        if (m_pendingVideoFrame->type() == media::FrameType::Empty) {
            m_pendingVideoFrame.reset();
            return YuvFrame{};
        } else if (m_pendingVideoFrame->type() == media::FrameType::End) {
            m_videoEnded = true;
            QMetaObject::invokeMethod(this, [this]{ endOfStream(); }, Qt::QueuedConnection);
            m_pendingVideoFrame.reset();
            return YuvFrame{};
        }

        int64_t mediaUs{0};
        {
            QMutexLocker locker(&m_clock.mutex);
            mediaUs = currentMediaUs(locker);
        }

        int64_t pts = m_pendingVideoFrame->pts();
        if (pts == MIN_INT64) {
            // 无效的pts，需要根据帧率估算
            pts = m_lastVideoPts + (1000000 / m_videoFps);
        }
        
        // 根据媒体时间判断是否可以放行/丢帧
        // 如果帧的pts小于媒体时间超过一帧的间隔，既 (1000000 / m_videoFps)，就需要丢掉
        constexpr int64_t tolerance = 2000;
        if (pts + (1000000 / m_videoFps) + tolerance < mediaUs) {
            m_lastVideoPts = pts;   // 这里要更新一下，不然下一帧会算错
            m_pendingVideoFrame.reset();
            continue;
        }

        // 如果大于媒体时间，先不渲染，本函数不能阻塞，等待下一次刷新请求
        if (pts > mediaUs) {
            return YuvFrame{};
        }

        // pts <= mediaUs, 可以渲染
        m_lastVideoPts = pts;   // 上屏或丢弃时才更新，不然下一次请求的是同一帧，可能导致错误累计
        {
            QMutexLocker locker(&m_clock.mutex);
            reanchorClock(mediaUs, locker);
        }
        auto frame = mediaFrameToViewFrame(m_pendingVideoFrame);
        m_pendingVideoFrame.reset();
        return frame;
    }

    return YuvFrame{};
}

void Controller::reanchorClock(int64_t mediaUs, QMutexLocker<QMutex>& locker)
{
    m_clock.anchorMediaUs = mediaUs;
    m_clock.anchorSteadyUs = steadyClockUs();
}

int64_t Controller::currentMediaUs(QMutexLocker<QMutex>& locker)
{
    return m_clock.anchorMediaUs + (steadyClockUs() - m_clock.anchorSteadyUs);
}

void Controller::endOfStream()
{
    if (m_videoEnded /*&& m_audioEnded*/) {
        stop();
    }
}

} // namespace view
