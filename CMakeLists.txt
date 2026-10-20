cmake_minimum_required(VERSION 3.22)
project(DT2Emulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DT2Emulator
    COMPANY_NAME "DT2 Emulator"
    BUNDLE_ID "com.dt2emulator.plugin"
    PLUGIN_MANUFACTURER_CODE Dt2e
    PLUGIN_CODE Dt2p
    PRODUCT_NAME "DT2 Emulator"
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Sampler Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_generate_juce_header(DT2Emulator)

# The Digitakt II panel (built web UI) is embedded in the binary and served to the web view.
file(GLOB_RECURSE WEBUI_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/webui/*")
juce_add_binary_data(DT2WebUI SOURCES ${WEBUI_FILES})

target_sources(DT2Emulator PRIVATE
    Source/Model.cpp
    Source/Samples.cpp
    Source/Voice.cpp
    Source/Engine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DT2Emulator PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DT2Emulator
    PRIVATE
        DT2WebUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
