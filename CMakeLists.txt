cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; build universal (x86_64 + arm64) on CI.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()
project(Crate12 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core engine (no JUCE dependency, unit-testable) ----
add_library(crate12_engine STATIC
    Source/engine/Engine.cpp
    Source/engine/Serialize.cpp)
target_include_directories(crate12_engine PUBLIC Source/engine)
set_target_properties(crate12_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(CRATE12_BUILD_TESTS "Build engine tests" ON)
if(CRATE12_BUILD_TESTS)
    add_executable(crate12_tests tests/EngineTests.cpp)
    target_link_libraries(crate12_tests PRIVATE crate12_engine)
    enable_testing()
    add_test(NAME engine COMMAND crate12_tests)
endif()

option(CRATE12_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(NOT CRATE12_BUILD_PLUGIN)
    return()
endif()

# ---- JUCE ----
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Crate12
    COMPANY_NAME "Crate12Audio"
    PLUGIN_MANUFACTURER_CODE Ct12
    PLUGIN_CODE C12a
    FORMATS VST3 Standalone
    PRODUCT_NAME "Crate12"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler)

target_sources(Crate12 PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp)

target_compile_definitions(Crate12 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Crate12
    PRIVATE
        crate12_engine
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
