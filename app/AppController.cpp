#include "AppController.h"
#include "ModelDownloader.h"
#include "AppPaths.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

// [SETBACK & FIX]: Qt defines 'emit' globally, which breaks Moonshine's
// Stream::emit(). Include the C++ wrapper with the macro undefined, then
// restore Qt's (empty) definition afterwards.
#undef emit
#include "moonshine-cpp.h"
#define emit

AppController::AppController(QObject* parent) : QObject(parent) {
    // German and French use community int8 conversions of the same opus-mt
    // models: a ~72 MB download instead of ~300 MB with negligible quality
    // change, which matters for mobile. The other languages have no hosted
    // int8 build yet (see docs/int8-model-hosting.md).
    m_supportedLanguages = {
        {"French", "craftwise/ct2-opus-mt-en-fr-int8", "piper_fr_FR-upmc-medium", "fr"},
        {"German", "cstr/opus-mt-en-de-ct2-int8", "piper_de_DE-thorsten-high", "de"},
        {"Spanish", "michaelfeil/ct2fast-opus-mt-en-es", "piper_es_ES-davefx-medium", "es"},
        {"Italian", "ooeoeo/opus-mt-en-it-ct2-float16", "piper_it_IT-paola-medium", "it"},
        {"Portuguese", "ooeoeo/opus-mt-tc-big-en-pt-ct2-float16", "piper_pt_BR-faber-medium", "pt"},
        {"Russian", "ooeoeo/opus-mt-en-ru-ct2-float16", "piper_ru_RU-dmitri-medium", "ru"}
    };

    QSettings settings("Moonslate", "LiveTranslator");
    const QString savedLang = settings.value("currentLanguage", "German").toString();
    const QString savedMoon = settings.value("currentMoonshineModel", "tiny").toString();
    m_keyterms = settings.value("keyterms", "").toString();

    m_currentLang = m_supportedLanguages[1]; // default to German
    for (const auto& lang : m_supportedLanguages) {
        if (lang.name == savedLang) m_currentLang = lang;
    }
    m_currentModelSize = modelSizes().contains(savedMoon) ? savedMoon : "tiny";

    m_player = new AudioPlayer();
    // The QML load check in --selftest instantiates this type; starting model
    // downloads or audio capture from a headless smoke test is not useful.
    if (!qEnvironmentVariableIsSet("MOONSLATE_SELFTEST")) {
        checkAndStartPipeline();
    }
}

QStringList AppController::languageNames() const {
    QStringList names;
    for (const auto& lang : m_supportedLanguages) names.append(lang.name);
    return names;
}

void AppController::setStatus(const QString& text, const QString& color, bool ready) {
    if (m_statusText != text || m_statusColor != color) {
        m_statusText = text;
        m_statusColor = color;
        emit statusChanged();
    }
    if (m_ready != ready) {
        m_ready = ready;
        emit readyChanged();
    }
}

void AppController::reflectRecordingState() {
    setStatus(m_recording ? "Recording: ON" : "Recording: OFF",
              m_recording ? "#4CAF50" : "#F44336", true);
}

void AppController::setRecording(bool rec) {
    if (!m_ready || m_recording == rec) return;
    m_recording = rec;
    emit recordingChanged();
    reflectRecordingState();
    emit recordingToggled(rec);
}

void AppController::setKeyterms(const QString& terms) {
    const QString normalized = terms.simplified();
    if (normalized == m_keyterms) return;
    m_keyterms = normalized;
    emit keytermsChanged();
    QSettings settings("Moonslate", "LiveTranslator");
    settings.setValue("keyterms", m_keyterms);
    checkAndStartPipeline();
}

void AppController::selectLanguage(const QString& name) {
    for (const auto& lang : m_supportedLanguages) {
        if (lang.name == name && lang.name != m_currentLang.name) {
            m_currentLang = lang;
            emit currentLanguageChanged();
            QSettings settings("Moonslate", "LiveTranslator");
            settings.setValue("currentLanguage", lang.name);
            checkAndStartPipeline();
            return;
        }
    }
}

void AppController::selectModelSize(const QString& size) {
    if (!modelSizes().contains(size) || size == m_currentModelSize) return;
    m_currentModelSize = size;
    emit currentModelChanged();
    QSettings settings("Moonslate", "LiveTranslator");
    settings.setValue("currentMoonshineModel", size);
    checkAndStartPipeline();
}

