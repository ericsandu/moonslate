# Moonslate — Agent Context

> **Living handoff document.** Everything an AI agent (or new contributor) needs to work
> on this repo effectively: architecture, verified facts, hard-won gotchas, in-flight work,
> and next steps. **Keep this updated as work progresses** — every session that changes
> something meaningful should end with a commit touching this file.

Last updated: 2026-08-16 (session: QML shell + int8 models landed)

---

## 1. What this project is

Moonslate is a fully offline, privacy-first **live speech-to-speech translator** written in
C++ with a Qt frontend: microphone → Moonshine streaming STT (English) → CTranslate2
opus-mt translation → Piper TTS in the target language, with RNNoise VAD and a software
AEC hack (mic muted for the TTS playback duration + 400 ms).

- Repo: `https://github.com/ericsandu/moonslate` (push via SSH: `git@github.com:ericsandu/moonslate.git` — the https remote has no credentials; `gh` CLI is logged in as `ericsandu`, git protocol = SSH)
- Branch in active development: `work` (PRs target `main`)
- Commit style: conventional prefixes exactly like the existing history — `feat:`, `fix:`, `ci:`, `chore:`, `docs:` — one logical change per commit, body explains the *why*
- License: AGPLv3

## 2. Layout

```
app/                  Qt application sources (see §4; Widgets UI is being replaced by QML)
  AppPaths.h          dev-vs-installed asset root resolution
  MainWindow.{h,cpp}  Widgets UI + pipeline orchestration (BEING RETIRED — see §7)
  LivePipelineWorker  QThread running STT→MT→TTS
  ModelDownloader     QNetworkAccessManager-based downloader (HF repos, moonshine CDN, generic file lists)
  AudioPlayer         trickle-feeds PCM into QAudioSink
  miniaudio.h         vendored, currently UNUSED
scripts/
  apply-moonshine-patches.sh   idempotent `git am ../patches/*.patch` into the submodule
  ci-build-linux.sh|windows.sh|macos.sh   per-OS build+package+selftest (Ninja, share EXTRA_CMAKE_FLAGS for ccache)
patches/              our moonshine commits exported with format-patch; applied at build time
build.sh              local dev entry: submodule init + scripts/ci-build-linux.sh
moonshine/            SUBMODULE pinned at upstream commit (see §3)
noise-suppression-for-voice/   SUBMODULE (RNNoise), pinned at upstream HEAD, nothing custom
.github/workflows/release.yml 3-OS release matrix (see §6)
```

## 3. Submodule rules (critical)

- `moonshine` pointer **must stay on an upstream-reachable commit** (currently
  `06f74196` = upstream main, "Record 0.1.2 in the changelog"). CI clones submodules from
  GitHub; local-only commits would break checkout. Our custom changes live in `patches/`
  and are applied via `git am` during every build (see `scripts/apply-moonshine-patches.sh`).
- Workflow for changing submodule patches: branch in the submodule from the pinned
  upstream SHA → commit → `git format-patch <upstream-sha> -o ../patches/` → reset the
  submodule worktree back to the upstream SHA (keep only the exported patch files).
- Submodule LFS: `actions/checkout` with `lfs: true` covers CI. Locally, `git-lfs` is NOT
  installed; the two needed LFS sources (`core/cpp-annote/src/community1_cpp_annote_embedded.cpp`,
  `core/moonshine-tts/src/zipvoice-voices-data.cpp`) can be fetched from
  `https://media.githubusercontent.com/media/moonshine-ai/moonshine/<UPSTREAM_SHA>/<path>`
  (use the upstream SHA, not local patch commits — they don't exist on GitHub) and verified
  against the sha256 in the pointer files.
- `noise-suppression-for-voice` is at upstream HEAD (`9c4e5c28`); nothing to maintain.

## 4. App architecture (current state on branch `work`)

Pipeline lifecycle lives in `MainWindow::checkAndStartPipeline()` — a recursive state
machine: [1] tear down worker → [2] download Moonshine STT model if missing → [3] download
CT2 MT model if missing → [3.5] download TTS voice/G2P assets if missing (via moonshine's
`TextToSpeech::getDependencies` JSON manifest, files land under the g2p root) → [4] start
`LivePipelineWorker`. Every downloader emits progress that is shown on the status button.

- `LivePipelineWorker` (QThread): maps model size → arch (`tiny|small|medium` →
  `TINY|SMALL|MEDIUM_STREAMING` — was hardcoded TINY, fixed), passes `keyterms` Transcriber
  option (context biasing, streaming archs only), loads CT2 translator + SentencePiece,
  Piper TTS via `g2pRoot` option, RNNoise VAD (0.6 threshold, pristine audio into STT),
  software AEC via `ignore_audio_until_ms`. Whole `run()` body is wrapped in try/catch →
  `pipelineError(QString)` signal (init failures used to SIGABRT the process).
- `AppPaths.h`: dev layout (`<repo>/models`, `<repo>/moonshine/core/moonshine-tts/data`)
  vs installed layout (`<prefix>/share/moonslate/{models,tts-data}`), selected by whether
  the exe sits in `<repo>/app/build`.
- `main.cpp --selftest`: headless smoke test (paths writable, audio inputs,
  `Transcriber::getCatalog()`). CI runs it with `QT_QPA_PLATFORM=offscreen`. Will gain a
  QML-load check as part of the QML refactor.
- `ModelDownloader::downloadFileList(list)` — generic (url, dest) queue used by the TTS
  asset stage. HF downloads need `NoLessSafeRedirectPolicy` (LFS→CDN 302s); the
  shared_vocabulary.json→.txt fallback logic exists for repos with txt vocab.

### The `emit` macro dance (needed anywhere moonshine headers meet Qt headers)

```cpp
#undef emit
#include "moonshine-cpp.h"
#define emit
```
Qt defines `emit` globally; moonshine has `Stream::emit()`. Files currently doing this:
`LivePipelineWorker.cpp`, `MainWindow.cpp`, `main.cpp` (and the new controller, see §7).

## 5. Languages & models (all verified on HF)

| Language | MT repo (CT2) | Notes | Piper voice |
|---|---|---|---|
| French | `michaelfeil/ct2fast-opus-mt-en-fr` | fp32 ~310 MB | `piper_fr_FR-upmc-medium` |
| German (default) | `michaelfeil/ct2fast-opus-mt-en-de` | fp32 | `piper_de_DE-thorsten-high` |
| Spanish | `michaelfeil/ct2fast-opus-mt-en-es` | fp32 | `piper_es_ES-davefx-medium` |
| Italian | `ooeoeo/opus-mt-en-it-ct2-float16` | float16→fp32 in RAM (~2× RAM, CT2 warns; fine on desktop) | `piper_it_IT-paola-medium` |
| Portuguese | `ooeoeo/opus-mt-tc-big-en-pt-ct2-float16` | same caveat | `piper_pt_BR-faber-medium` |
| Russian | `ooeoeo/opus-mt-en-ru-ct2-float16` | same caveat | `piper_ru_RU-dmitri-medium` |

- Dutch/Polish unsupported: no compatible ct2 build for Dutch (sentencepiece.model-only
  repos), no Piper Polish voice.
- Reverse (two-way) direction: only **Spanish ↔ English** is currently buildable —
  upstream moonshine STT exists for `ar, es, ja, ko, vi, uk, zh` (batch-only, no streaming
  except English); `michaelfeil/ct2fast-opus-mt-es-en` verified to exist. Two-way needs
  VAD-segmented batch transcription in the worker (see README roadmap note).
- TTS voices/G2P are runtime CDN downloads (`https://download.moonshine.ai/tts/<subdir>/...`),
  manifest from `moonshine::TextToSpeech::getDependencies(langCode, {{"g2p_root",...},{"voice",...}})`.
  Voice stems resolve per language subdir (e.g. pt voice → `pt_br/`).

### int8 model research (2026-08-16, for mobile/RAM)

Exists with our exact file layout (config.json, model.bin, source.spm, target.spm,
shared_vocabulary.json):
- `cstr/opus-mt-en-de-ct2-int8` — model.bin **72 MB** (int8)
- `craftwise/ct2-opus-mt-en-fr-int8` — model.bin **73 MB** (int8)

**NOT found**: int8 ct2 for es / it / pt / ru. Plan: switch fr/de to the int8 repos (or
keep fp32 on desktop and make int8 a mobile profile), and write a conversion/hosting guide
(`docs/int8-models.md`) for the missing four — convert with
`ct2-transformers-converter --quantization int8` from the Helsinki-NLP originals, push to
HF under the project owner's namespace, then add to `supportedLanguages`.

## 6. CI (all three OS jobs GREEN as of run 31949001281, 2026-08-16)

`release.yml`: triggers on `v*` tags + `workflow_dispatch`. Jobs build-linux
(ubuntu-latest), build-windows (windows-latest + MSVC via `ilammy/msvc-dev-cmd`),
build-macos (macos-15). Each: checkout (submodules recursive, lfs) → git identity (needed
for `git am` patches) → deps → ccache (`actions/cache` on `$RUNNER_TEMP/ccache`,
`EXTRA_CMAKE_FLAGS` injects compiler launchers) → `scripts/ci-build-<os>.sh` →
selftest inside script → `actions/upload-artifact` → `softprops/action-gh-release`
**gated on `startsWith(github.ref, 'refs/tags/')`** (so manual dispatches don't fail).
Artifacts: linux tar.gz (bin + share skeleton), windows zip (windeployqt'd), macOS
ditto'd signed .app (Info.plist has NSMicrophoneUsageDescription).

**CI gotchas learned the hard way** (don't relearn these):
0. Linux Qt Quick via distro packages needs a surprisingly wide apt set:
   `qt6-declarative-dev` + `qt6-base-private-dev` + `qt6-declarative-private-dev`
   (Qt6::QuickLayouts references QtGui/QtQuick versioned private include dirs),
   `libxkbcommon-dev` (XKB::XKB target for Qt6::GuiPrivate), and at RUNTIME the
   `qml6-module-*` packages (qtqml, qtqml-workerscript, qtquick, qtquick-controls,
   qtquick-layouts, qtquick-templates, qtquick-window) — linking succeeds without
   them, the selftest QML load fails with "module QtQuick is not installed".
1. Linux runtime: the loader does NOT inherit the executable's RUNPATH for transitive
   deps — `libonnxruntime.so.1` must be copied next to `libmoonshine.so` (script does).
2. Windows Qt: `install-qt-action@v4` input is **`modules:`**, NOT `components:`
   (silently ignored, no error). Module id: `qtmultimedia`. Use `cache-key-prefix` when
   the module set changes (stale cache predating modules).
3. Windows link: CTranslate2 pins static `/MT` for static builds; Qt needs `/MD`. Script
   perl-patches `ctranslate2-src/CMakeLists.txt`
   (`"MultiThreaded$<` → `"MultiThreadedDLL$<`) after configure; Ninja re-configures.
4. Windows moonshine: default is a STATIC `moonshine.lib` whose private deps
   (moonshine-utils, ort-utils) don't propagate → LNK2019 on `trim`/`replace_all`.
   Configure moonshine with `-DMOONSHINE_BUILD_SHARED=ON`.
5. macOS: Apple Clang has no OpenMP → CT2 `OPENMP_RUNTIME=NONE` (set in app/CMakeLists
   for APPLE; Accelerate GEMM backend doesn't need OpenMP).
6. macOS: selftest must run on the build-tree bundle BEFORE `macdeployqt` (deploy prunes
   all platform plugins except cocoa; runner has no window server).
7. Push verification: a workflow dispatch doesn't prove your push landed — one push failed
   silently over SSH while CI ran the stale commit. Always compare
   `git ls-remote ... refs/heads/work` with local HEAD after pushing.
8. `gh run watch <id> --exit-status` in a background task is the efficient polling loop;
   `gh run view --job <id> --log | grep -E "error|FAILED"` from the repo ROOT (running it
   inside the submodule dir queries the wrong repo).

## 7. QML shell (DONE, 2026-08-16) and int8 models

The Qt **Widgets** UI was replaced by a pure **Qt Quick (QML)** shell so the same UI
runs on desktop + Android + iOS (Widgets is unsupported on iOS):

- `app/AppController.{h,cpp}` — QObject orchestration layer (the old MainWindow logic):
  properties `statusText`/`statusColor`/`ready`/`recording`/`languageNames`/
  `currentLanguageName`/`translationLabel`/`modelSizes`/`currentModelSize`/`keyterms`,
  invokables `selectLanguage`/`selectModelSize`, WRITE property `keyterms`. Registered
  with `qmlRegisterType<AppController>("Moonslate", 1, 0, ...)` in main.cpp.
- `app/qml/Main.qml` — dark theme matching the old UI (bg #121212, panes #1E1E1E/#1A2E1A,
  accent #4CAF50), ToolBar header, settings Menu with nested Language / Moonshine Model
  submenus (Instantiator-driven), Custom Vocabulary dialog, record toggle bound to
  statusText/statusColor, two TranscriptPane components (ListView + signal connect on
  `transcriptReady`).
- `main.cpp` — `QGuiApplication` + `QQmlApplicationEngine`, qrc-loaded QML, selftest now
  ALSO loads the QML shell (`QT_QUICK_BACKEND=software`, `MOONSLATE_SELFTEST=1` env
  suppresses AppController's pipeline start so headless CI doesn't download models).
- CMake: `Core Gui Qml Quick QuickControls2 QuickLayouts Multimedia Network`
  (NO Widgets), `qt_add_resources(... PREFIX "/" FILES qml/Main.qml)` → `qrc:/qml/Main.qml`.
  **Gotchas learned**: qt_add_resources needs `set(CMAKE_AUTORCC ON)`; the resource
  alias stacks on the prefix — `PREFIX "/qml" FILES qml/Main.qml` lands at
  `/qml/qml/Main.qml`, use `PREFIX "/"`.
- CI: linux apt gained `qt6-declarative-dev` (aqt default archives and brew qt already
  include Quick); `windeployqt ... --qmldir app/qml` and
  `macdeployqt ... -qmldir=app/qml` so the Qt Quick modules are deployed — the QML is
  inside qrc, so the deploy tools cannot discover imports without being pointed at the
  sources.

int8 models: German→`cstr/opus-mt-en-de-ct2-int8` (72 MB), French→
`craftwise/ct2-opus-mt-en-fr-int8` (73 MB); es/it/pt/ru have no hosted int8 build —
`docs/int8-model-hosting.md` documents conversion (`ct2-transformers-converter
--quantization int8`), HF upload, model-card/attribution (opus-mt is CC-BY-4.0), and
wiring into `AppController`. MT cache dirs are now keyed by repo last path segment
(int8 switch must not be shadowed by previously cached fp32 weights).

Mobile follow-ups (analyzed, not started): Android = Qt for Android + NDK (moonshine
vendors per-ABI ORT .so; CT2+ruy cross-compiles; RECORD_AUDIO permission;
AppPaths → QStandardPaths::AppDataLocation). iOS = static framework path upstream already
provides; needs signing secrets.

## 8. Local environment notes (this machine)

- arm64 Linux (aarch64), GCC 16.1, cmake 4.4.2, Ninja, Qt 6.11.1 dev packages
  (Widgets+Multimedia; `qt6-declarative-dev` needed for the QML work), ccache present.
- Has 2 audio inputs (selftest enumerates them) but no display — always run GUI-adjacent
  tests with `QT_QPA_PLATFORM=offscreen`.
- Full build via `bash scripts/ci-build-linux.sh` (~4 min warm). `app/build` and
  `moonshine/core/build` are Ninja dirs; scripts auto-wipe non-Ninja stale dirs.
- venv at `/tmp/aqtvenv` has `aqtinstall` for querying Qt module names
  (`python -m aqt list-qt windows desktop --modules 6.8.3 win64_msvc2022_64`).

## 9. Verification checklist for any change

1. `bash scripts/ci-build-linux.sh` locally (build + selftest + package + selftest the
   packaged installed-layout copy from /tmp).
2. Push (SSH URL), **verify remote SHA matches**, `gh workflow run release.yml --ref work`,
   watch, triage with `--log-failed` / job logs.
3. Commit conventions + update this file.
