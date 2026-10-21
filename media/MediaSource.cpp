#include "MediaSource.h"

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

std::expected<PixelFormat, StreamError> MediaSource::enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats, bool hardwareAccelerated)
{
    if (m_demuxRunning) {
        LOG_ERROR(m_logger, "Cannot enable video stream while demuxer is running");
        return std::unexpected(StreamError::DemuxerRunning);
    }

    if (m_decodeWorkers.contains(streamIndex)) {
        LOG_ERROR(m_logger, "Video stream already enabled");
        return std::unexpected(StreamError::StreamAlreadyEnabled);
    }
}

std::expected<SampleFormat, StreamError> MediaSource::enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats)
{
    return std::unexpected(StreamError::None);
}

MediaFramePtr MediaSource::nextFrame(int streamIndex)
{
    return nullptr;
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