#pragma once
#include <cstdint>
#include <memory>

namespace media {
enum class PixelFormat {
    None,
    Yuv420p,
    Nv12,

    Yuv420p10le,
    P010le,
};

enum class MemoryType {
    None,
    Cpu,
    Gpu
};

enum class ColorSpace {
    Bt601,
    Bt709,
    Bt2020
};

enum class ColorRange {
    Limited,
    Full
};

enum class ColorTransferCharacteristic {
    None,
    Pq,
    Hlg,
};

enum class SampleFormat {
    None,
    S16le,
    S16lePlanar,
    S32le,
    S32lePlanar,
    Flt,
    FltPlanar
};

struct FrameFormat {
    MemoryType memoryType;
    int width, height;
    PixelFormat pixelFormat;
    ColorSpace colorSpace;
    ColorRange colorRange;
    ColorTransferCharacteristic colorTrc;

    int sampleRate;
    int channels;
    int samples;
    SampleFormat sampleFormat;
};

enum class FrameType {
    Empty,  // 帧队列为空时，返回此类型
    Normal, // 正常帧，只有这个类型的帧才包含有效数据
    End,    // 结束标记，特殊的空帧，渲染器可以根据此类型进行处理
};

class IMediaFrame {
public:
    static std::unique_ptr<IMediaFrame> makeEmptyFrame();

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
     * @param plane 数据索引
     * @return uint8_t* 帧数据指针
     * @note 对应 FFmpeg 的 AVFrame 中的 data 字段
     */
    virtual uint8_t* data(int plane) = 0;
    virtual const uint8_t* data(int plane) const = 0;

    /**
     * @brief 获取每行字节数
     * @param plane 数据索引
     * @return int 每行字节数
     * @note 对应 FFmpeg 的 AVFrame 中的 linesize 字段
     */
    virtual int lineSize(int plane) const = 0;

    /**
     * @brief 获取底层 AVFrame 原始指针
     * @return void* AVFrame 原始指针
     * @note 只有 Normal 类型的帧才包含有效的 AVFrame 指针。其他类型的帧对象会返回 nullptr。
     */
    virtual void* rawFrame() = 0;
    virtual const void* rawFrame() const = 0;

    /**
     * @brief 释放对 AVFrame 指针的所有权
     * @return void* 被释放的 AVFrame 原始指针
     * @note 调用此方法后，IMediaFrame 实例将不再拥有 AVFrame 的所有权，调用者需要负责释放返回的指针。
     * @note 只有 Normal 类型的帧才包含有效的 AVFrame 指针。其他类型的帧对象会返回 nullptr。掏空后，当前帧对象的类型会变为 Empty.
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