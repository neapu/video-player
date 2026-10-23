#pragma once
#include <memory>
#include "ffmpeg_helper/Frame.h"

namespace media {
class FrameProcessor {
public:
    virtual ~FrameProcessor() = default;

    virtual fh::FramePtr process(const fh::FramePtr& frame) = 0;
};
using FrameProcessorPtr = std::shared_ptr<FrameProcessor>;
} // namespace media
