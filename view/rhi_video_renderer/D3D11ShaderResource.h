#pragma once
#include "ShaderResource.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace view {
class D3D11ShaderResource : public ShaderResource {
public:
    D3D11ShaderResource(const Options& opts);

    QRhiShaderResourceBindings* shaderResourceBinding() const override { return m_srb.get(); }
    // 默认实现将操作排队进 rub;派生类允许立即提交，但必须满足 beginPass 前调用的时间契约
    void updateTexture(QRhiResourceUpdateBatch* rub, const Frame& frame) override;
protected:
    bool initializeResources() override;

protected:
    std::unique_ptr<QRhiTexture> m_yTexture{nullptr};
    std::unique_ptr<QRhiTexture> m_uvTexture{nullptr};

    ID3D11Device* m_device{nullptr};
    ID3D11DeviceContext* m_context{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nativeTexture{nullptr};
    DXGI_FORMAT m_textureFormat{ DXGI_FORMAT_UNKNOWN };

    // 解码设备的纹理通过共享句柄在本(渲染)设备打开后跨设备拷贝；
    // 解码帧池是一张纹理数组，句柄全池唯一，句柄变化(新解码池)时重开
    HANDLE m_sharedHandle{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sharedSource{nullptr};

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
};
} // namespace view