#pragma once
#include <QMainWindow>
#include <QProgressBar>
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

private:
    void createCentralWidget();
    void createMenu();

private slots:
    void onVideoRendererInitialized();
    void onOpenTriggered();

private:
    QString m_initialFilePath;
    bool m_hwaccel{true};

    QRhiVideoRenderer* m_videoRenderer{nullptr};

    Controller* m_controller{nullptr};
    QProgressBar* m_playbackBar{nullptr};
    QProgressBar* m_volumeBar{nullptr};

    QPushButton* m_playButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    
};
} // namespace view