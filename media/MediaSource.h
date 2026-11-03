#pragma once
#include <thread>
#include <mutex>
#include "media/IMediaSource.h"
#include "Demuxer.h"
#include <unordered_map>
#include "DecodeWorker.h"
#include "SwsConvertor.h"
#include "SwrConvertor.h"

namespace media {
class MediaSource : public IMediaSource {
public:
    ~MediaSource() override;
    static std::expected<std::unique_ptr<IMediaSource>, CreateError> create(const Options& options);

    std::vector<StreamInfo> streamsInfo() const override;
    std::expected<PixelFormat, StreamError> enableVideoStream(
        int streamIndex, 
        std::vector<PixelFormat> requirePixelFormats,
        bool hardwareAccelerated = false,
        void* hardwareContext = nullptr
    ) override;
    std::expected<SampleFormat, StreamError> enableAudioStream(
        int streamIndex,
        std::vector<SampleFormat> requireSampleFormats
    ) override;
    StreamError disableStream(int streamIndex) override;
    MediaFramePtr nextFrame(int streamIndex) override;
    int64_t duration() const override;
    void seek(int64_t timestamp) override;
    StartError start() override;
    void stop() override;
    bool running() const override 
    {
        return m_demuxRunning.load();
    }
    
private:
    MediaSource(const Options& options);
    CreateError initialize();

    std::unique_ptr<Decoder> createDecoder(AVStream* stream, bool hwaccel = false, void* hwctx = nullptr);
    DecodeWorkerPtr createVideoDecodeWorker(AVStream* stream, std::unique_ptr<Decoder>&& decoder, FrameProcessorPtr processor);
    DecodeWorkerPtr createAudioDecodeWorker(AVStream* stream, std::unique_ptr<Decoder>&& decoder, FrameProcessorPtr processor);

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

    // 保护 m_demuxRunning 的状态迁移、m_demuxThread 和 m_decodeWorkers 的访问。
    // 只在公开接口(start/stop/enable*/disableStream/seek/nextFrame)中获取；
    // demuxWorkerFunc 绝不能获取此锁(stop() 持锁 join demux 线程，加了就死锁)，
    // 它对 m_decodeWorkers 的无锁读依赖：写 map 前必须确认 m_demuxRunning 为 false，
    // 而 stop() 在持锁状态下 join 完 demux 线程才放锁。
    std::mutex m_stateMutex;

    std::unordered_map<int, DecodeWorkerPtr> m_decodeWorkers;

    std::atomic_int64_t m_seekTarget{AV_NOPTS_VALUE};
};
} // namespace media