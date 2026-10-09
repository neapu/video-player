#pragma once
#include <string>
#include <vector>
#include <functional>
#include "IMediaFrame.h"

namespace media {
enum class Error {
    /**
     * @brief 读取媒体数据失败。
     * @note 恢复方法：触发一次成功的 seek 操作。
     */
    ReadFailed,
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
         * @param success 是否成功完成 seek 操作。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(bool)> onSeekComplete{};
        /**
         * @brief 回调函数，在发生错误时被调用。
         * @param error 发生的错误类型。
         * @note 该回调发生在 media 内部线程，不允许在回调中析构 MediaSource 实例。
         */
        std::function<void(Error)> onError{};
    };

    static std::unique_ptr<IMediaSource> create(const Options& options);

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
     * @return 实际使用的像素格式。
     * @note 启用流后必须消费，否则可能饿死其他流。
     */
    virtual PixelFormat enableVideoStream(int streamIndex, std::vector<PixelFormat> requiredPixelFormats) = 0;
    /**
     * @brief 启用指定的媒体流，并返回实际使用的采样格式。适用于音频流。
     * @param streamIndex 媒体流索引。
     * @param requiredSampleFormats 所需的采样格式列表。
     * @return 实际使用的采样格式。
     * @note 启用流后必须消费，否则可能饿死其他流。
     */
    virtual SampleFormat enableAudioStream(int streamIndex, std::vector<SampleFormat> requiredSampleFormats) = 0;

    virtual MediaFramePtr nextFrame(int streamIndex) = 0;

    virtual int64_t duration() const = 0;

    virtual void seek(int64_t positionUs) = 0;

};

} // namespace media