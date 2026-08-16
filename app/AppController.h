#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "LangConfig.h"
#include "LivePipelineWorker.h"
#include "AudioPlayer.h"

// QML-facing application controller: owns the pipeline lifecycle state machine
// (model download -> engine startup) and exposes observable state that the
// Qt Quick shell binds to. This is the Qt Widgets MainWindow equivalent,
// reworked so the same logic serves desktop and mobile shells.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString statusColor READ statusColor NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
    Q_PROPERTY(QStringList languageNames READ languageNames CONSTANT)
    Q_PROPERTY(QString currentLanguageName READ currentLanguageName NOTIFY currentLanguageChanged)
    Q_PROPERTY(QString translationLabel READ translationLabel NOTIFY currentLanguageChanged)
    Q_PROPERTY(QStringList modelSizes READ modelSizes CONSTANT)
    Q_PROPERTY(QString currentModelSize READ currentModelSize NOTIFY currentModelChanged)
    Q_PROPERTY(QString keyterms READ keyterms WRITE setKeyterms NOTIFY keytermsChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    QString statusText() const { return m_statusText; }
    QString statusColor() const { return m_statusColor; }
    bool ready() const { return m_ready; }
    bool recording() const { return m_recording; }
    QStringList languageNames() const;
    QString currentLanguageName() const { return m_currentLang.name; }
    QString translationLabel() const { return m_currentLang.name + " (Translation)"; }
    QStringList modelSizes() const { return {"tiny", "small", "medium"}; }
    QString currentModelSize() const { return m_currentModelSize; }
    QString keyterms() const { return m_keyterms; }

    void setRecording(bool rec);
    void setKeyterms(const QString& terms);

    Q_INVOKABLE void selectLanguage(const QString& name);
    Q_INVOKABLE void selectModelSize(const QString& size);

signals:
    void statusChanged();
    void readyChanged();
    void recordingChanged();
    void currentLanguageChanged();
    void currentModelChanged();
    void keytermsChanged();
    void recordingToggled(bool isRecording);
    void transcriptReady(const QString& original, const QString& translated, const QString& execTime);
    void pipelineErrorOccurred(const QString& message);

private slots:
    void checkAndStartPipeline();

private:
    void setStatus(const QString& text, const QString& color, bool ready);
    void reflectRecordingState();

    QList<LangConfig> m_supportedLanguages;
    LangConfig m_currentLang;
    QString m_currentModelSize;
    QString m_keyterms;

    QString m_statusText;
    QString m_statusColor = "#555555";
    bool m_ready = false;
    bool m_recording = true;

    LivePipelineWorker* m_worker = nullptr;
    AudioPlayer* m_player = nullptr;
};
