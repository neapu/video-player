#include "MediaSource.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
std::expected<std::unique_ptr<IMediaSource>, CreateError> IMediaSource::create(const Options& options)
{
    return MediaSource::create(options);
}

std::expected<std::unique_ptr<IMediaSource>, CreateError> MediaSource::create(const Options& options)
{
    auto mediaSource = std::unique_ptr<MediaSource>(new MediaSource(options));
    auto err = mediaSource->initialize();
    if (err != CreateError::None) {
        return std::unexpected(err);
    }
    return std::move(mediaSource);
}

MediaSource::MediaSource(const Options& options)
    : m_source(options.source),
      m_logDir(options.logDir),
      m_instanceName(options.instanceName),
      m_onSeekComplete(options.onSeekComplete),
      m_onRunningError(options.onRunningError)
{
}

MediaSource::~MediaSource()
{
}

CreateError MediaSource::initialize()
{
    if (m_instanceName.empty()) {
        m_instanceName = "default";
    }
    if (!m_logDir.empty()) {
        m_logger = createLogger(m_instanceName, m_logDir);
    }

    if (!m_onRunningError || !m_onSeekComplete) {
        LOG_ERROR(m_logger, "Invalid argument: onRunningError and onSeekComplete callbacks must be provided");
        return CreateError::InvalidArgument;
    }

    m_demuxer = Demuxer::create({
        m_source,
        m_logger
    });
    if (!m_demuxer) {
        LOG_ERROR(m_logger, "Failed to create demuxer");
        return CreateError::FileOpenFailed;
    }

    // 至少要有一个视频流或音频流
    int streamCount = m_demuxer->streamCount();
    if (streamCount == 0) {
        LOG_ERROR(m_logger, "No valid stream found");
        return CreateError::NoValidStream;
    }
    bool hasVideoOrAudio = false;
    for (int i = 0; i < streamCount; ++i) {
        const AVStream* stream = m_demuxer->stream(i);
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO || stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            hasVideoOrAudio = true;
            break;
        }
    }
    if (!hasVideoOrAudio) {
        LOG_ERROR(m_logger, "No valid video or audio stream found");
        return CreateError::NoValidStream;
    }

    LOG_INFO(m_logger, "MediaSource initialized successfully");

    return CreateError::None;
}

std::vector<StreamInfo> MediaSource::getStreamInfo() const
{
    return {};
}

StreamError MediaSource::enableStream(int streamIndex, bool hardwareAccelerated, void* hardwareContext)
{
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot enable video stream while demuxer is running");
        return StreamError::DemuxerRunning;
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        LOG_ERROR(m_logger, "Video stream already enabled");
        return StreamError::StreamAlreadyEnabled;
    }

    if (streamIndex < 0 || streamIndex >= m_demuxer->streamCount()) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return StreamError::InvalidIndex;
    }

    AVStream* stream = m_demuxer->stream(streamIndex);
    if (!stream) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return StreamError::InvalidIndex;
    }

    Decoder::Options decoderOptions{};
    if (hardwareAccelerated && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#ifdef _WIN32
        decoderOptions.type = Decoder::Type::D3d11va;
        decoderOptions.hardwareContext = hardwareContext;
#else
        decoderOptions.type = Decoder::Type::Software;
#endif
    } else {
        decoderOptions.type = Decoder::Type::Software;
    }
    decoderOptions.stream = stream;
    decoderOptions.logger = m_logger;
    auto decoder = Decoder::create(decoderOptions);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create decoder for video stream");
        return StreamError::DecoderCreationFailed;
    }

    // 测试解码器工作是否正常（解码出一帧有效帧）
    bool testSuccess{false};
    for (int i = 0; i < 30; ++i) {
        auto packetExp = m_demuxer->readPacket();
        if (!packetExp) {
            int err = packetExp.error();
            LOG_ERROR(m_logger, "Failed to read packet from demuxer. Error: {}", fh::err2str(err));
            break;
        }
        auto packet = std::move(*packetExp);
        if (packet->stream_index != streamIndex) {
            continue;
        }
        int ret = decoder->sendPacket(std::move(packet));
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to send packet to decoder. Error: {}", fh::err2str(ret));
            break;
        }
        bool receiveError{false};
        while (true) {
            auto frameExp = decoder->receiveFrame();
            if (!frameExp) {
                int err = frameExp.error();
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame from decoder. Error: {}", fh::err2str(err));
                receiveError = true;
                break;
            }
            testSuccess = true;
            break;
        }
        if (receiveError) {
            break;
        }
    }
    m_demuxer->seek(0); // Reset demuxer to the beginning after test decoding
    if (!testSuccess) {
        LOG_ERROR(m_logger, "Decoder test failed");
        return StreamError::DecoderCreationFailed;
    }

    // 根据解码器类型，估算包队列长度
    // 目标是视频缓存10秒，音频缓存3秒
    // 由于包不携带时长信息（一包不一定对应一帧），视频根据帧率估算，音频根据采样率估算
    int packetQueueLength = 0;
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        int fps = stream->codecpar->framerate.num / stream->codecpar->framerate.den;
        fps = fps > 0 ? fps : 120;  // 留足余量，不差这点内存
        packetQueueLength = fps * 10; // 10秒的视频缓存
        LOG_INFO(m_logger, "Estimated video packet queue length: {}", packetQueueLength);
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        int sampleRate = stream->codecpar->sample_rate > 0 ? stream->codecpar->sample_rate : 48000;
        int samplesPerFrame = stream->codecpar->frame_size > 0 ? stream->codecpar->frame_size : 1024;
        packetQueueLength = sampleRate * 3 / samplesPerFrame; // 3秒的音频缓存
        LOG_INFO(m_logger, "Estimated audio packet queue length: {}", packetQueueLength);
    } else {
        packetQueueLength = 10; // 字幕流等
        LOG_INFO(m_logger, "Estimated packet queue length for non-video/audio stream: {}", packetQueueLength);
    }
    DecodeWorker::Options decodeWorkerOpts;
    decodeWorkerOpts.maxPacketQueueSize = packetQueueLength;
    decodeWorkerOpts.maxFrameQueueSize = 5;
    decodeWorkerOpts.logger = m_logger;
    auto decodeWorker = DecodeWorker::create(decodeWorkerOpts, std::move(decoder));
    if (!decodeWorker) {
        LOG_ERROR(m_logger, "Failed to create decode worker");
        return StreamError::DecoderCreationFailed;
    }
    m_decodeWorkers[streamIndex] = std::move(decodeWorker);

    return StreamError::None;
}

