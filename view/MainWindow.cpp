#include "MainWindow.h"
#include <QVBoxLayout>
#include <QMenuBar>
#include <QFileDialog>

namespace {
QPushButton* createIconButton(const QIcon& icon, const QString& toolTip, QWidget* parent = nullptr)
{
    auto* btn = new QPushButton(parent);
    btn->setIcon(icon);
    btn->setIconSize(QSize(18, 18));
    btn->setFixedSize(QSize(32, 32));
    btn->setToolTip(toolTip);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

}

namespace view {
MainWindow::MainWindow(const Options &options, QWidget* parent) 
    : QMainWindow(parent)
    , m_initialFilePath(options.initialFilePath)
    , m_hwaccel(options.hwaccel)
{
    resize(800, 600);

    createCentralWidget();
    createMenu();

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

    m_controller->openFile(filePath, m_hwaccel);
}

void MainWindow::createCentralWidget()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    m_videoRenderer = new QRhiVideoRenderer(central);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoRenderer);

    auto* bar1 = new QHBoxLayout(central);
    bar1->setSpacing(5);
    bar1->setContentsMargins(5, 5, 5, 5);
    m_playbackBar = new QSlider(Qt::Horizontal, central);
    bar1->addWidget(m_playbackBar, 1);
    m_volumeBar = new QSlider(Qt::Horizontal, central);
    m_volumeBar->setMaximumWidth(100);
    m_volumeBar->setMinimumWidth(100);
    m_volumeBar->setMaximum(100);
    m_volumeBar->setMinimum(0);
    m_volumeBar->setValue(100);
    bar1->addWidget(m_volumeBar);
    layout->addLayout(bar1);

    auto* bar2 = new QHBoxLayout(central);
    layout->addLayout(bar2);
    bar2->setSpacing(5);
    bar2->setContentsMargins(5, 5, 5, 5);
    m_playButton = new PlayOrPauseButton(central);
    bar2->addWidget(m_playButton);
    m_stopButton = createIconButton(QIcon(":/icons/stop.svg"), tr("Stop"), central);
    bar2->addWidget(m_stopButton);
    bar2->addStretch();

    connect(m_videoRenderer, &QRhiVideoRenderer::initialized, this, &MainWindow::onVideoRendererInitialized, Qt::QueuedConnection);
}

void MainWindow::createMenu()
{
    auto* fileMenu = menuBar()->addMenu(tr("File"));
    auto* openAct = fileMenu->addAction(tr("Open"));
    auto* exitAct = fileMenu->addAction(tr("Exit"));

    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    connect(exitAct, &QAction::triggered, this, &QMainWindow::close);
}

void MainWindow::onVideoRendererInitialized()
{
    m_videoRenderer->clear();
    if (!m_initialFilePath.isEmpty()) {
        openFile(m_initialFilePath);
        m_initialFilePath.clear();
    }
}
void MainWindow::onOpenTriggered()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open File"), QString(), tr("Videos (*.mp4 *.avi)"));
    if (fileName.isEmpty()) {
        return;
    }

    qInfo() << "Opening" << fileName;
    openFile(fileName);
}
} // namespace view
