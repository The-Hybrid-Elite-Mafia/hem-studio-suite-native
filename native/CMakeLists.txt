cmake_minimum_required(VERSION 3.22)
project(HEMStudioSuite VERSION 0.10.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(JUCE_PLUGINHOST_VST3=1)
option(HEM_BUILD_VST3_TEST_FIXTURE "Build the in-house VST3 test effect (never third-party scans)" OFF)
set(JUCE_PATH "" CACHE PATH "JUCE 8 source directory")
option(HEM_ENABLE_ASIO "Enable Windows ASIO with a separately supplied Steinberg SDK" OFF)
set(HEM_ASIO_SDK_PATH "" CACHE PATH "Root of the separately supplied ASIO SDK")
if(HEM_ENABLE_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "HEM_ENABLE_ASIO is supported only for a Windows target")
    endif()
    foreach(header common/asio.h host/asiodrivers.h host/pc/iasiodrv.h)
        if(NOT EXISTS "${HEM_ASIO_SDK_PATH}/${header}")
            message(FATAL_ERROR "ASIO requested but ${header} is missing. Set HEM_ASIO_SDK_PATH to the SDK root or leave HEM_ENABLE_ASIO=OFF for WASAPI.")
        endif()
    endforeach()
endif()
option(HEM_DSP_TESTS_ONLY "Build dependency-free numerical DSP tests only" OFF)
include(CTest)
# Individually runnable calibration and geometry checks.
add_executable(hem_audio_meter_tests Tests/AudioMetersTests.cpp)
add_test(NAME hem_audio_meter_tests COMMAND hem_audio_meter_tests)
add_executable(hem_control_geometry_tests Tests/ControlGeometryTests.cpp)
add_test(NAME hem_control_geometry_tests COMMAND hem_control_geometry_tests)
add_executable(hem_dsp_tests Tests/DspTests.cpp Source/DSP/DspCore.cpp)
target_include_directories(hem_dsp_tests PRIVATE Source)
add_test(NAME hem_dsp_tests COMMAND hem_dsp_tests)
add_executable(hem_specialty_tests Tests/SpecialtyTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_specialty_tests COMMAND hem_specialty_tests)
add_executable(hem_finishing_tests Tests/FinishingTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_finishing_tests COMMAND hem_finishing_tests)
add_executable(hem_master_limiter_tests Tests/MasterLimiterTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_master_limiter_tests COMMAND hem_master_limiter_tests)
add_executable(hem_monitor_router_tests Tests/MonitoringRouterTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_monitor_router_tests COMMAND hem_monitor_router_tests)
if(HEM_DSP_TESTS_ONLY)
    return()
endif()
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_PATH to the JUCE 8 source checkout")
endif()
add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
add_library(HEMCoreBaseline STATIC Source/CoreBaseline/Engine.cpp)
target_include_directories(HEMCoreBaseline PUBLIC Source/CoreBaseline)
target_compile_definitions(HEMCoreBaseline PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HEMCoreBaseline PUBLIC juce::juce_audio_formats juce::juce_dsp juce::juce_data_structures)
add_executable(hem_core_master_stage_tests Tests/CoreMasterStageTests.cpp)
target_link_libraries(hem_core_master_stage_tests PRIVATE HEMCoreBaseline)
add_test(NAME hem_core_master_stage_tests COMMAND hem_core_master_stage_tests)

juce_add_gui_app(HEMStudioSuite
    PRODUCT_NAME "Hybrid Elite Mafia - Studio Suite"
    COMPANY_NAME "HYBRID ELITE MAFIA"
    BUNDLE_ID "com.hybridelitemafia.studiosuite"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record and monitor the microphone selected in audio settings.")
juce_generate_juce_header(HEMStudioSuite)
# Render actual native control pixels in a separate test process.
juce_add_console_app(hem_hardware_control_tests PRODUCT_NAME "HEM Native Control Rendering Tests")
juce_generate_juce_header(hem_hardware_control_tests)
target_sources(hem_hardware_control_tests PRIVATE Tests/HardwareLookAndFeelTests.cpp)
target_compile_definitions(hem_hardware_control_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_hardware_control_tests PRIVATE juce::juce_gui_extra juce::juce_recommended_config_flags)
add_test(NAME hem_hardware_control_tests COMMAND hem_hardware_control_tests)
juce_add_binary_data(StudioArtwork SOURCES Assets/studio_suite_master.png Assets/ghost_brand_reference.png)
target_sources(HEMStudioSuite PRIVATE
    Source/StemFileImporter.cpp Source/StemFileImporter.h Source/StemPlayback.h
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/SessionState.h Source/DSP/MasterLimiter.h
    Source/Systems/AnalysisWorker.cpp Source/Systems/AnalysisView.cpp Source/Systems/MonitorProfile.cpp
    Source/Systems/TakeRecorder.cpp Source/Systems/LiveVocalManager.cpp Source/Systems/VocalTrackingPanel.cpp
    Source/Systems/OfflineMixRenderer.cpp Source/Systems/OfflineMixRenderer.h
    Source/Systems/OfflineMixWorker.h Source/Systems/OfflineMixPanel.h
    Source/UI/NativeWorkspace.cpp Source/UI/NativeWorkspace.h
    Source/Main.cpp Source/MainComponent.cpp Source/MainComponent.h
    Source/HardwareLookAndFeel.h Source/TrackEngine.h Source/StudioWindow.h
    Source/Plugins/FinishingProcessors.h Source/Plugins/FinishingEditors.h Source/DSP/FinishingCore.h
    Source/Plugins/PluginRegistry.h Source/Plugins/SpecialtyProcessors.h Source/Plugins/SpecialtyEditors.h
    Source/DSP/SpecialtyCore.h Source/DSP/RestorerCore.h Source/InsertChain.h Source/InsertChain.cpp
    Source/Plugins/Processors.h Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.h Source/Plugins/Editors.cpp
    Source/DSP/DspCore.h Source/DSP/DspCore.cpp)
target_compile_definitions(HEMStudioSuite PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(WIN32)
    # The recovered JUCE checkout does not bundle the Steinberg ASIO SDK.
    # WASAPI is the buildable default; ASIO is explicit and validated above.
    target_compile_definitions(HEMStudioSuite PRIVATE JUCE_ASIO=$<BOOL:${HEM_ENABLE_ASIO}> JUCE_WASAPI=1)
    if(HEM_ENABLE_ASIO)
        target_include_directories(HEMStudioSuite PRIVATE "${HEM_ASIO_SDK_PATH}/common"
            "${HEM_ASIO_SDK_PATH}/host" "${HEM_ASIO_SDK_PATH}/host/pc")
    endif()
endif()
target_link_libraries(HEMStudioSuite PRIVATE StudioArtwork HEMCoreBaseline
    juce::juce_dsp juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
# Native integration tests compile the SAME insert routing and processors.
juce_add_console_app(hem_chain_tests PRODUCT_NAME "HEM Chain Tests")
juce_generate_juce_header(hem_chain_tests)
target_sources(hem_chain_tests PRIVATE Tests/ChainTests.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_chain_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_chain_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_recommended_config_flags)
add_test(NAME hem_chain_tests COMMAND hem_chain_tests)

juce_add_console_app(hem_stem_tests PRODUCT_NAME "HEM 13 Stem Tests")
juce_generate_juce_header(hem_stem_tests)
target_sources(hem_stem_tests PRIVATE Tests/StemMixerTests.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp
    Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_stem_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_stem_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_stem_tests COMMAND hem_stem_tests)

juce_add_console_app(hem_import_tests PRODUCT_NAME "HEM WAV Import Tests")
juce_generate_juce_header(hem_import_tests)
target_sources(hem_import_tests PRIVATE Tests/ImportTests.cpp Source/StemFileImporter.cpp)
target_compile_definitions(hem_import_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_import_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_import_tests COMMAND hem_import_tests)

juce_add_console_app(hem_session_tests PRODUCT_NAME "HEM Session Tests")
juce_generate_juce_header(hem_session_tests)
target_sources(hem_session_tests PRIVATE Tests/SessionTests.cpp Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_session_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_session_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_session_tests COMMAND hem_session_tests)

juce_add_console_app(hem_analysis_tests PRODUCT_NAME "HEM Analysis Tests")
juce_generate_juce_header(hem_analysis_tests)
target_sources(hem_analysis_tests PRIVATE Tests/AnalysisTests.cpp Source/Systems/AnalysisWorker.cpp)
target_compile_definitions(hem_analysis_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_analysis_tests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
add_test(NAME hem_analysis_tests COMMAND hem_analysis_tests)

juce_add_console_app(hem_monitor_profile_tests PRODUCT_NAME "HEM Monitor Profile Tests")
juce_generate_juce_header(hem_monitor_profile_tests)
target_sources(hem_monitor_profile_tests PRIVATE Tests/MonitorProfileTests.cpp Source/Systems/MonitorProfile.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_monitor_profile_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_monitor_profile_tests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
add_test(NAME hem_monitor_profile_tests COMMAND hem_monitor_profile_tests)

juce_add_console_app(hem_take_tests PRODUCT_NAME "HEM Vocal Take Tests")
juce_generate_juce_header(hem_take_tests)
target_sources(hem_take_tests PRIVATE Tests/TakeRecorderTests.cpp Source/Systems/TakeRecorder.cpp)
target_compile_definitions(hem_take_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_take_tests PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)
add_test(NAME hem_take_tests COMMAND hem_take_tests)

juce_add_console_app(hem_vocal_pipeline_tests PRODUCT_NAME "HEM Vocal Pipeline Tests")
juce_generate_juce_header(hem_vocal_pipeline_tests)
target_sources(hem_vocal_pipeline_tests PRIVATE Tests/VocalPipelineTests.cpp Source/Systems/LiveVocalManager.cpp Source/Systems/TakeRecorder.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_vocal_pipeline_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_vocal_pipeline_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_vocal_pipeline_tests COMMAND hem_vocal_pipeline_tests)

juce_add_console_app(hem_device_input_tests PRODUCT_NAME "HEM Dynamic Input Tests")
juce_generate_juce_header(hem_device_input_tests)
target_sources(hem_device_input_tests PRIVATE Tests/DeviceInputTests.cpp Source/Systems/LiveVocalManager.cpp Source/Systems/TakeRecorder.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_device_input_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_device_input_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_device_input_tests COMMAND hem_device_input_tests)

# Offline exporter renders a private session snapshot with the same native DSP.
juce_add_console_app(hem_offline_mix_tests PRODUCT_NAME "HEM Offline Mix Tests")
juce_generate_juce_header(hem_offline_mix_tests)
target_sources(hem_offline_mix_tests PRIVATE Tests/OfflineMixTests.cpp Source/Systems/OfflineMixRenderer.cpp
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_offline_mix_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_offline_mix_tests PRIVATE HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_offline_mix_tests COMMAND hem_offline_mix_tests)

# Offline exporter renders a private session snapshot with the same native DSP.
juce_add_console_app(hem_offline_worker_tests PRODUCT_NAME "HEM Offline Worker Tests")
juce_generate_juce_header(hem_offline_worker_tests)
target_sources(hem_offline_worker_tests PRIVATE Tests/OfflineMixWorkerTests.cpp Source/Systems/OfflineMixRenderer.cpp
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_offline_worker_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_offline_worker_tests PRIVATE HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_offline_worker_tests COMMAND hem_offline_worker_tests)

juce_add_console_app(hem_studio_branding_tests PRODUCT_NAME "HEM Studio Branding Tests")
juce_generate_juce_header(hem_studio_branding_tests)
target_sources(hem_studio_branding_tests PRIVATE Tests/StudioBrandingTests.cpp)
target_compile_definitions(hem_studio_branding_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_studio_branding_tests PRIVATE StudioArtwork juce::juce_gui_extra juce::juce_recommended_config_flags)
add_test(NAME hem_studio_branding_tests COMMAND hem_studio_branding_tests)

# Test the real host command/lifecycle paths, not just serialization helpers.
get_target_property(HEM_NATIVE_TEST_SOURCES HEMStudioSuite SOURCES)
list(FILTER HEM_NATIVE_TEST_SOURCES INCLUDE REGEX "^Source/.*\\.cpp$")
list(REMOVE_ITEM HEM_NATIVE_TEST_SOURCES Source/Main.cpp)
juce_add_console_app(hem_native_lifecycle_tests PRODUCT_NAME "HEM Native Host Lifecycle Tests")
juce_generate_juce_header(hem_native_lifecycle_tests)
target_sources(hem_native_lifecycle_tests PRIVATE Tests/NativeHostLifecycleTests.cpp ${HEM_NATIVE_TEST_SOURCES})
target_compile_definitions(hem_native_lifecycle_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_native_lifecycle_tests PRIVATE StudioArtwork HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_native_lifecycle_tests COMMAND hem_native_lifecycle_tests)

juce_add_console_app(hem_native_workspace_tests PRODUCT_NAME "HEM Native Workspace Tests")
juce_generate_juce_header(hem_native_workspace_tests)
target_sources(hem_native_workspace_tests PRIVATE Tests/NativeWorkspaceTests.cpp Source/UI/NativeWorkspace.cpp
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_native_workspace_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_native_workspace_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_native_workspace_tests COMMAND hem_native_workspace_tests)

juce_add_console_app(hem_processor_binding_tests PRODUCT_NAME "HEM Native Parameter Binding Tests")
juce_generate_juce_header(hem_processor_binding_tests)
target_sources(hem_processor_binding_tests PRIVATE Tests/ProcessorControlBindingTests.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_processor_binding_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_processor_binding_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_processor_binding_tests COMMAND hem_processor_binding_tests)

juce_add_console_app(hem_plugin_meter_tests PRODUCT_NAME "HEM Plugin Meter Display Tests")
juce_generate_juce_header(hem_plugin_meter_tests)
target_sources(hem_plugin_meter_tests PRIVATE Tests/PluginMeterDisplayTests.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_plugin_meter_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_plugin_meter_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_plugin_meter_tests COMMAND hem_plugin_meter_tests)

# External-instance ownership is retained by the message-thread host service.
# All processor-bearing targets use the same implementation and JUCE host mode.
foreach(target HEMStudioSuite hem_chain_tests hem_stem_tests hem_session_tests
 hem_vocal_pipeline_tests hem_device_input_tests hem_offline_mix_tests hem_offline_worker_tests
 hem_native_lifecycle_tests hem_native_workspace_tests hem_processor_binding_tests hem_plugin_meter_tests)
 target_sources(${target} PRIVATE Source/Plugins/ExternalPluginHost.cpp)
endforeach()
if(HEM_BUILD_VST3_TEST_FIXTURE)
 add_subdirectory(Tests/VST3Fixture)
endif()

juce_add_console_app(hem_external_session_tests PRODUCT_NAME "HEM External Session Tests")
juce_generate_juce_header(hem_external_session_tests)
target_sources(hem_external_session_tests PRIVATE Tests/ExternalSessionTests.cpp Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/Plugins/ExternalPluginHost.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_external_session_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_session_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_external_session_tests COMMAND hem_external_session_tests)

juce_add_console_app(hem_external_integration_tests PRODUCT_NAME "HEM External Host Integration Tests")
juce_generate_juce_header(hem_external_integration_tests)
target_sources(hem_external_integration_tests PRIVATE Tests/ExternalIntegrationTests.cpp ${HEM_NATIVE_TEST_SOURCES})
target_compile_definitions(hem_external_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_integration_tests PRIVATE StudioArtwork HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
# This target intentionally requires an explicit test-owned bundle argument.
# It is not registered as an implicit system plug-in scan.

juce_add_console_app(hem_external_host_tests PRODUCT_NAME "HEM External VST3 Adapter Tests")
juce_generate_juce_header(hem_external_host_tests)
target_sources(hem_external_host_tests PRIVATE Tests/ExternalHostTests.cpp Source/InsertChain.cpp Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/Editors.cpp Source/Plugins/ExternalPluginHost.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_external_host_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_host_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
set(HEM_TEST_VST3_BUNDLE "" CACHE PATH "Explicit in-house fixture bundle for external-host tests; never a scan directory")
if(HEM_TEST_VST3_BUNDLE)
 add_test(NAME hem_external_host_tests COMMAND hem_external_host_tests "${HEM_TEST_VST3_BUNDLE}")
 add_test(NAME hem_external_integration_tests COMMAND hem_external_integration_tests "${HEM_TEST_VST3_BUNDLE}")
endif()
