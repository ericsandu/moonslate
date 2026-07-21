#include <iostream>
#include <fstream>
#include <chrono>

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include "moonshine-cpp.h"

#include <QCoreApplication>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QThread>
#include <QDebug>
#include <QByteArray>
#include <QTimer>
#include <atomic>
#include <cmath>

#include "ModelDownloader.h"

// Reused WAV parsing utility
bool load_wav_data(const char *path, float **out_float_data, size_t *out_num_samples, int32_t *out_sample_rate) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    char riff_header[4];
    if (std::fread(riff_header, 1, 4, file) != 4 || std::strncmp(riff_header, "RIFF", 4) != 0) { std::fclose(file); return false; }
    std::fseek(file, 4, SEEK_CUR);
    char wave_header[4];
    if (std::fread(wave_header, 1, 4, file) != 4 || std::strncmp(wave_header, "WAVE", 4) != 0) { std::fclose(file); return false; }
    char chunk_id[4];
    uint32_t chunk_size = 0;
    bool found_fmt = false;
    while (std::fread(chunk_id, 1, 4, file) == 4) {
        if (std::fread(&chunk_size, 4, 1, file) != 1) break;
        if (std::strncmp(chunk_id, "fmt ", 4) == 0) { found_fmt = true; break; }
        std::fseek(file, chunk_size, SEEK_CUR);
    }
    if (!found_fmt) { std::fclose(file); return false; }
    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0, byte_rate = 0;
    uint16_t block_align = 0;
    if (std::fread(&audio_format, sizeof(uint16_t), 1, file) != 1 ||
        std::fread(&num_channels, sizeof(uint16_t), 1, file) != 1 ||
        std::fread(&sample_rate, sizeof(uint32_t), 1, file) != 1 ||
        std::fread(&byte_rate, sizeof(uint32_t), 1, file) != 1 ||
        std::fread(&block_align, sizeof(uint16_t), 1, file) != 1 ||
        std::fread(&bits_per_sample, sizeof(uint16_t), 1, file) != 1) { std::fclose(file); return false; }
    if (chunk_size > 16) std::fseek(file, chunk_size - 16, SEEK_CUR);
    bool found_data = false;
    while (std::fread(chunk_id, 1, 4, file) == 4) {
        if (std::fread(&chunk_size, 4, 1, file) != 1) break;
        if (std::strncmp(chunk_id, "data", 4) == 0) { found_data = true; break; }
        std::fseek(file, chunk_size, SEEK_CUR);
    }
    if (!found_data) { std::fclose(file); return false; }
    size_t num_samples = chunk_size / (bits_per_sample / 8);
    float *result_data = (float *)malloc(num_samples * sizeof(float));
    for (size_t i = 0; i < num_samples; ++i) {
        int16_t sample = 0;
        if (std::fread(&sample, sizeof(int16_t), 1, file) != 1) break;
        result_data[i] = static_cast<float>(sample) / 32768.0f;
    }
    std::fclose(file);
    *out_float_data = result_data;
    *out_num_samples = num_samples;
    if (out_sample_rate != nullptr) *out_sample_rate = sample_rate;
    return true;
}

class AudioPlayer : public QObject {
    Q_OBJECT
    QAudioSink* sink = nullptr;
    QIODevice* device = nullptr;
    QByteArray buffer;

    void tryWriteMore() {
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

public:
    ~AudioPlayer() { if (sink) delete sink; }

public slots:
    void onChunkReady(const QByteArray& pcmData, int sampleRate) {
        if (!sink) {
            QAudioFormat format;
            format.setSampleRate(sampleRate);
            format.setChannelCount(1);
            format.setSampleFormat(QAudioFormat::Int16);
            
            sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), format, this);
            
            // Allocate a massive internal buffer for Qt to minimize dropouts (5 seconds worth)
            sink->setBufferSize(sampleRate * 2 * 5); 
            
            device = sink->start();

            // Whenever the hardware finishes playing a chunk and frees up space, pump more data
            connect(device, &QIODevice::bytesWritten, this, [this](qint64) {
                tryWriteMore();
            });
        }
        
