#include "VideoRenderer.h"

namespace view {

VideoRenderer::VideoRenderer(QWidget* parent) : QRhiWidget(parent) {}

VideoRenderer::~VideoRenderer() {}

void VideoRenderer::start(std::function<media::MediaFramePtr()> frameSource)
{
    // TODO: Implement start logic
}

void VideoRenderer::stop()
{
    // TODO: Implement stop logic
}

void VideoRenderer::initialize(QRhiCommandBuffer *cb)
{
    // TODO: Implement initialize logic
}

void VideoRenderer::render(QRhiCommandBuffer *cb)
{
    // TODO: Implement render logic
}

void VideoRenderer::releaseResources()
{
    // TODO: Implement releaseResources logic
}

} // namespace view
