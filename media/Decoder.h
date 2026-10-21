#pragma once
#include <optional>
#include <expected>
#include "ffmpeg_helper/CodecContext.h"
#include "ffmpeg_helper/Packet.h"
#include "ffmpeg_helper/Frame.h"
#include "Logger.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace media {
class Decoder {
public:
    enum class Type {
        Software,
        D3d11va,
    };

    struct Options {
        Type type{Type::Software};
        // 裸指针安全声明：只借用，需要确保在 create 函数调用期间该指针有效
        const AVStream* stream{nullptr};
        LoggerPtr logger{nullptr};
        // 裸指针安全声明：只借用，需要确保在 Decoder 对象存在期间该指针有效
        void* hardwareContext{nullptr};
    };

    static std::unique_ptr<Decoder> create(const Options& options);

    virtual ~Decoder() = default;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = default;
    Decoder& operator=(Decoder&&) = default;

    virtual Type type() const { return m_type; }

    virtual int sendPacket(fh::PacketPtr&& packet);
    virtual std::expected<fh::FramePtr, int> receiveFrame();

    virtual void flush();

protected:
    explicit Decoder(const Options& options);
    virtual bool initialize(const AVStream* stream);

    virtual bool createContext(const AVStream* stream);
    virtual bool openCodec();
    virtual bool createHardwareContext(const AVStream* stream) { return true; }

protected:
    Type m_type;
    LoggerPtr m_logger;
    fh::CodecContextPtr m_codecContext{nullptr};
};
} // namespace media