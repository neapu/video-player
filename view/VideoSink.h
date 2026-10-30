#pragma once
#include <QByteArray>
#include <functional>
#include <variant>
#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace view {
enum class PixelFormat {
    Yuv420p
};
struct YuvFrame {
    int width{0};
    int height{0};
    QByteArray y;
    int yStride{0};
    QByteArray u;
    int uStride{0};
    QByteArray v;
    int vStride{0};
};
#ifdef _WIN32
struct D3DFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{nullptr};
};
using Frame = std::variant<YuvFrame, D3DFrame>;
#else
using Frame = YuvFrame;
#endif

class VideoSink {
public:
    struct Options {
        PixelFormat format{PixelFormat::Yuv420p};
        int width{0};
        int height{0};
        std::function<Frame()> frameSource{};
    };
    virtual ~VideoSink() = default;
    virtual void start(const Options& opts) = 0;
    virtual void stop() = 0;
};
} // namespace view