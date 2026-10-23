#include "D3d11vaDecoder.h"
#include "ffmpeg_helper/FFmpegError.h"
// 这个头文件引用了 d3d11.h ，不能加 extern "C"
#include <libavutil/hwcontext_d3d11va.h>

namespace media {
std::unique_ptr<D3d11vaDecoder> D3d11vaDecoder::create(const Options& options)
{
    std::unique_ptr<D3d11vaDecoder> decoder(new D3d11vaDecoder(options));
    if (!decoder->initialize(options.stream)) {
        return nullptr;
    }
    return decoder;
}

D3d11vaDecoder::D3d11vaDecoder(const Options& options)
    : m_d3d11Device(static_cast<ID3D11Device*>(options.hardwareContext)), Decoder(options)
{
}

bool D3d11vaDecoder::createHardwareContext(const AVStream* stream)
{
    FUNC_TRACE(m_logger, LogLevel::info);
    for (int i = 0; ; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_codecContext->codec, i);
        if (!config) {
            LOG_INFO(m_logger, "No more hardware configurations available. Tried {} configurations.", i);
            break; // No more hardware configurations available
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            && config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
            m_hwPixelFormat = config->pix_fmt;
            break;
        }
    }
    if (m_hwPixelFormat == AV_PIX_FMT_NONE) {
        LOG_ERROR(m_logger, "Failed to find suitable D3D11VA hardware configuration");
        return false;
    }

    AVBufferRef* hwDeviceCtx{nullptr};
    // 两条路径，如果传入了d3d11设备上下文，就用用户传入的设备，否则用 ffmpeg 内部创建的设备
    if (m_d3d11Device) {
        LOG_INFO(m_logger, "Using user-provided D3D11 device context");
        hwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!hwDeviceCtx) {
            LOG_ERROR(m_logger, "Failed to allocate D3D11 device context");
            return false;
        }
        auto* hwDeviceCtxInternal = reinterpret_cast<AVHWDeviceContext*>(hwDeviceCtx->data);
        auto* d3d11DeviceCtx = reinterpret_cast<AVD3D11VADeviceContext*>(hwDeviceCtxInternal->hwctx);
        d3d11DeviceCtx->device = m_d3d11Device;
        d3d11DeviceCtx->device->AddRef();
        int ret = av_hwdevice_ctx_init(hwDeviceCtx);
        if (ret < 0) {
            av_buffer_unref(&hwDeviceCtx);
            hwDeviceCtx = nullptr;
            LOG_ERROR(m_logger, "Failed to initialize D3D11 device context", fh::err2str(ret));
            return false;
        }
    } else {
        LOG_INFO(m_logger, "Creating D3D11 device context internally");
        int ret = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to create D3D11 device context internally", fh::err2str(ret));
            return false;
        }
    }

    m_codecContext->hw_device_ctx = hwDeviceCtx;
    m_codecContext->opaque = this;
    m_codecContext->get_format = [](AVCodecContext* ctx, const AVPixelFormat* pix_fmts) -> AVPixelFormat {
        auto* decoder = reinterpret_cast<D3d11vaDecoder*>(ctx->opaque);
        for (const auto* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == decoder->m_hwPixelFormat) {
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    };

    return true;
}

} // namespace media