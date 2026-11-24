cmake_minimum_required(VERSION 3.22)
project(HUSK VERSION 2.0.0 LANGUAGES C CXX)
add_compile_options(-Wno-date-time -Wno-error=date-time)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# ---- Pure C++ engine tests (no JUCE) ----
add_executable(husk_engine_tests tests/test_engine.cpp)
target_include_directories(husk_engine_tests PRIVATE src/engine tests)
add_test(NAME engine COMMAND husk_engine_tests)
# ---- Sound-quality gates on a real vocal + a generated phrase (fail the build when missed) ----
add_executable(husk_quality_tests tests/test_quality.cpp)
target_include_directories(husk_quality_tests PRIVATE src/engine tests)
add_test(NAME quality COMMAND husk_quality_tests --data ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
if(NOT MSVC)
  target_compile_options(husk_engine_tests PRIVATE -O2)
  target_compile_options(husk_quality_tests PRIVATE -O2)
endif()

option(HUSK_ENGINE_ONLY "Build dependency-free engine tests only" OFF)
if(HUSK_ENGINE_ONLY)
  return()
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Husk
  VERSION 2.0.0
  COMPANY_NAME koobacorp
  COMPANY_WEBSITE "https://github.com/koobacorp"
  BUNDLE_ID com.koobacorp.husk
  PLUGIN_MANUFACTURER_CODE Koob
  PLUGIN_CODE Husk
  FORMATS VST3
  PRODUCT_NAME HUSK
  DESCRIPTION "HUSK voice-into-instrument effect"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Pitch Shift)
target_sources(Husk PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
target_include_directories(Husk PUBLIC src/engine src/plugin)
target_compile_definitions(Husk PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_CAMERA=0 JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STRICT_REFCOUNTEDPOINTER=1)
target_link_libraries(Husk PRIVATE juce::juce_audio_utils juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
set_target_properties(Husk PROPERTIES VISIBILITY_INLINES_HIDDEN ON CXX_VISIBILITY_PRESET hidden)
if(APPLE)
  target_link_options(Husk PUBLIC -Wl,-dead_strip)
endif()

option(HUSK_BUILD_TOOLS "Build JUCE plugin tests and UI screenshots" ON)
if(HUSK_BUILD_TOOLS)
  juce_add_console_app(husk_plugin_tests PRODUCT_NAME husk_plugin_tests)
  target_sources(husk_plugin_tests PRIVATE tests/plugin_tests.cpp)
  target_include_directories(husk_plugin_tests PRIVATE src/engine src/plugin tests $<TARGET_PROPERTY:Husk,INCLUDE_DIRECTORIES>)
  target_compile_definitions(husk_plugin_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 $<TARGET_PROPERTY:Husk,COMPILE_DEFINITIONS>)
  target_link_libraries(husk_plugin_tests PRIVATE Husk juce::juce_recommended_config_flags)
  target_compile_options(Husk PRIVATE -Wno-date-time -Wno-unused-parameter -Wno-float-equal)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shots)
  add_test(NAME plugin_tests COMMAND husk_plugin_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/shots)
endif()
