#include "LivePipelineWorker.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

// [SETBACK & FIX]: Qt defines a macro called 'emit' globally which completely breaks 
// standard C++ methods that happen to be named 'emit'. 
// The Moonshine library uses `Stream::emit()` internally for streaming tokens.
// If we include moonshine-cpp.h directly after Qt headers, it fails to compile
// with syntax errors due to the preprocessor expanding 'emit' into an empty string.
// To fix this, we `#undef emit` before including moonshine, and then `#define emit` back.
#undef emit
#include "moonshine-cpp.h"
#define emit


#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioSource>
#include <QIODevice>
#include <rnnoise.h>

LivePipelineWorker::LivePipelineWorker(QString m, QString c, QString pv, QString lc) 
    : moonPath(m), ct2Path(c), piperVoice(pv), langCode(lc) {}

void LivePipelineWorker::setRecording(bool rec) {
    is_recording.store(rec);
}

    void LivePipelineWorker::run() {
        std::cout << "[1] Initializing Live Voice-To-Text Transcription via Moonshine..." << std::endl;
        moonshine::Transcriber transcriber(moonPath.toUtf8().constData(), moonshine::ModelArch::TINY_STREAMING);

        std::cout << "[2] Initializing Machine Translation Engine..." << std::endl;
        sentencepiece::SentencePieceProcessor spm_source, spm_target;
        spm_source.Load((ct2Path + "/source.spm").toUtf8().constData());
        spm_target.Load((ct2Path + "/target.spm").toUtf8().constData());

        ctranslate2::ReplicaPoolConfig config;
        config.num_threads_per_replica = 0; 
        ctranslate2::Translator translator(ct2Path.toUtf8().constData(), ctranslate2::Device::CPU, ctranslate2::ComputeType::DEFAULT, {0}, false, config);

        std::cout << "[3] Initializing Piper TTS Engine..." << std::endl;
        std::string voiceStr = piperVoice.toStdString();
        std::vector<moonshine_option_t> piper_options = {
            {"g2p_root", "../../moonshine/core/moonshine-tts/data"},
            {"voice", voiceStr.c_str()},
        };
        moonshine::TextToSpeech tts_piper(langCode.toStdString().c_str(), piper_options);

        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioSource* source = new QAudioSource(QMediaDevices::defaultAudioInput(), format);
        source->setBufferSize(16000 * 2 * 2); // 2 seconds

        moonshine::Stream stream = transcriber.createStream(1.0, 0); 
        
        // [ARCHITECTURE & SETBACK]: Moonshine STT is extremely sensitive and will hallucinate text 
        // if fed pure static or silence. To combat this, we integrate RNNoise (Recurrent Neural Network Noise Suppression)
        // to provide a reliable Voice Activity Detection (VAD) probability.
        DenoiseState* rnnoise_st = rnnoise_create(NULL);
        std::vector<float> audio_buffer_16k;
        
        stream.addListener([&](const moonshine::TranscriptEvent& ev) {
            if (ev.type != moonshine::TranscriptEvent::LINE_COMPLETED && 
                ev.type != moonshine::TranscriptEvent::LINE_UPDATED) {
                return;
            }
            if (!ev.line.isComplete) return;
            
            // [VAD FILTERING]: Only proceed if RNNoise detected voice activity with > 60% confidence
            // at some point during this chunk's ingestion. Otherwise, silently drop the transcription.
            float vad = current_max_vad.exchange(0.0f);
            if (vad < 0.6f || ev.line.text.empty()) return; // Drop silence
            
            auto t1 = std::chrono::steady_clock::now();
            
            std::vector<std::string> tokens;
            std::string output_text;
            
            std::cout << "[En] Original  : " << ev.line.text << std::endl;
            spm_source.Encode(ev.line.text, &tokens);
            tokens.push_back("</s>");
            std::vector<ctranslate2::TranslationResult> results = translator.translate_batch({tokens});
            spm_target.Decode(results[0].output(), &output_text);
            
            if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
                output_text = output_text.substr(0, output_text.size() - 4);
            }
            
            std::cout << "     Translated: " << output_text << std::endl;
            
            moonshine::TtsSynthesisResult piper_result = tts_piper.synthesize(output_text);
            
            auto t2 = std::chrono::steady_clock::now();
            int exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
            
            emit transcriptUpdated(QString::fromStdString(ev.line.text), QString::fromStdString(output_text), QString("%1ms").arg(exec_ms));
            
            int duration_ms = (piper_result.samples.size() * 1000) / piper_result.sampleRateHz;
            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();
            
            // [SETBACK & FIX: Software AEC]: When the TTS begins playing, the microphone immediately picks it up, 
            // causing an infinite loop where the app endlessly translates its own speech output. 
            // Real Acoustic Echo Cancellation (AEC) is complex. Our hack is to simply mute microphone 
            // ingestion for the exact duration of the generated TTS audio + a 400ms padding.
            long long current_ignore = ignore_audio_until_ms.load();
            if (now < current_ignore) {
                ignore_audio_until_ms.store(current_ignore + duration_ms + 400); 
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

        std::cout << "\n--- Live Streaming Started ---\n" << std::endl;
        emit pipelineReady();
        stream.start();

        connect(io, &QIODevice::readyRead, this, [&]() {
            QByteArray raw = io->readAll();
            if (raw.isEmpty()) return;

            if (!is_recording.load()) return;

            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()).time_since_epoch().count();
            if (now < ignore_audio_until_ms.load()) {
                return; // Echo cancellation drop
            }

            const int16_t* ptr = reinterpret_cast<const int16_t*>(raw.constData());
            size_t count = raw.size() / sizeof(int16_t);
            std::vector<float> out_16k_chunk_pristine;
            for (size_t i = 0; i < count; ++i) {
                audio_buffer_16k.push_back(static_cast<float>(ptr[i]));
            }

            while (audio_buffer_16k.size() >= 160) {
                float frame_in_48k[480];
                float frame_out_48k[480];
                for (int i = 0; i < 160; ++i) {
                    float val = audio_buffer_16k[i];
                    // Point upsampling for RNNoise
                    frame_in_48k[i*3]     = val;
                    frame_in_48k[i*3 + 1] = val;
                    frame_in_48k[i*3 + 2] = val;
                }
                
                // We use RNNoise PURELY for its highly accurate Voice Activity Detection!
                float vad = rnnoise_process_frame(rnnoise_st, frame_out_48k, frame_in_48k);
                
                for (int i = 0; i < 160; ++i) {
                    // But we push the PRISTINE, unmodified audio into Moonshine so consonants are perfect!
                    out_16k_chunk_pristine.push_back(audio_buffer_16k[i] / 32768.0f);
                }
                audio_buffer_16k.erase(audio_buffer_16k.begin(), audio_buffer_16k.begin() + 160);
                
                float current = current_max_vad.load();
                while (vad > current && !current_max_vad.compare_exchange_weak(current, vad)) {}
            }

            if (!out_16k_chunk_pristine.empty()) {
                stream.addAudio(out_16k_chunk_pristine, 16000);
            }
        });

        exec();

        stream.stop();
        source->stop();
        delete source;
        rnnoise_destroy(rnnoise_st);
    }
