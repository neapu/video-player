#pragma once
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include "Demuxer.h"

namespace media {
// 套在 Demuxer 上的包缓存层，持有 Demuxer 的所有权：
// - 探测(DecodeProbe)阶段读到的所有包先入缓存（非破坏性回放，支持多次探测从头重放）
// - demux 线程读包时优先消费缓存（破坏性出队），探测读走的包不会丢失
// - seek 由本层代理：成功后自动丢弃缓存（旧位置的包）
// - Demuxer 的只读接口（流信息、时长等）通过 demuxer() 访问
// 不加锁：调用方(MediaSource)需保证探测只发生在 demux 线程未运行时。
class PacketCache {
public:
    // 缓存默认容量。DecodeProbe 的单次探测总包上限引用此值，两者耦合保证
    // 首轮探测读到的包不会溢出缓存（溢出的包不参与后续回放，见 probeReadPacket）
    static constexpr size_t PACKET_CACHE_CAPACITY = 300;

    PacketCache(std::unique_ptr<Demuxer> demuxer, LoggerPtr logger, size_t capacity = PACKET_CACHE_CAPACITY);

    // Demuxer 的只读访问（流信息、时长等）
    const Demuxer& demuxer() const { return *m_demuxer; }

    // demux 线程读包：先消费缓存，耗尽后直接读 demuxer，不再入缓存。
    // 行为与 Demuxer::readPacket 一致（EOF/错误透传）
    std::expected<fh::PacketPtr, int> readPacket();

    // 探测阶段读包：命中缓存则按序回放（非破坏性），缓存耗尽则从 demuxer 读取并追加进缓存。
    // 返回的引用归缓存所有，调用方只借用，不得保存。
    std::expected<std::reference_wrapper<const fh::PacketPtr>, int> probeReadPacket();

    // 新一次探测前调用：回放位置归零，从缓存头部重放
    void beginProbe() { m_probeReadPos = 0; }

    // 代理 Demuxer 的 seek：成功后清空缓存；seek 失败时 demuxer 位置未变，缓存仍有效
    int seek(int64_t timestamp);

    // 代理 Demuxer 的阻塞控制
    void interrupt() { m_demuxer->interrupt(); }
    void resetInterrupt() { m_demuxer->resetInterrupt(); }
    void cancel() { m_demuxer->cancel(); }
    void resume() { m_demuxer->resume(); }

private:
    std::unique_ptr<Demuxer> m_demuxer;
    LoggerPtr m_logger;
    size_t m_capacity{0};
    std::deque<fh::PacketPtr> m_cache;
    size_t m_probeReadPos{0};                      // 当前探测的回放位置
    fh::PacketPtr m_uncachedProbePacket{nullptr};  // 缓存满时暂存直读的包
};

} // namespace media
