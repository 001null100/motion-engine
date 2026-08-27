cmake_minimum_required(VERSION 3.24)
project(MotionEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MOTION_BUILD_TESTS "Build Motion Engine tests" OFF)
set(MOTION_JUCE_PATH "" CACHE PATH "Path to a JUCE checkout")

if(MOTION_JUCE_PATH)
    add_subdirectory("${MOTION_JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MotionEngine
    COMPANY_NAME "Null Exo"
    BUNDLE_ID "dev.nullexo.motionengine"
    PLUGIN_MANUFACTURER_CODE NuEx
    PLUGIN_CODE MoEn
    FORMATS VST3
    PRODUCT_NAME "Motion Engine"
    DESCRIPTION "Physics-driven modulation engine"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(MotionEngine)

target_sources(MotionEngine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/BridgeEngine.cpp
    Source/BridgeEngine.h
    Source/MotionEngineCore.cpp
    Source/MotionEngineCore.h
)

target_compile_definitions(MotionEngine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if(MSVC)
    target_compile_options(MotionEngine PRIVATE /utf-8)
endif()

target_link_libraries(MotionEngine PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
