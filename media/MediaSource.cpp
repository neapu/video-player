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
    return 0;
}

void MediaSource::seek(int64_t timestamp)
{
}

void MediaSource::start()
{

}

void MediaSource::stop()
{
}

} // namespace media