cmake_minimum_required(VERSION 3.22)
project(DustyTable VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DUSTYTABLE_BUILD_PLUGIN "Build the JUCE plugin (otherwise only the DSP and the offline renderer)" ON)
set(DUSTYTABLE_JUCE_PATH "" CACHE PATH "Optional local JUCE source directory (8.0.x)")

# Physical model, independent of JUCE.
add_library(DustyTableDSP STATIC Source/dsp/VinylEngine.cpp Source/dsp/Mechanics.cpp)
target_include_directories(DustyTableDSP PUBLIC Source)
set_target_properties(DustyTableDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(DustyTableDSP PRIVATE /W4 /utf-8 /fp:fast)
else()
  target_compile_options(DustyTableDSP PRIVATE -Wall -Wextra)
endif()

add_executable(DustyTableRender Tools/Render.cpp)
target_link_libraries(DustyTableRender PRIVATE DustyTableDSP)

if(DUSTYTABLE_BUILD_PLUGIN)
  if(DUSTYTABLE_JUCE_PATH)
    add_subdirectory("${DUSTYTABLE_JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()

  juce_add_plugin(DustyTable
    COMPANY_NAME "dustytable"
    # The codes are kept from the earlier name (PhysVinyl) so that existing host sessions still find the plugin.
    PLUGIN_MANUFACTURER_CODE Pvnl
    PLUGIN_CODE Pvn1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DustyTable"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(DustyTable PRIVATE
    Source/DustyTableProcessor.cpp
    Source/DustyTableEditor.cpp)

  target_compile_definitions(DustyTable PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

  if(MSVC)
    target_compile_options(DustyTable PRIVATE /utf-8)
  endif()

  target_link_libraries(DustyTable
    PRIVATE
      DustyTableDSP
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endif()
