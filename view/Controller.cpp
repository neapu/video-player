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

const char* pixelFormatName(media::PixelFormat format)
{
    switch (format) {
    case media::PixelFormat::Yuv420p: return "yuv420p";
    case media::PixelFormat::Nv12: return "nv12";
    case media::PixelFormat::Yuv420p10le: return "yuv420p10le";
    case media::PixelFormat::P010le: return "p010le";
    case media::PixelFormat::None: return "unknown";
    }
    return "unknown";
}

}

namespace view {

Controller::Controller(QObject* parent) : QObject(parent) {}

Controller::~Controller()
{
    stop();
}

void Controller::setVideoSink(VideoSink* sink)
{
    m_videoSink = sink;
}

void Controller::openFile(const QString& filePath, bool tryHw)
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
    m_audioStreamIndex = -1;

    auto streamsInfo = m_mediaSource->streamsInfo();
    int videoWidth{0};
    int videoHeight{0};
    int audioSampleRate{0};
    int audioChannels{0};
    for (auto& info : streamsInfo) {
        if (info.type == media::StreamType::Video && m_videoStreamIndex == -1) {
            m_videoStreamIndex = info.index;
            m_videoFps = info.frameRate.den > 0 ? (double)info.frameRate.num / info.frameRate.den : 0;
            videoWidth = info.width;
            videoHeight = info.height;
        }
        if (info.type == media::StreamType::Audio && m_audioStreamIndex.load() == -1) {
            m_audioStreamIndex.store(info.index);
            audioSampleRate = info.sampleRate;
            audioChannels = info.channels;
        }
    }

    if (m_videoStreamIndex == -1 && m_audioStreamIndex.load() == -1) {
        qCritical() << "No streams found";
        return;
    }

    if (m_videoFps == 0) {
        qWarning() << "Video FPS is 0";
        m_videoFps = 15;
    }

    bool hwaccel = tryHw;
    auto videoFormatOpt = enableVideoStream(hwaccel);
    auto audioFormatOpt = enableAudioStream();

    if (!videoFormatOpt && !audioFormatOpt) {
        // 至少要有一个成功
        qCritical() << "Failed to enable any stream";
        return;
    }

    // 启动解码前，先创建音频渲染器（不启动播放）
    if (audioFormatOpt) {
        AudioRenderer::Options audioOpts{};
        audioOpts.sampleRate = audioSampleRate;
        audioOpts.channels = audioChannels;
        audioOpts.sampleFormat = *audioFormatOpt;
        m_audioRenderer = AudioRenderer::create(audioOpts);
        if (!m_audioRenderer) {
            qCritical() << "Failed to create audio renderer";
            return;
        }
    }

    auto startError = m_mediaSource->start();
    if (startError != media::StartError::None) {
        qCritical() << "Failed to start media source. " << QString::fromStdString(media::errToString(startError));
        m_mediaSource.reset();
        m_videoStreamIndex = -1;
        m_audioStreamIndex = -1;
        return;
    }

    {
        // 时钟重锚到0，表示从这一刻开始播放
        QMutexLocker locker(&m_clock.mutex);
        reanchorClock(0, locker);
    }

    m_audioPlayedUs = 0;
    m_audioEnded = false;
    m_pendingVideoFrame.reset();

    if (m_audioRenderer) {
        auto ret = m_audioRenderer->start(
            [this] { return nextAudioFrame(); },
            // 音频回调线程触发，按可听位置校准时钟
            [this](int64_t audibleUs) {
                QMutexLocker locker(&m_clock.mutex);
                reanchorClock(audibleUs, locker);
            });
        if (!ret) { // 概率很小
            qCritical() << "Failed to start audio renderer";
            // 停止解码
            m_mediaSource->stop();
            m_audioRenderer.reset();
            return;
        }
    }

    m_lastVideoPts = 0;
    m_videoEnded = false;
    m_pendingVideoFrame.reset();

