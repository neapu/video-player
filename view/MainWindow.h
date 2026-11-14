#pragma once
#include <QMainWindow>
#include <QSlider>
#include <QPushButton>
#include "rhi_video_renderer/QRhiVideoRenderer.h"
#include "Controller.h"
#include "components/PlayOrPauseButton.h"

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
    QSlider* m_playbackBar{nullptr};
    QSlider* m_volumeBar{nullptr};

    PlayOrPauseButton* m_playButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    
};
} // namespace view