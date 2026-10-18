cmake_minimum_required(VERSION 3.22)

# This directory may be built on its own or added after the host loads JUCE.
# It never installs a plugin, scans a plugin directory, or downloads dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HEMVST3Fixture VERSION 1.0.0 LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(CTest)
    option(HEM_BUILD_VST3_TEST_FIXTURE "Build the in-house VST3 integration fixture" ON)
else()
    option(HEM_BUILD_VST3_TEST_FIXTURE "Build the in-house VST3 integration fixture" OFF)
endif()

if(NOT HEM_BUILD_VST3_TEST_FIXTURE)
    return()
endif()

if(NOT TARGET juce::juce_audio_processors)
    set(JUCE_PATH "" CACHE PATH "Existing official JUCE 8.0.10 source checkout")
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "Set JUCE_PATH to the existing JUCE 8.0.10 checkout. No downloads are performed.")
    endif()
    add_subdirectory("${JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
endif()

juce_add_plugin(HEMVST3TestFixture
    VERSION 1.0.0
    COMPANY_NAME "HYBRID ELITE MAFIA"
    BUNDLE_ID "com.hybridelitemafia.vst3testfixture"
    PLUGIN_MANUFACTURER_CODE Hema
    PLUGIN_CODE Hfx1
    PRODUCT_NAME "HEM VST3 Fixture"
    DESCRIPTION "In-house deterministic gain and latency test fixture"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)
target_sources(HEMVST3TestFixture PRIVATE FixtureProcessor.cpp FixtureProcessor.h)
target_compile_features(HEMVST3TestFixture PUBLIC cxx_std_20)
target_compile_definitions(HEMVST3TestFixture PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(HEMVST3TestFixture PRIVATE
    juce::juce_audio_processors juce::juce_recommended_config_flags)

if(BUILD_TESTING)
    # A normal console executable avoids conflicting JUCE_STANDALONE_APPLICATION
    # definitions from juce_add_console_app and the plugin's shared-code target.
    add_executable(hem_vst3_fixture_tests)
    target_sources(hem_vst3_fixture_tests PRIVATE FixtureSelfTests.cpp)
    target_link_libraries(hem_vst3_fixture_tests PRIVATE
        HEMVST3TestFixture juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_test(NAME hem_vst3_fixture_tests COMMAND hem_vst3_fixture_tests)
endif()
