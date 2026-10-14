#pragma once
#include <string>
#include <vector>
#include <functional>
#include <expected>
#include "IMediaFrame.h"

namespace media {
enum class Error {
    None,
    // ==== 创建阶段 ====
    /**
     * @brief 打开文件失败。
     * @note 所有文件相关问题（包括找不到文件，没有有效流等）都会返回此错误。
     */
    FileOpenFailed,

    // ==== 运行阶段 ====
    /**
     * @brief 读取媒体数据失败。
     * @note 恢复方法：触发一次成功的 seek 操作。
     */
    ReadFailed,
    /**
     * @brief Seek 操作失败。
     * @note 无需恢复，流会继续保持当前状态。
     */ 
    SeekFailed,

    // 根据后续实现添加错误类型

    /**
     * @brief 未知错误。
     * @note 所有未知错误都视为不可恢复，需要重建 MediaSource 实例。
     */
    Unknown
};

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
        std::function<void()> onSeekSuccess{};
        /**
         * @brief 回调函数，在发生错误时被调用。
         * @param error 发生的错误类型。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(Error)> onError{};
    };

    static std::expected<std::unique_ptr<IMediaSource>, Error> create(const Options& options);

    virtual ~IMediaSource() = default;

    /**
     * @brief 获取媒体流信息。
     * @return 包含所有媒体流信息的数组。
     */
    virtual std::vector<StreamInfo> getStreamInfo() const = 0;

    /**
     * @brief 启用指定的媒体流，并返回实际使用的像素格式。适用于视频流。
     * @param streamIndex 媒体流索引。
     * @param requiredPixelFormats 所需的像素格式列表。
     * @return std::expected<PixelFormat, Error> 实际使用的像素格式或错误信息。
     * @note 启用流后必须消费，否则可能饿死其他流。需要在调用 start 之前启用，播放过程中切换流时，需要先调用 stop
     * @note 如果启用硬件加速，会无视 requiredPixelFormats，实际使用的像素格式由硬件决定。
     */
    virtual std::expected<PixelFormat, Error> enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats, bool hardwareAccelerated = false) = 0;
    /**
     * @brief 启用指定的媒体流，并返回实际使用的采样格式。适用于音频流。
     * @param streamIndex 媒体流索引。
     * @param requiredSampleFormats 所需的采样格式列表。
     * @return std::expected<SampleFormat, Error> 实际使用的采样格式或错误信息。
     * @note 启用流后必须消费，否则可能饿死其他流。需要在调用 start 之前启用，播放过程中切换流时，需要先调用 stop
     */
    virtual std::expected<SampleFormat, Error> enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats) = 0;

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
     * @note 成功将触发 onSeekSuccess 回调。失败将触发 onError 回调。
     */
    virtual void seek(int64_t positionUs) = 0;

    /**
     * @brief 开始读取并解码媒体数据。
     */
    virtual void start() = 0;

    /**
     * @brief 停止读取并解码媒体数据。
     * @note 已经进入队列的帧仍然可以读取，不会停止解码线程，只是不再读取新的媒体数据。
     */
    virtual void stop() = 0;
};

} // namespace media