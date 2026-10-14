#pragma once
#include <memory>
#include <functional>
#include <miniaudio.h>
#include "media/IMediaFrame.h"

namespace view {
class AudioRenderer {
public:
    struct Options {
        int sampleRate{44100};
        int channels{2};
        media::SampleFormat sampleFormat{media::SampleFormat::Flt};
        float volume{1.0f};
    };

    static std::unique_ptr<AudioRenderer> create(const Options& options);

    ~AudioRenderer();

    void start(std::function<media::MediaFramePtr()> frameSource);
    void stop();

    // 取值 0.0 ~ 1.0
    void setVolume(float volume);

    // 音频时钟（微秒）：媒体时间轴上"正在被听到"的采样时刻
    int64_t playbackPosition() const;

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
};
} // namespace view