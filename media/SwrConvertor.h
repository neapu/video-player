#pragma once
#include "FrameProcessor.h"
#include "Logger.h"

namespace media {
class SwrConvertor : public FrameProcessor {
public:
    struct Options {
        // AV_SAMPLE_FMT_NONE 表示保持源音频格式
        AVSampleFormat targetFormat{AV_SAMPLE_FMT_NONE};
        // 0 表示保持源采样率
        int targetSampleRate{0};
        // 为空表示保持源布局
        AVChannelLayout targetLayout{};
        LoggerPtr logger{nullptr};
    };
    SwrConvertor(const Options& opts);

    fh::FramePtr process(const fh::FramePtr& frame) override;
};
}