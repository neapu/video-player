#include "AudioRenderer.h"
#include <QDebug>
#include <cstring>

namespace {
ma_format sampleFormatToMa(media::SampleFormat fmt)
{
    switch(fmt) {
        case media::SampleFormat::Flt: return ma_format_f32;
        case media::SampleFormat::S16le: return ma_format_s16;
        case media::SampleFormat::S32le: return ma_format_s32;
        default: return ma_format_unknown; // 暂不处理平面格式
    }
}
size_t sizePerSample(media::SampleFormat fmt)
{
    switch(fmt) {
        case media::SampleFormat::Flt: return 4;
        case media::SampleFormat::S16le: return 2;
        case media::SampleFormat::S32le: return 4;
        default: return 0;
    }
}
}

namespace view {
std::unique_ptr<AudioRenderer> AudioRenderer::create(const Options& options)
{
    auto renderer = std::unique_ptr<AudioRenderer>(new AudioRenderer(options));
    if (renderer->initialize()) {
        return renderer;
    }
    return nullptr;
}

AudioRenderer::AudioRenderer(const Options& options)
    : m_sampleRate(options.sampleRate),
      m_channels(options.channels),
      m_sampleFormat(options.sampleFormat),
      m_volume(options.volume)
{
}

AudioRenderer::~AudioRenderer()
{
    stop();
    ma_device_uninit(&m_device);
}

bool AudioRenderer::initialize()
{
    if (m_sampleRate <= 0 || m_channels <= 0) {
        qCritical() << "Invalid audio options";
        return false;
    }

    auto format = sampleFormatToMa(m_sampleFormat);
    if (format == ma_format_unknown) {
        qCritical() << "Unsupported sample format";
        return false;
    }

    qInfo() << "Initializing audio with" << m_sampleRate << "Hz," << m_channels << "channels";
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = format;
    config.playback.channels = static_cast<ma_uint32>(m_channels);
    config.sampleRate = static_cast<ma_uint32>(m_sampleRate);
    config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* renderer = static_cast<AudioRenderer*>(pDevice->pUserData);
        renderer->maDataCallback(pDevice, pOutput, pInput, frameCount);
    };
    config.pUserData = this;

    if (ma_device_init(NULL, &config, &m_device) != MA_SUCCESS) {
        qCritical() << "Failed to initialize audio device";
        return false;
    }

    // 实测设备内部缓冲大小，用于估算写入到出声的延迟
    ma_uint32 bufferFrames = 0;
#ifdef MA_SUPPORT_WASAPI
    // low-latency 共享模式下 internalPeriodSizeInFrames 不等于真实缓冲大小
    if (m_device.pContext && m_device.pContext->backend == ma_backend_wasapi) {
        bufferFrames = m_device.wasapi.actualBufferSizeInFramesPlayback;
    }
#endif
    if (bufferFrames == 0) {
        bufferFrames = m_device.playback.internalPeriodSizeInFrames * m_device.playback.internalPeriods;
    }
    ma_uint32 internalRate = m_device.playback.internalSampleRate;
    if (internalRate == 0) {
        internalRate = static_cast<ma_uint32>(m_sampleRate);
    }
    m_deviceBufferUs = (int64_t)bufferFrames * 1000000 / internalRate;
    qInfo() << "Audio device buffer:" << bufferFrames << "frames," << m_deviceBufferUs << "us";

    ma_device_set_master_volume(&m_device, m_volume);

    return true;
}

bool AudioRenderer::start(std::function<media::MediaFramePtr()> frameSource,
                          std::function<void(int64_t)> positionCallback)
{
    m_frameSource = frameSource;
    m_positionCallback = std::move(positionCallback);
    if (!m_frameSource) {
        qCritical() << "No frame source";
        return false;
    }
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        qCritical() << "Failed to start audio device";
        return false;
    }
    return true;
}

void AudioRenderer::stop()
{
    ma_device_stop(&m_device);
}

void AudioRenderer::setVolume(float volume)
{
    m_volume = volume;
    ma_device_set_master_volume(&m_device, m_volume);
}

void AudioRenderer::maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{
    const size_t requireSize = frameCount * m_channels * sizePerSample(m_sampleFormat);
    const size_t bytesPerFrame = m_channels * sizePerSample(m_sampleFormat);
    size_t playedSize{0};
    bool wroteData{false};
    while (playedSize < requireSize) {
        if (!m_playingFrame || m_playingOffset >= m_playingFrame->audioDataSize(0)) {
            m_playingFrame = m_frameSource();
            m_playingOffset = 0;
        }

        if (!m_playingFrame || m_playingFrame->type() != media::FrameType::Normal
            || m_playingFrame->audioDataSize(0) == 0) {
            // 播放静音，不推进游标也不上报位置，时钟由上层外推
            std::memset(static_cast<uint8_t*>(pOutput) + playedSize, 0, requireSize - playedSize);
            return;
        }

        // 走到这里帧已确认有效，pts也已由上游保证（无效pts在Controller推算并写回），按帧pts重同步游标
        m_frameStartMediaUs = m_playingFrame->pts();

        size_t frameSize = m_playingFrame->audioDataSize(0) - m_playingOffset;
        size_t copySize = std::min(requireSize - playedSize, frameSize);
        std::memcpy(static_cast<uint8_t*>(pOutput) + playedSize, m_playingFrame->audioData(0) + m_playingOffset, copySize);
        playedSize += copySize;
        m_playingOffset += copySize;
        wroteData = true;
        m_writeCursorMediaUs = m_frameStartMediaUs
            + (int64_t)(m_playingOffset / bytesPerFrame) * 1000000 / m_sampleRate;
    }

    // 回调结束时设备缓冲刚好填满，此刻的可听位置 = 写入游标 - 设备缓冲时长
    if (wroteData && m_positionCallback) {
        m_positionCallback(m_writeCursorMediaUs - m_deviceBufferUs);
    }
}

} // namespace view