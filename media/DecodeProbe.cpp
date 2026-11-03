#include "DecodeProbe.h"
#include "ffmpeg_helper/FFmpegError.h"
#include <algorithm>

extern "C" {
#include <libavcodec/codec_par.h>
}

namespace media {
DecodeProbe::DecodeProbe(
    PacketCache& cache,
    const std::unique_ptr<Decoder>& decoder,
    int streamIndex,
    LoggerPtr logger
) : m_cache(cache), m_decoder(decoder), m_streamIndex(streamIndex), m_logger(logger)
{}

fh::FramePtr DecodeProbe::testDecode()
{
    // 从缓存头部开始读（此前探测读到的包回放给本次探测，不产生二次丢失）
    m_cache.beginProbe();
    fh::FramePtr retFrame{nullptr};
    int streamPackets{0};
    int totalPackets{0};
    int budget = getBudget();
    while (streamPackets < budget && totalPackets < 300) {
        auto packetExp = m_cache.probeReadPacket();
        if (!packetExp) {
            int err = packetExp.error();
            LOG_ERROR(m_logger, "Failed to read packet. {}", fh::err2str(err));
            break;
        }
        totalPackets++;
        const fh::PacketPtr& packet = packetExp.value().get();
        if (packet->stream_index != m_streamIndex) {
            continue;
        }
        streamPackets++;

        int ret = m_decoder->sendPacket(packet);
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
    if (retFrame) {
        // 探测已把包喂进解码器并取走了首帧，而这个解码器会移入 DecodeWorker 重复使用，
        // demux 线程随后会从缓存头部重放同样的包；flush 后 worker 从干净状态从头解码，
        // 否则这些包会被二次解码，B 帧流还会因解码器内残留的重排状态出现开头乱序
        m_decoder->flush();
    }
    return retFrame;
}

int DecodeProbe::getBudget()
{
    const AVMediaType streamType = m_cache.demuxer().stream(m_streamIndex)->codecpar->codec_type;
    if (streamType == AVMEDIA_TYPE_VIDEO) {
        return std::min(m_decoder->threadCount() + m_decoder->hasBFrames() + 8, 48);
    }
    return std::min(m_decoder->threadCount() + 8, 48);
}

} // namespace media
