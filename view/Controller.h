#pragma once
#include <QObject>
#include <QString>

namespace view {
class Controller : public QObject {
    Q_OBJECT

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller();

    void openFile(const QString &filePath);
    void pauseOrResume();
    void stop();
};

} // namespace view