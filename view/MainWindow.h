#pragma once
#include <QMainWindow>
#include "rhi_video_renderer/QRhiVideoRenderer.h"
#include "Controller.h"

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    struct Options {
        QString initialFilePath{};
        bool hwaccel{true};
    };

    explicit MainWindow(const Options &options, QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private slots:
    void onVideoRendererInitialized();

private:
    QString m_initialFilePath;
    bool m_hwaccel{true};

    QRhiVideoRenderer* m_videoRenderer{nullptr};

    Controller* m_controller{nullptr};
    
};
} // namespace view