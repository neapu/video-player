#include "HwFrameDownloader.h"
#include "ffmpeg_helper/FFmpegError.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace media {
HwFrameDownloader::HwFrameDownloader(const Options& opts)
    : m_logger(opts.logger)
{
}

fh::FramePtr HwFrameDownloader::process(const fh::FramePtr& frame)
{
    if (!frame) {
        LOG_ERROR(m_logger, "Cannot download a null video frame");
        return nullptr;
    }
    const auto* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    if (!desc || !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL) || !frame->hw_frames_ctx) {
        LOG_ERROR(m_logger, "Video frame is not a hardware frame");
        return nullptr;
    }

    auto output = fh::makeFrame();
    if (!output) {
        LOG_ERROR(m_logger, "Failed to allocate downloaded video frame");
        return nullptr;
    }

    // 内部走平台相应的回读实现（D3D11 为 staging 纹理），输出帧自动为硬件帧池 sw_format 的 CPU 帧；
    // 同步执行（阻塞等待 GPU 拷贝完成），运行于解码线程
    int ret = av_hwframe_transfer_data(output.get(), frame.get(), 0);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to download hardware frame: {}", fh::err2str(ret));
        return nullptr;
    }

    // 只拷贝 pts/色彩属性等元数据，不涉及 data/hw_frames_ctx，
    // 下载帧保持无 hw_frames_ctx，下游按 CPU 帧处理
    ret = av_frame_copy_props(output.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to copy video frame properties: {}", fh::err2str(ret));
        return nullptr;
    }

    return output;
}
} // namespace media
