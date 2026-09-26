cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
project(J3Worship VERSION 1.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8)
endif()

add_library(j3core
  src/AudioEngine.cpp
  src/AmbientPad.cpp
  src/AudioDevice.cpp
  src/Dsp.cpp
  src/PadEngine.cpp
  src/Multitrack.cpp
  src/Scene.cpp
  src/Setlist.cpp
  src/Recording.cpp
  src/MonitorMix.cpp
  src/AutoSetup.cpp
  src/PluginCatalog.cpp
  src/DiagnosticReport.cpp
  src/Mixer.cpp
  src/Routing.cpp
  src/LiveEngine.cpp
  src/ClickEngine.cpp
  src/ClickGenerator.cpp
  src/Session.cpp
  src/Diagnostics.cpp
  src/Updater.cpp
)
target_include_directories(j3core PUBLIC include)
if (MSVC)
  target_compile_options(j3core PRIVATE /W4 /permissive-)
else()
  target_compile_options(j3core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(j3worship_core_demo src/main.cpp)
target_link_libraries(j3worship_core_demo PRIVATE j3core)

enable_testing()
add_executable(j3core_tests tests/test_core.cpp)
target_link_libraries(j3core_tests PRIVATE j3core)
add_test(NAME j3core_tests COMMAND j3core_tests)

option(J3_BUILD_DESKTOP_APP "Build the JUCE Windows desktop application" ${WIN32})
if(J3_BUILD_DESKTOP_APP)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JUCE)

  juce_add_binary_data(J3WorshipData
    SOURCES
      resources/J3WorshipLogo.png
  )

  juce_add_gui_app(J3Worship
    PRODUCT_NAME "J3 Worship"
    COMPANY_NAME "J3 Worship"
    BUNDLE_ID "com.j3worship.desktop"
    VERSION "1.6.0"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/J3WorshipLogo.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/J3WorshipLogo.png"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
  )
  juce_generate_juce_header(J3Worship)
  target_sources(J3Worship PRIVATE
    app/Main.cpp
    app/MainComponent.cpp
    app/MainComponent.h
    app/DawWorkspace.cpp
    app/DawWorkspace.h
    app/J3Theme.cpp
    app/J3Theme.h
    app/UpdateService.cpp
    app/UpdateService.h
  )
  target_include_directories(J3Worship PRIVATE include)
  target_compile_definitions(J3Worship PRIVATE
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
  )
  target_link_libraries(J3Worship PRIVATE
    j3core
    J3WorshipData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_cryptography
    winhttp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
  )
  set_target_properties(J3Worship PROPERTIES
    OUTPUT_NAME "J3Worship"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
  )

  juce_add_console_app(J3WorshipUiLayoutTests
    PRODUCT_NAME "J3 Worship UI Layout Tests"
  )
  juce_generate_juce_header(J3WorshipUiLayoutTests)
  target_sources(J3WorshipUiLayoutTests PRIVATE
    tests/test_ui_layout.cpp
    app/DawWorkspace.cpp
    app/DawWorkspace.h
  )
  target_include_directories(J3WorshipUiLayoutTests PRIVATE include)
  target_compile_definitions(J3WorshipUiLayoutTests PRIVATE
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
  )
  target_link_libraries(J3WorshipUiLayoutTests PRIVATE
    j3core
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
  )
  add_test(NAME j3ui_layout_tests COMMAND J3WorshipUiLayoutTests)
endif()
