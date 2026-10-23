#include "SwsConvertor.h"
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
SwsConvertor::SwsConvertor(const Options& opts)
    : m_targetFormat(opts.targetFormat)
    , m_targetWidth(opts.targetWidth)
    , m_targetHeight(opts.targetHeight)
    , m_swsContext(fh::allocateSwsContext())
    , m_logger(opts.logger)
{
    if (!m_swsContext) {
        LOG_ERROR(m_logger, "Failed to allocate video scaler context");
    }
}

fh::FramePtr SwsConvertor::process(const fh::FramePtr& frame)
{
    if (!frame) {
        LOG_ERROR(m_logger, "Cannot convert a null video frame");
        return nullptr;
    }
    if (!m_swsContext) {
        LOG_ERROR(m_logger, "Video scaler context is not initialized");
        return nullptr;
    }
    if (frame->width <= 0 || frame->height <= 0 || frame->format == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "Decoded video frame has an invalid pixel format or dimensions");
        return nullptr;
    }
    if (m_targetWidth < 0 || m_targetHeight < 0) {
        LOG_ERROR(m_logger, "Target video dimensions cannot be negative");
        return nullptr;
    }

    const auto outputFormat = m_targetFormat == AV_PIX_FMT_NONE
        ? static_cast<AVPixelFormat>(frame->format)
        : m_targetFormat;
    if (outputFormat == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "Target video pixel format is invalid");
        return nullptr;
    }

    auto output = fh::makeFrame();
    if (!output) {
        LOG_ERROR(m_logger, "Failed to allocate converted video frame");
        return nullptr;
    }

    int ret = av_frame_copy_props(output.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to copy video frame properties: {}", fh::err2str(ret));
        return nullptr;
    }
    output->format = outputFormat;
    output->width = m_targetWidth == 0 ? frame->width : m_targetWidth;
    output->height = m_targetHeight == 0 ? frame->height : m_targetHeight;

    ret = sws_scale_frame(m_swsContext.get(), output.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to convert video frame: {}", fh::err2str(ret));
        return nullptr;
    }

    m_outputFormat = outputFormat;
    return output;
}

AVPixelFormat SwsConvertor::targetFormat() const
{
    return m_outputFormat == AV_PIX_FMT_NONE ? m_targetFormat : m_outputFormat;
}
}