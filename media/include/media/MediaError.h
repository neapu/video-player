#pragma once
#include <string>

namespace media {
enum class CreateError {
    None,
    /**
     * @brief 打开文件失败。
     */
    FileOpenFailed,
    /**
     * @brief 没有有效的媒体流。
     */
    NoValidStream,
    /**
     * @brief 参数错误
     */
    InvalidArgument,
    /**
     * @brief 创建日志记录器失败。
     */
    LoggerCreationFailed,
    /**
     * @brief 日志记录器实例名称已被占用。
     */
    LoggerNameAlreadyExists,
};
enum class RunningError {
    None,
    /**
     * @brief 读取媒体数据失败。
     */
    ReadFailed
};
enum class StreamError {
    None,
    /**
     * @brief 无效流序号
     */
    InvalidIndex,
    /**
     * @brief 解封装线程未停止，无法操作流。
     */
    DemuxerRunning,
    /**
     * @brief 指定的流已经启用，无需再次启用。
     */
    StreamAlreadyEnabled,
    /**
     * @brief 指定的流未启用，无法操作。
     */
    StreamNotEnabled,
    /**
     * @brief 创建解码器失败。
     */ 
    DecoderCreationFailed,
    /**
     * @brief 指定的流类型不匹配
     */ 
    StreamTypeMismatch,
    /**
     * @brief 请求的像素/采样格式为空
     */
    RequestFormatEmpty
};
enum class SeekError {
    None,
    /**
     * @brief 媒体源不支持 seek 操作。
     */
    NotSupported,
    /**
     * @brief 其他 seek 错误。
     */
    Unknown
};
enum class StartError {
    None,
    /**
     * @brief 已经启动，无需再次启动。
     */
    AlreadyStarted,

    /**
     * @brief 没有启用任何流。
     */
    NoStreamEnabled
};
enum class ProcessError {
    None,
};

std::string errToString(CreateError err);
std::string errToString(RunningError err);
std::string errToString(StreamError err);
std::string errToString(SeekError err);
std::string errToString(StartError err);
std::string errToString(ProcessError err);
} // namespace media