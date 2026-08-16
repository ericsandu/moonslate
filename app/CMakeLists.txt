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
# Ruy is a lightweight, highly optimized matrix library made by Google specifically for ARM/x86 edge devices.
set(WITH_RUY ON CACHE BOOL "Enable Ruy backend" FORCE)

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

# Moonshine links dynamically against its vendored ONNX Runtime; the app binary
# needs to find that library both at link time and at runtime.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(ORT_ARCH_DIR aarch64)
else()
  set(ORT_ARCH_DIR x86_64)
endif()
set(ORT_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/third-party/onnxruntime/lib/linux/${ORT_ARCH_DIR})

add_executable(moonslate_app
    main.cpp
    MainWindow.cpp
    LivePipelineWorker.cpp
    AudioPlayer.cpp
    ModelDownloader.cpp
)
set_target_properties(moonslate_app PROPERTIES CXX_STANDARD 20)
target_link_directories(moonslate_app PRIVATE ${ORT_LIB_DIR})
target_link_options(moonslate_app PRIVATE "-Wl,-rpath,${ORT_LIB_DIR}")
target_include_directories(moonslate_app PRIVATE 
    ${sentencepiece_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/moonshine-utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../noise-suppression-for-voice/external/rnnoise/include
)
target_link_libraries(moonslate_app PRIVATE 
    ctranslate2 
    sentencepiece-static 
    ${CMAKE_CURRENT_SOURCE_DIR}/../moonshine/core/build/libmoonshine.so
    Qt6::Core Qt6::Multimedia Qt6::Network Qt6::Widgets Qt6::Gui
    RnNoise
)
