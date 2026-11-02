#include "MainWindow.h"
#include <QVBoxLayout>
namespace view {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
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

    m_controller->openFile(filePath);
}
} // namespace view
