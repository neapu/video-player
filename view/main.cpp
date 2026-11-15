#include <QApplication>
#include <QCommandLineParser>
#include "MainWindow.h"
#include "LogManager.h"

int main(int argc, char *argv[])
{
    view::LogManager::installMessageHandler();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(APP_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription("Video Player");
    parser.addHelpOption();
    parser.addVersionOption();

    
    // 是否强制软解(无值选项)
    QCommandLineOption softDecodeOption(
        QStringList() << "s" << "software",
        "Use software decoding"
    );
    parser.addOption(softDecodeOption);

    // 硬解码+纹理下载到CPU软渲染(无值选项)
    QCommandLineOption hwDownloadOption(
        QStringList() << "d" << "hw-download",
        "Hardware decoding with GPU texture download to CPU (software rendering)"
    );
    parser.addOption(hwDownloadOption);

    // 输入文件(位置参数或者'-i'，非必须)
    QCommandLineOption inputFileOption(
        QStringList() << "i",
        "Input video file",
        "file"
    );
    parser.addOption(inputFileOption);
    parser.addPositionalArgument("file", "Video file to open");

    parser.process(app);

    bool soft = parser.isSet(softDecodeOption);
    QString input = parser.value(inputFileOption);
    // 如果没有通过-i传，就取位置参数
    if(input.isEmpty()) {
        input = parser.positionalArguments().value(0);
    }

    media::VideoDecodeMode decodeMode = media::VideoDecodeMode::Hardware;
    if (soft) {
        decodeMode = media::VideoDecodeMode::Software;
    } else if (parser.isSet(hwDownloadOption)) {
        decodeMode = media::VideoDecodeMode::HardwareDownload;
    }

    view::MainWindow window{{input, decodeMode}};
    window.show();
    int ret = app.exec();
    view::LogManager::uninstallMessageHandler();
    return ret;
}