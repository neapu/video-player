#pragma once
#include <cstdint>
#include <memory>

namespace media {
enum class PixelFormat {
    NONE,
    YUV420P,
    NV12,

    YUV420P10LE,
    P010LE,
};

enum class MemoryType {
    NONE,
    CPU,
    GPU
};

enum class ColorSpace {
    BT601,
    BT709,
    BT2020
};

enum class ColorRange {
    LIMITED,
    FULL
};

// 音频采样格式，没有特殊说明都为交错格式
enum class SampleFormat {
    NONE,
    S16LE,
    FLT
};

struct FrameFormat {
    MemoryType memoryType;
    int width, height;
    PixelFormat pixelFormat;
    ColorSpace colorSpace;
    ColorRange colorRange;

    int sampleRate;
    int channels;
    int samples;
    SampleFormat sampleFormat;
};

enum class FrameType {
    EMPTY,  // 帧队列为空时，返回此类型
    NORMAL, // 正常帧，只有这个类型的帧才包含有效数据
    END,    // 结束标记，特殊的空帧，渲染器可以根据此类型进行处理
};

class IMediaFrame {
public:
    virtual ~IMediaFrame() = default;

    /**
     * @brief 获取帧类型
     * @return FrameType 帧类型
     */
    virtual FrameType type() const = 0;

    /**
     * @brief 获取帧格式
     * @return FrameFormat 帧格式
     */
    virtual FrameFormat format() const = 0;

    /**
     * @brief 获取帧数据
     * @param index 数据索引
     * @return uint8_t* 帧数据指针
     * @note 对应 FFmpeg 的 AVFrame 中的 data 字段
     */
    virtual uint8_t* data(int index) = 0;
    virtual const uint8_t* data(int index) const = 0;

    /**
     * @brief 获取每行字节数
     * @param index 数据索引
     * @return int 每行字节数
     * @note 对应 FFmpeg 的 AVFrame 中的 linesize 字段
     */
    virtual int lineSize(int index) const = 0;

    /**
     * @brief 获取底层 AVFrame 原始指针
     * @return void* AVFrame 原始指针
     * @note 只有 NORMAL 类型的帧才包含有效的 AVFrame 指针。其他类型的帧对象会返回 nullptr。
     */
    virtual void* rawFrame() = 0;
    virtual const void* rawFrame() const = 0;

    /**
     * @brief 释放对 AVFrame 指针的所有权
     * @return void* 被释放的 AVFrame 原始指针
     * @note 调用此方法后，IMediaFrame 实例将不再拥有 AVFrame 的所有权，调用者需要负责释放返回的指针。
     * @note 只有 NORMAL 类型的帧才包含有效的 AVFrame 指针。其他类型的帧对象会返回 nullptr。掏空后，当前帧对象的类型会变为 EMPTY.
     */
    virtual void* release() = 0;

    /**
     * @brief 获取帧的显示时间戳 (Presentation Timestamp)
     * @return int64_t 帧的显示时间戳
     * @note 单位已经转换为微秒 (microseconds)
     */
    virtual int64_t pts() const = 0;
};
using MediaFramePtr = std::unique_ptr<IMediaFrame>;

} // namespace media