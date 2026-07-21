#pragma once

#include <QThread>
#include <QString>
#include <QByteArray>
#include <atomic>

class LivePipelineWorker : public QThread {
    Q_OBJECT
public:
    QString moonPath;
    QString ct2Path;
    std::atomic<float> current_max_vad{0.0f};
    std::atomic<long long> ignore_audio_until_ms{0};
    std::atomic<bool> is_recording{true};
    QString piperVoice;
    QString langCode;

    LivePipelineWorker(QString m, QString c, QString pv, QString lc);

public slots:
    void setRecording(bool rec);

signals:
    void chunkReady(const QByteArray& pcmData, int sampleRate);
    void transcriptUpdated(const QString& original, const QString& translated, const QString& execTime);
    void pipelineReady();

protected:
    void run() override;
};
