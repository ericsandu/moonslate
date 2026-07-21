#include "AudioPlayer.h"
#include <QAudioFormat>
#include <QMediaDevices>
#include <QTimer>
#include <QCoreApplication>

void AudioPlayer::tryWriteMore() {
    // [ARCHITECTURE: Manual PCM Buffering]: QAudioSink operates asynchronously and relies on a finite internal buffer. 
    // Since our TTS generates audio faster than real-time and in chunks, we cannot push it all at once or it drops bytes.
    // Instead, we maintain a custom `QByteArray buffer` and periodically write as much as the sink has room for (`bytesFree`).
    // The `bytesWritten` signal will trigger this method repeatedly until the buffer is flushed.
    if (!sink || !device || buffer.isEmpty()) return;
    int freeBytes = sink->bytesFree();
    if (freeBytes > 0) {
        int toWrite = std::min(freeBytes, static_cast<int>(buffer.size()));
        int written = device->write(buffer.constData(), toWrite);
        if (written > 0) {
            buffer.remove(0, written);
        }
    }
}

AudioPlayer::~AudioPlayer() { if (sink) delete sink; }

void AudioPlayer::onChunkReady(const QByteArray& pcmData, int sampleRate) {
    if (!sink) {
        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        
        sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
        sink->setBufferSize(sampleRate * 2 * 5); 
        
        device = sink->start();

        connect(device, &QIODevice::bytesWritten, this, [this](qint64) {
            tryWriteMore();
        });
    }
    
    buffer.append(pcmData);
    tryWriteMore();
}

void AudioPlayer::onFinished() {
    QTimer::singleShot(15000, qApp, &QCoreApplication::quit);
}