void AppController::checkAndStartPipeline() {
    // -------------------------------------------------------------------------
    // ARCHITECTURE: Pipeline Lifecycle Management
    //
    // State 1: Tear down any existing background pipeline worker.
    // State 2: Download the required Moonshine (STT) model if missing.
    // State 3: Download the required CTranslate2 (MT) model if missing.
    // State 3.5: Download the TTS voice + G2P assets if missing.
    // State 4: Start the LivePipelineWorker.
    //
    // Each download's finished signal recurses into this function, advancing
    // to the next state; the QML status area observes the transitions.
    // -------------------------------------------------------------------------

    setStatus("Loading...", "#555555", false);

    if (m_worker) {
        m_worker->quit();
        m_worker->wait();
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    emit pipelineErrorOccurred(QString()); // clear any previous error
    const QString moonDir = AppPaths::modelsRoot() + "/moonshine-" + m_currentModelSize;
    // Cache directory per repo, not per language: French and German now point
    // at int8 repos, and a language-keyed directory would keep serving the
    // previously downloaded fp32 weights instead of the new quantized ones.
    const QString ct2Dir = AppPaths::modelsRoot() + "/" + m_currentLang.opusRepo.section('/', -1);

    // [STATE 2] Validate Moonshine STT Model
    if (!QDir(moonDir).exists() || QDir(moonDir).isEmpty()) {
        setStatus("Downloading Moonshine " + m_currentModelSize + "...", "#555555", false);
        ModelDownloader* downloader = new ModelDownloader(this);
        connect(downloader, &ModelDownloader::downloadProgress, this, [this](const QString& file, qint64 received, qint64 total) {
            if (total > 0) {
                setStatus(QString("Downloading %1... %2%").arg(file).arg(received * 100 / total), "#555555", false);
            } else {
                setStatus("Downloading " + file + "...", "#555555", false);
            }
        });
        connect(downloader, &ModelDownloader::downloadFinished, [this, downloader]() {
            downloader->deleteLater();
            checkAndStartPipeline(); // Recurse to check the next model
        });
        connect(downloader, &ModelDownloader::errorOccurred, [this, downloader](const QString& err) {
            setStatus("Download error", "#F44336", false);
            emit pipelineErrorOccurred(err);
            downloader->deleteLater();
        });
        downloader->downloadMoonshineModel(m_currentModelSize, moonDir);
        return;
    }

    // [STATE 3] Validate CTranslate2 Translation Model
    if (!QDir(ct2Dir).exists() || QDir(ct2Dir).isEmpty()) {
        setStatus("Downloading " + m_currentLang.name + "...", "#555555", false);
        ModelDownloader* downloader = new ModelDownloader(this);
        connect(downloader, &ModelDownloader::downloadProgress, this, [this](const QString& file, qint64 received, qint64 total) {
            if (total > 0) {
                setStatus(QString("Downloading %1... %2%").arg(file).arg(received * 100 / total), "#555555", false);
            } else {
                setStatus("Downloading " + file + "...", "#555555", false);
            }
        });
        connect(downloader, &ModelDownloader::downloadFinished, [this, downloader]() {
            downloader->deleteLater();
            checkAndStartPipeline(); // Recurse to actually start
        });
        connect(downloader, &ModelDownloader::errorOccurred, [this, downloader](const QString& err) {
            setStatus("Download error", "#F44336", false);
            emit pipelineErrorOccurred(err);
            downloader->deleteLater();
        });
        downloader->downloadModel(m_currentLang.opusRepo, ct2Dir);
        return;
    }

    // [STATE 3.5] Validate TTS voice + G2P assets
    const QString g2pRoot = AppPaths::ttsDataRoot();
    try {
        const std::string depsJson = moonshine::TextToSpeech::getDependencies(
            m_currentLang.langCode.toStdString(),
            {{"g2p_root", g2pRoot.toStdString()},
             {"voice", m_currentLang.piperVoice.toStdString()}});
        QList<QPair<QString, QString>> missing;
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(depsJson));
        for (const QJsonValue& groupVal : doc.object().value("groups").toArray()) {
            for (const QJsonValue& fileVal : groupVal.toObject().value("files").toArray()) {
                const QJsonObject file = fileVal.toObject();
                if (!QFile::exists(g2pRoot + "/" + file.value("name").toString())) {
                    missing.append(qMakePair(file.value("url").toString(),
                                             g2pRoot + "/" + file.value("name").toString()));
                }
            }
        }
        if (!missing.isEmpty()) {
            setStatus("Downloading TTS voice...", "#555555", false);
            ModelDownloader* downloader = new ModelDownloader(this);
            connect(downloader, &ModelDownloader::downloadProgress, this, [this](const QString& file, qint64 received, qint64 total) {
                if (total > 0) {
                    setStatus(QString("Downloading %1... %2%").arg(file).arg(received * 100 / total), "#555555", false);
                } else {
                    setStatus("Downloading " + file + "...", "#555555", false);
                }
            });
            connect(downloader, &ModelDownloader::downloadFinished, [this, downloader]() {
                downloader->deleteLater();
                checkAndStartPipeline(); // Recurse to actually start
            });
            connect(downloader, &ModelDownloader::errorOccurred, [this, downloader](const QString& err) {
                setStatus("Download error", "#F44336", false);
                emit pipelineErrorOccurred(err);
                downloader->deleteLater();
            });
            downloader->downloadFileList(missing);
            return;
        }
    } catch (const std::exception& e) {
        setStatus("TTS manifest error", "#F44336", false);
        emit pipelineErrorOccurred(QString::fromUtf8(e.what()));
        return;
    }

    // [STATE 4] Start Background Pipeline Execution
    m_worker = new LivePipelineWorker(moonDir, ct2Dir, m_currentLang.piperVoice, m_currentLang.langCode,
                                      m_currentModelSize, m_keyterms, g2pRoot);
    connect(this, &AppController::recordingToggled, m_worker, &LivePipelineWorker::setRecording);
    connect(m_worker, &LivePipelineWorker::transcriptUpdated, this, &AppController::transcriptReady);
    connect(m_worker, &LivePipelineWorker::chunkReady, m_player, &AudioPlayer::onChunkReady, Qt::QueuedConnection);
    connect(m_worker, &LivePipelineWorker::pipelineReady, this, [this]() {
        reflectRecordingState();
        if (m_worker) m_worker->setRecording(m_recording);
    });
    connect(m_worker, &LivePipelineWorker::pipelineError, this, [this](const QString& message) {
        setStatus("Pipeline error", "#F44336", false);
        emit pipelineErrorOccurred(message);
    });
    m_worker->start();
}
