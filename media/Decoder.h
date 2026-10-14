#pragma once
#include <expected>
#include "ffmpeg_helper/CodecContext.h"
#include "Logger.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace media {
class Decoder {
public:
    enum class Type {
        Software,
    };

    struct Options {
        Type type{Type::Software};
        const AVStream* stream{nullptr};
        LoggerPtr logger{nullptr};
    };

    // 错误为 FFmpeg 错误码
    static std::expected<Decoder, int> create(const Options& options);

    virtual ~Decoder() = default;

protected:
    explicit Decoder(const Options& options);
    int initialize();

protected:
    Type m_type;
    LoggerPtr m_logger;
    fh::CodecContextPtr m_codecContext{nullptr};
};
} // namespace media