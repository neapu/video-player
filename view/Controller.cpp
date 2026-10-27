#include "Controller.h"

namespace view {

Controller::Controller(QObject* parent) : QObject(parent) {}

Controller::~Controller() {}

void Controller::openFile(const QString& filePath)
{
    if (state() != State::Stopped) {
        stop();
    }

    using namespace media;
    IMediaSource::Options opts;
}

void Controller::pauseOrResume()
{
    // Implement pause or resume logic here
}

void Controller::stop()
{
    // Implement stop logic here
}

void Controller::setState(State state)
{
    QMutexLocker lock(&m_stateMutex);
    m_state = state;
}

Controller::State Controller::state() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_state;
}

} // namespace view
