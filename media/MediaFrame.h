#pragma once
#include <optional>
#include "media/IMediaFrame.h"
#include "ffmpeg_helper/Frame.h"

namespace media {
class MediaFrame : public IMediaFrame {
public:
    static std::optional<MediaFrame> createFrame(FrameType type);
    static AVPixelFormat toAvPixelFormat(PixelFormat format);
    static AVSampleFormat toAvSampleFormat(SampleFormat format);

    MediaFrame(FrameType type, fh::FramePtr&& frame);
    MediaFrame(const MediaFrame&) = delete;
    MediaFrame& operator=(const MediaFrame&) = delete;
    MediaFrame(MediaFrame&&) = default;
    MediaFrame& operator=(MediaFrame&&) = default;
    ~MediaFrame() override = default;

    FrameType type() const override { return m_type; }
    FrameFormat format() const override;
    uint8_t* data(int plane) override;
    const uint8_t* data(int plane) const override;
    int lineSize(int plane) const override;
    void* rawFrame() override { return m_frame.get(); }
    const void* rawFrame() const override { return m_frame.get(); }
    void* release() override;
    int64_t pts() const override;

    AVFrame* avFrame() { return m_frame.get(); }
    const AVFrame* avFrame() const { return m_frame.get(); }

protected:
    FrameType m_type{FrameType::Empty};
    fh::FramePtr m_frame;
};

} // namespace media