#pragma once
#include <string>
#include <vector>
#include <functional>
#include <expected>
#include "IMediaFrame.h"
#include "MediaError.h"

namespace media {
enum class StreamType {
    Video,
    Audio,
    // Subtitle,
    Unknown
};

enum class VideoDecodeMode {
    Software,          // 软解码，输出 CPU 帧
    Hardware,          // 硬解码（D3D11VA），输出 GPU 帧（零拷贝渲染）
    HardwareDownload,  // 硬解码 + 下载 GPU 纹理到 CPU，输出 CPU 帧（软渲染）
};

// 可用于解码的 GPU 信息（Windows 为 DXGI 适配器，已过滤软件适配器）
struct GpuInfo {
    int index{0};           // 传给 enableVideoStream 的 decodeAdapterIndex 的序号
    std::string name{};     // 适配器描述
    uint32_t luidHigh{0};   // 适配器 LUID 高 32 位，用于与渲染 GPU 比较
    uint32_t luidLow{0};    // 适配器 LUID 低 32 位
};

struct StreamInfo {
    StreamType type{StreamType::Unknown};
    int index{-1};
    std::string codecName{};
    int width{0};
    int height{0};
    struct FrameRate {
        int num{0};
        int den{1};
    } frameRate;
    int sampleRate{0};
    int channels{0};
};

class IMediaSource {
public:
    struct Options {
        std::string source{};
        std::string logDir{};
        std::string instanceName{"default"}; // 多实例时区分日志
        /**
         * @brief 回调函数，在 seek 操作完成后被调用。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(SeekError)> onSeekComplete{};
        /**
         * @brief 回调函数，在发生错误时被调用。
         * @param error 发生的错误类型。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(RunningError)> onRunningError{};
    };

    static std::expected<std::unique_ptr<IMediaSource>, CreateError> create(const Options& options);

    virtual ~IMediaSource() = default;

    /**
     * @brief 获取媒体流信息。
     * @return 包含所有媒体流信息的数组。
     */
    virtual std::vector<StreamInfo> streamsInfo() const = 0;

    /**
     * @brief 启用指定的媒体流。适用于视频流。
     * @param streamIndex 媒体流索引。
     * @param requirePixelFormats 期望的像素格式。Software 与 HardwareDownload 模式下必填且参与格式协商；
     *                             Hardware 模式下忽略（输出格式由硬解码器决定）。
     * @param decodeMode 解码模式，只对视频流有效。硬解码使用 media 内部创建的 D3D11 设备，与渲染设备解耦：
     *                   Hardware 模式的 GPU 帧通过共享纹理句柄由渲染线程跨设备拷贝（仅同 GPU 可靠，
     *                   跨 GPU 时调用方应选择 HardwareDownload）。
     * @param decodeAdapterIndex 解码适配器序号（enumerateGpus() 返回的 index），-1 = 使用 0 号适配器。
     * @return 成功返回实际输出的像素格式（Hardware 模式为硬件帧的底层像素格式；无法识别时为 None），失败返回错误。
     * @note Software 与 HardwareDownload 模式下，解码输出格式不在请求列表中时会转换为列表中首个可映射格式。
     * @note 启用流后必须消费，否则可能饿死其他流。只能在停止状态下操作流。
     * @note 线程安全，可与其他接口从不同线程并发调用。
     */
    virtual std::expected<PixelFormat, StreamError> enableVideoStream(
        int streamIndex,
        std::vector<PixelFormat> requirePixelFormats,
        VideoDecodeMode decodeMode = VideoDecodeMode::Software,
        int decodeAdapterIndex = -1
    ) = 0;

    /**
     * @brief 枚举可用于硬解码的 GPU。
     * @return GPU 信息列表（Windows 为 DXGI 适配器，已过滤软件适配器）；不支持的平台返回空列表。
     */
    static std::vector<GpuInfo> enumerateGpus();

    /**
     * @brief 查找适配器 LUID 对应的 GPU 序号。
     * @param adapterLuid 适配器 LUID（Windows 下为 DXGI_LUID*），只借用，调用期间有效即可。
     * @return enumerateGpus() 列表中的序号；找不到或平台不支持返回 -1。
     * @note 供调用方将渲染端 LUID 解析为解码 GPU 序号，实现"跟随渲染 GPU"或跨 GPU 降级决策。
     */
    static int gpuIndexByAdapterLuid(const void* adapterLuid);
    /**
     * @brief 启用指定的媒体流。适用于音频流。
     * @param streamIndex 媒体流索引。
     * @param requireSampleFormats 期望的采样格式。
     * @return 成功返回实际使用的采样格式，失败返回错误。
     * @note 启用流后必须消费，否则可能饿死其他流。只能在停止状态下操作流。
     * @note 线程安全，可与其他接口从不同线程并发调用。
     */
    virtual std::expected<SampleFormat, StreamError> enableAudioStream(
        int streamIndex,
        std::vector<SampleFormat> requireSampleFormats
    ) = 0;
    /**
     * @brief 禁用指定的媒体流。
     * @param streamIndex 媒体流索引。
     * @return StreamError 操作结果。
     * @note 只能在停止状态下操作流。
     * @note 当前版本 stop() 会移除所有已启用的流，因此本接口实际使用场景有限；
     *       接口保留是为后续版本支持运行过程中操作流做准备。
     * @note 线程安全，可与其他接口从不同线程并发调用。
     */
    virtual StreamError disableStream(int streamIndex) = 0;

    /**
     * @brief 获取指定媒体流的下一帧。
     * @param streamIndex 媒体流索引。
     * @return MediaFramePtr 指向下一帧的智能指针
     * @note 正常情况下不会返回nullptr，如果返回nullptr表示发生了不可恢复的内部错误。
     */
    virtual MediaFramePtr nextFrame(int streamIndex) = 0;

    /**
     * @brief 获取媒体的总时长。
     * @return int64_t 媒体的总时长，单位为微秒。
     */
    virtual int64_t duration() const = 0;

    /**
     * @brief 跳转到指定的播放位置。
     * @param positionUs 目标播放位置，单位为微秒。
     * @note 完成后触发 onSeekComplete 回调。
     */
    virtual void seek(int64_t positionUs) = 0;

    /**
     * @brief 开始读取并解码媒体数据。
     * @note 线程安全，可与其他接口从不同线程并发调用。
     */
    virtual StartError start() = 0;

    /**
     * @brief 停止读取并解码媒体数据，并将媒体源复位到刚创建时的状态。
     * @note 复位包括：停止读取线程、移除所有已启用的流(解码线程与解码队列随之销毁)、
     *       清空包缓存、demuxer 位置回到文件开头。
     * @note 复位后 nextFrame 只会返回 Empty 帧；如需再次播放，必须重新 enable 流并 start。
     * @note 尚未完成的 seek 请求会被作废。
     * @note 线程安全，可与其他接口从不同线程并发调用。
     */
    virtual void stop() = 0;

    /**
     * @brief 检查媒体源是否正在运行。
     * @return true 如果媒体源正在运行，false 否则。
     */
    virtual bool running() const = 0;
};

} // namespace media