#include "SwrConvertor.h"
#include <libavutil/channel_layout.h>
#include "ffmpeg_helper/FFmpegError.h"

namespace media {
SwrConvertor::SwrConvertor(const Options& opts)
    : m_targetFormat(opts.targetFormat)
    , m_targetSampleRate(opts.targetSampleRate)
    , m_logger(opts.logger)
{
    if (opts.targetLayout.nb_channels > 0) {
        int ret = av_channel_layout_copy(&m_targetLayout, &opts.targetLayout);
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to copy target audio channel layout: {}", fh::err2str(ret));
        } else {
            m_targetLayoutSpecified = true;
        }
    }
}

SwrConvertor::~SwrConvertor()
{
    av_channel_layout_uninit(&m_targetLayout);
    av_channel_layout_uninit(&m_inputLayout);
}

bool SwrConvertor::configure(const fh::FramePtr& frame, const AVChannelLayout& inputLayout)
{
    const auto inputFormat = static_cast<AVSampleFormat>(frame->format);
    const int inputSampleRate = frame->sample_rate;
    if (inputFormat == AV_SAMPLE_FMT_NONE || inputSampleRate <= 0) {
        LOG_ERROR(m_logger, "Decoded audio frame has an invalid sample format or sample rate");
        return false;
    }

    const bool targetLayoutFollowsInput = !m_targetLayoutSpecified;
    const AVChannelLayout& targetLayout = targetLayoutFollowsInput ? inputLayout : m_targetLayout;
    const auto targetFormat = m_targetFormat == AV_SAMPLE_FMT_NONE ? inputFormat : m_targetFormat;
    const int targetSampleRate = m_targetSampleRate == 0 ? inputSampleRate : m_targetSampleRate;
    if (targetFormat == AV_SAMPLE_FMT_NONE || targetSampleRate <= 0) {
        LOG_ERROR(m_logger, "Target audio format or sample rate is invalid");
        return false;
    }

    const bool inputChanged = !m_swrContext ||
        m_inputFormat != inputFormat ||
        m_inputSampleRate != inputSampleRate ||
        av_channel_layout_compare(&m_inputLayout, &inputLayout) != 0;
    const bool targetChanged = !m_swrContext ||
        targetFormat != m_outputFormat ||
        targetSampleRate != m_outputSampleRate ||
        (targetLayoutFollowsInput && av_channel_layout_compare(&m_targetLayout, &inputLayout) != 0);
    if (!inputChanged && !targetChanged) {
        return true;
    }

    auto context = fh::createSwrContext(
        &targetLayout, targetFormat, targetSampleRate,
        &inputLayout, inputFormat, inputSampleRate
    );
    if (!context) {
        LOG_ERROR(m_logger, "Failed to initialize audio resampler");
        return false;
    }

    if (targetLayoutFollowsInput) {
        av_channel_layout_uninit(&m_targetLayout);
        int ret = av_channel_layout_copy(&m_targetLayout, &inputLayout);
        if (ret < 0) {
            LOG_ERROR(m_logger, "Failed to copy input audio channel layout: {}", fh::err2str(ret));
            return false;
        }
    }
    av_channel_layout_uninit(&m_inputLayout);
    int ret = av_channel_layout_copy(&m_inputLayout, &inputLayout);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to copy input audio channel layout: {}", fh::err2str(ret));
        return false;
    }

    m_inputFormat = inputFormat;
    m_inputSampleRate = inputSampleRate;
    m_outputFormat = targetFormat;
    m_outputSampleRate = targetSampleRate;
    m_swrContext = std::move(context);
    return true;
}

fh::FramePtr SwrConvertor::process(const fh::FramePtr& frame)
{
    if (!frame) {
        LOG_ERROR(m_logger, "Cannot convert a null audio frame");
        return nullptr;
    }

    AVChannelLayout inputLayout{};
    int ret;
    if (frame->ch_layout.nb_channels > 0) {
        ret = av_channel_layout_copy(&inputLayout, &frame->ch_layout);
    } else {
        ret = AVERROR(EINVAL);
    }
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to resolve decoded audio channel layout: {}", fh::err2str(ret));
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }

    if (!configure(frame, inputLayout)) {
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }

    auto output = fh::makeFrame();
    if (!output) {
        LOG_ERROR(m_logger, "Failed to allocate converted audio frame");
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }

    ret = av_frame_copy_props(output.get(), frame.get());
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to copy audio frame properties: {}", fh::err2str(ret));
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }
    output->format = m_outputFormat;
    output->sample_rate = m_outputSampleRate;
    ret = av_channel_layout_copy(&output->ch_layout, &m_targetLayout);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to set converted audio channel layout: {}", fh::err2str(ret));
        av_channel_layout_uninit(&inputLayout);
        return nullptr;
    }

    ret = swr_convert_frame(
        m_swrContext.get(),
        output.get(),
        frame.get()
    );
    av_channel_layout_uninit(&inputLayout);
    if (ret < 0) {
        LOG_ERROR(m_logger, "Failed to convert audio frame: {}", fh::err2str(ret));
        return nullptr;
    }
    return output;
}
}