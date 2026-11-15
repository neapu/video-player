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
    virtual ID3D11Device* device() { return nullptr; };
    virtual ID3D11DeviceContext* context() { return nullptr; }
#endif
};
} // namespace view