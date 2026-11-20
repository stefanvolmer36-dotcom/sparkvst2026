cmake_minimum_required(VERSION 3.24)

project(SparkLEDrum VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.17-security.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SparkLEDrum
    COMPANY_NAME "SparkLE Lab"
    BUNDLE_ID com.sparklelab.drumengine
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Splb
    PLUGIN_CODE Sld1
    FORMATS VST3
    PRODUCT_NAME "SparkLE Drum Engine"
    VST3_CATEGORIES Instrument
    NEEDS_WEBVIEW2 TRUE
)

juce_add_binary_data(SparkLEDrumData
    SOURCES
        ui/index.html
        ui/app.js
        ui/three.module.min.js
)

juce_generate_juce_header(SparkLEDrum)

target_sources(SparkLEDrum
    PRIVATE
        Source/DrumEngine.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(SparkLEDrum
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2=1>
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
)

target_link_libraries(SparkLEDrum
    PRIVATE
        SparkLEDrumData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
