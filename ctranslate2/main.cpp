#include <iostream>
#include <vector>
#include <string>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <path_to_ct2_model> \"<text_to_translate>\"" << std::endl;
        return 1;
    }

    std::string model_path = argv[1];
    std::string text = argv[2];
    
    std::cout << "Loading OPUS model from: " << model_path << std::endl;

    try {
        // 1. Initialize SentencePiece Processors
        sentencepiece::SentencePieceProcessor source_spm;
        sentencepiece::SentencePieceProcessor target_spm;
        
        auto status1 = source_spm.Load(model_path + "/source.spm");
        if (!status1.ok()) {
            std::cerr << "Failed to load source.spm: " << status1.ToString() << std::endl;
            return 1;
        }
        
        auto status2 = target_spm.Load(model_path + "/target.spm");
        if (!status2.ok()) {
            std::cerr << "Failed to load target.spm: " << status2.ToString() << std::endl;
            return 1;
        }

        // 2. Initialize the CTranslate2 engine (CPU execution)
        ctranslate2::Translator translator(model_path, ctranslate2::Device::CPU);
        
        // 3. Tokenize the input text using SentencePiece
        std::vector<std::string> tokens;
        source_spm.Encode(text, &tokens);
        
        // OPUS-MT requires the End-Of-Sentence token to know when to stop
        tokens.push_back("</s>");
        
        std::vector<std::vector<std::string>> batch = {tokens};
        
        std::cout << "Translating: '" << text << "'..." << std::endl;
        
        // 4. Run the CTranslate2 inference
        std::vector<ctranslate2::TranslationResult> results = translator.translate_batch(batch);
        
        // 5. Decode the output tokens back into normal text
        std::vector<std::string> output_tokens = results[0].output();
        std::string output_text;
        target_spm.Decode(output_tokens, &output_text);
        
        // Remove trailing </s> if decoded
        if (output_text.size() >= 4 && output_text.substr(output_text.size() - 4) == "</s>") {
            output_text = output_text.substr(0, output_text.size() - 4);
        }
        
        std::cout << "Translation: " << output_text << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
