#include "MainWindow.h"
#include "AppPaths.h"

#include <QApplication>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QCommandLineParser>
#include <iostream>

// [SETBACK & FIX]: Qt defines 'emit' globally, which breaks Moonshine's
// Stream::emit(). Include the C++ wrapper with the macro undefined, then
// restore Qt's (empty) definition afterwards.
#undef emit
#include "moonshine-cpp.h"
#define emit

// Headless smoke test for CI: proves the binary starts, all shared libraries
// resolve (a missing Qt plugin / onnxruntime / moonshine library fails the
// process at startup or on the first call), the asset directories are
// creatable/writable, and libmoonshine answers a catalog query. Runs without
// a display, microphone, or downloaded models.
static int runSelfTest() {
    bool ok = true;

    std::cout << "selftest: qt " << QT_VERSION_STR << std::endl;
    std::cout << "selftest: exe dir " << QCoreApplication::applicationDirPath().toStdString() << std::endl;

    const QString modelsRoot = AppPaths::modelsRoot();
    const QString ttsDataRoot = AppPaths::ttsDataRoot();
    std::cout << "selftest: models root " << modelsRoot.toStdString() << std::endl;
    std::cout << "selftest: tts data root " << ttsDataRoot.toStdString() << std::endl;
    for (const QString& dir : {modelsRoot, ttsDataRoot}) {
        if (!QDir().mkpath(dir) || !QFileInfo(dir).isWritable()) {
            std::cerr << "selftest: FAIL cannot create/write " << dir.toStdString() << std::endl;
            ok = false;
        }
    }

    int inputs = 0;
    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        ++inputs;
        std::cout << "selftest: audio input " << device.id().toStdString() << std::endl;
    }
    std::cout << "selftest: " << inputs << " audio input(s)" << std::endl;

    try {
        const std::string catalog = moonshine::Transcriber::getCatalog();
        std::cout << "selftest: moonshine catalog " << catalog.size() << " bytes" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "selftest: FAIL moonshine catalog: " << e.what() << std::endl;
        ok = false;
    }

    std::cout << (ok ? "selftest: OK" : "selftest: FAILED") << std::endl;
    return ok ? 0 : 1;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    const QCommandLineOption selftestOption(QStringList{"selftest"}, "run headless diagnostics and exit");
    parser.addOption(selftestOption);
    parser.process(app);
    if (parser.isSet(selftestOption)) {
        return runSelfTest();
    }

    MainWindow window;
    window.show();

    // Initialization is now fully handled inside MainWindow

    return app.exec();
}
