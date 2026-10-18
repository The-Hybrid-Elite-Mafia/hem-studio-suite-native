cmake_minimum_required(VERSION 3.22)
project(HEMStudioSuite VERSION 0.11.5 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(JUCE_PLUGINHOST_VST3=1)
option(HEM_BUILD_VST3_TEST_FIXTURE "Build the in-house VST3 test effect (never third-party scans)" OFF)
set(JUCE_PATH "" CACHE PATH "JUCE 8 source directory")
# Public repo layout: Dependencies/ sits next to native/. Used when no path is given.
if(NOT JUCE_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/JUCE-8.0.10/CMakeLists.txt")
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/JUCE-8.0.10" CACHE PATH "JUCE 8 source directory" FORCE)
endif()
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
add_executable(hem_reduction_history_tests Tests/ReductionHistoryTests.cpp)
add_test(NAME hem_reduction_history_tests COMMAND hem_reduction_history_tests)
add_executable(hem_foundation_telemetry_tests Tests/FoundationTelemetryTests.cpp)
add_test(NAME hem_foundation_telemetry_tests COMMAND hem_foundation_telemetry_tests)
add_executable(hem_foundation_tests Tests/FoundationTests.cpp)
add_test(NAME hem_foundation_tests COMMAND hem_foundation_tests)
set_tests_properties(hem_foundation_tests PROPERTIES TIMEOUT 1800)
# Individually runnable calibration and geometry checks.
add_executable(hem_audio_meter_tests Tests/AudioMetersTests.cpp)
add_test(NAME hem_audio_meter_tests COMMAND hem_audio_meter_tests)
find_package(Threads REQUIRED)
add_executable(hem_audio_meter_publication_tests Tests/AudioMetersPublicationTests.cpp)
target_link_libraries(hem_audio_meter_publication_tests PRIVATE Threads::Threads)
add_test(NAME hem_audio_meter_publication_tests COMMAND hem_audio_meter_publication_tests)
add_executable(hem_neutral_mapping_tests Tests/NeutralMappingTests.cpp)
add_test(NAME hem_neutral_mapping_tests COMMAND hem_neutral_mapping_tests)
add_executable(hem_control_geometry_tests Tests/ControlGeometryTests.cpp)
add_test(NAME hem_control_geometry_tests COMMAND hem_control_geometry_tests)
add_executable(hem_utility_panel_sizing_tests Tests/UtilityPanelSizingTests.cpp)
add_test(NAME hem_utility_panel_sizing_tests COMMAND hem_utility_panel_sizing_tests)
add_executable(hem_instrumental_core_tests Tests/InstrumentalCoreTests.cpp)
add_test(NAME hem_instrumental_core_tests COMMAND hem_instrumental_core_tests)
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
add_executable(hem_analysis_plugin_tests Tests/AnalysisPluginTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_analysis_plugin_tests COMMAND hem_analysis_plugin_tests)
add_executable(hem_segmented_meter_model_tests Tests/SegmentedMeterModelTests.cpp)
add_test(NAME hem_segmented_meter_model_tests COMMAND hem_segmented_meter_model_tests)
add_executable(hem_algorithmic_reverb_tests Tests/AlgorithmicReverbTests.cpp)
add_test(NAME hem_algorithmic_reverb_tests COMMAND hem_algorithmic_reverb_tests)
add_executable(hem_reverb_scheduler_tests Tests/ReverbSchedulerTests.cpp)
add_test(NAME hem_reverb_scheduler_tests COMMAND hem_reverb_scheduler_tests)
add_executable(hem_modulation_tests Tests/ModulationTests.cpp)
add_test(NAME hem_modulation_tests COMMAND hem_modulation_tests)
set_tests_properties(hem_algorithmic_reverb_tests hem_reverb_scheduler_tests hem_modulation_tests PROPERTIES TIMEOUT 600)
add_executable(hem_delay_core_tests Tests/DelayCoreTests.cpp)
add_test(NAME hem_delay_core_tests COMMAND hem_delay_core_tests)
set_tests_properties(hem_delay_core_tests PROPERTIES TIMEOUT 1800)
add_executable(hem_band_module_tests Tests/BandModuleTests.cpp)
add_test(NAME hem_band_module_tests COMMAND hem_band_module_tests)
add_executable(hem_band_bank_tests Tests/BandBankTests.cpp)
add_test(NAME hem_band_bank_tests COMMAND hem_band_bank_tests)
set_tests_properties(hem_band_module_tests hem_band_bank_tests PROPERTIES TIMEOUT 7200)
add_executable(hem_restoration_core_tests Tests/RestorationCoreTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_restoration_core_tests COMMAND hem_restoration_core_tests)
set_tests_properties(hem_restoration_core_tests PROPERTIES TIMEOUT 1800)
add_executable(hem_clip_repair_core_tests Tests/ClipRepairCoreTests.cpp)
add_test(NAME hem_clip_repair_core_tests COMMAND hem_clip_repair_core_tests)
set_tests_properties(hem_clip_repair_core_tests PROPERTIES TIMEOUT 1800)
add_executable(hem_nonlinear_tests Tests/NonlinearTests.cpp Source/DSP/DspCore.cpp)
add_test(NAME hem_nonlinear_tests COMMAND hem_nonlinear_tests)
set_tests_properties(hem_nonlinear_tests PROPERTIES TIMEOUT 1800)
add_executable(hem_legacy_shaper_parity_tests Tests/LegacyShaperParityTests.cpp)
add_test(NAME hem_legacy_shaper_parity_tests COMMAND hem_legacy_shaper_parity_tests)
# Missing-engine core gates are isolated from registry/editor completion.
add_executable(hem_drum_replacer_core_tests Tests/DrumReplacerCoreTests.cpp)
add_executable(hem_reference_match_core_tests Tests/ReferenceMatchCoreTests.cpp Source/DSP/ReferenceMatchCore.cpp)
add_executable(hem_amp_cabinet_core_tests Tests/AmpCabinetTests.cpp Source/DSP/AmpCabinetCore.cpp Source/DSP/DspCore.cpp)
foreach(target IN ITEMS hem_drum_replacer_core_tests hem_reference_match_core_tests hem_amp_cabinet_core_tests)
    # These test oracles use assert; Release must not silently disable them.
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 180)
endforeach()

if(HEM_DSP_TESTS_ONLY)
    return()
endif()
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_PATH to the JUCE 8 source checkout")
endif()
add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
# Prepared spectral core regressions; execution deferred to the user's PC.
add_executable(hem_spectral_core_tests Tests/SpectralCoreTests.cpp)
target_compile_definitions(hem_spectral_core_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_spectral_core_tests PRIVATE juce::juce_dsp Threads::Threads)
add_test(NAME hem_spectral_core_tests COMMAND hem_spectral_core_tests)
# Approved isolated MIT SDK substitution; app/UI/DSP sources unchanged.
set(HEM_MIT_VST3_SDK_PATH "" CACHE PATH "Pinned official MIT VST3 SDK")
if(NOT HEM_MIT_VST3_SDK_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/vst3sdk/pluginterfaces/vst/vsttypes.h")
    set(HEM_MIT_VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/vst3sdk" CACHE PATH "Pinned official MIT VST3 SDK" FORCE)
endif()
if(NOT EXISTS "${HEM_MIT_VST3_SDK_PATH}/pluginterfaces/vst/vsttypes.h")
 message(FATAL_ERROR "Explicit pinned MIT VST3 SDK is required")
endif()
juce_set_vst3_sdk_path("${HEM_MIT_VST3_SDK_PATH}")
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
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/HEM-Icon-1024.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record and monitor the microphone selected in audio settings.")
# JUCE 8.0.10's resource rule omits Info.txt as a dependency. Without this,
# an incremental version bump leaves the Windows FileVersion resource stale.
if(WIN32)
    get_target_property(hem_generated_dir HEMStudioSuite JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(hem_info_file HEMStudioSuite JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${hem_generated_dir}/HEMStudioSuite_resources.rc"
        APPEND DEPENDS "${hem_info_file}")
    # Include a numeric PRODUCTVERSION as well as FILEVERSION for installer
    # identity checks; the stock JUCE resource provides only the latter.
    configure_file(Source/WindowsVersion.rc.in "${hem_generated_dir}/WindowsVersion.rc" @ONLY)
    target_compile_definitions(HEMStudioSuite_rc_lib PRIVATE
        JUCE_USER_DEFINED_RC_FILE="${hem_generated_dir}/WindowsVersion.rc")
    set_property(SOURCE "${hem_generated_dir}/HEMStudioSuite_resources.rc"
        APPEND PROPERTY OBJECT_DEPENDS "${hem_generated_dir}/WindowsVersion.rc")
endif()
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
    Source/Plugins/Processors.h Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
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
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_chain_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_chain_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_recommended_config_flags)
add_test(NAME hem_chain_tests COMMAND hem_chain_tests)

juce_add_console_app(hem_stem_tests PRODUCT_NAME "HEM 13 Stem Tests")
juce_generate_juce_header(hem_stem_tests)
target_sources(hem_stem_tests PRIVATE Tests/StemMixerTests.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp
    Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
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
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
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
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_vocal_pipeline_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_vocal_pipeline_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_vocal_pipeline_tests COMMAND hem_vocal_pipeline_tests)

juce_add_console_app(hem_device_input_tests PRODUCT_NAME "HEM Dynamic Input Tests")
juce_generate_juce_header(hem_device_input_tests)
target_sources(hem_device_input_tests PRIVATE Tests/DeviceInputTests.cpp Source/Systems/LiveVocalManager.cpp Source/Systems/TakeRecorder.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_device_input_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_device_input_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_device_input_tests COMMAND hem_device_input_tests)

# Offline exporter renders a private session snapshot with the same native DSP.
juce_add_console_app(hem_offline_mix_tests PRODUCT_NAME "HEM Offline Mix Tests")
juce_generate_juce_header(hem_offline_mix_tests)
target_sources(hem_offline_mix_tests PRIVATE Tests/OfflineMixTests.cpp Source/Systems/OfflineMixRenderer.cpp
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_offline_mix_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_offline_mix_tests PRIVATE HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_offline_mix_tests COMMAND hem_offline_mix_tests)

# Offline exporter renders a private session snapshot with the same native DSP.
juce_add_console_app(hem_offline_worker_tests PRODUCT_NAME "HEM Offline Worker Tests")
juce_generate_juce_header(hem_offline_worker_tests)
target_sources(hem_offline_worker_tests PRIVATE Tests/OfflineMixWorkerTests.cpp Source/Systems/OfflineMixRenderer.cpp
    Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
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
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_native_workspace_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_native_workspace_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_native_workspace_tests COMMAND hem_native_workspace_tests)

juce_add_console_app(hem_processor_binding_tests PRODUCT_NAME "HEM Native Parameter Binding Tests")
juce_generate_juce_header(hem_processor_binding_tests)
target_sources(hem_processor_binding_tests PRIVATE Tests/ProcessorControlBindingTests.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
    Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_processor_binding_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_processor_binding_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_processor_binding_tests COMMAND hem_processor_binding_tests)

juce_add_console_app(hem_plugin_meter_tests PRODUCT_NAME "HEM Plugin Meter Display Tests")
juce_generate_juce_header(hem_plugin_meter_tests)
target_sources(hem_plugin_meter_tests PRIVATE Tests/PluginMeterDisplayTests.cpp
    Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
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
 # JUCE 8.0.10's manifest helper hardcodes the bundled SDK in ten includes.
 # Redirect a build-local copy only; preserve the installed JUCE source tree.
 if(TARGET HEMVST3TestFixture_vst3_helper)
  file(READ "${JUCE_PATH}/modules/juce_audio_plugin_client/VST3/juce_VST3ManifestHelper.cpp" HEM_MANIFEST_HELPER)
  string(REPLACE "juce_audio_processors/format_types/VST3_SDK/" "" HEM_MANIFEST_HELPER "${HEM_MANIFEST_HELPER}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hem_MIT_VST3ManifestHelper.cpp" "${HEM_MANIFEST_HELPER}")
  set_property(TARGET HEMVST3TestFixture_vst3_helper PROPERTY SOURCES "${CMAKE_CURRENT_BINARY_DIR}/hem_MIT_VST3ManifestHelper.cpp")
  target_include_directories(HEMVST3TestFixture_vst3_helper BEFORE PRIVATE "${HEM_MIT_VST3_SDK_PATH}" "${JUCE_PATH}/modules/juce_audio_plugin_client/VST3")
 endif()
endif()

juce_add_console_app(hem_external_session_tests PRODUCT_NAME "HEM External Session Tests")
juce_generate_juce_header(hem_external_session_tests)
target_sources(hem_external_session_tests PRIVATE Tests/ExternalSessionTests.cpp Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/InsertChain.cpp Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/Plugins/ExternalPluginHost.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_external_session_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_session_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_external_session_tests COMMAND hem_external_session_tests)

juce_add_console_app(hem_external_integration_tests PRODUCT_NAME "HEM External Host Integration Tests")
juce_generate_juce_header(hem_external_integration_tests)
target_sources(hem_external_integration_tests PRIVATE Tests/ExternalIntegrationTests.cpp ${HEM_NATIVE_TEST_SOURCES}
    Source/Plugins/ExternalPluginHost.cpp)
target_compile_definitions(hem_external_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_integration_tests PRIVATE StudioArtwork HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
# This target intentionally requires an explicit test-owned bundle argument.
# It is not registered as an implicit system plug-in scan.

juce_add_console_app(hem_external_host_tests PRODUCT_NAME "HEM External VST3 Adapter Tests")
juce_generate_juce_header(hem_external_host_tests)
target_sources(hem_external_host_tests PRIVATE Tests/ExternalHostTests.cpp Source/InsertChain.cpp Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/Plugins/ExternalPluginHost.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_external_host_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_external_host_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
set(HEM_TEST_VST3_BUNDLE "" CACHE PATH "Explicit in-house fixture bundle for external-host tests; never a scan directory")
if(HEM_TEST_VST3_BUNDLE)
 add_test(NAME hem_external_host_tests COMMAND hem_external_host_tests "${HEM_TEST_VST3_BUNDLE}")
 add_test(NAME hem_external_integration_tests COMMAND hem_external_integration_tests "${HEM_TEST_VST3_BUNDLE}")
endif()

# UI-only immutable raster resources; no image work runs in audio callbacks.
juce_add_binary_data(OriginalArtwork HEADER_NAME OriginalArt.h NAMESPACE OriginalArt SOURCES Assets/native_sipa.png Assets/native_transient.png Assets/native_tape.png Assets/original_vocal.jpg Assets/original_subbass.jpg)
juce_add_binary_data(ControlArtwork HEADER_NAME ControlArt.h NAMESPACE ControlArt SOURCES
 Assets/foundation_dc.png Assets/foundation_gate.png Assets/foundation_rider.png Assets/foundation_plosive.png Assets/foundation_hum.png
 Assets/Controls/module_faceplate.png Assets/Controls/dial_body.png Assets/Controls/button_states.png
 Assets/Controls/host_clean_regions.png Assets/Controls/Reference/approved_module_reference.png Assets/Controls/Reference/reviewed_hardware_reference.png)
juce_add_console_app(hem_photorealistic_editor_tests PRODUCT_NAME "HEM Photorealistic Editor Tests")
juce_generate_juce_header(hem_photorealistic_editor_tests)
target_sources(hem_photorealistic_editor_tests PRIVATE Tests/PhotorealisticEditorTests.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp Source/Plugins/FoundationProcessors.cpp
 Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_photorealistic_editor_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_photorealistic_editor_tests PRIVATE StudioArtwork juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_photorealistic_editor_tests COMMAND hem_photorealistic_editor_tests)
juce_add_console_app(hem_foundation_integration_tests PRODUCT_NAME "HEM Foundation Integration Tests")
juce_generate_juce_header(hem_foundation_integration_tests)
target_sources(hem_foundation_integration_tests PRIVATE Tests/FoundationIntegrationTests.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
 Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/Plugins/ExternalPluginHost.cpp
 Source/DSP/DspCore.cpp Source/InsertChain.cpp Source/SessionState.cpp Source/Systems/PresetManager.cpp Source/Systems/OfflineMixRenderer.cpp)
target_compile_definitions(hem_foundation_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_foundation_integration_tests PRIVATE HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_foundation_integration_tests COMMAND hem_foundation_integration_tests)
juce_add_console_app(hem_foundation_editor_tests PRODUCT_NAME "HEM Foundation Original Artwork Tests")
juce_generate_juce_header(hem_foundation_editor_tests)
target_sources(hem_foundation_editor_tests PRIVATE Tests/FoundationEditorTests.cpp Source/UI/NativeWorkspace.cpp Source/InsertChain.cpp
 Source/Systems/VocalTrackingPanel.cpp Source/Systems/LiveVocalManager.cpp Source/Systems/TakeRecorder.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
 Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_foundation_editor_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_foundation_editor_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_foundation_editor_tests COMMAND hem_foundation_editor_tests)
juce_add_console_app(hem_vocal_spectrum_tests PRODUCT_NAME "HEM Vocal Spectrum Tests")
juce_generate_juce_header(hem_vocal_spectrum_tests)
target_sources(hem_vocal_spectrum_tests PRIVATE Tests/VocalSpectrumTests.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
 Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_vocal_spectrum_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_vocal_spectrum_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_vocal_spectrum_tests COMMAND hem_vocal_spectrum_tests)
juce_add_console_app(hem_plugin_telemetry_tests PRODUCT_NAME "HEM Plugin Telemetry Tests")
juce_generate_juce_header(hem_plugin_telemetry_tests)
target_sources(hem_plugin_telemetry_tests PRIVATE Tests/PluginTelemetryTests.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
 Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_plugin_telemetry_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_plugin_telemetry_tests PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_plugin_telemetry_tests COMMAND hem_plugin_telemetry_tests)
juce_add_console_app(hem_suite_visual_review PRODUCT_NAME "HEM All 17 Visual Review")
juce_generate_juce_header(hem_suite_visual_review)
target_sources(hem_suite_visual_review PRIVATE Tests/SuiteVisualReview.cpp
 Source/Plugins/Processors.cpp Source/Plugins/SpecialtyProcessors.cpp Source/Plugins/FinishingProcessors.cpp
 Source/Plugins/FoundationProcessors.cpp Source/Plugins/Editors.cpp Source/DSP/DspCore.cpp)
target_compile_definitions(hem_suite_visual_review PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_suite_visual_review PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME hem_suite_visual_review COMMAND hem_suite_visual_review)
get_property(HEM_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(HEM_TARGET IN LISTS HEM_ALL_TARGETS)
 get_target_property(HEM_TARGET_SOURCES ${HEM_TARGET} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST HEM_TARGET_SOURCES)
  target_link_libraries(${HEM_TARGET} PRIVATE ControlArtwork OriginalArtwork juce::juce_dsp)
 endif()
endforeach()

# Cloud recovered integration002: original32 faceplates, analysis lane and tested new meter helper.
juce_add_binary_data(ApprovedArtwork HEADER_NAME ApprovedArt.h NAMESPACE ApprovedArt SOURCES
 Assets/Approved/approved_00.png
 Assets/Approved/approved_01.png
 Assets/Approved/approved_02.png
 Assets/Approved/approved_03.png
 Assets/Approved/approved_04.png
 Assets/Approved/approved_05.png
 Assets/Approved/approved_06.png
 Assets/Approved/approved_07.png
 Assets/Approved/approved_08.png
 Assets/Approved/approved_09.png
 Assets/Approved/approved_10.png
 Assets/Approved/approved_11.png
 Assets/Approved/approved_12.png
 Assets/Approved/approved_13.png
 Assets/Approved/approved_14.png
 Assets/Approved/approved_15.png
 Assets/Approved/approved_16.png
 Assets/Approved/approved_17.png
 Assets/Approved/approved_18.png
 Assets/Approved/approved_19.png
 Assets/Approved/approved_20.png
 Assets/Approved/approved_21.png
 Assets/Approved/approved_22.png
 Assets/Approved/approved_23.png
 Assets/Approved/approved_24.png
 Assets/Approved/approved_25.png
 Assets/Approved/approved_26.png
 Assets/Approved/approved_27.png
 Assets/Approved/approved_28.png
 Assets/Approved/approved_29.png
 Assets/Approved/approved_30.png
 Assets/Approved/approved_31.png
)
foreach(pair IN ITEMS "hem_analysis_plugin_binding_tests|AnalysisPluginBindingTests" "hem_analysis_plugin_editor_tests|AnalysisPluginEditorTests" "hem_reverb_plugin_binding_tests|ReverbPluginBindingTests" "hem_reverb_plugin_editor_tests|ReverbPluginEditorTests" "hem_modulation_plugin_binding_tests|ModulationPluginBindingTests" "hem_modulation_plugin_editor_tests|ModulationPluginEditorTests")
 string(REPLACE "|" ";" fields "${pair}")
 list(GET fields 0 test_target)
 list(GET fields 1 test_file)
 juce_add_console_app(${test_target} PRODUCT_NAME "${test_file}")
 juce_generate_juce_header(${test_target})
 target_sources(${test_target} PRIVATE "Tests/${test_file}.cpp" ${HEM_NATIVE_TEST_SOURCES} Source/Plugins/ExternalPluginHost.cpp)
 target_compile_definitions(${test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
 target_link_libraries(${test_target} PRIVATE StudioArtwork HEMCoreBaseline juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
 add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
get_property(HEM_INTEGRATED_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(HEM_TARGET IN LISTS HEM_INTEGRATED_TARGETS)
 get_target_property(HEM_TARGET_SOURCES ${HEM_TARGET} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST HEM_TARGET_SOURCES)
  target_sources(${HEM_TARGET} PRIVATE Source/Plugins/AnalysisProcessors.cpp Source/Plugins/ReverbProcessors.cpp Source/Plugins/ModulationProcessors.cpp Source/Plugins/DelayProcessors.cpp Source/Plugins/DelayEditors.cpp)
  target_link_libraries(${HEM_TARGET} PRIVATE ControlArtwork OriginalArtwork ApprovedArtwork juce::juce_dsp juce::juce_cryptography)
 endif()
endforeach()

# Recovered standalone convolution numerical target.
juce_add_console_app(hem_convolution_reverb_tests PRODUCT_NAME "HEM Convolution Reverb Numerical Tests")
juce_generate_juce_header(hem_convolution_reverb_tests)
target_sources(hem_convolution_reverb_tests PRIVATE Tests/ReverbPluginTests.cpp)
target_compile_definitions(hem_convolution_reverb_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_convolution_reverb_tests PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
add_test(NAME hem_convolution_reverb_tests COMMAND hem_convolution_reverb_tests)
set_tests_properties(hem_convolution_reverb_tests PROPERTIES TIMEOUT 600)

# Existing recovered delay integration test, same real host and all processors.
juce_add_console_app(hem_delay_integration_tests PRODUCT_NAME "HEM Delay Integration Tests")
juce_generate_juce_header(hem_delay_integration_tests)
target_sources(hem_delay_integration_tests PRIVATE Tests/DelayIntegrationTests.cpp ${HEM_NATIVE_TEST_SOURCES} Source/Plugins/ExternalPluginHost.cpp Source/Plugins/AnalysisProcessors.cpp Source/Plugins/ReverbProcessors.cpp Source/Plugins/ModulationProcessors.cpp Source/Plugins/DelayProcessors.cpp Source/Plugins/DelayEditors.cpp)
target_compile_definitions(hem_delay_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_delay_integration_tests PRIVATE StudioArtwork HEMCoreBaseline ControlArtwork OriginalArtwork ApprovedArtwork juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
add_test(NAME hem_delay_integration_tests COMMAND hem_delay_integration_tests)

# Additive native project-tempo and approved-artwork regression gates.
foreach(pair IN ITEMS "hem_native_tempo_route_tests|NativeTempoRouteTests" "hem_delay_editor_approved_tests|DelayEditorApprovedTests" "hem_outer_callback_tempo_tests|OuterCallbackTempoTests" "hem_band_levels_only_tests|BandLevelsOnlyTests" "hem_band_history_glass_tests|BandHistoryGlassTests" "hem_nonlinear_lifecycle_tests|NonlinearLifecycleTests" "hem_nonlinear_editor_tests|NonlinearPluginEditorTests" "hem_restoration_binding_tests|RestorationBindingTests" "hem_restoration_editor_tests|RestorationEditorTests" "hem_restoration_lifecycle_tests|RestorationLifecycleTests" "hem_restoration_host_close_tests|RestorationHostCloseTests" "hem_calibrated_host_meter_tests|CalibratedHostMeterTests" "hem_native_pair_approved_editor_tests|NativePairApprovedEditorTests" "hem_foundation_approved_editor_tests|FoundationApprovedEditorTests" "hem_native_state_integration_tests|NativeStateIntegrationTests")
 string(REPLACE "|" ";" parts "${pair}")
 list(GET parts 0 target)
 list(GET parts 1 fixture)
 juce_add_console_app(${target} PRODUCT_NAME "${target}")
 juce_generate_juce_header(${target})
 target_sources(${target} PRIVATE Tests/${fixture}.cpp ${HEM_NATIVE_TEST_SOURCES} Source/Plugins/ExternalPluginHost.cpp Source/Plugins/AnalysisProcessors.cpp Source/Plugins/ReverbProcessors.cpp Source/Plugins/ModulationProcessors.cpp Source/Plugins/DelayProcessors.cpp Source/Plugins/DelayEditors.cpp)
 target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
 if(fixture STREQUAL "RestorationHostCloseTests")
  target_compile_definitions(${target} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
 endif()
 target_link_libraries(${target} PRIVATE StudioArtwork HEMCoreBaseline ControlArtwork OriginalArtwork ApprovedArtwork juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
 add_test(NAME ${target} COMMAND ${target})
endforeach()

# Recovered original band engines, attached once to every processor/registry target.
juce_add_console_app(hem_band_integration_tests PRODUCT_NAME "HEM Band Integration Tests")
juce_generate_juce_header(hem_band_integration_tests)
target_sources(hem_band_integration_tests PRIVATE Tests/BandIntegrationTests.cpp ${HEM_NATIVE_TEST_SOURCES} Source/Plugins/ExternalPluginHost.cpp Source/Plugins/AnalysisProcessors.cpp Source/Plugins/ReverbProcessors.cpp Source/Plugins/ModulationProcessors.cpp Source/Plugins/DelayProcessors.cpp Source/Plugins/DelayEditors.cpp)
target_compile_definitions(hem_band_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 HEM_BAND_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures/BandV1")
target_link_libraries(hem_band_integration_tests PRIVATE StudioArtwork HEMCoreBaseline ControlArtwork OriginalArtwork ApprovedArtwork juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
add_test(NAME hem_band_integration_tests COMMAND hem_band_integration_tests)
set_tests_properties(hem_band_integration_tests PROPERTIES TIMEOUT 7200)
get_property(HEM_BAND_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_BAND_TARGETS)
 get_target_property(sources ${target} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST sources)
  target_sources(${target} PRIVATE Source/Plugins/BandProcessors.cpp)
  target_link_libraries(${target} PRIVATE ApprovedArtwork)
 endif()
endforeach()

# Recovered nonlinear pair; existing Tape identity is updated, never duplicated.
juce_add_console_app(hem_nonlinear_integration_tests PRODUCT_NAME "HEM Nonlinear Integration Tests")
juce_generate_juce_header(hem_nonlinear_integration_tests)
target_sources(hem_nonlinear_integration_tests PRIVATE Tests/NonlinearIntegrationTests.cpp ${HEM_NATIVE_TEST_SOURCES} Source/Plugins/ExternalPluginHost.cpp Source/Plugins/AnalysisProcessors.cpp Source/Plugins/ReverbProcessors.cpp Source/Plugins/ModulationProcessors.cpp Source/Plugins/DelayProcessors.cpp Source/Plugins/DelayEditors.cpp Source/Plugins/BandProcessors.cpp)
target_compile_definitions(hem_nonlinear_integration_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_nonlinear_integration_tests PRIVATE StudioArtwork HEMCoreBaseline ControlArtwork OriginalArtwork ApprovedArtwork juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
add_test(NAME hem_nonlinear_integration_tests COMMAND hem_nonlinear_integration_tests)
get_property(HEM_NONLINEAR_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_NONLINEAR_TARGETS)
 get_target_property(sources ${target} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST sources)
  target_sources(${target} PRIVATE Source/Plugins/NonlinearProcessors.cpp)
  target_link_libraries(${target} PRIVATE ApprovedArtwork)
 endif()
endforeach()

# Existing recovered restoration family, appended without replacing prior engines.
get_property(HEM_RESTORATION_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_RESTORATION_TARGETS)
 get_target_property(sources ${target} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST sources)
  target_sources(${target} PRIVATE Source/Plugins/RestorationProcessors.cpp)
  target_link_libraries(${target} PRIVATE ApprovedArtwork)
 endif()
endforeach()

# Optional native resource state support: every session-bearing target gets the
# same implementation; no existing processor or registry identity is replaced.
get_property(HEM_NATIVE_STATE_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_NATIVE_STATE_TARGETS)
 get_target_property(sources ${target} SOURCES)
 if("Source/SessionState.cpp" IN_LIST sources)
  target_sources(${target} PRIVATE Source/Systems/NativeProcessorState.cpp Source/Systems/NativeResourceState.cpp Source/Systems/BoundedStateInput.cpp)
 endif()
endforeach()

juce_add_console_app(hem_native_resource_state_tests PRODUCT_NAME "HEM Native Resource Descriptor Tests")
juce_generate_juce_header(hem_native_resource_state_tests)
target_sources(hem_native_resource_state_tests PRIVATE Tests/NativeResourceStateTests.cpp Source/Systems/NativeResourceState.cpp)
target_compile_definitions(hem_native_resource_state_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_native_resource_state_tests PRIVATE juce::juce_data_structures juce::juce_recommended_config_flags)
add_test(NAME hem_native_resource_state_tests COMMAND hem_native_resource_state_tests)

juce_add_console_app(hem_bounded_state_input_tests PRODUCT_NAME "HEM Bounded State Input Tests")
juce_generate_juce_header(hem_bounded_state_input_tests)
target_sources(hem_bounded_state_input_tests PRIVATE Tests/BoundedStateInputTests.cpp Source/Systems/BoundedStateInput.cpp)
target_compile_definitions(hem_bounded_state_input_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(hem_bounded_state_input_tests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
add_test(NAME hem_bounded_state_input_tests COMMAND hem_bounded_state_input_tests "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures/BandV1")

# Every registry consumer links the actual resource-bearing Drum implementation.
get_property(HEM_DRUM_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_DRUM_TARGETS)
 get_target_property(sources ${target} SOURCES)
 if("Source/Plugins/Editors.cpp" IN_LIST sources)
  target_sources(${target} PRIVATE Source/Plugins/DrumReplacerProcessor.cpp Source/Plugins/DrumReplacerEditor.cpp Source/Plugins/AmpCabinetProcessor.cpp Source/Plugins/AmpCabinetEditor.cpp Source/DSP/AmpCabinetCore.cpp Source/Plugins/ReferenceMatchProcessor.cpp Source/DSP/ReferenceMatchCore.cpp)
  target_sources(${target} PRIVATE Source/Plugins/PitchTimeProcessors.cpp Source/Plugins/PitchTimeEditors.cpp)
  target_sources(${target} PRIVATE Source/Systems/ClipEditState.cpp Source/Systems/PitchTimeClipRenderer.cpp Source/DSP/PitchTimeBackend.cpp)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../pitch-time-work/signalsmith-stretch" "${CMAKE_CURRENT_SOURCE_DIR}/../pitch-time-work/signalsmith-linear/include")
  if("Source/MainComponent.cpp" IN_LIST sources)
   target_sources(${target} PRIVATE Source/MainComponentClip.cpp Source/MainComponentReference.cpp Source/MainComponentSpectral.cpp Source/MainComponentInstrumental.cpp Source/TrackRenderScheduler.cpp)
  endif()
  foreach(required IN ITEMS Source/SessionState.cpp Source/Systems/NativeProcessorState.cpp Source/Systems/NativeResourceState.cpp Source/Systems/BoundedStateInput.cpp Source/InsertChain.cpp Source/Plugins/ExternalPluginHost.cpp)
   if(NOT required IN_LIST sources)
    target_sources(${target} PRIVATE ${required})
   endif()
  endforeach()
 endif()
endforeach()
# Transport implementation is shared by production and every source-staging test.
get_property(HEM_TRANSPORT_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_TRANSPORT_TARGETS)
 get_target_property(transport_sources ${target} SOURCES)
 if("Source/StemFileImporter.cpp" IN_LIST transport_sources OR "Source/SessionState.cpp" IN_LIST transport_sources OR "Source/MainComponent.cpp" IN_LIST transport_sources)
  target_sources(${target} PRIVATE Source/Systems/ReadAheadTransport.cpp)
 endif()
endforeach()
# Device owner is used by the main host and the standalone vocal-panel fixtures.
get_property(HEM_DEVICE_OWNER_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS HEM_DEVICE_OWNER_TARGETS)
 get_target_property(device_owner_sources ${target} SOURCES)
 if("Source/MainComponent.cpp" IN_LIST device_owner_sources OR "Source/Systems/VocalTrackingPanel.cpp" IN_LIST device_owner_sources)
  target_sources(${target} PRIVATE Source/Systems/NativeAudioDeviceHost.cpp)
 endif()
endforeach()


