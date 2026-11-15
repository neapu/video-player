#pragma once
#include "Decoder.h"
#include <d3d11.h>
#include <cstdint>

namespace media {
class D3d11vaDecoder : public Decoder {
public:
    static std::unique_ptr<D3d11vaDecoder> create(const Options& options);

protected:
    D3d11vaDecoder(const Options& options);
    virtual bool createHardwareContext(const AVStream* stream);
    // 自建硬件帧池：池内纹理带共享标志，渲染设备可跨设备打开拷贝。
    // 在 get_format 选中硬件格式后调用（此时 sw_pix_fmt 与 coded 尺寸已就绪）
    bool createFramesContext(AVCodecContext* ctx);

protected:
    int m_hwPixelFormat{AV_PIX_FMT_NONE};
    int m_adapterIndex{-1};
};

} // namespace media
