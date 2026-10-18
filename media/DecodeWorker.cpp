#include "DecodeWorker.h"

namespace media {
std::unique_ptr<DecodeWorker> DecodeWorker::create(const options& opts, std::unique_ptr<Decoder>&& decoder)
{
    auto worker = std::unique_ptr<DecodeWorker>(new DecodeWorker(opts, std::move(decoder)));
    if (worker && worker->initialize()) {
        return worker;
    }
    return nullptr;
}

DecodeWorker::DecodeWorker(const options& opts, std::unique_ptr<Decoder>&& decoder)
    : m_maxPacketQueueSize(opts.maxPacketQueueSize)
    , m_maxFrameQueueSize(opts.maxFrameQueueSize)
    , m_logger(opts.logger)
    , m_decoder(std::move(decoder))
    , m_interrupted(false)
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
    return std::move(frame);
}

void DecodeWorker::workerThreadFunc()
{
    while (!m_exitFlag) {
        if (m_interrupted) {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
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
        } else if (packet.type() == PacketType::End) {
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
            std::unique_lock<std::mutex> lock(m_frameQueueMutex);
            if (m_frameQueue.size() >= m_maxFrameQueueSize) {
                m_frameQueueNotFullCondVar.wait(lock, [this]() {
                    return m_exitFlag.load() || m_interrupted || m_frameQueue.size() < m_maxFrameQueueSize;
                });
                if (m_exitFlag.load() || m_interrupted) {
                    break;
                }
            }
            m_frameQueue.push_back(MediaFrame{FrameType::Normal, std::move(*frame)});
        }
    }
}

} // namespace media