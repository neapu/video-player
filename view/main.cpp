#include <QApplication>
#include "MainWindow.h"
#include "LogManager.h"

int main(int argc, char *argv[])
{
    view::LogManager::installMessageHandler();
    QApplication app(argc, argv);
    view::MainWindow window;
    window.show();
    int ret = app.exec();
    view::LogManager::uninstallMessageHandler();
    return ret;
}