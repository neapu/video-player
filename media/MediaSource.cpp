#include "MediaSource.h"
#include "ffmpeg_helper/FFmpegError.h"
#include <exception>
#include <algorithm>
#include "SwsConvertor.h"
#include "SwrConvertor.h"
#include "DecodeProbe.h"

namespace media {
std::expected<std::unique_ptr<IMediaSource>, CreateError> IMediaSource::create(const Options& options)
{
    return MediaSource::create(options);
}

MediaSource::~MediaSource()
{
    // stop() 已做全量复位(停线程、移除所有流)
    stop();
    if (m_logger) {
        spdlog::drop(m_instanceName);
    }
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

std::vector<StreamInfo> MediaSource::streamsInfo() const
{
    int count = m_packetCache->demuxer().streamCount();
    std::vector<StreamInfo> infos;
    for (int i = 0; i < count; ++i) {
        const AVStream* stream = m_packetCache->demuxer().stream(i);
        StreamInfo info{};
        info.index = i;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            info.type = StreamType::Video;
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            info.type = StreamType::Audio;
        } else {
            info.type = StreamType::Unknown;
        }
        info.codecName = avcodec_get_name(stream->codecpar->codec_id);
        info.width = stream->codecpar->width;
        info.height = stream->codecpar->height;
        info.frameRate.num = stream->r_frame_rate.num;
        info.frameRate.den = stream->r_frame_rate.den;
        info.sampleRate = stream->codecpar->sample_rate;
        info.channels = stream->codecpar->ch_layout.nb_channels;
        infos.push_back(info);
    }
    return infos;
}

std::expected<PixelFormat, StreamError> MediaSource::enableVideoStream(
    int streamIndex,
    std::vector<PixelFormat> requirePixelFormats,
    bool hardwareAccelerated,
    void* hardwareContext
)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_demuxRunning) {
        return std::unexpected(StreamError::DemuxerRunning);
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        return std::unexpected(StreamError::StreamAlreadyEnabled);
    }

    if (streamIndex < 0 || streamIndex >= m_packetCache->demuxer().streamCount()) {
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (requirePixelFormats.empty()) {
        return std::unexpected(StreamError::RequestFormatEmpty);
    }

    const AVStream* stream = m_packetCache->demuxer().stream(streamIndex);
    if (!stream) {
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        LOG_ERROR(m_logger, "Stream type mismatch: expected video");
        return std::unexpected(StreamError::StreamTypeMismatch);
    }

    LOG_INFO(m_logger, "Got stream. Index = {}, codec = {}", streamIndex, avcodec_get_name(stream->codecpar->codec_id));

    auto decoder = createDecoder(stream, hardwareAccelerated, hardwareContext);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create decoder");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    DecodeProbe probe(*m_packetCache, decoder, streamIndex, m_logger);
    auto frame = probe.testDecode();
    if (!frame) {
        LOG_ERROR(m_logger, "Failed to test decode");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    MediaFrame mediaFrame(FrameType::Normal, std::move(frame));

    PixelFormat retFormat = mediaFrame.format().pixelFormat;
    // 检查像素格式是否在请求的格式里，如果不在，要创建转换器(硬解码时忽略)
    std::shared_ptr<SwsConvertor> converter{nullptr};
    if (!hardwareAccelerated && std::find(requirePixelFormats.begin(), requirePixelFormats.end(), retFormat) == requirePixelFormats.end()) {
        // 请求列表里可能含不可映射的项(如 None)，取第一个可映射的格式作为转换目标
        auto targetIt = std::find_if(requirePixelFormats.begin(), requirePixelFormats.end(), [](PixelFormat format) {
            return MediaFrame::toAvPixelFormat(format) != AV_PIX_FMT_NONE;
        });
        if (targetIt == requirePixelFormats.end()) {
            LOG_ERROR(m_logger, "No mappable pixel format in requested formats");
            return std::unexpected(StreamError::DecoderCreationFailed);
        }
        AVPixelFormat target = MediaFrame::toAvPixelFormat(*targetIt);
        SwsConvertor::Options opts{};
        opts.targetFormat = target;
        converter = std::make_shared<SwsConvertor>(opts);
        // 测试一下工作是否正常，就用刚才测试解码时的帧
        auto retFrame = converter->process(mediaFrame.frame());
        if (!retFrame) {
            LOG_ERROR(m_logger, "Failed to convert frame");
            return std::unexpected(StreamError::DecoderCreationFailed);
        }
        retFormat = *targetIt;
    }

    auto decodeWorker = createVideoDecodeWorker(stream, std::move(decoder), converter);
    if (!decodeWorker) {
        LOG_ERROR(m_logger, "Failed to create decode worker");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    m_decodeWorkers[streamIndex] = std::move(decodeWorker);

    return retFormat;
}

std::expected<SampleFormat, StreamError> MediaSource::enableAudioStream(int streamIndex, std::vector<SampleFormat> requireSampleFormats)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_demuxRunning) {
        return std::unexpected(StreamError::DemuxerRunning);
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        return std::unexpected(StreamError::StreamAlreadyEnabled);
    }

    if (streamIndex < 0 || streamIndex >= m_packetCache->demuxer().streamCount()) {
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (requireSampleFormats.empty()) {
        return std::unexpected(StreamError::RequestFormatEmpty);
    }

    const AVStream* stream = m_packetCache->demuxer().stream(streamIndex);
    if (!stream) {
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        LOG_ERROR(m_logger, "Stream type mismatch: expected audio");
        return std::unexpected(StreamError::StreamTypeMismatch);
    }

    LOG_INFO(m_logger, "Got stream. Index = {}, codec = {}", streamIndex, avcodec_get_name(stream->codecpar->codec_id));

    auto decoder = createDecoder(stream);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create decoder");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    DecodeProbe probe(*m_packetCache, decoder, streamIndex, m_logger);
    auto frame = probe.testDecode();
    if (!frame) {
        LOG_ERROR(m_logger, "Failed to test decode");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    MediaFrame mediaFrame(FrameType::Normal, std::move(frame));

    SampleFormat retFormat = mediaFrame.format().sampleFormat;
    // 检查采样格式是否在请求的格式里，如果不在，要创建转换器
    std::shared_ptr<SwrConvertor> converter{nullptr};
    if (std::find(requireSampleFormats.begin(), requireSampleFormats.end(), retFormat) == requireSampleFormats.end()) {
        // 请求列表里可能含不可映射的项(如 None)，取第一个可映射的格式作为转换目标
        auto targetIt = std::find_if(requireSampleFormats.begin(), requireSampleFormats.end(), [](SampleFormat format) {
            return MediaFrame::toAvSampleFormat(format) != AV_SAMPLE_FMT_NONE;
        });
        if (targetIt == requireSampleFormats.end()) {
            LOG_ERROR(m_logger, "No mappable sample format in requested formats");
            return std::unexpected(StreamError::DecoderCreationFailed);
        }
        AVSampleFormat target = MediaFrame::toAvSampleFormat(*targetIt);
        SwrConvertor::Options opts{};
        opts.targetFormat = target;
        opts.logger = m_logger;
        converter = std::make_shared<SwrConvertor>(opts);
        // 测试一下工作是否正常，就用刚才测试解码时的帧
        auto retFrame = converter->process(mediaFrame.frame());
        if (!retFrame) {
            LOG_ERROR(m_logger, "Failed to convert frame");
            return std::unexpected(StreamError::DecoderCreationFailed);
        }
        retFormat = *targetIt;
    }

    auto decodeWorker = createAudioDecodeWorker(stream, std::move(decoder), converter);
    if (!decodeWorker) {
        LOG_ERROR(m_logger, "Failed to create decode worker");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    m_decodeWorkers[streamIndex] = std::move(decodeWorker);

    return retFormat;
}

StreamError MediaSource::disableStream(int streamIndex)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot disable stream while demuxer is running");
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
    // popFrame 不阻塞，持锁调用安全；加锁防止与 disableStream 并发导致 use-after-free
    std::lock_guard<std::mutex> lock(m_stateMutex);
    auto it = m_decodeWorkers.find(streamIndex);
    if (it == m_decodeWorkers.end()) {
        LOG_ERROR(m_logger, "Stream not found");
        return std::make_unique<MediaFrame>(FrameType::Empty);
    }

    auto& worker = it->second;
    auto frame = worker->popFrame();
    return std::make_unique<MediaFrame>(std::move(frame));
}

int64_t MediaSource::duration() const
{
    return m_packetCache ? m_packetCache->demuxer().duration() : 0;
}

void MediaSource::seek(int64_t timestamp)
{
    m_seekTarget = timestamp;
    if (m_demuxRunning.load()) {
        m_packetCache->interrupt();
    }
    std::lock_guard<std::mutex> lock(m_stateMutex);
    for (auto& decodeWorker: m_decodeWorkers) {
        decodeWorker.second->interrupt();
    }
}

StartError MediaSource::start()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_demuxRunning.load()) {
        LOG_ERROR(m_logger, "Already started");
        return StartError::AlreadyStarted;
    }

    if (m_decodeWorkers.empty()) {
        LOG_ERROR(m_logger, "No stream enabled");
        return StartError::NoStreamEnabled;
    }

    m_packetCache->resume();
    m_demuxRunning.store(true);
    m_demuxThread = std::thread(&MediaSource::demuxWorkerFunc, this);

    return StartError::None;
}

