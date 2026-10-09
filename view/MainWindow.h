#pragma once
#include <QMainWindow>

namespace view {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
};
} // namespace view