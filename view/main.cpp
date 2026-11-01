#include <QApplication>
#include "MainWindow.h"
#include "LogManager.h"

int main(int argc, char *argv[])
{
    QString filePath{};
    if (argc > 1) {
        filePath = argv[1];
    }

    view::LogManager::installMessageHandler();
    QApplication app(argc, argv);
    view::MainWindow window;
    window.show();
    if (!filePath.isEmpty()) {
        window.openFile(filePath);
    }
    int ret = app.exec();
    view::LogManager::uninstallMessageHandler();
    return ret;
}