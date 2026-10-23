#pragma once
#include <atomic>
#include <expected>
#include <optional>
#include "ffmpeg_helper/InputContext.h"
#include "Logger.h"
#include "ffmpeg_helper/Packet.h"

namespace media {
class Demuxer {
public:
    struct Options {
        std::string url;
        LoggerPtr logger;
    };
    static std::unique_ptr<Demuxer> create(const Options& options);

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;
    Demuxer(Demuxer&&) = default;
    Demuxer& operator=(Demuxer&&) = default;

    std::expected<fh::PacketPtr, int> readPacket();
    int seek(int64_t timestamp);
    void interrupt();
    void resetInterrupt();
    void cancel();
    void resume();

    int streamCount() const;
    const AVStream* stream(int index) const;
    AVStream* stream(int index);

    int64_t duration() const;

private:
    Demuxer(const Options& options);
    bool initialize();
    static int interruptCallback(void* opaque);

private:
    std::string m_url;
    LoggerPtr m_logger;

    fh::InputContextPtr m_inputContext;
    std::atomic_bool m_interrupted{false};
    std::atomic_bool m_cancelled{false};
};

} // namespace media