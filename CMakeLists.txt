cmake_minimum_required(VERSION 3.22)
# must come before project(): the oldest macOS the plug-in runs on, and a universal binary
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(StringsArranger VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STRINGS_BUILD_PLUGIN "Build the JUCE plug-in (AU/VST3/Standalone)" ON)
set(STRINGS_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# ---- engine: plain C++17, no JUCE --------------------------------------------
# The four-voice search comes from the choir engine (namespace choir); core/Strings.cpp is the string layer on top.
add_library(strings_core STATIC
    core/Analysis.cpp core/Chordal.cpp core/Gregorian.cpp core/Smf.cpp core/Notation.cpp core/Harmonize.cpp
    core/Strings.cpp)
target_include_directories(strings_core PUBLIC core)
set_target_properties(strings_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# identical floating-point results on every platform (the engine is validated against the prototype)
if(MSVC)
    target_compile_options(strings_core PRIVATE /fp:precise)
else()
    target_compile_options(strings_core PRIVATE -ffp-contract=off)
endif()

add_executable(strings_cli cli/strings_main.cpp)
target_link_libraries(strings_cli PRIVATE strings_core)
# the skeleton engine alone, with the choir's options: tests/compare_golden.py holds it to the reference
add_executable(skeleton_cli cli/skeleton_main.cpp)
target_link_libraries(skeleton_cli PRIVATE strings_core)

# ---- plug-in ----------------------------------------------------------------
if(STRINGS_BUILD_PLUGIN)
    if(STRINGS_JUCE_PATH)
        add_subdirectory(${STRINGS_JUCE_PATH} juce)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON
            # lets the plug-in window accept regions dragged from Logic (file promises)
            PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-8.0.4-file-promises.patch
                          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake
            UPDATE_DISCONNECTED ON)
        FetchContent_MakeAvailable(juce)
    endif()

    juce_add_plugin(StringsArranger
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.stringsarranger"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE Strg
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "Strings Arranger"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Tools)

    if(APPLE)
        set(STRINGS_CLIPBOARD plugin/Clipboard_mac.mm)
    else()
        set(STRINGS_CLIPBOARD plugin/Clipboard.cpp)
    endif()
    target_sources(StringsArranger PRIVATE
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp ${STRINGS_CLIPBOARD})
    target_compile_definitions(StringsArranger PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(StringsArranger
        PRIVATE strings_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)

    # Strings Arranger Live: the MIDI FX (Logic: MIDI FX slot -> Audio Units -> Teilormusic)
    juce_add_plugin(StringsArrangerLive
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.stringsarrangerlive"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE StLv
        FORMATS AU VST3
        PRODUCT_NAME "Strings Arranger Live"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
        VST3_CATEGORIES Fx Tools)
    target_sources(StringsArrangerLive PRIVATE
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp)
    target_compile_definitions(StringsArrangerLive PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(StringsArrangerLive
        PRIVATE strings_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endif()

# ---- plug-in tests: capture, state, preview and editor snapshots (needs a display) -------
option(STRINGS_BUILD_TESTS "Build the plug-in test runner" OFF)
if(STRINGS_BUILD_PLUGIN AND STRINGS_BUILD_TESTS)
    juce_add_console_app(StringsPluginTests PRODUCT_NAME "StringsPluginTests")
    target_sources(StringsPluginTests PRIVATE tests/plugin_test.cpp
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp ${STRINGS_CLIPBOARD})
    target_include_directories(StringsPluginTests PRIVATE plugin)
    target_compile_definitions(StringsPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Strings Arranger" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        STRINGS_NO_PLUGIN_ENTRY=1)
    target_link_libraries(StringsPluginTests PRIVATE strings_core juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
