#include "QRhiVideoRenderer.h"
#include <QDebug>
#include <QFile>

namespace {
QShader loadShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open shader file:" << path;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

QString formatToShaderName(view::PixelFormat fmt)
{
    switch (fmt) {
        case view::PixelFormat::Yuv420p:
        case view::PixelFormat::Yuv420p10le: 
            return ":/shaders/yuv420p.frag.qsb";
        case view::PixelFormat::Nv12:
        case view::PixelFormat::P010le:
            return ":/shaders/nv12.frag.qsb";
        default: return "";
    }
}

// 创建顶点变换矩阵，负责保持宽高比
QMatrix4x4 createVertexTransformMatrix(const QSize& frameSize, const QSize& viewportSize)
{
    QMatrix4x4 matrix;
    if (frameSize.width() <= 0 || frameSize.height() <= 0
        || viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return matrix;
    }

    const float videoAspect = static_cast<float>(frameSize.width()) / frameSize.height();
    const float viewportAspect = static_cast<float>(viewportSize.width()) / viewportSize.height();
    if (viewportAspect > videoAspect) {
        matrix.scale(videoAspect / viewportAspect, 1.0f);
    } else {
        matrix.scale(1.0f, viewportAspect / videoAspect);
    }
    return matrix;
}

// 色彩范围转换矩阵，用于将limited range的YUV转换为full range的RGB
QMatrix4x4 createColorRangeConversionMatrix(view::ColorRange colorRange)
{
    static const QMatrix4x4 limitedRange{
        255.0f / 219.0f, 0.0f,          0.0f,           -16.0f / 219.0f,
        0.0f,           255.0f / 224.0f, 0.0f,          -128.0f / 224.0f,
        0.0f,           0.0f,           255.0f / 224.0f, -128.0f / 224.0f,
        0.0f,           0.0f,           0.0f,            1.0f
    };
    static const QMatrix4x4 fullRange{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 1.0f, -128.0f / 255.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return colorRange == view::ColorRange::Full ? fullRange : limitedRange;
}

// YUV转RGB的转换矩阵
QMatrix4x4 createYUVtoRGBMatrix(view::ColorSpace colorSpace)
{
    static const QMatrix4x4 bt601{
        1.0f,  0.0f,       1.402f,    0.0f,
        1.0f, -0.344136f, -0.714136f, 0.0f,
        1.0f,  1.772f,     0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };
    static const QMatrix4x4 bt709{
        1.0f,  0.0f,       1.5748f,   0.0f,
        1.0f, -0.187324f, -0.468124f, 0.0f,
        1.0f,  1.8556f,    0.0f,      0.0f,
        0.0f,  0.0f,       0.0f,      1.0f
    };

    return colorSpace == view::ColorSpace::Bt601 ? bt601 : bt709;
}

} // namespace

namespace view {
QRhiVideoRenderer::QRhiVideoRenderer(QWidget* parent) : QRhiWidget(parent) {}

QRhiVideoRenderer::~QRhiVideoRenderer() {}

void QRhiVideoRenderer::start(const Options& opts)
{
    m_frameSource = opts.frameSource;
    m_running = true;
    update();
}
void QRhiVideoRenderer::stop()
{
    m_frameSource = nullptr;
    m_running = false;
}
void QRhiVideoRenderer::clear()
{
    m_clearFlag = true;
    update();
}

void QRhiVideoRenderer::initialize(QRhiCommandBuffer* cb)
{
    bool needReinitialize{false};
    if (m_rhi != rhi()) {
        m_rhi = rhi();
        needReinitialize = true;
    }
    if (!m_rhi) {
        qCritical() << "Failed to obtain QRhi instance.";
        emit errorOccured("Failed to obtain QRhi instance.");
        return;
    }
    if (!needReinitialize) {
        return;
    }

    qInfo() << "Initializing QRhiVideoRenderer with new QRhi instance.";
    qInfo() << "Using QRhi backend:" << m_rhi->backendName();

#ifdef _WIN32
    if (m_rhi->backend() == QRhi::D3D11) {
        auto* nativeHandle = reinterpret_cast<const QRhiD3D11NativeHandles*>(m_rhi->nativeHandles());
        if (!nativeHandle) {
            qCritical() << "Failed to obtain D3D11 native handles.";
            emit errorOccured("Failed to obtain D3D11 native handles.");
            return;
        }
        auto* device = static_cast<ID3D11Device*>(nativeHandle->dev);
        auto* context = static_cast<ID3D11DeviceContext*>(nativeHandle->context);

        // 启用多线程支持
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
            m_device = device;
            // m_d3d11DeviceContext = context;
        } else {
            // 回退到渲染软件帧模式，不赋值m_d3d11Device和m_d3d11DeviceContext
            qWarning()
                << "Failed to enable multithread protection for D3D11 device. Falling back to software rendering mode.";
        }
    }
#endif
    // 纹理坐标原点在左上角，而QRhi的默认坐标原点在左下角，所以需要翻转v坐标
    const float vertexData[] = {
        // x, y, u, v
        -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
        1.0f, -1.0f, 1.0f, 1.0f,  // bottom-right
        -1.0f, 1.0f, 0.0f, 0.0f,  // top-left
        1.0f, 1.0f, 1.0f, 0.0f    // top-right
    };
    m_vertexBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(vertexData)));
    if (!m_vertexBuffer->create()) {
        qWarning() << "Failed to create vertex buffer.";
        m_vertexBuffer.reset();
        m_rhi = nullptr;
        emit errorOccured("Failed to create vertex buffer.");
        return;
    }

    // 上传顶点数据到GPU
    auto* rub = m_rhi->nextResourceUpdateBatch();
    rub->uploadStaticBuffer(m_vertexBuffer.get(), vertexData);
    cb->resourceUpdate(rub);

    qInfo() << "QRhiVideoRenderer initialized.";
}
void QRhiVideoRenderer::render(QRhiCommandBuffer* cb)
{
    if (!m_vertexBuffer) {
        return;
    }

    if (!m_frameSource) {
        qCritical() << "No frame source.";
        emit errorOccured("No frame source.");
        m_running = false;
        return;
    }

    if (m_clearFlag) {
        m_clearFlag = false;
        auto* rub = m_rhi->nextResourceUpdateBatch();
        cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
        cb->endPass();
        if (m_running) update();
        return;
    }

    auto frame = m_frameSource();
    if (!frame) {
        if (m_running) update();
        return;
    }

    renderFrame(cb, std::move(frame));

    if (m_running) update();
}

