#include "Demuxer.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
std::unique_ptr<Demuxer> Demuxer::create(const Options& options)
{
    std::unique_ptr<Demuxer> demuxer(new Demuxer(options));
    bool ret = demuxer->initialize();
    if (!ret) {
        return nullptr;
    }
    return demuxer;
}

Demuxer::Demuxer(const Options& options)
    : m_url(options.url)
    , m_logger(options.logger)
{
}

bool Demuxer::initialize()
{
    auto ctxExp = fh::makeInputContext(m_url.c_str(), {&Demuxer::interruptCallback, this});
    if (!ctxExp) {
        int err = ctxExp.error();
        LOG_ERROR(m_logger, "Failed to create input context: {}", fh::err2str(err));
        return false;
    }
    m_inputContext = std::move(ctxExp.value());
    int ret = avformat_find_stream_info(m_inputContext.get(), nullptr);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to find stream info: {}", fh::err2str(ret));
        return false;
    }

    return true;
}

int Demuxer::interruptCallback(void* opaque)
{
    auto* demuxer = static_cast<Demuxer*>(opaque);
    return demuxer->m_interrupted.load() || demuxer->m_cancelled.load();
}

std::expected<fh::PacketPtr, int> Demuxer::readPacket()
{
    fh::PacketPtr packet = fh::makePacket();
    if (!packet) {
        LOG_ERROR(m_logger, "Failed to allocate packet");
        return std::unexpected(AVERROR(ENOMEM));
    }
    int ret = av_read_frame(m_inputContext.get(), packet.get());
    if (ret != 0) {
        return std::unexpected(ret);
    }
    return packet;
}

void Demuxer::interrupt()
{
    m_interrupted.store(true);
}

void Demuxer::resetInterrupt()
{
    m_interrupted.store(false);
}

void Demuxer::cancel()
{
    m_cancelled.store(true);
}

void Demuxer::resume()
{
    m_interrupted.store(false);
    m_cancelled.store(false);
}

int Demuxer::seek(int64_t timestamp)
{
    if (!m_inputContext) {
        return AVERROR(EINVAL);
    }
    int ret = av_seek_frame(m_inputContext.get(), -1, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to seek: {}", fh::err2str(ret));
    }
    return ret;
}

int Demuxer::streamCount() const
{
    if (!m_inputContext) {
        return 0;
    }
    return m_inputContext->nb_streams;
}

const AVStream* Demuxer::stream(int index) const
{
    if (!m_inputContext || index < 0 || index >= m_inputContext->nb_streams) {
        return nullptr;
    }
    return m_inputContext->streams[index];
}

AVStream* Demuxer::stream(int index)
{
    if (!m_inputContext || index < 0 || index >= m_inputContext->nb_streams) {
        return nullptr;
    }
    return m_inputContext->streams[index];
}

int64_t Demuxer::duration() const
{
    if (!m_inputContext) {
        return 0;
    }
    return m_inputContext->duration;
}

} // namespace media