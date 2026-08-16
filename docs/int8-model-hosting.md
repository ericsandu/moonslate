# Hosting int8 opus-mt models on Hugging Face

Moonslate downloads its translation models from Hugging Face at first run.
The float32 / float16 conversions weigh 300–600 MB once loaded; the int8
conversions are ~72 MB on disk and roughly 2–4× faster on ARM, which is what
makes the mobile targets viable. This guide covers the current inventory and
how to produce and host the missing conversions yourself.

## Current inventory (verified 2026-08-16)

| Language | Repo | Format |
|---|---|---|
| French | [`craftwise/ct2-opus-mt-en-fr-int8`](https://huggingface.co/craftwise/ct2-opus-mt-en-fr-int8) | int8, 73 MB |
| German | [`cstr/opus-mt-en-de-ct2-int8`](https://huggingface.co/cstr/opus-mt-en-de-ct2-int8) | int8, 72 MB |
| Spanish | — | none hosted; fp32: `michaelfeil/ct2fast-opus-mt-en-es` |
| Italian | — | none hosted; fp16: `ooeoeo/opus-mt-en-it-ct2-float16` |
| Portuguese | — | none hosted; fp16: `ooeoeo/opus-mt-tc-big-en-pt-ct2-float16` |
| Russian | — | none hosted; fp16: `ooeoeo/opus-mt-en-ru-ct2-float16` |

The app accepts any repo whose root contains the five files CTranslate2
needs: `config.json`, `model.bin`, `source.spm`, `target.spm`, and
`shared_vocabulary.json` (or `.txt` — the downloader retries the other
extension automatically).

## Converting a model to int8

CTranslate2's own converter produces everything needed:

```bash
python3 -m venv /tmp/ct2env && source /tmp/ct2env/bin/activate
pip install "ctranslate2>=4.0" "transformers[sentencepiece]" torch

ct2-transformers-converter \
    --model Helsinki-NLP/opus-mt-en-es \
    --output_dir opus-mt-en-es-ct2-int8 \
    --quantization int8
```

Notes:

- Source model: the original `Helsinki-NLP/opus-mt-en-<lang>` (or
  `opus-mt-tc-big-en-<lang>` for the higher-quality big variants, at ~3× size).
- Verify the output contains all five files listed above; small models
  sometimes only emit a `sentencepiece.model`, which our downloader cannot
  use — if that happens, copy `source.spm`/`target.spm` from the
  `Helsinki-NLP` repo (they are the same tokenizers).
- Sanity-check the conversion loads and translates (see snippet below).
- Quality: int8 quantization of opus-mt models is near-lossless for
  translation; if you want to be careful, eyeball a handful of sentences
  against the fp32 model.

```python
import ctranslate2
translator = ctranslate2.Translator("opus-mt-en-es-ct2-int8")  # int8 auto-detected
print(translator.translate_batch([["▁Hello", "▁world", "."]]))
```

## Hosting on Hugging Face

1. Create a write token at <https://huggingface.co/settings/tokens> and log in:

   ```bash
   pip install -U "huggingface_hub[cli]"
   hf auth login            # paste the token when prompted
   ```

2. Create the repo (pick your namespace, e.g. `ericsandu`):

   ```bash
   hf repo create ericsandu/opus-mt-en-es-ct2-int8 --repo-type model
   ```

3. Write a model card (`README.md`) **before** uploading — HF renders it on
   the model page, and attribution matters because opus-mt weights are
   **CC-BY-4.0**:

   ```markdown
   ---
   language:
     - en
     - es
   tags:
     - translation
     - ctranslate2
     - int8
     - opus-mt
   license: cc-by-4.0
   base_model: Helsinki-NLP/opus-mt-en-es
   ---

   # opus-mt-en-es for CTranslate2 (int8)

   INT8 conversion of [Helsinki-NLP/opus-mt-en-es](https://huggingface.co/Helsinki-NLP/opus-mt-en-es)
   produced with `ct2-transformers-converter --quantization int8` (CTranslate2 4.x).
   Original weights and tokenizer are CC-BY-4.0, © University of Helsinki.
   ```

4. Upload:

   ```bash
   hf upload ericsandu/opus-mt-en-es-ct2-int8 opus-mt-en-es-ct2-int8 .
   ```

5. Verify one file resolves (LFS redirect → 200/206):

   ```bash
   curl -sIL "https://huggingface.co/ericsandu/opus-mt-en-es-ct2-int8/resolve/main/model.bin" | head -1
   ```

## Wiring it into Moonslate

Add or update the entry in `AppController`'s `m_supportedLanguages`
(`app/AppController.cpp`):

```cpp
{"Spanish", "ericsandu/opus-mt-en-es-ct2-int8", "piper_es_ES-davefx-medium", "es"},
```

The local cache directory is derived from the repo's last path segment, so
switching a language to a new repo triggers a fresh download instead of
shadowing the previously cached weights.