        // Append all the freshly synthesized PCM floats into our unlimited background queue
        buffer.append(pcmData);
        
        // Try shoving as much of the queue as possible into the hardware buffer right now
        tryWriteMore();
    }
    
    void onFinished() {
        // Wait longer (15 seconds) so the hardware can finish draining our custom buffer
        QTimer::singleShot(15000, qApp, &QCoreApplication::quit);
    }
};

class PipelineWorker : public QThread {
    Q_OBJECT
public:
    QString audioPath;
    QString moonPath;
    QString ct2Path;

    PipelineWorker(QString a, QString m, QString c) : audioPath(a), moonPath(m), ct2Path(c) {}

signals:
    void chunkReady(const QByteArray& pcmData, int sampleRate);

protected:
    void run() override {
        std::cout << "[1] Running Voice-To-Text Transcription via Moonshine..." << std::endl;
        
        float *wav_data = nullptr;
        size_t wav_data_size = 0;
        int32_t wav_sample_rate = 0;
        if (!load_wav_data(audioPath.toUtf8().constData(), &wav_data, &wav_data_size, &wav_sample_rate)) {
            std::cerr << "Failed to load WAV file!" << std::endl;
            return;
        }
        std::vector<float> audio_samples(wav_data, wav_data + wav_data_size);
        free(wav_data);

        moonshine::Transcriber transcriber(moonPath.toUtf8().constData(), moonshine::ModelArch::TINY);
        moonshine::Transcript transcript = transcriber.transcribeWithoutStreaming(audio_samples, wav_sample_rate, 0);

        std::cout << "[2] Initializing Machine Translation Engine..." << std::endl;
        sentencepiece::SentencePieceProcessor source_spm;
        sentencepiece::SentencePieceProcessor target_spm;
        source_spm.Load((ct2Path + "/source.spm").toUtf8().constData());
        target_spm.Load((ct2Path + "/target.spm").toUtf8().constData());

        ctranslate2::ReplicaPoolConfig config;
        config.num_threads_per_replica = 0; 
        ctranslate2::Translator translator(ct2Path.toUtf8().constData(), ctranslate2::Device::CPU, ctranslate2::ComputeType::DEFAULT, {0}, false, config);

        std::cout << "[3] Initializing Piper TTS Engine (German Neutral)..." << std::endl;
        std::vector<moonshine_option_t> piper_options = {
            {"g2p_root", "../../moonshine/core/moonshine-tts/data"},
            {"voice", "piper_de_DE-thorsten-medium"},
        };
        moonshine::TextToSpeech tts_piper("de", piper_options);

        std::cout << "\n--- Streaming Audio Playback Queue Started ---\n" << std::endl;

        for (size_t i = 0; i < transcript.lines.size(); ++i) {
            const auto& line = transcript.lines[i];
            if (line.text.empty()) continue;
            
            std::cout << "[Chunk " << (i + 1) << "]" << std::endl;
            std::cout << "  Original   : " << line.text << std::endl;
            
            std::vector<std::string> tokens;
            source_spm.Encode(line.text, &tokens);
            tokens.push_back("</s>");
            
            auto start_time = std::chrono::high_resolution_clock::now();
            std::vector<ctranslate2::TranslationResult> results = translator.translate_batch({tokens});
            auto end_time = std::chrono::high_resolution_clock::now();
            int translation_latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            
            std::string output_text;
            target_spm.Decode(results[0].output(), &output_text);
            if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
                output_text = output_text.substr(0, output_text.size() - 4);
            }
            
            std::cout << "  Translated : " << output_text << std::endl;
            
            auto piper_start = std::chrono::high_resolution_clock::now();
            auto piper_result = tts_piper.synthesize(output_text);
            int piper_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - piper_start).count();
            
            std::cout << "  -> Latency : VTT(" << line.lastTranscriptionLatencyMs << "ms) + CT2(" << translation_latency_ms << "ms) + Piper(" << piper_ms << "ms) = " << (line.lastTranscriptionLatencyMs + translation_latency_ms + piper_ms) << "ms\n" << std::endl;

