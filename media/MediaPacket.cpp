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

AVPacket* MediaPacket::avPacket()
{
    return m_packet.get();
}

} // namespace media