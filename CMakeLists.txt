cmake_minimum_required(VERSION 3.22)

project(Oxide3216 VERSION 0.1.1 LANGUAGES C CXX)

# Beta builds: bump the version above with each set of changes. The label is what the app and the zip show.
set(OXIDE_VERSION_LABEL "${PROJECT_VERSION}-beta")

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
add_library(Oxide3216Core STATIC
    Source/AudioEngine.cpp
    Source/AudioImport.cpp
    Source/BusCompressor.cpp
    Source/ChannelStrip.cpp
    Source/Dynamics.cpp
    Source/InsertChain.cpp
    Source/PluginManager.cpp
    Source/Session.cpp
    Source/StripLink.cpp
    Source/Params.cpp
    Source/TapeMachine.cpp)
target_include_directories(Oxide3216Core PUBLIC Source "${ASIO_SDK_DIR}/common")
target_compile_definitions(Oxide3216Core PUBLIC
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_DIRECTSOUND=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="Oxide-3216"
    JUCE_APPLICATION_VERSION_STRING="${OXIDE_VERSION_LABEL}")
target_link_libraries(Oxide3216Core
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_include_directories(Oxide3216Core INTERFACE
    $<TARGET_PROPERTY:Oxide3216Core,INCLUDE_DIRECTORIES>)
target_compile_definitions(Oxide3216Core INTERFACE
    $<TARGET_PROPERTY:Oxide3216Core,COMPILE_DEFINITIONS>)

juce_add_gui_app(Oxide3216
    PRODUCT_NAME "Oxide-3216"
    COMPANY_NAME "Oxide-3216"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/oxide-3216-icon.svg"
    VERSION ${PROJECT_VERSION})

target_sources(Oxide3216 PRIVATE
    Source/ImportAudioDialog.cpp
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/PatchBay.cpp
    Source/PluginWindows.cpp
    Source/SectionWindow.cpp
    Source/ChannelStripWindow.cpp
    Source/CompressorPanel.cpp
    Source/SessionController.cpp
    Source/InsertsUI.cpp
    Source/RackWindow.cpp
    Source/MeterBridge.cpp
    Source/TapeMachineWindow.cpp
    Source/EraseDialog.cpp
    Source/TalkbackSettings.cpp
    Source/AudioSettingsComponent.cpp
    Source/BusCompPanel.cpp
    Source/Cassette.cpp
    Source/ConsoleLookAndFeel.cpp
    Source/Desk.cpp
    Source/Widgets.cpp)

target_link_libraries(Oxide3216 PRIVATE
    Oxide3216Core
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Engine tests: feed synthetic audio through the engine, no audio device needed.
add_executable(EngineTests Tests/EngineTests.cpp)
target_link_libraries(EngineTests PRIVATE
    Oxide3216Core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The Oxide 3216 logo, drawn in the top bar.
juce_add_binary_data(Oxide3216Assets SOURCES Resources/oxide-3216-logo.svg)
target_link_libraries(Oxide3216 PRIVATE Oxide3216Assets)
