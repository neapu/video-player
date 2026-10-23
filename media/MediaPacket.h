#pragma once
#include <optional>
#include "ffmpeg_helper/Packet.h"

namespace media {
enum class PacketType {
    Empty,
    Normal,
    End,
    Flush
};
class MediaPacket {
public:
    static std::optional<MediaPacket> createPacket(PacketType type);

    MediaPacket(PacketType type = PacketType::Empty, fh::PacketPtr&& packet = nullptr);
    MediaPacket(const MediaPacket&) = delete;
    MediaPacket& operator=(const MediaPacket&) = delete;
    MediaPacket(MediaPacket&&) = default;
    MediaPacket& operator=(MediaPacket&&) = default;
    ~MediaPacket() = default;

    PacketType type() const { return m_type; }
    
    fh::PacketPtr releasePacket();

private:
    PacketType m_type{PacketType::Empty};
    fh::PacketPtr m_packet;

};

} // namespace media