void MediaSource::stop()
{
    // 必须持锁 join：保证 demux 线程存活期间没有其他线程能拿到锁去写 m_decodeWorkers
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_demuxRunning.load()) {
        m_demuxRunning.store(false);
        m_packetCache->cancel();
        if (m_demuxThread.joinable()) {
            m_demuxThread.join();
        }
        // cancel 只是为了打断 demux 线程的阻塞读，线程 join 后使命即完成；
        // 恢复 demuxer 的可用状态，否则下面的 seek(0) 与 stop 后再 enable 时的探测读包会直接 AVERROR_EXIT
        m_packetCache->resume();
    }
    // 全量复位：移除所有已启用的流(解码线程与队列随 worker 销毁)，demuxer 回到文件开头，
    // 未完成的 seek 请求一并作废。stop() 幂等，未运行时调用同样执行复位
    m_seekTarget.store(AV_NOPTS_VALUE);
    m_decodeWorkers.clear();
    if (m_packetCache->seek(0) < 0) {
        LOG_ERROR(m_logger, "Failed to seek to beginning on stop, demuxer position is not reset");
    }
}

MediaSource::MediaSource(const Options& options)
    : m_source(options.source),
      m_logDir(options.logDir),
      m_instanceName(options.instanceName),
      m_onSeekComplete(options.onSeekComplete),
      m_onRunningError(options.onRunningError)
{
}

