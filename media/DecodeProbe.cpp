#include "DecodeProbe.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
DecodeProbe::DecodeProbe(
    const std::unique_ptr<Demuxer>& demuxer,
    const std::unique_ptr<Decoder>& decoder,
    int streamIndex,
    LoggerPtr logger
) : m_demuxer(demuxer), m_decoder(decoder), m_streamIndex(streamIndex), m_logger(logger)
{}

fh::FramePtr DecodeProbe::testDecode()
{
    fh::FramePtr retFrame{nullptr};
    int streamPackets{0};
    int totalPackets{0};
    int budget = getBudget();
    while (streamPackets < budget && totalPackets < 300) {
        auto packetExp = m_demuxer->readPacket();
        if (!packetExp) {
            int err = packetExp.error();
            LOG_ERROR(m_logger, "Failed to read packet. {}", fh::err2str(err));
            break;
        }
        totalPackets++;
        auto packet = std::move(packetExp.value());
        if (packet->stream_index != m_streamIndex) {
            continue;
        }
        streamPackets++;

        int ret = m_decoder->sendPacket(std::move(packet));
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to send packet. {}", fh::err2str(ret));
            break;
        }

        auto frameExp = m_decoder->receiveFrame();
        if (!frameExp) {
            int err = frameExp.error();
            if (err == AVERROR(EAGAIN)) {
                continue;
            }
            LOG_ERROR(m_logger, "Failed to receive frame. {}", fh::err2str(err));
            break;
        }
        retFrame = std::move(frameExp.value());
        break;
    }
    return retFrame;
}

int DecodeProbe::getBudget()
{
    if (m_demuxer->stream(m_streamIndex)->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        return std::min(m_decoder->threadCount() + m_decoder->hasBFrames() + 8, 48);
    }
    return std::min(m_decoder->threadCount() + 8, 48);
}

} // namespace media