void QRhiVideoRenderer::releaseResources() {}

bool QRhiVideoRenderer::createPipeline(ShaderResource::Type type, int width, int height, PixelFormat fmt)
{
    QSize frameSize(width, height);
    if (m_pipeline && m_shaderResource && m_shaderResource->size() == frameSize && m_shaderResource->type() == type
        && m_shaderResource->format() == fmt
        && m_pipeline->renderPassDescriptor() == renderTarget()->renderPassDescriptor()) {
        return true;
    }

    qInfo() << "Creating pipeline.";

#ifdef _WIN32
    if (type == ShaderResource::Type::D3D11 && (!m_device)) {
        qWarning() << "D3D11 device not available.";
        return false;
    }
#endif
    m_pipeline.reset();

    ShaderResource::Options opts{};
    opts.type = type;
    opts.rhi = m_rhi;
    opts.size = frameSize;
    opts.format = fmt;
#ifdef _WIN32
    opts.device = m_device;
    opts.context = m_context;
#endif
    m_shaderResource = ShaderResource::create(opts);
    if (!m_shaderResource) {
        qCritical() << "Failed to create shader resource";
        return false;
    }

    auto vertexShader = loadShader(":/shaders/video.vert.qsb");
    auto fragmentShader = loadShader(formatToShaderName(fmt));
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        qWarning() << "Failed to load shaders.";
        return false;
    }

    // 顶点布局(类似OpenGL中的VAO)
    QRhiVertexInputLayout inputLayout{};
    inputLayout.setBindings({
        { sizeof(float) * 4 }   // 本项目只用了一个顶点缓冲区，同时描述顶点位置和纹理坐标，每个顶点共4个float
    });
    inputLayout.setAttributes({
        // 第一个参数表示绑定的顶点缓冲区的索引，因为只有一个顶点缓冲区，这里都设置为0
        // 第二个参数表示顶点着色器中对应的输入变量的位置(location)，这里假设顶点着色器中有两个输入变量，位置和纹理坐标，分别对应location 0和1
        // 第三个参数表示顶点属性的格式，这里使用Float2表示两个float组成的向量
        // 第四个参数表示顶点属性在顶点缓冲区中的偏移量，这里顶点位置在缓冲区的起始位置，所以偏移为0，纹理坐标在顶点位置之后，所以偏移为sizeof(float) * 2
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 }, // 顶点位置
        { 0, 1, QRhiVertexInputAttribute::Float2, sizeof(float) * 2 } // 纹理坐标
    });

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setVertexInputLayout(inputLayout);
    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertexShader },
        { QRhiShaderStage::Fragment, fragmentShader }
    });
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setShaderResourceBindings(m_shaderResource->shaderResourceBinding());
    // 绘制条带三角形，用四个顶点渲染两个三角形
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    if (!m_pipeline->create()) {
        qWarning() << "Failed to create graphics pipeline.";
        m_shaderResource.reset();
        m_pipeline.reset();
        return false;
    }

    return true;
}

void QRhiVideoRenderer::renderFrame(QRhiCommandBuffer* cb, Frame&& frame)
{
    ShaderResource::Type type{ShaderResource::Type::Yuv};
    auto format = frame->format();
#ifdef _WIN32
    if (format.memoryType == media::MemoryType::Gpu) {
        type = ShaderResource::Type::D3D11;
    }
#endif
    if (!createPipeline(type, format.width, format.height, format.pixelFormat)) return;

    QSize renderSize = renderTarget()->pixelSize();
    QSize frameSize{format.width, format.height};
    QMatrix4x4 vertexTransformMatrix = createVertexTransformMatrix(frameSize, renderSize);
    QMatrix4x4 colorRangeConversionMatrix = createColorRangeConversionMatrix(format.colorRange);
    QMatrix4x4 yuvToRGBMatrix = createYUVtoRGBMatrix(format.colorSpace);

    auto* rub = m_rhi->nextResourceUpdateBatch();

    // 1. 更新纹理数据
    m_shaderResource->updateTexture(rub, frame);

    // 2. 更新uniform缓冲区数据
    m_shaderResource->updateVertexTransformMatrix(rub, vertexTransformMatrix);
    m_shaderResource->updateColorRangeConversionMatrix(rub, colorRangeConversionMatrix);
    m_shaderResource->updateYUVtoRGBMatrix(rub, yuvToRGBMatrix);

    // 3. 开始渲染
    cb->beginPass(renderTarget(), QColor{0, 0, 0, 255}, QRhiDepthStencilClearValue{1.0f, 0}, rub);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setShaderResources(m_shaderResource->shaderResourceBinding());
    cb->setViewport(QRhiViewport(0.0f, 0.0f, static_cast<float>(renderSize.width()), static_cast<float>(renderSize.height())));

    const QRhiCommandBuffer::VertexInput vertexInput[] {
        { m_vertexBuffer.get(), 0 }
    };
    cb->setVertexInput(0, 1, vertexInput);
    cb->draw(4);
    cb->endPass();
}
} // namespace view
