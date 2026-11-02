#include "Decoder.h"
#include "ffmpeg_helper/FFmpegError.h"
#ifdef _WIN32
#include "D3d11vaDecoder.h"
#endif

namespace media {
std::unique_ptr<Decoder> Decoder::create(const Options& options)
{
    switch (options.type) {
        case Decoder::Type::D3d11va:
#ifdef _WIN32
            return D3d11vaDecoder::create(options);
#else
            return nullptr;
#endif
        case Decoder::Type::Software:
            {
                std::unique_ptr<Decoder> decoder(new Decoder(options));
                if (!decoder->initialize(options.stream)) {
                    return nullptr;
                }
                return decoder;
            }
        default:
            LOG_ERROR(options.logger, "Unsupported decoder type");
            return nullptr;
    }
}

Decoder::Decoder(const Options& options)
    : m_type(options.type),
      m_logger(options.logger)
{
}

bool Decoder::initialize(const AVStream* stream)
{
    FUNC_TRACE(m_logger, LogLevel::info);
    bool ret = createContext(stream);
    if (!ret) {
        return false;
    }
    ret = createHardwareContext(stream);
    if (!ret) {
        return false;
    }
    return openCodec();
}

bool Decoder::createContext(const AVStream* stream)
{
    FUNC_TRACE(m_logger, LogLevel::info);
    if (!stream) {
        LOG_ERROR(m_logger, "Stream is null");
        return false;
    }
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        LOG_ERROR(m_logger, "Decoder not found for codec id {}", static_cast<int>(stream->codecpar->codec_id));
        return false;
    }
    m_codecContext = fh::makeCodecContext(codec);
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Failed to allocate codec context");
        return false;
    }
    int ret = avcodec_parameters_to_context(m_codecContext.get(), stream->codecpar);
    if (ret != 0) {
        LOG_ERROR(m_logger, "Failed to copy codec parameters to context: {}", fh::err2str(ret));
        return false;
    }
    m_codecContext->codec = codec;
    m_codecContext->time_base = stream->time_base;
    m_codecContext->pkt_timebase = stream->time_base;
    m_codecContext->thread_count = 0; // Let FFmpeg decide the optimal number of threads
    return true;
}

bool Decoder::openCodec()
{
    FUNC_TRACE(m_logger, LogLevel::info);
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Codec context is not initialized");
        return false;
    }
    int ret = avcodec_open2(m_codecContext.get(), m_codecContext->codec, nullptr);
    if (ret != 0) {
        LOG_ERROR(m_logger, "Failed to open codec: {}", fh::err2str(ret));
        return false;
    }
    return true;
}

int Decoder::threadCount() const
{
    return m_codecContext ? m_codecContext->thread_count : 1;
}

int Decoder::hasBFrames() const
{
    return m_codecContext ? m_codecContext->has_b_frames : 0;
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

void Decoder::flush()
{
    if (!m_codecContext) {
        LOG_ERROR(m_logger, "Codec context is not initialized");
        return;
    }
    avcodec_flush_buffers(m_codecContext.get());
}

} // namespace media