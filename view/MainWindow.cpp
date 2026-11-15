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

QString timeToStr(int64_t us)
{
    int64_t min = us / 60000000;
    int64_t sec = (us / 1000000) % 60;
    return QString("%1:%2").arg(min).arg(sec, 2, 10, QChar('0'));
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

    connect(m_controller, &Controller::stateChanged, this, &MainWindow::onPlayStateChanged);
    connect(m_controller, &Controller::positionChanged, this, &MainWindow::onPositionChanged);
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
    auto duration = m_controller->duration();
    if (duration > 0) {
        // 进度条单位用毫秒：QSlider的取值是int，微秒在约35分钟处溢出
        m_playbackBar->setMaximum(static_cast<int>(duration / 1000));
        m_playbackBar->setValue(0);
        m_durationStr = timeToStr(duration);
        m_timeLabel->setText(QString("0:00/%1").arg(m_durationStr));
    }
}

void MainWindow::createCentralWidget()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    m_videoRenderer = new QRhiVideoRenderer(central);

    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoRenderer, 1);

    auto* bar1 = new QHBoxLayout(central);
    bar1->setSpacing(5);
    bar1->setContentsMargins(5, 5, 5, 5);
    m_playbackBar = new PlaybackSlider(Qt::Horizontal, central);
    m_playbackBar->setEnabled(false);
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
    m_timeLabel = new QLabel("00:00", central);
    bar2->addWidget(m_timeLabel);

    connect(m_videoRenderer, &QRhiVideoRenderer::initialized, this, &MainWindow::onVideoRendererInitialized, Qt::QueuedConnection);
    connect(m_playButton, &PlayOrPauseButton::clicked, this, &MainWindow::onPlayOrPauseButtonClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStopButtonClicked);

    // 拖动进度条：拖动中仅预览位置与时间，松手后才seek
    connect(m_playbackBar, &QSlider::sliderPressed, this, [this] {
        m_seeking = true;
        previewSeekPosition(m_playbackBar->value());
    });
    connect(m_playbackBar, &QSlider::sliderMoved, this, [this](int value) {
        previewSeekPosition(value);
    });
    connect(m_playbackBar, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        m_controller->seek(static_cast<int64_t>(m_playbackBar->value()) * 1000);
    });
}

void MainWindow::createMenu()
{
    auto* fileMenu = menuBar()->addMenu(tr("File"));
    auto* openAct = fileMenu->addAction(tr("Open"));
    auto* exitAct = fileMenu->addAction(tr("Exit"));

    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    connect(exitAct, &QAction::triggered, this, &QMainWindow::close);
}

void MainWindow::onPlayStateChanged(Controller::State state)
{
    qDebug() << "State changed" << static_cast<int>(state);
    switch (state) {
    case Controller::State::Playing:
        m_playButton->setPlaying(true);
        m_playbackBar->setEnabled(true);
        break;
    case Controller::State::Paused:
        m_playButton->setPlaying(false);
        break;
    case Controller::State::Stopped:
        m_playButton->setPlaying(false);
        m_playbackBar->setEnabled(false);
        m_playbackBar->setValue(0);
        break;
    }
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

void MainWindow::onPlayOrPauseButtonClicked()
{
    m_controller->pauseOrResume();
}

void MainWindow::onStopButtonClicked()
{
    m_controller->stop();
    // 主动停止的，清屏
    m_videoRenderer->clear();
}

void MainWindow::onPositionChanged(int64_t pos)
{
    if (m_seeking) {
        // 拖动中不接受播放位置回写，避免滑块来回跳动
        return;
    }
    m_playbackBar->setValue(static_cast<int>(pos / 1000));
    m_timeLabel->setText(QString("%1/%2").arg(timeToStr(pos)).arg(m_durationStr));
}

void MainWindow::previewSeekPosition(int valueMs)
{
    m_timeLabel->setText(QString("%1/%2").arg(timeToStr(static_cast<int64_t>(valueMs) * 1000)).arg(m_durationStr));
}

} // namespace view
