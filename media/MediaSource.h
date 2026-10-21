#pragma once
#include <thread>
#include "media/IMediaSource.h"
#include "Demuxer.h"
#include <unordered_map>
#include "DecodeWorker.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    ~MediaSource() override;
    static std::expected<std::unique_ptr<IMediaSource>, CreateError> create(const Options& options);

    std::vector<StreamInfo> getStreamInfo() const override;
    StreamError enableStream(
        int streamIndex, 
        bool hardwareAccelerated = false,
        void* hardwareContext = nullptr
    ) override;
    StreamError disableStream(int streamIndex) override;
    MediaFramePtr nextFrame(int streamIndex) override;
    int64_t duration() const override;
    void seek(int64_t timestamp) override;
    void start() override;
    void stop() override;
    bool running() const override 
    {
        return m_demuxRunning.load();
    }
    
private:
    MediaSource(const Options& options);
    CreateError initialize();

    void demuxWorkerFunc();
private:
    std::string m_source;
    std::string m_logDir;
    std::string m_instanceName;
    std::function<void(SeekError)> m_onSeekComplete;
    std::function<void(RunningError)> m_onRunningError;

    LoggerPtr m_logger;
    std::unique_ptr<Demuxer> m_demuxer;
    
    std::atomic_bool m_demuxRunning{false};
    std::thread m_demuxThread;

    std::unordered_map<int, DecodeWorkerPtr> m_decodeWorkers;
};
} // namespace media