#include "SwrConvertor.h"
namespace media {
SwrConvertor::SwrConvertor(const Options& opts) {}

fh::FramePtr SwrConvertor::process(const fh::FramePtr& frame)
{
    return fh::copyFrame(frame);
}
}