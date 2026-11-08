#include "AudioRenderer.h"
#include <QDebug>

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

    ma_device_set_master_volume(&m_device, m_volume);

    return true;
}

bool AudioRenderer::start(std::function<media::MediaFramePtr()> frameSource)
{
    m_frameSource = frameSource;
    if (!m_frameSource) {
        qCritical() << "No frame source";
        return false;
    }
    if (!ma_device_start(&m_device)) {
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
{}

} // namespace view