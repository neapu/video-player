#pragma once
#include <QRhiWidget>
#include <rhi/qrhi.h>
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
    void clear() override;

#ifdef _WIN32
    ID3D11Device* device() override { return m_device; }
    ID3D11DeviceContext* context() override { return m_context; }
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

#ifdef _WIN32
    ID3D11Device* m_device{nullptr};
    ID3D11DeviceContext* m_context{nullptr};
#endif
    bool m_clearFlag{false};
};

} // namespace view