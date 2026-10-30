#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include "../VideoSink.h"

namespace view {
class GLVideoRenderer : public QOpenGLWidget, public VideoSink {
    Q_OBJECT

public:
    explicit GLVideoRenderer(QWidget *parent = nullptr);
    ~GLVideoRenderer();

    void start(const Options& opts) override;
    void stop() override;

private:
    PixelFormat m_format;
    int m_width;
    int m_height;
    std::function<Frame()> m_frameSource;

    QOpenGLFunctions* m_funcs{nullptr};
};
} // namespace view