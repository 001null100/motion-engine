cmake_minimum_required(VERSION 3.24)
project(MotionEngine VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CTest)
option(MOTION_ENGINE_BUILD_PLUGIN "Build JUCE editor and native CLAP module" ON)

# Immutable shared-framework revision: backpressure-safe automation, bounded
# transactional state loading, numeric/event validation, and Release-active tests.
# Keep Motion Engine's safe aux-output routing and application DSP independent.
FetchContent_Declare(
    null_clap
    GIT_REPOSITORY https://github.com/001null100/null-clap.git
    GIT_TAG 9de3106360f324ee4b08906595c54a3e138f6cb7
    GIT_SHALLOW FALSE
)
set(NULLCLAP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# A consumer build should test the exact host plumbing it ships, not just the DSP.
set(NULLCLAP_BUILD_TESTS ${BUILD_TESTING} CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(null_clap)

if(MOTION_ENGINE_BUILD_PLUGIN)
# JUCE is retained only as the drawing/window toolkit. Motion Engine no longer
# uses JUCE AudioProcessor, APVTS, MIDI, state, VST3, or CLAP wrapper layers.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

nullclap_add_plugin(MotionEngine
    OUTPUT_NAME MotionEngine
    SOURCES
        Source/Entry.cpp
        Source/MotionEnginePlugin.cpp
        Source/MotionEnginePlugin.hpp
        Source/ParameterIds.hpp
        Source/MotionEngineCore.cpp
        Source/MotionEngineCore.h
        Source/BridgeEngine.cpp
        Source/BridgeEngine.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/JuceGuiDelegate.cpp
        Source/JuceGuiDelegate.hpp
)

target_link_libraries(MotionEngine PRIVATE
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
)

if(WIN32)
    target_link_libraries(MotionEngine PRIVATE ws2_32)
endif()

target_compile_definitions(MotionEngine PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

if(MSVC)
    target_compile_options(MotionEngine PRIVATE /utf-8)
endif()

endif()

if(BUILD_TESTING)
    add_executable(MotionEngineCoreTests
        tests/MotionEngineCoreTests.cpp
        Source/MotionEngineCore.cpp
        Source/MotionEngineCore.h
    )
    target_include_directories(MotionEngineCoreTests PRIVATE Source)
    if(MSVC)
        target_compile_options(MotionEngineCoreTests PRIVATE /utf-8)
    endif()
    add_test(NAME motion-engine-core COMMAND MotionEngineCoreTests)
endif()

if(BUILD_TESTING)
    add_executable(MotionEngineContractTests tests/MotionEngineContractTests.cpp
        Source/MotionEnginePlugin.cpp Source/MotionEngineCore.cpp Source/BridgeEngine.cpp)
    target_include_directories(MotionEngineContractTests PRIVATE Source)
    target_compile_definitions(MotionEngineContractTests PRIVATE MOTION_ENGINE_HEADLESS_TEST=1)
    target_link_libraries(MotionEngineContractTests PRIVATE nullclap::nullclap)
    if(WIN32)
        target_link_libraries(MotionEngineContractTests PRIVATE ws2_32)
    endif()
    if(MSVC)
        target_compile_options(MotionEngineContractTests PRIVATE /utf-8)
    endif()
    add_test(NAME motion-engine-contracts COMMAND MotionEngineContractTests)
endif()

if(BUILD_TESTING AND MOTION_ENGINE_BUILD_PLUGIN)
    add_executable(MotionEngineEditorTests tests/MotionEngineEditorTests.cpp
        Source/PluginEditor.cpp Source/MotionEnginePlugin.cpp Source/MotionEngineCore.cpp Source/BridgeEngine.cpp)
    target_include_directories(MotionEngineEditorTests PRIVATE Source)
    target_compile_definitions(MotionEngineEditorTests PRIVATE MOTION_ENGINE_HEADLESS_TEST=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MotionEngineEditorTests PRIVATE nullclap::nullclap juce::juce_gui_extra)
    if(WIN32)
        target_link_libraries(MotionEngineEditorTests PRIVATE ws2_32)
    endif()
    if(MSVC)
        target_compile_options(MotionEngineEditorTests PRIVATE /utf-8)
    endif()
    add_test(NAME motion-engine-editor COMMAND MotionEngineEditorTests)
endif()
