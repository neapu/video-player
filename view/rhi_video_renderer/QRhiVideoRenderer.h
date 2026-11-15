#pragma once
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <chrono>
#include "../VideoSink.h"
#include "ShaderResource.h"

namespace view {
class QRhiVideoRenderer : public QRhiWidget, public VideoSink {
    Q_OBJECT
public:
    QRhiVideoRenderer(QWidget *parent = nullptr);
    ~QRhiVideoRenderer() override;

    void start(const Options& opts) override;
    void stop() override;
    void pause() override;
    void resume() override;
    void clear() override;
    void refresh() override;

#ifdef _WIN32
    void* adapterLuid() override { return m_adapterLuidValid ? &m_adapterLuid : nullptr; }
#endif

signals:
    void initialized();
    void errorOccured(const QString& msg);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    bool createPipeline(ShaderResource::Type type, int width, int height, PixelFormat fmt);
    void renderFrame(QRhiCommandBuffer* cb, Frame&& frame);
private:
    std::function<Frame()> m_frameSource{};
    QRhi* m_rhi{nullptr};

    std::unique_ptr<QRhiBuffer> m_vertexBuffer{nullptr};
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline{nullptr};
    std::unique_ptr<ShaderResource> m_shaderResource{nullptr};

    bool m_running{false};
    // 暂停状态下的一次性刷新请求（seek后显示目标帧）：
    // 拉到帧渲染后或超时后撤销，期间持续轮询，不进入常驻渲染循环
    bool m_refreshPending{false};
    std::chrono::steady_clock::time_point m_refreshDeadline{};

#ifdef _WIN32
    ID3D11Device* m_device{nullptr};
    ID3D11DeviceContext* m_context{nullptr};
    // 渲染设备所在适配器 LUID（initialize 时从 IDXGIDevice 取得）
    LUID m_adapterLuid{};
    bool m_adapterLuidValid{false};
#endif
    bool m_clearFlag{false};
};

} // namespace view