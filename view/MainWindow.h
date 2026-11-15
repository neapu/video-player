#pragma once
#include <QMainWindow>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include "rhi_video_renderer/QRhiVideoRenderer.h"
#include "Controller.h"
#include "media/IMediaSource.h"
#include "components/PlayOrPauseButton.h"
#include "components/PlaybackSlider.h"
#include "components/VolumeButton.h"

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    struct Options {
        QString initialFilePath{};
        media::VideoDecodeMode decodeMode{media::VideoDecodeMode::Hardware};
        int decodeGpu{-1};  // media::IMediaSource::enumerateGpus() 序号，-1 = 自动
    };

    explicit MainWindow(const Options &options, QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private:
    void createCentralWidget();
    void createMenu();
    // 拖动预览：按进度条位置（毫秒）更新时间标签
    void previewSeekPosition(int valueMs);

private slots:
    void onPlayStateChanged(Controller::State state);
    void onVideoRendererInitialized();
    void onOpenTriggered();
    void onPlayOrPauseButtonClicked();
    void onStopButtonClicked();
    void onPositionChanged(int64_t pos);

private:
    QString m_initialFilePath;
    // 解码模式与解码 GPU，通过菜单切换，对之后打开的文件生效
    media::VideoDecodeMode m_decodeMode{media::VideoDecodeMode::Hardware};
    int m_decodeGpu{-1};

    QRhiVideoRenderer* m_videoRenderer{nullptr};

    Controller* m_controller{nullptr};
    PlaybackSlider* m_playbackBar{nullptr};
    PlaybackSlider* m_volumeBar{nullptr};
    VolumeButton* m_muteButton{nullptr};
    QLabel* m_volumeLabel{nullptr};

    // 静音前的音量，用于取消静音时恢复
    int m_volumeBeforeMute{100};

    PlayOrPauseButton* m_playButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    QLabel* m_timeLabel{nullptr};
    QString m_durationStr{};

    // 进度条拖动中：只做位置/时间预览，不回写播放位置，松手后才seek
    bool m_seeking{false};
    
};
} // namespace view