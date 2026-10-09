#pragma once
#include <QRhiWidget>
#include "media/IMediaFrame.h"

namespace view {
class VideoRenderer : public QRhiWidget {
    Q_OBJECT

public:
    explicit VideoRenderer(QWidget *parent = nullptr);
    ~VideoRenderer();

    void start(std::function<media::MediaFramePtr()> frameSource);
    void stop();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

private:
    std::function<media::MediaFramePtr()> m_frameSource;
};
} // namespace view