#include "D3d11vaDecoder.h"
#include "ffmpeg_helper/FFmpegError.h"
// 这个头文件引用了 d3d11.h ，不能加 extern "C"
#include <libavutil/hwcontext_d3d11va.h>
extern "C" {
#include <libavutil/pixdesc.h>
}
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>
#include <string>

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
    : m_adapterIndex(options.adapterIndex)
    , Decoder(options)
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

    // 解码与渲染设备解耦：这里按适配器序号自建 D3D11 设备，不再借用渲染器设备。
    // 枚举非软件适配器，序号空间约定与 view::enumerateGpus() 一致
    // （同为"跳过 DXGI_ADAPTER_FLAG_SOFTWARE 后按 EnumAdapters1 顺序连续编号"），两处需保持同步修改
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR(m_logger, "Failed to create DXGI factory");
        return false;
    }
    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;
    for (UINT i = 0; ; i++) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        adapters.push_back(std::move(adapter));
    }
    int adapterIndex = m_adapterIndex >= 0 ? m_adapterIndex : 0;
    if (adapterIndex < 0 || adapterIndex >= static_cast<int>(adapters.size())) {
        LOG_ERROR(m_logger, "Invalid D3D11 adapter index: {} ({} adapters available)", adapterIndex, adapters.size());
        return false;
    }
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapters[adapterIndex]->GetDesc1(&adapterDesc);
    // Description 是宽字符，窄化打日志
    std::string adapterName;
    for (const auto* c = adapterDesc.Description; *c != L'\0'; ++c) {
        adapterName.push_back(static_cast<char>(*c));
    }
    LOG_INFO(m_logger, "Creating D3D11 device on adapter {}: {}", adapterIndex, adapterName);

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    hr = D3D11CreateDevice(adapters[adapterIndex].Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        LOG_ERROR(m_logger, "Failed to create D3D11 device");
        return false;
    }

    AVBufferRef* hwDeviceCtx{nullptr};
    hwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!hwDeviceCtx) {
        LOG_ERROR(m_logger, "Failed to allocate D3D11 device context");
        return false;
    }
    auto* hwDeviceCtxInternal = reinterpret_cast<AVHWDeviceContext*>(hwDeviceCtx->data);
    auto* d3d11DeviceCtx = reinterpret_cast<AVD3D11VADeviceContext*>(hwDeviceCtxInternal->hwctx);
    // 引用移交 FFmpeg（Detach 不减引用，uninit 时 Release）
    d3d11DeviceCtx->device = device.Detach();
    // 不填 device_context：av_hwdevice_ctx_init 内部补 immediate context 并开启多线程保护
    int ret = av_hwdevice_ctx_init(hwDeviceCtx);
    if (ret < 0) {
        av_buffer_unref(&hwDeviceCtx);
        LOG_ERROR(m_logger, "Failed to initialize D3D11 device context: {}", fh::err2str(ret));
        return false;
    }

    m_codecContext->hw_device_ctx = hwDeviceCtx;
    m_codecContext->opaque = this;
    m_codecContext->get_format = [](AVCodecContext* ctx, const AVPixelFormat* pix_fmts) -> AVPixelFormat {
        auto* decoder = reinterpret_cast<D3d11vaDecoder*>(ctx->opaque);
        for (const auto* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == decoder->m_hwPixelFormat) {
                // 选中硬件格式后自建共享帧池，解码器不再自动建池
                if (!decoder->createFramesContext(ctx)) {
                    return AV_PIX_FMT_NONE;
                }
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    };

    return true;
}

bool D3d11vaDecoder::createFramesContext(AVCodecContext* ctx)
{
    if (ctx->hw_frames_ctx) {
        return true;
    }
    AVBufferRef* framesRef = av_hwframe_ctx_alloc(ctx->hw_device_ctx);
    if (!framesRef) {
        LOG_ERROR(m_logger, "Failed to allocate hardware frames context");
        return false;
    }
    // get_format 时机 sw_pix_fmt 是软解像素格式（yuv420p/yuv420p10le），
    // D3D11VA 解码输出必须是 DXGI 支持的对应格式，直接透传会被 FFmpeg 映射为
    // 420_OPAQUE（DXVA2 legacy），视频解码器无法在其上创建输出视图
    AVPixelFormat swFormat = AV_PIX_FMT_NONE;
    switch (ctx->sw_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        swFormat = AV_PIX_FMT_NV12;
        break;
    case AV_PIX_FMT_YUV420P10LE:
        swFormat = AV_PIX_FMT_P010LE;
        break;
    default:
        LOG_ERROR(m_logger, "Unsupported software pixel format for D3D11VA decoding: {}",
            av_get_pix_fmt_name(ctx->sw_pix_fmt));
        av_buffer_unref(&framesRef);
        return false;
    }

    auto* frames = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
    frames->format = AV_PIX_FMT_D3D11;
    frames->sw_format = swFormat;
    frames->width = ctx->coded_width;
    frames->height = ctx->coded_height;
    // 池大小 = 最大 DPB（H.264/HEVC 参考帧可达 16）+ 多线程解码的并发持有 + 输出缓冲，
    // 避免启动时帧线程并发 get_buffer 把池打满（实测 20 在 16 线程下会瞬时耗尽丢帧）
    const int threadCount = ctx->thread_count > 0 ? ctx->thread_count : 16;
    frames->initial_pool_size = 20 + threadCount;
    auto* d3dFrames = reinterpret_cast<AVD3D11VAFramesContext*>(frames->hwctx);
    d3dFrames->BindFlags = D3D11_BIND_DECODER;
    // 共享标志：池是一张纹理数组，渲染设备通过共享句柄打开后跨设备拷贝
    d3dFrames->MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    int ret = av_hwframe_ctx_init(framesRef);
    if (ret < 0) {
        av_buffer_unref(&framesRef);
        LOG_ERROR(m_logger, "Failed to initialize hardware frames context: {}", fh::err2str(ret));
        return false;
    }
    // 所有权移交给解码器上下文（close 时释放）
    ctx->hw_frames_ctx = framesRef;
    return true;
}

} // namespace media