            // Convert to Int16
            QByteArray pcmData;
            pcmData.reserve(piper_result.samples.size() * sizeof(int16_t));
            for (float s : piper_result.samples) {
                float clamped = std::max(-1.0f, std::min(1.0f, s));
                int16_t val = static_cast<int16_t>(clamped * 32767.0f);
                pcmData.append(reinterpret_cast<const char*>(&val), sizeof(int16_t));
            }

            // Push to Qt Event Loop asynchronously!
            emit chunkReady(pcmData, piper_result.sampleRateHz);
        }
        
        std::cout << "[4] Finished processing translation stream." << std::endl;
    }
};



class LivePipelineWorker : public QThread {
    Q_OBJECT
public:
    QString moonPath;
    QString ct2Path;
    std::atomic<float> current_max_rms{0.0f};

    LivePipelineWorker(QString m, QString c) : moonPath(m), ct2Path(c) {}

signals:
    void chunkReady(const QByteArray& pcmData, int sampleRate);

protected:
    void run() override {
        std::cout << "[1] Initializing Live Voice-To-Text Transcription via Moonshine..." << std::endl;
        
        moonshine::Transcriber transcriber(moonPath.toUtf8().constData(), moonshine::ModelArch::TINY_STREAMING);

        std::cout << "[2] Initializing Machine Translation Engine..." << std::endl;
        sentencepiece::SentencePieceProcessor source_spm;
        sentencepiece::SentencePieceProcessor target_spm;
        source_spm.Load((ct2Path + "/source.spm").toUtf8().constData());
        target_spm.Load((ct2Path + "/target.spm").toUtf8().constData());

        ctranslate2::ReplicaPoolConfig config;
        config.num_threads_per_replica = 0; 
        ctranslate2::Translator translator(ct2Path.toUtf8().constData(), ctranslate2::Device::CPU, ctranslate2::ComputeType::DEFAULT, {0}, false, config);

        std::cout << "[3] Initializing Piper TTS Engine (German Neutral)..." << std::endl;
        std::vector<moonshine_option_t> piper_options = {
            {"g2p_root", "../../moonshine/core/moonshine-tts/data"},
            {"voice", "piper_de_DE-thorsten-medium"},
        };
        moonshine::TextToSpeech tts_piper("de", piper_options);

        // We use a dedicated thread for translation/TTS to not block the audio capture thread, 
        // but for now we'll do it sequentially in this worker thread using Moonshine's stream API.
        
        // However, QAudioSource needs an event loop. So we'll create it here.
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioSource* source = new QAudioSource(QMediaDevices::defaultAudioInput(), format);
        source->setBufferSize(16000 * 2 * 2); // 2 seconds

        moonshine::Stream stream = transcriber.createStream(1.0, 0); // 1.0 second update interval
        
        stream.addListener([&](const moonshine::TranscriptEvent& ev) {
            if (ev.type != moonshine::TranscriptEvent::LINE_COMPLETED && 
                ev.type != moonshine::TranscriptEvent::LINE_UPDATED) {
                return;
            }
            
            // Only process lines when they are complete for translation to avoid stuttering/interruptions
            if (!ev.line.isComplete) {
                return;
            }
            float rms = current_max_rms.exchange(0.0f);
            if (rms < 0.10f) { // Increased RMS threshold to filter silence/hallucinations
                std::cout << "[Silence Ignored] (RMS: " << rms << ") Text: " << ev.line.text << std::endl;
                return;
            }
            
            std::cout << "[Line] (RMS: " << rms << ") " << ev.line.text << std::endl;
            
            std::vector<std::string> tokens;
            source_spm.Encode(ev.line.text, &tokens);
            tokens.push_back("</s>");
            
            std::vector<ctranslate2::TranslationResult> results = translator.translate_batch({tokens});
            std::string output_text;
            target_spm.Decode(results[0].output(), &output_text);
            if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
                output_text = output_text.substr(0, output_text.size() - 4);
            }
            
            std::cout << "  Translated : " << output_text << std::endl;
            
            auto piper_result = tts_piper.synthesize(output_text);
            
            QByteArray pcmData;
            pcmData.reserve(piper_result.samples.size() * sizeof(int16_t));
            for (float s : piper_result.samples) {
                float clamped = std::max(-1.0f, std::min(1.0f, s));
                int16_t val = static_cast<int16_t>(clamped * 32767.0f);
                pcmData.append(reinterpret_cast<const char*>(&val), sizeof(int16_t));
            }

            emit chunkReady(pcmData, piper_result.sampleRateHz);
        });

