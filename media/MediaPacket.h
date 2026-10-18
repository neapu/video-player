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
    static std::optional<MediaPacket> createPacket(int64_t epoch, PacketType type);

    MediaPacket(int64_t epoch = -1, PacketType type = PacketType::Empty, fh::PacketPtr&& packet = nullptr);
    MediaPacket(const MediaPacket&) = delete;
    MediaPacket& operator=(const MediaPacket&) = delete;
    MediaPacket(MediaPacket&&) = default;
    MediaPacket& operator=(MediaPacket&&) = default;
    ~MediaPacket() = default;

    PacketType type() const { return m_type; }
    int64_t epoch() const { return m_epoch; }
    
    fh::PacketPtr releasePacket();

private:
    PacketType m_type{PacketType::Empty};
    int64_t m_epoch{0};
    fh::PacketPtr m_packet;

};

} // namespace media