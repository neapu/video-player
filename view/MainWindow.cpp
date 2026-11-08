#include "MainWindow.h"
#include <QVBoxLayout>
namespace view {

MainWindow::MainWindow(const Options &options, QWidget* parent) 
    : QMainWindow(parent)
    , m_initialFilePath(options.initialFilePath)
    , m_hwaccel(options.hwaccel)
{
    resize(800, 600);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    m_videoRenderer = new QRhiVideoRenderer(central);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoRenderer);

    m_controller = new Controller(this);
    m_controller->setVideoSink(m_videoRenderer);

    connect(m_videoRenderer, &QRhiVideoRenderer::initialized, this, &MainWindow::onVideoRendererInitialized, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    m_controller->stop();
}

void MainWindow::openFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return;
    }

    m_controller->openFile(filePath, m_hwaccel);
}

void MainWindow::onVideoRendererInitialized()
{
    if (!m_initialFilePath.isEmpty()) {
        openFile(m_initialFilePath);
        m_initialFilePath.clear();
    }
}
} // namespace view
