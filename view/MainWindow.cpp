#include "MainWindow.h"
#include <QVBoxLayout>
namespace view {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    resize(800, 600);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    m_videoRenderer = new GLVideoRenderer(central);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoRenderer);
}

MainWindow::~MainWindow() {}

void MainWindow::openFile(const QString &filePath)
{}
} // namespace view
