# Moonslate 🌙

Moonslate is a blazing-fast, privacy-first, fully offline **Live Speech-to-Speech Translation** application. Built entirely in C++ with a Qt frontend, Moonslate listens to your voice, transcribes it in real-time, translates the text, and synthesizes the spoken result—all running entirely on your local hardware.

## ✨ Features

- **Live Speech-to-Text (STT)**: Utilizes the ultra-efficient [Moonshine](https://github.com/usefulsensors/moonshine) models for real-time streaming transcription of spoken English.
- **Machine Translation (MT)**: Uses [CTranslate2](https://github.com/OpenNMT/CTranslate2) paired with Helsinki-NLP's `opus-mt` models for lightning-fast inference.
- **Text-to-Speech (TTS)**: Integrates [Piper TTS](https://github.com/rhasspy/piper) high-quality local neural voices for incredibly natural playback.
- **Automated Model Management**: The GUI transparently handles the downloading, caching, and loading of Hugging Face models and Moonshine CDN weights.
- **Voice Activity Detection (VAD)**: Incorporates `RNNoise` to aggressively filter out background noise and prevent model hallucination during silence.
- **Software AEC**: Implements a clever software-level Acoustic Echo Cancellation hack to prevent the microphone from picking up its own TTS playback, eliminating feedback loops.
- **Persistent State**: Automatically remembers your preferred STT model sizes and translation languages across restarts.

## 🏗️ Architecture

Moonslate is composed of highly modularized C++ components:
- **`MainWindow`**: The Qt graphical interface, managing model configuration, settings persistence, and system initialization.
- **`LivePipelineWorker`**: A background Qt thread operating a state-machine that continuously feeds PCM audio into the STT -> MT -> TTS pipeline.
- **`ModelDownloader`**: An asynchronous network manager that handles redirects, Hugging Face LFS resolution, and CDN fetching.
- **`AudioPlayer`**: Manages trickle-feeding audio buffers to the OS `QAudioSink` to ensure zero-latency playback without buffer underruns.

## 🚀 Getting Started

### Prerequisites

You need a Linux environment with the following dependencies installed:
- `cmake`
- `build-essential`
- `qt6-base-dev`
- `qt6-multimedia-dev`
- `libpulse-dev` / `libasound2-dev`
- `curl`

### Build Instructions

Building the project is fully automated via the provided bash script. This script will initialize the git submodules, apply our custom architectural patches to Moonshine, build the core libraries, and link the Qt application.

```bash
git clone --recursive https://github.com/yourusername/moonslate.git
cd moonslate
./build.sh
```

### Usage

Once built, simply launch the binary:
```bash
./app/build/moonslate_app
```
1. Select your target language from the **Settings > Language** menu (defaults to German).
2. Select your desired Moonshine STT model size from **Settings > Moonshine Model** (defaults to `tiny`).
3. Click **Start Translation** to begin speaking. The app will automatically download the necessary models to your `models/` directory if they are not already cached.

## 🗺️ Roadmap & Future Work

Moonslate is rapidly evolving. The following features are planned for future iterations:

- [ ] **Two-Way Translation**: Currently, the pipeline is hardcoded for English -> Target Language. We plan to introduce full duplex conversation support (e.g., German back to English).
- [ ] **Client-Server Architecture**: Support offloading the heavy ML processing (CTranslate2 / Moonshine) to a separate, self-hosted GPU processing server, allowing the Qt frontend to act as a lightweight, low-power client.
- [ ] **Hardware AEC Integration**: Replace the software muting hack with true DSP-based Acoustic Echo Cancellation for uninterrupted conversational flow.
- [ ] **GPU Acceleration**: Expose options to leverage CUDA/TensorRT for Moonshine and CTranslate2 for users with dedicated hardware.
- [ ] **Extended Language Support**: Expand the supported language matrix beyond French, German, and Spanish.

## 🤝 Contributing

Contributions, issues, and feature requests are welcome! Feel free to check the issues page. When modifying submodules, please ensure you export your changes using `git format-patch` and place them in the `patches/` directory so the `build.sh` script can automatically apply them for other contributors.
