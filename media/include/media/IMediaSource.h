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

struct StreamInfo {
    StreamType type{StreamType::Unknown};
    int index{-1};
    std::string codecName{};
    int width{0};
    int height{0};
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
     * @param requirePixelFormats 期望的像素格式。
     * @param hardwareAccelerated 是否启用硬件加速，只对视频流有效。
     * @param hardwareContext 硬件上下文指针，仅在启用硬件加速时使用。裸指针安全声明：只借用，需要确保在 MediaSource 对象存在期间该指针有效。
     * @return 成功返回实际使用的像素格式，失败返回错误。
     * @note 启用流后必须消费，否则可能饿死其他流。只能在停止状态下操作流。
     * @note 如果启用硬件加速解码，会无视 requirePixelFormats，返回的PixelFormat没有意义(一般会返回None)
     */
    virtual std::expected<PixelFormat, StreamError> enableVideoStream(
        int streamIndex,
        std::vector<PixelFormat> requirePixelFormats,
        bool hardwareAccelerated = false,
        void* hardwareContext = nullptr
    ) = 0;
    /**
     * @brief 启用指定的媒体流。适用于音频流。
     * @param streamIndex 媒体流索引。
     * @param requireSampleFormats 期望的采样格式。
     * @return 成功返回实际使用的采样格式，失败返回错误。
     * @note 启用流后必须消费，否则可能饿死其他流。只能在停止状态下操作流。
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
     */
    virtual StartError start() = 0;

    /**
     * @brief 停止读取并解码媒体数据。
     * @note 已经进入队列的帧仍然可以读取，不会停止解码线程，只是不再读取新的媒体数据。
     */
    virtual void stop() = 0;

    /**
     * @brief 检查媒体源是否正在运行。
     * @return true 如果媒体源正在运行，false 否则。
     */
    virtual bool running() const = 0;
};

} // namespace media