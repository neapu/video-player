#include "MediaFrame.h"
#ifdef _WIN32
#include <d3d11.h>
#endif
extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext.h>
}

namespace {
media::PixelFormat toPixelFormat(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
        return media::PixelFormat::Yuv420p;
    case AV_PIX_FMT_NV12:
        return media::PixelFormat::Nv12;
    case AV_PIX_FMT_YUV420P10LE:
        return media::PixelFormat::Yuv420p10le;
    case AV_PIX_FMT_P010LE:
        return media::PixelFormat::P010le;
    default:
        return media::PixelFormat::None;
    }
}
AVPixelFormat toAvPixelFormat(media::PixelFormat format)
{
    switch (format) {
    case media::PixelFormat::Yuv420p:
        return AV_PIX_FMT_YUV420P;
    case media::PixelFormat::Nv12:
        return AV_PIX_FMT_NV12;
    case media::PixelFormat::Yuv420p10le:
        return AV_PIX_FMT_YUV420P10LE;
    case media::PixelFormat::P010le:
        return AV_PIX_FMT_P010LE;
    default:
        return AV_PIX_FMT_NONE;
    }
}
media::SampleFormat toSampleFormat(int format)
{
    switch (format) {
    case AV_SAMPLE_FMT_S16:
        return media::SampleFormat::S16le;
    case AV_SAMPLE_FMT_S16P:
        return media::SampleFormat::S16lePlanar;
    case AV_SAMPLE_FMT_S32:
        return media::SampleFormat::S32le;
    case AV_SAMPLE_FMT_S32P:
        return media::SampleFormat::S32lePlanar;
    case AV_SAMPLE_FMT_FLT:
        return media::SampleFormat::Flt;
    case AV_SAMPLE_FMT_FLTP:
        return media::SampleFormat::FltPlanar;
    default:
        return media::SampleFormat::None;
    }
}
AVSampleFormat toAvSampleFormat(media::SampleFormat format)
{
    switch (format) {
    case media::SampleFormat::S16le:
        return AV_SAMPLE_FMT_S16;
    case media::SampleFormat::S16lePlanar:
        return AV_SAMPLE_FMT_S16P;
    case media::SampleFormat::S32le:
        return AV_SAMPLE_FMT_S32;
    case media::SampleFormat::S32lePlanar:
        return AV_SAMPLE_FMT_S32P;
    case media::SampleFormat::Flt:
        return AV_SAMPLE_FMT_FLT;
    case media::SampleFormat::FltPlanar:
        return AV_SAMPLE_FMT_FLTP;
    default:
        return AV_SAMPLE_FMT_NONE;
    }
}
media::ColorSpace toColorSpace(int space)
{
    switch (space) {
    case AVCOL_SPC_BT709:
        return media::ColorSpace::Bt709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return media::ColorSpace::Bt2020;
    default:
        return media::ColorSpace::Bt601; // Default to Bt601 if unknown
    }
}
media::ColorRange toColorRange(int range)
{
    switch (range) {
    case AVCOL_RANGE_MPEG:
        return media::ColorRange::Limited;
    case AVCOL_RANGE_JPEG:
        return media::ColorRange::Full;
    default:
        return media::ColorRange::Limited; // Default to Limited if unknown
    }
}
media::ColorTransferCharacteristic toColorTrc(int trc)
{
    switch (trc) {
    case AVCOL_TRC_ARIB_STD_B67:
        return media::ColorTransferCharacteristic::Hlg;
    case AVCOL_TRC_SMPTE2084:
        return media::ColorTransferCharacteristic::Pq;
    default:
        return media::ColorTransferCharacteristic::None; // Default to None if unknown
    }
}

}

