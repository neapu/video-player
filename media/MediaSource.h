#pragma once
#include <thread>
#include "media/IMediaSource.h"
#include "Demuxer.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    ~MediaSource() override;
    static std::expected<std::unique_ptr<IMediaSource>, Error> create(const Options& options);

    std::vector<StreamInfo> getStreamInfo() const override;
    std::expected<PixelFormat, Error> enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats, bool hardwareAccelerated = false) override;
    std::expected<SampleFormat, Error> enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats) override;
    MediaFramePtr nextFrame(int streamIndex) override;
    int64_t duration() const override;
    void seek(int64_t timestamp) override;
    void start() override;
    void stop() override;
    
private:
    MediaSource(const Options& options);
    Error initialize();

    void demuxWorkerFunc();
private:
    std::string m_source;
    std::string m_logDir;
    std::string m_instanceName;
    std::function<void()> m_onSeekSuccess;
    std::function<void(Error)> m_onError;

    LoggerPtr m_logger;
    std::unique_ptr<Demuxer> m_demuxer;
    
    std::atomic_bool m_demuxeRunning{false};
    std::thread m_demuxThread;
};
} // namespace media