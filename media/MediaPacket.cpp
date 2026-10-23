#include "MediaPacket.h"

namespace media {
std::optional<MediaPacket> MediaPacket::createPacket(PacketType type)
{
    if (type == PacketType::Normal) {
        auto packet = fh::makePacket();
        if (!packet) {
            return std::nullopt;
        }
        return MediaPacket(type, std::move(packet));
    }
    return MediaPacket(type, nullptr);
}

MediaPacket::MediaPacket(PacketType type, fh::PacketPtr&& packet)
    : m_type(type), m_packet(std::move(packet))
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