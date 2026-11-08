#include "YuvShaderResource.h"
#include <QDebug>

namespace view {
YuvShaderResource::YuvShaderResource(const Options& opts)
    : ShaderResource(opts)
{
}

void YuvShaderResource::updateTexture(QRhiResourceUpdateBatch* rub, const Frame& frame)
{
    auto format = frame->format();
    if (format.memoryType != media::MemoryType::Cpu) {
        qCritical() << "Unsupported memory type";
        return;
    }
    if (format.pixelFormat != m_format) {
        qCritical() << "Unsupported pixel format";
        return;
    }

    auto uploadTextureData = [&](QRhiTexture* texture, const QByteArray& data, int width, int height, int lineSize) {
        QRhiTextureSubresourceUploadDescription sub(data);
        sub.setSourceSize(QSize(width, height));
        sub.setDataStride(lineSize);
        QRhiTextureUploadEntry entry{0, 0, sub};
        QRhiTextureUploadDescription desc{{entry}};
        rub->uploadTexture(texture, desc);
    };

    if (m_format == PixelFormat::Yuv420p || m_format == PixelFormat::Yuv420p10le) {
        QByteArray yData(reinterpret_cast<const char*>(frame->y()), frame->yStride() * format.height);
        uploadTextureData(m_yTexture.get(), yData, format.width, format.height, frame->yStride());
        QByteArray uData(reinterpret_cast<const char*>(frame->u()), frame->uStride() * (format.height / 2));
        uploadTextureData(m_uTexture.get(), uData, (format.width + 1) / 2, (format.height + 1) / 2, frame->uStride());
        QByteArray vData(reinterpret_cast<const char*>(frame->v()), frame->vStride() * (format.height / 2));
        uploadTextureData(m_vTexture.get(), vData, (format.width + 1) / 2, (format.height + 1) / 2, frame->vStride());
    } else if (m_format == PixelFormat::Nv12 || m_format == PixelFormat::P010le) {
        QByteArray yData(reinterpret_cast<const char*>(frame->y()), frame->yStride() * format.height);
        uploadTextureData(m_yTexture.get(), yData, format.width, format.height, frame->yStride());
        QByteArray uvData(reinterpret_cast<const char*>(frame->uv()), frame->uvStride() * (format.height / 2));
        uploadTextureData(m_uvTexture.get(), uvData, (format.width + 1) / 2, (format.height + 1) / 2, frame->uvStride());
    } else {
        qCritical() << "Unsupported pixel format";
    }
}

bool YuvShaderResource::initializeResources()
{
    if (m_type != Type::Yuv) {
        qCritical() << "called with incorrect type";
        return false;
    }

    // m_rhi / m_size 已由基类 ShaderResource::initialize() 校验并赋值，
    // 矩阵 uniform buffer 也已由基类创建

    m_sampler.reset(m_rhi->newSampler(
        QRhiSampler::Linear,        // 放大过滤，指像素放大时如何插值
        QRhiSampler::Linear,        // 缩小过滤，指像素缩小时如何插值
        QRhiSampler::None,          // mipmap过滤，禁用了mipmap所以使用None
        QRhiSampler::ClampToEdge,   // U方向(水平方向)的纹理坐标超出[0,1]范围时的处理方式
        QRhiSampler::ClampToEdge    // V方向(垂直方向)的纹理坐标超出[0,1]范围时的处理方式
    ));
    if (!m_sampler->create()) {
        qCritical() << "Failed to create sampler";
        return false;
    }

    

    // 初始化纹理
    QSize uvSize((m_size.width() + 1) / 2, (m_size.height() + 1) / 2);

    if (m_format == PixelFormat::Yuv420p || m_format == PixelFormat::Yuv420p10le) {
        QRhiTexture::Format format = (m_format == PixelFormat::Yuv420p) ? QRhiTexture::R8 : QRhiTexture::R16;
        m_yTexture.reset(m_rhi->newTexture(format, m_size, 1, QRhiTexture::Flags{}));
        m_uTexture.reset(m_rhi->newTexture(format, uvSize, 1, QRhiTexture::Flags{}));
        m_vTexture.reset(m_rhi->newTexture(format, uvSize, 1, QRhiTexture::Flags{}));
        if (!m_yTexture->create() || !m_uTexture->create() || !m_vTexture->create()) {
            qCritical() << "failed to create YUV textures";
            return false;
        }

        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_vTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())
        });
    } else if (m_format == PixelFormat::Nv12 || m_format == PixelFormat::P010le) {
        QRhiTexture::Format yFormat = (m_format == PixelFormat::Nv12) ? QRhiTexture::R8 : QRhiTexture::R16;
        QRhiTexture::Format uvFormat = (m_format == PixelFormat::Nv12) ? QRhiTexture::RG8 : QRhiTexture::RG16;
        m_yTexture.reset(m_rhi->newTexture(yFormat, m_size, 1, QRhiTexture::Flags{}));
        m_uvTexture.reset(m_rhi->newTexture(uvFormat, uvSize, 1, QRhiTexture::Flags{}));
        if (!m_yTexture->create() || !m_uvTexture->create()) {
            qCritical() << "failed to create NV12/P010LE textures";
            return false;
        }

        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, m_yTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_uvTexture.get(), m_sampler.get()),
            QRhiShaderResourceBinding::uniformBuffer(3, QRhiShaderResourceBinding::VertexStage, m_vsVertexTransformMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage, m_fsColorRangeConversionMatrix.get()),
            QRhiShaderResourceBinding::uniformBuffer(5, QRhiShaderResourceBinding::FragmentStage, m_fsYUVtoRGBMatrix.get())
        });
    } else {
        qCritical() << "Unsupported pixel format";
        return false;
    }

    if (!m_srb->create()) {
        qCritical() << "Failed to create shader resource bindings";
        return false;
    }

    return true;
}

} // namespace view