StreamError MediaSource::disableStream(int streamIndex)
{
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot enable video stream while demuxer is running");
        return StreamError::DemuxerRunning;
    }

    auto it = m_decodeWorkers.find(streamIndex);
    if (it == m_decodeWorkers.end()) {
        LOG_ERROR(m_logger, "Stream not found");
        return StreamError::InvalidIndex;
    }

    m_decodeWorkers.erase(it);

    return StreamError::None;
}

MediaFramePtr MediaSource::nextFrame(int streamIndex)
{
    auto it = m_decodeWorkers.find(streamIndex);
    if (it == m_decodeWorkers.end()) {
        LOG_ERROR(m_logger, "Stream not found");
        return nullptr;
    }

    auto& worker = it->second;
    auto frame = worker->popFrame();
    return std::make_unique<MediaFrame>(std::move(frame));
}

int64_t MediaSource::duration() const
{
    return m_demuxer ? m_demuxer->duration() : 0;
}

void MediaSource::seek(int64_t timestamp)
{
    m_seekTarget = timestamp;
    for (auto& decodeWorker: m_decodeWorkers) {
        decodeWorker.second->interrupt();
    }
}

StartError MediaSource::start()
{
    if (m_demuxRunning.load()) {
        LOG_ERROR(m_logger, "Already started");
        return StartError::AlreadyStarted;
    }

    if (m_decodeWorkers.empty()) {
        LOG_ERROR(m_logger, "No stream enabled");
        return StartError::NoStreamEnabled;
    }
    
    m_demuxRunning.store(true);
    m_demuxThread = std::thread(&MediaSource::demuxWorkerFunc, this);

    return StartError::None;
}

void MediaSource::stop()
{
    if (!m_demuxRunning.load()) {
        return;
    }
    m_demuxRunning.store(false);
    for (auto& decodeWorker: m_decodeWorkers) {
        decodeWorker.second->interrupt();
    }

    if (m_demuxThread.joinable()) {
        m_demuxThread.join();
    }
}

void MediaSource::demuxWorkerFunc()
{
    int retryCount = 0;
    while (m_demuxRunning.load()) {
        auto seekTarget = m_seekTarget.exchange(AV_NOPTS_VALUE);
        if (seekTarget != AV_NOPTS_VALUE) {
            int ret = m_demuxer->seek(seekTarget);
            if (ret < 0) {
                LOG_ERROR(m_logger, "Seek failed. error: {}", fh::err2str(ret));
                continue;
            }
            retryCount = 0;
            // 插入 Flash 包提醒解码线程
            for (auto& worker: m_decodeWorkers) {
                worker.second->pushPacket(MediaPacket{m_epoch, PacketType::Flush, nullptr});
            }
        }

        auto packetExp = m_demuxer->readPacket();
        if (!packetExp.has_value()) {
            int err = packetExp.error();
            if (err == AVERROR_EOF) {
                // 为所有流都插入End包
                for (auto& worker: m_decodeWorkers) {
                    worker.second->pushPacket(MediaPacket{m_epoch, PacketType::End, nullptr});
                }
                continue;
            }
            // 先重试20次，间隔20ms，如果还失败，上报错误，并将间隔调整到500ms
            if (retryCount < 20) {
                ++retryCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            retryCount = 0;
            LOG_ERROR(m_logger, "Read packet failed, err: {}", fh::err2str(err));
            m_onRunningError(RunningError::ReadFailed);
            continue;
        }
        retryCount = 0;

        auto packet = std::move(packetExp.value());
        int streamIndex = packet->stream_index;
        auto it = m_decodeWorkers.find(streamIndex);
        if (it == m_decodeWorkers.end()) {
            // 没有激活的流，忽略
            continue;
        }
        auto& worker = it->second;
        worker->pushPacket(MediaPacket{m_epoch, PacketType::Normal, std::move(packet)});
    }
}

} // namespace media