    if (videoFormatOpt) {
        VideoSink::Options videoSinkOpts{};
        videoSinkOpts.format = *videoFormatOpt;
        videoSinkOpts.width = videoWidth;
        videoSinkOpts.height = videoHeight;
        videoSinkOpts.hwaccel = hwaccel;
        videoSinkOpts.frameSource = [this] { return nextVideoFrame(); };
        m_videoSink->start(videoSinkOpts);
    }

    

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
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_audioRenderer.reset();
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
        return nullptr;
    }

    // 一帧的时长，单位微秒，仅用于估算无效pts
    const int64_t frameIntervalUs = static_cast<int64_t>(1000000 / m_videoFps);

    while (true) {
        if (!m_pendingVideoFrame) {
            m_pendingVideoFrame = m_mediaSource->nextFrame(m_videoStreamIndex);
        }

        if (!m_pendingVideoFrame) {
            // 严重错误，mediaSource不应返回空指针
            qCritical() << "Got null video frame";
            return nullptr;
        }

        if (m_pendingVideoFrame->type() == media::FrameType::Empty) {
            m_pendingVideoFrame.reset();
            return nullptr;
        } else if (m_pendingVideoFrame->type() == media::FrameType::End) {
            m_videoEnded = true;
            QMetaObject::invokeMethod(this, [this]{ endOfStream(); }, Qt::QueuedConnection);
            m_pendingVideoFrame.reset();
            return nullptr;
        }

        int64_t mediaUs{0};
        {
            QMutexLocker locker(&m_clock.mutex);
            mediaUs = currentMediaUs(locker);
        }

        int64_t pts = m_pendingVideoFrame->pts();
        if (pts == MIN_INT64) {
            // 无效的pts，根据上一帧的pts和帧率估算
            pts = m_lastVideoPts + frameIntervalUs;
        }

        // 如果大于媒体时间，先不渲染，本函数不能阻塞，等待下一次刷新请求
        if (pts > mediaUs) {
            return nullptr;
        }

        // 走到这里说明当前帧已到期，是否丢帧只取决于队列中有没有更新的已到期帧：
        // 有，当前帧已被取代，丢弃，让位于更新的帧；
        // 没有，即使当前帧已经迟到也照常渲染，不再按帧率估算的间隔判断迟到。
        auto due = std::move(m_pendingVideoFrame);
        auto next = m_mediaSource->nextFrame(m_videoStreamIndex);
        bool superseded = false;
        if (!next) {
            // 严重错误，mediaSource不应返回空指针，当作没有更新的帧处理
            qCritical() << "Got null video frame";
        } else if (next->type() == media::FrameType::Empty) {
            // 队列中暂时没有更新的帧，Empty不保留
        } else {
            if (next->type() == media::FrameType::Normal) {
                int64_t nextPts = next->pts();
                if (nextPts == MIN_INT64) {
                    // 无效的pts，按当前帧的pts加一帧间隔估算
                    nextPts = pts + frameIntervalUs;
                }
                if (nextPts <= mediaUs) {
                    // 更新的帧也已到期，当前帧没必要再上屏
                    superseded = true;
                }
            }
            // Normal和End都留作下一次刷新的候选帧，End会在下一次刷新时走上面的结束流程
            m_pendingVideoFrame = std::move(next);
        }

        if (superseded) {
            m_lastVideoPts = pts;   // 丢弃也要更新，不然下一帧会算错
            continue;
        }

        // 当前帧是队列中最新的已到期帧，上屏
        m_lastVideoPts = pts;   // 上屏或丢弃时才更新，不然下一次请求的是同一帧，可能导致错误累计
        {
            QMutexLocker locker(&m_clock.mutex);
            reanchorClock(mediaUs, locker);
        }
        return std::move(due);
    }

    return nullptr;
}

