#include "AudioRenderer.h"

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
}

bool AudioRenderer::initialize()
{
    // TODO: Initialize the audio device using miniaudio
    return true;
}

void AudioRenderer::start(std::function<media::MediaFramePtr()> frameSource)
{
    m_frameSource = frameSource;
    // TODO: Start the audio device and begin rendering audio frames from m_frameSource
}

void AudioRenderer::stop()
{
    // TODO: Stop the audio device and stop rendering audio frames
}

void AudioRenderer::setVolume(float volume)
{
    m_volume = volume;
}

int64_t AudioRenderer::playbackPosition() const
{
    // TODO: Return the current playback position in the audio stream
    return 0;
}

void AudioRenderer::maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{}

} // namespace view