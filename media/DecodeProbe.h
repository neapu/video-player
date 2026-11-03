#pragma once
#include "PacketCache.h"
#include "Decoder.h"
#include "Logger.h"

namespace media {
class DecodeProbe {
public:
    DecodeProbe(
        PacketCache& cache,
        const std::unique_ptr<Decoder>& decoder,
        int streamIndex,
        LoggerPtr logger
    );

    fh::FramePtr testDecode();

private:
    int getBudget();

private:
    PacketCache& m_cache;
    const std::unique_ptr<Decoder>& m_decoder;
    int m_streamIndex{-1};
    LoggerPtr m_logger{};
};

} // namespace media
