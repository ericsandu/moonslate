#include "AppController.h"
#include "AppPaths.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
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

static void registerQmlTypes() {
    qmlRegisterType<AppController>("Moonslate", 1, 0, "AppController");
}

// Headless smoke test for CI: proves the binary starts, all shared libraries
// resolve (a missing Qt plugin / onnxruntime / moonshine library fails the
// process at startup or on the first call), the asset directories are
// creatable/writable, libmoonshine answers a catalog query, and the QML shell
// (software renderer) instantiates without errors. Runs without a display,
// microphone, or downloaded models.
static int runSelfTest() {
    qputenv("MOONSLATE_SELFTEST", "1"); // AppController must not start downloads
    qputenv("QT_QUICK_BACKEND", "software");
    bool ok = true;

    std::cout << "selftest: qt " << QT_VERSION_STR << std::endl;
    std::cout << "selftest: exe dir " << QGuiApplication::applicationDirPath().toStdString() << std::endl;

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

    registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "selftest: FAIL QML shell did not load" << std::endl;
        ok = false;
    } else {
        std::cout << "selftest: qml shell loaded" << std::endl;
    }

    std::cout << (ok ? "selftest: OK" : "selftest: FAILED") << std::endl;
    return ok ? 0 : 1;
}

int main(int argc, char *argv[]) {
    // --selftest must be parsed before QGuiApplication forces "software" on us
    // only where needed; QCommandLineParser still needs the app instance.
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    const QCommandLineOption selftestOption(QStringList{"selftest"}, "run headless diagnostics and exit");
    parser.addOption(selftestOption);
    parser.process(app);
    if (parser.isSet(selftestOption)) {
        return runSelfTest();
    }

    registerQmlTypes();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
