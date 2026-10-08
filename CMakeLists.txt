cmake_minimum_required(VERSION 3.22)

project(ConsoleTape VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the .exe runs on another PC without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# Steinberg ASIO SDK (licensed separately, not in git). See BRIEF.md.
set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/ASIOSDK" CACHE PATH "Folder containing the ASIO SDK (with common/iasiodrv.h)")
if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    message(FATAL_ERROR
        "ASIO SDK not found. Download it from https://www.steinberg.net/developers/ "
        "and unzip it so that ${ASIO_SDK_DIR}/common/iasiodrv.h exists.")
endif()

# Code shared by the app and the tests. JUCE modules are compiled once, into this
# library (linked PRIVATE); their include paths and defines are re-exported below.
add_library(ConsoleTapeCore STATIC
    Source/AudioEngine.cpp)
target_include_directories(ConsoleTapeCore PUBLIC Source "${ASIO_SDK_DIR}/common")
target_compile_definitions(ConsoleTapeCore PUBLIC
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="ConsoleTape"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ConsoleTapeCore
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_include_directories(ConsoleTapeCore INTERFACE
    $<TARGET_PROPERTY:ConsoleTapeCore,INCLUDE_DIRECTORIES>)
target_compile_definitions(ConsoleTapeCore INTERFACE
    $<TARGET_PROPERTY:ConsoleTapeCore,COMPILE_DEFINITIONS>)

juce_add_gui_app(ConsoleTape
    PRODUCT_NAME "ConsoleTape"
    COMPANY_NAME "ConsoleTape"
    VERSION ${PROJECT_VERSION})

target_sources(ConsoleTape PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioSettingsComponent.cpp
    Source/ConsoleLookAndFeel.cpp)

target_link_libraries(ConsoleTape PRIVATE
    ConsoleTapeCore
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Engine tests: feed synthetic audio through the engine, no audio device needed.
add_executable(EngineTests Tests/EngineTests.cpp)
target_link_libraries(EngineTests PRIVATE
    ConsoleTapeCore
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
