#pragma once
#include "Demuxer.h"
#include "Decoder.h"
#include "Logger.h"

namespace media {
class DecodeProbe {
public:
    DecodeProbe(
        const std::unique_ptr<Demuxer>& demuxer,
        const std::unique_ptr<Decoder>& decoder,
        int streamIndex,
        LoggerPtr logger
    );

    fh::FramePtr testDecode();

private:
    int getBudget();

private:
    const std::unique_ptr<Demuxer>& m_demuxer;
    const std::unique_ptr<Decoder>& m_decoder;
    int m_streamIndex{-1};
    LoggerPtr m_logger{};
};

} // namespace media