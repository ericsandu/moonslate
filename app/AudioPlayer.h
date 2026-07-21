#pragma once

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>

class AudioPlayer : public QObject {
    Q_OBJECT
    QAudioSink* sink = nullptr;
    QIODevice* device = nullptr;
    QByteArray buffer;

    void tryWriteMore();

public:
    ~AudioPlayer();

public slots:
    void onChunkReady(const QByteArray& pcmData, int sampleRate);
    void onFinished();
};
