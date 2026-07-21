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
#include <rnnoise.h>

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
            {"speed", "0.75"}, // Slow down the TTS speed slightly (1.0 is default)
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
    std::atomic<float> current_max_vad{0.0f};
    std::atomic<long long> ignore_audio_until_ms{0};

    LivePipelineWorker(QString m, QString c) : moonPath(m), ct2Path(c) {}

signals:
    void chunkReady(const QByteArray& pcmData, int sampleRate);

protected:
    float compute_cosine_similarity(const float* a, const float* b, size_t size) {
        float dot = 0.0f, magA = 0.0f, magB = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            dot += a[i] * b[i];
            magA += a[i] * a[i];
            magB += b[i] * b[i];
        }
        if (magA == 0.0f || magB == 0.0f) return 0.0f;
        return dot / (std::sqrt(magA) * std::sqrt(magB));
    }

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
        
        // QAudioSource needs an event loop. So we'll create it here.
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioSource* source = new QAudioSource(QMediaDevices::defaultAudioInput(), format);
        source->setBufferSize(16000 * 2 * 2); // 2 seconds

        moonshine::Stream stream = transcriber.createStream(1.0, 0); // 1.0 second update interval
        
        DenoiseState* rnnoise_st = rnnoise_create(NULL);
        std::vector<float> audio_buffer_16k;
        std::vector<float> rolling_audio_16k; // Holds the last 4 seconds of raw audio for fingerprinting
        std::mutex rolling_mutex;
        
        std::vector<float> user_profile_audio;
        QFile file("user_profile.wav");
        if (file.open(QIODevice::ReadOnly)) {
            file.seek(44); // skip WAV header
            QByteArray raw = file.readAll();
            const int16_t* ptr = reinterpret_cast<const int16_t*>(raw.constData());
            size_t count = raw.size() / sizeof(int16_t);
            for (size_t i = 0; i < count; ++i) {
                user_profile_audio.push_back(ptr[i] / 32768.0f);
            }
        }
        
        float* user_embedding = nullptr;
        uint64_t user_embedding_size = 0;
        if (!user_profile_audio.empty()) {
            std::cout << "[Profile] Extracting acoustic fingerprint for speaker verification..." << std::endl;
            if (moonshine_extract_speaker_embedding(user_profile_audio.data(), user_profile_audio.size(), 16000, &user_embedding, &user_embedding_size) != 0) {
                std::cerr << "[Warning] Failed to extract speaker embedding! Speaker verification will be disabled." << std::endl;
            } else {
                std::cout << "[Profile] Successfully enrolled user voice fingerprint! (Size: " << user_embedding_size << " dimensions)" << std::endl;
            }
        } else {
            std::cout << "[Warning] No user_profile.wav found. Speaker verification will be disabled." << std::endl;
        }

        stream.addListener([&](const moonshine::TranscriptEvent& ev) {
            if (ev.type != moonshine::TranscriptEvent::LINE_COMPLETED && 
                ev.type != moonshine::TranscriptEvent::LINE_UPDATED) {
                return;
            }
            
            // Only process lines when they are complete for translation to avoid stuttering/interruptions
            if (!ev.line.isComplete) {
                return;
            }
            float vad = current_max_vad.exchange(0.0f);
            if (vad < 0.6f || ev.line.text.empty()) { // VAD threshold to filter silence/hallucinations
                std::cout << "[Silence Ignored] (VAD: " << vad << ") Text: " << ev.line.text << std::endl;
                return;
            }
            
            if (user_embedding != nullptr) {
                float* live_embedding = nullptr;
                uint64_t live_size = 0;
                
                std::vector<float> local_audio;
                {
                    std::lock_guard<std::mutex> lock(rolling_mutex);
                    local_audio = rolling_audio_16k;
                }
                
                if (!local_audio.empty()) {
                    if (moonshine_extract_speaker_embedding(local_audio.data(), local_audio.size(), 16000, &live_embedding, &live_size) == 0) {
                        if (live_size == user_embedding_size) {
                            float similarity = compute_cosine_similarity(user_embedding, live_embedding, live_size);
                            if (similarity < 0.65f) { // User voice fingerprint threshold
                                std::cout << "[External Voice Rejected] (Similarity: " << similarity << ") Text: " << ev.line.text << std::endl;
                                moonshine_free_speaker_embedding(live_embedding);
                                return;
                            }
                            std::cout << "[Voice Match: " << similarity << "] ";
                        }
                        moonshine_free_speaker_embedding(live_embedding);
                    }
                }
            }
            
            std::cout << "[Line] (VAD: " << vad << ") " << ev.line.text << std::endl;
            
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
            
            int duration_ms = (piper_result.samples.size() * 1000) / piper_result.sampleRateHz;
            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();
            
            long long current_ignore = ignore_audio_until_ms.load();
            if (now < current_ignore) {
                ignore_audio_until_ms.store(current_ignore + duration_ms + 400); // Add 400ms buffer for speaker reverb
            } else {
                ignore_audio_until_ms.store(now + duration_ms + 400);
            }
            
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

            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();
            if (now < ignore_audio_until_ms.load()) {
                // Drop the audio chunk completely to prevent echo cancellation loops
                audio_buffer_16k.clear();
                return;
            }

            const int16_t* ptr = reinterpret_cast<const int16_t*>(raw.constData());
            size_t count = raw.size() / sizeof(int16_t);
            std::vector<float> out_16k_chunk;
            for (size_t i = 0; i < count; ++i) {
                audio_buffer_16k.push_back(static_cast<float>(ptr[i]));
            }

            while (audio_buffer_16k.size() >= 160) {
                float frame_in_48k[480];
                float frame_out_48k[480];
                for (int i = 0; i < 160; ++i) {
                    float val = audio_buffer_16k[i];
                    // Point upsampling
                    frame_in_48k[i*3]     = val;
                    frame_in_48k[i*3 + 1] = val;
                    frame_in_48k[i*3 + 2] = val;
                }
                audio_buffer_16k.erase(audio_buffer_16k.begin(), audio_buffer_16k.begin() + 160);
                
                float vad = rnnoise_process_frame(rnnoise_st, frame_out_48k, frame_in_48k);
                
                for (int i = 0; i < 160; ++i) {
                    // Moving average downsampling to prevent high-frequency aliasing artifacts
                    float val = (frame_out_48k[i*3] + frame_out_48k[i*3+1] + frame_out_48k[i*3+2]) / 3.0f;
                    float normalized = val / 32768.0f;
                    out_16k_chunk.push_back(normalized);
                    {
                        std::lock_guard<std::mutex> lock(rolling_mutex);
                        rolling_audio_16k.push_back(normalized);
                        // Keep only the last 4 seconds for speaker verification buffer
                        if (rolling_audio_16k.size() > 16000 * 4) {
                            rolling_audio_16k.erase(rolling_audio_16k.begin(), rolling_audio_16k.begin() + (rolling_audio_16k.size() - 16000 * 4));
                        }
                    }
                }
                
                float current = current_max_vad.load();
                while (vad > current && !current_max_vad.compare_exchange_weak(current, vad)) {}
            }

            if (!out_16k_chunk.empty()) {
                stream.addAudio(out_16k_chunk, 16000);
            }
        });

        // Run the event loop for QAudioSource
        exec();

        stream.stop();
        source->stop();
        delete source;
        rnnoise_destroy(rnnoise_st);
        if (user_embedding != nullptr) {
            moonshine_free_speaker_embedding(user_embedding);
        }
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

    if (argc > 1 && std::string(argv[1]) == "download_moonshine") {
        std::string modelName = "small-streaming-en";
        if (argc > 2) {
            modelName = argv[2];
        }

        ModelDownloader* downloader = new ModelDownloader(&app);
        
        QObject::connect(downloader, &ModelDownloader::downloadProgress, [](const QString& filename, qint64 bytesReceived, qint64 bytesTotal) {
            std::cout << "\rDownloading " << filename.toStdString() << "... " 
                      << bytesReceived << " / " << bytesTotal << " bytes" << std::flush;
        });
        
        QObject::connect(downloader, &ModelDownloader::fileDownloaded, [](const QString& filename) {
            std::cout << "\nFinished downloading " << filename.toStdString() << "!" << std::endl;
        });
        
        QObject::connect(downloader, &ModelDownloader::errorOccurred, [&](const QString& errorString) {
            std::cerr << "\nError: " << errorString.toStdString() << std::endl;
            app.quit();
        });
        
        QObject::connect(downloader, &ModelDownloader::downloadFinished, [&]() {
            std::cout << "\nAll Moonshine files downloaded successfully!" << std::endl;
            app.quit();
        });

        downloader->downloadMoonshineModel(QString::fromStdString(modelName), "../models/" + QString::fromStdString(modelName));
        return app.exec();
    }

    if (argc > 1 && std::string(argv[1]) == "enroll") {
        std::cout << "\n=== User Voice Enrollment ===" << std::endl;
        std::cout << "We are going to record your voice profile so the system only listens to you." << std::endl;
        std::cout << "Please read the following sentence clearly into your microphone:" << std::endl;
        std::cout << "\n  \"The quick brown fox jumps over the lazy dog and explores the beautiful forest.\"\n" << std::endl;
        
        std::cout << "Recording will start in 3 seconds..." << std::flush;
        QThread::sleep(1); std::cout << " 2..." << std::flush;
        QThread::sleep(1); std::cout << " 1..." << std::flush;
        QThread::sleep(1); std::cout << "\n[Recording Started - Speak Now for 8 seconds]" << std::endl;

        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioSource* source = new QAudioSource(QMediaDevices::defaultAudioInput(), format);
        source->setBufferSize(16000 * 2 * 10); // 10 seconds buffer
        
        QFile file("user_profile.wav");
        if (!file.open(QIODevice::WriteOnly)) {
            std::cerr << "Failed to open user_profile.wav for writing!" << std::endl;
            return 1;
        }

        // Write a dummy WAV header, we will fill in the sizes later
        char dummy_header[44] = {0};
        file.write(dummy_header, 44);

        QIODevice* io = source->start();
        if (!io) {
            std::cerr << "Failed to open microphone!" << std::endl;
            return 1;
        }

        DenoiseState* rnnoise_st = rnnoise_create(NULL);
        std::vector<int16_t> audio_buffer_16k;
        qint64 totalBytes = 0;

        QObject::connect(io, &QIODevice::readyRead, [&]() {
            QByteArray chunk = io->readAll();
            const int16_t* ptr = reinterpret_cast<const int16_t*>(chunk.constData());
            size_t count = chunk.size() / sizeof(int16_t);
            for (size_t i = 0; i < count; ++i) {
                audio_buffer_16k.push_back(ptr[i]);
            }
            
            while (audio_buffer_16k.size() >= 160) {
                float frame_in_48k[480];
                float frame_out_48k[480];
                for (int i = 0; i < 160; ++i) {
                    float val = audio_buffer_16k[i];
                    frame_in_48k[i*3] = val;
                    frame_in_48k[i*3 + 1] = val;
                    frame_in_48k[i*3 + 2] = val;
                }
                audio_buffer_16k.erase(audio_buffer_16k.begin(), audio_buffer_16k.begin() + 160);
                rnnoise_process_frame(rnnoise_st, frame_out_48k, frame_in_48k);
                
                int16_t out_16k_chunk[160];
                for (int i = 0; i < 160; ++i) {
                    float val = (frame_out_48k[i*3] + frame_out_48k[i*3+1] + frame_out_48k[i*3+2]) / 3.0f;
                    float clamped = std::max(-32768.0f, std::min(32767.0f, val));
                    out_16k_chunk[i] = static_cast<int16_t>(clamped);
                }
                file.write(reinterpret_cast<const char*>(out_16k_chunk), 160 * sizeof(int16_t));
                totalBytes += 160 * sizeof(int16_t);
            }
        });

        // Record for 8 seconds
        QTimer::singleShot(8000, [&]() {
            source->stop();
            rnnoise_destroy(rnnoise_st);
            
            // Fix up WAV header
            file.seek(0);
            QDataStream out(&file);
            out.setByteOrder(QDataStream::LittleEndian);
            out.writeRawData("RIFF", 4);
            out << static_cast<quint32>(36 + totalBytes);
            out.writeRawData("WAVE", 4);
            out.writeRawData("fmt ", 4);
            out << static_cast<quint32>(16); // Subchunk1Size
            out << static_cast<quint16>(1);  // AudioFormat (PCM)
            out << static_cast<quint16>(1);  // NumChannels
            out << static_cast<quint32>(16000); // SampleRate
            out << static_cast<quint32>(16000 * 2); // ByteRate
            out << static_cast<quint16>(2);  // BlockAlign
            out << static_cast<quint16>(16); // BitsPerSample
            out.writeRawData("data", 4);
            out << static_cast<quint32>(totalBytes);
            
            file.close();
            std::cout << "[Recording Finished] Saved to user_profile.wav" << std::endl;
            std::cout << "You can now use this profile for speaker verification!" << std::endl;
            app.quit();
        });

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
