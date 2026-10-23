#include "DecodeWorker.h"

namespace media {
std::unique_ptr<DecodeWorker> DecodeWorker::create(const Options& opts, std::unique_ptr<Decoder>&& decoder)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker(opts, std::move(decoder)));
    if (worker && worker->initialize()) {
        return worker;
    }
    return nullptr;
}

DecodeWorker::DecodeWorker(const Options& opts, std::unique_ptr<Decoder>&& decoder)
    : m_maxPacketQueueSize(opts.maxPacketQueueSize)
    , m_maxFrameQueueSize(opts.maxFrameQueueSize)
    , m_logger(opts.logger)
    , m_decoder(std::move(decoder))
{
}

bool DecodeWorker::initialize()
{
    if (!m_decoder) {
        LOG_ERROR(m_logger, "Decoder is null");
        return false;
    }
    m_exitFlag = false;
    m_thread = std::thread(&DecodeWorker::workerThreadFunc, this);
    return true;
}

DecodeWorker::~DecodeWorker()
{
    m_exitFlag = true;
    m_packetQueueNotEmptyCondVar.notify_all();
    m_packetQueueNotFullCondVar.notify_all();
    m_frameQueueNotFullCondVar.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void DecodeWorker::interrupt()
{
    {
        std::lock_guard<std::mutex> lock1(m_packetQueueMutex);
        std::lock_guard<std::mutex> lock2(m_frameQueueMutex);
        m_interrupted = true;
    }
    m_packetQueueNotEmptyCondVar.notify_all();
    m_packetQueueNotFullCondVar.notify_all();
    m_frameQueueNotFullCondVar.notify_all();
}

bool DecodeWorker::pushPacket(MediaPacket&& packet)
{
    std::unique_lock<std::mutex> lock(m_packetQueueMutex);
    // 控制包直接入队，不管队列大小和interrupt标识
    if (packet.type() != PacketType::Normal && packet.type() != PacketType::Empty) {
        m_packetQueue.push_back(std::move(packet));
        m_packetQueueNotEmptyCondVar.notify_one();
        return true;
    }

    if (m_interrupted) {
        return false;
    }
    if (m_packetQueue.size() >= m_maxPacketQueueSize) {
        m_packetQueueNotFullCondVar.wait(lock, [this]() {
            return m_exitFlag.load() || m_interrupted || m_packetQueue.size() < m_maxPacketQueueSize;
        });
        if (m_exitFlag.load() || m_interrupted) {
            return false;
        }
    }
    m_packetQueue.push_back(std::move(packet));
    m_packetQueueNotEmptyCondVar.notify_one();
    return true;
}

MediaFrame DecodeWorker::popFrame()
{
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    if (m_frameQueue.empty()) {
        return MediaFrame{FrameType::Empty};
    }
    auto frame = std::move(m_frameQueue.front());
    m_frameQueue.pop_front();
    m_frameQueueNotFullCondVar.notify_one();
    return std::move(frame);
}

void DecodeWorker::workerThreadFunc()
{
    while (!m_exitFlag) {
        if (m_interrupted) {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            // 清除所有非 Flash 包
            while (!m_packetQueue.empty() && m_packetQueue.front().type() != PacketType::Flush) {
                m_packetQueue.pop_front();
            }
            m_interrupted = false;
        }
        MediaPacket packet{};
        {
            std::unique_lock<std::mutex> lock(m_packetQueueMutex);
            if (m_packetQueue.empty()) {
                m_packetQueueNotEmptyCondVar.wait(lock, [this]() {
                    return m_interrupted || m_exitFlag.load() || !m_packetQueue.empty();
                });
                if (m_exitFlag.load()) {
                    break;
                }
                if (m_interrupted) {
                    continue;
                }
            }
            packet = std::move(m_packetQueue.front());
            m_packetQueue.pop_front();
            m_packetQueueNotFullCondVar.notify_one();
        }

        if (packet.type() == PacketType::Flush) {
            m_decoder->flush();
            std::lock_guard<std::mutex> lock(m_frameQueueMutex);
            m_frameQueue.clear();
            continue;
        } else if (packet.type() == PacketType::End) {
            // End 包只表示输入结束，解码器内部可能还有缓存的帧，
            // 必须先排空，再把 End 标记放到帧队列尾部，保证帧的顺序和完整性
            drainDecoder();
            std::lock_guard<std::mutex> lock(m_frameQueueMutex);
            // End 帧无视水位直接入队
            m_frameQueue.push_back(MediaFrame{FrameType::End});
            continue;
        } else if (packet.type() == PacketType::Empty) {
            continue;
        }

        auto fhPacket = packet.releasePacket();
        if (!fhPacket) {
            LOG_ERROR(m_logger, "Failed to release packet");
            continue;
        }

        int ret = m_decoder->sendPacket(std::move(fhPacket));
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to send packet to decoder, ret: {}", ret);
            continue;
        }
        while (true) {
            auto frame = m_decoder->receiveFrame();
            if (!frame) {
                int err = frame.error();
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame from decoder, err: {}", err);
                break;
            }
            if (!enqueueFrame(std::move(*frame))) {
                break;
            }
        }
    }
}

void DecodeWorker::drainDecoder()
{
    // 发送空包让解码器进入排空(draining)模式，把内部缓存的帧全部取出；
    // 若上一次发送的包还没接收完，sendPacket 会返回 EAGAIN，先收帧再重试
    while (true) {
        int ret = m_decoder->sendPacket(nullptr);
        if (ret == AVERROR_EOF) {
            return;
        }
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            LOG_ERROR(m_logger, "Failed to drain decoder, ret: {}", ret);
            return;
        }

        bool gotFrame = false;
        while (true) {
            auto frame = m_decoder->receiveFrame();
            if (!frame) {
                int err = frame.error();
                if (err == AVERROR_EOF) {
                    return;
                }
                if (err == AVERROR(EAGAIN)) {
                    break;
                }
                LOG_ERROR(m_logger, "Failed to receive frame while draining decoder, err: {}", err);
                return;
            }
            gotFrame = true;
            if (!enqueueFrame(std::move(*frame))) {
                return;
            }
        }

        if (!gotFrame) {
            // 既没有可取的帧，也没能进入排空模式，避免空转
            LOG_ERROR(m_logger, "Failed to drain decoder: no frame available");
            return;
        }
    }
}

bool DecodeWorker::enqueueFrame(fh::FramePtr&& frame)
{
    std::unique_lock<std::mutex> lock(m_frameQueueMutex);
    if (m_frameQueue.size() >= m_maxFrameQueueSize) {
        m_frameQueueNotFullCondVar.wait(lock, [this]() {
            return m_exitFlag.load() || m_interrupted || m_frameQueue.size() < m_maxFrameQueueSize;
        });
        if (m_exitFlag.load() || m_interrupted) {
            return false;
        }
    }
    m_frameQueue.push_back(MediaFrame{FrameType::Normal, std::move(frame)});
    return true;
}

} // namespace media