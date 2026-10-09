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

    D3D11
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

enum SampleFormat {
    NONE,
    S16LE,
    FLT
};

struct FrameFormat {
    int width, height;
    PixelFormat pixelFormat;
    ColorSpace colorSpace;
    ColorRange colorRange;

    int sampleRate;
    int channels;
    int samples;
    SampleFormat sampleFormat;
};

enum FrameType {
    Empty,
    Normal,
    End,
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
     */
    virtual void* rawFrame() = 0;
    virtual const void* rawFrame() const = 0;

    /**
     * @brief 释放对 AVFrame 指针的所有权
     * @return void* 被释放的 AVFrame 原始指针
     */
    virtual void* release() = 0;
};
using MediaFramePtr = std::unique_ptr<IMediaFrame>;

} // namespace media