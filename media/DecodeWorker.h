#pragma once
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include "MediaPacket.h"
#include "MediaFrame.h"
#include "Logger.h"
#include "Decoder.h"

namespace media {
class DecodeWorker {
public:
    struct options {
        int maxPacketQueueSize = 100;
        int maxFrameQueueSize = 5;
        LoggerPtr logger;
    };

    static std::unique_ptr<DecodeWorker> create(const options& opts, std::unique_ptr<Decoder>&& decoder);
    DecodeWorker(const DecodeWorker&) = delete;
    DecodeWorker& operator=(const DecodeWorker&) = delete;
    DecodeWorker(DecodeWorker&&) = delete;
    DecodeWorker& operator=(DecodeWorker&&) = delete;
    ~DecodeWorker();

    bool pushPacket(MediaPacket&& packet);
    MediaFrame popFrame();

    void interrupt();
private:
    DecodeWorker(const options& opts, std::unique_ptr<Decoder>&& decoder);
    bool initialize();

    void workerThreadFunc();

private:
    int m_maxPacketQueueSize;
    int m_maxFrameQueueSize;
    LoggerPtr m_logger;
    std::unique_ptr<Decoder> m_decoder;
    std::thread m_thread;

    std::deque<MediaPacket> m_packetQueue;
    std::mutex m_packetQueueMutex;
    std::condition_variable m_packetQueueNotEmptyCondVar;
    std::condition_variable m_packetQueueNotFullCondVar;

    std::deque<MediaFrame> m_frameQueue;
    std::mutex m_frameQueueMutex;
    // 帧队列出队不阻塞
    std::condition_variable m_frameQueueNotFullCondVar;

    bool m_interrupted;
    std::atomic_bool m_exitFlag{false};
};
using DecodeWorkerPtr = std::unique_ptr<DecodeWorker>;
} // namespace media