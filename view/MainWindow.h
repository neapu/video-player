#pragma once
#include <QMainWindow>
#include "rhi_video_renderer/QRhiVideoRenderer.h"
#include "Controller.h"

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private:
    QRhiVideoRenderer* m_videoRenderer{nullptr};

    Controller* m_controller{nullptr};
    
};
} // namespace view