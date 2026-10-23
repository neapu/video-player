#include "Frame.h"

namespace fh {

void FrameDeleter::operator()(AVFrame* frame) const
{
    if (frame) {
        av_frame_free(&frame);
    }
}

FramePtr makeFrame()
{
    AVFrame* frame = av_frame_alloc();
    return FramePtr(frame);
}

FramePtr copyFrame(const FramePtr& frame)
{
    AVFrame* f = av_frame_clone(frame.get());
    return FramePtr(f);
}

} // namespace fh