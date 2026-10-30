#include "Controller.h"
#include "LogManager.h"
#include <QDebug>

namespace {
int64_t steadyClockUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
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
    opts.onRunningError = [](media::RunningError err) {
        // Handle error
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

    VideoSink::Options videoSinkOpts{};
    videoSinkOpts.format = PixelFormat::Yuv420p;
    videoSinkOpts.width = videoWidth;
    videoSinkOpts.height = videoHeight;
    videoSinkOpts.frameSource = [this] { return nextVideoFrame(); };
    m_videoSink->start(videoSinkOpts);
}

void Controller::pauseOrResume()
{
    // Implement pause or resume logic here
}

void Controller::stop()
{
    // Implement stop logic here
}

void Controller::setState(State state)
{
    QMutexLocker lock(&m_stateMutex);
    m_state = state;
}

Controller::State Controller::state() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_state;
}

view::Frame Controller::nextVideoFrame()
{
    if (m_videoStreamIndex == -1) {
        return YuvFrame{};
    }

    if (!m_pendingVideoFrame) {
        m_pendingVideoFrame = m_mediaSource->nextFrame(m_videoStreamIndex);
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

} // namespace view
