cmake_minimum_required(VERSION 3.24)
project(MotionEngine VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CTest)

# Pin the exact null-clap revision validated for raw MIDI ports, process context,
# bounds-safe event handling, writable-parameter state restore, and physical-pixel
# GUI sizing on scaled Win32 hosts.
FetchContent_Declare(
    null_clap
    GIT_REPOSITORY https://github.com/001null100/null-clap.git
    GIT_TAG 8612a19fbe7fbe81b249c22e1593da18bb998b97
    GIT_SHALLOW FALSE
)
set(NULLCLAP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NULLCLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(null_clap)

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
