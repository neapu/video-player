#pragma once
#include "FrameProcessor.h"
#include "Logger.h"

namespace media {
// 硬解软渲染：把硬件帧（任何带 hw_frames_ctx 的格式，如 D3D11/VAAPI/VideoToolbox/CUDA）
// 下载到 CPU 内存，输出硬件帧池 sw_format 的 CPU 帧。
// 基于 av_hwframe_transfer_data（FFmpeg hwcontext 的统一下载接口），与平台无关
class HwFrameDownloader : public FrameProcessor {
public:
    struct Options {
        LoggerPtr logger{nullptr};
    };
    explicit HwFrameDownloader(const Options& opts);

    fh::FramePtr process(const fh::FramePtr& frame) override;

private:
    LoggerPtr m_logger{nullptr};
};
} // namespace media
