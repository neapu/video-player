#include "DecodeProbe.h"
#include "ffmpeg_helper/FFmpegError.h"
#include <algorithm>

extern "C" {
#include <libavcodec/codec_par.h>
}

namespace media {
namespace {
// 出首帧预算的余量与上限：帧级多线程引入 thread_count-1 帧的管线延迟，
// 叠加 has_b_frames 的重排延迟，出首帧约需 thread_count + has_b_frames 个流包；
// margin 覆盖解析器漏报等情况，max 保证异常码流下探测耗时有界。
// 预算只计目标流的包（其他流的包不消耗预算），排空兜底见 testDecode。
constexpr int PROBE_PACKET_BUDGET_MARGIN = 8;
constexpr int PROBE_PACKET_BUDGET_MAX = 48;

// 单次探测的总包上限（含其他流的包，音视频交错极端稀疏时的安全上限）。
// 与 PacketCache 容量耦合：取同一常量，保证首轮探测读到的包都能入缓存
constexpr int PROBE_TOTAL_PACKET_CAP = static_cast<int>(PacketCache::PACKET_CACHE_CAPACITY);
}

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
    // 硬错误（读包/发包/收帧失败）下排空也无济于事，直接失败；
    // 预算耗尽或 EOF 属于输入不足，走排空兜底
    bool hardError{false};
    int streamPackets{0};
    int totalPackets{0};
    int budget = getBudget();
    while (streamPackets < budget && totalPackets < PROBE_TOTAL_PACKET_CAP) {
        auto packetExp = m_cache.probeReadPacket();
        if (!packetExp) {
            int err = packetExp.error();
            if (err != AVERROR_EOF) { // EOF 是输入耗尽的正常终止，不算硬错误
                LOG_ERROR(m_logger, "Failed to read packet. {}", fh::err2str(err));
                hardError = true;
            }
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
            hardError = true;
            break;
        }

        auto frameExp = m_decoder->receiveFrame();
        if (!frameExp) {
            int err = frameExp.error();
            if (err == AVERROR(EAGAIN)) {
                continue;
            }
            LOG_ERROR(m_logger, "Failed to receive frame. {}", fh::err2str(err));
            hardError = true;
            break;
        }
        retFrame = std::move(frameExp.value());
        break;
    }
    if (!retFrame && !hardError) {
        // 预算耗尽或输入不足时发空包进入排空模式：帧线程管线中滞留的帧会被强制吐出，
        // 包数不足以填满管线深度的少帧/单帧文件依赖此路径出首帧
        int ret = m_decoder->sendPacket(nullptr);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            LOG_ERROR(m_logger, "Failed to drain decoder. {}", fh::err2str(ret));
        } else {
            auto frameExp = m_decoder->receiveFrame();
            if (frameExp) {
                retFrame = std::move(frameExp.value());
                LOG_INFO(m_logger, "Got first frame after drain. Decoder output delay exceeded probe budget "
                    "(stream packets: {}, total packets: {}, budget: {}).", streamPackets, totalPackets, budget);
            } else if (frameExp.error() != AVERROR_EOF) {
                LOG_ERROR(m_logger, "Failed to receive frame. {}", fh::err2str(frameExp.error()));
            }
        }
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
        return std::min(m_decoder->threadCount() + m_decoder->hasBFrames() + PROBE_PACKET_BUDGET_MARGIN,
                        PROBE_PACKET_BUDGET_MAX);
    }
    // 音频解码没有重排管线，管线深度按帧线程数估算即可
    return std::min(m_decoder->threadCount() + PROBE_PACKET_BUDGET_MARGIN, PROBE_PACKET_BUDGET_MAX);
}

} // namespace media
