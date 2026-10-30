#include "MediaSource.h"
#include "ffmpeg_helper/FFmpegError.h"
#include <exception>
#include "SwsConvertor.h"
#include "SwrConvertor.h"

namespace media {
std::string errToString(CreateError err)
{
    switch (err) {
        case CreateError::None: return "None";
        case CreateError::FileOpenFailed: return "Failed to open file";
        case CreateError::NoValidStream: return "No valid stream";
        case CreateError::InvalidArgument: return "Invalid argument";
        case CreateError::LoggerCreationFailed: return "Logger creation failed";
        case CreateError::LoggerNameAlreadyExists: return "Logger name exists";
        default: return "Unknown";
    }
}
std::string errToString(RunningError err)
{
    switch (err) {
        case RunningError::None: return "None";
        case RunningError::ReadFailed: return "Read failed";
        default: return "Unknown";
    }
}
std::string errToString(StreamError err)
{
    switch (err) {
        case StreamError::None: return "None";
        case StreamError::InvalidIndex: return "Invalid Stream index";
        case StreamError::DemuxerRunning: return "Demuxer running, can't operate stream";
        case StreamError::StreamAlreadyEnabled: return "Stream already enabled";
        case StreamError::StreamNotEnabled: return "Stream not enabled";
        case StreamError::DecoderCreationFailed: return "Decoder creation failed";
        case StreamError::StreamTypeMismatch: return "Stream type mismatch";
        case StreamError::RequestFormatEmpty: return "Request format empty";
        default: return "Unknown";
    }
}
std::string errToString(SeekError err)
{
    switch (err) {
        case SeekError::None: return "None";
        case SeekError::NotSupported: return "Not supported";
        default: return "Unknown";
    }
}
std::string errToString(StartError err)
{
    switch (err) {
        case StartError::None: return "None";
        case StartError::AlreadyStarted: return "Already started";
        case StartError::NoStreamEnabled: return "No stream enabled";
        default: return "Unknown";
    }
}
std::string errToString(ProcessError err)
{
    switch (err) {
        case ProcessError::None: return "None";
        default: return "Unknown";
    }
}

std::expected<std::unique_ptr<IMediaSource>, CreateError> IMediaSource::create(const Options& options)
{
    return MediaSource::create(options);
}

MediaSource::~MediaSource()
{
    stop();
    // 释放所有解码器
    m_decodeWorkers.clear();
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
    int count = m_demuxer->streamCount();
    std::vector<StreamInfo> infos;
    for (int i = 0; i < count; ++i) {
        const AVStream* stream = m_demuxer->stream(i);
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
    auto streamExp = validateVideoStream(streamIndex, requirePixelFormats);
    if (!streamExp) {
        return std::unexpected(streamExp.error());
    }
    auto* stream = *streamExp;

    auto decoder = createVideoDecoder(stream, hardwareAccelerated, hardwareContext);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create decoder for video stream");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    auto testResult = testVideoDecoder(*decoder, streamIndex, requirePixelFormats);
    if (!testResult) {
        LOG_ERROR(m_logger, "Decoder test failed");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    auto decodeWorker = createVideoDecodeWorker(stream, std::move(decoder), testResult->convertor);
    if (!decodeWorker) {
        LOG_ERROR(m_logger, "Failed to create decode worker");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    m_decodeWorkers[streamIndex] = std::move(decodeWorker);

    return testResult->pixelFormat;
}

std::expected<SampleFormat, StreamError> MediaSource::enableAudioStream(int streamIndex, std::vector<SampleFormat> requireSampleFormats)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    auto streamExp = validateAudioStream(streamIndex, requireSampleFormats);
    if (!streamExp) {
        return std::unexpected(streamExp.error());
    }
    auto* stream = *streamExp;

    auto decoder = createAudioDecoder(stream);
    if (!decoder) {
        LOG_ERROR(m_logger, "Failed to create decoder for audio stream");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    auto testResult = testAudioDecoder(*decoder, streamIndex, requireSampleFormats);
    if (!testResult) {
        LOG_ERROR(m_logger, "Audio decoder test failed");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }

    auto decodeWorker = createAudioDecodeWorker(stream, std::move(decoder), testResult->convertor);
    if (!decodeWorker) {
        LOG_ERROR(m_logger, "Failed to create audio decode worker");
        return std::unexpected(StreamError::DecoderCreationFailed);
    }
    m_decodeWorkers[streamIndex] = std::move(decodeWorker);

    return testResult->sampleFormat;
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
    return m_demuxer ? m_demuxer->duration() : 0;
}

void MediaSource::seek(int64_t timestamp)
{
    m_seekTarget = timestamp;
    if (m_demuxRunning.load()) {
        m_demuxer->interrupt();
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

    m_demuxer->resume();
    m_demuxRunning.store(true);
    m_demuxThread = std::thread(&MediaSource::demuxWorkerFunc, this);

    return StartError::None;
}

void MediaSource::stop()
{
    // 必须持锁 join：保证 demux 线程存活期间没有其他线程能拿到锁去写 m_decodeWorkers
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (!m_demuxRunning.load()) {
        return;
    }
    m_demuxRunning.store(false);
    m_demuxer->cancel();
    for (auto& decodeWorker: m_decodeWorkers) {
        decodeWorker.second->interrupt();
    }

    if (m_demuxThread.joinable()) {
        m_demuxThread.join();
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

std::expected<AVStream*, StreamError> MediaSource::validateVideoStream(
    int streamIndex,
    const std::vector<PixelFormat>& requirePixelFormats
) const
{
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot enable video stream while demuxer is running");
        return std::unexpected(StreamError::DemuxerRunning);
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        LOG_ERROR(m_logger, "Video stream already enabled");
        return std::unexpected(StreamError::StreamAlreadyEnabled);
    }

    if (streamIndex < 0 || streamIndex >= m_demuxer->streamCount()) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return std::unexpected(StreamError::InvalidIndex);
    }

    AVStream* stream = m_demuxer->stream(streamIndex);
    if (!stream) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        return std::unexpected(StreamError::StreamTypeMismatch);
    }

    if (requirePixelFormats.empty()) {
        return std::unexpected(StreamError::RequestFormatEmpty);
    }

    return stream;
}

std::expected<AVStream*, StreamError> MediaSource::validateAudioStream(
    int streamIndex,
    const std::vector<SampleFormat>& requireSampleFormats
) const
{
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot enable audio stream while demuxer is running");
        return std::unexpected(StreamError::DemuxerRunning);
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        LOG_ERROR(m_logger, "Audio stream already enabled");
        return std::unexpected(StreamError::StreamAlreadyEnabled);
    }

    if (streamIndex < 0 || streamIndex >= m_demuxer->streamCount()) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return std::unexpected(StreamError::InvalidIndex);
    }

    AVStream* stream = m_demuxer->stream(streamIndex);
    if (!stream) {
        LOG_ERROR(m_logger, "Invalid stream index");
        return std::unexpected(StreamError::InvalidIndex);
    }

    if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        LOG_ERROR(m_logger, "Stream type mismatch: expected audio");
        return std::unexpected(StreamError::StreamTypeMismatch);
    }

    if (requireSampleFormats.empty()) {
        LOG_ERROR(m_logger, "Requested sample formats are empty");
        return std::unexpected(StreamError::RequestFormatEmpty);
    }

    return stream;
}

std::unique_ptr<Decoder> MediaSource::createVideoDecoder(AVStream* stream, bool hardwareAccelerated, void* hardwareContext)
{
    Decoder::Options decoderOptions{};
    if (hardwareAccelerated) {
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
    return Decoder::create(decoderOptions);
}

std::unique_ptr<Decoder> MediaSource::createAudioDecoder(AVStream* stream)
{
    Decoder::Options decoderOptions{};
    decoderOptions.type = Decoder::Type::Software;
    decoderOptions.stream = stream;
    decoderOptions.logger = m_logger;
    return Decoder::create(decoderOptions);
}

std::optional<MediaSource::VideoDecoderTestResult> MediaSource::testVideoDecoder(
    Decoder& decoder,
    int streamIndex,
    const std::vector<PixelFormat>& requirePixelFormats
)
{
    VideoDecoderTestResult result{PixelFormat::None, nullptr};
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
        int ret = decoder.sendPacket(std::move(packet));
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to send packet to decoder. Error: {}", fh::err2str(ret));
            break;
        }
        bool receiveError{false};
        while (true) {
            auto frameExp = decoder.receiveFrame();
            if (!frameExp) {
                int err = frameExp.error();
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame from decoder. Error: {}", fh::err2str(err));
                receiveError = true;
                break;
            }
            auto frame = std::move(*frameExp);
            // 非硬解码的情况，需要测试转换器，如果目标格式中不包含当前帧格式，需要创建转换器并测试
            auto frameFmt = MediaFrame::toPixelFormat(static_cast<AVPixelFormat>(frame->format));
            if (decoder.type() == Decoder::Type::Software && std::find(requirePixelFormats.begin(), requirePixelFormats.end(), frameFmt) == requirePixelFormats.end()) {
                auto swsOpt = createSwsConvertor(frame, MediaFrame::toAvPixelFormat(requirePixelFormats[0]));
                if (!swsOpt) break;
                result.convertor = *swsOpt;
                if (result.convertor) {
                    result.pixelFormat = MediaFrame::toPixelFormat(result.convertor->targetFormat());
                }
            } else {
                result.pixelFormat = frameFmt;
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
        return std::nullopt;
    }
    decoder.flush();
    return result;
}

std::optional<MediaSource::AudioDecoderTestResult> MediaSource::testAudioDecoder(
    Decoder& decoder,
    int streamIndex,
    const std::vector<SampleFormat>& requireSampleFormats
)
{
    AudioDecoderTestResult result{SampleFormat::None, nullptr};
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
        int ret = decoder.sendPacket(std::move(packet));
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to send packet to decoder. Error: {}", fh::err2str(ret));
            break;
        }

        bool receiveError{false};
        while (true) {
            auto frameExp = decoder.receiveFrame();
            if (!frameExp) {
                int err = frameExp.error();
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame from decoder. Error: {}", fh::err2str(err));
                receiveError = true;
                break;
            }

            auto frame = std::move(*frameExp);
            const auto frameFormat = MediaFrame::toSampleFormat(static_cast<AVSampleFormat>(frame->format));
            if (frameFormat != SampleFormat::None &&
                std::find(requireSampleFormats.begin(), requireSampleFormats.end(), frameFormat) != requireSampleFormats.end()) {
                result.sampleFormat = frameFormat;
            } else {
                auto targetFormat = std::find_if(requireSampleFormats.begin(), requireSampleFormats.end(), [](SampleFormat format) {
                    return MediaFrame::toAvSampleFormat(format) != AV_SAMPLE_FMT_NONE;
                });
                if (targetFormat == requireSampleFormats.end()) {
                    LOG_ERROR(m_logger, "No supported requested audio sample format");
                    receiveError = true;
                    break;
                }

                SwrConvertor::Options convertorOptions{};
                convertorOptions.targetFormat = MediaFrame::toAvSampleFormat(*targetFormat);
                convertorOptions.logger = m_logger;
                auto convertor = std::make_shared<SwrConvertor>(convertorOptions);
                if (!convertor->process(frame)) {
                    LOG_ERROR(m_logger, "Failed to convert decoded audio frame to requested sample format");
                    receiveError = true;
                    break;
                }
                result.sampleFormat = *targetFormat;
                result.convertor = std::move(convertor);
            }

            testSuccess = true;
            break;
        }
        if (receiveError) {
            break;
        }
        if (testSuccess) {
            break;
        }
    }

    m_demuxer->seek(0);
    if (!testSuccess) {
        return std::nullopt;
    }
    decoder.flush();
    return result;
}

DecodeWorkerPtr MediaSource::createVideoDecodeWorker(
    AVStream* stream,
    std::unique_ptr<Decoder>&& decoder,
    FrameProcessorPtr processor
)
{
    // 根据解码器类型，估算包队列长度
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

DecodeWorkerPtr MediaSource::createAudioDecodeWorker(
    AVStream* stream,
    std::unique_ptr<Decoder>&& decoder,
    FrameProcessorPtr processor
)
{
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
            m_demuxer->resetInterrupt();
            int ret = m_demuxer->seek(seekTarget);
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

        auto packetExp = m_demuxer->readPacket();
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

std::optional<std::shared_ptr<SwsConvertor>> MediaSource::createSwsConvertor(const fh::FramePtr& frame, AVPixelFormat target)
{
    if (!frame) return std::nullopt;
    SwsConvertor::Options opts{};
    opts.targetFormat = target;
    auto sws = std::make_shared<SwsConvertor>(opts);
    auto retFrame = sws->process(frame);
    if (!retFrame) return std::nullopt;
    return sws;
}

} // namespace media