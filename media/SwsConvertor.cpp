#include "SwsConvertor.h"
namespace media {
SwsConvertor::SwsConvertor(const Options& opts) {}

fh::FramePtr SwsConvertor::process(const fh::FramePtr& frame)
{
    return fh::copyFrame(frame);
}
}