#include "MediaPacket.h"

namespace media {
std::optional<MediaPacket> MediaPacket::createPacket(int64_t epoch, PacketType type)
{
    if (type == PacketType::Normal) {
        auto packet = fh::makePacket();
        if (!packet) {
            return std::nullopt;
        }
        return MediaPacket(epoch, type, std::move(packet));
    }
    return MediaPacket(epoch, type, nullptr);
}

MediaPacket::MediaPacket(int64_t epoch, PacketType type, fh::PacketPtr&& packet)
    : m_epoch(epoch), m_type(type), m_packet(std::move(packet))
{
}

fh::PacketPtr MediaPacket::releasePacket()
{
    if (!m_packet) {
        return nullptr;
    }
    m_type = PacketType::Empty;
    return std::move(m_packet);
}

} // namespace media