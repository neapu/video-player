#pragma once
#include "FrameProcessor.h"
#include "Logger.h"
#include "ffmpeg_helper/SwsContext.h"

namespace media {
class SwsConvertor : public FrameProcessor {
public:
    struct Options {
        // AV_PIX_FMT_NONE 表示保持源视频帧的格式
        AVPixelFormat targetFormat{AV_PIX_FMT_NONE};
        // 0 表示保持源视频帧的尺寸
        int targetWidth{0};
        int targetHeight{0};
        LoggerPtr logger{nullptr};
    };
    SwsConvertor(const Options& opts);

    fh::FramePtr process(const fh::FramePtr& frame) override;

private:
    AVPixelFormat m_targetFormat{AV_PIX_FMT_NONE};
    int m_targetWidth{0};
    int m_targetHeight{0};
    AVPixelFormat m_outputFormat{AV_PIX_FMT_NONE};
    fh::SwsContextPtr m_swsContext;
    LoggerPtr m_logger{nullptr};
};
} // namespace media