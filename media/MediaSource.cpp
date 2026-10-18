#include "MediaSource.h"

namespace media {
std::expected<std::unique_ptr<IMediaSource>, Error> IMediaSource::create(const Options& options)
{
    return MediaSource::create(options);
}

std::expected<std::unique_ptr<IMediaSource>, Error> MediaSource::create(const Options& options)
{
    auto mediaSource = std::unique_ptr<MediaSource>(new MediaSource(options));
    Error err = mediaSource->initialize();
    if (err != Error::None) {
        return std::unexpected(err);
    }
    return std::move(mediaSource);
}

MediaSource::MediaSource(const Options& options)
    : m_source(options.source),
      m_logDir(options.logDir),
      m_instanceName(options.instanceName),
      m_onSeekSuccess(options.onSeekSuccess),
      m_onError(options.onError)
{
}

MediaSource::~MediaSource()
{
}

Error MediaSource::initialize()
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
        return Error::FileOpenFailed;
    }

    LOG_INFO(m_logger, "MediaSource initialized successfully");

    return Error::None;
}

std::vector<StreamInfo> MediaSource::getStreamInfo() const
{
    return {};
}

std::expected<PixelFormat, Error> MediaSource::enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats, bool hardwareAccelerated)
{
    return std::unexpected(Error::None);
}

std::expected<SampleFormat, Error> MediaSource::enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats)
{
    return std::unexpected(Error::None);
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