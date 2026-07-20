#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include "moonshine-cpp.h"

// Duplicate of load_wav_data in debug-utils.cpp to avoid depending on internal moonshine-utils
bool load_wav_data(const char *path, float **out_float_data,
                   size_t *out_num_samples, int32_t *out_sample_rate) {
  *out_float_data = nullptr;
  *out_num_samples = 0;
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
// Helper function to write raw PCM floats to a .wav file
void write_wav(const std::string& filename, const std::vector<float>& samples, int sampleRate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return;

    int32_t numSamples = samples.size();
    int32_t subchunk2Size = numSamples * sizeof(int16_t);
    int32_t chunkSize = 36 + subchunk2Size;
    int32_t byteRate = sampleRate * sizeof(int16_t);

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    int32_t subchunk1Size = 16;
    file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    int16_t audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    int16_t numChannels = 1; // Mono
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    int16_t blockAlign = sizeof(int16_t);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    int16_t bitsPerSample = 16;
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subchunk2Size), 4);

    for (float sample : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        int16_t intSample = static_cast<int16_t>(clamped * 32767.0f);
        file.write(reinterpret_cast<const char*>(&intSample), sizeof(int16_t));
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <audio.wav> <moonshine_model_dir> <ct2_model_dir>\n";
        return 1;
    }

    std::string audio_path = argv[1];
    std::string moon_model_path = argv[2];
    std::string ct2_model_path = argv[3];

    // ==========================================
    // 1. Voice-To-Text (Moonshine)
    // ==========================================
    std::cout << "[1] Running Voice-To-Text Transcription via Moonshine..." << std::endl;
    
    // Load Moonshine Model (Tiny)
    moonshine::Transcriber transcriber(moon_model_path, moonshine::ModelArch::TINY);
    
    // Load local .wav file into float array
    std::vector<float> audio_samples;
    std::ifstream wav_file(audio_path, std::ios::binary);
    wav_file.seekg(44); // Skip 44-byte wav header for this basic example
    int16_t sample;
    while (wav_file.read(reinterpret_cast<char*>(&sample), sizeof(int16_t))) {
        audio_samples.push_back(sample / 32768.0f);
    }
    
    // Transcribe entire file to get the natural chunks
    moonshine::Transcript transcript = transcriber.transcribeWithoutStreaming(audio_samples);

    // ==========================================
    // 2. Machine Translation (CTranslate2)
    // ==========================================
    std::cout << "\n[2] Initializing Machine Translation Engine..." << std::endl;
    
    // Load sentencepiece
    sentencepiece::SentencePieceProcessor source_spm;
    sentencepiece::SentencePieceProcessor target_spm;
    source_spm.Load(ct2_model_path + "/source.spm");
    target_spm.Load(ct2_model_path + "/target.spm");

    // Dynamic Thread Detection
    unsigned int total_threads = std::thread::hardware_concurrency();
    if (total_threads == 0) total_threads = 4; // Fallback
    unsigned int target_threads = std::max(1u, (unsigned int)(total_threads * 0.8));
    
    std::cout << "  -> Detected " << total_threads << " CPU threads. Allocating " << target_threads << " threads for CTranslate2..." << std::endl;

    ctranslate2::ReplicaPoolConfig config;
    config.num_threads_per_replica = target_threads;

    // Initialize Translator Engine
    ctranslate2::Translator translator(ct2_model_path, ctranslate2::Device::CPU, ctranslate2::ComputeType::DEFAULT, {0}, false, config);
    
    // ==========================================
    // 3. Text-To-Speech (Piper)
    // ==========================================
    std::cout << "\n[3] Initializing Piper TTS Engine (German)..." << std::endl;
    std::vector<moonshine_option_t> piper_options = {
        {"g2p_root", "../../moonshine/core/moonshine-tts/data"},
        {"voice", "piper_de_DE-thorsten-medium"},
    };
    moonshine::TextToSpeech tts_piper("de", piper_options);

    std::cout << "\n--- Chunked Real-Time Pipeline Stream ---" << std::endl;

    std::vector<float> all_tts_audio;
    int tts_sample_rate = 0;

    for (size_t i = 0; i < transcript.lines.size(); ++i) {
        const auto& line = transcript.lines[i];
        
        // Skip empty lines
        if (line.text.empty()) continue;
        
        std::cout << "[Chunk " << (i + 1) << " | " << line.startTime << "s - " << (line.startTime + line.duration) << "s]" << std::endl;
        std::cout << "  Original   : " << line.text << std::endl;
        
        // Tokenize the exact chunk text
        std::vector<std::string> tokens;
        source_spm.Encode(line.text, &tokens);
        tokens.push_back("</s>");
        
        // Measure Translation Latency
        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<ctranslate2::TranslationResult> results = translator.translate_batch({tokens});
        auto end_time = std::chrono::high_resolution_clock::now();
        int translation_latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        // Decode the translated tokens back to human readable text
        std::string output_text;
        target_spm.Decode(results[0].output(), &output_text);
        
        // Remove trailing </s> if present
        if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
            output_text = output_text.substr(0, output_text.size() - 4);
        }
        
        std::cout << "  Translated : " << output_text << std::endl;
        
        // Generate Audio Piper
        auto piper_start = std::chrono::high_resolution_clock::now();
        auto piper_result = tts_piper.synthesize(output_text);
        int piper_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - piper_start).count();
        
        // Append to full audio buffer
        all_tts_audio.insert(all_tts_audio.end(), piper_result.samples.begin(), piper_result.samples.end());
        tts_sample_rate = piper_result.sampleRateHz;

        std::cout << "  -> Latency : VTT(" << line.lastTranscriptionLatencyMs << "ms) + CT2(" << translation_latency_ms << "ms) + Piper(" << piper_ms << "ms) = " << (line.lastTranscriptionLatencyMs + translation_latency_ms + piper_ms) << "ms" << std::endl << std::endl;
    }

    std::cout << "\n[4] Saving Synthesized Audio to disk..." << std::endl;
    std::string out_file = "output_german.wav";
    write_wav(out_file, all_tts_audio, tts_sample_rate);
    std::cout << "  -> Saved to " << out_file << " (" << all_tts_audio.size() / (float)tts_sample_rate << " seconds of audio)" << std::endl;

    return 0;
}
