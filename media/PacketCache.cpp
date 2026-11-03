#include "PacketCache.h"

namespace media {
PacketCache::PacketCache(std::unique_ptr<Demuxer> demuxer, LoggerPtr logger, size_t capacity)
    : m_demuxer(std::move(demuxer)), m_logger(logger), m_capacity(capacity)
{}

std::expected<std::reference_wrapper<const fh::PacketPtr>, int> PacketCache::probeReadPacket()
{
    if (m_probeReadPos < m_cache.size()) {
        return std::ref(m_cache[m_probeReadPos++]);
    }
    auto packetExp = m_demuxer->readPacket();
    if (!packetExp) {
        return std::unexpected(packetExp.error());
    }
    if (m_cache.size() < m_capacity) {
        m_cache.push_back(std::move(packetExp.value()));
        m_probeReadPos = m_cache.size();
        return std::ref(m_cache.back());
    }
    // 缓存已满：包只送给本次探测，不参与后续回放（退化场景，如探测失败后重试且首轮已读满缓存）
    LOG_WARN(m_logger, "Probe packet cache is full, packet will not be replayed");
    m_uncachedProbePacket = std::move(packetExp.value());
    return std::ref(m_uncachedProbePacket);
}

std::expected<fh::PacketPtr, int> PacketCache::readPacket()
{
    if (!m_cache.empty()) {
        fh::PacketPtr packet = std::move(m_cache.front());
        m_cache.pop_front();
        return packet;
    }
    return m_demuxer->readPacket();
}

int PacketCache::seek(int64_t timestamp)
{
    int ret = m_demuxer->seek(timestamp);
    if (ret >= 0) {
        m_cache.clear();
        m_probeReadPos = 0;
    }
    return ret;
}

} // namespace media
