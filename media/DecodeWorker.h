#pragma once
#include "MediaPacket.h"
#include "MediaFrame.h"
#include "Logger.h"

namespace media {
class DecodeWorker {
public:
    DecodeWorker(LoggerPtr logger);

    void pushPacket(MediaPacket&& packet);
    MediaFrame popFrame();

private:
    LoggerPtr m_logger;
};
} // namespace media