view::Frame Controller::nextAudioFrame()
{
    // 线程安全由启动顺序保证，在音频运行期间，本函数访问的成员不应被GUI线程修改
    // 音频考虑到连续性，
    // 门禁的处罚条件为pts与时钟相差超过200ms，
    // 且只阻挡未到期的帧，不丢弃过期帧
    // 典型的场景是视频开头的静音部分（第一帧的pts不是从0开始）
    if (!m_pendingAudioFrame) {
        m_pendingAudioFrame = m_mediaSource->nextFrame(m_audioStreamIndex);
    }
    if (!m_pendingAudioFrame) {
        // 严重错误，mediaSource不应返回空指针
        qCritical() << "Got null audio frame";
        return nullptr;
    }
    if (m_pendingAudioFrame->type() == media::FrameType::Empty) {
        m_pendingAudioFrame.reset();
        return nullptr;
    } else if (m_pendingAudioFrame->type() == media::FrameType::End) {
        m_audioEnded = true;
        QMetaObject::invokeMethod(this, [this]{ endOfStream(); }, Qt::QueuedConnection);
        m_pendingAudioFrame.reset();
        return nullptr;
    }

    int64_t mediaUs{0};
    {
        QMutexLocker locker(&m_clock.mutex);
        mediaUs = currentMediaUs(locker);
    }

    auto pts = m_pendingAudioFrame->pts();
    if (pts == MIN_INT64) {
        // 无效的pts，按累加的时长推算，写回后下游只需信任pts
        pts = m_audioPlayedUs;
        m_pendingAudioFrame->setPts(pts);
    }
    if (pts > mediaUs + 200000) {
        // 达到门禁条件，先不播放
        return nullptr;
    }

    m_audioPlayedUs += m_pendingAudioFrame->duration();

    // 时钟校准由 AudioRenderer 的位置回调完成（按写入游标与实测设备缓冲估算可听位置）
    auto ret = std::move(m_pendingAudioFrame);
    m_pendingAudioFrame.reset();
    return ret;
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
    if (m_videoEnded && m_audioEnded) {
        qInfo() << "End of stream";
        stop();
    }
}

std::optional<view::PixelFormat> Controller::enableVideoStream(bool& hw)
{
    if (m_videoStreamIndex < 0) return std::nullopt;
    if (!m_videoSink) {
        qCritical() << "No video sink";
        return std::nullopt;
    }
    if (hw) { // 先尝试硬解
#ifdef _WIN32
        auto* context = m_videoSink->device();
        if (context) {
            auto formatExp = m_mediaSource->enableVideoStream(m_videoStreamIndex, {}, true, context);
            if (formatExp && (formatExp.value() == media::PixelFormat::Nv12 || formatExp.value() == media::PixelFormat::P010le)) {
                hw = true;
                return formatExp.value();
            } else if (formatExp) {
                qWarning() << "Unsupported hw format";
                m_mediaSource->disableStream(m_videoStreamIndex);
            } else {
                qWarning() << "Enable hw decode failed. Use software decode";
            }
        } else {
            qWarning() << "No video context. Use software decode";
        }
#endif
    }

    auto formatExp = m_mediaSource->enableVideoStream(m_videoStreamIndex, {media::PixelFormat::Yuv420p, media::PixelFormat::Yuv420p10le});
    if (!formatExp) {
        qCritical() << "Enable video stream failed";
        return std::nullopt;
    }

    if (formatExp.value() != media::PixelFormat::Yuv420p && formatExp.value() != media::PixelFormat::Yuv420p10le) {
        qWarning() << "Unexpected format";
        return std::nullopt;
    }
    hw = false;
    return formatExp.value();
}

std::optional<view::SampleFormat> Controller::enableAudioStream()
{
    if (m_audioStreamIndex < 0) return std::nullopt;
    auto formatExp = m_mediaSource->enableAudioStream(m_audioStreamIndex, {media::SampleFormat::Flt});
    if (!formatExp) {
        qCritical() << "Enable audio stream failed";
        return std::nullopt;
    }
    if (formatExp.value() != media::SampleFormat::Flt) {
        qWarning() << "Unexpected audio format";
        return std::nullopt;
    }
    return formatExp.value();
}

} // namespace view
