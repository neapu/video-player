#pragma once
#include <QMainWindow>
#include "gl_video_renderer/GLVideoRenderer.h"

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private:
    GLVideoRenderer* m_videoRenderer{nullptr};
};
} // namespace view