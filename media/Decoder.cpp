#include "Decoder.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
std::expected<Decoder, int> Decoder::create(const Options& options)
{
    Decoder decoder(options);
    int err = decoder.initialize(options.stream);
    if (err != 0) {
        return std::unexpected(err);
    }
    return std::move(decoder);
}

Decoder::Decoder(const Options& options)
    : m_type(options.type),
      m_logger(options.logger)
{
}

int Decoder::initialize(const AVStream* stream)
{
    FUNC_TRACE(m_logger, LogLevel::info);
    int ret = createContext(stream);
    if (ret != 0) {
        return ret;
    }
    return openCodec();
}

int Decoder::createContext(const AVStream* stream)
{
    FUNC_TRACE(m_logger, LogLevel::info);
    if (!stream) {
        LOG_ERROR(m_logger, "Stream is null");
        return AVERROR(EINVAL);
    }
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        LOG_ERROR(m_logger, "Decoder not found for codec id {}", stream->codecpar->codec_id);
        return AVERROR_DECODER_NOT_FOUND;
    }
    m_codecContext = fh::makeCodecContext(codec);
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Failed to allocate codec context");
        return AVERROR(ENOMEM);
    }
    int ret = avcodec_parameters_to_context(m_codecContext.get(), stream->codecpar);
    if (ret != 0) {
        LOG_ERROR(m_logger, "Failed to copy codec parameters to context: {}", fh::err2str(ret));
        return ret;
    }
    m_codecContext->codec = codec;
    m_codecContext->time_base = stream->time_base;
    m_codecContext->pkt_timebase = stream->time_base;
    m_codecContext->thread_count = 0; // Let FFmpeg decide the optimal number of threads
    return 0;
}

int Decoder::openCodec()
{
    FUNC_TRACE(m_logger, LogLevel::info);
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Codec context is not initialized");
        return AVERROR(EINVAL);
    }
    int ret = avcodec_open2(m_codecContext.get(), m_codecContext->codec, nullptr);
    if (ret != 0) {
        LOG_ERROR(m_logger, "Failed to open codec: {}", fh::err2str(ret));
        return ret;
    }
    return 0;
}

int Decoder::sendPacket(fh::PacketPtr&& packet)
{
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Codec context is not initialized");
        return AVERROR(EINVAL);
    }
    int ret = avcodec_send_packet(m_codecContext.get(), packet.get());
    if (ret != 0) {
        return ret;
    }
    return 0;
}

std::expected<fh::FramePtr, int> Decoder::receiveFrame()
{
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Codec context is not initialized");
        return std::unexpected(AVERROR(EINVAL));
    }
    fh::FramePtr frame = fh::makeFrame();
    if (!frame) {
        LOG_ERROR(m_logger, "Failed to allocate frame");
        return std::unexpected(AVERROR(ENOMEM));
    }
    int ret = avcodec_receive_frame(m_codecContext.get(), frame.get());
    if (ret != 0) {
        return std::unexpected(ret);
    }
    return frame;
}

} // namespace media