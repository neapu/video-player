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
        // 硬解码使用的适配器序号（MediaSource 枚举的非软件 DXGI 适配器，-1 = 0 号）
        int adapterIndex{-1};
    };

    static std::unique_ptr<Decoder> create(const Options& options);

    virtual ~Decoder() = default;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = default;
    Decoder& operator=(Decoder&&) = default;

    virtual Type type() const { return m_type; }

    // avcodec_open2 之后 thread_count 是 FFmpeg 解析出的实际线程数（0/自动时非 0）
    int threadCount() const;
    int hasBFrames() const;

    // 按借用语义送包：avcodec_send_packet 内部只对包缓冲区取引用（av_read_frame
    // 出来的包均为引用计数），调用后包仍归调用方所有，因此调用方可以把包同时
    // 保留在自己的缓存里。传 nullptr 可触发排空(drain)模式。
    virtual int sendPacket(const fh::PacketPtr& packet);
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