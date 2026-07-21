#include "MainWindow.h"
#include <QApplication>
#include <iostream>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "Live mode requires: <moonshine_model_dir>\n";
        return 1;
    }

    MainWindow window(argv[1]);
    window.show();
    
    LangConfig defaultLang = {"German", "michaelfeil/ct2fast-opus-mt-en-de", "piper_de_DE-thorsten-medium", "de"};
    window.switchLanguage(defaultLang);

    return app.exec();
}
