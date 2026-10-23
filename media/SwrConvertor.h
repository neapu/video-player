#pragma once
#include "FrameProcessor.h"
#include "Logger.h"
#include "ffmpeg_helper/SwrContext.h"

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
    ~SwrConvertor() override;

    fh::FramePtr process(const fh::FramePtr& frame) override;

private:
    bool configure(const fh::FramePtr& frame, const AVChannelLayout& inputLayout);

private:
    AVSampleFormat m_targetFormat{AV_SAMPLE_FMT_NONE};
    int m_targetSampleRate{0};
    AVChannelLayout m_targetLayout{};
    bool m_targetLayoutSpecified{false};
    AVSampleFormat m_inputFormat{AV_SAMPLE_FMT_NONE};
    int m_inputSampleRate{0};
    AVChannelLayout m_inputLayout{};
    AVSampleFormat m_outputFormat{AV_SAMPLE_FMT_NONE};
    int m_outputSampleRate{0};
    fh::SwrContextPtr m_swrContext;
    LoggerPtr m_logger{nullptr};
};
}