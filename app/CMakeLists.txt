cmake_minimum_required(VERSION 3.17)
project(ct2_demo)

set(CMAKE_CXX_STANDARD 17)

# FetchContent is the standard way to handle cross-platform dependencies in CMake without package managers.
include(FetchContent)

message(STATUS "Fetching CTranslate2 from source...")

# We download CTranslate2 directly from GitHub
FetchContent_Declare(
  ctranslate2
  GIT_REPOSITORY https://github.com/OpenNMT/CTranslate2.git
  GIT_TAG v4.4.0
)

# Configure CTranslate2 for cross-platform edge execution (No CUDA, No Heavy Intel MKL)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static library" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(BUILD_CLI OFF CACHE BOOL "Disable CLI" FORCE)
set(WITH_CUDA OFF CACHE BOOL "Disable CUDA" FORCE)
set(WITH_CUDNN OFF CACHE BOOL "Disable CUDNN" FORCE)
set(WITH_MKL OFF CACHE BOOL "Disable Intel MKL" FORCE)
set(WITH_OPENBLAS OFF CACHE BOOL "Disable OpenBLAS" FORCE)
set(OPENMP_RUNTIME "COMP" CACHE STRING "Use standard compiler OpenMP instead of Intel" FORCE)
set(ENABLE_CPU_DISPATCH OFF CACHE BOOL "Disable runtime CPU dispatch" FORCE)
# GEMM backend per platform: ruy (Google's edge matrix library) on Linux and
# Windows, Apple's Accelerate framework on macOS. CT2's own Windows builds use
# MKL/DNNL, which would drag an Intel oneAPI install into CI; ruy is vendored
# by CT2 and MSVC-supported, so it is the pragmatic first attempt.
if(APPLE)
  set(WITH_ACCELERATE ON CACHE BOOL "Enable Accelerate backend" FORCE)
  set(WITH_RUY OFF CACHE BOOL "Disable Ruy backend" FORCE)
else()
  set(WITH_RUY ON CACHE BOOL "Enable Ruy backend" FORCE)
endif()

FetchContent_MakeAvailable(ctranslate2)

FetchContent_Declare(
  sentencepiece
  GIT_REPOSITORY https://github.com/google/sentencepiece.git
  GIT_TAG v0.2.0
)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "Disable SPM shared lib" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "Disable SPM TCMalloc" FORCE)
FetchContent_MakeAvailable(sentencepiece)

# ==========================================
# Qt Multimedia Application (Moonslate Live)
# ==========================================
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Multimedia Network Widgets Gui)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../noise-suppression-for-voice/external/rnnoise rnnoise_build)

# Resolve the vendored ONNX Runtime for this platform using moonshine's own
# helper (sets ONNXRUNTIME_LIB_PATH).
include(${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/third-party/onnxruntime/find-ort-library-path.cmake)

set(MOONSHINE_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/build)
if(WIN32)
  set(MOONSHINE_LIB ${MOONSHINE_BUILD_DIR}/moonshine.lib)
  set(MOONSHINE_RUNTIME_LIBS ${MOONSHINE_BUILD_DIR}/moonshine.dll ${ONNXRUNTIME_LIB_PATH})
elseif(APPLE)
  set(MOONSHINE_LIB ${MOONSHINE_BUILD_DIR}/libmoonshine.dylib)
  set(MOONSHINE_RUNTIME_LIBS ${MOONSHINE_LIB} ${ONNXRUNTIME_LIB_PATH})
else()
  set(MOONSHINE_LIB ${MOONSHINE_BUILD_DIR}/libmoonshine.so)
  set(MOONSHINE_RUNTIME_LIBS ${MOONSHINE_LIB} ${ONNXRUNTIME_LIB_PATH})
endif()

add_executable(moonslate_app
    main.cpp
    MainWindow.cpp
    LivePipelineWorker.cpp
    AudioPlayer.cpp
    ModelDownloader.cpp
)
set_target_properties(moonslate_app PROPERTIES CXX_STANDARD 20)

if(APPLE)
  # Proper .app bundle with the mandatory microphone usage description;
  # macdeployqt assembles the Qt frameworks around it in CI.
  set_target_properties(moonslate_app PROPERTIES
      OUTPUT_NAME Moonslate
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)
  set(MACOSX_BUNDLE_BUNDLE_NAME Moonslate)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER com.moonslate.app)
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "0.1.0")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")
  set(MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0")
endif()

# Windows and macOS ship moonshine/ORT as shared libraries next to the
# executable; macOS resolves them via the dylib's @loader_path rpath.
if(NOT UNIX)
  add_custom_command(TARGET moonslate_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${MOONSHINE_RUNTIME_LIBS} $<TARGET_FILE_DIR:moonslate_app>
      COMMENT "Copying moonshine and ONNX Runtime runtime libraries")
endif()

if(UNIX AND NOT APPLE)
  # Linux: resolve moonshine/ORT from the executable's directory when running
  # from a release package, and from the build tree during development.
  target_link_options(moonslate_app PRIVATE
      "-Wl,-rpath,$ORIGIN"
      "-Wl,-rpath,${MOONSHINE_BUILD_DIR}"
      "-Wl,-rpath,${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/third-party/onnxruntime/lib/linux/aarch64"
      "-Wl,-rpath,${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/third-party/onnxruntime/lib/linux/x86_64")
elseif(APPLE)
  target_link_options(moonslate_app PRIVATE "-Wl,-rpath,@executable_path")
endif()

target_include_directories(moonslate_app PRIVATE
    ${sentencepiece_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/moonshine-utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../noise-suppression-for-voice/external/rnnoise/include
)
target_link_libraries(moonslate_app PRIVATE
    ctranslate2
    sentencepiece-static
    ${MOONSHINE_LIB}
    ${ONNXRUNTIME_LIB_PATH}
    Qt6::Core Qt6::Multimedia Qt6::Network Qt6::Widgets Qt6::Gui
    RnNoise
)
