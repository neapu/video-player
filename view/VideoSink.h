#pragma once
#include <QByteArray>
#include <functional>
#include <variant>
#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif
#include "media/IMediaFrame.h"

namespace view {
using PixelFormat = media::PixelFormat;
using Frame = media::MediaFramePtr;
using FrameFormat = media::FrameFormat;
using ColorRange = media::ColorRange;
using ColorSpace = media::ColorSpace;

class VideoSink {
public:
    struct Options {
        PixelFormat format{PixelFormat::Yuv420p};
        int width{0};
        int height{0};
        bool hwaccel{false};
        std::function<Frame()> frameSource{};
    };
    virtual ~VideoSink() = default;
    virtual void start(const Options& opts) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void clear() {}
    // 暂停状态下请求渲染一帧（如暂停中seek后刷新画面），渲染完或超时后保持暂停
    virtual void refresh() {}

#ifdef _WIN32
    // 渲染设备所在 GPU 的序号（view::enumerateGpus() 的序号空间，与 media 解码适配器序号一致）；
    // 渲染器无 D3D11 设备或未知时返回 -1。供 Controller 做解码 GPU 自动跟随与跨 GPU 降级决策
    virtual int renderGpuIndex() const { return -1; }
#endif
};
} // namespace view