#include "media/IMediaSource.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    ~MediaSource() override;
    static std::expected<std::unique_ptr<IMediaSource>, Error> create(const Options& options);

    std::vector<StreamInfo> getStreamInfo() const override;
    PixelFormat enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats) override;
    SampleFormat enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats) override;
    MediaFramePtr nextFrame(int streamIndex) override;
    int64_t duration() const override;
    void seek(int64_t timestamp) override;
    
private:
    MediaSource(const Options& options);
    Error initialize();

private:
    std::string m_source;
    std::string m_logDir;
    std::string m_instanceName;
    std::function<void()> m_onSeekSuccess;
    std::function<void(Error)> m_onError;
};
} // namespace media