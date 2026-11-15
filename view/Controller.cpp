#include "Controller.h"
#include "LogManager.h"
#include <QDebug>

namespace {
constexpr int64_t MIN_INT64 = std::numeric_limits<int64_t>::min();

// seek落定时接受的帧pts窗口（微秒）：窗口外的Normal帧视为seek前的旧帧或待跳过的过渡帧丢弃
constexpr int64_t SEEK_TOLERANCE_US = 500000;
// seek落定前连续丢弃音频帧的上限，超过后无条件接受，防止异常流导致永久静音
constexpr int AUDIO_SEEK_DROP_LIMIT = 50;

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

void Controller::openFile(const QString& filePath, media::VideoDecodeMode decodeMode)
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
    opts.onSeekComplete = [this](media::SeekError err) {
        // demux线程触发，投递到GUI线程
        QMetaObject::invokeMethod(this, [this, err] {
            if (err == media::SeekError::None) {
                qDebug() << "Seek complete";
            } else {
                qWarning() << "Seek failed:" << static_cast<int>(err);
            }
        }, Qt::QueuedConnection);
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

    bool gpuRender = false;
    auto videoFormatOpt = enableVideoStream(decodeMode, gpuRender);
    auto audioFormatOpt = enableAudioStream();

    if (!videoFormatOpt && !audioFormatOpt) {
        // 至少要有一个成功
        qCritical() << "Failed to enable any stream";
        return;
    }

    // 启动解码前，先创建音频渲染器（不启动播放）；渲染器每个文件重建，带上当前音量
    if (audioFormatOpt) {
        AudioRenderer::Options audioOpts{};
        audioOpts.sampleRate = audioSampleRate;
        audioOpts.channels = audioChannels;
        audioOpts.sampleFormat = *audioFormatOpt;
        audioOpts.volume = m_volume / 100.0f;
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
    m_videoSeekTargetUs.store(SEEK_IDLE);
    m_audioSeekTargetUs.store(SEEK_IDLE);
    m_audioSeekDropCount = 0;

    if (m_audioRenderer) {
        auto ret = m_audioRenderer->start(
            [this] { return nextAudioFrame(); },
            // 音频回调线程触发，按可听位置校准时钟
            [this](int64_t audibleUs) {
                // seek未落定时设备里可能还在消费旧位置的样本，
                // 此时不校准时钟，落定后位置会由新帧的pts重新同步
                if (m_audioSeekTargetUs.load() != SEEK_IDLE) {
                    return;
                }
                emit positionChanged(audibleUs);
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

    m_videoPlayedUs = 0;
    m_videoEnded = false;
    m_pendingVideoFrame.reset();

    if (videoFormatOpt) {
        VideoSink::Options videoSinkOpts{};
        videoSinkOpts.format = *videoFormatOpt;
        videoSinkOpts.width = videoWidth;
        videoSinkOpts.height = videoHeight;
        videoSinkOpts.hwaccel = gpuRender;
        videoSinkOpts.frameSource = [this] { return nextVideoFrame(); };
        m_videoSink->start(videoSinkOpts);
    }

    

    setState(State::Playing);
}

void Controller::pauseOrResume()
{
    if (state() == State::Playing) {
        {
            QMutexLocker locker(&m_clock.mutex);
            auto mediaUs = currentMediaUs(locker);
            reanchorClock(mediaUs, locker);
        }
        m_videoSink->pause();
        if (m_audioRenderer) m_audioRenderer->pause();
        setState(State::Paused);
    } else if (state() == State::Paused) {
        // media 时间不变，将 anchorSteadyUs 改为现在
        {
            QMutexLocker locker(&m_clock.mutex);
            m_clock.anchorSteadyUs = steadyClockUs();
        }
        m_videoSink->resume();
        if (m_audioRenderer) m_audioRenderer->resume();
        setState(State::Playing);
    }
}

void Controller::seek(int64_t targetUs)
{
    if (state() == State::Stopped || !m_mediaSource) {
        return;
    }

    auto durationUs = duration();
    if (targetUs < 0) {
        targetUs = 0;
    }
    if (durationUs > 0 && targetUs > durationUs) {
        targetUs = durationUs;
    }

    // 先让音频回调线程丢弃在播的旧帧，再重锚时钟，
    // 缩小时钟重锚后旧位置回调又把时钟拉回去的窗口
    if (m_audioRenderer) {
        m_audioRenderer->flush();
    }

    // 置seek标记：拉帧线程据此丢弃seek前的旧状态与旧帧（这些成员归各消费线程所有，
    // GUI线程不能直接修改），首个新位置的帧到来后由消费线程清除标记（落定）
    if (m_videoStreamIndex != -1) {
        m_videoSeekTargetUs.store(targetUs);
    }
    if (m_audioStreamIndex.load() != -1) {
        m_audioSeekTargetUs.store(targetUs);
    }

    {
        QMutexLocker locker(&m_clock.mutex);
        reanchorClock(targetUs, locker);
    }

    m_mediaSource->seek(targetUs);

    // 暂停状态下渲染循环已停止，请求一次性刷新以显示目标帧
    if (state() == State::Paused && m_videoStreamIndex != -1) {
        m_videoSink->refresh();
    }
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
    emit stateChanged(state);
}

Controller::State Controller::state() const
{
    // QMutexLocker lock(&m_stateMutex);
    return m_state;
}

void Controller::setVolume(int volume)
{
    m_volume = volume;
    if (m_audioRenderer) {
        m_audioRenderer->setVolume(static_cast<float>(m_volume) / 100);
    }
}

int64_t Controller::duration() const
{
    return m_mediaSource ? m_mediaSource->duration() : 0;
}

view::Frame Controller::nextVideoFrame()
{
    if (m_videoStreamIndex == -1) {
        return nullptr;
    }

    // 一帧的时长，单位微秒，视频帧的duration()为0，按帧率估算，用于累计播放时长和估算无效pts
    const int64_t frameIntervalUs = static_cast<int64_t>(1000000 / m_videoFps);

    while (true) {
        // seek未落定：丢弃seek前残留的待播帧，重置pts推算与累计时长的基点
        auto seekTarget = m_videoSeekTargetUs.load();
        if (seekTarget != SEEK_IDLE) {
            m_pendingVideoFrame.reset();
            m_videoEnded = false;
            m_videoPlayedUs = seekTarget;
        }

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
            // 流已结束，seek流程随之终止
            m_videoSeekTargetUs.store(SEEK_IDLE);
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
            // 无效的pts，按累计的播放时长推算，写回后下游只需信任pts
            pts = m_videoPlayedUs;
            m_pendingVideoFrame->setPts(pts);
        }

        if (seekTarget != SEEK_IDLE) {
            // seek落定前的过滤：只接受目标位置附近的帧。
            // 更早的帧是seek点关键帧到目标之间的过渡帧（本就要跳过）；
            // 更晚的帧只可能是seek前的残留旧帧（av_seek_frame回退保证新帧不会超过目标）
            if (pts < seekTarget - SEEK_TOLERANCE_US || pts > seekTarget + SEEK_TOLERANCE_US) {
                m_pendingVideoFrame.reset();
                continue;
            }
            // 首个到达目标附近的帧，seek落定，恢复正常流程
            m_videoSeekTargetUs.store(SEEK_IDLE);
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
                    // 无效的pts，当前帧消费后累计播放时长再加一帧间隔，写回后下游只需信任pts
                    nextPts = m_videoPlayedUs + frameIntervalUs;
                    next->setPts(nextPts);
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
            m_videoPlayedUs += frameIntervalUs; // 丢弃也要累计，不然下一帧会算错
            continue;
        }

        // 当前帧是队列中最新的已到期帧，上屏
        m_videoPlayedUs += frameIntervalUs;     // 上屏或丢弃时才累计，不然下一次请求的是同一帧，会重复累计
        if (!m_audioRenderer) {
            emit positionChanged(mediaUs);
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
    // seek未落定：丢弃seek前残留的待播帧，重置pts推算与累计时长的基点
    auto seekTarget = m_audioSeekTargetUs.load();
    if (seekTarget != SEEK_IDLE) {
        m_pendingAudioFrame.reset();
        m_audioEnded = false;
        m_audioPlayedUs = seekTarget;
    }

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
        // 流已结束，seek流程随之终止
        m_audioSeekTargetUs.store(SEEK_IDLE);
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

    if (seekTarget != SEEK_IDLE) {
        // seek落定前的过滤：只丢弃远超目标位置的seek前残留旧帧
        //（它们会卡住下面的到期门禁并占住待播槽），到达目标附近的帧即视为落定。
        // 音频帧间隔小，不做下限过滤，避免稀疏音轨被误杀；
        // 连续丢弃超限后无条件接受，防止异常流导致seek后永久静音
        if (pts > seekTarget + SEEK_TOLERANCE_US && m_audioSeekDropCount < AUDIO_SEEK_DROP_LIMIT) {
            ++m_audioSeekDropCount;
            m_pendingAudioFrame.reset();
            return nullptr;
        }
        m_audioSeekTargetUs.store(SEEK_IDLE);
        m_audioSeekDropCount = 0;
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

std::optional<view::PixelFormat> Controller::enableVideoStream(media::VideoDecodeMode decodeMode, bool& hw)
{
    if (m_videoStreamIndex < 0) return std::nullopt;
    if (!m_videoSink) {
        qCritical() << "No video sink";
        return std::nullopt;
    }
#ifdef _WIN32
    if (decodeMode != media::VideoDecodeMode::Software) { // 硬解码模式（直渲染或软渲染）
        auto* context = m_videoSink->device();
        if (context) {
            // 硬解软渲染请求 {Nv12, P010le}：下载格式直接用，零转换；
            // 硬解直渲染与现状一致，不传格式列表
            std::vector<media::PixelFormat> requireFormats{};
            if (decodeMode == media::VideoDecodeMode::HardwareDownload) {
                requireFormats = {media::PixelFormat::Nv12, media::PixelFormat::P010le};
            }
            auto formatExp = m_mediaSource->enableVideoStream(m_videoStreamIndex, std::move(requireFormats), decodeMode, context);
            if (formatExp && (formatExp.value() == media::PixelFormat::Nv12 || formatExp.value() == media::PixelFormat::P010le)) {
                // Hardware：GPU 帧零拷贝渲染；HardwareDownload：下载后的 CPU 帧软渲染
                hw = decodeMode == media::VideoDecodeMode::Hardware;
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
    }
#else
    Q_UNUSED(decodeMode);
#endif

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
