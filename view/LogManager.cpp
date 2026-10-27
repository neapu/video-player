#include "LogManager.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {
constexpr auto MAX_FILE_SIZE = 10 * 1024 * 1024; // 10MB
constexpr auto MAX_FILES = 5;

QString baseLogDir()
{
#ifdef DEBUG
    const QString dir = QString::fromUtf8(SOURCE_DIR) + QStringLiteral("/logs");
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
#endif
    if (!QDir(dir).exists()) {
        if (!QDir().mkpath(dir)) {
            // 目录创建失败时，退化到临时目录
            return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        }
    }
    return dir;
}
spdlog::filename_t toFilenameT(const QString& path)
{
#ifdef SPDLOG_WCHAR_FILENAMES
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}

// 注意：WIN32 下定义了 SPDLOG_WCHAR_FILENAMES，spdlog::filename_t 是 std::wstring，
// 因此文件名参数必须用 spdlog::filename_t / SPDLOG_FILENAME_T 才能跨平台一致
std::shared_ptr<spdlog::logger> createModuleLogger(const std::string& moduleName, const spdlog::filename_t& logFileName)
{
    // 创建主程序logger
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFileName, 
        MAX_FILE_SIZE,
        MAX_FILES
    );

#ifdef DEBUG
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(moduleName, spdlog::sinks_init_list({fileSink, consoleSink}));
    logger->set_level(spdlog::level::debug);
#else
    auto logger = std::make_shared<spdlog::logger>(moduleName, spdlog::sinks_init_list({fileSink}));
    logger->set_level(spdlog::level::info);
#endif
    logger->flush_on(spdlog::level::warn); // warning以上级别的日志立即刷新，避免程序崩溃时日志丢失
    // 设置日志格式
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    return logger;
}

spdlog::level::level_enum qtMsgTypeToSpdlogLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return spdlog::level::debug;
    case QtInfoMsg:
        return spdlog::level::info;
    case QtWarningMsg:
        return spdlog::level::warn;
    case QtCriticalMsg:
        return spdlog::level::critical;
    case QtFatalMsg:
        return spdlog::level::err;
    default:
        return spdlog::level::info;
    }
}

}

namespace view {
void LogManager::installMessageHandler()
{
    auto logDir = baseLogDir();
    auto mainLogger = createModuleLogger("main", toFilenameT(logDir + "/main.log"));
    spdlog::set_default_logger(mainLogger);

    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg){
        spdlog::log(
            spdlog::source_loc{
                context.file,
                context.line,
                ""
            }, qtMsgTypeToSpdlogLevel(type), msg.toStdString()
        );
    });
}

void LogManager::uninstallMessageHandler()
{
    qInstallMessageHandler(nullptr);
    spdlog::drop_all();
}

std::string LogManager::mediaLogPath()
{
    return baseLogDir().toStdString();
}

} // namespace view