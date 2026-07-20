#include <iostream>
#include <vector>
#include <string>
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
  *out_float_data = result_data;
  *out_num_samples = num_samples;
  if (out_sample_rate != nullptr) *out_sample_rate = sample_rate;
  return true;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <wav_file> <moonshine_model_dir> <ct2_model_dir>" << std::endl;
        return 1;
    }

    std::string wav_path = argv[1];
    std::string moonshine_model_path = argv[2];
    std::string ct2_model_path = argv[3];

    // ==========================================
    // 1. Voice To Text (Moonshine)
    // ==========================================
    float *wav_data = nullptr;
    size_t wav_data_size = 0;
    int32_t wav_sample_rate = 0;
    if (!load_wav_data(wav_path.c_str(), &wav_data, &wav_data_size, &wav_sample_rate)) {
        std::cerr << "Failed to load WAV file!" << std::endl;
        return 1;
    }

    std::cout << "[1] Running Voice-To-Text Transcription via Moonshine..." << std::endl;
    moonshine::Transcriber transcriber(moonshine_model_path, moonshine::ModelArch::TINY);
    moonshine::Transcript transcript = transcriber.transcribeWithoutStreaming(
        std::vector<float>(wav_data, wav_data + wav_data_size), wav_sample_rate, 0);
    free(wav_data);

    // ==========================================
    // 2. Machine Translation (CTranslate2)
    // ==========================================
    std::cout << "\n[2] Initializing Machine Translation Engine..." << std::endl;
    
    // Load sentencepiece
    sentencepiece::SentencePieceProcessor source_spm;
    sentencepiece::SentencePieceProcessor target_spm;
    source_spm.Load(ct2_model_path + "/source.spm");
    target_spm.Load(ct2_model_path + "/target.spm");

    // Initialize Translator Engine
    ctranslate2::Translator translator(ct2_model_path, ctranslate2::Device::CPU);
    
    std::cout << "\n--- Chunked Real-Time Translation Stream ---" << std::endl;

    for (size_t i = 0; i < transcript.lines.size(); ++i) {
        const auto& line = transcript.lines[i];
        if (line.text.empty()) continue;

        // Print the chunk timing and what Moonshine heard
        std::cout << "[Chunk " << (i + 1) << " | " << line.startTime << "s - " << (line.startTime + line.duration) << "s]" << std::endl;
        std::cout << "  Original   : " << line.text << std::endl;

        // Tokenize the exact chunk text
        std::vector<std::string> tokens;
        source_spm.Encode(line.text, &tokens);
        tokens.push_back("</s>"); // Tell the translator the sentence is complete
        
        // Translate this specific chunk!
        std::vector<ctranslate2::TranslationResult> results = translator.translate_batch({tokens});
        
        // Decode the translated tokens back to human readable text
        std::string output_text;
        target_spm.Decode(results[0].output(), &output_text);
        
        // Remove trailing </s> if present
        if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
            output_text = output_text.substr(0, output_text.size() - 4);
        }
        
        std::cout << "  Translated : " << output_text << std::endl << std::endl;
    }

    return 0;
}
