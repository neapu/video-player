#pragma once
#include <cstdint>
#include <memory>

#ifdef _WIN32
struct ID3D11Texture2D;
#endif

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
     * @brief 获取 Y 平面数据指针（所有视频格式均有）
     * @return uint8_t* Y 平面数据指针，非视频帧或无数据时返回 nullptr
     */
    virtual uint8_t* y() const = 0;

    /**
     * @brief 获取 Y 平面每行字节数
     * @return int 每行字节数，非视频帧或无数据时返回 0
     */
    virtual int yStride() const = 0;

    /**
     * @brief 获取 U 平面数据指针（仅 Yuv420p / Yuv420p10le 格式）
     * @return uint8_t* U 平面数据指针，其他格式返回 nullptr
     */
    virtual uint8_t* u() const = 0;

    /**
     * @brief 获取 U 平面每行字节数（仅 Yuv420p / Yuv420p10le 格式）
     * @return int 每行字节数，其他格式返回 0
     */
    virtual int uStride() const = 0;

    /**
     * @brief 获取 V 平面数据指针（仅 Yuv420p / Yuv420p10le 格式）
     * @return uint8_t* V 平面数据指针，其他格式返回 nullptr
     */
    virtual uint8_t* v() const = 0;

    /**
     * @brief 获取 V 平面每行字节数（仅 Yuv420p / Yuv420p10le 格式）
     * @return int 每行字节数，其他格式返回 0
     */
    virtual int vStride() const = 0;

    /**
     * @brief 获取 UV 交织平面数据指针（仅 Nv12 / P010le 格式）
     * @return uint8_t* UV 平面数据指针，其他格式返回 nullptr
     */
    virtual uint8_t* uv() const = 0;

    /**
     * @brief 获取 UV 交织平面每行字节数（仅 Nv12 / P010le 格式）
     * @return int 每行字节数，其他格式返回 0
     */
    virtual int uvStride() const = 0;

    /**
     * @brief 获取音频平面数据指针
     * @param plane 平面索引。交织格式（S16le / S32le / Flt）仅平面 0 有效；
     *              平面格式（*Planar）每个声道一个平面，索引范围 0 ~ channels-1
     * @return uint8_t* 平面数据指针，非音频帧或索引无效时返回 nullptr
     */
    virtual uint8_t* audioData(int plane) const = 0;

    /**
     * @brief 获取音频平面的有效数据字节数
     * @param plane 平面索引，规则同 audioData()
     * @return int 有效字节数，非音频帧或索引无效时返回 0
     * @note 按帧格式参数计算：交织格式为 采样数 × 每采样字节数 × 声道数，
     *       平面格式为 采样数 × 每采样字节数。不能使用底层缓冲区的行字节数，其可能包含对齐填充
     */
    virtual int audioDataSize(int plane) const = 0;

    /**
     * @brief 获取帧时长
     * @return int64_t 帧时长，单位微秒 (microseconds)。音频帧按 采样数 / 采样率 计算，非音频帧返回 0
     */
    virtual int64_t duration() const = 0;

    /**
     * @brief 获取采样格式的每采样字节数
     * @param format 采样格式
     * @return int 每采样（每声道）字节数，未知格式返回 0
     */
    static int bytesPerSample(SampleFormat format);

#ifdef _WIN32
    /**
     * @brief 获取 GPU 帧的 D3D11 纹理指针（仅 MemoryType::Gpu 帧）
     * @return ID3D11Texture2D* 纹理指针（可能是纹理数组），非 GPU 帧返回 nullptr
     */
    virtual ID3D11Texture2D* texture() const = 0;

    /**
     * @brief 获取 GPU 帧纹理在纹理数组中的切片索引（仅 MemoryType::Gpu 帧）
     * @return int 切片索引（即 MipLevel 为 0 时的子资源索引），非 GPU 帧返回 0
     */
    virtual int subresource() const = 0;
#endif

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

    /**
     * @brief 设置帧的显示时间戳 (Presentation Timestamp)
     * @param pts 显示时间戳，单位微秒 (microseconds)
     * @note 写入底层 AVFrame（换算为其时间基）。无效pts的帧经此修正后，下游可无条件信任 pts()
     */
    virtual void setPts(int64_t pts) = 0;
};
using MediaFramePtr = std::unique_ptr<IMediaFrame>;

} // namespace media