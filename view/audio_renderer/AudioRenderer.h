#pragma once
#include <memory>
#include <functional>
#include <miniaudio.h>
#include "media/IMediaFrame.h"

namespace view {
using SampleFormat = media::SampleFormat;
class AudioRenderer {
public:
    struct Options {
        int sampleRate{44100};
        int channels{2};
        SampleFormat sampleFormat{SampleFormat::Flt};
        float volume{1.0f};
    };

    static std::unique_ptr<AudioRenderer> create(const Options& options);

    ~AudioRenderer();

    // positionCallback 在音频回调线程触发，参数为估算的当前可听位置（微秒，媒体时间），
    // 每个写入过有效数据的回调周期结束时调用一次
    bool start(std::function<media::MediaFramePtr()> frameSource,
               std::function<void(int64_t audibleMediaUs)> positionCallback = nullptr);
    void stop();

    // 取值 0.0 ~ 1.0
    void setVolume(float volume);

private:
    explicit AudioRenderer(const Options& options);
    bool initialize();

    void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

private:
    int m_sampleRate{0};
    int m_channels{0};
    media::SampleFormat m_sampleFormat{media::SampleFormat::None};
    float m_volume{1.0f};
    ma_device m_device;

    std::function<media::MediaFramePtr()> m_frameSource;
    std::function<void(int64_t)> m_positionCallback;
    media::MediaFramePtr m_playingFrame;
    size_t m_playingOffset{0};

    // 设备内部缓冲时长（微秒），initialize 后实测，数据从写入到出声的延迟
    int64_t m_deviceBufferUs{0};
    // 已写入设备的最后采样对应的媒体时间（微秒），仅音频回调线程访问
    int64_t m_writeCursorMediaUs{0};
    // 当前帧第一采样对应的媒体时间（微秒），无效pts时按游标连续性外推
    int64_t m_frameStartMediaUs{0};
};
} // namespace view