#include "media/MediaError.h"
#include <string>

namespace media {
std::string errToString(CreateError err)
{
    switch (err) {
        case CreateError::None: return "None";
        case CreateError::FileOpenFailed: return "Failed to open file";
        case CreateError::NoValidStream: return "No valid stream";
        case CreateError::InvalidArgument: return "Invalid argument";
        case CreateError::LoggerCreationFailed: return "Logger creation failed";
        case CreateError::LoggerNameAlreadyExists: return "Logger name exists";
        default: return "Unknown";
    }
}
std::string errToString(RunningError err)
{
    switch (err) {
        case RunningError::None: return "None";
        case RunningError::ReadFailed: return "Read failed";
        default: return "Unknown";
    }
}
std::string errToString(StreamError err)
{
    switch (err) {
        case StreamError::None: return "None";
        case StreamError::InvalidIndex: return "Invalid Stream index";
        case StreamError::DemuxerRunning: return "Demuxer running, can't operate stream";
        case StreamError::StreamAlreadyEnabled: return "Stream already enabled";
        case StreamError::StreamNotEnabled: return "Stream not enabled";
        case StreamError::DecoderCreationFailed: return "Decoder creation failed";
        case StreamError::StreamTypeMismatch: return "Stream type mismatch";
        case StreamError::RequestFormatEmpty: return "Request format empty";
        default: return "Unknown";
    }
}
std::string errToString(SeekError err)
{
    switch (err) {
        case SeekError::None: return "None";
        case SeekError::NotSupported: return "Not supported";
        default: return "Unknown";
    }
}
std::string errToString(StartError err)
{
    switch (err) {
        case StartError::None: return "None";
        case StartError::AlreadyStarted: return "Already started";
        case StartError::NoStreamEnabled: return "No stream enabled";
        default: return "Unknown";
    }
}
std::string errToString(ProcessError err)
{
    switch (err) {
        case ProcessError::None: return "None";
        default: return "Unknown";
    }
}
}