#pragma once
#include <QMainWindow>
#include "rhi_video_renderer/QRhiVideoRenderer.h"
#include "Controller.h"

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    struct Options {
        // 命令行输入
        QString initialFilePath;
    };

    explicit MainWindow(const Options &options, QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private slots:
    void onVideoRendererInitialized();

private:
    QString m_initialFilePath;

    QRhiVideoRenderer* m_videoRenderer{nullptr};

    Controller* m_controller{nullptr};
    
};
} // namespace view