        QIODevice* io = source->start();
        if (!io) {
            std::cerr << "Failed to open QAudioSource!" << std::endl;
            return;
        }

        std::cout << "\n--- Live Microphone Streaming Started ---\n" << std::endl;
        stream.start();

        connect(io, &QIODevice::readyRead, this, [&]() {
            QByteArray raw = io->readAll();
            if (raw.isEmpty()) return;

            const int16_t* ptr = reinterpret_cast<const int16_t*>(raw.constData());
            size_t count = raw.size() / sizeof(int16_t);
            std::vector<float> floats(count);
            float sum_sq = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                float val = ptr[i] / 32768.0f;
                floats[i] = val;
                sum_sq += val * val;
            }
            
            if (count > 0) {
                float rms = std::sqrt(sum_sq / count);
                float current = current_max_rms.load();
                while (rms > current && !current_max_rms.compare_exchange_weak(current, rms)) {}
            }

            stream.addAudio(floats, 16000);
        });

        // Run the event loop for QAudioSource
        exec();

        stream.stop();
        source->stop();
        delete source;
    }
};

// Required for Qt to link moc properly in a single file
#include "integration_qt.moc"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc == 4 && std::string(argv[1]) == "download") {
        QString repoId = argv[2];
        QString destDir = argv[3];
        ModelDownloader* downloader = new ModelDownloader(&app);
        
        QObject::connect(downloader, &ModelDownloader::downloadProgress, [](const QString& filename, qint64 bytesReceived, qint64 bytesTotal) {
            std::cout << "\rDownloading " << filename.toStdString() << ": " << bytesReceived << " / " << bytesTotal << " bytes" << std::flush;
        });
        
        QObject::connect(downloader, &ModelDownloader::fileDownloaded, [](const QString& filename) {
            std::cout << "\nFinished downloading " << filename.toStdString() << std::endl;
        });

        QObject::connect(downloader, &ModelDownloader::errorOccurred, [](const QString& errorString) {
            std::cerr << "\nError: " << errorString.toStdString() << std::endl;
        });

        QObject::connect(downloader, &ModelDownloader::downloadFinished, [&app]() {
            std::cout << "All downloads completed successfully!" << std::endl;
            app.quit();
        });

        std::cout << "Starting download of " << repoId.toStdString() << " to " << destDir.toStdString() << std::endl;
        downloader->downloadModel(repoId, destDir);

        return app.exec();
    }

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <audio.wav> <moonshine_model_dir> <ct2_model_dir>\n";
        std::cerr << "       " << argv[0] << " live <moonshine_model_dir> <ct2_model_dir>\n";
        std::cerr << "       " << argv[0] << " download <repo_id> <dest_dir>\n";
        return 1;
    }

    if (std::string(argv[1]) == "live") {
        LivePipelineWorker* worker = new LivePipelineWorker(argv[2], argv[3]);
        AudioPlayer player;

        QObject::connect(worker, &LivePipelineWorker::chunkReady, &player, &AudioPlayer::onChunkReady, Qt::QueuedConnection);
        
        worker->start();
        return app.exec();
    }

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <audio.wav> <moonshine_model_dir> <ct2_model_dir>\n";
        return 1;
    }

    PipelineWorker worker(argv[1], argv[2], argv[3]);
    AudioPlayer player;

    QObject::connect(&worker, &PipelineWorker::chunkReady, &player, &AudioPlayer::onChunkReady, Qt::QueuedConnection);
    QObject::connect(&worker, &PipelineWorker::finished, &player, &AudioPlayer::onFinished);

    worker.start();

    return app.exec();
}
