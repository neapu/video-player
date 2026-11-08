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
    static PixelFormat toPixelFormat(AVPixelFormat format);
    static SampleFormat toSampleFormat(AVSampleFormat format);

    explicit MediaFrame(FrameType type, fh::FramePtr&& frame = nullptr);
    MediaFrame(const MediaFrame&) = delete;
    MediaFrame& operator=(const MediaFrame&) = delete;
    MediaFrame(MediaFrame&&) = default;
    MediaFrame& operator=(MediaFrame&&) = default;
    ~MediaFrame() override = default;

    FrameType type() const override { return m_type; }
    FrameFormat format() const override;
    uint8_t* y() const override;
    int yStride() const override;
    uint8_t* u() const override;
    int uStride() const override;
    uint8_t* v() const override;
    int vStride() const override;
    uint8_t* uv() const override;
    int uvStride() const override;
    uint8_t* audioData(int plane) const override;
    int audioDataSize(int plane) const override;
    int64_t duration() const override;
#ifdef _WIN32
    ID3D11Texture2D* texture() const override;
    int subresource() const override;
#endif
    void* rawFrame() override { return m_frame.get(); }
    const void* rawFrame() const override { return m_frame.get(); }
    void* release() override;
    int64_t pts() const override;
    void setPts(int64_t pts) override;

    fh::FramePtr releaseFrame();
    const fh::FramePtr& frame() const { return m_frame; }

protected:
    FrameType m_type{FrameType::Empty};
    fh::FramePtr m_frame;
};

} // namespace media