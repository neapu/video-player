#pragma once
#include <thread>
#include <mutex>
#include <optional>
#include "media/IMediaSource.h"
#include "PacketCache.h"
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
        VideoDecodeMode decodeMode = VideoDecodeMode::Software,
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

    std::unique_ptr<Decoder> createDecoder(const AVStream* stream, bool hwaccel = false, void* hwctx = nullptr);
    DecodeWorkerPtr createVideoDecodeWorker(const AVStream* stream, std::unique_ptr<Decoder>&& decoder, std::vector<FrameProcessorPtr> processors);
    DecodeWorkerPtr createAudioDecodeWorker(const AVStream* stream, std::unique_ptr<Decoder>&& decoder, std::vector<FrameProcessorPtr> processors);
    // CPU 帧格式协商：探测帧格式在请求列表中→不追加处理器；
    // 否则追加 SwsConvertor 转换到列表中首个可映射格式，并用探测帧试转换验证。
    // 成功返回最终格式，失败返回 nullopt（调用方按解码器创建失败处理）
    std::optional<PixelFormat> negotiateCpuFrameFormat(
        const fh::FramePtr& probeFrame,
        const std::vector<PixelFormat>& requirePixelFormats,
        std::vector<FrameProcessorPtr>& outProcessors);

    void demuxWorkerFunc();
private:
    std::string m_source;
    std::string m_logDir;
    std::string m_instanceName;
    std::function<void(SeekError)> m_onSeekComplete;
    std::function<void(RunningError)> m_onRunningError;

    LoggerPtr m_logger;
    // 套在 Demuxer 上的包缓存层，持有 Demuxer 的所有权：
    // 探测阶段读到的包缓存于此，demux 线程读包与 seek 均经过此层
    std::unique_ptr<PacketCache> m_packetCache;
    
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