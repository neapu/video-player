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
    return Error::None;
}

std::vector<StreamInfo> MediaSource::getStreamInfo() const
{
    return {};
}

PixelFormat MediaSource::enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats)
{
    return PixelFormat::None;
}

SampleFormat MediaSource::enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats)
{
    return SampleFormat::None;
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

} // namespace media