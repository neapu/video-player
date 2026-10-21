#pragma once

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
};
enum class RunningError {
    None,
    /**
     * @brief 读取媒体数据失败。
     * @note 恢复方法：触发一次成功的 seek 操作。
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
    DecoderCreationFailed
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
} // namespace media