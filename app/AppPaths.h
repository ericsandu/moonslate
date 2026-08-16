#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

// Centralizes where Moonslate keeps its downloadable assets (STT/MT models and
// TTS voice/G2P data). Two layouts are supported:
//
//  * Source tree (development): the binary lives in <repo>/app/build and
//    reuses <repo>/models plus the moonshine submodule's TTS data directory.
//  * Installed layout (releases): <prefix>/bin/moonslate_app with everything
//    under <prefix>/share/moonslate. This also holds for the macOS bundle,
//    where the share root lands inside Moonslate.app/Contents/share.
namespace AppPaths {

inline QString exeDir() {
    return QCoreApplication::applicationDirPath();
}

// True when running from a build directory inside the repository checkout.
inline bool sourceTreeLayout() {
    return QDir(exeDir() + "/../../moonshine/core/moonshine-tts/data").exists();
}

inline QString shareRoot() {
    if (sourceTreeLayout()) {
        return QDir(exeDir() + "/../..").absolutePath(); // repository root
    }
    return QDir(exeDir() + "/../share/moonslate").absolutePath();
}

inline QString modelsRoot() {
    return QDir(shareRoot()).filePath("models");
}

inline QString ttsDataRoot() {
    if (sourceTreeLayout()) {
        return QDir(shareRoot()).filePath("moonshine/core/moonshine-tts/data");
    }
    return QDir(shareRoot()).filePath("tts-data");
}

} // namespace AppPaths