namespace media {
std::unique_ptr<IMediaFrame> IMediaFrame::makeEmptyFrame()
{
    return std::make_unique<MediaFrame>(FrameType::Empty, nullptr);
}

std::optional<MediaFrame> MediaFrame::createFrame(FrameType type)
{
    if (type != FrameType::Normal) {
        return MediaFrame(type, nullptr);
    }
    auto frame = fh::makeFrame();
    if (!frame) {
        return std::nullopt;
    }
    return MediaFrame(type, std::move(frame));
}

AVPixelFormat MediaFrame::toAvPixelFormat(PixelFormat format)
{
    return ::toAvPixelFormat(format);
}

AVSampleFormat MediaFrame::toAvSampleFormat(SampleFormat format)
{
    return ::toAvSampleFormat(format);
}

PixelFormat MediaFrame::toPixelFormat(AVPixelFormat format)
{
    return ::toPixelFormat(format);
}

SampleFormat MediaFrame::toSampleFormat(AVSampleFormat format)
{
    return ::toSampleFormat(format);
}

MediaFrame::MediaFrame(FrameType type, fh::FramePtr&& frame)
    : m_type(type)
    , m_frame(std::move(frame))
{
}

FrameFormat MediaFrame::format() const
{
    FrameFormat fmt{};
    if (!m_frame) {
        return fmt;
    }

    if (m_frame->hw_frames_ctx) {
        fmt.memoryType = MemoryType::Gpu;
        auto* hwFrameCtx = reinterpret_cast<AVHWFramesContext*>(m_frame->hw_frames_ctx->data);
        fmt.pixelFormat = ::toPixelFormat(hwFrameCtx->sw_format);
    } else {
        fmt.memoryType = MemoryType::Cpu;
        fmt.pixelFormat = ::toPixelFormat(m_frame->format);
    }

    fmt.width = m_frame->width;
    fmt.height = m_frame->height;
    fmt.colorSpace = ::toColorSpace(m_frame->colorspace);
    fmt.colorRange = ::toColorRange(m_frame->color_range);
    fmt.colorTrc = ::toColorTrc(m_frame->color_trc);
    
    fmt.sampleRate = m_frame->sample_rate;
    fmt.channels = m_frame->ch_layout.nb_channels;
    fmt.samples = m_frame->nb_samples;
    fmt.sampleFormat = ::toSampleFormat(m_frame->format);

    return fmt;
}

uint8_t* MediaFrame::y() const
{
    if (!m_frame) {
        return nullptr;
    }
    return m_frame->data[0];
}

int MediaFrame::yStride() const
{
    if (!m_frame) {
        return 0;
    }
    return m_frame->linesize[0];
}

uint8_t* MediaFrame::u() const
{
    if (!m_frame) {
        return nullptr;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Yuv420p && format != PixelFormat::Yuv420p10le) {
        return nullptr;
    }
    return m_frame->data[1];
}

int MediaFrame::uStride() const
{
    if (!m_frame) {
        return 0;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Yuv420p && format != PixelFormat::Yuv420p10le) {
        return 0;
    }
    return m_frame->linesize[1];
}

uint8_t* MediaFrame::v() const
{
    if (!m_frame) {
        return nullptr;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Yuv420p && format != PixelFormat::Yuv420p10le) {
        return nullptr;
    }
    return m_frame->data[2];
}

int MediaFrame::vStride() const
{
    if (!m_frame) {
        return 0;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Yuv420p && format != PixelFormat::Yuv420p10le) {
        return 0;
    }
    return m_frame->linesize[2];
}

uint8_t* MediaFrame::uv() const
{
    if (!m_frame) {
        return nullptr;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Nv12 && format != PixelFormat::P010le) {
        return nullptr;
    }
    return m_frame->data[1];
}

int MediaFrame::uvStride() const
{
    if (!m_frame) {
        return 0;
    }
    auto format = ::toPixelFormat(m_frame->format);
    if (format != PixelFormat::Nv12 && format != PixelFormat::P010le) {
        return 0;
    }
    return m_frame->linesize[1];
}

#ifdef _WIN32
ID3D11Texture2D* MediaFrame::texture() const
{
    if (!m_frame || !m_frame->hw_frames_ctx) {
        return nullptr;
    }
    // AV_PIX_FMT_D3D11 约定：data[0] 存放 ID3D11Texture2D*（纹理数组）
    return reinterpret_cast<ID3D11Texture2D*>(m_frame->data[0]);
}

int MediaFrame::subresource() const
{
    if (!m_frame || !m_frame->hw_frames_ctx) {
        return 0;
    }
    // AV_PIX_FMT_D3D11 约定：data[1] 以指针宽度整数存放纹理数组切片索引
    return static_cast<int>(reinterpret_cast<uintptr_t>(m_frame->data[1]));
}
#endif

void* MediaFrame::release()
{
    if (!m_frame) {
        return nullptr;
    }
    m_type = FrameType::Empty;
    return m_frame.release();
}

int64_t MediaFrame::pts() const
{
    if (!m_frame) {
        return AV_NOPTS_VALUE;
    }
    if (m_frame->time_base.num == 0 || m_frame->time_base.den == 0) {
        return AV_NOPTS_VALUE;
    }
    return av_rescale_q(m_frame->pts, m_frame->time_base, AVRational{1, 1000000}); // Convert to microseconds
}

fh::FramePtr MediaFrame::releaseFrame()
{
    if (!m_frame) {
        return nullptr;
    }
    m_type = FrameType::Empty;
    return std::move(m_frame);
}

} // namespace media