#pragma once
#include "MediaPacket.h"
#include "MediaFrame.h"
#include "Logger.h"
#include "Decoder.h"

namespace media {
class DecodeWorker {
public:
    DecodeWorker(LoggerPtr logger, Decoder&& decoder);

    void pushPacket(MediaPacket&& packet);
    MediaFrame popFrame();

private:
    LoggerPtr m_logger;
    Decoder m_decoder;
};
} // namespace media