CreateError MediaSource::initialize()
{
    if (m_instanceName.empty()) {
        m_instanceName = "default";
    }
    if (!m_logDir.empty()) {
        if (spdlog::get(m_instanceName)) {
            return CreateError::LoggerNameAlreadyExists;
        }
        try {
            m_logger = createLogger(m_instanceName, m_logDir);
        } catch (const std::exception&) {
            if (spdlog::get(m_instanceName)) {
                return CreateError::LoggerNameAlreadyExists;
            }
            return CreateError::LoggerCreationFailed;
        }
    }

    if (!m_onRunningError || !m_onSeekComplete) {
        LOG_ERROR(m_logger, "Invalid argument: onRunningError and onSeekComplete callbacks must be provided");
        return CreateError::InvalidArgument;
    }

    LOG_INFO(m_logger, "Initializing MediaSource. Source: {}", m_source);

    auto demuxer = Demuxer::create({
        m_source,
        m_logger
    });
    if (!demuxer) {
        LOG_ERROR(m_logger, "Failed to create demuxer");
        return CreateError::FileOpenFailed;
    }
    // Demuxer 的所有权交给 PacketCache，读取包与 seek 都经过缓存层
    m_packetCache = std::make_unique<PacketCache>(std::move(demuxer), m_logger);

    // 至少要有一个视频流或音频流
    int streamCount = m_packetCache->demuxer().streamCount();
    if (streamCount == 0) {
        LOG_ERROR(m_logger, "No valid stream found");
        return CreateError::NoValidStream;
    }
    bool hasVideoOrAudio = false;
    for (int i = 0; i < streamCount; ++i) {
        const AVStream* stream = m_packetCache->demuxer().stream(i);
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

void MediaSource::demuxWorkerFunc()
{
    // 此函数运行在 m_demuxThread 上，禁止获取 m_stateMutex：
    // stop() 持锁 join 本线程，加了就死锁。
    // 对 m_decodeWorkers 的无锁读是安全的：修改 map 的接口都要求 m_demuxRunning 为 false，
    // 而 stop() 会在放锁前 join 掉本线程（见 MediaSource.h 中 m_stateMutex 的说明）。
    int retryCount{0};
    bool end{false};
    while (m_demuxRunning.load()) {
        auto seekTarget = m_seekTarget.exchange(AV_NOPTS_VALUE);
        if (seekTarget != AV_NOPTS_VALUE) {
            if (!m_demuxRunning.load()) {
                break;
            }
            m_packetCache->resetInterrupt();
            int ret = m_packetCache->seek(seekTarget);
            if (ret < 0) {
                if (ret == AVERROR_EXIT && !m_demuxRunning.load()) {
                    break;
                }
                if (ret == AVERROR_EXIT && m_seekTarget.load() != AV_NOPTS_VALUE) {
                    continue;
                }
                LOG_ERROR(m_logger, "Seek failed. error: {}", fh::err2str(ret));
                m_onSeekComplete(SeekError::Unknown);
                continue;
            }
            retryCount = 0;
            end = false;
            // seek 已由 PacketCache 代理，成功时缓存随之清空
            // 插入 Flash 包提醒解码线程
            for (auto& worker: m_decodeWorkers) {
                worker.second->pushPacket(MediaPacket{PacketType::Flush, nullptr});
            }
            m_onSeekComplete(SeekError::None);
        }
        if (end) {
            // 100ms 既不会太影响体验(感知不明显)，也不会太频繁占用CPU性能
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto packetExp = m_packetCache->readPacket();
        if (!packetExp.has_value()) {
            int err = packetExp.error();
            if (err == AVERROR_EXIT) {
                if (!m_demuxRunning.load()) {
                    break;
                }
                continue;
            }
            if (err == AVERROR_EOF) {
                // 为所有流都插入End包
                for (auto& worker: m_decodeWorkers) {
                    worker.second->pushPacket(MediaPacket{PacketType::End, nullptr});
                }
                end = true;
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
        worker->pushPacket(MediaPacket{PacketType::Normal, std::move(packet)});
    }
}

std::unique_ptr<Decoder> MediaSource::createDecoder(const AVStream* stream, bool hwaccel, void* hwctx)
{
    Decoder::Options opts{};
    if (hwaccel) {
#ifdef _WIN32
        opts.type = Decoder::Type::D3d11va;
        opts.hardwareContext = hwctx;
#else
        // 硬解不支持时直接失败，不回退软解：调用方是按硬解输出格式(如 D3D11 纹理)准备的渲染路径
        LOG_ERROR(m_logger, "Hardware acceleration is not supported on this platform");
        return nullptr;
#endif
    } else {
        opts.type = Decoder::Type::Software;
    }
    opts.stream = stream;
    opts.logger = m_logger;
    return Decoder::create(opts);
}

std::unique_ptr<DecodeWorker> MediaSource::createVideoDecodeWorker(const AVStream* stream, std::unique_ptr<Decoder>&& decoder, FrameProcessorPtr processor)
{
    // 估算包队列长度
    // 目标是视频缓存10秒，音频缓存3秒
    // 由于包不携带时长信息（一包不一定对应一帧），视频根据帧率估算，音频根据采样率估算
    const auto frameRate = stream->codecpar->framerate;
    int fps = frameRate.den > 0 ? frameRate.num / frameRate.den : 0;
    fps = fps > 0 ? fps : 120;  // 留足余量，不差这点内存
    int packetQueueLength = fps * 10; // 10秒的视频缓存
    LOG_INFO(m_logger, "Estimated video packet queue length: {}", packetQueueLength);

    DecodeWorker::Options decodeWorkerOpts;
    decodeWorkerOpts.maxPacketQueueSize = packetQueueLength;
    decodeWorkerOpts.maxFrameQueueSize = 5;
    decodeWorkerOpts.processor = std::move(processor);
    decodeWorkerOpts.logger = m_logger;
    return DecodeWorker::create(decodeWorkerOpts, std::move(decoder));
}
std::unique_ptr<DecodeWorker> MediaSource::createAudioDecodeWorker(const AVStream* stream, std::unique_ptr<Decoder>&& decoder, FrameProcessorPtr processor)
{
    // 估算包队列长度
    // 目标是视频缓存10秒，音频缓存3秒
    // 由于包不携带时长信息（一包不一定对应一帧），视频根据帧率估算，音频根据采样率估算
    const int sampleRate = stream->codecpar->sample_rate;
    const int frameSize = stream->codecpar->frame_size;
    const int packetsPerSecond = sampleRate > 0 && frameSize > 0
        ? (sampleRate + frameSize - 1) / frameSize
        : 100;
    const int packetQueueLength = std::max(1, packetsPerSecond * 3);
    LOG_INFO(m_logger, "Estimated audio packet queue length: {}", packetQueueLength);

    DecodeWorker::Options decodeWorkerOpts;
    decodeWorkerOpts.maxPacketQueueSize = packetQueueLength;
    decodeWorkerOpts.maxFrameQueueSize = 30;
    decodeWorkerOpts.processor = std::move(processor);
    decodeWorkerOpts.logger = m_logger;
    return DecodeWorker::create(decodeWorkerOpts, std::move(decoder));
}
} // namespace media