#pragma once
#ifdef _WIN32
#include "FrameProcessor.h"
#include "Logger.h"

namespace media {
// 硬解软渲染：把 D3D11VA 解码出的 GPU 纹理下载到 CPU 内存，
// 输出硬件帧池 sw_format（NV12/P010）的 CPU 帧
class D3D11FrameDownloader : public FrameProcessor {
public:
    struct Options {
        LoggerPtr logger{nullptr};
    };
    explicit D3D11FrameDownloader(const Options& opts);

    fh::FramePtr process(const fh::FramePtr& frame) override;

private:
    LoggerPtr m_logger{nullptr};
};
} // namespace media
#endif
