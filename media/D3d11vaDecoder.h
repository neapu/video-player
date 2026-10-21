#pragma once
#include "Decoder.h"
#include <d3d11.h>

namespace media {
class D3d11vaDecoder : public Decoder {
public:
    static std::unique_ptr<D3d11vaDecoder> create(const Options& options);
protected:
    D3d11vaDecoder(const Options& options);
    virtual bool createHardwareContext(const AVStream* stream);

protected:
    int m_hwPixelFormat{AV_PIX_FMT_NONE};
    ID3D11Device* m_d3d11Device{nullptr};
};

} // namespace media