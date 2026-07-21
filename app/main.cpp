#include "MainWindow.h"
#include <QApplication>
#include <iostream>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();
    
    // Initialization is now fully handled inside MainWindow

    return app.exec();
}
