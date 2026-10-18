# Current native studio, bounded VST3 host and test source

Generated from the checked 2026-10-01 milestone. See README.md and verification/licensing documents for limits.

## CMakeLists.txt

```cmake
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

```

## Source/CoreBaseline/Engine.cpp

```cpp
#include "Engine.h"
namespace hem {
namespace {
float approach(float x,float target,float step) noexcept{return x+std::clamp(target-x,-step,step);}
float clean(float x)noexcept{return std::isfinite(x)?x:0;}
float normalizedTrimDb(float db) noexcept{
 db=std::clamp(clean(db),-24.f,24.f);
 return std::round(db*10.f)*.1f;
}
float amplitude(float db){return std::pow(10.0f,db*.05f);}
void copy(juce::AudioBuffer<float>& dst,const juce::AudioBuffer<float>& src,int n) noexcept{for(int c=0;c<dst.getNumChannels();++c)dst.copyFrom(c,0,src,c,0,n);}
}
ClipReport OfflineWorkerUtility::condition(Clip& clip,float headroomDb,float attenuationDb){
 auto& b=clip.audio;ClipReport r;int n=b.getNumSamples(),ch=b.getNumChannels();
 if(n<=0||ch<=0)throw std::invalid_argument("Empty clip");
 if(!std::isfinite(headroomDb)||headroomDb>0||!std::isfinite(attenuationDb)||attenuationDb> -3||attenuationDb< -6)throw std::invalid_argument("Invalid headroom/attenuation");
 r.mean.resize(size_t(ch));double energy=0;uint64_t nearFull=0;
 for(int c=0;c<ch;++c){double sum=0;auto* x=b.getWritePointer(c);for(int i=0;i<n;++i){if(!std::isfinite(x[i]))throw std::invalid_argument("Nonfinite clip");sum+=x[i];}r.mean[size_t(c)]=sum/n;for(int i=0;i<n;++i){x[i]-=float(r.mean[size_t(c)]);float a=std::abs(x[i]);r.peak=std::max(r.peak,a);energy+=double(x[i])*x[i];nearFull+=a>=.999f;}}
 r.rms=float(std::sqrt(energy/(double(n)*ch)));r.crestDb=r.rms>0?20*std::log10(r.peak/r.rms):0;
 r.suspectedClipping=nearFull>0;r.trimDb=(r.suspectedClipping||r.peak>amplitude(headroomDb))?attenuationDb:0;
 b.applyGain(amplitude(r.trimDb));return r;
}
std::unique_ptr<Clip> OfflineWorkerUtility::loadWav(const juce::File& file,double targetRate){
 if(!(targetRate>0)||!std::isfinite(targetRate)||!file.hasFileExtension("wav"))throw std::invalid_argument("WAV/rate required");
 juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
 if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>INT_MAX||reader->numChannels<1||reader->numChannels>2||!(reader->sampleRate>0))throw std::runtime_error("Unsupported or unreadable WAV");
 juce::AudioBuffer<float> input(int(reader->numChannels),int(reader->lengthInSamples));if(!reader->read(&input,0,input.getNumSamples(),0,true,true))throw std::runtime_error("WAV decode failed");
 double ratio=reader->sampleRate/targetRate;double count=std::ceil(input.getNumSamples()/ratio);if(count>INT_MAX)throw std::length_error("Clip exceeds AudioBuffer sample indexing; use segmented streaming");
 auto result=std::make_unique<Clip>();result->sampleRate=targetRate;result->audio.setSize(input.getNumChannels(),int(count));
 // Offline windowed-sinc resampler, 64 taps, anti-alias cutoff reduced for downsampling.
 double cutoff=.94*std::min(1.0,1.0/ratio);
 for(int c=0;c<input.getNumChannels();++c)for(int i=0;i<int(count);++i){double t=i*ratio,sum=0,norm=0;int centre=int(std::floor(t));for(int k=-31;k<=32;++k){int j=centre+k;double d=t-j;if(std::abs(d)>32)continue;double v=cutoff*d;double sinc=std::abs(v)<1e-12?1:std::sin(juce::MathConstants<double>::pi*v)/(juce::MathConstants<double>::pi*v);double window=.5+.5*std::cos(juce::MathConstants<double>::pi*d/32);double w=cutoff*sinc*window;norm+=w;if(j>=0&&j<input.getNumSamples())sum+=w*input.getSample(c,j);}result->audio.setSample(c,i,float(sum/norm));}
 return result;
}
void ClipPlayback::render(juce::AudioBuffer<float>& out,int n) noexcept{
 if(!previous&&publication.canRetire())if(auto* next=publication.take()){previous=active;active=next;fade=0;}
 for(int i=0;i<n;++i){float t=previous?std::min(1.f,float(++fade)/fadeLength):1;
  for(int c=0;c<out.getNumChannels();++c){auto sample=[&](Clip* p){return p&&position<p->audio.getNumSamples()?p->audio.getSample(std::min(c,p->audio.getNumChannels()-1),int(position)):0.f;};out.setSample(c,i,(1-t)*sample(previous)+t*sample(active));}++position;
  if(previous&&fade>=fadeLength){publication.retire(previous);previous=nullptr;}}
}
std::unique_ptr<CurveTable> VectorizedDynamics::makeCurve(const std::vector<std::pair<float,float>>& points){
 if(points.size()<2||points.front().first!= -60||points.back().first!=0)throw std::invalid_argument("Curve must cover -60 to 0 dBFS");
 for(size_t i=0;i<points.size();++i)if(!std::isfinite(points[i].first)||!std::isfinite(points[i].second)||points[i].second>points[i].first||points[i].second< -120||(i&&points[i].first<=points[i-1].first))throw std::invalid_argument("Invalid downward dynamics points");
 std::vector<float> slope(points.size());for(size_t i=0;i<points.size();++i){if(i==0)slope[i]=(points[1].second-points[0].second)/(points[1].first-points[0].first);else if(i+1==points.size())slope[i]=(points[i].second-points[i-1].second)/(points[i].first-points[i-1].first);else{float a=(points[i].second-points[i-1].second)/(points[i].first-points[i-1].first),b=(points[i+1].second-points[i].second)/(points[i+1].first-points[i].first);slope[i]=a*b<=0?0:2*a*b/(a+b);}}
 auto table=std::make_unique<CurveTable>();size_t segment=0;
 // Linear amplitude indexing avoids log() in the callback; curve itself is defined in dB.
 for(size_t i=0;i<1024;++i){float a=std::max(.001f,float(i)/1023),x=20*std::log10(a);while(segment+2<points.size()&&x>points[segment+1].first)++segment;auto [x0,y0]=points[segment];auto [x1,y1]=points[segment+1];float h=x1-x0,t=std::clamp((x-x0)/h,0.f,1.f);float y=(2*t*t*t-3*t*t+1)*y0+(t*t*t-2*t*t+t)*h*slope[segment]+(-2*t*t*t+3*t*t)*y1+(t*t*t-t*t)*h*slope[segment+1];table->gain[i]=amplitude(std::min(0.f,y-x));}
 return table;
}
void VectorizedDynamics::configure(const std::vector<std::pair<float,float>>& points,float a,float r,bool mode){
 if(!(a>0&&r>0)||!std::isfinite(a)||!std::isfinite(r))throw std::invalid_argument("Invalid detector times");
 auto table=makeCurve(points);cached=juce::ValueTree("Dynamics");cached.setProperty("attackMs",a,nullptr);cached.setProperty("releaseMs",r,nullptr);cached.setProperty("rms",mode,nullptr);for(auto [x,y]:points){juce::ValueTree p("Point");p.setProperty("x",x,nullptr);p.setProperty("y",y,nullptr);cached.addChild(p,-1,nullptr);}rms=mode;attack=std::exp(-1.f/float(sr*a*.001));release=std::exp(-1.f/float(sr*r*.001));tables.publish(std::move(table));
}
void VectorizedDynamics::prepare(double rate,int,int){sr=rate;env=0;std::vector<std::pair<float,float>> points;for(int i=0;i<cached.getNumChildren();++i){auto p=cached.getChild(i);points.emplace_back(float(p["x"]),float(p["y"]));}if(points.empty())points={{-60,-60},{0,0}};configure(points,float(cached.getProperty("attackMs",10)),float(cached.getProperty("releaseMs",100)),bool(cached.getProperty("rms",false)));}
void VectorizedDynamics::process(juce::AudioBuffer<float>& buffer,int numSamples) noexcept{
 juce::ScopedNoDenormals guard;int channels=buffer.getNumChannels();if(numSamples<=0||channels<=0)return;if(numSamples>buffer.getNumSamples()){buffer.clear();return;}
 auto* table=tables.acquire();if(!table)return;constexpr int chunkSize=128;
 alignas(16) std::array<float,chunkSize> detector{},channelValues{},gainValues{};
 for(int offset=0;offset<numSamples;offset+=chunkSize){int count=std::min(chunkSize,numSamples-offset);juce::FloatVectorOperations::clear(detector.data(),count);
  for(int c=0;c<channels;++c){float* input=buffer.getWritePointer(c,offset);for(int i=0;i<count;++i)input[i]=clean(input[i]);
   if(rms){juce::FloatVectorOperations::multiply(channelValues.data(),input,input,count);juce::FloatVectorOperations::add(detector.data(),channelValues.data(),count);}
   else{juce::FloatVectorOperations::abs(channelValues.data(),input,count);juce::FloatVectorOperations::max(detector.data(),detector.data(),channelValues.data(),count);}}
  if(rms)juce::FloatVectorOperations::multiply(detector.data(),1.f/channels,count);
  // Recursive envelope and indexed LUT lookup remain sequential: JUCE FVO
  // has no recursive scan/gather primitive. Detector prep/application are SIMD-capable.
  for(int i=0;i<count;++i){float target=detector[size_t(i)],coefficient=target>env?attack:release;env=coefficient*env+(1-coefficient)*target;
   float envelope=rms?std::sqrt(std::max(0.f,env)):env;float index=std::clamp(envelope*1023,0.f,1023.f);int lower=int(index),upper=std::min(lower+1,1023);float fraction=index-lower;
   float a=table->gain[size_t(lower)],b=table->gain[size_t(upper)];gainValues[size_t(i)]=a+fraction*(b-a);}
  for(int c=0;c<channels;++c)juce::FloatVectorOperations::multiply(buffer.getWritePointer(c,offset),gainValues.data(),count);
 }
}
void ModularInsertSlot::Pair::prepare(double rate,int block,int channels,int budget){
 for(size_t k=0;k<2;++k){int latency=0;if(processor[k]){processor[k]->prepare(rate,block,channels);latency=processor[k]->latency();if(latency<0||latency>budget)throw std::invalid_argument("Processor exceeds slot latency budget");auto s=processor[k]->state();if(k==0)State_A=s;else State_B=s;}align[k].prepare(channels,budget-latency);}
}
void ModularInsertSlot::prepare(double rate,int size,int ch,int latencyBudget){if(size<1||ch<1||latencyBudget<0)throw std::invalid_argument("Slot preparation");sampleRate=rate;budget=latencyBudget;block=size;channels=ch;step=1.f/float(std::max(1,int(std::lround(rate*.01))));for(auto* b:{&dryDelayBuffer,&wetA,&wetB,&oldWet}){b->setSize(ch,size);b->clear();}dryDelay.prepare(ch,budget);ab=bypassMix=deltaMix=0;auto pair=std::make_unique<Pair>();pair->prepare(rate,size,ch,budget);pairs.publish(std::move(pair));}
void ModularInsertSlot::install(std::unique_ptr<Pair> pair,double rate){if(!pair)throw std::invalid_argument("Null pair");pair->prepare(rate,block,channels,budget);pairs.publish(std::move(pair));}
void ModularInsertSlot::process(juce::AudioBuffer<float>& b,int n) noexcept{
 if(!previous&&pairs.canRetire())if(auto* p=pairs.take()){previous=active;active=p;replacement=0;warm=budget;}
 copy(dryDelayBuffer,b,n);dryDelay.process(dryDelayBuffer,n);
 auto render=[&](Pair* p){copy(wetA,b,n);copy(wetB,b,n);if(p)for(size_t k=0;k<2;++k){auto& w=k?wetB:wetA;if(p->processor[k])p->processor[k]->process(w,n);p->align[k].process(w,n);}};
 // Preserve per-sample A/B ramp for both paths through a replacement.
 float startAB=ab;if(previous){render(previous);float t=startAB;for(int i=0;i<n;++i){t=approach(t,selectB.load(std::memory_order_relaxed)?1.f:0.f,step);for(int c=0;c<channels;++c)oldWet.setSample(c,i,(1-t)*wetA.getSample(c,i)+t*wetB.getSample(c,i));}}
 render(active);bool targetB=selectB.load(std::memory_order_relaxed),targetBypass=bypass.load(std::memory_order_relaxed),targetDelta=delta.load(std::memory_order_relaxed);
 for(int i=0;i<n;++i){ab=approach(ab,targetB?1.f:0.f,step);bypassMix=approach(bypassMix,targetBypass?1.f:0.f,step);deltaMix=approach(deltaMix,targetDelta?1.f:0.f,step);float replace=1;if(previous){if(warm>0){--warm;replace=0;}else replace=std::min(1.f,float(++replacement)*step);}
 for(int c=0;c<channels;++c){float wet=(1-ab)*wetA.getSample(c,i)+ab*wetB.getSample(c,i);if(previous)wet=(1-replace)*oldWet.getSample(c,i)+replace*wet;float dry=dryDelayBuffer.getSample(c,i);float audited=(1-deltaMix)*wet+deltaMix*(dry-wet);b.setSample(c,i,(1-bypassMix)*audited+bypassMix*dry);}
 // Retirement occurs after the block so oldWet remains valid for its full block.
 }
 if(previous&&warm==0&&float(replacement)*step>=1){pairs.retire(previous);previous=nullptr;}
}
void MasterTruePeakLimiter::prepare(double rate,int block,int channels){
 if(!std::isfinite(rate)||rate<=0||block<1||channels<1)throw std::invalid_argument("Invalid limiter preparation");
 maxBlock=block;delay=4*std::max(1,int(std::lround(rate*.004)));
 const int attackSamples=std::clamp(int(std::lround(double(delay)*.125)),1,delay);
 attackShape.resize(size_t(attackSamples)+1);
 for(int k=0;k<=attackSamples;++k){const float t=float(k)/attackSamples;attackShape[size_t(k)]=t*t*(3-2*t);}
 ring.setSize(channels+1,delay);ring.clear();juce::FloatVectorOperations::fill(ring.getWritePointer(channels),1.f,delay);
 pos=0;gain=1;ceiling=std::pow(10.f,-1.f/20.f);counter=head=tail=0;deque.clear();
 releaseFast=std::exp(-1/(rate*4*.080));releaseSlow=std::exp(-1/(rate*4*.300));
 oversampler=std::make_unique<juce::dsp::Oversampling<float>>(size_t(channels),2,juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,true,true);
 oversampler->initProcessing(size_t(block));oversampler->reset();
}
void MasterTruePeakLimiter::process(juce::AudioBuffer<float>& buffer,int numSamples) noexcept{
 juce::ScopedNoDenormals guard;const int channels=buffer.getNumChannels();
 if(!oversampler||numSamples<0||numSamples>maxBlock||numSamples>buffer.getNumSamples()||channels!=ring.getNumChannels()-1){buffer.clear();return;}if(numSamples==0)return;
 // Prevent a single invalid input from contaminating interpolation FIR history.
 for(int c=0;c<channels;++c){auto* input=buffer.getWritePointer(c);for(int i=0;i<numSamples;++i)input[i]=clean(input[i]);}
 juce::dsp::AudioBlock<float> whole(buffer);auto block=whole.getSubBlock(0,size_t(numSamples));auto up=oversampler->processSamplesUp(block);
 float* scheduledGain=ring.getWritePointer(channels);const int attackSamples=std::clamp(int(std::lround(double(delay)*.125)),1,delay);
 for(size_t i=0;i<up.getNumSamples();++i){float peak=0;
  for(int c=0;c<channels;++c){float x=clean(up.getSample(c,int(i)));up.setSample(c,int(i),x);peak=std::max(peak,std::abs(x));}
  const double coefficient=gain<.5?releaseSlow:releaseFast;const double released=1-coefficient*(1-gain);gain=std::min(released,double(scheduledGain[pos]));
  for(int c=0;c<channels;++c){float* audioDelay=ring.getWritePointer(c);float delayed=audioDelay[pos];audioDelay[pos]=up.getSample(c,int(i));up.setSample(c,int(i),delayed*gain);}
  scheduledGain[pos]=1;
  if(peak>ceiling){float target=ceiling/peak;int j=(pos+delay-attackSamples)%delay;
   // Schedule cubic smoothstep BEFORE the delayed peak; overlap uses minimum.
   // Bounded O(attackSamples) overload cost requires real-time benchmarking.
   for(int k=0;k<=attackSamples;++k){float constraint=1+(target-1)*attackShape[size_t(k)];scheduledGain[j]=std::min(scheduledGain[j],constraint);if(++j==delay)j=0;}}
  if(++pos==delay)pos=0;
 }
 oversampler->processSamplesDown(block);
 // Final SAMPLE ceiling only: not certified true-peak/codec protection.
 for(int c=0;c<channels;++c){auto* output=buffer.getWritePointer(c);for(int i=0;i<numSamples;++i)output[i]=clean(output[i]);juce::FloatVectorOperations::clip(output,output,-ceiling,ceiling,numSamples);}
}
bool StemIngestManager::setInputTrimDb(size_t strip,float db) noexcept{
 if(strip>=trimDb.size())return false;
 trimDb[strip].store(normalizedTrimDb(db),std::memory_order_relaxed);return true;
}
StemIngestManager::StemIngestManager(){for(size_t s=0;s<13;++s){trimDb[s].store(0.f);gainDb[s].store(0);highPass[s].store(false);mute[s].store(false);solo[s].store(false);hardwareChannel[s]=int(s);}}
void StemIngestManager::prepare(double rate,int block){
 if(rate<=0||block<1)throw std::invalid_argument("Ingest preparation");maxBlock=block;step=1.f/float(std::max(1,int(rate*.01)));double k=std::tan(juce::MathConstants<double>::pi*30/rate),norm=1/(1+std::sqrt(2.)*k+k*k);
 for(size_t s=0;s<13;++s){scratch[s].setSize(2,block);scratch[s].clear();trim[s].reset(rate,.010);trim[s].setCurrentAndTargetValue(amplitude(normalizedTrimDb(trimDb[s].load())));mix[s]=1;hpMix[s]=0;for(auto& f:hp[s]){f={};f.b0=norm;f.b1=-2*norm;f.b2=norm;f.a1=2*(k*k-1)*norm;f.a2=(1-std::sqrt(2.)*k+k*k)*norm;}for(auto& slot:slots[s])slot.prepare(rate,block,2,512);}limiter.prepare(rate,block,2);
}
void StemIngestManager::processBlock(const juce::AudioBuffer<float>& in,juce::AudioBuffer<float>& out,int n) noexcept{
 juce::ScopedNoDenormals guard;if(n<0||n>maxBlock||out.getNumChannels()!=2||out.getNumSamples()<n){out.clear();return;}
 // Separate input and output buffers required: all physical inputs remain intact.
 out.clear();bool anySolo=false;for(auto& flag:solo)anySolo|=flag.load(std::memory_order_relaxed);
 for(size_t s=0;s<13;++s){auto& b=scratch[s];int channel=hardwareChannel[s];float requestedTrim=amplitude(normalizedTrimDb(trimDb[s].load(std::memory_order_relaxed)));float requestedGain=amplitude(std::clamp(clean(gainDb[s].load()),-60.f,12.f));bool enabled=!mute[s].load()&&(!anySolo||solo[s].load());float targetHP=highPass[s].load()?1.f:0.f;trim[s].setTargetValue(requestedTrim);
 for(int i=0;i<n;++i){const float inputTrim=trim[s].getNextValue();hpMix[s]=approach(hpMix[s],targetHP,step);float x=channel>=0&&channel<in.getNumChannels()?clean(in.getSample(channel,i))*inputTrim:0;for(int c=0;c<2;++c){float filtered=hp[s][size_t(c)].tick(x);b.setSample(c,i,x+hpMix[s]*(filtered-x));}}
 for(auto& slot:slots[s])slot.process(b,n);
 for(int i=0;i<n;++i){mix[s]=approach(mix[s],enabled?requestedGain:0.f,step);for(int c=0;c<2;++c)out.addSample(c,i,b.getSample(c,i)*mix[s]*(1.f/13.f));}
 }
 limiter.process(out,n);
}
}

```

## Source/CoreBaseline/Engine.h

```cpp
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <functional>
#include <climits>
namespace hem {
inline constexpr double defaultSampleRate = 48000.0; // Primary native target; prepare uses actual device/session rate.
static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
// Single control/worker producer, one audio consumer. Destruction only on producer.
template<class T> class Publication {
 std::atomic<T*> pending{nullptr}; std::array<T*,64> retired{};
 std::atomic<unsigned> read{0},write{0}; T* current=nullptr;
public:
 ~Publication(){ delete pending.load(); delete current; collect(); }
 void publish(std::unique_ptr<T> value){ collect(); delete pending.exchange(value.release(),std::memory_order_acq_rel); }
 void collect(){auto r=read.load(std::memory_order_relaxed);auto w=write.load(std::memory_order_acquire);while(r!=w){delete retired[r%64];++r;}read.store(r,std::memory_order_release);}
 T* acquire() noexcept {auto w=write.load(std::memory_order_relaxed);if(w-read.load(std::memory_order_acquire)<64){if(auto* p=pending.exchange(nullptr,std::memory_order_acq_rel)){if(current){retired[w%64]=current;write.store(w+1,std::memory_order_release);}current=p;}}return current;}
 T* peek() const noexcept{return current;}
 // For clip crossfades: caller retains old pointer until retire is called.
 T* take() noexcept {return pending.exchange(nullptr,std::memory_order_acq_rel);}
 bool canRetire() const noexcept{return write.load(std::memory_order_relaxed)-read.load(std::memory_order_acquire)<64;}
 void retire(T* p) noexcept{if(!p)return;auto w=write.load(std::memory_order_relaxed);jassert(canRetire());retired[w%64]=p;write.store(w+1,std::memory_order_release);}
};
struct Clip {juce::AudioBuffer<float> audio; double sampleRate=defaultSampleRate;};
struct ClipReport {std::vector<double> mean;float peak=0,rms=0,crestDb=0,trimDb=0;bool suspectedClipping=false;};
class OfflineWorkerUtility {
public:
 static ClipReport condition(Clip&,float headroomDb=-3,float attenuationDb=-4);
 static std::unique_ptr<Clip> loadWav(const juce::File&,double targetRate);
 class Job final: public juce::ThreadPoolJob {
  std::unique_ptr<Clip> clip; Publication<Clip>& destination;
 public: std::atomic<int> result{0}; // 0 pending, 1 published, 2 cancelled, 3 failed.
  Job(std::unique_ptr<Clip> c,Publication<Clip>& d):ThreadPoolJob("Clip conditioning"),clip(std::move(c)),destination(d){}
  JobStatus runJob() override {try{condition(*clip);if(shouldExit())result.store(2);else{destination.publish(std::move(clip));result.store(1);}}catch(...){result.store(3);}return jobHasFinished;}
 };
};
class ClipPlayback {
 Publication<Clip>& publication; Clip* active=nullptr;Clip* previous=nullptr;int fade=0,fadeLength=480; int64_t position=0;
public:
 explicit ClipPlayback(Publication<Clip>& p):publication(p){}
 ~ClipPlayback(){delete active;delete previous;} // Stop/join callback first.
 void prepare(double rate){fadeLength=std::max(1,int(std::lround(.01*rate)));}
 void seek(int64_t p) noexcept{position=std::max<int64_t>(0,p);} // Audio thread command only.
 void render(juce::AudioBuffer<float>& out,int n) noexcept;
};
class Delay {
 juce::AudioBuffer<float> ring;int pos=0,delay=0;
public:
 void prepare(int channels,int samples){delay=samples;ring.setSize(channels,std::max(1,samples));ring.clear();pos=0;}
 void process(juce::AudioBuffer<float>& b,int n) noexcept{if(!delay)return;for(int i=0;i<n;++i){for(int c=0;c<b.getNumChannels();++c){auto* r=ring.getWritePointer(c);auto* x=b.getWritePointer(c);float t=r[pos];r[pos]=x[i];x[i]=t;}if(++pos==delay)pos=0;}}
};
struct Processor {
 virtual ~Processor()=default;
 virtual void prepare(double,int,int)=0;
 virtual void process(juce::AudioBuffer<float>&,int) noexcept=0;
 virtual int latency() const noexcept=0;
 virtual juce::ValueTree state() const=0; // Control thread, before publication.
};
struct CurveTable {std::array<float,1024> gain{};};
class VectorizedDynamics final:public Processor {
 Publication<CurveTable> tables;juce::ValueTree cached{"Dynamics"};double sr=defaultSampleRate;float env=0,attack=0,release=0;bool rms=false;
public:
 static std::unique_ptr<CurveTable> makeCurve(const std::vector<std::pair<float,float>>&);
 void updateCurve(const std::vector<std::pair<float,float>>& points){tables.publish(makeCurve(points));}
 void configure(const std::vector<std::pair<float,float>>&,float attackMs,float releaseMs,bool rmsDetector);
 void prepare(double,int,int) override;
 void process(juce::AudioBuffer<float>&,int) noexcept override;
 int latency() const noexcept override{return 0;}
 juce::ValueTree state() const override{return cached.createCopy();}
 void collect(){tables.collect();}
};
class ModularInsertSlot {
public:
 struct Pair {
  std::array<std::unique_ptr<Processor>,2> processor;
  juce::ValueTree State_A{"A"},State_B{"B"};std::array<Delay,2> align;
  static std::unique_ptr<Pair> fromStates(const juce::ValueTree& a,const juce::ValueTree& b,
   const std::function<std::unique_ptr<Processor>(const juce::ValueTree&)>& factory){
   auto p=std::make_unique<Pair>();p->processor[0]=factory(a);p->processor[1]=factory(b);
   if(!p->processor[0]||!p->processor[1])throw std::invalid_argument("State factory failed");
   p->State_A=a.createCopy();p->State_B=b.createCopy();return p;
  }
  void prepare(double rate,int block,int channels,int budget);
 };
private:
 Publication<Pair> pairs;juce::AudioBuffer<float> dryDelayBuffer,wetA,wetB,oldWet;
 Pair* active=nullptr;Pair* previous=nullptr;int replacement=0,warm=0;
 Delay dryDelay;double sampleRate=defaultSampleRate;int budget=0,block=0,channels=0;float ab=0,bypassMix=0,deltaMix=0,step=.001f;
public:
 ~ModularInsertSlot(){delete active;delete previous;}
 std::atomic<bool> selectB{false},bypass{false},delta{false};
 void prepare(double,int,int,int latencyBudget=512);
 // Fully prepare on the single control thread; publishes at callback boundary.
 void install(std::unique_ptr<Pair>,double rate);
 void remove(){install(std::make_unique<Pair>(),sampleRate);}
 void collect(){pairs.collect();}
 void process(juce::AudioBuffer<float>&,int) noexcept;
 int latency()const noexcept{return budget;}
};
class MasterTruePeakLimiter {
 std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;juce::AudioBuffer<float> ring;
 struct Peak {uint64_t index;float value;};std::vector<Peak> deque;uint64_t counter=0,head=0,tail=0;
 int delay=0,pos=0,maxBlock=0;
 std::vector<float> attackShape; // Prepared cubic table; never resized in process().
 // Double release state avoids float rounding stalling below unity.
 double gain=1,releaseFast=0,releaseSlow=0;float ceiling=.891250938f;
public:
 void prepare(double,int,int=2);
 void process(juce::AudioBuffer<float>&,int) noexcept;
 int latency() const noexcept{return oversampler?delay/4+int(std::lround(oversampler->getLatencyInSamples())):0;}
};
class StemIngestManager {
 struct Biquad {double b0=1,b1=0,b2=0,a1=0,a2=0;float z1=0,z2=0;float tick(float x)noexcept{float y=float(b0*x+z1);z1=float(b1*x-a1*y+z2);z2=float(b2*x-a2*y);return y;}};
 std::array<std::array<Biquad,2>,13> hp;std::array<juce::AudioBuffer<float>,13> scratch;
 std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,13> trim;std::array<float,13> mix{},hpMix{};int maxBlock=0;float step=0;MasterTruePeakLimiter limiter;
public:
 std::array<std::array<ModularInsertSlot,8>,13> slots;
 std::array<std::atomic<float>,13> trimDb,gainDb;
 std::array<std::atomic<bool>,13> highPass,mute,solo;
 std::array<int,13> hardwareChannel; // Configure only with callback stopped; -1 means disabled.
 static constexpr const char* inputTrimParameterId = "inputTrimDb";
 // Control-thread setter: sanitizes, clamps and rounds to the 0.1 dB contract.
 // Existing direct trimDb writes remain supported and are normalized on read.
 bool setInputTrimDb(size_t strip, float db) noexcept;
 StemIngestManager();
 void prepare(double,int);
 void processBlock(const juce::AudioBuffer<float>& hardwareInput,juce::AudioBuffer<float>& stereoOutput,int n) noexcept;
 int latency()const noexcept{return slots[0][0].latency()*8+limiter.latency();}
};
}

```

## Source/CoreMasterStage.h

```cpp
#pragma once
#include "CoreBaseline/Engine.h"

namespace hem {
// Host adapter only. All limiting is the byte-preserved Task #2B implementation.
// Preparation runs with the callback stopped. No processing buffers are resized
// or allocated by process(). Metering reports sample peak, not reconstructed TP.
class CoreMasterStage final {
public:
    void prepare(double actualRate, int maximumBlock) {
        if (!std::isfinite(actualRate) || actualRate <= 0 || maximumBlock <= 0)
            throw std::invalid_argument("Invalid native master preparation");
        maximum = maximumBlock;
        buffer.setSize(2, maximum);
        buffer.clear();
        limiter.prepare(actualRate, maximum, 2);
        reportedLatency.store(limiter.latency(), std::memory_order_release);
        samplePeak.store(0, std::memory_order_relaxed);
    }

    void process(float* left, float* right, int count) noexcept {
        if (!left || !right || count <= 0) return;
        if (maximum <= 0 || count > maximum) {
            std::fill_n(left, count, 0.f);
            std::fill_n(right, count, 0.f);
            samplePeak.store(0, std::memory_order_relaxed);
            return;
        }
        buffer.copyFrom(0, 0, left, count);
        buffer.copyFrom(1, 0, right, count);
        limiter.process(buffer, count);
        std::copy_n(buffer.getReadPointer(0), count, left);
        std::copy_n(buffer.getReadPointer(1), count, right);
        samplePeak.store(std::max(buffer.getMagnitude(0, 0, count),
                                  buffer.getMagnitude(1, 0, count)),
                         std::memory_order_relaxed);
    }

    int latencySamples() const noexcept { return reportedLatency.load(std::memory_order_acquire); }
    float samplePeakDb() const noexcept {
        return juce::Decibels::gainToDecibels(samplePeak.load(std::memory_order_relaxed), -120.f);
    }
private:
    MasterTruePeakLimiter limiter;
    juce::AudioBuffer<float> buffer;
    std::atomic<float> samplePeak{0};
    std::atomic<int> reportedLatency{0};
    int maximum = 0;
};
}

```

## Source/DSP/DspCore.cpp

```cpp
#include "DspCore.h"
#include <complex>
namespace hem {
void Biquad::configure(Type type,double rate,float hz,float q,float db) noexcept {
    const double w=2*pi*std::clamp(double(hz),1.0,rate*.45)/rate;
    const double c=std::cos(w),s=std::sin(w),alpha=s/(2*std::max(.1f,q));
    const double A=std::pow(10.0,double(db)/40);double a0=1;
    if(type==HighPass){b0=(1+c)/2;b1=-(1+c);b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==LowPass){b0=(1-c)/2;b1=1-c;b2=b0;a0=1+alpha;a1=-2*c;a2=1-alpha;}
    else if(type==Bell){b0=1+alpha*A;b1=-2*c;b2=1-alpha*A;a0=1+alpha/A;a1=-2*c;a2=1-alpha/A;}
    else {const double v=2*std::sqrt(A)*s/std::sqrt(2.0);
        b0=A*((A+1)+(A-1)*c+v);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-v);
        a0=(A+1)-(A-1)*c+v;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-v;}
    b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0;
}
float Biquad::magnitude(double hz,double rate) const noexcept {
    const std::complex<double> z=std::polar(1.0,-2*pi*hz/rate);
    return float(std::abs((b0+b1*z+b2*z*z)/(1.0+a1*z+a2*z*z)));
}
void HostDelay::process(float* l,float* r,int n) noexcept {
    if(delay==0)return;
    for(int i=0;i<n;++i){const int read=(pos+1)%(delay+1);const auto p=size_t(pos),j=size_t(read);
        ring[0][p]=l[i];ring[1][p]=r[i];l[i]=ring[0][j];r[i]=ring[1][j];pos=read;}
}
void StereoCore::prepare(double rate){sr=rate;sideHighPass.reset();width.reset(sr,1);cutoff.reset(sr,120);balance.reset(sr,0);output.reset(sr,1);bassMix.reset(sr,0);monoMix.reset(sr,0);inversionL.reset(sr,1);inversionR.reset(sr,1);swapMix.reset(sr,0);}
void StereoCore::process(float* l,float* r,int n,const StereoParameters& p) noexcept {
    width.set(p.width/100);cutoff.set(p.monoBass);balance.set(p.balance/100);output.set(gain(p.output));bassMix.set(p.bassMono?1.f:0.f);monoMix.set(p.mono?1.f:0.f);inversionL.set(p.invertL?-1.f:1.f);inversionR.set(p.invertR?-1.f:1.f);swapMix.set(p.swap?1.f:0.f);
    double ll=0,rr=0,lr=0;float pk=0;
    for(int i=0;i<n;++i){sideHighPass.configure(Biquad::HighPass,sr,cutoff.next(),.7071068f);
        const float mid=(l[i]+r[i])*.5f,side=(l[i]-r[i])*.5f,hi=sideHighPass.tick(side,0),wet=bassMix.next();
        const float s=(side+(hi-side)*wet)*width.next()*(1-monoMix.next());
        const float b=balance.next(),g=output.next(),left=(mid+s)*(1-std::max(0.f,b))*inversionL.next(),right=(mid-s)*(1+std::min(0.f,b))*inversionR.next(),sw=swapMix.next();
        l[i]=(left+(right-left)*sw)*g;r[i]=(right+(left-right)*sw)*g;
        ll+=l[i]*l[i];rr+=r[i]*r[i];lr+=l[i]*r[i];pk=std::max({pk,std::abs(l[i]),std::abs(r[i])});}
    correlation.store(ll*rr>1e-20?float(lr/std::sqrt(ll*rr)):0,std::memory_order_relaxed);peak.store(pk,std::memory_order_relaxed);
}
void EqCore::prepare(double rate){sr=rate;controlPhase=0;cutoff.reset(sr,30);output.reset(sr,1);const EqParameters p;
    for(size_t i=0;i<4;++i){hp[i].reset();eq[i].reset();frequencies[i].reset(sr,p.hz[i]);gains[i].reset(sr,0);qs[i].reset(sr,p.q[i]);hpMix[i].reset(sr,0);}}
void EqCore::process(float* l,float* r,int n,const EqParameters& p) noexcept {
    cutoff.set(p.cutoff);output.set(gain(p.output));for(size_t j=0;j<4;++j){frequencies[j].set(p.hz[j]);gains[j].set(p.db[j]);qs[j].set(p.q[j]);hpMix[j].set(p.highPass&&int(j)<=p.slope?1.f:0.f);}
    float pk=0;
    for(int i=0;i<n;++i){const float f=cutoff.next();std::array<float,4> mixes{};
        for(size_t j=0;j<4;++j){const float hz=frequencies[j].next(),d=gains[j].next(),q=qs[j].next();mixes[j]=hpMix[j].next();
            // Recompute stable RBJ coefficients at a bounded control cadence.
            if(controlPhase==0){hp[j].configure(Biquad::HighPass,sr,f,.7071068f);eq[j].configure(j==3?Biquad::HighShelf:Biquad::Bell,sr,hz,q,d);}}
        controlPhase=(controlPhase+1)&15;const float g=output.next();
        for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];float x=sample;
            for(size_t j=0;j<4;++j){const float filtered=hp[j].tick(x,ch);x+=(filtered-x)*mixes[j];}
            for(auto& filter:eq)x=filter.tick(x,ch);sample=x*g;pk=std::max(pk,std::abs(sample));}}
    peak.store(pk,std::memory_order_relaxed);
}
float EqCore::response(double hz,double rate,const EqParameters& p) noexcept {
    Biquad f;float magnitude=gain(p.output);if(p.highPass){f.configure(Biquad::HighPass,rate,p.cutoff,.7071068f);magnitude*=std::pow(f.magnitude(hz,rate),p.slope+1);}
    for(size_t i=0;i<4;++i){f.configure(i==3?Biquad::HighShelf:Biquad::Bell,rate,p.hz[i],p.q[i],p.db[i]);magnitude*=f.magnitude(hz,rate);}return decibels(magnitude);
}
void CompressorCore::prepare(double rate){sr=rate;envelopeDb=programme=0;detector.reset();threshold.reset(sr,0);ratio.reset(sr,1);attack.reset(sr,30);release.reset(sr,100);knee.reset(sr,0);makeup.reset(sr,1);mix.reset(sr,1);cutoff.reset(sr,80);scMix.reset(sr,0);}
void CompressorCore::process(float* l,float* r,int n,const CompressorParameters& p) noexcept {
    threshold.set(p.threshold);ratio.set(p.ratio);attack.set(p.attack);release.set(p.release);knee.set(p.knee);makeup.set(gain(p.makeup));mix.set(p.mix/100);cutoff.set(p.sidechain);scMix.set(p.scEnabled?1.f:0.f);float maxReduction=0,pk=0;
    for(int i=0;i<n;++i){detector.configure(Biquad::HighPass,sr,cutoff.next(),.7071068f);const float sc=scMix.next(),a=detector.tick(l[i],0),b=detector.tick(r[i],1);
        const float detected=std::max(std::abs(l[i]+sc*(a-l[i])),std::abs(r[i]+sc*(b-r[i])));
        const float over=decibels(detected)-threshold.next(),k=knee.next(),slope=1-1/std::max(1.f,ratio.next());float gr=0;
        if(k>0&&over>-k*.5f&&over<k*.5f)gr=slope*(over+k*.5f)*(over+k*.5f)/(2*k);else if(over>=k*.5f)gr=slope*over;
        programme+=float(1-std::exp(-1/(sr*.25)))*(gr-programme);
        const float at=attack.next(),rel=release.next()*(p.autoRelease?1+std::min(2.f,programme/6):1);
        const float c=float(std::exp(-1/(sr*.001*std::max(.1f,gr>envelopeDb?at:rel))));envelopeDb=c*envelopeDb+(1-c)*gr;
        const float g=gain(-envelopeDb)*makeup.next(),wet=mix.next(),combined=1+wet*(g-1);l[i]*=combined;r[i]*=combined;
        maxReduction=std::max(maxReduction,envelopeDb);pk=std::max({pk,std::abs(l[i]),std::abs(r[i])});}
    reduction.store(maxReduction,std::memory_order_relaxed);peak.store(pk,std::memory_order_relaxed);
}
Reconstruction::Reconstruction(){for(int p=0;p<phases;++p){double sum=0;for(int i=0;i<taps;++i){const double x=i-(taps/2-1)-double(p)/phases;
    const double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);const double window=.42-.5*std::cos(2*pi*i/(taps-1))+.08*std::cos(4*pi*i/(taps-1));h[size_t(p)][size_t(i)]=sinc*window;sum+=sinc*window;}
    for(auto& c:h[size_t(p)])c/=sum;}}
void LimiterCore::prepare(double rate){sr=rate;delay=std::max(Reconstruction::taps,int(std::ceil(sr*.005)));size=delay+Reconstruction::taps+1;
    for(auto& c:audio)c.assign(size_t(size),0);budgets.assign(size_t(size),1);history={};meterHistory={};position=histPos=meterPos=0;previousGain=1;input.reset(sr,1);ceiling.reset(sr,-1);release.reset(sr,100);}
void LimiterCore::process(float* l,float* r,int n,const LimiterParameters& p) noexcept {
    input.set(gain(p.input));ceiling.set(p.ceiling);release.set(p.release);float maxGR=0,maxTP=0;
    for(int sample=0;sample<n;++sample){const float drive=input.next(),limit=gain(ceiling.next())*.99999f;
        audio[0][size_t(position)]=l[sample]*drive;audio[1][size_t(position)]=r[sample]*drive;budgets[size_t(position)]=1;
        history[0][size_t(histPos)]=l[sample]*drive;history[1][size_t(histPos)]=r[sample]*drive;
        // A conservative bound, not just sample clipping: every reconstructed
        // window constrains ALL contributing future output samples. Triangle
        // inequality then bounds each 8x FIR phase by the ceiling. Budgets are
        // final before the 5 ms delayed sample is emitted. Stereo is linked.
        double bound=std::max(std::abs(l[sample]*drive),std::abs(r[sample]*drive));
        for(int ch=0;ch<2;++ch)for(const auto& phase:filter.h){double b=0;for(int k=0;k<Reconstruction::taps;++k)b+=std::abs(phase[size_t(k)]*history[size_t(ch)][size_t((histPos-k+Reconstruction::taps)%Reconstruction::taps)]);bound=std::max(bound,b);}
        const float budget=bound>limit?float(limit/bound):1.f;
        for(int k=0;k<Reconstruction::taps;++k){auto& v=budgets[size_t((position-k+size)%size)];v=std::min(v,budget);}
        const int read=(position-delay+size)%size;const float releaseCoeff=float(std::exp(-1/(sr*.001*release.next())));
        previousGain=std::min(budgets[size_t(read)],releaseCoeff*previousGain+(1-releaseCoeff));
        l[sample]=audio[0][size_t(read)]*previousGain;r[sample]=audio[1][size_t(read)]*previousGain;
        meterHistory[0][size_t(meterPos)]=l[sample];meterHistory[1][size_t(meterPos)]=r[sample];
        for(int ch=0;ch<2;++ch)for(const auto& phase:filter.h){double v=0;for(int k=0;k<Reconstruction::taps;++k)v+=phase[size_t(k)]*meterHistory[size_t(ch)][size_t((meterPos-k+Reconstruction::taps)%Reconstruction::taps)];maxTP=std::max(maxTP,float(std::abs(v)));}
        maxGR=std::max(maxGR,-decibels(previousGain));position=(position+1)%size;histPos=(histPos+1)%Reconstruction::taps;meterPos=(meterPos+1)%Reconstruction::taps;}
    reduction.store(maxGR,std::memory_order_relaxed);truePeak.store(maxTP,std::memory_order_relaxed);
}
} // namespace hem

```

## Source/DSP/DspCore.h

```cpp
#pragma once
// Dependency-free DSP kernels. JUCE processors and numerical tests use these
// SAME implementations. All storage is reserved in prepare(), never process().
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace hem {
constexpr double pi = 3.14159265358979323846;
inline float gain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float decibels(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-12f)); }
struct Ramp {
    float value=0, target=0, step=0; int remaining=0, length=960;
    void reset(double rate, float initial) noexcept { length=std::max(1,int(rate*.01));value=target=initial;remaining=0; }
    void set(float v) noexcept { if(v!=target){target=v;remaining=length;step=(target-value)/float(length);} }
    float next() noexcept { if(remaining>0){value+=step;if(--remaining==0)value=target;}return value; }
};
struct Biquad {
    enum Type { HighPass, LowPass, Bell, HighShelf };
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::array<double,2> z1{},z2{};
    void reset() noexcept { z1={};z2={}; }
    void configure(Type,double rate,float hz,float q,float db=0) noexcept;
    float tick(float x,int channel) noexcept {
        const auto i=static_cast<size_t>(channel);
        const double y=b0*x+z1[i];z1[i]=b1*x-a1*y+z2[i];z2[i]=b2*x-a2*y;
        return static_cast<float>(y);
    }
    float magnitude(double hz,double rate) const noexcept;
};
class HostDelay {
public:
    void prepare(int samples) { delay=std::max(0,samples);for(auto& c:ring)c.assign(static_cast<size_t>(delay+1),0);pos=0; }
    void process(float* l,float* r,int n) noexcept;
private:
    std::array<std::vector<float>,2> ring; int pos=0,delay=0;
};
struct StereoParameters { float width=100,monoBass=120,balance=0,output=0; bool bassMono=false,mono=false,invertL=false,invertR=false,swap=false; };
class StereoCore {
public:
    void prepare(double rate); void process(float*,float*,int,const StereoParameters&) noexcept;
    std::atomic<float> correlation{0},peak{0};
private:
    double sr=96000;Biquad sideHighPass;Ramp width,cutoff,balance,output,bassMix,monoMix,inversionL,inversionR,swapMix;
};
struct EqParameters {
    float cutoff=30;bool highPass=false;int slope=0;
    std::array<float,4> hz{300,1500,6500,12000},db{0,0,0,0},q{1,1,2,.7071f};float output=0;
};
class EqCore {
public:
    void prepare(double); void process(float*,float*,int,const EqParameters&) noexcept;
    static float response(double hz,double rate,const EqParameters&) noexcept;
    std::atomic<float> peak{0};
private:
    double sr=96000; std::array<Biquad,4> hp,eq;
    std::array<Ramp,4> frequencies,gains,qs,hpMix;
    Ramp cutoff,output; int controlPhase=0;
};
struct CompressorParameters { float threshold=0,ratio=1,attack=30,release=100,knee=0,makeup=0,mix=100,sidechain=80;bool scEnabled=false,autoRelease=false; };
class CompressorCore {
public:
    void prepare(double);void process(float*,float*,int,const CompressorParameters&) noexcept;
    std::atomic<float> reduction{0},peak{0};
private:
    double sr=96000;Biquad detector;float envelopeDb=0,programme=0;
    Ramp threshold,ratio,attack,release,knee,makeup,mix,cutoff,scMix;
};
// The meter's explicitly defined 8x, 32-tap, windowed-sinc reconstruction.
// Coefficients sum to one for each fractional phase.
struct Reconstruction {
    static constexpr int taps=32,phases=8;
    std::array<std::array<double,taps>,phases> h{};
    Reconstruction();
};
struct LimiterParameters { float input=0,ceiling=-1,release=100; };
class LimiterCore {
public:
    void prepare(double);void process(float*,float*,int,const LimiterParameters&) noexcept;
    int latency() const noexcept { return delay; }
    std::atomic<float> reduction{0},truePeak{0};
private:
    Reconstruction filter;
    double sr=96000; int delay=480,size=0,position=0;
    std::array<std::vector<float>,2> audio;
    std::vector<float> budgets;
    std::array<std::array<float,Reconstruction::taps>,2> history{},meterHistory{};
    int histPos=0,meterPos=0;
    Ramp input,ceiling,release;
    float previousGain=1;
};
} // namespace hem

```

## Source/DSP/FinishingCore.h

```cpp
#pragma once
#include "SpecialtyCore.h"

namespace hem {
// 4x oversampling with 65-tap Blackman-windowed low-pass filters on both sides
// of the nonlinearity. Fixed round-trip group delay: 16 base-rate samples.
// No allocation, trigonometry or coefficient design occurs in tick().
class OversampledSaturator {
public:
    static constexpr int taps=65,latencySamples=16;
    void prepare() noexcept {
        constexpr double cutoff=.1125;double sum=0;
        for(int i=0;i<taps;++i){const double x=i-32.;
            const double sinc=std::abs(x)<1.e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x);
            const double window=.42-.5*std::cos(2*pi*i/64)+.08*std::cos(4*pi*i/64);
            coefficients[size_t(i)]=sinc*window;sum+=coefficients[size_t(i)];}
        for(auto& v:coefficients)v/=sum;
        up={};down={};dry={};position={};dryPosition={};
    }
    float tick(float x,int channel,float drive,float bias,float amount) noexcept {
        const auto ch=size_t(channel);auto& dp=dryPosition[ch];
        const float delayed=dry[ch][size_t(dp)];dry[ch][size_t(dp)]=x;dp=(dp+1)%latencySamples;
        auto& pos=position[ch];float result=0;
        const double zero=std::tanh(double(bias));
        const double slope=std::max(.1,1-zero*zero);
        for(int phase=0;phase<4;++phase){
            up[ch][size_t(pos)]=phase==0?x*4:0;
            double interpolated=0;
            for(int k=0;k<taps;++k)interpolated+=coefficients[size_t(k)]*up[ch][size_t((pos-k+taps)%taps)];
            const double shaped=(std::tanh(interpolated*drive+bias)-zero)/(drive*slope);
            down[ch][size_t(pos)]=shaped;
            double filtered=0;
            for(int k=0;k<taps;++k)filtered+=coefficients[size_t(k)]*down[ch][size_t((pos-k+taps)%taps)];
            if(phase==0)result=float(filtered);
            pos=(pos+1)%taps;
        }
        return delayed+amount*(result-delayed);
    }
private:
    std::array<double,taps> coefficients{};
    std::array<std::array<double,taps>,2> up{},down{};
    std::array<std::array<float,latencySamples>,2> dry{};
    std::array<int,2> position{},dryPosition{};
};
struct TubeSettings {float drive=0,bias=0,tone=0,mix=100,output=0;};
class TubeCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;shaper.prepare();shelf.reset();dc.reset();phase=0;
        dc.configure(Biquad::HighPass,sr,8,.7071f);
        controls.reset(sr,{0,0,0,1,1});peak.store(0);
    }
    int latency() const noexcept {return OversampledSaturator::latencySamples;}
    void process(float* l,float* r,int n,const TubeSettings& p) noexcept {
        controls.target({p.drive,p.bias*.01f,p.tone,p.mix*.01f,gain(p.output)});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0)shelf.configure(Biquad::HighShelf,sr,6000,.7071f,v[2]);
            const float amount=std::clamp(v[0]/6.f,0.f,1.f),d=gain(v[0]);
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];
                const float shaped=shaper.tick(sample,ch,d,v[1],amount*v[3]);
                const float filtered=dc.tick(shaped,ch);
                const float dcRemoved=shaped+amount*v[3]*(filtered-shaped);
                const float toned=shelf.tick(dcRemoved,ch);
                sample=(dcRemoved+v[3]*(toned-dcRemoved))*v[4];
                maximum=std::max(maximum,std::abs(sample));
            }
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;OversampledSaturator shaper;Biquad shelf,dc;unsigned phase=0;Controls<5> controls;
};
struct TapeSettings {float drive=0,warmth=0,rolloff=0,mix=100,output=0;};
class TapeCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;shaper.prepare();headBump.reset();shelf.reset();phase=0;
        controls.reset(sr,{0,0,0,1,1});peak.store(0);
    }
    int latency() const noexcept {return OversampledSaturator::latencySamples;}
    void process(float* l,float* r,int n,const TapeSettings& p) noexcept {
        controls.target({p.drive,p.warmth,p.rolloff,p.mix*.01f,gain(p.output)});float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){headBump.configure(Biquad::Bell,sr,80,.7f,v[1]);shelf.configure(Biquad::HighShelf,sr,10000,.7071f,v[2]);}
            const float d=gain(v[0]),amount=std::clamp(v[0]/6.f,0.f,1.f);
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];
                // Symmetric soft saturation with independent low-frequency bump
                // and high-frequency loss. This is tape-inspired DSP, not a
                // measured magnetic hysteresis model of a specific machine.
                const float shaped=shaper.tick(sample,ch,d,0,amount*v[3]);
                const float toned=shelf.tick(headBump.tick(shaped,ch),ch);
                sample=(shaped+v[3]*(toned-shaped))*v[4];maximum=std::max(maximum,std::abs(sample));
            }
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;OversampledSaturator shaper;Biquad headBump,shelf;unsigned phase=0;Controls<5> controls;
};
struct VocalSettings {
    float cutoff=80,body=0,mud=0,presence=0,brightness=0,air=0;
    float resonance=2800,q=2,threshold=0,range=0,attack=5,release=120,output=0;
    bool cutEnabled=false;
};
class VocalCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;phase=0;hp.reset();for(auto& f:tone)f.reset();dynamic.prepare(sr);
        controls.reset(sr,{80,0,0,0,0,0,0,1});peak.store(0);
    }
    void process(float* l,float* r,int n,const VocalSettings& p) noexcept {
        controls.target({p.cutoff,p.body,p.mud,p.presence,p.brightness,p.air,p.cutEnabled?1.f:0.f,gain(p.output)});
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){hp.configure(Biquad::HighPass,sr,v[0],.7071f);
                constexpr std::array<float,5> frequencies{150,350,2500,5000,12000};
                constexpr std::array<float,5> qs{.7f,1.2f,.8f,.8f,.7071f};
                for(size_t b=0;b<5;++b)tone[b].configure(b==4?Biquad::HighShelf:Biquad::Bell,sr,frequencies[b],qs[b],v[b+1]);}
            for(int ch=0;ch<2;++ch){float& x=ch==0?l[i]:r[i];const float filtered=hp.tick(x,ch);x+=v[6]*(filtered-x);
                for(auto& f:tone)x=f.tick(x,ch);x*=v[7];}
        }
        DhtaSettings d;d.frequency=p.resonance;d.q=p.q;d.threshold=p.threshold;d.range=p.range;d.attack=p.attack;d.release=p.release;
        dynamic.process(l,r,n,d);float maximum=0;for(int i=0;i<n;++i)maximum=std::max({maximum,std::abs(l[i]),std::abs(r[i])});peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
    float reduction() const noexcept {return dynamic.reduction.load(std::memory_order_relaxed);}
private:double sr=96000;unsigned phase=0;Biquad hp;std::array<Biquad,5> tone;DhtaCore dynamic;Controls<8> controls;
};
struct MonitorSettings {float bass=0,body=0,presence=0,air=0,preamp=0;bool enabled=false;};
class MonitorCore {
public:
    void prepare(double rate) noexcept {sr=rate;phase=0;for(auto& f:filters)f.reset();controls.reset(sr,{0,0,0,0,1,0});peak.store(0);}
    void process(float* l,float* r,int n,const MonitorSettings& p) noexcept {
        controls.target({p.bass,p.body,p.presence,p.air,gain(p.preamp),p.enabled?1.f:0.f});float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){filters[0].configure(Biquad::Bell,sr,80,.7f,v[0]);filters[1].configure(Biquad::Bell,sr,250,1.f,v[1]);filters[2].configure(Biquad::Bell,sr,3000,1.f,v[2]);filters[3].configure(Biquad::HighShelf,sr,10000,.7071f,v[3]);}
            for(int ch=0;ch<2;++ch){float& sample=ch==0?l[i]:r[i];float wet=sample;for(auto& f:filters)wet=f.tick(wet,ch);
                sample+=v[5]*(wet*v[4]-sample);maximum=std::max(maximum,std::abs(sample));}
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;unsigned phase=0;std::array<Biquad,4> filters;Controls<6> controls;
};
}

```

## Source/DSP/MasterLimiter.h

```cpp
#pragma once
#include "DspCore.h"
#include <cstdint>
#include <stdexcept>

namespace hem {
// Mandatory final device stage. No bypass, makeup, or gain may follow this.
// The ceiling bounds the defined 8x/32-tap reconstruction, not unknown DACs.
// prepare() is a lifecycle operation; process() allocates nothing and locks nothing.
class MasterLimiter final {
public:
    void prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000)
            throw std::invalid_argument("Unsupported master limiter sample rate");
        core.prepare(sampleRate);
        faults.store(0); ready = true;
    }
    void process(float* left, float* right, int count) noexcept {
        if (!ready) { std::fill_n(left,count,0.f); std::fill_n(right,count,0.f); return; }
        // Broken upstream DSP must never poison the limiter history. A very
        // large finite overload is contained too, before float arithmetic.
        std::uint32_t bad = 0;
        for (int i=0;i<count;++i) for (auto* channel : {left,right}) {
            float& x=channel[i];
            if (!std::isfinite(x)) { x=0; ++bad; }
            else x=std::max(-1000000.f,std::min(1000000.f,x));
        }
        if (bad) faults.fetch_add(bad,std::memory_order_relaxed);
        LimiterParameters p; p.input=0; p.ceiling=-1.f; p.release=100.f;
        core.process(left,right,count,p);
    }
    int latencySamples() const noexcept { return core.latency(); }
    float truePeakDb() const noexcept { return decibels(core.truePeak.load()); }
    float reductionDb() const noexcept { return core.reduction.load(); }
    std::uint32_t invalidSampleCount() const noexcept { return faults.load(); }
private:
    LimiterCore core;
    std::atomic<std::uint32_t> faults{0};
    bool ready=false;
};
}

```

## Source/DSP/MonitoringRouter.h

```cpp
#pragma once
#include "DspCore.h"
#include <stdexcept>
namespace hem {
// Fixed-size prepared coefficients are built away from the audio thread.
// Device routing itself belongs to AudioDeviceManager; this class selects the
// listening signal for that device, never a Bluetooth codec or OS endpoint.
class MonitoringRouter final {
public:
    static constexpr int maxBands=12;
    struct Curve {std::array<Biquad,maxBands> filters;int count=0;float preamp=1;};
    void prepare(double rate) {
        active={};incoming={};transitioning=false;
        profileMix.reset(rate,0);profileMix.length=std::max(1,int(rate*.02));
        routeMix.reset(rate,0);routeMix.length=std::max(1,int(rate*.02));headphones.store(false);
    }
    // Host excludes the audio callback for this bounded struct copy. Reject
    // replacement during a fade, rather than resetting a live IIR mid-signal.
    bool installPrepared(const Curve& curve) noexcept {
        if(transitioning||curve.count<0||curve.count>maxBands)return false;
        incoming=curve;for(auto& f:incoming.filters)f.reset();transitioning=true;
        profileMix.value=profileMix.target=0;profileMix.remaining=0;profileMix.set(1);return true;
    }
    void setHeadphoneMode(bool enabled) noexcept {headphones.store(enabled,std::memory_order_relaxed);}
    bool headphoneMode() const noexcept {return headphones.load(std::memory_order_relaxed);}
    // Step 1: create the listening branch without modifying the master input.
    void renderCalibration(const float* left,const float* right,float* outL,float* outR,int n) noexcept {
        for(int i=0;i<n;++i){
            const float cleanL=std::isfinite(left[i])?std::clamp(left[i],-1000000.f,1000000.f):0.f;
            const float cleanR=std::isfinite(right[i])?std::clamp(right[i],-1000000.f,1000000.f):0.f;
            float aL=cleanL*active.preamp,aR=cleanR*active.preamp;
            for(int b=0;b<active.count;++b){aL=active.filters[size_t(b)].tick(aL,0);aR=active.filters[size_t(b)].tick(aR,1);}
            if(transitioning){float bL=cleanL*incoming.preamp,bR=cleanR*incoming.preamp;
                for(int b=0;b<incoming.count;++b){bL=incoming.filters[size_t(b)].tick(bL,0);bR=incoming.filters[size_t(b)].tick(bR,1);}
                const float mix=profileMix.next();aL+=mix*(bL-aL);aR+=mix*(bR-aR);
                if(profileMix.remaining==0){active=incoming;transitioning=false;}}
            outL[i]=aL;outR[i]=aR;
        }
    }
    // Optional manual listening EQ may process outL/outR between these stages.
    // Step 2: direct mode really bypasses ALL listening correction. Final
    // MasterLimiter must run after this mix; exports consume the master branch.
    void mixRoute(const float* masterL,const float* masterR,float* outL,float* outR,int n) noexcept {
        routeMix.set(headphoneMode()?1.f:0.f);
        for(int i=0;i<n;++i){const float mix=routeMix.next();
            if(mix==0){outL[i]=masterL[i];outR[i]=masterR[i];}
            else {outL[i]=masterL[i]+mix*(outL[i]-masterL[i]);outR[i]=masterR[i]+mix*(outR[i]-masterR[i]);}}
    }
private:
    Curve active,incoming;
    Ramp profileMix,routeMix;
    bool transitioning=false;
    std::atomic<bool> headphones{false};
};
}

```

## Source/DSP/RestorerCore.h

```cpp
#pragma once
#include "SpecialtyCore.h"
namespace hem {
struct RestorerSettings {
    float amount=0,floor=-60,release=100,protect=100,mix=100,output=0;
    bool learn=false;
};
// Eight complementary bands reconstructed by subtracting adjacent low-pass
// outputs. Explicit noise-only learning; no neural inference or AI detection.
class RestorerCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;env={};slow={};noise.fill(1.e-3f);learned=false;
        constexpr std::array<float,7> edges{125,250,500,1000,2000,4000,8000};
        for(size_t b=0;b<7;++b){filters[b].reset();filters[b].configure(Biquad::LowPass,sr,edges[b],.7071f);}
        controls.reset(sr,{0,-60,100,1,1,1,1});reduction.store(0);
    }
    void process(float* l,float* r,int n,const RestorerSettings& p) noexcept {
        controls.target({p.amount,p.floor,p.release,p.protect*.01f,p.mix*.01f,gain(p.output),p.learn?0.f:1.f});
        float maxReduction=0;
        for(int i=0;i<n;++i){auto v=controls.next();std::array<std::array<float,8>,2> bands{};
            for(int ch=0;ch<2;++ch){const float x=ch==0?l[i]:r[i];float previous=0;
                for(size_t b=0;b<7;++b){const float filtered=filters[b].tick(x,ch);bands[size_t(ch)][b]=filtered-previous;previous=filtered;}
                bands[size_t(ch)][7]=x-previous;
            }
            float outL=l[i],outR=r[i];
            for(size_t b=0;b<8;++b){const float x=std::max(std::abs(bands[0][b]),std::abs(bands[1][b]));
                follow(env[b],x,envelopeCoefficient(1,sr),envelopeCoefficient(v[2],sr));
                follow(slow[b],x,envelopeCoefficient(30,sr),envelopeCoefficient(250,sr));
                if(p.learn){noise[b]+=float(1-std::exp(-1/(sr*.2)))*(x-noise[b]);learned=true;}
                const float reference=learned?std::max(1.e-8f,noise[b]*2):gain(v[1]);
                const float lowLevel=std::clamp(1-env[b]/(reference*2),0.f,1.f);
                const float transient=std::clamp((env[b]-slow[b])/std::max(env[b],1.e-8f),0.f,1.f);
                const float cut=v[0]*lowLevel*(1-v[3]*transient)*v[6];
                const float removed=(1-gain(-cut))*v[4];
                outL-=removed*bands[0][b];outR-=removed*bands[1][b];
                maxReduction=std::max(maxReduction,cut*v[4]);
            }
            l[i]=outL*v[5];r[i]=outR*v[5];
        }
        reduction.store(maxReduction,std::memory_order_relaxed);
    }
    std::atomic<float> reduction{0};
private:
    double sr=96000;bool learned=false;
    std::array<Biquad,7> filters;std::array<float,8> env{},slow{},noise{};Controls<7> controls;
};
}

```

## Source/DSP/SpecialtyCore.h

```cpp
#pragma once
#include "DspCore.h"

namespace hem {
// All filter histories and parameter ramps belong to the audio thread. Meters
// are the only data shared back to editors, through atomic stores/loads.
inline float envelopeCoefficient(float ms, double sr) noexcept {
    return float(std::exp(-1.0 / (sr * std::max(.05f, ms) * .001)));
}
inline void follow(float& envelope, float x, float attack, float release) noexcept {
    const float c = x > envelope ? attack : release;
    envelope = x + c * (envelope - x);
}
template<size_t N> struct Controls {
    std::array<Ramp,N> ramps;
    void reset(double sr, const std::array<float,N>& values) noexcept {
        for(size_t i=0;i<N;++i) ramps[i].reset(sr,values[i]);
    }
    void target(const std::array<float,N>& values) noexcept {
        for(size_t i=0;i<N;++i) ramps[i].set(values[i]);
    }
    std::array<float,N> next() noexcept {
        std::array<float,N> v{};
        for(size_t i=0;i<N;++i) v[i]=ramps[i].next();
        return v;
    }
};
struct DhtaSettings {
    float frequency=6800,q=2,threshold=0,range=0,attack=2,release=80,mix=100,output=0;
    bool wide=false,delta=false;
};
class DhtaCore {
public:
    void prepare(double rate) noexcept {
        sr=rate;band.reset();env=0;phase=0;
        controls.reset(sr,{6800,2,0,0,2,80,1,1,0,0});reduction.store(0);
    }
    void process(float* l,float* r,int n,const DhtaSettings& p) noexcept {
        controls.target({p.frequency,p.q,p.threshold,p.range,p.attack,p.release,p.mix*.01f,gain(p.output),p.wide?1.f:0.f,p.delta?1.f:0.f});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){
                // Constant-peak RBJ band-pass. Subtracting its attenuated
                // contribution creates a dynamically controlled notch.
                const double w=2*pi*std::clamp(double(v[0]),20.,sr*.45)/sr;
                const double alpha=std::sin(w)/(2*std::max(.5f,v[1]));
                band.b0=alpha/(1+alpha);band.b1=0;band.b2=-band.b0;
                band.a1=-2*std::cos(w)/(1+alpha);band.a2=(1-alpha)/(1+alpha);
            }
            const float bl=band.tick(l[i],0),br=band.tick(r[i],1);
            const float excess=std::max(0.f,decibels(std::max(std::abs(bl),std::abs(br)))-v[2]);
            follow(env,std::min(std::abs(v[3]),excess),envelopeCoefficient(v[4],sr),envelopeCoefficient(v[5],sr));
            const float attenuation=1-gain(-env);
            const float removedL=attenuation*(bl+v[8]*(l[i]-bl));
            const float removedR=attenuation*(br+v[8]*(r[i]-br));
            const float processedL=l[i]-v[6]*removedL,processedR=r[i]-v[6]*removedR;
            l[i]=(processedL+v[9]*(v[6]*removedL-processedL))*v[7];
            r[i]=(processedR+v[9]*(v[6]*removedR-processedR))*v[7];
            maximum=std::max(maximum,env*v[6]);
        }
        reduction.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> reduction{0};
private:
    double sr=96000;Biquad band;float env=0;unsigned phase=0;Controls<10> controls;
};
struct TransientSettings {float attack=0,sustain=0,speed=2,recovery=100,mix=100,output=0;};
class TransientCore {
public:
    void prepare(double rate) noexcept {sr=rate;fast=slow=0;controls.reset(sr,{0,0,2,100,1,1});gainChange.store(0);}
    void process(float* l,float* r,int n,const TransientSettings& p) noexcept {
        controls.target({p.attack,p.sustain,p.speed,p.recovery,p.mix*.01f,gain(p.output)});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();const float x=std::max(std::abs(l[i]),std::abs(r[i]));
            follow(fast,x,envelopeCoefficient(v[2],sr),envelopeCoefficient(v[3]*.25f,sr));
            follow(slow,x,envelopeCoefficient(v[2]*8,sr),envelopeCoefficient(v[3],sr));
            // Positive envelope contrast identifies onset; negative contrast
            // identifies decay. Amounts are bounded in dB, not threshold gain.
            const float contrast=(fast-slow)/std::max(1.e-7f,std::max(fast,slow));
            const float db=v[0]*std::max(0.f,contrast)+v[1]*std::max(0.f,-contrast);
            const float g=(1+v[4]*(gain(db)-1))*v[5];l[i]*=g;r[i]*=g;
            if(std::abs(db)>std::abs(maximum))maximum=db;
        }
        gainChange.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> gainChange{0};
private:double sr=96000;float fast=0,slow=0;Controls<6> controls;
};
struct SubBassSettings {float crossover=100,lowGain=0,subCut=25,drive=0,mono=0,mix=100,output=0;bool cutEnabled=false;};
class SubBassCore {
public:
    void prepare(double rate) noexcept {sr=rate;low.reset();hp.reset();phase=0;controls.reset(sr,{100,1,25,0,0,1,1,0});peak.store(0);}
    void process(float* l,float* r,int n,const SubBassSettings& p) noexcept {
        controls.target({p.crossover,gain(p.lowGain),p.subCut,p.drive,p.mono*.01f,p.mix*.01f,gain(p.output),p.cutEnabled?1.f:0.f});
        float maximum=0;
        for(int i=0;i<n;++i){const auto v=controls.next();
            if(phase++%16==0){low.configure(Biquad::LowPass,sr,v[0],.7071f);hp.configure(Biquad::HighPass,sr,v[2],.7071f);}
            const float a=low.tick(l[i],0),b=low.tick(r[i],1),mid=(a+b)*.5f;
            const float d=gain(v[3]);const float nonlinear=v[3]/12.f;
            // Subtractive split guarantees exact dry reconstruction at unity.
            // Saturation is restricted to the extracted low band. No fabricated
            // subharmonic oscillator is mixed into the source.
            float bassL=a+v[4]*(mid-a),bassR=b+v[4]*(mid-b);
            bassL+=nonlinear*(std::tanh(bassL*d)/d-bassL);
            bassR+=nonlinear*(std::tanh(bassR*d)/d-bassR);
            float wetL=l[i]+bassL*v[1]-a,wetR=r[i]+bassR*v[1]-b;
            const float cutL=hp.tick(wetL,0),cutR=hp.tick(wetR,1);
            wetL+=v[7]*(cutL-wetL);wetR+=v[7]*(cutR-wetR);
            l[i]=(l[i]+v[5]*(wetL-l[i]))*v[6];r[i]=(r[i]+v[5]*(wetR-r[i]))*v[6];
            maximum=std::max({maximum,std::abs(l[i]),std::abs(r[i])});
        }
        peak.store(maximum,std::memory_order_relaxed);
    }
    std::atomic<float> peak{0};
private:double sr=96000;Biquad low,hp;unsigned phase=0;Controls<8> controls;
};
}

```

## Source/HardwareLookAndFeel.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "UI/ControlGeometry.h"

// Both modes draw real controls. Artwork mode keeps its geometry inside the
// mapped host hitboxes; it never suppresses state or repaints the whole faceplate.
// Names/value labels remain separate, upright components; only dial pointers turn.
class HardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit HardwareLookAndFeel(bool imageOverlay = false) : artworkOverlay(imageOverlay) {
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0b1117));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff65300c));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xff70f3ff));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffd18a));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1117));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff007f90));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff080c12));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::Label::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffffbb55));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff090d12));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff24505c));
        setColour(juce::Slider::thumbColourId, amber());
        setColour(juce::Slider::trackColourId, cyan());
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff142c34));
        setColour(juce::ToggleButton::textColourId, juce::Colour(0xff70f3ff));
        setColour(juce::ToggleButton::tickColourId, amber());
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff52616a));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float start, float end, juce::Slider& slider) override {
        const auto d = hem::ui::dialGeometry(float(x), float(y), float(w), float(h), position, start, end);
        if (!d.drawable) return;
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(x, y, w, h);
        const float scale = juce::jlimit(0.55f, 1.8f, float(juce::jmin(w, h)) / 96.0f);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const bool focused = slider.hasKeyboardFocus(true);
        const bool touched = slider.isMouseOverOrDragging();
        const auto accent = (touched ? amber() : cyan()).withAlpha(alpha);
        const auto circle = juce::Rectangle<float>(d.centreX - d.radius, d.centreY - d.radius,
                                                    d.radius * 2.0f, d.radius * 2.0f);

        // An opaque inner body covers the artwork's static centre. Its original
        // outer metal trim remains visible around the host's live replacement.
        g.setColour(juce::Colour(0xff03080d));
        g.fillEllipse(circle.expanded(2.0f * scale));
        juce::Path track;
        track.addCentredArc(d.centreX, d.centreY, d.ringRadius, d.ringRadius, 0, start, end, true);
        g.setColour(juce::Colour(0xff183c46).withAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(3.0f * scale));
        juce::Path progress;
        progress.addCentredArc(d.centreX, d.centreY, d.ringRadius, d.ringRadius, 0, start, d.angle, true);
        g.setColour(accent);
        g.strokePath(progress, juce::PathStrokeType(2.5f * scale));
        juce::ColourGradient metal(juce::Colour(0xff56616b).withMultipliedAlpha(alpha),
                                   circle.getX(), circle.getY(), juce::Colour(0xff070b12),
                                   circle.getRight(), circle.getBottom(), false);
        g.setGradientFill(metal);
        g.fillEllipse(circle);
        g.setColour(accent.withMultipliedAlpha(touched || focused ? 1.0f : 0.65f));
        g.drawEllipse(circle, (focused ? 2.0f : 1.2f) * scale);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawEllipse(circle.reduced(3.0f * scale), 1.0f * scale);
        // The current JUCE normalized position drives only this pointer and arc.
        g.setColour(amber().withAlpha(alpha));
        g.drawLine(d.pointerStartX, d.pointerStartY, d.pointerEndX, d.pointerEndY, 3.0f * scale);
        const float cap = 3.0f * scale;
        g.fillEllipse(d.pointerEndX - cap * 0.5f, d.pointerEndY - cap * 0.5f, cap, cap);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float minimum, float maximum, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override {
        // Retain JUCE's own multi-thumb/bar semantics instead of silently drawing
        // a one-thumb control for a different slider type.
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical) {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, position, minimum, maximum, style, slider);
            return;
        }
        if (w <= 0 || h <= 0) return;
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(slider.getLocalBounds());
        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const float scale = juce::jlimit(0.65f, 1.5f, float(juce::jmin(w, h)) / 32.0f);
        const float centreX = float(x) + float(w) * 0.5f;
        const float centreY = float(y) + float(h) * 0.5f;
        const float pos = hem::ui::boundedSliderPosition(position, float(horizontal ? x : y),
                                                        float(horizontal ? w : h));
        const auto track = horizontal ? juce::Line<float>(float(x), centreY, float(x + w), centreY)
                                      : juce::Line<float>(centreX, float(y + h), centreX, float(y));
        const auto active = horizontal ? juce::Line<float>(float(x), centreY, pos, centreY)
                                       : juce::Line<float>(centreX, float(y + h), centreX, pos);
        g.setColour(juce::Colour(0xff020609));
        g.drawLine(track, 8.0f * scale);
        g.setColour(juce::Colour(0xff22434d).withAlpha(alpha));
        g.drawLine(track, 4.0f * scale);
        g.setColour(cyan().withAlpha(alpha));
        g.drawLine(active, 3.0f * scale);
        const float across = juce::jmin(float(horizontal ? h : w) * 0.75f, 26.0f * scale);
        const float along = 11.0f * scale;
        const auto thumb = horizontal ? juce::Rectangle<float>(pos - along * 0.5f, centreY - across * 0.5f, along, across)
                                       : juce::Rectangle<float>(centreX - across * 0.5f, pos - along * 0.5f, across, along);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5f6c76), thumb.getX(), thumb.getY(),
                                              juce::Colour(0xff0b1117), thumb.getRight(), thumb.getBottom(), false));
        g.fillRoundedRectangle(thumb, 2.0f * scale);
        g.setColour(amber().withAlpha(alpha));
        g.drawRoundedRectangle(thumb, 2.0f * scale, 1.3f * scale);
        if (horizontal) g.drawVerticalLine(int(std::round(pos)), thumb.getY() + 3 * scale, thumb.getBottom() - 3 * scale);
        else g.drawHorizontalLine(int(std::round(pos)), thumb.getX() + 3 * scale, thumb.getRight() - 3 * scale);
        if (slider.hasKeyboardFocus(true)) {
            g.setColour(cyan().withAlpha(alpha));
            g.drawRoundedRectangle(thumb.expanded(2.0f * scale), 3.0f * scale, 1.0f);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool over, bool down) override {
        if (button.getWidth() < 3 || button.getHeight() < 3) return;
        const bool active = button.getToggleState() || down;
        const float alpha = button.isEnabled() ? 1.0f : 0.35f;
        const float scale = juce::jlimit(0.65f, 1.3f, float(button.getHeight()) / 44.0f);
        const auto bounds = button.getLocalBounds().toFloat().reduced(2.0f * scale);
        const auto accent = (active ? amber() : cyan()).withAlpha(alpha);
        const auto base = active ? button.findColour(juce::TextButton::buttonOnColourId) : background;
        // Opaque glass conceals static faceplate icons under the live text. It is
        // inset, leaving the artwork's original outer bevel undisturbed.
        g.setGradientFill(juce::ColourGradient(base.brighter(over ? 0.30f : 0.13f), bounds.getX(), bounds.getY(),
                                              base.darker(0.4f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 4.0f * scale);
        if (active) {
            g.setColour(accent.withMultipliedAlpha(0.22f));
            g.drawRoundedRectangle(bounds.reduced(1.0f * scale), 4.0f * scale, 4.0f * scale);
        }
        g.setColour(accent.withMultipliedAlpha(active || over ? 1.0f : 0.50f));
        g.drawRoundedRectangle(bounds, 4.0f * scale, (active ? 1.8f : 1.0f) * scale);
        if (active) {
            g.setColour(accent);
            g.fillRoundedRectangle(bounds.getCentreX() - bounds.getWidth() * 0.17f,
                                   bounds.getBottom() - 4.0f * scale, bounds.getWidth() * 0.34f,
                                   2.0f * scale, 1.0f * scale);
        }
        if (button.hasKeyboardFocus(true)) {
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.drawRoundedRectangle(bounds.reduced(3.0f * scale), 2.0f * scale, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) override {
        auto colour = button.findColour((button.getToggleState() || down) ? juce::TextButton::textColourOnId
                                                                         : juce::TextButton::textColourOffId);
        if (!button.isEnabled()) colour = colour.withMultipliedAlpha(0.40f);
        const float fontHeight = juce::jlimit(10.0f, artworkOverlay ? 16.0f : 15.0f,
                                            float(button.getHeight()) * 0.36f);
        g.setFont(juce::Font(juce::FontOptions(fontHeight).withStyle("Bold")));
        const auto textBounds=button.getLocalBounds().reduced(6,3);
        if(button.isEnabled()) {
            g.setColour(colour.withAlpha(0.18f));
            for(const auto offset:std::array<juce::Point<int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                g.drawFittedText(button.getButtonText(),textBounds.translated(offset.x,offset.y),juce::Justification::centred,1,0.65f);
        }
        g.setColour(colour);g.drawFittedText(button.getButtonText(),textBounds,juce::Justification::centred,1,0.65f);
    }

    void drawLabel(juce::Graphics& g,juce::Label& label) override {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if(!label.isBeingEdited()) {
            const auto font=getLabelFont(label);g.setFont(font);
            const auto bounds=label.getBorderSize().subtractedFrom(label.getLocalBounds());
            const int lines=juce::jmax(1,int(float(bounds.getHeight())/font.getHeight()));
            const auto colour=label.findColour(juce::Label::textColourId).withMultipliedAlpha(label.isEnabled()?1.f:.45f);
            if(label.isEnabled()) {
                g.setColour(colour.withMultipliedAlpha(.16f));
                for(const auto offset:std::array<juce::Point<int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
                    g.drawFittedText(label.getText(),bounds.translated(offset.x,offset.y),label.getJustificationType(),lines,label.getMinimumHorizontalScale());
            }
            g.setColour(colour);g.drawFittedText(label.getText(),bounds,label.getJustificationType(),lines,label.getMinimumHorizontalScale());
        }
        g.setColour(label.findColour(juce::Label::outlineColourId));g.drawRect(label.getLocalBounds());
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override {
        const float alpha = button.isEnabled() ? 1.0f : 0.35f;
        const bool active = button.getToggleState();
        const auto accent = (active || down ? amber() : cyan()).withAlpha(alpha);
        const float size = juce::jmin(22.0f, float(button.getHeight()) * 0.65f);
        const auto lamp = juce::Rectangle<float>(6.0f, (float(button.getHeight()) - size) * 0.5f, size, size);
        g.setColour(active ? juce::Colour(0xff65300c) : juce::Colour(0xff09131b));
        g.fillRoundedRectangle(lamp, 4.0f);
        g.setColour(accent.withMultipliedAlpha(active || over || down ? 1.0f : 0.55f));
        g.drawRoundedRectangle(lamp, 4.0f, active || down ? 2.0f : 1.0f);
        if (active) {
            juce::Path tick;
            tick.startNewSubPath(lamp.getX() + size * 0.22f, lamp.getY() + size * 0.51f);
            tick.lineTo(lamp.getX() + size * 0.43f, lamp.getY() + size * 0.72f);
            tick.lineTo(lamp.getX() + size * 0.79f, lamp.getY() + size * 0.28f);
            g.strokePath(tick, juce::PathStrokeType(2.3f));
        }
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha));
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(11.0f, 15.0f, float(button.getHeight()) * 0.36f))));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(int(size) + 16).reduced(2),
                         juce::Justification::centredLeft, 2);
        if (button.hasKeyboardFocus(true)) {
            g.setColour(cyan().withAlpha(alpha));
            g.drawRoundedRectangle(button.getLocalBounds().toFloat().reduced(1), 3, 1);
        }
    }

private:
    static juce::Colour cyan() { return juce::Colour(0xff00e5ff); }
    static juce::Colour amber() { return juce::Colour(0xffffaa44); }
    const bool artworkOverlay;
};

```

## Source/InsertChain.cpp

```cpp
#include "InsertChain.h"
#include <stdexcept>
namespace hem {
void InsertChain::PreparedNode::prepare(double sr,int block,int budget){
 processor->setPlayConfigDetails(2,2,sr,block);processor->prepareToPlay(sr,block);
 const int latency=processor->processingReady()?processor->getLatencySamples():0;
 if(latency<0||latency>budget)throw std::runtime_error("Processor latency exceeds this host's 6 ms per-slot budget");
 compensation.prepare(budget-latency);
}
InsertChain::InsertChain(){for(auto& s:slots)s.owned.reserve(3);}
void InsertChain::prepare(double rate,int block){sampleRate=rate;maximumBlock=std::max(1,block);latencyBudget=int(std::ceil(rate*.006));
 for(auto* pair:{&dry,&oldWet,&newWet})for(auto& ch:*pair)ch.assign(size_t(maximumBlock),0);
 for(auto& s:slots){s.dryDelay.prepare(latencyBudget);s.wet.reset(rate,s.bypassed.load()?0.f:1.f);s.replace.reset(rate,0);s.replace.length=std::max(1,int(rate*.02));s.transitioning=false;s.warmup=0;
    for(auto& p:s.owned)p->prepare(rate,maximumBlock,latencyBudget);}
}
void InsertChain::release(){for(auto& s:slots)for(auto& p:s.owned)p->processor->releaseResources();}
std::unique_ptr<InsertChain::PreparedNode> InsertChain::stageNode(std::unique_ptr<ModuleProcessor> p,double rate,int block,int budget){
 if(!p)return {};if(p->monitorOnly())throw std::invalid_argument("Monitoring-only processors cannot enter a track insert rack");auto node=std::make_unique<PreparedNode>();node->processor=std::move(p);node->prepare(rate,block,budget);return node;
}
void InsertChain::installInitial(int i,std::unique_ptr<PreparedNode> node,bool bypassed){
 if(i<0||i>=slotCount)throw std::out_of_range("Insert slot");
 auto& s=slots[size_t(i)];
 if(!s.owned.empty()||s.request.load()||s.busy.load())throw std::logic_error("Initial install requires an empty unpublished slot");
 auto* pointer=node.get();if(node)s.owned.push_back(std::move(node));
 s.active.store(pointer);s.bypassed.store(bypassed);s.wet.reset(sampleRate,bypassed?0.f:1.f);
}
bool InsertChain::isBusy(int i) const noexcept {if(i<0||i>=slotCount)return true;const auto& s=slots[size_t(i)];return s.busy.load(std::memory_order_acquire)||s.retired.load(std::memory_order_acquire)!=nullptr;}
bool InsertChain::publish(int i,std::unique_ptr<PreparedNode> node){if(isBusy(i))return false;auto& s=slots[size_t(i)];auto* ptr=node.get();
 if(node)s.owned.push_back(std::move(node));s.busy.store(true,std::memory_order_release);s.incoming.store(ptr,std::memory_order_release);s.request.store(true,std::memory_order_release);return true;
}
bool InsertChain::publishStopped(int i,std::unique_ptr<PreparedNode> node){
 if(!publish(i,std::move(node)))return false;completePendingStopped();return true;
}
void InsertChain::completePendingStopped(){
 for(auto& s:slots){
  if(!s.request.load(std::memory_order_acquire))continue;
  auto* old=s.active.load(std::memory_order_relaxed);
  s.active.store(s.incoming.load(std::memory_order_acquire),std::memory_order_release);
  s.incoming.store(nullptr,std::memory_order_release);s.retired.store(old,std::memory_order_release);
  s.request.store(false,std::memory_order_release);s.busy.store(false,std::memory_order_release);
  s.transitioning=false;s.warmup=0;s.replace.reset(sampleRate,1);s.wet.reset(sampleRate,s.bypassed.load()?0.f:1.f);
 }
}
std::unique_ptr<InsertChain::PreparedNode> InsertChain::collectRetired(int i){auto& s=slots[size_t(i)];auto* ptr=s.retired.exchange(nullptr,std::memory_order_acq_rel);if(!ptr)return {};
 auto it=std::find_if(s.owned.begin(),s.owned.end(),[ptr](const auto& p){return p.get()==ptr;});if(it==s.owned.end())return {};auto node=std::move(*it);s.owned.erase(it);return node;
}
ModuleProcessor* InsertChain::activeProcessor(int i) const noexcept {auto* p=slots[size_t(i)].active.load(std::memory_order_acquire);return p?p->processor.get():nullptr;}
void InsertChain::bypass(int i,bool b) noexcept {if(i>=0&&i<slotCount)slots[size_t(i)].bypassed.store(b,std::memory_order_release);}
bool InsertChain::isBypassed(int i) const noexcept {return slots[size_t(i)].bypassed.load(std::memory_order_acquire);}
bool InsertChain::swapSlotsStopped(int a,int b){
 if(a<0||b<0||a>=slotCount||b>=slotCount||isBusy(a)||isBusy(b))return false;
 // Message thread, transport stopped, device callback excluded by the host.
 auto& x=slots[size_t(a)];auto& y=slots[size_t(b)];
 auto* xp=x.active.load();auto* yp=y.active.load();
 x.owned.swap(y.owned);x.active.store(yp);y.active.store(xp);
 const bool xb=x.bypassed.load(),yb=y.bypassed.load();x.bypassed.store(yb);y.bypassed.store(xb);
 x.wet.reset(sampleRate,yb?0.f:1.f);y.wet.reset(sampleRate,xb?0.f:1.f);
 return true;
}
bool InsertChain::run(PreparedNode* node,float* l,float* r,int n) noexcept {if(!node||!node->processor->processingReady())return false;
 float* channels[]{l,r};view.setDataToReferTo(channels,2,n);midi.clear();node->processor->processBlock(view,midi);
 // A plug-in can change latency/layout while processing this block. Never emit
 // its now-uncompensated output; use the slot's already aligned dry path.
 if(!node->processor->processingReady())return false;node->compensation.process(l,r,n);return true;
}
void InsertChain::process(float* left,float* right,int samples) noexcept {
 for(int offset=0;offset<samples;offset+=maximumBlock){const int n=std::min(maximumBlock,samples-offset);float* l=left+offset;float* r=right+offset;
  for(auto& s:slots){auto* old=s.active.load(std::memory_order_relaxed);
    if(!s.transitioning&&s.request.load(std::memory_order_acquire)) {s.transitioning=true;s.warmup=latencyBudget;s.replace.reset(sampleRate,0);s.replace.length=std::max(1,int(sampleRate*.02));}
    auto* next=s.transitioning?s.incoming.load(std::memory_order_acquire):nullptr;
    std::copy_n(l,n,dry[0].data());std::copy_n(r,n,dry[1].data());s.dryDelay.process(dry[0].data(),dry[1].data(),n);
    if(old){std::copy_n(l,n,oldWet[0].data());std::copy_n(r,n,oldWet[1].data());if(!run(old,oldWet[0].data(),oldWet[1].data(),n)){std::copy_n(dry[0].data(),n,oldWet[0].data());std::copy_n(dry[1].data(),n,oldWet[1].data());}}
    else {std::copy_n(dry[0].data(),n,oldWet[0].data());std::copy_n(dry[1].data(),n,oldWet[1].data());}
    if(s.transitioning){if(next){std::copy_n(l,n,newWet[0].data());std::copy_n(r,n,newWet[1].data());if(!run(next,newWet[0].data(),newWet[1].data(),n)){std::copy_n(dry[0].data(),n,newWet[0].data());std::copy_n(dry[1].data(),n,newWet[1].data());}}
        else {std::copy_n(dry[0].data(),n,newWet[0].data());std::copy_n(dry[1].data(),n,newWet[1].data());}}
    s.wet.set(s.bypassed.load(std::memory_order_acquire)?0.f:1.f);
    for(int i=0;i<n;++i){if(s.transitioning&&s.warmup>0){if(--s.warmup==0)s.replace.set(1);}
        const float blend=s.transitioning?s.replace.next():0,wet=s.wet.next();
        l[i]=dry[0][size_t(i)]+wet*(oldWet[0][size_t(i)]+blend*(newWet[0][size_t(i)]-oldWet[0][size_t(i)])-dry[0][size_t(i)]);
        r[i]=dry[1][size_t(i)]+wet*(oldWet[1][size_t(i)]+blend*(newWet[1][size_t(i)]-oldWet[1][size_t(i)])-dry[1][size_t(i)]);}
    if(s.transitioning&&s.warmup==0&&s.replace.remaining==0&&s.replace.value==1){
        s.active.store(next,std::memory_order_release);s.incoming.store(nullptr,std::memory_order_release);s.retired.store(old,std::memory_order_release);
        s.request.store(false,std::memory_order_release);s.transitioning=false;s.busy.store(false,std::memory_order_release);}
  }
 }
}
}

```

## Source/InsertChain.h

```cpp
#pragma once
#include "Plugins/Processors.h"
#include <array>

namespace hem {
// Message-thread ownership; audio-thread borrowed pointers and bounded fades.
// Audio process() never acquires a lock, allocates memory, or destroys a node.
// The owner must hold AudioDeviceManager::getAudioCallbackLock() for prepare(),
// release(), publish() and collectRetired(). stageNode() deliberately runs outside
// that lock. These lifecycle methods are not called by process().
class InsertChain {
public:
 static constexpr int slotCount=8;
 struct PreparedNode {
    std::unique_ptr<ModuleProcessor> processor;
    HostDelay compensation;
    void prepare(double sr,int block,int budget);
 };
 InsertChain();
 void prepare(double rate,int maximumBlock);
 void release();
 std::unique_ptr<PreparedNode> stageNode(std::unique_ptr<ModuleProcessor>,double rate,int block,int budget);
 // Only for an unpublished, prepared, empty chain on the staging worker.
 void installInitial(int,std::unique_ptr<PreparedNode>,bool bypassed);
 bool publish(int,std::unique_ptr<PreparedNode>); // null means remove.
 // Message thread only, with NO audio callback running. Publishes without
 // waiting for an unavailable callback; old ownership remains in retired.
 bool publishStopped(int,std::unique_ptr<PreparedNode>);
 void completePendingStopped();
 std::unique_ptr<PreparedNode> collectRetired(int);
 ModuleProcessor* activeProcessor(int) const noexcept;
 bool isBusy(int) const noexcept;
 void bypass(int,bool) noexcept;bool isBypassed(int) const noexcept;
 void process(float* left,float* right,int samples) noexcept;
 bool swapSlotsStopped(int first,int second);
 int latencySamples() const noexcept {return latencyBudget*slotCount;}
 int slotLatency() const noexcept {return latencyBudget;}
private:
 struct Slot {
    std::vector<std::unique_ptr<PreparedNode>> owned;
    std::atomic<PreparedNode*> active{nullptr},incoming{nullptr},retired{nullptr};
    std::atomic<bool> request{false},busy{false},bypassed{false};
    HostDelay dryDelay;Ramp wet,replace;
    bool transitioning=false;int warmup=0;
 };
 std::array<Slot,slotCount> slots;
 std::array<std::vector<float>,2> dry,oldWet,newWet;
 juce::AudioBuffer<float> view;
 juce::MidiBuffer midi;
 double sampleRate=96000;int maximumBlock=256;std::atomic<int> latencyBudget{576};
 bool run(PreparedNode*,float*,float*,int) noexcept;
};
}

```

## Source/Main.cpp

```cpp
#include "MainComponent.h"

class StudioApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Hybrid Elite Mafia - Studio Suite"; }
    const juce::String getApplicationVersion() override { return "0.10.0"; }
    void initialise (const juce::String&) override { window = std::make_unique<Window> (getApplicationName()); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { if(window)window->requestClose();else quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff020508), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (768, 432, 7680, 4320);
            centreWithSize (1280, 720);
            setVisible (true);
        }
        void requestClose(){if(auto* studio=dynamic_cast<MainComponent*>(getContentComponent()))studio->requestClose([]{juce::JUCEApplication::getInstance()->quit();});else juce::JUCEApplication::getInstance()->quit();}
        void closeButtonPressed() override { requestClose(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (StudioApplication)

```

## Source/MainComponent.cpp

```cpp
#include "MainComponent.h"
#include <BinaryData.h>
#include "UI/StudioBranding.h"
namespace {
juce::String timeText(double t){int n=int(std::max(0.0,t));return juce::String(n/60)+":"+juce::String(n%60).paddedLeft('0',2);}
}
MainComponent::MainComponent(){
 setLookAndFeel(&look);setOpaque(true);addAndMakeVisible(outputMeterView);
 faceplate=juce::ImageCache::getFromMemory(BinaryData::studio_suite_master_png,BinaryData::studio_suite_master_pngSize);
 ghostBrand=juce::ImageCache::getFromMemory(BinaryData::ghost_brand_reference_png,BinaryData::ghost_brand_reference_pngSize);
 formats.registerBasicFormats();diskThread.startThread();configureKnob(inputKnob,inputDb,"INPUT");configureKnob(outputKnob,outputDb,"MASTER HEADROOM / OUTPUT");
 for(auto* b:std::array<juce::TextButton*,7>{&play,&stop,&rewind,&forward,&loop,&file,&settings}){addAndMakeVisible(*b);b->setLookAndFeel(&overlayLook);}
 play.setButtonText("PLAY");stop.setButtonText("STOP");rewind.setButtonText("REWIND");forward.setButtonText("+10 SEC");loop.setButtonText("LOOP");file.setButtonText("FILE");settings.setButtonText("I/O");
 play.setTooltip("Play all loaded tracks");stop.setTooltip("Stop all and rewind");rewind.setTooltip("Rewind all");forward.setTooltip("Forward 10 seconds");loop.setTooltip("Loop tracks individually");file.setTooltip("Load selected track");settings.setTooltip("Audio devices");
 file.onClick=[this]{openAudio();};settings.onClick=[this]{showSettings();};play.onClick=[this]{playAll();};stop.onClick=[this]{stopAll(true);};
 rewind.onClick=[this]{seekAll(0);};forward.onClick=[this]{seekAll(sessionPosition()+10);};
 loop.onClick=[this]{
    if(vocals.recorder.busy()){message.setText("Finish the take before changing loop mode",juce::dontSendNotification);return;}
    if(sessionWorker&&sessionWorker->isBusy())return;
    const bool resume=playing();const double commonPosition=sessionPosition();stopAll(false);looping=!looping;
    for(auto& t:bank->tracks)if(t.loaded){const auto position=t.transport().getCurrentPosition();t.transport().setSource(nullptr);t.playback->reader->setLooping(looping);t.transport().setSource(t.playback->reader.get(),32768,&diskThread,t.sourceRate,2);t.transport().setPosition(position);}
    seekAll(commonPosition);if(resume)playAll();repaint();
 };
 for(int i=0;i<8;++i){addAndMakeVisible(slotButtons[size_t(i)]);slotButtons[size_t(i)].onClick=[this,i]{selectSlot(i);openPlugin(i);};}
 addAndMakeVisible(bay);
 importer=std::make_unique<hem::StemFileImporter>(diskThread,[this]{
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    return hem::StemFileImporter::Context{prepared?sampleRate:0,blockSize,generation,looping};
 },[this](hem::StemFileImporter::Batch& batch){return commitImport(batch);});
 bay.addAndMakeVisible(*importer);
 sessionWorker=std::make_unique<hem::SessionWorker>(diskThread,
    [this](std::unique_ptr<hem::TrackBank>& b,const hem::SessionSnapshot& s,hem::SessionWorker::Context c){return commitSession(b,s,c);},
    [this](const juce::String& text){message.setText(text,juce::dontSendNotification);},
    [this](bool saving,const juce::File& file,const juce::Result& result){sessionFinished(saving,file,result);},
    [this](hem::TrackBank& b,const hem::SessionSnapshot& s,hem::SessionWorker::Context c){return stageExternalSession(b,s,c);});
 exportWorker=std::make_unique<hem::OfflineMixWorker>([this](const juce::String& text){message.setText(text,juce::dontSendNotification);},&externalHost);
 bay.addAndMakeVisible(exportButton);exportButton.onClick=[this]{showExport();};
 bay.addAndMakeVisible(saveSessionButton);bay.addAndMakeVisible(loadSessionButton);
 bay.addAndMakeVisible(vocalButton);vocalButton.onClick=[this]{showVocals();};
 bay.addAndMakeVisible(analysisButton);bay.addAndMakeVisible(calibrationButton);bay.addAndMakeVisible(routeButton);
 analysisButton.onClick=[this]{showAnalysis();};
 routeButton.onClick=[this]{monitoringRouter.setHeadphoneMode(!monitoringRouter.headphoneMode());};
 calibrationButton.onClick=[this]{openCalibration();};
 profileLoader=std::make_unique<hem::MonitorProfileLoader>(
    [this](const hem::MonitorProfile& p,const hem::MonitoringRouter::Curve& curve,unsigned gen){
        bool installed=false;
        {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(prepared&&generation==gen)installed=monitoringRouter.installPrepared(curve);}
        if(installed){calibrationName=p.name+" / "+p.device+" / "+p.connection+" / "+p.deviceMode;message.setText("Calibration loaded; select HEADPHONE CORRECTION to audition",juce::dontSendNotification);}
        else message.setText("Device changed or profile transition busy; load calibration again",juce::dontSendNotification);
    },[this](const juce::String& error){message.setText(error,juce::dontSendNotification);});
 saveSessionButton.onClick=[this]{chooseSession(true);};loadSessionButton.onClick=[this]{chooseSession(false);};
 for(auto* c:std::array<juce::Component*,15>{&pauseButton,&outputButton,&monitorButton,&moduleMenu,&trackMenu,&insert,&remove,&bypass,&rackButton,&openButton,&mute,&solo,&trackTrim,&title,&message})bay.addAndMakeVisible(*c);
 const auto& registry=hem::PluginRegistry::entries();for(size_t i=0;i<registry.size();++i)moduleMenu.addItem(registry[i].name,int(i)+1);moduleMenu.setSelectedId(1);
 for(int i=0;i<trackCount;++i)trackMenu.addItem("TRACK "+juce::String(i+1),i+1);trackMenu.setSelectedId(1);
 trackMenu.onChange=[this]{selectTrack(trackMenu.getSelectedId()-1);};
 insert.onClick=[this]{installSelected(moduleMenu.getSelectedId()-1);};remove.onClick=[this]{installSelected(-1);};bypass.onClick=[this]{current().chain.bypass(selected,!current().chain.isBypassed(selected));};
 pauseButton.onClick=[this]{pauseAll();};outputButton.onClick=[this]{showSettings();};
 monitorButton.onClick=[this]{showMonitor();};
 rackButton.onClick=[this]{showRack();};openButton.onClick=[this]{openPlugin(selected);};
 mute.onClick=[this]{current().muted.store(!current().muted.load());};solo.onClick=[this]{current().solo.store(!current().solo.load());};
 trackTrim.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);trackTrim.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);trackTrim.setRange(-60,6,.1);trackTrim.setValue(0);trackTrim.setTextValueSuffix(" dB / TRACK");trackTrim.setDoubleClickReturnValue(true,0);
 trackTrim.onValueChange=[this]{current().trimDb.store(float(trackTrim.getValue()));};
 for(auto* l:std::array<juce::Label*,4>{&track,&telemetry,&inValue,&outValue}){addAndMakeVisible(*l);l->setJustificationType(juce::Justification::centred);l->setColour(juce::Label::textColourId,juce::Colours::cyan);l->setInterceptsMouseClicks(false,false);}
 hem::NativeWorkspace::Callbacks workspaceCallbacks;
 workspaceCallbacks.bank=[this]() -> hem::TrackBank& {return *bank;};
 workspaceCallbacks.getSelectedTrack=[this]{return selectedTrack;};workspaceCallbacks.getSelectedSlot=[this]{return selected;};
 workspaceCallbacks.setSelectedTrack=[this](int index){selectTrack(index);};workspaceCallbacks.setSelectedSlot=[this](int slot){selected=juce::jlimit(0,7,slot);};
 workspaceCallbacks.onOpenSlot=[this](int slot){openPlugin(slot);};workspaceCallbacks.onInstallModule=[this](int module){installSelected(module);};workspaceCallbacks.onMonitor=[this]{showMonitor();};
 workspaceCallbacks.onLoadVst3=[this]{chooseExternalPlugin();};workspaceCallbacks.onReloadVst3=[this]{reloadExternalPlugin();};
 workspaceCallbacks.routeTelemetry=[this]{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());hem::NativeWorkspace::RouteTelemetry t;t.output=outputMeters.snapshot();t.headphoneCorrection=monitoringRouter.headphoneMode();t.monitorReady=monitorRouteReady.load();t.calibrationName=calibrationName;t.sampleRate=prepared?sampleRate:0;t.blockSize=blockSize;t.limiterLatencySamples=masterLimiter.latencySamples();if(auto* d=deviceManager.getCurrentAudioDevice()){t.outputDevice=d->getName();t.hardwareOutputs=d->getActiveOutputChannels().countNumberOfSetBits();}else{t.outputDevice="NO AUDIO DEVICE / OFFLINE EDITING";t.hardwareOutputs=0;}return t;};
 workspace=std::make_unique<hem::NativeWorkspace>(std::move(workspaceCallbacks));addChildComponent(*workspace);
 addAndMakeVisible(statusBar);statusBar.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));statusBar.setColour(juce::Label::textColourId,juce::Colour(0xffffca7c));message.setVisible(false);
 addChildComponent(utilityViewport);addChildComponent(utilityTitle);utilityViewport.setScrollBarsShown(true,true);utilityTitle.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));utilityTitle.setColour(juce::Label::backgroundColourId,juce::Colour(0xff061017));
 const std::array<const char*,6> names{"HOME","MIXER","PLUGIN","PLUGIN VAULT","ROUTING","SETTINGS"};
 for(size_t i=0;i<navigation.size();++i){auto& nav=navigation[i];addAndMakeVisible(nav);nav.setButtonText(names[i]);nav.setComponentID("navigation-"+juce::String(int(i)));nav.onClick=[this,i]{if(i==2)openPlugin(selected);else showShellPage(static_cast<ShellPage>(i));};}
 addAndMakeVisible(unloadSourcesButton);addAndMakeVisible(newSessionButton);
 unloadSourcesButton.setTooltip("Unload source audio without deleting WAV files; insert chains and gains are retained");
 unloadSourcesButton.onClick=[this]{juce::PopupMenu menu;menu.addItem(1,"Unload selected source / keep inserts and gain");menu.addItem(2,"Unload all sources / keep inserts and gains");juce::Component::SafePointer<MainComponent> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&unloadSourcesButton),[safe](int choice){if(safe&&(choice==1||choice==2)){const auto result=safe->clearTrackSources(choice==2);if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});};
 newSessionButton.setTooltip("Start a new empty session after confirmation; source files are not deleted");newSessionButton.onClick=[this]{requestNewSession();};
 setSize(1536,864);setAudioChannels(0,2);auto setup=deviceManager.getAudioDeviceSetup();if(auto* d=deviceManager.getCurrentAudioDevice()){
 const auto rates=d->getAvailableSampleRates();if(rates.contains(hem::defaultSampleRate))setup.sampleRate=hem::defaultSampleRate;
 const auto blocks=d->getAvailableBufferSizes();if(blocks.contains(256))setup.bufferSize=256;
 }
 deviceError=deviceManager.setAudioDeviceSetup(setup,true);if(!deviceManager.getCurrentAudioDevice())deviceManager.restartLastAudioDevice();prepareOfflineSession();startTimerHz(20);selectSlot(0);lastSavedSessionState=sessionFingerprint(captureSession(false));
}
MainComponent::~MainComponent(){
 stopTimer();utilityViewport.setViewedComponent(nullptr,false);utilityContent=nullptr;embeddedExport.reset();embeddedVocal.reset();embeddedMonitor.reset();embeddedAnalysis.reset();closePluginEditor();workspace.reset();exportWindow.reset();exportWorker.reset();picker.reset();profileLoader.reset();analysisWindow.reset();sessionWorker.reset();importer.reset();shutdownAudio();vocalWindow.reset();pluginWindow.reset();rackWindow.reset();monitorWindow.reset();deviceSettings.reset();
 for(auto& t:bank->tracks){t.transport().setSource(nullptr);t.playback->reader.reset();}diskThread.stopThread(5000);bank.reset();externalHost.collectGarbage();
 for(auto* c:std::array<juce::Component*,9>{&inputKnob,&outputKnob,&play,&stop,&rewind,&forward,&loop,&file,&settings})c->setLookAndFeel(nullptr);setLookAndFeel(nullptr);
}
void MainComponent::configureKnob(juce::Slider& knob,std::atomic<float>& value,const juce::String& name){
 addAndMakeVisible(knob);knob.setLookAndFeel(&overlayLook);knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
 knob.setRange(-60,6,.1);knob.setValue(0,juce::dontSendNotification);knob.setDoubleClickReturnValue(true,0);knob.setMouseDragSensitivity(250);knob.setName(name);knob.setTooltip(name+" gain / double-click 0 dB");
 auto* p=&value;auto* s=&knob;knob.onValueChange=[p,s]{p->store(float(s->getValue()),std::memory_order_relaxed);};
}
void MainComponent::prepareToPlay(int block,double rate){
 auto* inputDevice=deviceManager.getCurrentAudioDevice();hardwareOutputs=inputDevice?inputDevice->getActiveOutputChannels().countNumberOfSetBits():0;vocals.prepare(rate,std::max(1,block),inputDevice?inputDevice->getActiveInputChannels().countNumberOfSetBits():0);
 monitoringRouter.prepare(rate);analyser.prepare(rate);outputMeters.prepare(inputDevice?rate:0);masterLimiter.prepare(rate,std::max(1,block));sampleRate=rate;blockSize=std::max(1,block);++generation;sum.setSize(2,blockSize);listeningSource.setSize(2,blockSize);monitorBuffer.setSize(2,blockSize);monitorRouteReady.store(false);monitorProcessor.prepareToPlay(rate,blockSize);inputValues.assign(size_t(blockSize),1);
 for(auto& t:bank->tracks)t.prepare(rate,blockSize);inputGain.reset(rate,hem::gain(inputDb.load()));outputGain.reset(rate,hem::gain(outputDb.load()));prepared=true;
}
void MainComponent::prepareOfflineSession(){
 const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
 if(prepared||deviceManager.getCurrentAudioDevice())return;
 // No device callback exists here. Rebind playing transports without stop()'s
 // callback acknowledgement wait, retaining a common source timeline.
 const double position=sessionPosition();
 for(auto& t:bank->tracks)if(t.loaded&&t.transport().isPlaying()){
  t.transport().setSource(t.playback->reader.get(),65536,&diskThread,t.sourceRate,2);
  t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));
 }
 prepareToPlay(256,hem::defaultSampleRate);
 for(auto& t:bank->tracks)t.chain.completePendingStopped();
 outputMeters.prepare(0);
}
void MainComponent::closePluginEditor(){if(workspace)workspace->clearEditor();pluginWindow.reset();editedProcessor=nullptr;}
void MainComponent::releaseResources(){outputMeters.prepare(0);vocals.release();monitorProcessor.releaseResources();for(auto& t:bank->tracks)t.release();prepared=false;++generation;}
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info){
 juce::ScopedNoDenormals guard;if(!info.buffer)return;if(!prepared||info.buffer->getNumChannels()==0){info.clearActiveBufferRegion();return;}
 inputGain.set(hem::gain(inputDb.load()));outputGain.set(hem::gain(outputDb.load()));float maximum=0;bool anySolo=false;for(auto& t:bank->tracks)anySolo|=t.solo.load();
 for(int offset=0;offset<info.numSamples;offset+=blockSize){const int n=std::min(blockSize,info.numSamples-offset),start=info.startSample+offset;
    vocals.capture(*info.buffer,start,n,callbackTimeline());
    sum.clear();for(int i=0;i<n;++i)inputValues[size_t(i)]=inputGain.next();
    for(auto& t:bank->tracks)t.render(sum,n,anySolo,inputValues.data());
    // Master result remains in sum. Monitoring touches only this copy.
    // An offline exporter must consume the master path, not monitorBuffer.
    for(int i=0;i<n;++i){const float g=outputGain.next();sum.setSample(0,i,sum.getSample(0,i)*g);sum.setSample(1,i,sum.getSample(1,i)*g);}
    // Analysis is tapped before all listening correction. Audio only copies
    // into a bounded queue; FFT and waveform decimation run on its worker.
    analyser.push(sum.getReadPointer(0),sum.getReadPointer(1),n);
    for(int channel=0;channel<2;++channel)monitorBuffer.copyFrom(channel,0,sum,channel,0,n);
    vocals.addMonitor(monitorBuffer,n);
    // Use a separate combined listening buffer as the direct/corrected source.
    for(int channel=0;channel<2;++channel)listeningSource.copyFrom(channel,0,monitorBuffer,channel,0,n);
    monitoringRouter.renderCalibration(listeningSource.getReadPointer(0),listeningSource.getReadPointer(1),monitorBuffer.getWritePointer(0),monitorBuffer.getWritePointer(1),n);
    float* channels[]{monitorBuffer.getWritePointer(0),monitorBuffer.getWritePointer(1)};
    monitorView.setDataToReferTo(channels,2,n);if(monitorRouteReady.load(std::memory_order_acquire))monitorProcessor.processBlock(monitorView,monitorMidi);
    // Last processing stage, including optional monitor EQ boosts. Only an
    // unchanged sample copy to the device follows this safety limiter.
    monitoringRouter.mixRoute(listeningSource.getReadPointer(0),listeningSource.getReadPointer(1),channels[0],channels[1],n);
    if(hardwareOutputs==1)for(int i=0;i<n;++i)channels[0][i]=channels[1][i]=.5f*(channels[0][i]+channels[1][i]);
    masterLimiter.process(channels[0],channels[1],n);
    // Exact emitted samples, including mono fold-down and limiter delay.
    outputMeters.push(channels[0],channels[1],n);
    for(int i=0;i<n;++i){const float l=monitorBuffer.getSample(0,i),r=monitorBuffer.getSample(1,i);maximum=std::max({maximum,std::abs(l),std::abs(r)});
        info.buffer->setSample(0,start+i,l);if(hardwareOutputs>1&&info.buffer->getNumChannels()>1)info.buffer->setSample(1,start+i,r);}
 }
 peak.store(maximum,std::memory_order_relaxed);
}
bool MainComponent::playing() const {for(const auto& t:bank->tracks)if(t.transport().isPlaying())return true;return false;}
double MainComponent::sessionPosition() const {
 // The longest loaded source remains the timeline anchor when shorter stems
 // finish. Muted tracks are still rendered and keep their playback positions.
 const hem::TrackEngine* anchor=nullptr;double length=-1;
 for(const auto& t:bank->tracks)if(t.loaded&&t.transport().getLengthInSeconds()>length){anchor=&t;length=t.transport().getLengthInSeconds();}
 return anchor?anchor->transport().getCurrentPosition():0;
}
void MainComponent::playAll(){
 if(!deviceManager.getCurrentAudioDevice()){message.setText("No audio device: editing, sessions and export remain available",juce::dontSendNotification);return;}
 if(exportWorker&&exportWorker->isBusy()){message.setText("Finish or cancel the offline export before playback",juce::dontSendNotification);return;}

 if(vocals.recorder.busy()&&!playing()){message.setText("Start backing playback before recording, or finish the current take",juce::dontSendNotification);return;}
 if(sessionWorker&&sessionWorker->isBusy())return;
 if(playing())return;
 bool loaded=false;double duration=0;for(const auto& t:bank->tracks)if(t.loaded){loaded=true;duration=std::max(duration,t.transport().getLengthInSeconds());}
 if(!loaded){openAudio();return;}
 const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
 double position=sessionPosition();if(position>=duration)position=0;
 for(auto& t:bank->tracks)if(t.loaded){t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));t.transport().start();}
}
void MainComponent::pauseAll(){
 double position;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());position=sessionPosition();}
 // JUCE stop() waits for its callback fade. It must run OUTSIDE the device
 // lock. Afterwards restore all stems to the same captured pause position.
 stopAll(false);seekAll(position);
}
void MainComponent::stopAll(bool rewindAll){
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());vocals.stopTake();}
 // stop() can wait for the callback, so never hold the device lock here.
 for(auto& t:bank->tracks)t.transport().stop();if(rewindAll)seekAll(0);
}
void MainComponent::seekAll(double position){const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());vocals.stopTake();for(auto& t:bank->tracks)if(t.loaded)t.transport().setPosition(std::min(position,t.transport().getLengthInSeconds()));}
void MainComponent::installSelected(int module){
 if(sessionWorker&&sessionWorker->isBusy())return;
 const auto& entries=hem::PluginRegistry::entries();
 if(module>=0&&module<int(entries.size())&&entries[size_t(module)].monitoringOnly){showMonitor();message.setText("Monitor EQ is listening-only; track insert unchanged",juce::dontSendNotification);return;}
 auto& chain=current().chain;if(chain.isBusy(selected)){message.setText("Insert transition in progress",juce::dontSendNotification);return;}
 double rate;int block,budget;unsigned version;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(!prepared)return;rate=sampleRate;block=blockSize;budget=chain.slotLatency();version=generation;}
 try{auto node=chain.stageNode(hem::createModule(module),rate,block,budget);bool success=false,offline=false;
    std::unique_ptr<hem::InsertChain::PreparedNode> retired;
    {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(prepared&&generation==version){
      offline=deviceManager.getCurrentAudioDevice()==nullptr;
      success=offline?chain.publishStopped(selected,std::move(node)):chain.publish(selected,std::move(node));
      if(offline&&success)retired=chain.collectRetired(selected);
    }}
    if(retired){if(editedProcessor==retired->processor.get())closePluginEditor();retired->processor->releaseResources();}
    message.setText(success?"TRACK "+juce::String(selectedTrack+1)+" / SLOT "+juce::String(selected+1)+" / "+(module>=0&&module<int(entries.size())?juce::String(entries[size_t(module)].name):juce::String("EMPTY"))+(offline?" / updated offline":" / queued; open after transition"):"Device changed; insert again",juce::dontSendNotification);
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}
void MainComponent::selectTrack(int i){if(i<0||i>=trackCount)return;if(i!=selectedTrack)closePluginEditor();selectedTrack=i;trackMenu.setSelectedId(i+1,juce::dontSendNotification);trackTrim.setValue(current().trimDb.load(),juce::dontSendNotification);if(workspace)workspace->refresh();}
void MainComponent::selectSlot(int i){selected=juce::jlimit(0,7,i);showShellPage(ShellPage::home);}
void MainComponent::openPlugin(int slot){
 selected=juce::jlimit(0,7,slot);auto* p=current().chain.activeProcessor(selected);closePluginEditor();
 if(p){editedProcessor=p;editedTrack=selectedTrack;editedSlot=selected;workspace->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));}
 else message.setText("Empty insert: select a module in PLUGIN VAULT",juce::dontSendNotification);
 showShellPage(ShellPage::plugin);
}
void MainComponent::moveSlot(int from,int to){
 if(playing()){message.setText("Stop playback before changing insert order",juce::dontSendNotification);return;}
 closePluginEditor();bool moved;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());moved=current().chain.swapSlotsStopped(from,to);}
 if(moved)selected=to;message.setText(moved?"Insert order updated":"Wait for insert transition",juce::dontSendNotification);
}
void MainComponent::showRack(){openPlugin(selected);}
void MainComponent::timerCallback(){
 prepareOfflineSession();
 if(auto* device=deviceManager.getCurrentAudioDevice()){
    const auto name=device->getName();
    if(name!=lastOutputDevice||!monitorRouteReady.load()){
        monitorProcessor.state.getParameter("enabled")->setValueNotifyingHost(0.f);
        monitoringRouter.setHeadphoneMode(false);calibrationName="Device restarted: reload measurement profile";
        lastOutputDevice=name;monitorRouteReady.store(true,std::memory_order_release);
    }
 }

 std::array<std::array<std::unique_ptr<hem::InsertChain::PreparedNode>,8>,trackCount> retired;
 std::array<std::array<hem::ModuleProcessor*,8>,trackCount> active{};
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i){active[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.activeProcessor(i);retired[size_t(t)][size_t(i)]=bank->tracks[size_t(t)].chain.collectRetired(i);}}
 // Snapshot before retirement. Close any editor that still refers to an old
 // processor before the corresponding unique_ptr leaves this scope.
 if(editedProcessor&&active[size_t(editedTrack)][size_t(editedSlot)]!=editedProcessor){closePluginEditor();}
 for(int t=0;t<trackCount;++t)for(int i=0;i<8;++i)if(retired[size_t(t)][size_t(i)])retired[size_t(t)][size_t(i)]->processor->releaseResources();
 auto& nodes=active[size_t(selectedTrack)];auto& chain=current().chain;auto* p=nodes[size_t(selected)];const bool running=playing();
 play.setToggleState(running,juce::dontSendNotification);loop.setToggleState(looping,juce::dontSendNotification);
 for(int i=0;i<8;++i){auto* node=nodes[size_t(i)];const auto name=juce::String(i+1)+" / "+(node?node->getName():juce::String("EMPTY"));slotButtons[size_t(i)].setButtonText(name);
    if(rackView){auto& row=rackView->rows[size_t(i)];row.name.setText((selected==i?"> ":"  ")+name,juce::dontSendNotification);row.bypass.setToggleState(chain.isBypassed(i),juce::dontSendNotification);row.up.setEnabled(!running&&i>0&&!chain.isBusy(i));row.down.setEnabled(!running&&i<7&&!chain.isBusy(i));row.remove.setEnabled(node&&!chain.isBusy(i));}}
 if(rackView){rackView->tracks.setSelectedId(selectedTrack+1,juce::dontSendNotification);rackView->status.setText("SELECTED INSERT "+juce::String(selected+1)+" / "+(running?"Stop transport to reorder":"UP / DOWN changes order")+" / "+message.getText(),juce::dontSendNotification);}
 title.setText("TRACK "+juce::String(selectedTrack+1)+" / INSERT "+juce::String(selected+1)+" / "+(p?p->getName():juce::String("EMPTY")),juce::dontSendNotification);
 bypass.setToggleState(chain.isBypassed(selected),juce::dontSendNotification);insert.setEnabled(!chain.isBusy(selected));remove.setEnabled(p&&!chain.isBusy(selected));openButton.setEnabled(p!=nullptr);
 mute.setToggleState(current().muted.load(),juce::dontSendNotification);solo.setToggleState(current().solo.load(),juce::dontSendNotification);
 track.setText(current().name+"\n"+timeText(sessionPosition())+" / "+timeText(current().transport().getLengthInSeconds()),juce::dontSendNotification);
 inValue.setText("INPUT "+juce::String(inputDb.load(),1)+" dB",juce::dontSendNotification);outValue.setText("OUTPUT "+juce::String(outputDb.load(),1)+" dB",juce::dontSendNotification);
 if(auto* d=deviceManager.getCurrentAudioDevice())telemetry.setText("AUDIO OUTPUT\n"+d->getName()+"\n"+juce::String(d->getCurrentSampleRate()/1000,1)+" kHz / "+juce::String(d->getCurrentBufferSizeSamples())+" frames",juce::dontSendNotification);else telemetry.setText("NO AUDIO DEVICE",juce::dontSendNotification);
 telemetry.setTooltip(deviceError+" / Task 2B master / output sample peak "+juce::String(masterLimiter.samplePeakDb(),2)+" dBFS / latency "+juce::String(masterLimiter.latencySamples())+" samples / reconstructed true-peak certification pending");
 vocalButton.setButtonText(vocals.recorder.busy()?"VOCAL / ACTIVE":"VOCAL TRACKING");vocalButton.setToggleState(vocals.recorder.busy(),juce::dontSendNotification);
 if(workspace&&workspace->isVisible()){
  switch(workspace->page()){case hem::NativeWorkspace::Page::plugin:shellPage=ShellPage::plugin;break;case hem::NativeWorkspace::Page::mixer:shellPage=ShellPage::mixer;break;case hem::NativeWorkspace::Page::vault:shellPage=ShellPage::vault;break;case hem::NativeWorkspace::Page::routing:shellPage=ShellPage::routing;break;}
 }
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(shellPage==static_cast<ShellPage>(i),juce::dontSendNotification);
 if(deviceSettings)deviceSettings->setDeviceChangesAllowed(!running&&!vocals.recorder.busy()&&!exportWorker->isBusy());
 const bool canClear=!running&&!vocals.recorder.busy()&&!sessionWorker->isBusy()&&!exportWorker->isBusy()&&!importer->hasPendingWork();unloadSourcesButton.setEnabled(canClear);newSessionButton.setEnabled(canClear);
 const bool sessionBusy=sessionWorker&&sessionWorker->isBusy();
 routeButton.setButtonText(monitoringRouter.headphoneMode()?"HEADPHONE CORRECTION":"DIRECT OUTPUT");routeButton.setToggleState(monitoringRouter.headphoneMode(),juce::dontSendNotification);routeButton.setTooltip(calibrationName);
 statusBar.setText(message.getText(),juce::dontSendNotification);statusBar.setTooltip(message.getText());
 saveSessionButton.setEnabled(!sessionBusy);loadSessionButton.setEnabled(!sessionBusy);importer->setEnabled(!sessionBusy);
 juce::String externalFault;
 for(int ti=0;ti<trackCount&&externalFault.isEmpty();++ti)for(int si=0;si<8&&externalFault.isEmpty();++si)
  if(auto* node=active[size_t(ti)][size_t(si)];node&&node->isExternal()&&!node->processingReady())
   externalFault="TRACK "+juce::String(ti+1)+" / SLOT "+juce::String(si+1)+" / "+node->externalDiagnostic();
 if(externalFault!=lastExternalDiagnostic){lastExternalDiagnostic=externalFault;if(externalFault.isNotEmpty())message.setText(externalFault+" / select this slot and RELOAD CURRENT VST3",juce::dontSendNotification);}
 externalHost.collectGarbage();
 repaint();pumpCloseRequest();
}
void MainComponent::openAudio(){if(sessionWorker&&sessionWorker->isBusy())return;const int destination=selectedTrack;picker=std::make_unique<juce::FileChooser>("Load selected track",juce::File(),"*.wav");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,destination](const juce::FileChooser& f){if(safe&&f.getResult().existsAsFile())safe->loadAudio(f.getResult(),destination);});
}
void MainComponent::loadAudio(const juce::File& f,int index){
 if(sessionWorker&&sessionWorker->isBusy())return;
 juce::StringArray files;files.add(f.getFullPathName());importer->submitFiles(files,index);
}
juce::Result MainComponent::commitImport(hem::StemFileImporter::Batch& batch){
 if(vocals.recorder.busy())return juce::Result::fail("Finish vocal recording before replacing backing stems");
 if(sessionWorker&&sessionWorker->isBusy())return juce::Result::fail("Wait for session operation");
 if(playing())return juce::Result::fail("Pause or stop playback, then APPLY. Current audio is unchanged.");
 if(batch.firstTrack<0||batch.count<1||batch.firstTrack+batch.count>trackCount)return juce::Result::fail("Invalid track mapping");
 for(int i=0;i<batch.count;++i)if(!batch.stems[size_t(i)])return juce::Result::fail("Incomplete batch; import again");
 {
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    if(!prepared||generation!=batch.context.generation||looping!=batch.context.looping)
        return juce::Result::fail("Output device or loop mode changed. CLEAR and import again.");
    // Already prepared sources swap ownership atomically as a group. No file
    // opening, resampler preparation, reader destruction or GUI work here.
    for(int i=0;i<batch.count;++i)bank->tracks[size_t(batch.firstTrack+i)].playback.swap(batch.stems[size_t(i)]);
 }
 for(int i=0;i<batch.count;++i){auto& t=bank->tracks[size_t(batch.firstTrack+i)];t.loaded=true;t.name=t.playback->file.getFileName();t.sourceRate=t.playback->sourceRate;}
 seekAll(0);message.setText(juce::String(batch.count)+" WAV stems applied / all tracks rewound / insert chains retained",juce::dontSendNotification);
 return juce::Result::ok();
}
void MainComponent::showMonitor(){
 monitoringRouter.setHeadphoneMode(true);if(!embeddedMonitor)embeddedMonitor.reset(monitorProcessor.createEditor());
 showUtility(*embeddedMonitor,"LISTENING-ONLY MONITOR EQ / EXCLUDED FROM EXPORT");
}
void MainComponent::showSettings(){showShellPage(ShellPage::settings);}
juce::Rectangle<int> MainComponent::map(float x,float y,float w,float h) const {return juce::Rectangle<float>(face.getX()+x/1536*face.getWidth(),face.getY()+y/864*face.getHeight(),w/1536*face.getWidth(),h/864*face.getHeight()).toNearestInt();}
void MainComponent::resized(){const float scale=std::min(float(getWidth())/1536,float(getHeight())/864);face={(getWidth()-1536*scale)*.5f,(getHeight()-864*scale)*.5f,1536*scale,864*scale};
 outputMeterView.setBounds(map(1310,280,190,350));statusBar.setBounds(map(304,627,994,28));statusBar.setFont(juce::Font(juce::FontOptions(std::max(10.f,13*scale))));
 inputKnob.setBounds(map(338,696,94,98));outputKnob.setBounds(map(1100,696,102,98));rewind.setBounds(map(502,725,101,55));stop.setBounds(map(612,725,90,55));play.setBounds(map(716,725,102,55));forward.setBounds(map(829,725,99,55));loop.setBounds(map(944,725,87,55));file.setBounds(map(53,25,62,44));settings.setBounds(map(196,25,91,44));
 track.setBounds(map(52,700,236,85));telemetry.setBounds(map(1257,700,231,85));inValue.setBounds(map(311,797,147,22));outValue.setBounds(map(1078,797,147,22));
 for(int i=0;i<8;++i)slotButtons[size_t(i)].setBounds(map(43,145.f+i*56.f,195,45));bay.setBounds(map(304,136,994,479));
 auto nav=map(304,96,994,34);for(size_t i=0;i<navigation.size();++i){const int x=nav.getX()+nav.getWidth()*int(i)/6,next=nav.getX()+nav.getWidth()*int(i+1)/6;navigation[i].setBounds(x,nav.getY(),next-x-3,nav.getHeight());}
 unloadSourcesButton.setBounds(map(43,105,95,32));newSessionButton.setBounds(map(143,105,95,32));
 const auto content=map(43,141,1255,474);if(workspace)workspace->setBounds(content);if(deviceSettings)deviceSettings->setBounds(content);
 auto utility=content;utilityTitle.setBounds(utility.removeFromTop(28));utilityViewport.setBounds(utility);
 if(utilityContent)utilityContent->setSize(std::max(utilityMinimum.x,utility.getWidth()-utilityViewport.getScrollBarThickness()),std::max(utilityMinimum.y,utility.getHeight()-utilityViewport.getScrollBarThickness()));
 const float ui=float(bay.getWidth())/994.f;
 auto b=bay.getLocalBounds().reduced(int(12*ui));title.setBounds(b.removeFromTop(int(36*ui)));auto row=b.removeFromTop(int(44*ui));trackMenu.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));rackButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));openButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(44*ui));moduleMenu.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));insert.setBounds(row.reduced(3));row=b.removeFromTop(int(44*ui));remove.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));bypass.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitorButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(44*ui));mute.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(3));solo.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));pauseButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));outputButton.setBounds(row.reduced(3));row=b.removeFromTop(int(38*ui));saveSessionButton.setBounds(row.removeFromLeft(row.getWidth()/5).reduced(3));loadSessionButton.setBounds(row.removeFromLeft(row.getWidth()/4).reduced(3));analysisButton.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));calibrationButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));vocalButton.setBounds(row.reduced(3));
 row=b.removeFromTop(int(32*ui));exportButton.setBounds(row.removeFromRight(row.getWidth()/3).reduced(3));routeButton.setBounds(row.reduced(3));message.setBounds({});importer->setBounds(b.removeFromBottom(int(100*ui)));trackTrim.setBounds(b);
}
void MainComponent::paint(juce::Graphics& g){g.fillAll(juce::Colour(0xff020508));g.drawImage(faceplate,face,juce::RectanglePlacement::stretchToFit);
 // The archived artwork had an unwired Edit label. Hide it rather than promise an editor.
 g.setColour(juce::Colour(0xff061117));g.fillRoundedRectangle(map(125,25,70,44).toFloat(),4);
 // The native title bar owns working minimize/maximize/close actions. Hide their
 // decorative duplicates printed on the historical artwork.
 g.fillRoundedRectangle(map(1323,24,189,49).toFloat(),4);
 hem::ui::StudioBranding::draw(g,face,ghostBrand);
 if(bay.isVisible()){g.setColour(juce::Colour(0xe0080c12));g.fillRoundedRectangle(bay.getBounds().toFloat(),12);g.setColour(juce::Colour(0xff00cdda));g.drawRoundedRectangle(bay.getBounds().toFloat(),12,1.5f);}
}

void MainComponent::chooseSession(bool saving){
 if(sessionWorker->isBusy())return;
 if(!saving&&(vocals.recorder.busy()||playing()||importer->hasPendingWork())){message.setText("Pause transport and APPLY or CLEAR pending imports before recalling a session",juce::dontSendNotification);return;}
 picker=std::make_unique<juce::FileChooser>(saving?"Save HEM session":"Recall HEM session",juce::File(),"*.xml;*.json");
 juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync((saving?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,
    [safe,saving](const juce::FileChooser& chooser){
        if(!safe||chooser.getResult()==juce::File()||safe->sessionWorker->isBusy())return;
        const auto result=saving?safe->saveSessionFile(chooser.getResult()):safe->loadSessionFile(chooser.getResult());
        if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);
    });
}
juce::Result MainComponent::commitSession(std::unique_ptr<hem::TrackBank>& staged,const hem::SessionSnapshot& state,hem::SessionWorker::Context context){
 if(!staged||vocals.recorder.busy()||playing())return juce::Result::fail("Session unchanged: stop playback before recalling");
 // Close every editor borrowing a processor from the bank BEFORE retirement.
 closePluginEditor();
 {
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    if(!prepared||generation!=context.generation)return juce::Result::fail("Session unchanged: audio device changed during recall; load again");
    bank.swap(staged);inputDb.store(state.inputDb);outputDb.store(state.outputDb);looping=state.looping;
    // A source batch prepared for the previous session must not be committed.
    ++generation;
 }
 // Monitor state was range-checked by the worker. Do not enable an old room or
 // headphone correction on a potentially different current output device.
 auto monitor=state.monitorParameters.createCopy();
 for(auto parameter:monitor)if(parameter["id"].toString()=="enabled")parameter.setProperty("value",0.0,nullptr);
 hem::SessionState::restoreParameters(monitorProcessor,monitor);
 inputKnob.setValue(state.inputDb,juce::dontSendNotification);outputKnob.setValue(state.outputDb,juce::dontSendNotification);
 monitoringRouter.setHeadphoneMode(false);selectTrack(0);selectSlot(0);return juce::Result::ok();
}

void MainComponent::openCalibration(){
 if(profileLoader->isBusy())return;
 picker=std::make_unique<juce::FileChooser>("Load measured headphone calibration JSON",juce::File(),"*.json");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& chooser){
    if(!safe||!chooser.getResult().existsAsFile())return;
    double rate;unsigned gen;
    {const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());if(!safe->prepared)return;rate=safe->sampleRate;gen=safe->generation;}
    safe->profileLoader->load(chooser.getResult(),rate,gen);
 });
}

double MainComponent::callbackTimeline() const {
 const hem::TrackEngine* anchor=nullptr;double duration=-1;
 for(const auto& t:bank->tracks){const double length=t.transport().getLengthInSeconds();if(length>duration){duration=length;anchor=&t;}}
 return anchor?anchor->transport().getCurrentPosition():0;
}
void MainComponent::showVocals(){
 if(!embeddedVocal)embeddedVocal=std::make_unique<hem::VocalTrackingPanel>(vocals,deviceManager,[this]{showSettings();},
    [this]{return bank->tracks[0].chain.latencySamples()+masterLimiter.latencySamples();},[this]{return masterLimiter.latencySamples();},
    [this]{if(exportWorker&&exportWorker->isBusy())return juce::String("Finish or cancel the offline export before recording");if(looping)return juce::String("Disable backing loops before recording a continuous take");if(sessionWorker->isBusy())return juce::String("Wait for session recall to finish");return juce::String();});
 showUtility(*embeddedVocal,"LIVE VOCAL RECORDING / TRACKING");
}

void MainComponent::showExport(){
 if(!embeddedExport)embeddedExport=std::make_unique<hem::OfflineMixPanel>(*exportWorker,[this](const juce::File& file,const hem::OfflineMixOptions& options){return startExport(file,options);});
 showUtility(*embeddedExport,"OFFLINE MIX EXPORT / STEREO FLOAT32 WAV");
}
void MainComponent::showAnalysis(){if(!embeddedAnalysis)embeddedAnalysis=std::make_unique<hem::AnalysisView>(analyser);showUtility(*embeddedAnalysis,"MASTER WAVEFORM / SPECTRUM / PRE-LISTENING TAP");}
void MainComponent::showUtility(juce::Component& content,const juce::String& name){
 utilityContent=&content;utilityMinimum={std::max(600,content.getWidth()),std::max(350,content.getHeight())};utilityViewport.setViewedComponent(&content,false);utilityTitle.setText(name,juce::dontSendNotification);showShellPage(ShellPage::utility);
}
void MainComponent::showShellPage(ShellPage page){
 shellPage=page;bay.setVisible(page==ShellPage::home);for(auto& b:slotButtons)b.setVisible(page==ShellPage::home);
 const bool work=page==ShellPage::plugin||page==ShellPage::mixer||page==ShellPage::vault||page==ShellPage::routing;
 if(workspace){workspace->setVisible(work);if(work){const auto target=page==ShellPage::mixer?hem::NativeWorkspace::Page::mixer:page==ShellPage::vault?hem::NativeWorkspace::Page::vault:page==ShellPage::routing?hem::NativeWorkspace::Page::routing:hem::NativeWorkspace::Page::plugin;workspace->setPage(target);}}
 if(page==ShellPage::settings&&!deviceSettings){deviceSettings=std::make_unique<hem::DeviceSettingsPanel>(deviceManager);addChildComponent(*deviceSettings);}
 if(deviceSettings){deviceSettings->setVisible(page==ShellPage::settings);deviceSettings->setDeviceChangesAllowed(!playing()&&!vocals.recorder.busy()&&!exportWorker->isBusy());}
 utilityViewport.setVisible(page==ShellPage::utility);utilityTitle.setVisible(page==ShellPage::utility);
 for(size_t i=0;i<navigation.size();++i)navigation[i].setToggleState(page==static_cast<ShellPage>(i),juce::dontSendNotification);
 resized();repaint();
}
void MainComponent::requestNewSession(){
 juce::Component::SafePointer<MainComponent> safe(this);
 juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Start a new session?").withMessage("This clears unsaved track sources, inserts and mix settings. Source WAV files and saved sessions are not deleted.").withButton("Start new").withButton("Cancel"),[safe](int button){if(safe&&button==1){const auto result=safe->resetSessionConfirmed();if(result.failed())safe->message.setText(result.getErrorMessage(),juce::dontSendNotification);}});
}
juce::Result MainComponent::startExport(const juce::File& destination,const hem::OfflineMixOptions& options){
 if(exportWorker->isBusy())return juce::Result::fail("An export is already running");
 if(playing()||vocals.recorder.busy())return juce::Result::fail("Pause transport and finish the take before exporting");
 if(sessionWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Finish session work and APPLY or CLEAR pending imports first");
 try{
  auto snapshot=captureSession();
  if(!exportWorker->start(std::move(snapshot),destination,options))return juce::Result::fail(exportWorker->result());
  return juce::Result::ok();
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}

juce::Result MainComponent::saveSessionFile(const juce::File& file){
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(hasExternalInserts()&&(playing()||vocals.recorder.busy()))return juce::Result::fail("Pause transport and finish recording before saving external plug-in state");
 try{auto snapshot=captureSession();
  pendingSavedSessionState=sessionFingerprint(snapshot);
  return sessionWorker->save(std::move(snapshot),file)?juce::Result::ok():juce::Result::fail("Cannot start session save");
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
juce::Result MainComponent::loadSessionFile(const juce::File& file){
 if(sessionWorker->isBusy())return juce::Result::fail("Wait for the current session operation");
 if(vocals.recorder.busy()||playing()||importer->hasPendingWork())return juce::Result::fail("Pause and APPLY or CLEAR pending imports before recalling");
 prepareOfflineSession();hem::SessionWorker::Context context;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());context={prepared?sampleRate:0,blockSize,generation};}
 return sessionWorker->load(file,context)?juce::Result::ok():juce::Result::fail("Cannot start session recall");
}
juce::Result MainComponent::clearTrackSources(bool all){
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending recording, session, import or export work first");
 prepareOfflineSession();
 std::array<std::unique_ptr<hem::StemPlayback>,trackCount> replacement;
 for(int i=0;i<trackCount;++i)if(all||i==selectedTrack){replacement[size_t(i)]=std::make_unique<hem::StemPlayback>();replacement[size_t(i)]->transport.prepareToPlay(blockSize,sampleRate);}
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
  for(int i=0;i<trackCount;++i)if(replacement[size_t(i)]){auto& t=bank->tracks[size_t(i)];t.playback.swap(replacement[size_t(i)]);t.loaded=false;t.name="EMPTY TRACK";t.sourceRate=0;t.signalMeters.prepare(sampleRate);}
  ++generation;
 }
 // Retire readers here on the stopped message thread, never in the callback lock.
 message.setText(all?"All source audio unloaded / inserts and gains retained":"Selected source unloaded / inserts and gain retained",juce::dontSendNotification);
 return juce::Result::ok();
}
juce::Result MainComponent::resetSessionConfirmed(){
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before a new session");
 prepareOfflineSession();auto empty=std::make_unique<hem::TrackBank>();for(auto& t:empty->tracks)t.prepare(sampleRate,blockSize);
 closePluginEditor();
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());bank.swap(empty);inputDb=0;outputDb=0;looping=false;++generation;}
 monitoringRouter.setHeadphoneMode(false);monitorProcessor.state.getParameter("enabled")->setValueNotifyingHost(0.f);
 inputKnob.setValue(0,juce::dontSendNotification);outputKnob.setValue(0,juce::dontSendNotification);selectTrack(0);selectSlot(0);
 currentSessionFile=juce::File();lastSavedSessionState=sessionFingerprint(captureSession(false));message.setText("New empty session / source files unchanged",juce::dontSendNotification);return juce::Result::ok();
}

juce::ValueTree MainComponent::sessionFingerprint(hem::SessionSnapshot snapshot) const {
 snapshot.positionSeconds=0; // Transport motion alone is not an unsaved edit.
 for(auto& track:snapshot.tracks)for(auto& slot:track.slots)slot.external.state.reset();
 auto encoded=hem::SessionState::encode(snapshot,juce::File::getCurrentWorkingDirectory().getChildFile("__HEM_state_comparison__.xml"));
 for(auto track:encoded)if(track.hasType("TRACK")){const int ti=int(track["index"]);for(auto slot:track){const int si=int(slot["index"]);if(ti>=0&&ti<13&&si>=0&&si<8&&snapshot.tracks[size_t(ti)].slots[size_t(si)].pluginId=="external-vst3")slot.setProperty("externalRevision",juce::String(snapshot.tracks[size_t(ti)].slots[size_t(si)].externalRevision),nullptr);}}
 return encoded;
}
bool MainComponent::hasUnsavedSessionChanges(){
 try{return !lastSavedSessionState.isValid()||!sessionFingerprint(captureSession(false)).isEquivalentTo(lastSavedSessionState);}
 catch(const std::exception&){return true;} // Pending publication is conservatively dirty.
}
void MainComponent::sessionFinished(bool saving,const juce::File& file,const juce::Result& result){
 if(result.wasOk()){
  currentSessionFile=file;
  if(saving)lastSavedSessionState=pendingSavedSessionState.createCopy();
  else lastSavedSessionState=sessionFingerprint(captureSession(false));
 }
 if(saving&&closePhase==ClosePhase::saving){if(result.wasOk())closePhase=ClosePhase::complete;else abortClose("Save failed; Studio remains open: "+result.getErrorMessage());}
}
bool MainComponent::activeCloseWork() const {
 if(externalLoadPending||playing()||vocals.recorder.busy()||exportWorker->isBusy()||sessionWorker->isBusy()||importer->hasPendingWork())return true;
 for(const auto& t:bank->tracks)for(int i=0;i<8;++i)if(t.chain.isBusy(i))return true;
 return false;
}
void MainComponent::requestClose(std::function<void()> approved){
 if(closePhase!=ClosePhase::idle)return;
 const bool dirty=hasUnsavedSessionChanges(),active=activeCloseWork();
 if(!dirty&&!active){if(approved)approved();return;}
 closeApproved=std::move(approved);closePhase=ClosePhase::prompt;
 juce::Component::SafePointer<MainComponent> safe(this);
 auto resolved=[safe](CloseChoice choice){if(safe)safe->resolveCloseChoice(choice);};
 if(closePrompt){closePrompt(dirty,active,std::move(resolved));return;}
 if(dirty){
  juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::WarningIcon,"Close Studio?",
   "This session has unsaved edits. Save before closing? Playback and recording will stop; pending import/export will be cancelled. Source WAV files and completed exports are not deleted.",
   "Save and close","Discard and close","Keep open",this,juce::ModalCallbackFunction::create([resolved](int result){resolved(result==1?CloseChoice::save:result==2?CloseChoice::discard:CloseChoice::cancel);}));
 }else{
  juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,"Close Studio?",
   "Work is active. Stop recording/playback, cancel pending import/export, finish any session write, and close? Completed files are preserved.","Stop work and close","Keep open",this,
   juce::ModalCallbackFunction::create([resolved](int result){resolved(result==1?CloseChoice::discard:CloseChoice::cancel);}));
 }
}
void MainComponent::abortClose(const juce::String& reason){closePhase=ClosePhase::idle;closeApproved={};closeNeedsSave=false;closeWasRecording=false;setEnabled(true);message.setText(reason,juce::dontSendNotification);}
void MainComponent::resolveCloseChoice(CloseChoice choice){
 if(closePhase!=ClosePhase::prompt)return;
 if(choice==CloseChoice::cancel){abortClose("Close cancelled / session unchanged");return;}
 closeNeedsSave=choice==CloseChoice::save;closeWasRecording=vocals.recorder.busy();closePhase=ClosePhase::waiting;setEnabled(false);
 if(externalLoadPending){externalLoadPending=false;++externalLoadRequest;picker.reset();juce::PopupMenu::dismissAllActiveMenus();}
 prepareOfflineSession();stopAll(false);exportWorker->cancel();importer->cancelPendingWork();
 message.setText("Finishing recording/session work and cancelling pending export/import before close...",juce::dontSendNotification);
}
void MainComponent::pumpCloseRequest(){
 if(closePhase==ClosePhase::complete){auto approved=std::move(closeApproved);closePhase=ClosePhase::idle;setEnabled(true);if(approved)approved();return;}
 if(closePhase!=ClosePhase::waiting||activeCloseWork())return;
 if(closeWasRecording&&vocals.recorder.state()==hem::TakeRecorder::State::Failed){abortClose("Recording could not finalize; Studio remains open: "+vocals.recorder.result());return;}
 if(!closeNeedsSave||!hasUnsavedSessionChanges()){closePhase=ClosePhase::complete;return;}
 if(currentSessionFile!=juce::File()){
  closePhase=ClosePhase::saving;const auto result=saveSessionFile(currentSessionFile);if(result.failed())abortClose("Save failed; Studio remains open: "+result.getErrorMessage());return;
 }
 closePhase=ClosePhase::choosingSave;
 picker=std::make_unique<juce::FileChooser>("Save session before closing",juce::File(),"*.xml;*.json");juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe](const juce::FileChooser& chooser){
  if(!safe||safe->closePhase!=ClosePhase::choosingSave)return;
  if(chooser.getResult()==juce::File()){safe->abortClose("Save cancelled; Studio remains open");return;}
  safe->closePhase=ClosePhase::saving;const auto result=safe->saveSessionFile(chooser.getResult());if(result.failed())safe->abortClose("Save failed; Studio remains open: "+result.getErrorMessage());
 });
}

bool MainComponent::hasExternalInserts() const {
 if(!bank)return false;
 for(const auto& track:bank->tracks)for(int slot=0;slot<8;++slot)
  if(auto* p=track.chain.activeProcessor(slot);p&&p->isExternal())return true;
 return false;
}
hem::SessionSnapshot MainComponent::captureSession(bool includeExternalState){
 struct CaptureGates {
  std::vector<std::pair<hem::ModuleProcessor*,bool>> held;
  ~CaptureGates(){for(auto& entry:held)entry.first->gateForStateCapture(entry.second);}
 } gates;
 if(includeExternalState&&hasExternalInserts()){
  if(playing()||vocals.recorder.busy())throw std::runtime_error("Pause playback and finish recording before capturing external VST3 state");
  gates.held.reserve(13*8);
  // Existing callback exclusion establishes that no previously-entered process
  // call remains. Expensive vendor serialization happens only after unlocking.
  {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
   for(auto& track:bank->tracks)for(int i=0;i<8;++i){
    if(track.chain.isBusy(i))throw std::runtime_error("Wait for insert transitions before capturing VST3 state");
    if(auto* p=track.chain.activeProcessor(i);p&&p->isExternal())gates.held.emplace_back(p,p->gateForStateCapture(true));
   }
  }
 }
 return hem::SessionState::capture(*bank,inputDb.load(),outputDb.load(),sessionPosition(),looping,monitorProcessor,includeExternalState);
}
juce::Result MainComponent::stageExternalSession(hem::TrackBank& staged,const hem::SessionSnapshot& snapshot,hem::SessionWorker::Context context){
 if(closePhase==ClosePhase::waiting||closePhase==ClosePhase::complete)return juce::Result::fail("Session recall cancelled while closing; existing session retained");
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());if(context.generation!=generation||context.sampleRate!=sampleRate||context.blockSize!=blockSize)return juce::Result::fail("Device changed during recall; existing session retained");}
 for(size_t track=0;track<snapshot.tracks.size();++track)for(int slot=0;slot<8;++slot){
  const auto& saved=snapshot.tracks[track].slots[size_t(slot)];if(saved.pluginId!="external-vst3")continue;
  juce::String error;auto& chain=staged.tracks[track].chain;
  auto processor=externalHost.stage(saved.external,context.sampleRate,context.blockSize,chain.slotLatency(),false,error);
  if(!processor)return juce::Result::fail("VST3 recall unchanged: "+error);
  auto node=chain.stageNode(std::move(processor),context.sampleRate,context.blockSize,chain.slotLatency());
  chain.installInitial(slot,std::move(node),saved.bypassed);
 }
 return juce::Result::ok();
}
juce::Result MainComponent::installExternal(const hem::ExternalPluginState& state,int trackIndex,int slotIndex,unsigned expectedGeneration){
 if(trackIndex<0||trackIndex>=13||slotIndex<0||slotIndex>=8)return juce::Result::fail("Invalid VST3 destination");
 if(closePhase!=ClosePhase::idle||playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork())return juce::Result::fail("Pause and finish pending work before loading a VST3");
 auto& chain=bank->tracks[size_t(trackIndex)].chain;
 double rate;int block,budget;
 {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
  if(!prepared||generation!=expectedGeneration||chain.isBusy(slotIndex))return juce::Result::fail("Device/session changed or slot is transitioning; existing insert retained");
  rate=sampleRate;block=blockSize;budget=chain.slotLatency();
 }
 try{
  juce::String error;auto processor=externalHost.stage(state,rate,block,budget,false,error);
  if(!processor)return juce::Result::fail(error);
  auto node=chain.stageNode(std::move(processor),rate,block,budget);
  bool installed=false;std::unique_ptr<hem::InsertChain::PreparedNode> retired;
  {const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
   if(prepared&&generation==expectedGeneration&&!chain.isBusy(slotIndex)){
    const bool offline=deviceManager.getCurrentAudioDevice()==nullptr;
    installed=offline?chain.publishStopped(slotIndex,std::move(node)):chain.publish(slotIndex,std::move(node));
    if(offline&&installed)retired=chain.collectRetired(slotIndex);
   }
  }
  if(retired&&editedProcessor==retired->processor.get())closePluginEditor();
  if(!installed)return juce::Result::fail("Device changed while loading VST3; existing insert retained");
  message.setText("VST3 loaded / stereo effect / "+juce::String(budget)+" sample maximum slot latency / pending transition if device active",juce::dontSendNotification);
  return juce::Result::ok();
 }catch(const std::exception& e){return juce::Result::fail(e.what());}
}
void MainComponent::chooseExternalPlugin(){
 if(externalLoadPending||closePhase!=ClosePhase::idle)return;
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||importer->hasPendingWork()){
  message.setText("Pause and finish pending work before loading a trusted VST3",juce::dontSendNotification);return;
 }
 const unsigned request=++externalLoadRequest;unsigned version;{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());version=generation;}const int targetTrack=selectedTrack,targetSlot=selected;
 externalLoadPending=true;message.setText("Select one trusted VST3. Discovery executes native code in this process; no automatic folder scan.",juce::dontSendNotification);
 picker=std::make_unique<juce::FileChooser>("Select one trusted stereo-effect VST3 / native code / 6 ms slot limit",juce::File(),"*.vst3");
 juce::Component::SafePointer<MainComponent> safe(this);
 picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectDirectories,
  [safe,request,version,targetTrack,targetSlot](const juce::FileChooser& chooser){
   if(!safe||safe->externalLoadRequest!=request)return;
   const auto selectedFile=chooser.getResult();
   if(selectedFile==juce::File()){safe->externalLoadPending=false;safe->message.setText("VST3 selection cancelled / insert unchanged",juce::dontSendNotification);return;}
   juce::MessageManager::callAsync([safe,request,version,targetTrack,targetSlot,selectedFile]{
    if(!safe||safe->externalLoadRequest!=request)return;
    juce::String error;auto descriptions=safe->externalHost.discoverSelected(selectedFile,error);
    if(descriptions.isEmpty()){safe->externalLoadPending=false;safe->message.setText(error,juce::dontSendNotification);return;}
    auto selectedType=[safe,request,version,targetTrack,targetSlot,descriptions](int result){
     if(!safe||safe->externalLoadRequest!=request)return;
     safe->externalLoadPending=false;
     if(result<1||result>descriptions.size()){safe->message.setText("VST3 type selection cancelled / insert unchanged",juce::dontSendNotification);return;}
     hem::ExternalPluginState state;state.descriptionXml=descriptions.getReference(result-1).createXml()->toString();
     const auto installed=safe->installExternal(state,targetTrack,targetSlot,version);
     if(installed.failed())safe->message.setText(installed.getErrorMessage(),juce::dontSendNotification);
    };
    if(descriptions.size()==1){selectedType(1);return;}
    juce::PopupMenu menu;for(int i=0;i<descriptions.size();++i)menu.addItem(i+1,descriptions.getReference(i).name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(safe->workspace.get()),std::move(selectedType));
   });
  });
}
void MainComponent::reloadExternalPlugin(){
 if(playing()||vocals.recorder.busy()||sessionWorker->isBusy()||exportWorker->isBusy()||externalLoadPending){message.setText("Pause and finish pending work before reloading VST3",juce::dontSendNotification);return;}
 auto* processor=current().chain.activeProcessor(selected);
 if(!processor||!processor->isExternal()){message.setText("Select an existing VST3 slot to reload",juce::dontSendNotification);return;}
 try{
  auto snapshot=captureSession();unsigned version;{const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());version=generation;}const auto result=installExternal(snapshot.tracks[size_t(selectedTrack)].slots[size_t(selected)].external,selectedTrack,selected,version);
  if(result.failed())message.setText("Reload rejected; previous state retained: "+result.getErrorMessage(),juce::dontSendNotification);
 }catch(const std::exception& e){message.setText(e.what(),juce::dontSendNotification);}
}

```

## Source/MainComponent.h

```cpp
#pragma once
#include "Systems/DeviceSettingsPanel.h"
#include "Systems/OfflineMixPanel.h"
#include <JuceHeader.h>
#include "SessionState.h"
#include "CoreMasterStage.h"
#include "Systems/CalibratedMeterView.h"
#include "Systems/PresetManager.h"
#include "Systems/AnalysisWorker.h"
#include "Systems/AnalysisView.h"
#include "Systems/MonitorProfile.h"
#include "Systems/VocalTrackingPanel.h"
#include "StemFileImporter.h"
#include "StudioWindow.h"
#include "Plugins/PluginRegistry.h"
#include "UI/NativeWorkspace.h"
#include "Plugins/ExternalPluginHost.h"

class MainComponent final:public juce::AudioAppComponent,private juce::Timer {
public:
 MainComponent();~MainComponent() override;
 enum class CloseChoice {cancel,discard,save};
 void requestClose(std::function<void()> approved);
 bool hasUnsavedSessionChanges();
 void prepareToPlay(int,double) override;
 void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
 void releaseResources() override;
 void paint(juce::Graphics&) override;void resized() override;
private:
 friend struct MainComponentTestAccess;
 // Declared before worker/bank owners, and therefore destroyed after them.
 hem::ExternalPluginHost externalHost;
 bool externalLoadPending=false;unsigned externalLoadRequest=0;
 juce::String lastExternalDiagnostic;
 bool hasExternalInserts() const;
 hem::SessionSnapshot captureSession(bool includeExternalState=true);
 juce::Result stageExternalSession(hem::TrackBank&,const hem::SessionSnapshot&,hem::SessionWorker::Context);
 juce::Result installExternal(const hem::ExternalPluginState&,int trackIndex,int slotIndex,unsigned expectedGeneration);
 void chooseExternalPlugin();void reloadExternalPlugin();
 void prepareOfflineSession();
 juce::Result saveSessionFile(const juce::File&);
 juce::Result loadSessionFile(const juce::File&);
 juce::Result clearTrackSources(bool all);
 juce::Result resetSessionConfirmed();
 void closePluginEditor();
 juce::ValueTree sessionFingerprint(hem::SessionSnapshot) const;
 void sessionFinished(bool saving,const juce::File&,const juce::Result&);
 void resolveCloseChoice(CloseChoice);
 void pumpCloseRequest();
 void abortClose(const juce::String&);
 bool activeCloseWork() const;
 enum class ClosePhase {idle,prompt,waiting,choosingSave,saving,complete};
 ClosePhase closePhase=ClosePhase::idle;
 bool closeNeedsSave=false,closeWasRecording=false;
 std::function<void()> closeApproved;
 using ClosePrompt=std::function<void(bool dirty,bool active,std::function<void(CloseChoice)>)>;
 ClosePrompt closePrompt;
 juce::File currentSessionFile;
 juce::ValueTree lastSavedSessionState,pendingSavedSessionState;
 enum class ShellPage {home,mixer,plugin,vault,routing,settings,utility};
 ShellPage shellPage=ShellPage::home;
 void showShellPage(ShellPage);
 void showUtility(juce::Component&,const juce::String&);
 void showAnalysis();
 void requestNewSession();
 std::unique_ptr<hem::NativeWorkspace> workspace;
 std::array<juce::TextButton,6> navigation;
 juce::TextButton unloadSourcesButton{"UNLOAD"},newSessionButton{"NEW"};
 juce::Viewport utilityViewport;
 juce::Label utilityTitle,statusBar;
 juce::Component* utilityContent=nullptr;
 juce::Point<int> utilityMinimum{600,350};
 std::unique_ptr<juce::Component> embeddedAnalysis,embeddedMonitor,embeddedVocal,embeddedExport;
 static constexpr int trackCount=13;
 void timerCallback() override;
 juce::Result commitImport(hem::StemFileImporter::Batch&);
 std::unique_ptr<hem::StemFileImporter> importer;
 void selectSlot(int);void installSelected(int);void openAudio();void loadAudio(const juce::File&,int);
 void showMonitor();
 void showExport();
 juce::Result startExport(const juce::File&,const hem::OfflineMixOptions&);
 std::unique_ptr<hem::OfflineMixWorker> exportWorker;
 std::unique_ptr<StudioWindow> exportWindow;
 juce::TextButton exportButton{"EXPORT MIX"};
 void showSettings();void showRack();void openPlugin(int);void moveSlot(int,int);
 double sessionPosition() const;
 void pauseAll();
 void playAll();void stopAll(bool);void seekAll(double);bool playing() const;
 void selectTrack(int);void configureKnob(juce::Slider&,std::atomic<float>&,const juce::String&);
 juce::Rectangle<int> map(float,float,float,float) const;
 hem::TrackEngine& current(){return bank->tracks[size_t(selectedTrack)];}
 HardwareLookAndFeel look,overlayLook{true};juce::Image faceplate,ghostBrand;juce::Rectangle<float> face;
 juce::Slider inputKnob,outputKnob,trackTrim;
 juce::TextButton play,stop,rewind,forward,loop,file,settings;
 std::array<juce::TextButton,8> slotButtons;
 juce::Component bay;juce::ComboBox moduleMenu,trackMenu;
 juce::TextButton insert{"INSERT / REPLACE"},remove{"REMOVE"},bypass{"BYPASS"},rackButton{"INSERT RACK"},openButton{"OPEN PLUGIN"},mute{"MUTE"},solo{"SOLO"},monitorButton{"MONITOR EQ"},pauseButton{"PAUSE"},outputButton{"AUDIO DEVICE / I/O"};
 juce::Label title,track,telemetry,inValue,outValue,message;
 std::unique_ptr<StudioWindow> pluginWindow,rackWindow,monitorWindow;
 hem::MonitorProcessor monitorProcessor;
 std::atomic<bool> monitorRouteReady{false};
 juce::String lastOutputDevice;
 juce::AudioBuffer<float> monitorBuffer,monitorView;
 juce::MidiBuffer monitorMidi;
 RackView* rackView=nullptr;
 hem::ModuleProcessor* editedProcessor=nullptr;int editedTrack=-1,editedSlot=-1;
 int hardwareOutputs=2;
 std::unique_ptr<hem::DeviceSettingsPanel> deviceSettings;
 int selected=0,selectedTrack=0;bool looping=false;
 juce::AudioFormatManager formats;juce::TimeSliceThread diskThread{"HEM audio reader"};
 std::unique_ptr<hem::TrackBank> bank=std::make_unique<hem::TrackBank>();
 hem::CoreMasterStage masterLimiter;
 hem::AudioMeters outputMeters;
 hem::CalibratedMeterView outputMeterView{outputMeters};
 hem::LiveVocalManager vocals;
 std::unique_ptr<StudioWindow> vocalWindow;
 juce::TextButton vocalButton{"VOCAL TRACKING"};
 void showVocals();
 double callbackTimeline() const;
 hem::MonitoringRouter monitoringRouter;
 hem::AnalysisWorker analyser;
 std::unique_ptr<StudioWindow> analysisWindow;
 std::unique_ptr<hem::MonitorProfileLoader> profileLoader;
 juce::TextButton analysisButton{"ANALYSIS"},calibrationButton{"LOAD CALIBRATION"},routeButton{"DIRECT OUTPUT"};
 juce::String calibrationName{"No measurement profile loaded"};
 void openCalibration();
 std::unique_ptr<hem::SessionWorker> sessionWorker;
 juce::TextButton saveSessionButton{"SAVE SESSION"},loadSessionButton{"LOAD SESSION"};
 void chooseSession(bool save);
 juce::Result commitSession(std::unique_ptr<hem::TrackBank>&,const hem::SessionSnapshot&,hem::SessionWorker::Context);
 std::unique_ptr<juce::FileChooser> picker;juce::String deviceError;
 double sampleRate=hem::defaultSampleRate;int blockSize=256;unsigned generation=0;bool prepared=false;
 std::atomic<float> inputDb{0},outputDb{0},peak{0};
 hem::Ramp inputGain,outputGain;std::vector<float> inputValues;
 juce::AudioBuffer<float> sum,listeningSource;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

```

## Source/Plugins/Editors.cpp

```cpp
#include "Editors.h"
#include "PluginMeterReadout.h"
namespace hem {
ModuleEditor::ModuleEditor(ModuleProcessor& p):AudioProcessorEditor(p),processor(p){
 setLookAndFeel(&look);for(const auto& spec:p.parameterSpecs()){
    auto c=std::make_unique<Control>();c->isToggle=spec.toggle;
    if(spec.toggle){c->toggle.setComponentID(spec.id);c->toggle.setName(spec.name);addAndMakeVisible(c->toggle);c->toggle.setButtonText(spec.name);c->buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,spec.id,c->toggle);}
    else {addAndMakeVisible(c->label);c->label.setText(spec.name,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);
        c->knob.setComponentID(spec.id);c->knob.setName(spec.name);c->knob.setTooltip(juce::String(spec.name)+" / "+spec.unit);addAndMakeVisible(c->knob);c->knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,110,22);c->knob.setTextValueSuffix(juce::String(" ")+spec.unit);
        c->knob.setMouseDragSensitivity(250);c->knob.setDoubleClickReturnValue(true,spec.initial);
        c->sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,spec.id,c->knob);}
    controls.push_back(std::move(c));}
 setSize(960,520);startTimerHz(25);
}
ModuleEditor::~ModuleEditor(){stopTimer();controls.clear();setLookAndFeel(nullptr);}
void ModuleEditor::resized(){const int cols=4,rows=(int(controls.size())+cols-1)/cols;const int top=125,cellW=getWidth()/cols,cellH=juce::jmax(1,(getHeight()-top)/rows);
 for(size_t i=0;i<controls.size();++i){auto& c=*controls[i];juce::Rectangle<int> b(int(i)%cols*cellW,top+int(i)/cols*cellH,cellW,cellH);
    b.reduce(4,2);if(c.isToggle)c.toggle.setBounds(b);else{c.label.setBounds(b.removeFromTop(20));c.knob.setBounds(b);}}}
void ModuleEditor::paint(juce::Graphics& g){
 g.fillAll(juce::Colour(0xf0080b10));
 juce::ColourGradient glass(juce::Colour(0xff253039),0,0,juce::Colour(0xff07090d),0,float(getHeight()),false);
 g.setGradientFill(glass);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3),12);
 g.setColour(juce::Colour(0x25ffffff));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(5),10,1);g.setColour(juce::Colour(0xff00ddff));g.drawRect(getLocalBounds(),1);
 g.setFont(18.f);g.drawText(processor.getName(),12,6,getWidth()-24,26,juce::Justification::centred);
 const auto area=juce::Rectangle<float>(16,42,float(getWidth()-32),77);
 const auto readout=ui::pluginMeterReadout(processor);
 if(auto* eq=dynamic_cast<SurgicalEqProcessor*>(&processor)){
    // This is the analytic TARGET response of the controls, not an analyser.
    // Keep the separately measured output peak and readiness visible beside it.
    auto responseArea=area.withWidth(area.getWidth()*.52f);
    const auto meterArea=area.withTrimmedLeft(responseArea.getWidth()+8);
    g.setColour(juce::Colour(0xff021018));g.fillRect(responseArea);
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(11.f);
    g.drawText("ANALYTIC CONTROL RESPONSE / TARGET",responseArea.withHeight(15).toNearestInt(),juce::Justification::centred);
    const auto plot=responseArea.reduced(31,0).withY(area.getY()+19).withHeight(39);
    const auto p=eq->settings();const double sr=eq->actualRate.load();
    const double maxHz=std::max(20.0,std::min(20000.0,sr*.45));
    g.setFont(9.f);
    for(const float db:{-24.f,0.f,24.f}){
        const float y=plot.getCentreY()-db*plot.getHeight()/48;
        g.setColour(juce::Colour(0xff344751));g.drawHorizontalLine(int(std::round(y)),plot.getX(),plot.getRight());
        g.setColour(juce::Colour(0xffa9bac4));g.drawText(juce::String(db,0),int(responseArea.getX()),int(y)-6,28,12,juce::Justification::centredRight);
    }
    juce::Path curve;
    for(int i=0;i<256;++i){const double hz=20*std::pow(maxHz/20,double(i)/255);const float db=EqCore::response(hz,sr,p);
        const float x=plot.getX()+plot.getWidth()*float(i)/255,y=plot.getCentreY()-juce::jlimit(-24.f,24.f,db)*plot.getHeight()/48;
        if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}
    g.setColour(juce::Colours::orange);g.strokePath(curve,juce::PathStrokeType(2));
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(10.f);
    g.drawText("dB / 20 Hz",int(responseArea.getX()+4),int(area.getY()+63),100,13,juce::Justification::centredLeft);
    g.drawText(juce::String(maxHz/1000,1)+" kHz / LOG",int(responseArea.getRight()-124),int(area.getY()+63),120,13,juce::Justification::centredRight);
    ui::paintPluginMeter(g,meterArea,readout);
 }else ui::paintPluginMeter(g,area,readout);
}
}

```

## Source/Plugins/Editors.h

```cpp
#pragma once
#include "Processors.h"
#include "../HardwareLookAndFeel.h"
namespace hem {
class ModuleEditor : public juce::AudioProcessorEditor,private juce::Timer {
public:
 explicit ModuleEditor(ModuleProcessor&);~ModuleEditor() override;
 void paint(juce::Graphics&) override;void resized() override;
private:
 struct Control {
    juce::Label label;juce::Slider knob;juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    bool isToggle=false;
 };
 void timerCallback() override {repaint();}
 ModuleProcessor& processor;HardwareLookAndFeel look;
 std::vector<std::unique_ptr<Control>> controls;
};
class SipaEditor final:public ModuleEditor {public:explicit SipaEditor(SipaProcessor& p):ModuleEditor(p){}};
class SurgicalEqEditor final:public ModuleEditor {public:explicit SurgicalEqEditor(SurgicalEqProcessor& p):ModuleEditor(p){}};
class VcaEditor final:public ModuleEditor {public:explicit VcaEditor(VcaProcessor& p):ModuleEditor(p){}};
class TruePeakEditor final:public ModuleEditor {public:explicit TruePeakEditor(TruePeakProcessor& p):ModuleEditor(p){}};
}

```

## Source/Plugins/ExternalPluginHost.cpp

```cpp
#include "ExternalPluginHost.h"
#include <algorithm>
#include <cmath>
#if JUCE_PLUGINHOST_VST3
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#endif

namespace hem {
namespace {
bool onMessageThread() noexcept {
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager != nullptr && manager->isThisTheMessageThread();
}

bool requireMessageThread(juce::String& error) {
    if (onMessageThread()) return true;
    error = "VST3 discovery, preparation, state capture, and editor operations require the message thread.";
    return false;
}

enum class ExternalFault : int {
    none, contextChanged, restartRequired, modeChanged, latencyChanged, configurationChanged,
    invalidBuffer, nonFiniteAudio, processingException, directStateRestore
};

// A replacement/reload can have the same descriptor but different opaque
// state. Never reset its dirty token to zero or reuse another owner's token.
// Allocation is one lock-free atomic operation, also in audio-thread callbacks.
std::atomic<uint64_t> nextExternalRevision{1};
uint64_t allocateExternalRevision() noexcept {
    return nextExternalRevision.fetch_add(1, std::memory_order_relaxed);
}

#if JUCE_PLUGINHOST_VST3
// Public JUCE extension visitor + public VST3 interfaces, used only on the
// message thread before publication. Never retain the temporary VST3Client.
// The queried audio interface owns one reference; the component is borrowed
// from the still-live AudioPluginInstance.
class NativeVST3Probe final : public juce::ExtensionsVisitor {
public:
    ~NativeVST3Probe() override {if (audio != nullptr) audio->release();}
    void visitVST3Client(const VST3Client& client) override {
        component = client.getIComponentPtr();
        void* queried = nullptr;
        if (component != nullptr
            && component->queryInterface(Steinberg::Vst::IAudioProcessor_iid, &queried) == Steinberg::kResultOk)
            audio = static_cast<Steinberg::Vst::IAudioProcessor*>(queried);
    }
    bool hasAudioInterface(juce::String& error) const {
        if (component != nullptr && audio != nullptr) return true;
        error = "The VST3 does not expose a verifiable native audio-processing interface.";
        return false;
    }
    Steinberg::Vst::IComponent* component = nullptr;
    Steinberg::Vst::IAudioProcessor* audio = nullptr;
};
#endif

bool verifyNativeFloat32(juce::AudioPluginInstance& instance, juce::String& error) {
   #if JUCE_PLUGINHOST_VST3
    NativeVST3Probe probe;
    instance.getExtensions(probe);
    if (!probe.hasAudioInterface(error)) return false;
    if (probe.audio->canProcessSampleSize(Steinberg::Vst::kSample32) != Steinberg::kResultTrue) {
        error = "The VST3 does not support 32-bit float audio processing. Double-only effects are unsupported.";
        return false;
    }
    return true;
   #else
    juce::ignoreUnused(instance);
    error = "This build does not have VST3 hosting enabled.";
    return false;
   #endif
}

bool verifyNativePreparation(juce::AudioPluginInstance& instance, int budget,
                             int& latency, juce::String& error) {
   #if JUCE_PLUGINHOST_VST3
    NativeVST3Probe probe;
    instance.getExtensions(probe);
    if (!probe.hasAudioInterface(error)) return false;
    const auto nativeLatency = probe.audio->getLatencySamples(); // uint32, not JUCE's clamped int.
    if (nativeLatency > static_cast<Steinberg::uint32>(budget)) {
        error = "VST3 latency is " + juce::String(static_cast<juce::int64>(nativeLatency))
            + " samples; this slot permits at most " + juce::String(budget) + " samples (6 ms).";
        return false;
    }
    latency = static_cast<int>(nativeLatency); // Safe only after checking the small positive budget.
    if (instance.getLatencySamples() != latency) {
        error = "The VST3 latency changed during preparation. Reload VST3 to obtain a stable configuration.";
        return false;
    }
    using namespace Steinberg::Vst;
    if (probe.component->getBusCount(kAudio, kInput) != instance.getBusCount(true)
        || probe.component->getBusCount(kAudio, kOutput) != 1) {
        error = "The VST3 native bus count changed during preparation."; return false;
    }
    for (const auto direction : {kInput, kOutput}) {
        BusInfo info{};
        SpeakerArrangement arrangement{};
        if (probe.component->getBusInfo(kAudio, direction, 0, info) != Steinberg::kResultOk
            || info.mediaType != kAudio || info.direction != direction
            || info.busType != kMain || info.channelCount != 2
            || probe.audio->getBusArrangement(direction, 0, arrangement) != Steinberg::kResultOk
            || arrangement != SpeakerArr::kStereo) {
            error = "The prepared VST3 native input/output arrangement is not ordinary stereo.";
            return false;
        }
    }
    // Disabled auxiliary buses can retain their channel arrangements in VST3.
    // setBusesLayout has already verified they accept deactivation. The public
    // native interface has no getter for current bus activation state.
    return true;
   #else
    juce::ignoreUnused(instance, budget, latency);
    error = "This build does not have VST3 hosting enabled.";
    return false;
   #endif
}

bool hasStereoOnlyLayout(const juce::AudioPluginInstance& instance) {
    if (instance.getBusCount(true) < 1 || instance.getBusCount(false) != 1
        || instance.getTotalNumInputChannels() != 2 || instance.getTotalNumOutputChannels() != 2)
        return false;
    const auto layout = instance.getBusesLayout();
    if (layout.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        || layout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        if (!layout.inputBuses.getReference(i).isDisabled()) return false;
    return true;
}

bool configureStereoOnly(juce::AudioPluginInstance& instance, juce::String& error) {
    if (instance.getBusCount(true) < 1 || instance.isMidiEffect()) {
        error = "VST3 instruments and MIDI-only effects are unsupported; a stereo audio input is required.";
        return false;
    }
    if (instance.getBusCount(false) != 1) {
        error = "Multi-output VST3 effects are unsupported; exactly one stereo output bus is required.";
        return false;
    }
    auto layout = instance.getBusesLayout();
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference(i) = juce::AudioChannelSet::disabled();
    if (!instance.setBusesLayout(layout) || !hasStereoOnlyLayout(instance)) {
        error = "The VST3 does not support stereo-in/stereo-out with every auxiliary input disabled. Mandatory sidechains are unsupported.";
        return false;
    }
    return true;
}
} // namespace

// Registered on the real instance rather than the short-lived adapter. Listener
// callbacks may run in processBlock: only lock-free atomics are touched here.
class ExternalInstanceOwner final : private juce::AudioProcessorListener {
public:
    ExternalInstanceOwner(std::unique_ptr<juce::AudioPluginInstance> p, juce::String xml,
                          double rate, int block, int latency, bool offline, double tail)
        : instance(std::move(p)), descriptionXml(std::move(xml)), preparedRate(rate),
          preparedBlock(block), preparedLatency(latency), preparedOffline(offline), preparedTail(tail) {
        jassert(onMessageThread());
        instance->addListener(this);
    }
    ~ExternalInstanceOwner() override {
        jassert(onMessageThread());
        jassert(!editorOpen);
        instance->removeListener(this);
        instance->releaseResources();
        // JUCE's VST3 destructor itself calls callSync. Since this owner is
        // destroyed on the message thread, it cannot deadlock a worker join.
        instance.reset();
    }
    void invalidate(ExternalFault reason) noexcept {
        auto expected = ExternalFault::none;
        if (fault.compare_exchange_strong(expected, reason, std::memory_order_acq_rel))
            revision.store(allocateExternalRevision(), std::memory_order_relaxed);
    }
    bool ready() const noexcept {
        return fault.load(std::memory_order_acquire) == ExternalFault::none
            && !captureGated.load(std::memory_order_acquire);
    }
    juce::String diagnostic() const {
        switch (fault.load(std::memory_order_acquire)) {
            case ExternalFault::none:
                return captureGated.load(std::memory_order_acquire)
                    ? "VST3 temporarily gated for state capture." : juce::String{};
            case ExternalFault::contextChanged: return "The audio sample rate or maximum block changed. Reload VST3 to resume.";
            case ExternalFault::restartRequired: return "The VST3 processing lifecycle restarted. Reload VST3 to reset its audio history.";
            case ExternalFault::modeChanged: return "The realtime/offline mode changed. Reload VST3 for this render context.";
            case ExternalFault::latencyChanged: return "The VST3 reported a latency change. Reload VST3 to revalidate alignment.";
            case ExternalFault::configurationChanged: return "The VST3 reported an I/O, program, or parameter-layout change. Reload VST3 to revalidate it.";
            case ExternalFault::invalidBuffer: return "The VST3 received an unsupported channel count or oversized block. Reload VST3.";
            case ExternalFault::nonFiniteAudio: return "The VST3 produced non-finite audio and was disabled. Reload VST3 after checking its settings.";
            case ExternalFault::processingException: return "The VST3 threw an exception while processing and was disabled. Reload VST3.";
            case ExternalFault::directStateRestore: return "VST3 state must be restored into a separately staged instance. Reload VST3.";
        }
        return "The VST3 is unavailable. Reload VST3.";
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;
    const juce::String descriptionXml;
    const double preparedRate;
    const int preparedBlock, preparedLatency;
    const bool preparedOffline;
    const double preparedTail;
    std::atomic<ExternalFault> fault{ExternalFault::none};
    std::atomic<bool> captureGated{false};
    std::atomic<uint64_t> revision{allocateExternalRevision()};
    bool editorOpen = false; // Message thread only.

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {
        revision.store(allocateExternalRevision(), std::memory_order_relaxed);
    }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        revision.store(allocateExternalRevision(), std::memory_order_relaxed);
        if (details.latencyChanged) invalidate(ExternalFault::latencyChanged);
        // JUCE 8's VST3 kIoChanged restart emits parameterInfoChanged and
        // programChanged, not a dedicated I/O flag. Fail closed, including
        // metadata-only changes, rather than reading mutable bus/latency fields
        // from the processing thread. The prepared latency stays immutable.
        else if (details.parameterInfoChanged || details.programChanged)
            invalidate(ExternalFault::configurationChanged);
    }
};
static_assert(std::atomic<ExternalFault>::is_always_lock_free
              && std::atomic<bool>::is_always_lock_free
              && std::atomic<uint64_t>::is_always_lock_free);

namespace {
// This first base is destroyed last, after AudioProcessorEditor's destructor.
// Even an editor retained past adapter replacement keeps the actual instance
// alive. Normal host teardown still closes all editors before collecting owners.
struct EditorOwnerLifetime {
    explicit EditorOwnerLifetime(std::shared_ptr<ExternalInstanceOwner> value) : owner(std::move(value)) {}
    std::shared_ptr<ExternalInstanceOwner> owner;
};

class ExternalEditor final : private EditorOwnerLifetime,
                             public juce::AudioProcessorEditor,
                             private juce::ComponentListener {
   #if JUCE_LINUX || JUCE_BSD
    class EmbeddingMovementWatcher final : public juce::ComponentMovementWatcher {
    public:
        explicit EmbeddingMovementWatcher(ExternalEditor& editor)
            : juce::ComponentMovementWatcher(&editor), outer(editor) {}
        void componentMovedOrResized(bool, bool) override {outer.refreshEmbeddedBounds();}
        void componentPeerChanged() override {outer.refreshEmbeddedBounds();}
        void componentVisibilityChanged() override {outer.refreshEmbeddedBounds();}
    private:
        ExternalEditor& outer;
    };
   #endif
public:
    ExternalEditor(std::shared_ptr<ExternalInstanceOwner> held,
                   std::unique_ptr<juce::AudioProcessorEditor> content)
        : EditorOwnerLifetime(std::move(held)), juce::AudioProcessorEditor(*owner->instance),
          child(std::move(content)) {
        jassert(onMessageThread());
        owner->editorOpen = true;
        addAndMakeVisible(*child);
        child->addComponentListener(this);
        setResizable(child->isResizable(), false);
        setSize(juce::jlimit(64, 8192, child->getWidth()), juce::jlimit(64, 8192, child->getHeight()));
       #if JUCE_LINUX || JUCE_BSD
        // Observe all ancestor moves, not just resize notifications on the
        // plug-in child. JUCE's VST3PluginWindow ignores moved-only callbacks,
        // which otherwise leaves its native XEmbed window at the old origin.
        movementWatcher = std::make_unique<EmbeddingMovementWatcher>(*this);
        refreshEmbeddedBounds();
       #endif
    }
    ~ExternalEditor() override {
        jassert(onMessageThread());
        closing = true;
       #if JUCE_LINUX || JUCE_BSD
        movementWatcher.reset(); // Detach ancestor listeners before destroying the child.
       #endif
        child->removeComponentListener(this);
        owner->instance->editorBeingDeleted(child.get());
        child.reset();
        owner->editorOpen = false;
    }
    void resized() override {
        if (resizing || !child) return;
        const juce::ScopedValueSetter<bool> guard(resizing, true);
        child->setBounds(getLocalBounds());
        refreshEmbeddedBounds();
    }
private:
    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override {
        if (!wasResized || resizing || &component != child.get()) return;
        const juce::ScopedValueSetter<bool> guard(resizing, true);
        setSize(juce::jlimit(64, 8192, child->getWidth()), juce::jlimit(64, 8192, child->getHeight()));
        child->setTopLeftPosition(0, 0);
        refreshEmbeddedBounds();
    }
    void refreshEmbeddedBounds() {
       #if JUCE_LINUX || JUCE_BSD
        if (closing || syncingEmbedded || !child) return;
        jassert(onMessageThread());
        juce::Component::SafePointer<ExternalEditor> safe(this);
        syncingEmbedded = true;
        const juce::ScopeGuard resetGuard{[safe] {if (safe != nullptr) safe->syncingEmbedded = false;}};
        refreshEmbeddedSubtree(*child);
       #endif
    }
   #if JUCE_LINUX || JUCE_BSD
    static void refreshEmbeddedSubtree(juce::Component& component) {
        // Updating a native child can synchronously notify components. Never
        // retain a raw descendant across that callback boundary.
        juce::Component::SafePointer<juce::Component> safe(&component);
        if (auto* embedded = dynamic_cast<juce::XEmbedComponent*>(&component))
            embedded->updateEmbeddedBounds();
        for (int i = 0; safe != nullptr && i < safe->getNumChildComponents(); ++i)
            if (auto* descendant = safe->getChildComponent(i)) refreshEmbeddedSubtree(*descendant);
    }
    std::unique_ptr<EmbeddingMovementWatcher> movementWatcher;
    bool syncingEmbedded = false;
   #endif
    std::unique_ptr<juce::AudioProcessorEditor> child;
    bool resizing = false, closing = false;
};

class ExternalModuleProcessor final : public ModuleProcessor {
public:
    explicit ExternalModuleProcessor(std::shared_ptr<ExternalInstanceOwner> instanceOwner,
                                     const juce::String& name)
        : ModuleProcessor("EXTERNAL_VST3", name, {}), owner(std::move(instanceOwner)) {
        // This is the wrapper's latency. It is never changed in response to a
        // plugin callback; InsertChain's dry/compensation delays depend on it.
        setLatencySamples(owner->preparedLatency);
        actualRate.store(owner->preparedRate, std::memory_order_relaxed);
        juce::AudioProcessor::setNonRealtime(owner->preparedOffline);
        publishMeterPreparation();
    }
    bool isExternal() const noexcept override {return true;}
    double getTailLengthSeconds() const override {return owner->preparedTail;}
    bool processingReady() const noexcept override {return owner->ready();}
    bool gateForStateCapture(bool gated) noexcept override {
        return owner->captureGated.exchange(gated, std::memory_order_acq_rel);
    }
    uint64_t externalStateRevision() const noexcept override {
        return owner->revision.load(std::memory_order_relaxed);
    }
    juce::String externalDescriptionXml() const override {return owner->descriptionXml;}
    juce::String externalDiagnostic() const override {return owner->diagnostic();}
    void prepareToPlay(double rate, int maximumBlock) override {
        // Called from both device callbacks and offline workers. Never forward
        // to VST3 setupProcessing here, and never wait on the message thread.
        if (rate != owner->preparedRate || maximumBlock != owner->preparedBlock) {
            owner->invalidate(ExternalFault::contextChanged); return;
        }
        // The first call validates the fresh, already-prepared adapter when
        // stageNode installs it. Any later call represents another lifecycle
        // preparation, even at identical rate/block. The vendor's delay/history
        // cannot safely be reset on this arbitrary caller thread.
        if (preparationValidated.exchange(true, std::memory_order_acq_rel)) {
            owner->invalidate(ExternalFault::restartRequired); return;
        }
        if (owner->fault.load(std::memory_order_acquire) != ExternalFault::none) return;
        resetMeterTelemetry();
        outputPeak.store(0, std::memory_order_relaxed);
        publishMeterPreparation();
    }
    void releaseResources() override {
        // Actual release/destruction remains with the message-thread owner.
        // Quarantine immediately so host-delay resets cannot reuse stale DSP.
        owner->invalidate(ExternalFault::restartRequired);
    }
    void setNonRealtime(bool offline) noexcept override {
        if (offline != owner->preparedOffline) owner->invalidate(ExternalFault::modeChanged);
        juce::AudioProcessor::setNonRealtime(offline);
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (!processingReady()) return;
        if (buffer.getNumChannels() != 2 || buffer.getNumSamples() > owner->preparedBlock) {
            owner->invalidate(ExternalFault::invalidBuffer); return;
        }
        if (buffer.getNumSamples() <= 0) return;
        juce::ScopedNoDenormals noDenormals;
        try {
            // JUCE's VST3 wrapper takes an internal process mutex. This adapter
            // does not claim arbitrary third-party code is lock/alloc-free.
            owner->instance->processBlock(buffer, midi);
        } catch (...) {
            owner->invalidate(ExternalFault::processingException); return;
        }
        if (!processingReady()) return; // Caller also checks and selects aligned dry.
        float peak = 0;
        for (int channel = 0; channel < 2; ++channel) {
            const auto* samples = buffer.getReadPointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                if (!std::isfinite(samples[i])) {
                    owner->invalidate(ExternalFault::nonFiniteAudio); return;
                }
                peak = juce::jmax(peak, std::abs(samples[i]));
            }
        }
        outputPeak.store(peak, std::memory_order_relaxed);
        publishMeterBlock(buffer);
    }
    using ModuleProcessor::processBlock;
    float meter() const noexcept override {return outputPeak.load(std::memory_order_relaxed);}
    juce::String meterLabel() const override {return "VST3 OUTPUT SAMPLE PEAK";}
    juce::AudioProcessorEditor* createEditor() override {
        if (!onMessageThread() || owner->editorOpen || owner->instance->getActiveEditor() != nullptr)
            return nullptr;
        bool allowNativeEditor = true;
       #if JUCE_LINUX || JUCE_BSD
        // JUCE's XEmbed native editor requires an X display. Offscreen tests
        // and headless exports can still use the genuine parameter editor.
        allowNativeEditor = juce::SystemStats::getEnvironmentVariable("DISPLAY", {}).isNotEmpty();
       #endif
        std::unique_ptr<juce::AudioProcessorEditor> child;
        if (allowNativeEditor) child.reset(owner->instance->createEditorIfNeeded());
        if (!child) child = std::make_unique<juce::GenericAudioProcessorEditor>(*owner->instance);
        return new ExternalEditor(owner, std::move(child));
    }
    void getStateInformation(juce::MemoryBlock& destination) override {
        // This override deliberately never serializes ModuleProcessor's empty
        // APVTS. Host capture provides errors; generic callers get no state if
        // they have not established the required capture contract.
        destination.reset();
        if (!onMessageThread() || !owner->captureGated.load(std::memory_order_acquire)) return;
        owner->instance->getStateInformation(destination);
        if (destination.getSize() > ExternalPluginState::maxStateBytes) destination.reset();
    }
    void setStateInformation(const void*, int) override {
        // Mutating an installed instance would bypass bus/latency validation.
        owner->invalidate(ExternalFault::directStateRestore);
    }

    const std::shared_ptr<ExternalInstanceOwner> owner;
private:
    std::atomic<bool> preparationValidated{false};
    std::atomic<float> outputPeak{0};
};

// The owner pool is the normal last reference. If a caller violates the host
// lifetime contract, never let JUCE's synchronous VST3 destructor run on a
// worker that the message thread may be joining. Queue cleanup, without a wait.
// If the message system has already gone away, retain the orphan rather than
// invoke undefined third-party teardown off-thread during process shutdown.
void destroyOwnerOnMessageThread(ExternalInstanceOwner* owner) {
    if (onMessageThread()) { delete owner; return; }
    jassertfalse;
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        juce::MessageManager::callAsync([owner] {delete owner;});
}
} // namespace

ExternalPluginHost::ExternalPluginHost() {
    jassert(onMessageThread());
   #if JUCE_PLUGINHOST_VST3
    // Do not use addDefaultFormats(): this bounded host accepts only VST3.
    formats.addFormat(new juce::VST3PluginFormat());
   #endif
}

ExternalPluginHost::~ExternalPluginHost() {
    jassert(onMessageThread());
    for (const auto& owner : owners) {
        juce::ignoreUnused(owner);
        jassert(owner.use_count() == 1); // Join workers and close editors first.
    }
    owners.clear();
}

juce::Array<juce::PluginDescription> ExternalPluginHost::discoverSelected(const juce::File& file,
                                                                        juce::String& error) {
    error.clear();
    juce::Array<juce::PluginDescription> result;
    if (!requireMessageThread(error)) return result;
    if (!file.hasFileExtension(".vst3") || !file.exists()) {
        error = "Select an existing .vst3 file or bundle."; return result;
    }
    if (formats.getNumFormats() != 1) {
        error = "This build does not have VST3 hosting enabled."; return result;
    }
    juce::OwnedArray<juce::PluginDescription> descriptions;
    try {formats.getFormat(0)->findAllTypesForFile(descriptions, file.getFullPathName());}
    catch (...) {error = "The selected VST3 threw an exception during discovery."; return result;}
    if (descriptions.size() > 64) {
        error = "The selected VST3 container exceeds the 64-class discovery limit."; return result;
    }
    for (const auto* description : descriptions) {
        ExternalPluginState candidate;
        candidate.descriptionXml = description->createXml()->toString();
        juce::String candidateError;
        if (candidate.validate(candidateError)) result.add(*description);
        else if (error.isEmpty()) error = candidateError;
    }
    if (result.isEmpty() && error.isEmpty()) error = "No supported VST3 audio effect was found in the selected file.";
    if (!result.isEmpty()) error.clear();
    return result;
}

std::unique_ptr<ModuleProcessor> ExternalPluginHost::stage(const ExternalPluginState& saved,
                                                         double rate, int block, int budget,
                                                         bool offline, juce::String& error) {
    error.clear();
    if (!requireMessageThread(error)) return {};
    juce::PluginDescription description;
    if (!saved.getDescription(description, error)) return {};
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000 || block < 1 || block > 1048576
        || budget != static_cast<int>(std::ceil(rate * 0.006))) {
        error = "Invalid VST3 render context: an actual sample rate, maximum block, and fixed 6 ms slot budget are required.";
        return {};
    }
    if (!juce::File(description.fileOrIdentifier).exists()) {
        error = "The saved VST3 file is missing: " + description.fileOrIdentifier; return {};
    }
    if (formats.getNumFormats() != 1) {
        error = "This build does not have VST3 hosting enabled."; return {};
    }
    // Nothing below can mutate the old node. Failure destroys this unpublished
    // instance here, on the message thread.
    std::unique_ptr<juce::AudioPluginInstance> instance;
    try {
        instance = formats.createPluginInstance(description, rate, block, error);
        if (!instance) {
            if (error.isEmpty()) error = "The selected VST3 could not be instantiated.";
            return {};
        }
        juce::PluginDescription actualDescription;
        instance->fillInPluginDescription(actualDescription);
        if (actualDescription.pluginFormatName != "VST3" || actualDescription.isInstrument
            || actualDescription.uniqueId != description.uniqueId) {
            error = "The loaded VST3 does not match the saved stereo-effect descriptor."; return {};
        }
        instance->releaseResources();
        instance->setNonRealtime(offline);
        instance->setProcessingPrecision(juce::AudioProcessor::singlePrecision);
        instance->setRateAndBufferSizeDetails(rate, block);
        if (saved.state.getSize() != 0)
            instance->setStateInformation(saved.state.getData(), static_cast<int>(saved.state.getSize()));
        instance->releaseResources();
        if (!configureStereoOnly(*instance, error)) return {};
        if (!verifyNativeFloat32(*instance, error)) return {};
        // JUCE's public prepareToPlay is void and internally discards native
        // setup/activation results. Read-only capability/layout/latency checks
        // below narrow acceptance; they cannot certify arbitrary vendor setup
        // success, and the adapter does not bypass JUCE's lifecycle machinery.
        instance->prepareToPlay(rate, block);
        if (!hasStereoOnlyLayout(*instance)) {
            error = "The VST3 changed to an unsupported bus layout during preparation."; return {};
        }
        int latency = 0;
        if (!verifyNativePreparation(*instance, budget, latency, error)) return {};
        const auto reportedTail = instance->getTailLengthSeconds();
        const double tail = std::isfinite(reportedTail) && reportedTail >= 0 ? reportedTail : 0;
        auto owner = std::shared_ptr<ExternalInstanceOwner>(
            new ExternalInstanceOwner(std::move(instance), saved.descriptionXml, rate, block, latency, offline, tail),
            destroyOwnerOnMessageThread);
        auto adapter = std::make_unique<ExternalModuleProcessor>(owner, description.name);
        owners.push_back(std::move(owner));
        return adapter;
    } catch (...) {
        error = "The VST3 threw an exception while loading, restoring, or preparing. The previous insert was left unchanged.";
        return {};
    }
}

bool ExternalPluginHost::capture(ModuleProcessor& processor, ExternalPluginState& destination,
                                 juce::String& error) {
    error.clear();
    if (!requireMessageThread(error)) return false;
    auto* adapter = dynamic_cast<ExternalModuleProcessor*>(&processor);
    if (!adapter) {error = "The selected processor is not a hosted VST3."; return false;}
    if (!adapter->owner->captureGated.load(std::memory_order_acquire)) {
        error = "Gate VST3 processing and wait for audio/worker quiescence before capturing state."; return false;
    }
    ExternalPluginState captured;
    captured.descriptionXml = adapter->owner->descriptionXml;
    try {adapter->owner->instance->getStateInformation(captured.state);}
    catch (...) {error = "The VST3 threw an exception while capturing state."; return false;}
    if (!captured.validate(error)) return false;
    destination = std::move(captured);
    return true;
}

void ExternalPluginHost::collectGarbage() {
    jassert(onMessageThread());
    if (!onMessageThread()) return;
    owners.erase(std::remove_if(owners.begin(), owners.end(), [] (const auto& owner) {
        return owner.use_count() == 1;
    }), owners.end());
}
} // namespace hem

```

## Source/Plugins/ExternalPluginHost.h

```cpp
#pragma once
#include "Processors.h"
#include "ExternalPluginState.h"

namespace hem {
class ExternalInstanceOwner;

// Explicit, in-process, selected-file VST3 hosting. No directory scanning,
// downloads, background instantiation, or crash-isolation claim is made.
// All public operations and destruction MUST run on the JUCE message thread.
// Keep this service alive until workers have joined, editors have closed, and
// all chain adapters are destroyed; then collectGarbage() on that thread.
class ExternalPluginHost final {
public:
    ExternalPluginHost();
    ~ExternalPluginHost();

    juce::Array<juce::PluginDescription> discoverSelected(const juce::File&, juce::String& error);
    std::unique_ptr<ModuleProcessor> stage(const ExternalPluginState&, double sampleRate,
                                         int maximumBlock, int latencyBudget, bool nonRealtime,
                                         juce::String& error);
    // The caller must first gate the adapter and establish audio/worker
    // quiescence. A gate alone does not wait for an already executing block.
    static bool capture(ModuleProcessor&, ExternalPluginState&, juce::String& error);
    void collectGarbage();
    size_t retainedInstanceCount() const noexcept {return owners.size();}

private:
    juce::AudioPluginFormatManager formats;
    std::vector<std::shared_ptr<ExternalInstanceOwner>> owners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalPluginHost)
};
} // namespace hem

```

## Source/Plugins/ExternalPluginState.h

```cpp
#pragma once
#include <JuceHeader.h>

namespace hem {
// A bounded descriptor and the VST3's own opaque state, never the empty HEM
// adapter APVTS. An empty opaque block is valid for a newly selected plug-in.
struct ExternalPluginState {
    static constexpr size_t maxDescriptionBytes = 64 * 1024;
    static constexpr size_t maxStateBytes = 16 * 1024 * 1024;
    juce::String descriptionXml;
    juce::MemoryBlock state;

    bool getDescription(juce::PluginDescription& description, juce::String& error) const {
        error.clear();
        if (descriptionXml.isEmpty() || descriptionXml.getNumBytesAsUTF8() > maxDescriptionBytes) {
            error = "The VST3 descriptor is empty or exceeds 64 KiB."; return false;
        }
        if (state.getSize() > maxStateBytes) {
            error = "The VST3 state exceeds the 16 MiB per-plug-in limit."; return false;
        }
        // No document types, entity declarations, or external XML resources.
        if (descriptionXml.containsIgnoreCase("<!DOCTYPE") || descriptionXml.containsIgnoreCase("<!ENTITY")) {
            error = "The VST3 descriptor contains unsupported XML declarations."; return false;
        }
        int markupStarts = 0;
        for (auto character : descriptionXml)
            if (character == '<' && ++markupStarts > 3) {
                // A descriptor has only a root, optional closing root, and an
                // optional XML declaration. Reject deep input before parsing.
                error = "The VST3 descriptor contains unsupported nested XML."; return false;
            }
        const auto xml = juce::XmlDocument::parse(descriptionXml);
        if (!xml || !xml->hasTagName("PLUGIN") || xml->getNumChildElements() != 0
            || xml->getNumAttributes() > 32 || !xml->hasAttribute("uniqueId")) {
            error = "The VST3 descriptor is malformed."; return false;
        }
        // JUCE's descriptor reader intentionally uses permissive integer
        // conversions. Persistence is stricter: never turn corrupted IDs or
        // channel counts into a different valid-looking plug-in descriptor.
        const auto validDigits = [] (const juce::String& value, const char* digits, int maxLength) {
            return value.isNotEmpty() && value.length() <= maxLength
                && value.containsOnly(digits);
        };
        for (const auto* attribute : {"uniqueId", "uid", "fileTime", "infoUpdateTime"}) {
            if (!xml->hasAttribute(attribute) && juce::String(attribute) != "uniqueId") continue;
            const int maximum = juce::String(attribute).endsWith("Time") ? 16 : 8;
            if (!validDigits(xml->getStringAttribute(attribute), "0123456789abcdefABCDEF", maximum)) {
                error = "The VST3 descriptor has an invalid hexadecimal identifier or timestamp."; return false;
            }
        }
        for (const auto* attribute : {"numInputs", "numOutputs"}) {
            const auto text = xml->getStringAttribute(attribute);
            if (!validDigits(text, "0123456789", 4) || text.getIntValue() > 4096) {
                error = "The VST3 descriptor has invalid channel counts."; return false;
            }
        }
        for (const auto* attribute : {"isInstrument", "isShell", "hasARAExtension"}) {
            if (!xml->hasAttribute(attribute) && juce::String(attribute) != "isInstrument") continue;
            const auto text = xml->getStringAttribute(attribute);
            if (text != "0" && text != "1") {
                error = "The VST3 descriptor has an invalid boolean flag."; return false;
            }
        }
        if (!description.loadFromXml(*xml)) {
            error = "The VST3 descriptor is malformed."; return false;
        }
        if (description.pluginFormatName != "VST3") {
            error = "Only VST3 effect descriptors are supported."; return false;
        }
        if (description.name.isEmpty() || description.name.length() > 4096
            || description.fileOrIdentifier.length() > 4096
            || !juce::File::isAbsolutePath(description.fileOrIdentifier)
            || !description.fileOrIdentifier.endsWithIgnoreCase(".vst3")) {
            error = "The VST3 descriptor needs a name and an absolute .vst3 file or bundle path."; return false;
        }
        if (description.isInstrument) {
            error = "VST3 instruments are unsupported; select a stereo audio effect."; return false;
        }
        if (description.numInputChannels < 0 || description.numOutputChannels < 0
            || description.numInputChannels > 4096 || description.numOutputChannels > 4096) {
            error = "The VST3 descriptor has invalid channel counts."; return false;
        }
        return true;
    }

    bool validate(juce::String& error) const {
        juce::PluginDescription description;
        return getDescription(description, error);
    }
};
} // namespace hem

```

## Source/Plugins/FinishingEditors.h

```cpp
#pragma once
#include "Editors.h"
#include "FinishingProcessors.h"
namespace hem {
class VocalEditor final : public ModuleEditor {
public: explicit VocalEditor(VocalProcessor& p) : ModuleEditor(p) {}
};
class TubeEditor final : public ModuleEditor {
public: explicit TubeEditor(TubeProcessor& p) : ModuleEditor(p) {}
};
class TapeEditor final : public ModuleEditor {
public: explicit TapeEditor(TapeProcessor& p) : ModuleEditor(p) {}
};
class MonitorEditor final : public ModuleEditor {
public: explicit MonitorEditor(MonitorProcessor& p) : ModuleEditor(p) {}
};
}

```

## Source/Plugins/FinishingProcessors.cpp

```cpp
#include "FinishingProcessors.h"
#include "FinishingEditors.h"
namespace hem {
VocalProcessor::VocalProcessor() : ModuleProcessor("VPR_ARCHITECT","Vocal Presence & Resonance Architect",{
    {"cutoff","LOW CUT",20.0f,300.0f,80.0f,1.0f,"Hz"},
    {"body","BODY / 150 Hz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"mud","MUD / 350 Hz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"presence","PRESENCE / 2.5 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"brightness","BRIGHTNESS / 5 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"air","AIR / 12 kHz",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"resonance","RESONANCE TARGET",1000.0f,10000.0f,2800.0f,1.0f,"Hz"},
    {"q","RESONANCE Q",0.5f,8.0f,2.0f,0.01f,"Q"},
    {"threshold","DYNAMIC THRESHOLD",-60.0f,0.0f,0.0f,0.1f,"dB"},
    {"range","MAX RESONANCE CUT",0.0f,18.0f,0.0f,0.1f,"dB"},
    {"attack","ATTACK",0.1f,30.0f,5.0f,0.1f,"ms"},
    {"release","RELEASE",10.0f,500.0f,120.0f,1.0f,"ms"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"cutEnabled","LOW CUT ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void VocalProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(0); publishMeterPreparation(); }
void VocalProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    VocalSettings p;
    p.cutoff=value(0);
    p.body=value(1);
    p.mud=value(2);
    p.presence=value(3);
    p.brightness=value(4);
    p.air=value(5);
    p.resonance=value(6);
    p.q=value(7);
    p.threshold=value(8);
    p.range=value(9);
    p.attack=value(10);
    p.release=value(11);
    p.output=value(12);
    p.cutEnabled=value(13)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* VocalProcessor::createEditor() { return new VocalEditor(*this); }
TubeProcessor::TubeProcessor() : ModuleProcessor("TRIODE_TUBE","Triode Tube Saturator / 4x",{
    {"drive","DRIVE",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"bias","ASYMMETRY",-50.0f,50.0f,0.0f,0.1f,"%"},
    {"tone","TONE / 6 kHz",-6.0f,6.0f,0.0f,0.1f,"dB"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
}) {}
void TubeProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(core.latency()); publishMeterPreparation(); }
void TubeProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TubeSettings p;
    p.drive=value(0);
    p.bias=value(1);
    p.tone=value(2);
    p.mix=value(3);
    p.output=value(4);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TubeProcessor::createEditor() { return new TubeEditor(*this); }
TapeProcessor::TapeProcessor() : ModuleProcessor("TAPE","Tape Saturator / 4x",{
    {"drive","DRIVE",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"warmth","HEAD BUMP / 80 Hz",-6.0f,6.0f,0.0f,0.1f,"dB"},
    {"rolloff","HF LOSS / 10 kHz",-12.0f,0.0f,0.0f,0.1f,"dB"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
}) {}
void TapeProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(core.latency()); publishMeterPreparation(); }
void TapeProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TapeSettings p;
    p.drive=value(0);
    p.warmth=value(1);
    p.rolloff=value(2);
    p.mix=value(3);
    p.output=value(4);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TapeProcessor::createEditor() { return new TapeEditor(*this); }
MonitorProcessor::MonitorProcessor() : ModuleProcessor("MONITOR_EQ","Audio Output / Optional Monitor EQ",{
    {"bass","BASS / 80 Hz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"body","BODY / 250 Hz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"presence","PRESENCE / 3 kHz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"air","AIR / 10 kHz",-12.0f,6.0f,0.0f,0.1f,"dB"},
    {"preamp","MONITOR HEADROOM",-24.0f,0.0f,0.0f,0.1f,"dB"},
    {"enabled","MONITOR EQ ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void MonitorProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); core.prepare(rate); setLatencySamples(0); publishMeterPreparation(); }
void MonitorProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    MonitorSettings p;
    p.bass=value(0);
    p.body=value(1);
    p.presence=value(2);
    p.air=value(3);
    p.preamp=value(4);
    p.enabled=value(5)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* MonitorProcessor::createEditor() { return new MonitorEditor(*this); }
}

```

## Source/Plugins/FinishingProcessors.h

```cpp
#pragma once
#include "Processors.h"
#include "../DSP/FinishingCore.h"
namespace hem {
class VocalProcessor final : public ModuleProcessor {
public:
    VocalProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: VocalCore core;
};
class TubeProcessor final : public ModuleProcessor {
public:
    TubeProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: TubeCore core;
};
class TapeProcessor final : public ModuleProcessor {
public:
    TapeProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: TapeCore core;
};
class MonitorProcessor final : public ModuleProcessor {
public:
    MonitorProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
    bool monitorOnly() const noexcept override {return true;}
private: MonitorCore core;
};
}

```

## Source/Plugins/PluginMeterReadout.h

```cpp
#pragma once
#include "Processors.h"
#include <limits>

namespace hem::ui {
// Display calibration only. DSP values and parameter contracts are untouched.
struct PluginMeterReadout {
    PluginMeterSnapshot snapshot;
    float minimum=-60,maximum=0,value=0;
    std::array<float,6> ticks{{-60,-48,-24,-12,-6,0}};
    int tickCount=6;
    bool signedScale=false,available=false;
    juce::String heading,valueText,detail;
    float position(float v) const noexcept {
        if(std::isnan(v))return 0;
        return juce::jlimit(0.f,1.f,(v-minimum)/(maximum-minimum));
    }
    float zeroPosition() const noexcept {return signedScale?position(0):0;}
    float fillStart() const noexcept {return available?std::min(zeroPosition(),position(value)):zeroPosition();}
    float fillEnd() const noexcept {return available?std::max(zeroPosition(),position(value)):zeroPosition();}
};
inline PluginMeterReadout pluginMeterReadout(const ModuleProcessor& processor) {
    PluginMeterReadout result;
    result.snapshot=processor.meterSnapshot();
    const auto& snapshot=result.snapshot;
    const auto kind=processor.meterKind();
    result.heading=processor.meterLabel();
    juce::String statistic="LAST BLOCK MAX";
    if(kind==PluginMeterKind::correlation){
        result.minimum=-1;result.maximum=1;result.signedScale=true;
        result.ticks={{-1,-.5f,0,.5f,1,0}};result.tickCount=5;
        result.heading="OUTPUT L/R CORRELATION / -1 TO +1";
        statistic="LAST BLOCK CORRELATION";
    }else if(kind==PluginMeterKind::envelopeGain){
        result.minimum=-12;result.maximum=12;result.signedScale=true;
        result.ticks={{-12,-6,0,6,12,0}};result.tickCount=5;
        result.heading="ENVELOPE GAIN / PRE-MIX + PRE-OUTPUT / dB";
        statistic="LAST BLOCK SIGNED ABS-MAX";
    }else if(kind==PluginMeterKind::gainReduction){
        result.minimum=0;result.maximum=24;
        result.ticks={{0,6,12,18,24,0}};result.tickCount=5;
        if(dynamic_cast<const VcaProcessor*>(&processor))
            result.heading="DETECTOR GAIN REDUCTION / PRE-MIX + PRE-MAKEUP / dB";
        else result.heading += " / WET-SCALED DETECTOR";
    }else if(kind==PluginMeterKind::firTruePeak){
        result.heading="OUTPUT PEAK ESTIMATE / 8x, 32-TAP WINDOWED-SINC FIR / dBTP";
    }else result.heading="OUTPUT SAMPLE PEAK / dBFS";

    const double rate=snapshot.sampleRate;
    const auto window=juce::String(snapshot.blockSamples)+" samples / "
        +juce::String(rate>0?1000.0*double(snapshot.blockSamples)/rate:0,2)+" ms";
    result.detail=statistic+" / "+window+" / NO DISPLAY HOLD";
    if(kind==PluginMeterKind::firTruePeak){
        const int latency=snapshot.latencySamples;
        result.detail=snapshot.isPrepared?"REPORTED LATENCY: "+juce::String(latency)+" samples / "
            +juce::String(rate>0?1000.0*double(latency)/rate:0,3)+" ms / "+statistic+" / "+window
            :"REPORTED LATENCY UNAVAILABLE / NOT PREPARED";
    }
    if(!snapshot.hasProcessedAudio){
        result.valueText="NO PROCESSED AUDIO";
        if(kind!=PluginMeterKind::firTruePeak)result.detail="WAITING FOR A NONEMPTY AUDIO BLOCK";
        return result;
    }
    if(!snapshot.valueAvailable){
        result.valueText=kind==PluginMeterKind::correlation
            ?"UNDEFINED / SILENCE OR INSUFFICIENT STEREO ENERGY":"UNAVAILABLE / INVALID MEASUREMENT";
        return result;
    }
    result.available=true;result.value=snapshot.value;
    if(kind==PluginMeterKind::samplePeak||kind==PluginMeterKind::firTruePeak){
        const auto units=kind==PluginMeterKind::firTruePeak?" dBTP EST.":" dBFS";
        result.value=snapshot.value>0?20.f*std::log10(snapshot.value):-std::numeric_limits<float>::infinity();
        result.valueText=snapshot.value>0?juce::String(result.value,2)+units:juce::String("-inf")+units+" / SILENCE";
        if(kind==PluginMeterKind::firTruePeak)result.valueText+=" / GR "+juce::String(snapshot.auxiliary,2)+" dB";
    }else{
        result.valueText=(result.signedScale&&result.value>0?"+":"")+juce::String(result.value,kind==PluginMeterKind::correlation?3:2);
        if(kind!=PluginMeterKind::correlation)result.valueText+=" dB";
        if(!snapshot.signalPresent)result.valueText+=" / SILENT OUTPUT";
    }
    if(std::isfinite(result.value)&&(result.value<result.minimum||result.value>result.maximum))
        result.valueText+=result.value<result.minimum?" / BELOW SCALE":" / ABOVE SCALE";
    return result;
}
inline juce::Rectangle<float> pluginMeterTrack(juce::Rectangle<float> area) noexcept {
    return {area.getX()+18,area.getY()+38,area.getWidth()-36,12};
}
inline constexpr juce::uint32 pluginMeterPositiveColour=0xff00cddd;
inline constexpr juce::uint32 pluginMeterNegativeColour=0xffffb36b;
inline constexpr juce::uint32 pluginMeterZeroColour=0xffd9e5ea;
inline constexpr juce::uint32 pluginMeterTrackColour=0xff061f2b;
inline void paintPluginMeter(juce::Graphics& g,juce::Rectangle<float> area,const PluginMeterReadout& readout) {
    g.setColour(juce::Colour(0xff021018));g.fillRect(area);
    g.setColour(juce::Colour(0xffa9bac4));g.setFont(11.5f);
    g.drawText(readout.heading,area.withHeight(15).toNearestInt(),juce::Justification::centred);
    g.setColour(juce::Colours::white);g.setFont(14.f);
    g.drawText(readout.valueText,area.withY(area.getY()+16).withHeight(18).toNearestInt(),juce::Justification::centred);
    const auto track=pluginMeterTrack(area);
    g.setColour(juce::Colour(pluginMeterTrackColour));g.fillRect(track);
    if(readout.available&&readout.fillEnd()>readout.fillStart()){
        g.setColour(juce::Colour(readout.signedScale&&readout.value<0?pluginMeterNegativeColour:pluginMeterPositiveColour));
        g.fillRect(juce::Rectangle<float>(track.getX()+track.getWidth()*readout.fillStart(),track.getY(),
            track.getWidth()*(readout.fillEnd()-readout.fillStart()),track.getHeight()));
    }
    // The centre line is a calibration mark, not a measurement or level bar.
    if(readout.signedScale){g.setColour(juce::Colour(pluginMeterZeroColour));
        const float x=track.getX()+track.getWidth()*readout.zeroPosition();g.drawVerticalLine(int(std::round(x)),track.getY()-2,track.getBottom()+2);}
    g.setFont(10.5f);g.setColour(juce::Colour(0xffa9bac4));
    for(int i=0;i<readout.tickCount;++i){
        const float tick=readout.ticks[size_t(i)],x=track.getX()+track.getWidth()*readout.position(tick);
        g.drawVerticalLine(int(std::round(x)),track.getBottom(),track.getBottom()+3);
        const auto label=(tick>0&&readout.signedScale?"+":"")+juce::String(tick,std::floor(tick)==tick?0:1);
        g.drawText(label,juce::Rectangle<float>(x-18,area.getY()+52,36,12).toNearestInt(),juce::Justification::centred);
    }
    g.setFont(10.f);g.drawText(readout.detail,area.withY(area.getY()+65).withHeight(12).toNearestInt(),juce::Justification::centred);
}
} // namespace hem::ui

```

## Source/Plugins/PluginRegistry.h

```cpp
#pragma once
#include "SpecialtyProcessors.h"
#include "FinishingProcessors.h"
namespace hem {
// Stable string IDs are the persistence contract; menu indices are UI-only.
// Registration and construction are message-thread operations.
class PluginRegistry {
public:
    struct Entry {
        const char* id;
        const char* name;
        const char* category;
        std::unique_ptr<ModuleProcessor> (*create)();
        bool monitoringOnly=false;
    };
    static const std::array<Entry,12>& entries() {
        static const std::array<Entry,12> list{{
            {"sipa","S.I.P.A. / Stereo Image & Phase","Spatial",&make<SipaProcessor>},
            {"surgical-eq","Surgical Master EQ","EQ",&make<SurgicalEqProcessor>},
            {"vca","VCA Compressor","Dynamics",&make<VcaProcessor>},
            {"true-peak","True-Peak Limiter","Master",&make<TruePeakProcessor>},
            {"dhta","D.H.T.A. / De-Esser & Harshness","Dynamics",&make<DhtaProcessor>},
            {"transient","Transient Shaper & Attack Enhancer","Dynamics",&make<TransientProcessor>},
            {"sub-bass","808 Sub-Bass Manager","Bass",&make<SubBassProcessor>},
            {"spectral-restorer","Artifact & Spectral Restorer","Restoration",&make<RestorerProcessor>},
            {"vocal-presence","Vocal Presence & Resonance Architect","Vocal",&make<VocalProcessor>},
            {"triode-tube","Triode Tube Saturator","Saturation",&make<TubeProcessor>},
            {"tape","Tape Saturator","Saturation",&make<TapeProcessor>},
            {"monitor-eq","Optional Monitor EQ","Monitoring",&make<MonitorProcessor>,true}
        }};
        return list;
    }
    static std::unique_ptr<ModuleProcessor> create(const juce::String& id) {
        for(const auto& entry:entries()) if(id==entry.id) return entry.create();
        return {};
    }
    static juce::String idFor(const ModuleProcessor& processor) {
        if(processor.isExternal())return "external-vst3";
        static const std::array<const char*,12> types{{"SIPA","SURGICAL_EQ","VCA","TRUE_PEAK","DHTA","TRANSIENT","SUB_BASS","RESTORER","VPR_ARCHITECT","TRIODE_TUBE","TAPE","MONITOR_EQ"}};
        for(size_t i=0;i<types.size();++i) if(processor.state.state.hasType(types[i])) return entries()[i].id;
        return {};
    }
private:
    template<class T> static std::unique_ptr<ModuleProcessor> make() {return std::make_unique<T>();}
};
}

```

## Source/Plugins/Processors.cpp

```cpp
#include "Processors.h"
#include "Editors.h"
#include "PluginRegistry.h"
namespace hem {
juce::AudioProcessorValueTreeState::ParameterLayout ModuleProcessor::layout(const std::vector<ParameterSpec>& specs){
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    for(const auto& s:specs){const juce::ParameterID key(s.id,1);
        if(s.toggle)result.add(std::make_unique<juce::AudioParameterBool>(key,s.name,s.initial>.5f));
        else {juce::NormalisableRange<float> range(s.minimum,s.maximum,s.step);
            if(juce::String(s.unit)=="Hz"&&s.maximum/s.minimum>20)range.setSkewForCentre(std::sqrt(s.minimum*s.maximum));
            result.add(std::make_unique<juce::AudioParameterFloat>(key,s.name,range,s.initial,
                juce::AudioParameterFloatAttributes().withLabel(s.unit)));}}
    return result;
}
ModuleProcessor::ModuleProcessor(juce::String identifier,juce::String name,std::vector<ParameterSpec> s,PluginMeterKind meterType)
 :AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),
 state(*this,nullptr,juce::Identifier(identifier),layout(s)),id(std::move(identifier)),title(std::move(name)),specs(std::move(s)),kind(meterType) {
    for(const auto& p:specs)parameters.push_back(state.getRawParameterValue(p.id));
}
void ModuleProcessor::resetMeterTelemetry() noexcept {
    meterSequence.fetch_add(1);
    meterFlags.store(0);meterValue.store(0);meterAuxiliary.store(0);
    meterSamples.store(0);meterBlocks.store(0);meterRate.store(0);meterLatency.store(0);
    meterSequence.fetch_add(1);
}
void ModuleProcessor::publishMeterPreparation() noexcept {
    // Preparation and processing share one serialized writer. Read JUCE's
    // non-atomic latency only here, after setLatencySamples/core.prepare, never
    // on the GUI thread. Rate and latency join the same coherent meter snapshot.
    meterSequence.fetch_add(1);
    meterRate.store(actualRate.load(std::memory_order_relaxed));
    meterLatency.store(getLatencySamples());meterFlags.store(8u);
    meterSequence.fetch_add(1);
}
void ModuleProcessor::publishMeterBlock(const juce::AudioBuffer<float>& buffer,float auxiliary) noexcept {
    const int n=buffer.getNumSamples();
    if(n<=0 || buffer.getNumChannels()<2)return;
    const float measured=meter();
    bool valid=std::isfinite(measured)&&std::isfinite(auxiliary),active=false;
    if(kind==PluginMeterKind::correlation){
        // Match the EXISTING StereoCore denominator guard exactly. Its zero
        // fallback means undefined at silence/one silent channel, not measured
        // decorrelation. This extra scan is bounded and only used by S.I.P.A.
        double ll=0,rr=0;const auto* l=buffer.getReadPointer(0);const auto* r=buffer.getReadPointer(1);
        for(int i=0;i<n;++i){ll+=l[i]*l[i];rr+=r[i]*r[i];}
        active=ll>0 || rr>0;
        valid=valid&&std::isfinite(ll)&&std::isfinite(rr)&&ll*rr>1.e-20;
    }else if(kind==PluginMeterKind::samplePeak || kind==PluginMeterKind::firTruePeak){
        // Reuse the core's real detector; do not rescan six peak processors.
        valid=valid&&measured>=0;active=valid&&measured>0;
    }else{
        // Dynamics values may remain nonzero during release into silence.
        // An early-exit output scan distinguishes that from signal activity.
        const auto* l=buffer.getReadPointer(0);const auto* r=buffer.getReadPointer(1);
        for(int i=0;i<n&&!active;++i)active=(std::isfinite(l[i])&&l[i]!=0)||(std::isfinite(r[i])&&r[i]!=0);
    }
    meterSequence.fetch_add(1);
    meterValue.store(measured);meterAuxiliary.store(auxiliary);
    meterFlags.store((meterFlags.load()&8u)|1u|(active?2u:0u)|(valid?4u:0u));
    meterSamples.store(uint32_t(n));
    const auto blocks=meterBlocks.load();meterBlocks.store(blocks==UINT32_MAX?blocks:blocks+1);
    meterSequence.fetch_add(1);
}
PluginMeterSnapshot ModuleProcessor::meterSnapshot() const noexcept {
    for(int attempt=0;attempt<3;++attempt){
        const auto before=meterSequence.load();if(before&1u)continue;
        const auto flags=meterFlags.load();
        PluginMeterSnapshot result;
        result.isPrepared=(flags&8u)!=0;result.hasProcessedAudio=(flags&1u)!=0;result.signalPresent=(flags&2u)!=0;result.valueAvailable=(flags&4u)!=0;
        result.value=meterValue.load();result.auxiliary=meterAuxiliary.load();
        result.blockSamples=meterSamples.load();result.processedBlocks=meterBlocks.load();
        result.sampleRate=meterRate.load();result.latencySamples=meterLatency.load();
        if(before==meterSequence.load())return result;
    }
    // Never spin waiting on the audio thread or show a torn measurement.
    return {};
}
bool ModuleProcessor::isBusesLayoutSupported(const BusesLayout& l) const {return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();}
void ModuleProcessor::getStateInformation(juce::MemoryBlock& block){auto xml=state.copyState().createXml();copyXmlToBinary(*xml,block);}
void ModuleProcessor::setStateInformation(const void* data,int bytes){if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(id))state.replaceState(juce::ValueTree::fromXml(*xml));}
SipaProcessor::SipaProcessor():ModuleProcessor("SIPA","S.I.P.A.  -  Stereo Image & Phase Architect",{
 {"width","WIDTH",0,200,100,.1f,"%"},{"bass","MONO BASS",60,200,120,1,"Hz"},{"balance","BALANCE",-100,100,0,.1f,"%"},{"output","OUTPUT",-60,6,0,.1f,"dB"},
 {"bassOn","MONO BASS ENABLE",0,1,0,1,"",true},{"mono","MONO",0,1,0,1,"",true},{"invertL","INVERT L",0,1,0,1,"",true},{"invertR","INVERT R",0,1,0,1,"",true},{"swap","SWAP L/R",0,1,0,1,"",true}},PluginMeterKind::correlation){}
void SipaProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void SipaProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;
 StereoParameters p;p.width=value(0);p.monoBass=value(1);p.balance=value(2);p.output=value(3);p.bassMono=value(4)>.5f;p.mono=value(5)>.5f;p.invertL=value(6)>.5f;p.invertR=value(7)>.5f;p.swap=value(8)>.5f;core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b);}
SurgicalEqProcessor::SurgicalEqProcessor():ModuleProcessor("SURGICAL_EQ","SURGICAL MASTER EQ",{
 {"hpOn","SUB HPF ENABLE",0,1,0,1,"",true},{"hp","SUB CUTOFF",20,80,30,.1f,"Hz"},{"slope","HP CASCADES (12 dB EACH)",1,4,1,1,""},
 {"mudFreq","MUD FREQ",150,600,300,.1f,"Hz"},{"mudGain","MUD GAIN",-12,6,0,.1f,"dB"},{"mudQ","MUD Q",.5f,10,1,.01f,"Q"},
 {"midFreq","CORE FREQ",600,3500,1500,1,"Hz"},{"midGain","CORE GAIN",-9,9,0,.1f,"dB"},{"midQ","CORE Q",.5f,6,1,.01f,"Q"},
 {"harshFreq","HARSHNESS FREQ",4000,8500,6500,1,"Hz"},{"harshGain","HARSHNESS GAIN",-12,3,0,.1f,"dB"},{"harshQ","HARSHNESS Q",1,8,2,.01f,"Q"},
 {"airFreq","AIR FREQ",10000,20000,12000,1,"Hz"},{"airGain","AIR GAIN",-6,8,0,.1f,"dB"},{"output","OUTPUT",-60,6,0,.1f,"dB"}}){}
EqParameters SurgicalEqProcessor::settings() const noexcept {EqParameters p;p.highPass=value(0)>.5f;p.cutoff=value(1);p.slope=int(value(2))-1;
 for(size_t i=0;i<3;++i){p.hz[i]=value(3+i*3);p.db[i]=value(4+i*3);p.q[i]=value(5+i*3);}p.hz[3]=value(12);p.db[3]=value(13);p.output=value(14);return p;}
void SurgicalEqProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void SurgicalEqProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),settings());publishMeterBlock(b);}
VcaProcessor::VcaProcessor():ModuleProcessor("VCA","VCA COMPRESSOR",{
 {"threshold","THRESHOLD",-40,0,0,.1f,"dB"},{"ratio","RATIO",1,10,1,.01f,":1"},{"attack","ATTACK",1,100,30,.1f,"ms"},{"release","RELEASE",20,1200,100,1,"ms"},
 {"knee","KNEE",0,20,0,.1f,"dB"},{"makeup","MAKEUP",-6,12,0,.1f,"dB"},{"mix","WET / DRY",0,100,100,.1f,"%"},{"scFreq","SC HPF",30,300,80,1,"Hz"},
 {"scOn","SC HPF ENABLE",0,1,0,1,"",true},{"autoRelease","AUTO RELEASE",0,1,0,1,"",true}},PluginMeterKind::gainReduction){}
void VcaProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);setLatencySamples(0);core.prepare(sr);publishMeterPreparation();}
void VcaProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;
 CompressorParameters p;p.threshold=value(0);p.ratio=value(1);p.attack=value(2);p.release=value(3);p.knee=value(4);p.makeup=value(5);p.mix=value(6);p.sidechain=value(7);p.scEnabled=value(8)>.5f;p.autoRelease=value(9)>.5f;core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b);}
TruePeakProcessor::TruePeakProcessor():ModuleProcessor("TRUE_PEAK","TRUE-PEAK LIMITER",{
 {"input","INPUT DRIVE",0,12,0,.1f,"dB"},{"ceiling","OUTPUT CEILING",-12,0,-1,.1f,"dBTP"},{"release","RELEASE",10,500,100,1,"ms"}},PluginMeterKind::firTruePeak){}
void TruePeakProcessor::prepareToPlay(double sr,int){resetMeterTelemetry();actualRate.store(sr);core.prepare(sr);setLatencySamples(core.latency());publishMeterPreparation();}
void TruePeakProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){juce::ScopedNoDenormals guard;if(b.getNumChannels()<2||b.getNumSamples()<=0)return;
 LimiterParameters p;p.input=value(0);p.ceiling=value(1);p.release=value(2);core.process(b.getWritePointer(0),b.getWritePointer(1),b.getNumSamples(),p);publishMeterBlock(b,reduction());}
juce::AudioProcessorEditor* SipaProcessor::createEditor(){return new SipaEditor(*this);}
juce::AudioProcessorEditor* SurgicalEqProcessor::createEditor(){return new SurgicalEqEditor(*this);}
juce::AudioProcessorEditor* VcaProcessor::createEditor(){return new VcaEditor(*this);}
juce::AudioProcessorEditor* TruePeakProcessor::createEditor(){return new TruePeakEditor(*this);}
std::unique_ptr<ModuleProcessor> createModule(int index) {
    const auto& entries=PluginRegistry::entries();
    return index>=0 && index<int(entries.size()) ? entries[size_t(index)].create() : nullptr;
}
} // namespace hem

```

## Source/Plugins/Processors.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "../DSP/DspCore.h"
#include <cstdint>

namespace hem {
struct ParameterSpec {
    const char* id; const char* name;float minimum,maximum,initial,step;
    const char* unit;bool toggle=false;
};
// Raw detector values are amplitude for peaks, dB for dynamics, and -1..+1
// for correlation. A zero-initialized detector is NOT evidence of audio.
enum class PluginMeterKind { samplePeak, correlation, gainReduction, envelopeGain, firTruePeak };
struct PluginMeterSnapshot {
    bool isPrepared=false, hasProcessedAudio=false, signalPresent=false, valueAvailable=false;
    float value=0, auxiliary=0;
    uint32_t blockSamples=0, processedBlocks=0;
    double sampleRate=0;
    int latencySamples=0;
};
class ModuleProcessor : public juce::AudioProcessor {
public:
    ModuleProcessor(juce::String identifier,juce::String title,std::vector<ParameterSpec>,
                    PluginMeterKind=PluginMeterKind::samplePeak);
    const juce::String getName() const override {return title;}
    bool acceptsMidi() const override {return false;} bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;} double getTailLengthSeconds() const override {return .02;}
    int getNumPrograms() override {return 1;}int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override = 0;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void releaseResources() override {} // Buffers live until off-thread destruction.
    virtual bool monitorOnly() const noexcept {return false;}
    // External adapters keep their immutable preparation contract separate from
    // temporary capture gating. These hooks never touch a third-party instance.
    virtual bool isExternal() const noexcept {return false;}
    virtual bool processingReady() const noexcept {return true;}
    virtual bool gateForStateCapture(bool) noexcept {return false;}
    virtual uint64_t externalStateRevision() const noexcept {return 0;}
    virtual juce::String externalDescriptionXml() const {return {};}
    virtual juce::String externalDiagnostic() const {return {};}
    virtual float meter() const noexcept=0;
    virtual juce::String meterLabel() const=0;
    PluginMeterKind meterKind() const noexcept {return kind;}
    PluginMeterSnapshot meterSnapshot() const noexcept;
    const std::vector<ParameterSpec>& parameterSpecs() const {return specs;}
    juce::AudioProcessorValueTreeState state;
    std::atomic<double> actualRate{96000};
protected:
    // Single audio writer: prepare/process must be serialized by the host.
    // No allocation, lock, GUI clock, or test-signal source in these calls.
    void resetMeterTelemetry() noexcept;
    void publishMeterPreparation() noexcept;
    void publishMeterBlock(const juce::AudioBuffer<float>&,float auxiliary=0) noexcept;
    float value(size_t i) const noexcept {return parameters[i]->load(std::memory_order_relaxed);}
private:
    juce::String id,title;std::vector<ParameterSpec> specs;
    const PluginMeterKind kind;
    // Atomic payload + bounded sequence read: a GUI snapshot cannot combine
    // new readiness with an old detector value. Seq-cst also orders the payload.
    std::atomic<uint32_t> meterSequence{0},meterFlags{0},meterSamples{0},meterBlocks{0};
    std::atomic<float> meterValue{0},meterAuxiliary{0};
    std::atomic<double> meterRate{0};
    std::atomic<int> meterLatency{0};
    static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free
                  && std::atomic<double>::is_always_lock_free && std::atomic<int>::is_always_lock_free);
    std::vector<std::atomic<float>*> parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(const std::vector<ParameterSpec>&);
};
class SipaProcessor final : public ModuleProcessor {
public:
    SipaProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.correlation.load();}
    juce::String meterLabel() const override {return "L/R CORRELATION";}
private:StereoCore core;
};
class SurgicalEqProcessor final : public ModuleProcessor {
public:
    SurgicalEqProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    EqParameters settings() const noexcept;
    float meter() const noexcept override {return core.peak.load();}
    juce::String meterLabel() const override {return "OUTPUT SAMPLE PEAK";}
private:EqCore core;
};
class VcaProcessor final : public ModuleProcessor {
public:
    VcaProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.reduction.load();}
    juce::String meterLabel() const override {return "GAIN REDUCTION / dB";}
private:CompressorCore core;
};
class TruePeakProcessor final : public ModuleProcessor {
public:
    TruePeakProcessor();void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override {return core.truePeak.load();}
    float reduction() const noexcept {return core.reduction.load();}
    juce::String meterLabel() const override {return "OUTPUT PEAK ESTIMATE / 8x FIR";}
private:LimiterCore core;
};
std::unique_ptr<ModuleProcessor> createModule(int index);
} // namespace hem

```

## Source/Plugins/SpecialtyEditors.h

```cpp
#pragma once
#include "Editors.h"
#include "SpecialtyProcessors.h"
namespace hem {
class DhtaEditor final : public ModuleEditor {
public: explicit DhtaEditor(DhtaProcessor& p) : ModuleEditor(p) {}
};
class TransientEditor final : public ModuleEditor {
public: explicit TransientEditor(TransientProcessor& p) : ModuleEditor(p) {}
};
class SubBassEditor final : public ModuleEditor {
public: explicit SubBassEditor(SubBassProcessor& p) : ModuleEditor(p) {}
};
class RestorerEditor final : public ModuleEditor {
public: explicit RestorerEditor(RestorerProcessor& p) : ModuleEditor(p) {}
};
}

```

## Source/Plugins/SpecialtyProcessors.cpp

```cpp
#include "SpecialtyProcessors.h"
#include "SpecialtyEditors.h"
namespace hem {
DhtaProcessor::DhtaProcessor() : ModuleProcessor("DHTA","D.H.T.A. / De-Esser & Harshness Tamer",{
    {"frequency","TARGET",1000.0f,16000.0f,6800.0f,1.0f,"Hz"},
    {"q","BANDWIDTH",0.5f,8.0f,2.0f,0.01f,"Q"},
    {"threshold","THRESHOLD",-60.0f,0.0f,0.0f,0.1f,"dB"},
    {"range","MAX CUT",0.0f,24.0f,0.0f,0.1f,"dB"},
    {"attack","ATTACK",0.1f,20.0f,2.0f,0.1f,"ms"},
    {"release","RELEASE",10.0f,500.0f,80.0f,1.0f,"ms"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"wide","WIDEBAND",0.0f,1.0f,0.0f,1.0f,"",true},
    {"delta","DELTA LISTEN",0.0f,1.0f,0.0f,1.0f,"",true},
},PluginMeterKind::gainReduction) {}
void DhtaProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void DhtaProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    DhtaSettings p;
    p.frequency=value(0);
    p.q=value(1);
    p.threshold=value(2);
    p.range=value(3);
    p.attack=value(4);
    p.release=value(5);
    p.mix=value(6);
    p.output=value(7);
    p.wide=value(8)>.5f;
    p.delta=value(9)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* DhtaProcessor::createEditor() { return new DhtaEditor(*this); }
TransientProcessor::TransientProcessor() : ModuleProcessor("TRANSIENT","Transient Shaper & Attack Enhancer",{
    {"attack","ATTACK AMOUNT",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"sustain","SUSTAIN AMOUNT",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"speed","ATTACK TIME",0.1f,20.0f,2.0f,0.1f,"ms"},
    {"recovery","RECOVERY",20.0f,500.0f,100.0f,1.0f,"ms"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
},PluginMeterKind::envelopeGain) {}
void TransientProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void TransientProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    TransientSettings p;
    p.attack=value(0);
    p.sustain=value(1);
    p.speed=value(2);
    p.recovery=value(3);
    p.mix=value(4);
    p.output=value(5);
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* TransientProcessor::createEditor() { return new TransientEditor(*this); }
SubBassProcessor::SubBassProcessor() : ModuleProcessor("SUB_BASS","Low-End 808 Sub-Bass Manager",{
    {"crossover","LOW BAND",40.0f,250.0f,100.0f,1.0f,"Hz"},
    {"lowGain","BASS LEVEL",-12.0f,12.0f,0.0f,0.1f,"dB"},
    {"subCut","SUB CUT",15.0f,50.0f,25.0f,0.1f,"Hz"},
    {"drive","LOW SATURATION",0.0f,12.0f,0.0f,0.1f,"dB"},
    {"mono","LOW MONO",0.0f,100.0f,0.0f,0.1f,"%"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"cutEnabled","SUB CUT ENABLE",0.0f,1.0f,0.0f,1.0f,"",true},
}) {}
void SubBassProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void SubBassProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    SubBassSettings p;
    p.crossover=value(0);
    p.lowGain=value(1);
    p.subCut=value(2);
    p.drive=value(3);
    p.mono=value(4);
    p.mix=value(5);
    p.output=value(6);
    p.cutEnabled=value(7)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* SubBassProcessor::createEditor() { return new SubBassEditor(*this); }
RestorerProcessor::RestorerProcessor() : ModuleProcessor("RESTORER","Artifact & Spectral Restorer / Multiband DSP",{
    {"amount","MAX REDUCTION",0.0f,18.0f,0.0f,0.1f,"dB"},
    {"floor","NOISE FLOOR",-90.0f,-20.0f,-60.0f,0.1f,"dBFS"},
    {"release","RELEASE",20.0f,500.0f,100.0f,1.0f,"ms"},
    {"protect","TRANSIENT PRESERVE",0.0f,100.0f,100.0f,0.1f,"%"},
    {"mix","MIX",0.0f,100.0f,100.0f,0.1f,"%"},
    {"output","OUTPUT",-24.0f,6.0f,0.0f,0.1f,"dB"},
    {"learn","LEARN NOISE ONLY",0.0f,1.0f,0.0f,1.0f,"",true},
},PluginMeterKind::gainReduction) {}
void RestorerProcessor::prepareToPlay(double rate,int) { resetMeterTelemetry(); actualRate.store(rate); setLatencySamples(0); core.prepare(rate); publishMeterPreparation(); }
void RestorerProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    if(buffer.getNumChannels()<2||buffer.getNumSamples()<=0) return;
    RestorerSettings p;
    p.amount=value(0);
    p.floor=value(1);
    p.release=value(2);
    p.protect=value(3);
    p.mix=value(4);
    p.output=value(5);
    p.learn=value(6)>.5f;
    core.process(buffer.getWritePointer(0),buffer.getWritePointer(1),buffer.getNumSamples(),p);
    publishMeterBlock(buffer);
}
juce::AudioProcessorEditor* RestorerProcessor::createEditor() { return new RestorerEditor(*this); }
}

```

## Source/Plugins/SpecialtyProcessors.h

```cpp
#pragma once
#include "Processors.h"
#include "../DSP/SpecialtyCore.h"
#include "../DSP/RestorerCore.h"
namespace hem {
class DhtaProcessor final : public ModuleProcessor {
public:
    DhtaProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.reduction.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "GAIN REDUCTION / dB"; }
private: DhtaCore core;
};
class TransientProcessor final : public ModuleProcessor {
public:
    TransientProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.gainChange.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "ENVELOPE GAIN / dB"; }
private: TransientCore core;
};
class SubBassProcessor final : public ModuleProcessor {
public:
    SubBassProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.peak.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "OUTPUT SAMPLE PEAK / dBFS"; }
private: SubBassCore core;
};
class RestorerProcessor final : public ModuleProcessor {
public:
    RestorerProcessor();
    void prepareToPlay(double,int) override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    float meter() const noexcept override { return core.reduction.load(std::memory_order_relaxed); }
    juce::String meterLabel() const override { return "MAX BAND REDUCTION / dB"; }
private: RestorerCore core;
};
}

```

## Source/SessionState.cpp

```cpp
#include "SessionState.h"
#include "Systems/PresetManager.h"
#include "Plugins/ExternalPluginHost.h"
#include <set>
#include <stdexcept>
namespace hem {
namespace {
[[noreturn]] void fail(const juce::String& text){throw std::runtime_error(text.toStdString());}
double number(const juce::ValueTree& t,const juce::Identifier& key,double low,double high){
 if(!t.hasProperty(key))fail("Missing property: "+key.toString());
 const auto text=t[key].toString().trim();
 // String::getDoubleValue silently accepts malformed input. Require complete
 // conversion, including no trailing junk, before using a session value.
 size_t used=0;double v=0;const auto str=text.toStdString();
 try{v=std::stod(str,&used);}catch(...){fail("Invalid number: "+key.toString());}
 if(used!=str.size()||!std::isfinite(v)||v<low||v>high)fail("Out-of-range property: "+key.toString());
 return v;
}
bool flag(const juce::ValueTree& t,const juce::Identifier& key){const auto n=number(t,key,0,1);if(n!=0&&n!=1)fail("Invalid boolean");return n==1;}
void require(bool b,const juce::String& text){if(!b)fail(text);}
void emptyParameters(const juce::ValueTree& parameters){
 require(parameters.hasType("PARAMETERS")&&parameters.getNumChildren()==0&&parameters.getNumProperties()==0,
         "External inserts must have an empty PARAMETERS element");
}
void validateExternal(const ExternalPluginState& external,bool requireFile){
 juce::String error;if(!external.validate(error))fail("Invalid external insert state: "+error);
 juce::PluginDescription description;if(!external.getDescription(description,error))fail("Invalid external insert description: "+error);
 if(requireFile)require(juce::File(description.fileOrIdentifier).exists(),"Missing VST3 plug-in: "+description.fileOrIdentifier);
}
ExternalPluginState decodeExternal(const juce::ValueTree& element){
 require(element.hasType("EXTERNAL")&&element.getNumChildren()==0&&element.getNumProperties()==3
         &&element.hasProperty("descriptionXml")&&element.hasProperty("stateBase64")&&element.hasProperty("stateBytes"),
         "Invalid EXTERNAL container");
 require(element["descriptionXml"].isString()&&element["stateBase64"].isString()&&!element["stateBytes"].isBool(),
         "Invalid EXTERNAL property types");
 ExternalPluginState result;result.descriptionXml=element["descriptionXml"].toString();
 require(result.descriptionXml.getNumBytesAsUTF8()<=ExternalPluginState::maxDescriptionBytes,"External description is too large");
 const auto bytes=number(element,"stateBytes",0,double(ExternalPluginState::maxStateBytes));
 require(bytes==std::floor(bytes),"External state byte count must be an integer");
 const auto encoded=element["stateBase64"].toString();
 const auto expected=((size_t(bytes)+2)/3)*4;
 require(encoded.getNumBytesAsUTF8()==expected&&expected<=SessionState::maxSerializedBytes,"Invalid external Base64 size");
 juce::MemoryOutputStream stream;
 require(juce::Base64::convertFromBase64(stream,encoded),"Malformed external Base64 state");
 require(stream.getDataSize()==size_t(bytes),"External state byte count mismatch");
 require(juce::Base64::toBase64(stream.getData(),stream.getDataSize())==encoded,"Non-canonical external Base64 state");
 result.state=stream.getMemoryBlock();validateExternal(result,true);return result;
}
void requireSerializedSize(const juce::ValueTree& root){
 auto xml=root.createXml();require(xml!=nullptr,"Cannot encode session XML");
 require(xml->toString().getNumBytesAsUTF8()<=SessionState::maxSerializedBytes,"Session is larger than 8 MB");
}
}
juce::ValueTree SessionState::captureParameters(ModuleProcessor& p){
 juce::ValueTree result("PARAMETERS");
 for(const auto& spec:p.parameterSpecs()){
    juce::ValueTree v("PARAM");v.setProperty("id",spec.id,nullptr);
    v.setProperty("value",double(p.state.getRawParameterValue(spec.id)->load()),nullptr);result.addChild(v,-1,nullptr);
 }
 return result;
}
void SessionState::restoreParameters(ModuleProcessor& p,const juce::ValueTree& values){
 require(values.hasType("PARAMETERS")&&values.getNumChildren()==int(p.parameterSpecs().size()),"Parameter schema mismatch: "+p.getName());
 std::set<juce::String> ids;
 // Validate every value BEFORE changing this processor.
 for(auto child:values){
    require(child.hasType("PARAM")&&child.getNumChildren()==0,"Invalid parameter element");
    auto id=child["id"].toString();require(ids.insert(id).second,"Duplicate parameter: "+id);
    auto it=std::find_if(p.parameterSpecs().begin(),p.parameterSpecs().end(),[&](const auto& s){return id==s.id;});
    require(it!=p.parameterSpecs().end(),"Unknown parameter: "+id);
    const auto n=number(child,"value",it->minimum,it->maximum);
    if(it->toggle)require(n==0||n==1,"Invalid toggle: "+id);
 }
 for(auto child:values){auto* parameter=p.state.getParameter(child["id"].toString());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(float(double(child["value"]))));}
}
SessionSnapshot SessionState::capture(const TrackBank& bank,float in,float out,double position,bool loop,ModuleProcessor& monitor,bool includeExternalState){
 SessionSnapshot s;s.inputDb=in;s.outputDb=out;s.positionSeconds=position;s.looping=loop;
 s.monitorParameters=captureParameters(monitor);
 for(size_t i=0;i<s.tracks.size();++i){const auto& t=bank.tracks[i];auto& st=s.tracks[i];
    st.source=t.loaded?t.playback->file:juce::File();st.name=t.name;st.gainDb=t.trimDb.load();st.muted=t.muted.load();st.solo=t.solo.load();
    for(int j=0;j<8;++j){require(!t.chain.isBusy(j),"Wait for insert transitions before saving");auto& slot=st.slots[size_t(j)];slot.bypassed=t.chain.isBypassed(j);
        if(auto* p=t.chain.activeProcessor(j)){slot.pluginId=PluginRegistry::idFor(*p);require(slot.pluginId.isNotEmpty(),"Unregistered processor");
            if(p->isExternal()){
                slot.pluginId=externalPluginId;slot.externalRevision=p->externalStateRevision();
                if(includeExternalState){juce::String error;if(!ExternalPluginHost::capture(*p,slot.external,error))fail("Cannot capture external insert: "+error);}
                else slot.external.descriptionXml=p->externalDescriptionXml();
            }else slot.parameters=captureParameters(*p);}}
 }
 return s;
}
bool SessionState::hasExternal(const SessionSnapshot& snapshot) noexcept {
 for(const auto& track:snapshot.tracks)for(const auto& slot:track.slots)if(slot.pluginId==externalPluginId)return true;
 return false;
}
juce::ValueTree SessionState::encode(const SessionSnapshot& s,const juce::File& file){
 juce::ValueTree root("HEM_SESSION");root.setProperty("version",hasExternal(s)?2:1,nullptr);root.setProperty("tracks",13,nullptr);root.setProperty("slotsPerTrack",8,nullptr);
 root.setProperty("inputDb",s.inputDb,nullptr);root.setProperty("outputDb",s.outputDb,nullptr);root.setProperty("positionSeconds",s.positionSeconds,nullptr);root.setProperty("looping",int(s.looping),nullptr);
 root.setProperty("masterCeilingDbTP",-1.0,nullptr);
 juce::ValueTree monitor("MONITOR");monitor.addChild(s.monitorParameters.createCopy(),-1,nullptr);root.addChild(monitor,-1,nullptr);
 size_t externalBytes=0;
 for(size_t i=0;i<s.tracks.size();++i){const auto& st=s.tracks[i];juce::ValueTree t("TRACK");t.setProperty("index",int(i),nullptr);t.setProperty("name",st.name,nullptr);
    t.setProperty("source",st.source==juce::File()?juce::String():st.source.getRelativePathFrom(file.getParentDirectory()),nullptr);
    t.setProperty("absoluteSource",st.source.getFullPathName(),nullptr);t.setProperty("gainDb",st.gainDb,nullptr);t.setProperty("muted",int(st.muted),nullptr);t.setProperty("solo",int(st.solo),nullptr);
    for(int j=0;j<8;++j){const auto& ss=st.slots[size_t(j)];juce::ValueTree slot("SLOT");slot.setProperty("index",j,nullptr);slot.setProperty("pluginId",ss.pluginId,nullptr);slot.setProperty("bypassed",int(ss.bypassed),nullptr);slot.addChild(ss.parameters.createCopy(),-1,nullptr);
        if(ss.pluginId==externalPluginId){
            emptyParameters(ss.parameters);validateExternal(ss.external,false);
            externalBytes+=((ss.external.state.getSize()+2)/3)*4+ss.external.descriptionXml.getNumBytesAsUTF8();
            require(externalBytes<=maxSerializedBytes,"Session external state is larger than 8 MB");
            juce::ValueTree external("EXTERNAL");external.setProperty("descriptionXml",ss.external.descriptionXml,nullptr);
            external.setProperty("stateBytes",juce::int64(ss.external.state.getSize()),nullptr);
            external.setProperty("stateBase64",juce::Base64::toBase64(ss.external.state.getData(),ss.external.state.getSize()),nullptr);
            slot.addChild(external,-1,nullptr);
        }else require(ss.external.descriptionXml.isEmpty()&&ss.external.state.getSize()==0,"Native or empty insert contains external state");
        t.addChild(slot,-1,nullptr);}
    root.addChild(t,-1,nullptr);
 }
 requireSerializedSize(root);return root;
}
SessionSnapshot SessionState::decode(const juce::ValueTree& root,const juce::File& file){
 require(root.hasType("HEM_SESSION"),"Not a HEM session");const auto version=number(root,"version",1,2);require(version==1||version==2,"Unsupported session version");
 requireSerializedSize(root);
 number(root,"tracks",13,13);number(root,"slotsPerTrack",8,8);number(root,"masterCeilingDbTP",-1,-1);
 require(root.getNumChildren()==14,"Session must contain 13 tracks and monitor settings");
 SessionSnapshot result;result.inputDb=float(number(root,"inputDb",-60,6));result.outputDb=float(number(root,"outputDb",-60,6));result.positionSeconds=number(root,"positionSeconds",0,1.e9);result.looping=flag(root,"looping");
 std::array<bool,13> seen{};bool monitorSeen=false;
 for(auto t:root){
    if(t.hasType("MONITOR")){require(!monitorSeen&&t.getNumChildren()==1,"Invalid monitor state");monitorSeen=true;result.monitorParameters=t.getChild(0).createCopy();continue;}
    require(t.hasType("TRACK"),"Unknown session element");const auto index=number(t,"index",0,12);require(index==int(index)&&!seen[size_t(index)],"Duplicate or invalid track index");seen[size_t(index)]=true;
    auto& st=result.tracks[size_t(index)];st.name=t["name"].toString();st.gainDb=float(number(t,"gainDb",-60,6));st.muted=flag(t,"muted");st.solo=flag(t,"solo");
    const auto relative=t["source"].toString();const auto absolute=t["absoluteSource"].toString();
    if(relative.isNotEmpty()||absolute.isNotEmpty()){
        if(relative.isNotEmpty())st.source=file.getParentDirectory().getChildFile(relative);
        if(!st.source.existsAsFile()&&juce::File::isAbsolutePath(absolute))st.source=juce::File(absolute);
        require(st.source.existsAsFile(),"Missing audio on track "+juce::String(int(index)+1)+": "+relative);
    }
    require(t.getNumChildren()==8,"Each track must have eight slots");std::array<bool,8> slots{};
    for(auto child:t){require(child.hasType("SLOT"),"Invalid insert element");auto si=number(child,"index",0,7);require(si==int(si)&&!slots[size_t(si)],"Duplicate or invalid slot index");slots[size_t(si)]=true;
        auto& ss=st.slots[size_t(si)];ss.pluginId=child["pluginId"].toString();ss.bypassed=flag(child,"bypassed");
        const auto external=ss.pluginId==externalPluginId;
        require(child.getNumChildren()==(external?2:1),"Invalid or mixed insert state");
        ss.parameters=child.getChildWithName("PARAMETERS").createCopy();
        require(ss.parameters.hasType("PARAMETERS"),"Missing parameters");
        if(external){require(version==2,"External insert requires session version 2");emptyParameters(ss.parameters);ss.external=decodeExternal(child.getChildWithName("EXTERNAL"));}
        else if(ss.pluginId.isEmpty())require(ss.parameters.getNumChildren()==0,"Empty slot has parameters");
        else {auto it=std::find_if(PluginRegistry::entries().begin(),PluginRegistry::entries().end(),[&](const auto& e){return ss.pluginId==e.id;});require(it!=PluginRegistry::entries().end()&&!it->monitoringOnly,"Unknown or prohibited insert: "+ss.pluginId);}
    }
 }
 require(monitorSeen&&std::all_of(seen.begin(),seen.end(),[](bool x){return x;}),"Incomplete session");return result;
}
void SessionState::save(const SessionSnapshot& state,const juce::File& file){
 auto xml=encode(state,file).createXml();require(xml!=nullptr,"Cannot encode session XML");const auto text=xml->toString();
 require(text.getNumBytesAsUTF8()<=maxSerializedBytes,"Session is larger than 8 MB");
 juce::TemporaryFile temporary(file);
 require(temporary.getFile().replaceWithText(text,false,false,nullptr),"Could not write session temporary file");
 require(temporary.getFile().getSize()==juce::int64(text.getNumBytesAsUTF8()),"Session temporary file size mismatch");
 require(temporary.overwriteTargetFileWithTemporary(),"Could not replace session file; check permissions");
}
SessionSnapshot SessionState::load(const juce::File& file){
 require(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=juce::int64(maxSerializedBytes),"Session missing, empty, or larger than 8 MB");
 const auto text=file.loadFileAsString();
 require(!text.containsIgnoreCase("<!DOCTYPE")&&!text.containsIgnoreCase("<!ENTITY"),"Session XML declarations are not allowed");
 auto xml=juce::XmlDocument::parse(text);require(xml!=nullptr,"Invalid session XML");return decode(juce::ValueTree::fromXml(*xml),file);
}
SessionWorker::SessionWorker(juce::TimeSliceThread& d,Commit c,Report r,Completion doneCallback,StageExternal externalCallback):juce::Thread("HEM session worker"),disk(d),commit(std::move(c)),report(std::move(r)),completion(std::move(doneCallback)),stageExternal(std::move(externalCallback)){startTimerHz(15);}
SessionWorker::~SessionWorker(){stopTimer();signalThreadShouldExit();waitForThreadToExit(-1);staged.reset();}
bool SessionWorker::save(SessionSnapshot s,const juce::File& f){
 if(isBusy())return false;snapshot=std::move(s);file=f;operation=Operation::Save;error.clear();done=false;busy=true;
 if(!startThread()){busy=false;report("Cannot start session worker");return false;}return true;
}
bool SessionWorker::load(const juce::File& f,Context c){
 if(isBusy())return false;file=f;context=c;operation=Operation::Load;error.clear();done=false;busy=true;
 if(!startThread()){busy=false;report("Cannot start session worker");return false;}return true;
}
void SessionWorker::run(){
 try{
    if(operation==Operation::Retire)staged.reset();
    else if(operation==Operation::Save)PresetManager::save(snapshot,file);
    else{
        if(context.sampleRate<=0||context.blockSize<=0)fail("Session processing context is not prepared");
        snapshot=PresetManager::load(file);
        // Validate monitor settings against their actual registered ranges.
        MonitorProcessor monitor;SessionState::restoreParameters(monitor,snapshot.monitorParameters);
        juce::AudioFormatManager formats;formats.registerBasicFormats();staged=std::make_unique<TrackBank>();
        for(size_t i=0;i<13;++i){if(threadShouldExit())fail("Session load cancelled");auto& track=staged->tracks[i];const auto& saved=snapshot.tracks[i];
            track.trimDb=saved.gainDb;track.muted=saved.muted;track.solo=saved.solo;track.name=saved.name;
            track.prepare(context.sampleRate,context.blockSize);
            if(saved.source!=juce::File()){
                track.playback->prepareFile(saved.source,formats,disk,context.sampleRate,context.blockSize,snapshot.looping);
                track.sourceRate=track.playback->sourceRate;track.loaded=true;
                track.transport().setPosition(std::min(snapshot.positionSeconds,track.transport().getLengthInSeconds()));
            }
            for(int j=0;j<8;++j){const auto& slot=saved.slots[size_t(j)];
                if(slot.pluginId==SessionState::externalPluginId)continue; // Message-thread staging only.
                auto p=PluginRegistry::create(slot.pluginId);
                if(p)SessionState::restoreParameters(*p,slot.parameters);
                auto node=track.chain.stageNode(std::move(p),context.sampleRate,context.blockSize,track.chain.slotLatency());
                track.chain.installInitial(j,std::move(node),slot.bypassed);
            }
        }
    }
 }catch(const std::exception& e){error=e.what();staged.reset();}
 done.store(true,std::memory_order_release);
}
void SessionWorker::timerCallback(){
 if(!done.load(std::memory_order_acquire)||isThreadRunning())return;
 done=false;
 if(operation==Operation::Retire){busy=false;return;}
 if(error.isNotEmpty()){report("Session unchanged: "+error);busy=false;if(completion)completion(operation==Operation::Save,file,juce::Result::fail(error));return;}
 if(operation==Operation::Save){report("Session saved: "+file.getFileName());busy=false;if(completion)completion(true,file,juce::Result::ok());return;}
 auto result=juce::Result::ok();
 try{
    if(SessionState::hasExternal(snapshot)){
        if(!stageExternal)result=juce::Result::fail("External VST3 staging is unavailable; session unchanged");
        else if(!staged)result=juce::Result::fail("No staged session bank; session unchanged");
        else {
            result=stageExternal(*staged,snapshot,context);
            if(result.wasOk())for(size_t i=0;i<snapshot.tracks.size();++i)for(size_t j=0;j<snapshot.tracks[i].slots.size();++j)
                if(snapshot.tracks[i].slots[j].pluginId==SessionState::externalPluginId){
                    const auto* processor=staged->tracks[i].chain.activeProcessor(int(j));
                    require(processor&&processor->isExternal()&&processor->processingReady(),"External insert was not staged; session unchanged");
                }
        }
    }
    if(result.wasOk())result=commit(staged,snapshot,context);
 }catch(const std::exception& e){result=juce::Result::fail("Session unchanged: "+juce::String(e.what()));}
 catch(...){result=juce::Result::fail("Session unchanged: external staging or commit threw an exception");}
 report(result.wasOk()?"Session recalled / transport paused / monitor EQ disabled":result.getErrorMessage());
 if(completion)completion(false,file,result);
 // On success, staged owns the OLD bank. On failure, it owns the rejected bank.
 // External adapters may retire here; ExternalPluginHost retains actual plug-in
 // instances until main-thread collection. Never call back into main from run().
 operation=Operation::Retire;
 if(!startThread()){staged.reset();busy=false;report("Session cleanup completed on message thread");}
}
}

```

## Source/SessionState.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "TrackEngine.h"
#include "Plugins/PluginRegistry.h"
#include "Plugins/ExternalPluginState.h"

namespace hem {
// The entire bank has one owner. A staged bank can replace the live bank with
// one unique_ptr swap under the device callback lock, without deleting audio
// sources or allocating memory in that lock or on the audio thread.
struct TrackBank { std::array<TrackEngine,13> tracks; };
struct SlotSnapshot {
    juce::String pluginId;
    bool bypassed=false;
    juce::ValueTree parameters{"PARAMETERS"};
    ExternalPluginState external;
    // Dirty-state comparison only. Never persisted in a session or preset.
    uint64_t externalRevision=0;
};
struct TrackSnapshot {
    juce::File source;
    juce::String name{"EMPTY TRACK"};
    float gainDb=0;
    bool muted=false,solo=false;
    std::array<SlotSnapshot,8> slots;
};
struct SessionSnapshot {
    std::array<TrackSnapshot,13> tracks;
    float inputDb=0,outputDb=0;
    double positionSeconds=0;
    bool looping=false;
    juce::ValueTree monitorParameters{"PARAMETERS"};
};
class SessionState final {
public:
    static constexpr size_t maxSerializedBytes=8*1024*1024;
    static constexpr const char* externalPluginId="external-vst3";
    // Capture on the message thread, while insert publication is stable.
    // No XML, file access, APVTS copying, or processor construction belongs in
    // getNextAudioBlock(). Parameter reads here use APVTS atomic values.
    // For external opaque capture the caller must also gate external processing
    // and establish quiescence. false reads descriptor/revision metadata only.
    static SessionSnapshot capture(const TrackBank&,float inputDb,float outputDb,
                                   double position,bool looping,ModuleProcessor& monitor,
                                   bool includeExternalState=true);
    static juce::ValueTree encode(const SessionSnapshot&,const juce::File& sessionFile);
    static SessionSnapshot decode(const juce::ValueTree&,const juce::File& sessionFile);
    static void save(const SessionSnapshot&,const juce::File&);
    static SessionSnapshot load(const juce::File&);
    static bool hasExternal(const SessionSnapshot&) noexcept;
    static juce::ValueTree captureParameters(ModuleProcessor&);
    // Reject missing, duplicate, unknown, non-finite and out-of-range values.
    static void restoreParameters(ModuleProcessor&,const juce::ValueTree&);
};

// Single-operation background worker. Public methods and completion callbacks
// run on the message thread. busy stays true until off-thread retirement ends.
// Destructor joins the worker before the shared read-ahead thread is stopped.
class SessionWorker final : private juce::Thread,private juce::Timer {
public:
    struct Context {double sampleRate=0;int blockSize=0;unsigned generation=0;};
    using Commit=std::function<juce::Result(std::unique_ptr<TrackBank>&,const SessionSnapshot&,Context)>;
    using Report=std::function<void(const juce::String&)>;
    using Completion=std::function<void(bool saving,const juce::File&,const juce::Result&)>;
    // Runs on the message thread, after native/file staging and before commit.
    // The callback must stage every external slot into this unpublished bank.
    using StageExternal=std::function<juce::Result(TrackBank&,const SessionSnapshot&,Context)>;
    SessionWorker(juce::TimeSliceThread&,Commit,Report,Completion={},StageExternal={});
    ~SessionWorker() override;
    bool save(SessionSnapshot,const juce::File&);
    bool load(const juce::File&,Context);
    bool isBusy() const noexcept {return busy.load(std::memory_order_acquire);}
private:
    enum class Operation {Save,Load,Retire};
    void run() override;
    void timerCallback() override;
    juce::TimeSliceThread& disk;
    Commit commit;Report report;Completion completion;StageExternal stageExternal;
    Operation operation=Operation::Save;
    juce::File file;
    SessionSnapshot snapshot;
    Context context;
    std::unique_ptr<TrackBank> staged;
    juce::String error;
    std::atomic<bool> busy{false},done{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionWorker)
};
}

```

## Source/StemFileImporter.cpp

```cpp
#include "StemFileImporter.h"
namespace hem {
StemFileImporter::StemFileImporter(juce::TimeSliceThread& d,ContextProvider c,Commit handler)
 :Thread("HEM WAV import worker"),disk(d),contextProvider(std::move(c)),commit(std::move(handler)){
    setLookAndFeel(&look);for(auto* child:std::array<juce::Component*,4>{&browseButton,&applyButton,&cancelButton,&status})addAndMakeVisible(*child);
    status.setText("DROP 1-13 WAV FILES / sequential tracks 1-13",juce::dontSendNotification);
    status.setInterceptsMouseClicks(false,false);status.setJustificationType(juce::Justification::centred);
    browseButton.onClick=[this]{browse();};applyButton.onClick=[this]{apply();};cancelButton.onClick=[this]{cancel();};
    applyButton.setEnabled(false);setSize(900,110);startTimerHz(20);
}
StemFileImporter::~StemFileImporter(){shutdown();setLookAndFeel(nullptr);}
void StemFileImporter::shutdown(){
    if(closed)return;closed=true;stopTimer();picker.reset();signalThreadShouldExit();waitForThreadToExit(-1);
    // Shutdown occurs before the shared disk read-ahead thread is stopped.
    pending.reset();finished.reset();garbage.reset();
}
bool StemFileImporter::isInterestedInFileDrag(const juce::StringArray& files){
    if(closed||busy.load()||pending||files.isEmpty()||files.size()>capacity)return false;
    for(const auto& name:files)if(!name.endsWithIgnoreCase(".wav"))return false;
    return true;
}
void StemFileImporter::fileDragEnter(const juce::StringArray&,int,int){hovered=true;repaint();}
void StemFileImporter::fileDragExit(const juce::StringArray&){hovered=false;repaint();}
void StemFileImporter::filesDropped(const juce::StringArray& files,int,int){hovered=false;submitFiles(files);repaint();}
void StemFileImporter::mouseUp(const juce::MouseEvent& e){if(e.originalComponent==this)browse();}
void StemFileImporter::browse(){
    if(closed||busy.load()||pending)return;
    picker=std::make_unique<juce::FileChooser>("Import up to 13 mono/stereo WAV stems",juce::File(),"*.wav");
    juce::Component::SafePointer<StemFileImporter> safe(this);
    picker->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::canSelectMultipleItems,
        [safe](const juce::FileChooser& chooser){if(!safe)return;juce::StringArray paths;for(const auto& f:chooser.getResults())paths.add(f.getFullPathName());if(!paths.isEmpty())safe->submitFiles(paths);});
}
void StemFileImporter::submitFiles(const juce::StringArray& files,int firstTrack){
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if(closed||busy.load()||pending){status.setText("Finish or clear the current import first",juce::dontSendNotification);return;}
    if(firstTrack<0||files.isEmpty()||firstTrack+files.size()>capacity){status.setText("Import must fit within 13 tracks; no files were changed",juce::dontSendNotification);return;}
    for(const auto& path:files)if(!path.endsWithIgnoreCase(".wav")){status.setText("WAV only; the whole batch was rejected",juce::dontSendNotification);return;}
    jobContext=contextProvider();if(jobContext.sampleRate<=0||jobContext.blockSize<=0){status.setText("Session processing is not prepared; try again",juce::dontSendNotification);return;}
    jobFiles=files;jobFirstTrack=firstTrack;error.clear();progress.store(0);ready.store(false);cleanupJob=false;busy.store(true);
    status.setText("Preparing WAV readers and sample-rate conversion...",juce::dontSendNotification);browseButton.setEnabled(false);applyButton.setEnabled(false);
    if(!startThread()){busy.store(false);browseButton.setEnabled(true);status.setText("Could not start the import worker",juce::dontSendNotification);}
}
void StemFileImporter::run(){
    if(cleanupJob){garbage.reset();ready.store(true,std::memory_order_release);return;}
    try{
        juce::AudioFormatManager formats;formats.registerFormat(new juce::WavAudioFormat(),true);
        auto batch=std::make_unique<Batch>();batch->context=jobContext;batch->firstTrack=jobFirstTrack;batch->count=jobFiles.size();
        for(int i=0;i<batch->count;++i){
            if(threadShouldExit()){error="Import cancelled; original tracks unchanged";ready.store(true,std::memory_order_release);return;}
            auto stem=std::make_unique<StemPlayback>();stem->prepareFile(juce::File(jobFiles[i]),formats,disk,jobContext.sampleRate,jobContext.blockSize,jobContext.looping);
            batch->stems[size_t(i)]=std::move(stem);progress.store(i+1,std::memory_order_relaxed);
        }
        if(threadShouldExit())error="Import cancelled; original tracks unchanged";else finished=std::move(batch);
    }catch(const std::exception& e){error=juce::String(e.what());}
    ready.store(true,std::memory_order_release);
}
void StemFileImporter::timerCallback(){
    if(busy.load()&&!cleanupJob&&!ready.load())status.setText("PREPARING "+juce::String(progress.load())+" / "+juce::String(jobFiles.size())+" WAV files  -  current audio unchanged",juce::dontSendNotification);
    if(!ready.load(std::memory_order_acquire)||isThreadRunning())return;
    ready.store(false);busy.store(false);
    if(cleanupJob){cleanupJob=false;browseButton.setEnabled(true);return;}
    if(error.isNotEmpty()){status.setText(error,juce::dontSendNotification);browseButton.setEnabled(true);return;}
    pending=std::move(finished);if(!pending)return;
    juce::String mapping;
    for(int i=0;i<pending->count;++i)mapping+="TRACK "+juce::String(pending->firstTrack+i+1)+": "+pending->stems[size_t(i)]->file.getFileName()+"\n";
    status.setTooltip(mapping);status.setText("READY: "+juce::String(pending->count)+" WAVs -> TRACKS "+juce::String(pending->firstTrack+1)+"-"+juce::String(pending->firstTrack+pending->count)+" / Pause or stop, then APPLY",juce::dontSendNotification);
    applyButton.setEnabled(true);
}
void StemFileImporter::apply(){
    if(!pending||busy.load())return;
    const auto result=commit(*pending);
    if(result.failed()){status.setText(result.getErrorMessage(),juce::dontSendNotification);return;}
    status.setText("IMPORT APPLIED / original insert chains retained / press PLAY",juce::dontSendNotification);
    disposePending();
}
void StemFileImporter::disposePending(){
    // The host swaps replaced sources INTO the batch. Reclaim their readers and
    // buffering clients on this worker, never in the callback or commit lock.
    garbage=std::move(pending);cleanupJob=true;busy.store(true);ready.store(false);applyButton.setEnabled(false);
    if(!startThread()){busy.store(false);cleanupJob=false;status.setText("Cleanup worker unavailable; close importer to release retained sources",juce::dontSendNotification);}
}
void StemFileImporter::cancel(){
    if(busy.load()){if(!cleanupJob)signalThreadShouldExit();return;}
    if(pending){status.setText("Prepared batch cleared; original tracks unchanged",juce::dontSendNotification);disposePending();}
}
void StemFileImporter::paint(juce::Graphics& g){
    juce::ColourGradient glass(juce::Colour(0xff263039),0,0,juce::Colour(0xff070a10),0,float(getHeight()),false);
    g.setGradientFill(glass);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2),10);
    g.setColour(hovered?juce::Colour(0xffff8800):juce::Colour(0xff00cdda));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2),10,2);
}
void StemFileImporter::resized(){auto b=getLocalBounds().reduced(8);auto row=b.removeFromBottom(32);browseButton.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));applyButton.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));cancelButton.setBounds(row.reduced(2));status.setBounds(b);}
}

```

## Source/StemFileImporter.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "StemPlayback.h"
#include "HardwareLookAndFeel.h"
#include <array>
#include <functional>
#include <stdexcept>

namespace hem {
class StemFileImporter final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::Thread,
                               private juce::Timer {
public:
    static constexpr int capacity=13;
    struct Context {double sampleRate=0;int blockSize=0;unsigned generation=0;bool looping=false;};
    struct Batch {
        Context context;int firstTrack=0,count=0;
        std::array<std::unique_ptr<StemPlayback>,capacity> stems;
    };
    using ContextProvider=std::function<Context()>;
    // Called only on the message thread. The host checks its device generation
    // and swaps source pointers while the device callback is briefly excluded.
    using Commit=std::function<juce::Result(Batch&)>;
    StemFileImporter(juce::TimeSliceThread&,ContextProvider,Commit);
    ~StemFileImporter() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&,int,int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&,int,int) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    // Same safe path for an existing one-file picker; firstTrack is zero-based.
    void submitFiles(const juce::StringArray&,int firstTrack=0);
    bool isBusy() const noexcept {return busy.load();}
    bool hasPendingWork() const noexcept {return busy.load()||ready.load()||pending!=nullptr;} // message thread
    void shutdown();
    void cancelPendingWork(){cancel();}
private:
    void run() override;
    void timerCallback() override;
    void browse();
    void apply();
    void cancel();
    void disposePending();
    juce::TimeSliceThread& disk;
    ContextProvider contextProvider;Commit commit;
    HardwareLookAndFeel look;
    juce::TextButton browseButton{"BROWSE WAVS"},applyButton{"APPLY WHEN STOPPED"},cancelButton{"CANCEL / CLEAR"};
    juce::Label status;
    juce::TooltipWindow tooltips{nullptr,500};
    juce::StringArray jobFiles;int jobFirstTrack=0;Context jobContext;
    std::unique_ptr<Batch> pending,finished,garbage;
    juce::String error;std::atomic<bool> busy{false},ready{false};
    std::atomic<int> progress{0};bool hovered=false,cleanupJob=false,closed=false;
    std::unique_ptr<juce::FileChooser> picker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemFileImporter)
};
}

```

## Source/StemPlayback.h

```cpp
#pragma once
#include <JuceHeader.h>
#include <stdexcept>
namespace hem {
// Prepared entirely off the audio/message threads during a batch import.
// Ownership moves into a TrackEngine at commit; transport and reader addresses
// do not change. Destruction of replaced sources is queued back to the worker.
struct StemPlayback {
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource transport;
    juce::File file;
    double sourceRate=0;
    int channels=0;
    ~StemPlayback(){transport.setSource(nullptr);transport.releaseResources();}
    void prepareFile(const juce::File& f,juce::AudioFormatManager& formats,
                     juce::TimeSliceThread& disk,double targetRate,int block,bool looping=false){
        std::unique_ptr<juce::AudioFormatReader> raw(formats.createReaderFor(f));
        if(!raw)throw std::runtime_error("Cannot decode WAV: "+f.getFileName().toStdString());
        if(raw->lengthInSamples<=0||raw->sampleRate<=0||raw->numChannels<1||raw->numChannels>2)
            throw std::runtime_error("Use a non-empty mono or stereo WAV: "+f.getFileName().toStdString());
        file=f;sourceRate=raw->sampleRate;channels=int(raw->numChannels);
        reader=std::make_unique<juce::AudioFormatReaderSource>(raw.release(),true);
        reader->setLooping(looping);
        // Source-rate correction instantiates JUCE's resampler. Read-ahead
        // keeps file reads on disk's TimeSliceThread, not the audio callback.
        transport.setSource(reader.get(),65536,&disk,sourceRate,2);
        transport.prepareToPlay(block,targetRate);
        transport.setPosition(0);
    }
};
}

```

## Source/StudioWindow.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "HardwareLookAndFeel.h"

// Native document windows keep ownership of editors explicit. Closing hides a
// window; the host destroys it on the message thread before retiring its DSP.
class StudioWindow final : public juce::DocumentWindow {
public:
    explicit StudioWindow(const juce::String& name,juce::Component* content)
        :DocumentWindow(name,juce::Colour(0xff080b10),DocumentWindow::closeButton){
        setLookAndFeel(&look);setUsingNativeTitleBar(false);
        setContentOwned(content,true);setResizable(true,false);
        setResizeLimits(800,500,1920,1200);centreWithSize(1120,680);setVisible(true);
    }
    ~StudioWindow() override {clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed() override {setVisible(false);}
private: HardwareLookAndFeel look;
};

// Eight rows are fixed-size UI storage. Callbacks execute on the message thread.
class RackView final : public juce::Component {
public:
    struct Row {
        juce::Label name;
        juce::TextButton open{"OPEN"},bypass{"BYP"},up{"UP"},down{"DOWN"},remove{"REMOVE"};
    };
    std::array<Row,8> rows;
    juce::ComboBox tracks,plugins;
    juce::TextButton load{"LOAD TRACK"},insert{"INSERT / REPLACE SELECTED"};
    juce::Label title,status;
    std::function<void(int)> onOpen,onBypass,onUp,onDown,onRemove;
    RackView(){
        setLookAndFeel(&look);
        for(auto* c:std::array<juce::Component*,6>{&tracks,&plugins,&load,&insert,&title,&status})addAndMakeVisible(*c);
        title.setText("HYBRID ELITE MAFIA / PER-TRACK SERIAL INSERT RACK",juce::dontSendNotification);
        for(int i=0;i<8;++i){auto& row=rows[size_t(i)];
            for(auto* c:std::array<juce::Component*,6>{&row.name,&row.open,&row.bypass,&row.up,&row.down,&row.remove})addAndMakeVisible(*c);
            row.open.onClick=[this,i]{if(onOpen)onOpen(i);};row.bypass.onClick=[this,i]{if(onBypass)onBypass(i);};
            row.up.onClick=[this,i]{if(onUp)onUp(i);};row.down.onClick=[this,i]{if(onDown)onDown(i);};row.remove.onClick=[this,i]{if(onRemove)onRemove(i);};
        }
        setSize(1120,640);
    }
    ~RackView() override {setLookAndFeel(nullptr);}
    void paint(juce::Graphics& g) override {
        juce::ColourGradient glass(juce::Colour(0xff242d35),0,0,juce::Colour(0xff06080c),0,float(getHeight()),false);
        g.setGradientFill(glass);g.fillAll();g.setColour(juce::Colour(0xff00d9ea));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3),12,2);
        g.setColour(juce::Colour(0x35ffffff));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(7),9,1);
    }
    void resized() override {
        auto b=getLocalBounds().reduced(16);title.setBounds(b.removeFromTop(28));auto top=b.removeFromTop(40);
        tracks.setBounds(top.removeFromLeft(210).reduced(3));load.setBounds(top.removeFromLeft(140).reduced(3));plugins.setBounds(top.removeFromLeft(290).reduced(3));insert.setBounds(top.reduced(3));
        status.setBounds(b.removeFromBottom(30));const int h=b.getHeight()/8;
        for(auto& r:rows){auto line=b.removeFromTop(h).reduced(3);r.remove.setBounds(line.removeFromRight(80).reduced(2));r.down.setBounds(line.removeFromRight(60).reduced(2));r.up.setBounds(line.removeFromRight(60).reduced(2));r.bypass.setBounds(line.removeFromRight(70).reduced(2));r.open.setBounds(line.removeFromRight(80).reduced(2));r.name.setBounds(line);}
    }
private:HardwareLookAndFeel look;
};

```

## Source/Systems/AnalysisView.cpp

```cpp
#include "AnalysisView.h"
namespace hem {
void AnalysisView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff080e16));
    auto area = getLocalBounds().toFloat().reduced(18);
    g.setColour(juce::Colours::cyan);
    g.drawText(worker.isOperational() ? "PRE-LISTENING MASTER / before vocal return, correction and final limiter"
                                      : "ANALYSIS WORKER UNAVAILABLE",
               area.removeFromTop(26), juce::Justification::centred);
    g.setColour(juce::Colours::lightgrey);
    g.drawText("L cyan / R amber | FFT: sine-peak dBFS | waveform: linear full scale",
               area.removeFromTop(22), juce::Justification::centred);
    auto spectrumFrame = area.removeFromTop(area.getHeight() * .54f);
    auto spectrum = spectrumFrame.reduced(43, 22);
    const auto yForDb = [&](float db) {
        return spectrum.getBottom() - juce::jlimit(0.f, 1.f, (db + 100.f) / 100.f) * spectrum.getHeight();
    };
    const double maximumHz = frame.sampleRate > 40 ? std::min(20000., frame.sampleRate * .5) : 20000.;
    const auto xForHz = [&](double hz) {
        return spectrum.getX() + float(std::log(hz / 20.) / std::log(maximumHz / 20.)) * spectrum.getWidth();
    };
    for (float tick : {0.f, -20.f, -40.f, -60.f, -80.f, -100.f}) {
        g.setColour(juce::Colour(0xff20414b));
        g.drawHorizontalLine(int(yForDb(tick)), spectrum.getX(), spectrum.getRight());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(juce::String(int(tick)), spectrum.getX() - 40, yForDb(tick) - 8, 34, 16,
                   juce::Justification::centredRight);
    }
    for (double hz : {20., 100., 1000., 10000., 20000.}) {
        if (hz > maximumHz) continue;
        const float x = xForHz(hz);
        g.setColour(juce::Colour(0xff18323e));
        g.drawVerticalLine(int(x), spectrum.getY(), spectrum.getBottom());
        g.setColour(juce::Colours::lightgrey);
        g.drawText(hz >= 1000 ? juce::String(hz / 1000., 0) + "k" : juce::String(hz, 0),
                   x - 24, spectrum.getBottom() + 3, 48, 16, juce::Justification::centred);
    }
    if (frame.sampleRate > 0 && spectrum.getWidth() > 0) {
        for (int channel = 0; channel < 2; ++channel) {
            juce::Path path;
            const auto& values = channel == 0 ? frame.leftDb : frame.rightDb;
            for (int x = 0; x < int(spectrum.getWidth()); ++x) {
                const double hz = 20 * std::pow(maximumHz / 20., double(x) / std::max(1.f, spectrum.getWidth() - 1));
                const int bin = juce::jlimit(0, AnalysisWorker::bins - 1, int(hz * AnalysisWorker::fftSize / frame.sampleRate));
                if (x == 0) path.startNewSubPath(spectrum.getX(), yForDb(values[size_t(bin)]));
                else path.lineTo(spectrum.getX() + x, yForDb(values[size_t(bin)]));
            }
            g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
            g.strokePath(path, juce::PathStrokeType(1.3f));
        }
    } else {
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Waiting for a complete audio FFT window", spectrum, juce::Justification::centred);
    }
    area.removeFromBottom(38);
    auto waveform = area.reduced(43, 10);
    for (int channel = 0; channel < 2; ++channel) {
        auto lane = waveform.withY(waveform.getY() + channel * waveform.getHeight() * .5f)
                            .withHeight(waveform.getHeight() * .5f).reduced(0, 6);
        const auto yForAmplitude = [&](float sample) {
            return lane.getCentreY() - juce::jlimit(-1.f, 1.f, sample) * lane.getHeight() * .45f;
        };
        for (float tick : {-1.f, 0.f, 1.f}) {
            g.setColour(juce::Colour(0xff20414b));
            g.drawHorizontalLine(int(yForAmplitude(tick)), lane.getX(), lane.getRight());
            g.setColour(juce::Colours::lightgrey);
            g.drawText(juce::String(tick, 0), lane.getX() - 31, yForAmplitude(tick) - 7, 26, 14,
                       juce::Justification::centredRight);
        }
        g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
        g.drawText(channel == 0 ? "L" : "R", lane.getRight() + 4, lane.getCentreY() - 8, 20, 16,
                   juce::Justification::centredLeft);
        for (int i = 0; i < frame.validColumns; ++i) {
            const auto& bucket = frame.waveform[size_t(i)];
            const float low = channel == 0 ? bucket.lowL : bucket.lowR;
            const float high = channel == 0 ? bucket.highL : bucket.highR;
            const float x = lane.getX() + float(i) * lane.getWidth() / std::max(1, frame.validColumns);
            g.drawVerticalLine(int(x), yForAmplitude(high), yForAmplitude(low));
            if (high > 1 || low < -1) {
                g.setColour(juce::Colours::red);
                if (high > 1) g.fillRect(x, lane.getY(), 2.f, 3.f);
                if (low < -1) g.fillRect(x, lane.getBottom() - 3, 2.f, 3.f);
                g.setColour(channel == 0 ? juce::Colours::cyan : juce::Colours::orange);
            }
        }
    }
    const double seconds = frame.sampleRate > 0 ? frame.validColumns * double(AnalysisWorker::bucketSize) / frame.sampleRate : 0;
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Waveform history " + juce::String(seconds, 3) + " s ending at latest snapshot | 8192 FFT / 64-frame min/max",
               getLocalBounds().removeFromBottom(38).removeFromTop(18), juce::Justification::centred);
    g.setColour(frame.droppedFrames > 0 || frame.discontinuity ? juce::Colours::orange : juce::Colours::lightgrey);
    g.drawText("Dropped display frames: " + juce::String(frame.droppedFrames)
               + (frame.discontinuity ? " / history reset after a gap" : " / display does not affect audio"),
               getLocalBounds().removeFromBottom(19), juce::Justification::centred);
}
}

```

## Source/Systems/AnalysisView.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "AnalysisWorker.h"
namespace hem {
// Lightweight native display. Drawing uses worker snapshots, never live audio
// pointers or FFT calls on the GUI thread.
class AnalysisView final : public juce::Component,private juce::Timer {
public:
    explicit AnalysisView(AnalysisWorker& w):worker(w){startTimerHz(20);setSize(900,520);}
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override {if(worker.copyLatest(frame))repaint();}
    AnalysisWorker& worker;AnalysisWorker::Snapshot frame;
};
}

```

## Source/Systems/AnalysisWorker.cpp

```cpp
#include "AnalysisWorker.h"
namespace hem {
AnalysisWorker::AnalysisWorker():juce::Thread("HEM spectrum and waveform"){
    for(int i=0;i<fftSize;++i){window[size_t(i)]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));windowSum+=window[size_t(i)];}
    latest.leftDb.fill(-160);latest.rightDb.fill(-160);startThread();
}
AnalysisWorker::~AnalysisWorker(){signalThreadShouldExit();notify();waitForThreadToExit(-1);}
void AnalysisWorker::prepare(double rate) noexcept {producerRate=rate;producerFrame=0;++producerEpoch;}
void AnalysisWorker::push(const float* l,const float* r,int count) noexcept {
    for(int offset=0;offset<count;offset+=packetSize){const int n=std::min(packetSize,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){drops.fetch_add(unsigned(n),std::memory_order_relaxed);producerFrame+=unsigned(n);continue;}
        auto& p=packets[size_t(n1?a:b)];p.size=n;p.rate=producerRate;p.epoch=producerEpoch;p.first=producerFrame;
        std::copy_n(l+offset,n,p.left.data());std::copy_n(r+offset,n,p.right.data());producerFrame+=unsigned(n);fifo.finishedWrite(1);
    }
}
bool AnalysisWorker::copyLatest(Snapshot& output) const {
    const juce::ScopedTryLock lock(snapshotLock);if(!lock.isLocked())return false;output=latest;return true;
}
void AnalysisWorker::resetHistory(){ringL.fill(0);ringR.fill(0);filled=write=hop=0;peakCount=peakWrite=bucketFill=0;accumulator={};}
void AnalysisWorker::consume(const Packet& p){
    if(p.epoch!=workerEpoch||p.first!=expectedFrame){resetHistory();gap=true;workerEpoch=p.epoch;}
    workerRate=p.rate;
    for(int i=0;i<p.size;++i){const float l=std::isfinite(p.left[size_t(i)])?p.left[size_t(i)]:0.f,r=std::isfinite(p.right[size_t(i)])?p.right[size_t(i)]:0.f;
        ringL[size_t(write)]=l;ringR[size_t(write)]=r;write=(write+1)%fftSize;filled=std::min(fftSize,filled+1);
        if(bucketFill==0)accumulator={l,l,r,r};
        else {accumulator.lowL=std::min(accumulator.lowL,l);accumulator.highL=std::max(accumulator.highL,l);accumulator.lowR=std::min(accumulator.lowR,r);accumulator.highR=std::max(accumulator.highR,r);}
        if(++bucketFill==bucketSize){peaks[size_t(peakWrite)]=accumulator;peakWrite=(peakWrite+1)%columns;peakCount=std::min(columns,peakCount+1);bucketFill=0;}
        expectedFrame=p.first+unsigned(i)+1;
        if(++hop>=fftSize/4&&filled==fftSize){hop=0;publish();}
    }
    expectedFrame=p.first+unsigned(p.size);
}
void AnalysisWorker::publish(){
    fftL.fill(0);fftR.fill(0);
    for(int i=0;i<fftSize;++i){const auto ix=size_t((write+i)%fftSize);fftL[size_t(i)]=ringL[ix]*window[size_t(i)];fftR[size_t(i)]=ringR[ix]*window[size_t(i)];}
    fft.performFrequencyOnlyForwardTransform(fftL.data(),true);fft.performFrequencyOnlyForwardTransform(fftR.data(),true);
    for(int i=0;i<bins;++i){const double scale=(i==0||i==bins-1?1.:2.)/windowSum;
        work.leftDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftL[size_t(i)])*scale)));
        work.rightDb[size_t(i)]=float(20*std::log10(std::max(1.e-8,double(fftR[size_t(i)])*scale)));}
    work.validColumns=peakCount;for(int i=0;i<peakCount;++i)work.waveform[size_t(i)]=peaks[size_t((peakWrite-peakCount+i+columns)%columns)];
    work.sampleRate=workerRate;work.frameEnd=expectedFrame;work.sequence=++sequence;work.droppedFrames=drops.load();work.discontinuity=gap;gap=false;
    const juce::ScopedLock lock(snapshotLock);latest=work;
}
void AnalysisWorker::run(){
    while(!threadShouldExit()){
        int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
        if(n1+n2==0){wait(3);continue;}
        consume(packets[size_t(n1?a:b)]);fifo.finishedRead(1);
    }
}
}

```

## Source/Systems/AnalysisWorker.h

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
namespace hem {
// One audio producer, one analysis consumer. No GUI lock, allocation, FFT or
// event notification occurs in push(). A slow analyser loses display packets,
// never audio. The GUI uses a non-blocking snapshot read.
class AnalysisWorker final : private juce::Thread {
public:
    static constexpr int fftOrder=13,fftSize=1<<fftOrder,bins=fftSize/2+1;
    static constexpr int columns=2048,bucketSize=64,packetSize=256,packetCount=128;
    struct Peak {float lowL=0,highL=0,lowR=0,highR=0;};
    struct Snapshot {
        std::array<float,bins> leftDb{},rightDb{};
        std::array<Peak,columns> waveform{}; // Oldest to newest; first validColumns entries.
        int validColumns=0;
        double sampleRate=0;
        std::uint64_t frameEnd=0,sequence=0;
        std::uint32_t droppedFrames=0;
        bool discontinuity=false;
    };
    AnalysisWorker();~AnalysisWorker() override;
    // Called with the audio callback excluded, e.g. prepareToPlay(). Only
    // producer metadata changes; worker FIFO/history is never reset here.
    bool isOperational() const {return isThreadRunning();}
    void prepare(double sampleRate) noexcept;
    void push(const float* left,const float* right,int count) noexcept;
    bool copyLatest(Snapshot&) const; // Message thread, returns false if busy.
private:
    struct Packet {std::array<float,packetSize> left{},right{};int size=0;double rate=0;std::uint64_t first=0;unsigned epoch=0;};
    void run() override;void consume(const Packet&);void publish();void resetHistory();
    juce::AbstractFifo fifo{packetCount};
    std::array<Packet,packetCount> packets;
    double producerRate=96000,workerRate=96000;unsigned producerEpoch=0,workerEpoch=0;
    std::uint64_t producerFrame=0,expectedFrame=0,sequence=0;
    std::atomic<std::uint32_t> drops{0};
    juce::dsp::FFT fft{fftOrder};
    std::array<float,fftSize> ringL{},ringR{},window{};
    std::array<float,2*fftSize> fftL{},fftR{};
    std::array<Peak,columns> peaks{};
    Peak accumulator;int bucketFill=0,peakWrite=0,peakCount=0,write=0,filled=0,hop=0;
    double windowSum=0;bool gap=false;
    mutable juce::CriticalSection snapshotLock;
    Snapshot latest,work;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisWorker)
};
}

```

## Source/Systems/AudioMeters.h

```cpp
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace hem {
// Final-output SAMPLE-peak and exponentially weighted RMS meter. This does not
// calculate true peak or LUFS. Its detector state is owned by the audio thread;
// the UI sees only atomic snapshots and may request a latch reset.
class AudioMeters {
public:
    static constexpr float silenceFloorDb = -120.0f;
    static constexpr double rmsTimeSeconds = 0.300;
    static constexpr double peakReleaseDbPerSecond = 24.0;
    static constexpr double holdTimeSeconds = 1.0;
    static constexpr double holdReleaseDbPerSecond = 18.0;

    struct ChannelSnapshot {
        float peakDb = silenceFloorDb;
        float rmsDb = silenceFloorDb;
        float heldPeakDb = silenceFloorDb;
        bool clipped = false;
    };
    struct Snapshot {
        ChannelSnapshot left, right;
        std::uint64_t nonFiniteSamples = 0;
        bool ready = false;
    };

    // Call while callback processing is stopped (e.g. prepareToPlay). An invalid
    // rate produces an unavailable meter, never a guessed time scale.
    bool prepare(double actualRate) noexcept {
        leftState = {};
        rightState = {};
        invalidSamples = 0;
        clearRequested.store(false, std::memory_order_relaxed);
        prepared = std::isfinite(actualRate) && actualRate > 0.0;
        if (prepared) {
            rmsMemory = std::exp(-1.0 / (actualRate * rmsTimeSeconds));
            peakMemory = std::pow(10.0, -peakReleaseDbPerSecond / (20.0 * actualRate));
            holdMemory = std::pow(10.0, -holdReleaseDbPerSecond / (20.0 * actualRate));
            holdSamples = static_cast<std::uint64_t>(std::max(1.0, std::round(actualRate * holdTimeSeconds)));
        }
        publish();
        return prepared;
    }

    // Null channels are silence. A missing right channel is NOT silently copied
    // from left: the caller must pass the actual emitted mono signal twice.
    // No allocation, lock, per-sample atomic operation or GUI callback occurs here.
    void push(const float* left, const float* right, int samples) noexcept {
        if (!prepared || samples < 0) return;
        if (clearRequested.exchange(false, std::memory_order_relaxed)) {
            leftState.clipped = rightState.clipped = false;
            invalidSamples = 0;
        }
        for (int i = 0; i < samples; ++i) {
            advance(leftState, left ? left[i] : 0.0f);
            advance(rightState, right ? right[i] : 0.0f);
        }
        publish();
    }

    // Consumed at the next push, including a zero-length push. A new clipped
    // sample in that same callback legitimately re-latches the indicator.
    void resetClip() noexcept { clearRequested.store(true, std::memory_order_relaxed); }

    Snapshot snapshot() const noexcept {
        Snapshot result;
        // The audio thread never waits. UI reads retry at most three times if a
        // block publication overlaps. All individual fields remain data-race-free.
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto before = revision.load(std::memory_order_seq_cst);
            result.left = load(leftPublished);
            result.right = load(rightPublished);
            result.nonFiniteSamples = invalidPublished.load(std::memory_order_seq_cst);
            result.ready = readyPublished.load(std::memory_order_seq_cst);
            const auto after = revision.load(std::memory_order_seq_cst);
            if (before == after && (after & 1u) == 0) break;
        }
        return result;
    }

private:
    static_assert(std::atomic<bool>::is_always_lock_free, "Meter flags must be lock-free");
    static_assert(std::atomic<float>::is_always_lock_free, "Meter float snapshots must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Meter counters must be lock-free");
    struct ChannelState {
        double peak = 0.0, square = 0.0, held = 0.0;
        std::uint64_t holdRemaining = 0;
        bool clipped = false;
    };
    struct PublishedChannel {
        std::atomic<float> peak{silenceFloorDb}, rms{silenceFloorDb}, held{silenceFloorDb};
        std::atomic<bool> clipped{false};
    };
    static float db(double amplitude) noexcept {
        return amplitude > 0.000001 ? static_cast<float>(20.0 * std::log10(amplitude)) : silenceFloorDb;
    }
    void advance(ChannelState& state, float sample) noexcept {
        const bool finite = std::isfinite(sample);
        if (!finite && invalidSamples != UINT64_MAX) ++invalidSamples;
        const double magnitude = finite ? std::abs(static_cast<double>(sample)) : 0.0;
        state.clipped = state.clipped || magnitude >= 1.0;
        state.peak = std::max(magnitude, state.peak * peakMemory);
        // Double squaring remains finite even for the largest finite float.
        state.square = rmsMemory * state.square + (1.0 - rmsMemory) * magnitude * magnitude;
        if (magnitude >= state.held && magnitude > 0.0) {
            state.held = magnitude;
            state.holdRemaining = holdSamples;
        } else if (state.holdRemaining > 0) {
            --state.holdRemaining;
        } else {
            state.held = std::max(magnitude, state.held * holdMemory);
        }
        // Avoid long-running denormals after silence without changing the floor.
        if (state.peak < 1.0e-12) state.peak = 0.0;
        if (state.held < 1.0e-12) state.held = 0.0;
        if (state.square < 1.0e-24) state.square = 0.0;
    }
    static void publishChannel(PublishedChannel& to, const ChannelState& from) noexcept {
        to.peak.store(db(from.peak), std::memory_order_seq_cst);
        to.rms.store(db(std::sqrt(from.square)), std::memory_order_seq_cst);
        to.held.store(db(from.held), std::memory_order_seq_cst);
        to.clipped.store(from.clipped, std::memory_order_seq_cst);
    }
    static ChannelSnapshot load(const PublishedChannel& from) noexcept {
        return {from.peak.load(std::memory_order_seq_cst), from.rms.load(std::memory_order_seq_cst),
                from.held.load(std::memory_order_seq_cst), from.clipped.load(std::memory_order_seq_cst)};
    }
    void publish() noexcept {
        revision.fetch_add(1, std::memory_order_seq_cst);
        publishChannel(leftPublished, leftState);
        publishChannel(rightPublished, rightState);
        invalidPublished.store(invalidSamples, std::memory_order_seq_cst);
        readyPublished.store(prepared, std::memory_order_seq_cst);
        revision.fetch_add(1, std::memory_order_seq_cst);
    }
    ChannelState leftState, rightState;
    PublishedChannel leftPublished, rightPublished;
    double rmsMemory = 0.0, peakMemory = 0.0, holdMemory = 0.0;
    std::uint64_t holdSamples = 0, invalidSamples = 0;
    bool prepared = false;
    std::atomic<bool> clearRequested{false}, readyPublished{false};
    std::atomic<std::uint64_t> invalidPublished{0}, revision{0};
};
} // namespace hem

```

## Source/Systems/CalibratedMeterView.h

```cpp
#pragma once
#include <JuceHeader.h>
#include "AudioMeters.h"

namespace hem {
// UI-only view: opaque background covers the faceplate's decorative meter area.
// Heights use a fixed -60..0 dBFS scale, never normalization to the current audio.
class CalibratedMeterView : public juce::Component, private juce::Timer {
public:
    explicit CalibratedMeterView(AudioMeters& source) : meters(source) {
        setOpaque(true);
        setName("Final audio output sample peak and RMS meters");
        setWantsKeyboardFocus(true);
        data = meters.snapshot(); // First paint reflects already prepared/processed audio.
        startTimerHz(25);
    }
    ~CalibratedMeterView() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff061017));
        const auto bounds = getLocalBounds().toFloat();
        if (bounds.getWidth() < 60 || bounds.getHeight() < 100) return;
        const float sx = bounds.getWidth() / 190.0f, sy = bounds.getHeight() / 310.0f;
        const float font = juce::jlimit(8.0f, 14.0f, 12.0f * juce::jmin(sx, sy));
        const auto plot = juce::Rectangle<float>(40 * sx, 52 * sy, 136 * sx, 186 * sy);
        const auto yFor = [&](float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour(juce::Colour(0xff00a7be));
        g.drawRoundedRectangle(bounds.reduced(1), 4, 1);
        g.setFont(juce::Font(juce::FontOptions(font).withStyle("Bold")));
        g.setColour(juce::Colour(0xffd5faff));
        g.drawText("OUTPUT / dBFS", 0, int(6 * sy), getWidth(), int(19 * sy), juce::Justification::centred);
        g.setFont(juce::Font(juce::FontOptions(font * 0.87f)));
        g.drawText("SAMPLE PEAK + RMS", 0, int(25 * sy), getWidth(), int(15 * sy), juce::Justification::centred);
        for (const float tick : {0.0f, -6.0f, -12.0f, -24.0f, -48.0f, -60.0f}) {
            const float y = yFor(tick);
            g.setColour(juce::Colour(0xff25404b));
            g.drawLine(plot.getX(), y, plot.getRight(), y, 1.0f);
            g.setColour(juce::Colour(0xff8aa6b3));
            g.drawText(juce::String(int(tick)), int(3 * sx), int(y - 7 * sy), int(31 * sx), int(14 * sy), juce::Justification::centredRight);
        }
        drawChannel(g, data.left, juce::Rectangle<float>(51 * sx, plot.getY(), 43 * sx, plot.getHeight()), "L");
        drawChannel(g, data.right, juce::Rectangle<float>(121 * sx, plot.getY(), 43 * sx, plot.getHeight()), "R");
        g.setColour(juce::Colour(0xffd5faff));
        g.setFont(juce::Font(juce::FontOptions(font * 0.90f)));
        g.drawText("PEAK  " + reading(data.left.peakDb) + " / " + reading(data.right.peakDb),
                   2, int(254 * sy), getWidth() - 4, int(17 * sy), juce::Justification::centred);
        g.setColour(juce::Colour(0xff55dccc));
        g.drawText("RMS   " + reading(data.left.rmsDb) + " / " + reading(data.right.rmsDb),
                   2, int(271 * sy), getWidth() - 4, int(17 * sy), juce::Justification::centred);
        const bool warning = data.left.clipped || data.right.clipped || data.nonFiniteSamples > 0;
        g.setColour(warning ? juce::Colour(0xffff8a55) : juce::Colour(0xff8aa6b3));
        g.drawText(!data.ready ? "NO AUDIO" : data.nonFiniteSamples > 0 ? "INVALID / CLICK RESET" : warning ? "CLIP / CLICK RESET" : "1s HOLD / CLICK RESET",
                   0, int(291 * sy), getWidth(), int(15 * sy), juce::Justification::centred);
        if (hasKeyboardFocus(true)) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(bounds.reduced(3), 3, 1);
        }
    }
    void mouseDown(const juce::MouseEvent&) override { meters.resetClip(); }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            meters.resetClip();
            return true;
        }
        return false;
    }

private:
    static juce::String reading(float db) { return db <= AudioMeters::silenceFloorDb ? juce::String("-inf") : juce::String(db, 1); }
    void drawChannel(juce::Graphics& g, const AudioMeters::ChannelSnapshot& channel,
                     juce::Rectangle<float> lane, const juce::String& name) const {
        const auto yFor = [&](float db) { return lane.getBottom() - lane.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour(juce::Colour(0xff02070b));
        g.fillRect(lane);
        g.setColour(juce::Colour(0xff235767));
        g.fillRect(lane.withTop(yFor(channel.peakDb)));
        g.setColour(juce::Colour(0xff35baa9));
        g.fillRect(lane.reduced(lane.getWidth() * 0.20f, 0).withTop(yFor(channel.rmsDb)));
        if (channel.peakDb > -60) {
            g.setColour(juce::Colour(0xff52e7ff));
            g.drawLine(lane.getX(), yFor(channel.peakDb), lane.getRight(), yFor(channel.peakDb), 2);
        }
        if (channel.heldPeakDb > -60) {
            g.setColour(juce::Colour(0xffffbf60));
            g.drawLine(lane.getX(), yFor(channel.heldPeakDb), lane.getRight(), yFor(channel.heldPeakDb), 2);
        }
        g.setColour(channel.clipped ? juce::Colour(0xffff504a) : juce::Colour(0xff28353d));
        g.fillRoundedRectangle(lane.getX(), lane.getY() - 8, lane.getWidth(), 4, 1);
        g.setColour(juce::Colour(0xffd5faff));
        g.drawText(name, int(lane.getX()), int(lane.getBottom() + 3), int(lane.getWidth()), 14, juce::Justification::centred);
    }
    void timerCallback() override { data = meters.snapshot(); repaint(); }
    AudioMeters& meters;
    AudioMeters::Snapshot data;
};
} // namespace hem

```

## Source/Systems/DeviceSettingsPanel.h

```cpp
#pragma once
#include <JuceHeader.h>
namespace hem {
// The platform backend supplies names and channel masks. No model assumptions.
class DeviceSettingsPanel final : public juce::Component {
public:
 explicit DeviceSettingsPanel(juce::AudioDeviceManager& m):manager(m) {
  addAndMakeVisible(refresh);addAndMakeVisible(note);addAndMakeVisible(view);
  note.setText("Choose driver, input/output device and active channels. USB interfaces appear under their installed driver. Select one mono output or two stereo outputs. Changes restart the audio device.",juce::dontSendNotification);
  refresh.onClick=[this]{scan();};scan();
 }
 void setDeviceChangesAllowed(bool allowed){
  if(changesAllowed==allowed)return;changesAllowed=allowed;refresh.setEnabled(allowed);if(selector)selector->setEnabled(allowed);
  note.setText(allowed?"Choose installed driver, devices and active channels. Actual hardware names/rates are shown. Changes restart the audio device.":"Hardware settings are read-only while playback, recording or export is active. Navigation and the shared transport remain available; pause/finish before changing devices.",juce::dontSendNotification);
 }
 void resized() override {
  auto b=getLocalBounds().reduced(10);refresh.setBounds(b.removeFromTop(34));
  note.setBounds(b.removeFromTop(64));view.setBounds(b);
  if(selector)selector->setSize(std::max(200,view.getWidth()-20),std::max(600,view.getHeight()));
 }
private:
 void scan(){
  for(auto* type:manager.getAvailableDeviceTypes())type->scanForDevices();
  // AudioSourcePlayer supports at most 128 compacted channels. Individual
  // hardware channel identities remain selectable, not fixed to input 1/2.
  selector=std::make_unique<juce::AudioDeviceSelectorComponent>(manager,0,128,1,2,false,false,false,false);
  selector->setEnabled(changesAllowed);view.setViewedComponent(selector.get(),false);resized();
 }
 bool changesAllowed=true;
 juce::AudioDeviceManager& manager;
 juce::TextButton refresh{"RESCAN CONNECTED AUDIO DEVICES"};juce::Label note;
 std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
 juce::Viewport view;
};
}

```

## Source/Systems/LiveVocalManager.cpp

```cpp
#include "LiveVocalManager.h"
#include <stdexcept>
namespace hem {
void TrackingInsertChain::prepare(double rate,int block){maximumBlock=std::max(1,block);for(auto& b:dry)b.assign(size_t(maximumBlock),0);
 for(auto& p:slots)if(p){p->processor->setPlayConfigDetails(2,2,rate,block);p->processor->prepareToPlay(rate,block);p->dryDelay.prepare(p->processor->getLatencySamples());p->wet.reset(rate,p->bypassed?0.f:1.f);}}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::makeNode(const juce::String& id,double rate,int block){
 if(id.isEmpty())return {};auto p=PluginRegistry::create(id);if(!p||p->monitorOnly())throw std::runtime_error("Invalid vocal insert");
 auto node=std::make_unique<Node>();node->processor=std::move(p);node->processor->setPlayConfigDetails(2,2,rate,block);node->processor->prepareToPlay(rate,block);node->dryDelay.prepare(node->processor->getLatencySamples());node->wet.reset(rate,1);return node;
}
std::unique_ptr<TrackingInsertChain::Node> TrackingInsertChain::exchange(int index,std::unique_ptr<Node> p){if(index<0||index>=8)throw std::out_of_range("Vocal insert");slots[size_t(index)].swap(p);return p;}
void TrackingInsertChain::process(float* l,float* r,int n) noexcept {
 for(auto& node:slots)if(node){std::copy_n(l,n,dry[0].data());std::copy_n(r,n,dry[1].data());node->dryDelay.process(dry[0].data(),dry[1].data(),n);
    float* c[]{l,r};view.setDataToReferTo(c,2,n);midi.clear();node->processor->processBlock(view,midi);node->wet.set(node->bypassed?0.f:1.f);
    for(int i=0;i<n;++i){const float w=node->wet.next();l[i]=dry[0][size_t(i)]+w*(l[i]-dry[0][size_t(i)]);r[i]=dry[1][size_t(i)]+w*(r[i]-dry[1][size_t(i)]);}}
}
void LiveVocalManager::prepare(double sr,int block,int inputs){recorder.stop();++generation;rate=sr;channels=std::clamp(inputs,0,128);input=0;inputWeights.resize(size_t(channels));for(int c=0;c<channels;++c)inputWeights[size_t(c)].reset(sr,c==0?1.f:0.f);dry.setSize(2,block);processed.setSize(2,block);rack.prepare(sr,block);dryMix.reset(sr,0);wetMix.reset(sr,0);level.reset(sr,1);monitor=Monitor::Off;}
bool LiveVocalManager::configureTake(TakeRecorder::Setup& setup){if(recorder.busy())return false;if(channels<=0||input.load()<0||input.load()>=channels)return false;setup.rate=rate;setup.channels=printInserts?2:1;setup.printedLatency=printInserts?rack.latency():0;takeGeneration=generation;return true;}
void LiveVocalManager::capture(const juce::AudioBuffer<float>& buffer,int start,int n,double position) noexcept {
 const int index=input.load();const bool valid=index>=0&&index<channels&&index<buffer.getNumChannels();float max=0;
 for(int c=0;c<channels;++c)inputWeights[size_t(c)].set(valid&&c==index?1.f:0.f);
 for(int i=0;i<n;++i){float raw=0;for(int c=0;c<channels;++c){const float weight=inputWeights[size_t(c)].next();if(c<channels&&c<buffer.getNumChannels())raw+=weight*buffer.getSample(c,start+i);}const float sample=std::isfinite(raw)?raw:0.f;dry.setSample(0,i,sample);dry.setSample(1,i,sample);max=std::max(max,std::abs(sample));}
 peak.store(max);for(int c=0;c<2;++c)processed.copyFrom(c,0,dry,c,0,n);rack.process(processed.getWritePointer(0),processed.getWritePointer(1),n);
 if(takeGeneration!=generation)recorder.stop();
 const auto& tap=printInserts?processed:dry;recorder.push(tap.getReadPointer(0),tap.getReadPointer(1),n,position);
}
void LiveVocalManager::addMonitor(juce::AudioBuffer<float>& bus,int n) noexcept {
 const auto m=monitor.load();dryMix.set(m==Monitor::DrySoftware?1.f:0.f);wetMix.set(m==Monitor::InsertsSoftware?1.f:0.f);level.set(gain(monitorDb.load()));
 auto* l=bus.getWritePointer(0);auto* r=bus.getWritePointer(1);
 for(int i=0;i<n;++i){const float d=dryMix.next(),w=wetMix.next(),g=level.next();l[i]+=g*(d*dry.getSample(0,i)+w*processed.getSample(0,i));r[i]+=g*(d*dry.getSample(1,i)+w*processed.getSample(1,i));}
}
}

```

## Source/Systems/LiveVocalManager.h

```cpp
#pragma once
#include "TakeRecorder.h"
#include "../Plugins/PluginRegistry.h"
namespace hem {
// Dedicated tracking rack: only actual processor latency, no fixed 6 ms reserve
// for an empty slot. Structural edits require monitoring OFF and recording idle.
// Parameters/bypass remain live. All publication uses the host callback lock.
class TrackingInsertChain final {
public:
    struct Node {std::unique_ptr<ModuleProcessor> processor;HostDelay dryDelay;Ramp wet;bool bypassed=false;};
    void prepare(double,int);
    std::unique_ptr<Node> makeNode(const juce::String&,double,int);
    std::unique_ptr<Node> exchange(int,std::unique_ptr<Node>);
    ModuleProcessor* processor(int i) const {return slots[size_t(i)]?slots[size_t(i)]->processor.get():nullptr;}
    void bypass(int i,bool b){if(slots[size_t(i)])slots[size_t(i)]->bypassed=b;}
    bool bypassed(int i) const {return slots[size_t(i)]&&slots[size_t(i)]->bypassed;}
    void process(float*,float*,int) noexcept;
    int latency() const {int n=0;for(const auto& p:slots)if(p)n+=p->processor->getLatencySamples();return n;}
private:
    std::array<std::unique_ptr<Node>,8> slots;
    std::array<std::vector<float>,2> dry;
    juce::AudioBuffer<float> view;juce::MidiBuffer midi;
    int maximumBlock=256;
};
class LiveVocalManager final {
public:
    enum class Monitor {Off,DrySoftware,InsertsSoftware,InterfaceDirect};
    void prepare(double,int,int enabledInputCount);
    void release(){recorder.stop();++generation;monitor=Monitor::Off;}
    // info buffer still contains AudioSourcePlayer's compacted active inputs.
    // Call BEFORE clearing/writing the host output region. n <= prepared block.
    void capture(const juce::AudioBuffer<float>&,int start,int n,double timelineSeconds) noexcept;
    void addMonitor(juce::AudioBuffer<float>& master,int n) noexcept;
    bool setInput(int compactChannel){if(recorder.busy()||compactChannel<0||compactChannel>=channels)return false;input.store(compactChannel);return true;}
    int selectedInput() const {return input.load();}
    void setMonitor(Monitor m){monitor.store(m);}
    Monitor monitoring() const {return monitor.load();}
    bool setPrintInserts(bool b){if(recorder.busy())return false;printInserts=b;return true;} // callback excluded, recording idle
    bool printing() const {return printInserts;}
    void setMonitorGain(float db){monitorDb.store(std::clamp(db,-60.f,6.f));}
    float inputPeak() const {return peak.load();}
    int monitorLatency() const {return monitoring()==Monitor::InsertsSoftware?rack.latency():0;}
    bool configureTake(TakeRecorder::Setup&); // callback excluded; then recorder.start(setup) OUTSIDE that lock
    void stopTake(){recorder.stop();}
    TrackingInsertChain rack;TakeRecorder recorder;
private:
    double rate=96000;int channels=0;unsigned generation=0,takeGeneration=0;
    juce::AudioBuffer<float> dry,processed;
    std::atomic<int> input{0};std::atomic<Monitor> monitor{Monitor::Off};
    std::atomic<float> monitorDb{0},peak{0};
    std::vector<Ramp> inputWeights;
    Ramp dryMix,wetMix,level;bool printInserts=false;
};
}

```

## Source/Systems/MonitorProfile.cpp

```cpp
#include "MonitorProfile.h"
#include <stdexcept>
namespace hem {
namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
double numeric(const juce::var& v,double low,double high){require(v.isInt()||v.isInt64()||v.isDouble(),"Calibration parameter must be numeric");double n=v;require(std::isfinite(n)&&n>=low&&n<=high,"Calibration parameter outside supported range");return n;}
const char* filterName(MonitorProfile::Filter t){return t==MonitorProfile::Filter::Bell?"bell":t==MonitorProfile::Filter::LowShelf?"lowShelf":"highShelf";}
}
MonitorProfile MonitorProfile::load(const juce::File& f){
    require(f.existsAsFile()&&f.getSize()>0&&f.getSize()<=65536,"Calibration file missing or larger than 64 KB");
    juce::var root;require(juce::JSON::parse(f.loadFileAsString(),root).wasOk()&&root.isObject(),"Invalid calibration JSON");
    require(numeric(root["version"],1,1)==1,"Unsupported calibration version");MonitorProfile p;
    p.name=root["name"].toString();p.device=root["device"].toString();p.connection=root["connection"].toString();p.deviceMode=root["deviceMode"].toString();p.measurementSource=root["measurementSource"].toString();
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration must identify device, connection, mode and measurement source");
    p.preampDb=float(numeric(root["preampDb"],-36,0));auto list=root["bands"];auto* bands=list.getArray();require(bands&&bands->size()<=MonitoringRouter::maxBands,"Calibration supports up to twelve bands");
    for(const auto& b:*bands){require(b.isObject(),"Invalid calibration band");auto type=b["type"].toString();require(type=="bell"||type=="lowShelf"||type=="highShelf","Unsupported calibration filter");
        p.bands.push_back({type=="bell"?Filter::Bell:type=="lowShelf"?Filter::LowShelf:Filter::HighShelf,float(numeric(b["hz"],10,40000)),float(numeric(b["q"],.1,10)),float(numeric(b["db"],-18,12))});}
    return p;
}
MonitoringRouter::Curve MonitorProfile::prepare(double sampleRate) const {
    require(std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=384000,"Unsupported calibration sample rate");
    require(bands.size()<=MonitoringRouter::maxBands&&std::isfinite(preampDb)&&preampDb>=-36&&preampDb<=0,"Invalid calibration headroom");
    MonitoringRouter::Curve curve;curve.count=int(bands.size());curve.preamp=gain(preampDb);
    for(size_t i=0;i<bands.size();++i){const auto& b=bands[i];require(std::isfinite(b.hz)&&std::isfinite(b.q)&&std::isfinite(b.db)&&b.hz>=10&&b.hz<sampleRate*.49&&b.q>=.1&&b.q<=10&&b.db>=-18&&b.db<=12,"Calibration band invalid for current sample rate");auto& f=curve.filters[i];
        require(b.hz<=40000&&(b.type==Filter::Bell||b.type==Filter::LowShelf||b.type==Filter::HighShelf),"Invalid calibration filter type/frequency");
        // Compute directly: no legacy frequency clamping or fixed shelf Q.
        const double a=std::pow(10.,b.db/40.),w=2*pi*b.hz/sampleRate,c=std::cos(w),alpha=std::sin(w)/(2*b.q),r=2*std::sqrt(a)*alpha;
        double a0=1;
        if(b.type==Filter::Bell){
            f.b0=1+alpha*a;f.b1=-2*c;f.b2=1-alpha*a;a0=1+alpha/a;f.a1=-2*c;f.a2=1-alpha/a;
        }else if(b.type==Filter::LowShelf){
            f.b0=a*((a+1)-(a-1)*c+r);f.b1=2*a*((a-1)-(a+1)*c);f.b2=a*((a+1)-(a-1)*c-r);
            a0=(a+1)+(a-1)*c+r;f.a1=-2*((a-1)+(a+1)*c);f.a2=(a+1)+(a-1)*c-r;
        }else{
            f.b0=a*((a+1)+(a-1)*c+r);f.b1=-2*a*((a-1)+(a+1)*c);f.b2=a*((a+1)+(a-1)*c-r);
            a0=(a+1)-(a-1)*c+r;f.a1=2*((a-1)-(a+1)*c);f.a2=(a+1)-(a-1)*c-r;
        }
        f.b0/=a0;f.b1/=a0;f.b2/=a0;f.a1/=a0;f.a2/=a0;
    }
    return curve;
}
void MonitorProfile::save(const MonitorProfile& p,const juce::File& file){
    p.prepare(96000);auto* object=new juce::DynamicObject();juce::var root(object);object->setProperty("version",1);
    object->setProperty("name",p.name);object->setProperty("device",p.device);object->setProperty("connection",p.connection);object->setProperty("deviceMode",p.deviceMode);object->setProperty("measurementSource",p.measurementSource);object->setProperty("preampDb",p.preampDb);
    require(p.name.isNotEmpty()&&p.device.isNotEmpty()&&p.connection.isNotEmpty()&&p.deviceMode.isNotEmpty()&&p.measurementSource.isNotEmpty(),"Calibration metadata is incomplete");
    juce::Array<juce::var> bands;for(const auto& b:p.bands){auto* v=new juce::DynamicObject();v->setProperty("type",filterName(b.type));v->setProperty("hz",b.hz);v->setProperty("q",b.q);v->setProperty("db",b.db);bands.add(juce::var(v));}object->setProperty("bands",bands);
    juce::TemporaryFile temporary(file);require(temporary.getFile().replaceWithText(juce::JSON::toString(root,false))&&temporary.overwriteTargetFileWithTemporary(),"Cannot save calibration profile");
}
MonitorProfileLoader::MonitorProfileLoader(Ready r,Error e):juce::Thread("HEM calibration loader"),ready(std::move(r)),errorHandler(std::move(e)){startTimerHz(15);}
MonitorProfileLoader::~MonitorProfileLoader(){stopTimer();signalThreadShouldExit();waitForThreadToExit(-1);}
bool MonitorProfileLoader::load(const juce::File& f,double sr,unsigned gen){if(busy.load())return false;file=f;rate=sr;generation=gen;error.clear();done=false;busy=true;if(!startThread()){busy=false;errorHandler("Cannot start calibration worker");return false;}return true;}
void MonitorProfileLoader::run(){try{profile=MonitorProfile::load(file);curve=profile.prepare(rate);}catch(const std::exception& e){error=e.what();}done.store(true,std::memory_order_release);}
void MonitorProfileLoader::timerCallback(){if(!done.load(std::memory_order_acquire)||isThreadRunning())return;done=false;busy=false;if(error.isNotEmpty())errorHandler(error);else ready(profile,curve,generation);}
}

```

## Source/Systems/MonitorProfile.h

```cpp
#pragma once
#include <juce_events/juce_events.h>
#include "../DSP/MonitoringRouter.h"
namespace hem {
struct MonitorProfile {
    enum class Filter {Bell,LowShelf,HighShelf};
    struct Band {Filter type=Filter::Bell;float hz=1000,q=.7071f,db=0;};
    juce::String name,device,connection,deviceMode,measurementSource;
    float preampDb=0;
    std::vector<Band> bands;
    static MonitorProfile load(const juce::File&);
    static void save(const MonitorProfile&,const juce::File&);
    MonitoringRouter::Curve prepare(double sampleRate) const;
};
// JSON I/O and coefficient generation stay away from GUI/audio callbacks.
class MonitorProfileLoader final : private juce::Thread,private juce::Timer {
public:
    using Ready=std::function<void(const MonitorProfile&,const MonitoringRouter::Curve&,unsigned)>;
    using Error=std::function<void(const juce::String&)>;
    MonitorProfileLoader(Ready,Error);~MonitorProfileLoader() override;
    bool load(const juce::File&,double sampleRate,unsigned deviceGeneration);
    bool isBusy() const noexcept {return busy.load();}
private:
    void run() override;void timerCallback() override;
    Ready ready;Error errorHandler;
    juce::File file;double rate=96000;unsigned generation=0;
    MonitorProfile profile;MonitoringRouter::Curve curve;juce::String error;
    std::atomic<bool> busy{false},done{false};
};
}

```

## Source/Systems/NoReplaceFile.h

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <filesystem>

namespace hem::noReplaceFile {

inline std::filesystem::path nativePath(const juce::File& file) {
    return std::filesystem::u8path(file.getFullPathName().toStdString());
}

// This is only a preflight check, never permission to replace the name later.
// Unlike File::exists(), symlink_status detects a dangling symlink as occupied.
inline bool available(const juce::File& destination, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(nativePath(destination), error);
    if (error == std::errc::no_such_file_or_directory) error.clear();
    return !error && status.type() == std::filesystem::file_type::not_found;
}

// The filesystem atomically creates the destination name or fails if ANY
// entry already owns it. Never fall back to JUCE move/replace, rename, or copy:
// those can replace another writer's file. The closed sibling source remains
// intact on both success and failure; its owner removes it only after success.
// Filesystems without hard-link support fail safely with the source retained.
inline std::error_code publish(const juce::File& source, const juce::File& destination) {
    std::error_code error;
    std::filesystem::create_hard_link(nativePath(source), nativePath(destination), error);
    return error;
}

} // namespace hem::noReplaceFile

```

## Source/Systems/OfflineMixPanel.h

```cpp
#pragma once
#include "OfflineMixWorker.h"
#include "../HardwareLookAndFeel.h"

namespace hem {
class OfflineMixPanel final : public juce::Component, private juce::Timer {
public:
    using Start = std::function<juce::Result(const juce::File&, const OfflineMixOptions&)>;
    OfflineMixPanel(OfflineMixWorker& worker, Start start) : renderer(worker), startExport(std::move(start)) {
        setLookAndFeel(&look);
        for (auto* c : std::array<juce::Component*,6>{&heading, &description, &tail, &choose, &cancel, &status}) addAndMakeVisible(*c);
        heading.setText("OFFLINE STEREO MIX / 48 kHz / FLOAT32 WAV", juce::dontSendNotification);
        description.setText("Exports from timeline zero through the longest loaded stem, plus the selected tail.\nIncludes track inserts, mute/solo, track gain, input/output gain and the current master limiter.\nListening EQ, headphone correction and live microphone monitoring are excluded.\nLooping and playback position do not change the export. The session is copied at export start.\nTrue-peak certification remains pending for the current limiter.", juce::dontSendNotification);
        tail.setName("Export tail seconds"); tail.setSliderStyle(juce::Slider::LinearHorizontal);
        tail.setRange(0,30,0.1); tail.setValue(2); tail.setTextBoxStyle(juce::Slider::TextBoxRight,false,110,28);
        tail.setTextValueSuffix(" s tail"); tail.setTooltip("Additional audio after the longest stem; increase for long effect tails");
        choose.onClick=[this] { chooseDestination(); };
        cancel.onClick=[this] { renderer.cancel(); status.setText("Cancelling safely...", juce::dontSendNotification); };
        status.setText(renderer.result(),juce::dontSendNotification); startTimerHz(20); setSize(850,430);
    }
    ~OfflineMixPanel() override { stopTimer(); picker.reset(); setLookAndFeel(nullptr); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff08121a)); }
    void resized() override {
        auto area=getLocalBounds().reduced(24); heading.setBounds(area.removeFromTop(38));
        description.setBounds(area.removeFromTop(155)); tail.setBounds(area.removeFromTop(44));
        auto row=area.removeFromTop(48); choose.setBounds(row.removeFromLeft(row.getWidth()*2/3).reduced(4)); cancel.setBounds(row.reduced(4));
        status.setBounds(area.reduced(4));
    }
private:
    void chooseDestination() {
        if (renderer.isBusy()) return;
        picker=std::make_unique<juce::FileChooser>("Export stereo mix to a NEW WAV file",juce::File(),"*.wav");
        juce::Component::SafePointer<OfflineMixPanel> safe(this);
        picker->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& chooser) {
                if (!safe || chooser.getResult()==juce::File()) return;
                const auto destination=chooser.getResult().withFileExtension("wav");
                if(destination.exists()) { safe->status.setText("Choose a new filename; existing audio is never overwritten",juce::dontSendNotification); return; }
                OfflineMixOptions options; options.tailSeconds=safe->tail.getValue();
                const auto result=safe->startExport(destination,options);
                safe->status.setText(result.wasOk()?"Rendering frozen mix snapshot":result.getErrorMessage(),juce::dontSendNotification);
            });
    }
    void timerCallback() override {
        const bool busy=renderer.isBusy(); choose.setEnabled(!busy); tail.setEnabled(!busy); cancel.setEnabled(busy);
        if (busy) status.setText("Export " + juce::String(renderer.progress()*100,1) + "% / live session remains unchanged",juce::dontSendNotification);
        else if (wasBusy) status.setText(renderer.result(),juce::dontSendNotification);
        wasBusy=busy;
    }
    HardwareLookAndFeel look;
    OfflineMixWorker& renderer;
    Start startExport;
    juce::Label heading,description,status;
    juce::Slider tail;
    juce::TextButton choose{"CHOOSE NEW FILE / EXPORT"},cancel{"CANCEL EXPORT"};
    std::unique_ptr<juce::FileChooser> picker;
    bool wasBusy=false;
};
}

```

## Source/Systems/OfflineMixRenderer.cpp

```cpp
#include "OfflineMixRenderer.h"
#include "../CoreMasterStage.h"
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace hem {
namespace {

// Leave ample room for JUCE's RIFF/JUNK/fmt/data headers. Never silently switch
// to RF64 or wrap a classic 32-bit RIFF/data length.
constexpr juce::int64 maximumDataBytes = 0xff000000LL;
constexpr juce::int64 maximumFrames = maximumDataBytes / (2 * sizeof(float));

[[noreturn]] void fail(const juce::String& message) {
    throw std::runtime_error(message.toStdString());
}

void require(bool condition, const juce::String& message) {
    if (!condition) fail(message);
}

void validateGain(float db, const juce::String& label) {
    require(std::isfinite(db) && db >= -60.f && db <= 6.f,
            label + " must be finite and within -60 to +6 dB");
}

void validateSamples(const juce::AudioBuffer<float>& buffer, int start, int count,
                     const juce::String& label) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const auto* samples = buffer.getReadPointer(channel, start);
        for (int i = 0; i < count; ++i)
            if (!std::isfinite(samples[i])) fail("Non-finite audio in " + label);
    }
}

juce::int64 boundedCeil(long double frames, const juce::String& label) {
    require(std::isfinite(frames) && frames >= 0
            && frames <= static_cast<long double>(maximumFrames),
            label + " exceeds classic stereo float WAV capacity");
    return static_cast<juce::int64>(std::ceil(frames));
}

// Unlike AudioTransportSource, this adapter never starts a read-ahead thread,
// never has a transport fade, and propagates a decoder read failure. JUCE's
// reader duplicates mono into both destination channels. The resampler calls
// this synchronously on the export worker and sees explicit zeros after EOF.
class SynchronousWavSource final : public juce::AudioSource {
public:
    explicit SynchronousWavSource(const juce::File& file) : name(file.getFileName()) {
        require(file.existsAsFile(), "Missing source: " + file.getFullPathName());
        auto stream = file.createInputStream();
        require(stream != nullptr && stream->getStatus().wasOk(), "Cannot open source: " + name);
        juce::WavAudioFormat wav;
        reader.reset(wav.createReaderFor(stream.release(), true));
        require(reader != nullptr, "Cannot decode WAV: " + name);
        require(reader->lengthInSamples > 0 && reader->numChannels >= 1 && reader->numChannels <= 2,
                "Use a non-empty mono or stereo WAV: " + name);
        require(std::isfinite(reader->sampleRate) && reader->sampleRate >= 8000
                && reader->sampleRate <= 384000, "Unsupported source sample rate: " + name);
    }

    double sampleRate() const noexcept { return reader->sampleRate; }
    juce::int64 length() const noexcept { return reader->lengthInSamples; }
    void prepareToPlay(int, double) override { position = 0; }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        info.clearActiveBufferRegion();
        const auto remaining = std::max<juce::int64>(0, length() - position);
        const int count = static_cast<int>(std::min<juce::int64>(info.numSamples, remaining));
        if (count > 0) {
            require(reader->read(info.buffer, info.startSample, count, position, true, true),
                    "Source read failed: " + name);
            validateSamples(*info.buffer, info.startSample, count, name);
            position += count;
        }
    }
private:
    juce::String name;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 position = 0;
};

struct OfflineTrack {
    // Reverse destruction keeps the resampler's borrowed source valid.
    std::unique_ptr<SynchronousWavSource> source;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    InsertChain chain;
    float gain = 0;
    ~OfflineTrack() {
        if (resampler) resampler->releaseResources();
        chain.release();
    }
};

// All readers, resamplers, prepared nodes and buffers are private to this render.
// No live TrackEngine or live mic/monitor object can enter the export graph.
struct SourceSnapshot {
    std::array<OfflineTrack, 13> tracks;
    juce::int64 sourceFrames = 0;
};

std::filesystem::path nativePath(const juce::File& file) {
    return std::filesystem::u8path(file.getFullPathName().toStdString());
}

void ensureNoDestination(const juce::File& destination) {
    std::error_code error;
    // symlink_status also recognises a dangling symlink, which must never be
    // treated as a free destination. Atomic publication repeats this check in
    // the filesystem, so a race cannot overwrite another writer's file.
    const auto status = std::filesystem::symlink_status(nativePath(destination), error);
    require(!error || error == std::errc::no_such_file_or_directory,
            "Cannot inspect export destination: " + juce::String(error.message()));
    require(status.type() == std::filesystem::file_type::not_found,
            "Export destination already exists; choose a new name");
}

void verifyCompletedWav(const juce::File& file, double rate, juce::int64 frames) {
    require(file.getSize() >= frames * 8 && file.getSize() <= maximumDataBytes + 65536,
            "Export size validation failed");
    auto stream = file.createInputStream();
    require(stream != nullptr && stream->getStatus().wasOk(), "Cannot verify export WAV");
    require(stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("RIFF")),
            "Export is not a classic RIFF WAV");
    stream->setPosition(0);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    require(reader && reader->numChannels == 2 && reader->bitsPerSample == 32
            && reader->usesFloatingPointData && reader->sampleRate == rate
            && reader->lengthInSamples == frames, "Export WAV header verification failed");
}

} // namespace

OfflineMixResult OfflineMixRenderer::render(const SessionSnapshot& snapshot,
                                           const juce::File& destination,
                                           const OfflineMixOptions& options,
                                           const std::function<bool()>& cancelled,
                                           const std::function<void(double)>& progress,
                                           OfflinePreparedPlugins* preparedExternal) {
    OfflineMixResult result;
    try {
        juce::ScopedNoDenormals noDenormals;
        const auto checkCancellation = [&] {
            if (cancelled && cancelled()) fail("Offline mix cancelled");
        };
        // Progress is informational. A UI callback must not invalidate a
        // successfully published file by throwing after the commit point.
        const auto reportProgress = [&](double amount) noexcept {
            if (progress) { try { progress(amount); } catch (...) {} }
        };
        require(std::isfinite(options.sampleRate) && options.sampleRate >= 8000
                && options.sampleRate <= 384000 && std::floor(options.sampleRate) == options.sampleRate,
                "Export sample rate must be an integer from 8000 to 384000 Hz");
        require(options.blockSize >= 1 && options.blockSize <= 8192,
                "Export block size must be from 1 to 8192 samples");
        require(std::isfinite(options.tailSeconds) && options.tailSeconds >= 0 && options.tailSeconds <= 3600,
                "Export tail must be finite and from 0 to 3600 seconds");
        require(destination != juce::File() && destination.getParentDirectory().isDirectory(),
                "Choose an export destination in an existing directory");
        ensureNoDestination(destination);
        validateGain(snapshot.inputDb, "Input gain");
        validateGain(snapshot.outputDb, "Output gain");
        checkCancellation();
        reportProgress(0);

        SourceSnapshot sources;
        const bool anySolo = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(),
                                        [](const auto& track) { return track.solo; });
        const float inputGain = gain(snapshot.inputDb);
        const float outputGain = gain(snapshot.outputDb);
        bool loaded = false;
        for (size_t i = 0; i < snapshot.tracks.size(); ++i) {
            checkCancellation();
            const auto& saved = snapshot.tracks[i];
            auto& track = sources.tracks[i];
            const auto label = "Track " + juce::String(static_cast<int>(i) + 1);
            validateGain(saved.gainDb, label + " gain");
            track.gain = !saved.muted && (!anySolo || saved.solo) ? gain(saved.gainDb) : 0.f;
            track.chain.prepare(options.sampleRate, options.blockSize);
            if (saved.source != juce::File()) {
                track.source = std::make_unique<SynchronousWavSource>(saved.source);
                const auto length = boundedCeil(static_cast<long double>(track.source->length())
                                                * options.sampleRate / track.source->sampleRate(), label);
                sources.sourceFrames = std::max(sources.sourceFrames, length);
                track.resampler = std::make_unique<juce::ResamplingAudioSource>(track.source.get(), false, 2);
                track.resampler->setResamplingRatio(track.source->sampleRate() / options.sampleRate);
                track.resampler->prepareToPlay(options.blockSize, options.sampleRate);
                loaded = true;
            }
            for (int slotIndex = 0; slotIndex < InsertChain::slotCount; ++slotIndex) {
                checkCancellation();
                const auto& slot = saved.slots[static_cast<size_t>(slotIndex)];
                require(slot.parameters.hasType("PARAMETERS"), label + " has invalid insert parameters");
                std::unique_ptr<ModuleProcessor> processor;
                if (slot.pluginId.isEmpty()) {
                    require(slot.parameters.getNumChildren() == 0, label + " empty insert has parameters");
                } else if (slot.pluginId == "external-vst3") {
                    require(preparedExternal != nullptr, "External VST3 export requires message-thread staging");
                    processor = std::move(preparedExternal->slots[i][static_cast<size_t>(slotIndex)]);
                    require(processor && processor->isExternal() && processor->processingReady(),
                            label + " external VST3 is not prepared or was invalidated");
                } else {
                    const auto& registry = PluginRegistry::entries();
                    const auto entry = std::find_if(registry.begin(), registry.end(),
                                                   [&](const auto& e) { return slot.pluginId == e.id; });
                    require(entry != registry.end() && !entry->monitoringOnly,
                            label + " has an unknown or monitoring-only insert: " + slot.pluginId);
                    processor = entry->create();
                    require(processor != nullptr && !processor->monitorOnly(), "Cannot create insert: " + slot.pluginId);
                    // Capture owns an independent value tree. Copy once more
                    // so only this export owns the tree used for restoration.
                    SessionState::restoreParameters(*processor, slot.parameters.createCopy());
                    processor->setNonRealtime(true);
                }
                auto node = track.chain.stageNode(std::move(processor), options.sampleRate,
                                                 options.blockSize, track.chain.slotLatency());
                track.chain.installInitial(slotIndex, std::move(node), slot.bypassed);
            }
        }
        require(loaded, "Load at least one non-empty WAV stem before exporting");
        // Keep the user-facing double's multiplication in double precision:
        // promoting .0125 before multiplying by 48000 preserves a tiny binary
        // representation residue and would incorrectly request 601, not 600.
        const auto tailFrames = boundedCeil(options.tailSeconds * options.sampleRate,
                                           "Export tail");
        require(sources.sourceFrames <= maximumFrames - tailFrames,
                "Mix plus tail exceeds classic stereo float WAV capacity");
        const auto frames = sources.sourceFrames + tailFrames;
        CoreMasterStage master;
        master.prepare(options.sampleRate, options.blockSize);
        const int latency = sources.tracks.front().chain.latencySamples() + master.latencySamples();
        const auto processingFrames = frames + latency;
        juce::AudioBuffer<float> mix(2, options.blockSize), scratch(2, options.blockSize);

        checkCancellation();
        juce::TemporaryFile temporary(destination);
        require(!temporary.getFile().exists(), "Temporary export path is already in use");
        std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
        require(stream != nullptr, "Cannot create temporary export WAV");
        auto* fileStream = dynamic_cast<juce::FileOutputStream*>(stream.get());
        require(fileStream && fileStream->getStatus().wasOk(), "Cannot open export stream");
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream,
            juce::AudioFormatWriterOptions().withSampleRate(options.sampleRate).withNumChannels(2)
                .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        require(writer != nullptr, "Cannot create stereo float32 WAV writer");
        juce::int64 written = 0;
        for (juce::int64 position = 0; position < processingFrames;) {
            checkCancellation();
            const int count = static_cast<int>(std::min<juce::int64>(options.blockSize, processingFrames - position));
            mix.clear();
            for (auto& track : sources.tracks) {
                checkCancellation();
                scratch.clear();
                if (track.resampler)
                    track.resampler->getNextAudioBlock({&scratch, 0, count});
                scratch.applyGain(0, count, inputGain);
                track.chain.process(scratch.getWritePointer(0), scratch.getWritePointer(1), count);
                for(int slot=0;slot<InsertChain::slotCount;++slot)
                    if(auto* processor=track.chain.activeProcessor(slot);processor && processor->isExternal())
                        require(processor->processingReady(), "External VST3 changed latency/layout or failed during export; no final file published");
                validateSamples(scratch, 0, count, "track insert output");
                if (track.gain != 0.f)
                    for (int channel = 0; channel < 2; ++channel)
                        mix.addFrom(channel, 0, scratch, channel, 0, count, track.gain);
            }
            mix.applyGain(0, count, outputGain);
            validateSamples(mix, 0, count, "master input");
            master.process(mix.getWritePointer(0), mix.getWritePointer(1), count);
            validateSamples(mix, 0, count, "master output");
            const int skip = static_cast<int>(juce::jlimit<juce::int64>(0, count, latency - position));
            if (skip < count) {
                require(writer->writeFromAudioSampleBuffer(mix, skip, count - skip), "Export disk write failed");
                written += count - skip;
            }
            position += count;
            // Reserve the final 1% for flush, verification and publication.
            reportProgress(0.99 * static_cast<double>(position) / static_cast<double>(processingFrames));
        }
        require(written == frames, "Export frame count mismatch");
        checkCancellation();
        require(writer->flush(), "Cannot finalize export WAV header");
        fileStream->flush();
        require(fileStream->getStatus().wasOk(), "Export flush failed");
        writer.reset();
        verifyCompletedWav(temporary.getFile(), options.sampleRate, frames);
        checkCancellation();
        // create_hard_link is an atomic no-replace operation even if another
        // process creates destination between validation and this commit.
        std::error_code error;
        std::filesystem::create_hard_link(nativePath(temporary.getFile()), nativePath(destination), error);
        // Do not allocate/build an error string after a successful commit.
        // Everything after publication is noexcept/informational.
        if (error)
            fail("Cannot publish export without overwriting; destination may exist or this filesystem may not support hard links: "
                 + juce::String(error.message()));
        result.frames = frames;
        result.latencyRemoved = latency;
        reportProgress(1);
    } catch (const std::exception& exception) {
        result.status = juce::Result::fail(juce::String(exception.what()));
    } catch (...) {
        result.status = juce::Result::fail("Unknown offline mix failure");
    }
    return result;
}

} // namespace hem

```

## Source/Systems/OfflineMixRenderer.h

```cpp
#pragma once
#include "../SessionState.h"
#include <functional>

namespace hem {

struct OfflineMixOptions {
    double sampleRate = 48000;
    int blockSize = 256;
    double tailSeconds = 2;
};

// Created on the message thread; adapters may retire on workers because the
// ExternalPluginHost retains the actual VST3 owners until main-thread collection.
struct OfflinePreparedPlugins {
    std::array<std::array<std::unique_ptr<ModuleProcessor>,8>,13> slots;
};

struct OfflineMixResult {
    juce::Result status = juce::Result::ok();
    // Set only after the complete file has been atomically published.
    juce::int64 frames = 0;
    int latencyRemoved = 0;
};

// Synchronous worker-thread operation. No audio device or read-ahead thread is
// used. The caller must pass an independently captured, immutable snapshot.
// Callbacks run on the calling worker, never on the message/audio thread.
//
// The export starts at zero, ignores transport position/looping and all monitor
// state, and ends at ceil(longest source duration * rate) + ceil(tail * rate).
// Input trim -> 13 stereo 8-slot chains -> track gain/mute/solo -> sum -> output
// gain -> unchanged CoreMasterStage. Fixed insert + actual master latency is
// removed; processing continues through that latency and the requested tail.
//
// A same-directory temporary WAV is flushed and checked before an atomic,
// no-replace hard-link publication. Existing destinations are never replaced.
// Cancellation/failure removes the temporary and leaves no final output.
// A filesystem without hard-link support is rejected safely.
class OfflineMixRenderer final {
public:
    static OfflineMixResult render(
        const SessionSnapshot&, const juce::File&, const OfflineMixOptions&,
        const std::function<bool()>& cancelled = {},
        const std::function<void(double)>& progress = {},
        OfflinePreparedPlugins* preparedExternal = nullptr);
};

} // namespace hem

```

## Source/Systems/OfflineMixWorker.h

```cpp
#pragma once
#include "OfflineMixRenderer.h"
#include "../Plugins/ExternalPluginHost.h"

namespace hem {
// Owns a frozen snapshot. External lifecycle staging happens on the message
// timer, one instance per tick. Render/file work and adapter retirement stay on
// the worker; actual VST3 owners remain in ExternalPluginHost until main collects.
class OfflineMixWorker final : private juce::Thread, private juce::Timer {
public:
    using Report = std::function<void(const juce::String&)>;
    explicit OfflineMixWorker(Report report,ExternalPluginHost* host=nullptr)
        : juce::Thread("HEM offline mix"), reportResult(std::move(report)), externalHost(host) { startTimerHz(20); }
    ~OfflineMixWorker() override { stopTimer(); cancel(); waitForThreadToExit(-1); }
    bool start(SessionSnapshot state, juce::File destination, OfflineMixOptions settings) {
        if (isBusy()) return false;
        // External preparation precedes the renderer's validation. Reject bad
        // values here before converting the rate-derived latency to an integer.
        if(!std::isfinite(settings.sampleRate)||settings.sampleRate<8000||settings.sampleRate>384000
           ||std::floor(settings.sampleRate)!=settings.sampleRate||settings.blockSize<1||settings.blockSize>8192
           ||!std::isfinite(settings.tailSeconds)||settings.tailSeconds<0||settings.tailSeconds>3600){
            outcome="Invalid export rate, block size or tail; no plug-in was prepared";return false;
        }
        bool hasExternal=false;
        for (auto& track : state.tracks)
            for (auto& slot : track.slots) {slot.parameters=slot.parameters.createCopy();hasExternal|=slot.pluginId=="external-vst3";}
        if(hasExternal&&!externalHost){outcome="External VST3 export requires a message-thread host";return false;}
        state.monitorParameters = state.monitorParameters.createCopy();
        snapshot = std::move(state); file = std::move(destination); options = settings;
        preparedExternal=std::make_unique<OfflinePreparedPlugins>();nextExternal=0;preparingExternal=hasExternal;
        cancelRequested=false;progressValue.store(0); finished.store(false); busy.store(true);
        outcome = hasExternal?"Preparing private VST3 export instances / cancel available":"Rendering frozen mix snapshot";
        if(!preparingExternal&&!startThread()){busy=false;outcome="Cannot start export worker";return false;}
        return true;
    }
    void cancel() noexcept { cancelRequested=true;signalThreadShouldExit(); }
    bool isBusy() const noexcept { return busy.load(std::memory_order_acquire); }
    double progress() const noexcept { return progressValue.load(std::memory_order_relaxed); }
    const juce::String& result() const noexcept { return outcome; }
private:
    void run() override {
        rendered = OfflineMixRenderer::render(snapshot, file, options,
            [this] { return cancelRequested.load()||threadShouldExit(); },
            [this](double value) { progressValue.store(value, std::memory_order_relaxed); },preparedExternal.get());
        finished.store(true, std::memory_order_release);
    }
    void finishStaging(const juce::String& error){
        preparingExternal=false;preparedExternal.reset();busy=false;outcome="Export unchanged: "+error;
        if(externalHost)externalHost->collectGarbage();if(reportResult)reportResult(outcome);
    }
    void timerCallback() override {
        if(preparingExternal){
            if(cancelRequested.load()){finishStaging("Export cancelled before processing");return;}
            while(nextExternal<13*8){const int index=nextExternal++;const auto& slot=snapshot.tracks[size_t(index/8)].slots[size_t(index%8)];
                if(slot.pluginId!="external-vst3")continue;
                juce::String error;
                auto processor=externalHost->stage(slot.external,options.sampleRate,options.blockSize,int(std::ceil(options.sampleRate*.006)),true,error);
                if(!processor){finishStaging(error);return;}
                preparedExternal->slots[size_t(index/8)][size_t(index%8)]=std::move(processor);
                return; // Permit UI cancellation between potentially slow vendor instances.
            }
            preparingExternal=false;
            if(cancelRequested.load()){finishStaging("Export cancelled before processing");return;}
            outcome="Rendering frozen mix snapshot";
            if(!startThread()){finishStaging("Cannot start export worker");return;}
        }
        if (!finished.load(std::memory_order_acquire) || isThreadRunning()) return;
        finished.store(false); busy.store(false);preparedExternal.reset();
        if(externalHost)externalHost->collectGarbage();
        outcome = rendered.status.wasOk()
            ? "Saved " + file.getFileName() + " / " + juce::String(rendered.frames) + " frames / " + juce::String(rendered.latencyRemoved) + " latency samples removed"
            : "Export unchanged: " + rendered.status.getErrorMessage();
        if (reportResult) reportResult(outcome);
    }
    Report reportResult;
    ExternalPluginHost* externalHost=nullptr;
    std::unique_ptr<OfflinePreparedPlugins> preparedExternal;
    SessionSnapshot snapshot;
    juce::File file;
    OfflineMixOptions options;
    OfflineMixResult rendered{juce::Result::ok()};
    juce::String outcome{"Ready to export"};
    bool preparingExternal=false;int nextExternal=0;
    std::atomic<bool> busy{false}, finished{false},cancelRequested{false};
    std::atomic<double> progressValue{0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineMixWorker)
};
} // namespace hem

```

## Source/Systems/PresetManager.cpp

```cpp
#include "PresetManager.h"
#include <stdexcept>
namespace hem {
namespace {void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}}
bool PresetManager::supported(const juce::File& f){return f.hasFileExtension("xml;json");}
juce::var PresetManager::toJson(const juce::ValueTree& tree){
    auto* object=new juce::DynamicObject();juce::var result(object);
    object->setProperty("type",tree.getType().toString());
    auto* properties=new juce::DynamicObject();object->setProperty("properties",juce::var(properties));
    for(int i=0;i<tree.getNumProperties();++i){auto name=tree.getPropertyName(i);properties->setProperty(name,tree[name]);}
    juce::Array<juce::var> children;for(auto child:tree)children.add(toJson(child));
    object->setProperty("children",children);return result;
}
juce::ValueTree PresetManager::fromJson(const juce::var& value,int depth){
    check(depth<=6,"Preset JSON nesting is too deep");auto* object=value.getDynamicObject();check(object!=nullptr,"Preset JSON node must be an object");
    check(object->getProperties().size()==3&&object->hasProperty("type")&&object->hasProperty("properties")&&object->hasProperty("children"),"Invalid preset JSON node fields");
    check(object->getProperty("type").isString(),"Preset JSON node type must be a string");
    auto type=object->getProperty("type").toString();
    check(type=="HEM_SESSION"||type=="MONITOR"||type=="TRACK"||type=="SLOT"||type=="PARAMETERS"||type=="PARAM"||type=="EXTERNAL","Unknown preset JSON node");
    auto* properties=object->getProperty("properties").getDynamicObject();check(properties!=nullptr,"Preset JSON properties missing");
    auto children=object->getProperty("children");auto* array=children.getArray();check(array!=nullptr&&array->size()<=128,"Invalid preset JSON children");
    check(properties->getProperties().size()<=32,"Too many preset properties");
    juce::ValueTree result{juce::Identifier(type)};
    for(const auto& entry:properties->getProperties()){
        const auto& v=entry.value;check(v.isString()||v.isInt()||v.isInt64()||v.isDouble()||v.isBool(),"Preset property is not a scalar");
        check(!v.isDouble()||std::isfinite(double(v)),"Preset contains non-finite value");result.setProperty(entry.name,v,nullptr);
    }
    for(const auto& child:*array)result.addChild(fromJson(child,depth+1),-1,nullptr);
    return result;
}
void PresetManager::save(const SessionSnapshot& snapshot,const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    if(file.hasFileExtension("xml")){SessionState::save(snapshot,file);return;}
    const auto text=juce::JSON::toString(toJson(SessionState::encode(snapshot,file)),false);
    check(text.getNumBytesAsUTF8()<=SessionState::maxSerializedBytes,"Preset JSON is larger than 8 MB");
    juce::TemporaryFile temporary(file);
    check(temporary.getFile().replaceWithText(text,false,false,nullptr),"Cannot write preset JSON");
    check(temporary.getFile().getSize()==juce::int64(text.getNumBytesAsUTF8()),"Preset JSON temporary file size mismatch");
    check(temporary.overwriteTargetFileWithTemporary(),"Cannot replace preset file");
}
SessionSnapshot PresetManager::load(const juce::File& file){
    check(supported(file),"Use an .xml or .json preset extension");
    SessionSnapshot snapshot;
    if(file.hasFileExtension("xml"))snapshot=SessionState::load(file);
    else {
        check(file.existsAsFile()&&file.getSize()>0&&file.getSize()<=juce::int64(SessionState::maxSerializedBytes),"Preset missing, empty or larger than 8 MB");
        juce::var document;auto parsed=juce::JSON::parse(file.loadFileAsString(),document);
        check(parsed.wasOk(),"Malformed JSON preset");snapshot=SessionState::decode(fromJson(document),file);
    }
    // Both formats reject invalid plugin parameter states before any host commit.
    for(const auto& t:snapshot.tracks)for(const auto& s:t.slots)if(s.pluginId.isNotEmpty()){
        // decode() already validates the opaque external container. Recreating
        // plug-ins here would execute third-party code on the session worker.
        if(s.pluginId==SessionState::externalPluginId)continue;
        auto processor=PluginRegistry::create(s.pluginId);check(processor!=nullptr,"Unknown preset plugin");
        SessionState::restoreParameters(*processor,s.parameters);
    }
    MonitorProcessor monitor;SessionState::restoreParameters(monitor,snapshot.monitorParameters);
    return snapshot;
}
juce::Array<juce::File> PresetManager::list(const juce::File& directory){
    juce::Array<juce::File> result;
    for(const auto& file:directory.findChildFiles(juce::File::findFiles,false))if(supported(file))result.add(file);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.getFileName().compareNatural(b.getFileName())<0;});return result;
}
juce::Result PresetManager::rename(const juce::File& file,const juce::String& name){
    if(!supported(file)||!file.existsAsFile())return juce::Result::fail("Preset file not found");
    if(name.isEmpty()||name!=juce::File::createLegalFileName(name)||name=="."||name=="..")return juce::Result::fail("Invalid preset name");
    const auto target=file.getSiblingFile(name+file.getFileExtension());
    if(target==file)return juce::Result::ok();
    if(target.exists())return juce::Result::fail("A preset with that name already exists");
    return file.moveFileTo(target)?juce::Result::ok():juce::Result::fail("Could not rename preset");
}
juce::Result PresetManager::remove(const juce::File& file){
    if(!supported(file)||!file.existsAsFile())return juce::Result::fail("Preset file not found");
    return file.deleteFile()?juce::Result::ok():juce::Result::fail("Could not delete preset");
}
}

```

## Source/Systems/PresetManager.h

```cpp
#pragma once
#include "../SessionState.h"

namespace hem {
// Disk operations run on SessionWorker (or another non-audio worker).
// XML and JSON represent the SAME validated 13 x 8 session schema.
class PresetManager final {
public:
    static void save(const SessionSnapshot&,const juce::File&);
    static SessionSnapshot load(const juce::File&);
    static juce::Array<juce::File> list(const juce::File& directory);
    static juce::Result rename(const juce::File&,const juce::String& newBaseName);
    static juce::Result remove(const juce::File&);
    static juce::var toJson(const juce::ValueTree&);
    static juce::ValueTree fromJson(const juce::var&,int depth=0);
private:
    static bool supported(const juce::File&);
};
}

```

## Source/Systems/TakeRecorder.cpp

```cpp
#include "TakeRecorder.h"
#include "NoReplaceFile.h"
#include <stdexcept>
namespace hem {
TakeRecorder::TakeRecorder():juce::Thread("HEM vocal take writer"){}
TakeRecorder::~TakeRecorder(){stop();waitForThreadToExit(-1);}
bool TakeRecorder::start(const Setup& s){
    if(busy())return false;
    if(!std::isfinite(s.rate)||s.rate<8000||s.rate>384000||s.channels<1||s.channels>2||s.file==juce::File()||!std::isfinite(s.manualOffsetMs))return false;
    setup=s;outcome.clear();partial=juce::File();fifo.reset();written=0;invalidSamples=0;firstTimeline=-1;overflow=false;stopRequested=false;status=State::Opening;
    if(!startThread()){outcome="Could not start recording worker";status=State::Failed;return false;}return true;
}
void TakeRecorder::stop() noexcept {stopRequested.store(true,std::memory_order_release);}
void TakeRecorder::push(const float* l,const float* r,int count,double timeline) noexcept {
    if(stopRequested.load(std::memory_order_acquire))return;
    auto s=state();if(s!=State::Armed&&s!=State::Recording)return;
    if(s==State::Armed){firstTimeline.store(timeline);status.store(State::Recording,std::memory_order_release);}
    for(int offset=0;offset<count;offset+=packetFrames){
        const int n=std::min(packetFrames,count-offset);int a,n1,b,n2;fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1+n2==0){overflow.store(true);stopRequested.store(true,std::memory_order_release);return;}
        auto& p=packets[size_t(n1?a:b)];p.frames=n;
        for(int i=0;i<n;++i){auto clean=[this](float x){if(std::isfinite(x))return x;invalidSamples.fetch_add(1,std::memory_order_relaxed);return 0.f;};p.l[size_t(i)]=clean(l[offset+i]);if(setup.channels==2)p.r[size_t(i)]=clean(r[offset+i]);}
        fifo.finishedWrite(1);
    }
}
juce::String TakeRecorder::result() const {return busy()?juce::String("Recording / finalizing"):outcome;}
void TakeRecorder::run(){
    bool wavPublished=false;
    juce::File metadataPartial;
    try{
        std::error_code inspectError;
        if(!noReplaceFile::available(setup.file,inspectError)){
            if(inspectError)throw std::runtime_error("Cannot inspect take destination: "+inspectError.message());
            throw std::runtime_error("Take destination already exists; choose a new name");
        }
        partial=setup.file.getSiblingFile(setup.file.getFileNameWithoutExtension()+".partial-"+juce::Uuid().toString()+".wav");
        if(!noReplaceFile::available(partial,inspectError))throw std::runtime_error("Temporary take path is already in use or inaccessible");
        std::unique_ptr<juce::OutputStream> stream=partial.createOutputStream();if(!stream)throw std::runtime_error("Cannot create take file");
        auto* fileStream=dynamic_cast<juce::FileOutputStream*>(stream.get());
        juce::WavAudioFormat wav;
        auto options=juce::AudioFormatWriterOptions().withSampleRate(setup.rate).withNumChannels(setup.channels).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer=wav.createWriterFor(stream,options);if(!writer)throw std::runtime_error("Cannot create float WAV writer");
        status.store(State::Armed,std::memory_order_release);
        bool diskError=false;
        while(true){
            int a,n1,b,n2;fifo.prepareToRead(1,a,n1,b,n2);
            if(n1+n2){const auto& p=packets[size_t(n1?a:b)];const float* channels[]{p.l.data(),p.r.data()};
                if(!writer->writeFromFloatArrays(channels,setup.channels,p.frames)){diskError=true;stopRequested=true;}
                else written.fetch_add(unsigned(p.frames));fifo.finishedRead(1);
                // Prevent legacy RIFF size overflow; close a valid partial take.
                if(written.load()*unsigned(setup.channels)*4>=0xff000000ULL){overflow=true;stopRequested=true;}
                if(diskError)break;continue;
            }
            if(stopRequested.load(std::memory_order_acquire))break;
            wait(3);
        }
        status.store(State::Stopping,std::memory_order_release);
        const bool flushed=writer->flush();if(fileStream)fileStream->flush();const bool streamOK=fileStream&&fileStream->getStatus().wasOk();writer.reset();
        if(diskError||!flushed||!streamOK)throw std::runtime_error("Disk write/flush failed; partial WAV retained");
        if(overflow.load())throw std::runtime_error("Recording stopped: disk queue or RIFF capacity exceeded; partial WAV retained without skipping timeline samples");
        if(written.load()==0){partial.deleteFile();throw std::runtime_error("No input frames recorded");}
        if(const auto error=noReplaceFile::publish(partial,setup.file))
            throw std::runtime_error("Cannot publish take without overwriting; partial WAV retained (destination may exist or hard links may be unsupported): "+error.message());
        // This is the audio commit point. A later metadata error must never
        // claim that the recorded WAV failed or hide it from completedFile().
        wavPublished=true;
        partial.deleteFile(); // A failed cleanup only leaves a recoverable duplicate.
        juce::DynamicObject::Ptr metadata=new juce::DynamicObject();
        metadata->setProperty("sampleRate",setup.rate);metadata->setProperty("frames",juce::int64(written.load()));metadata->setProperty("channels",setup.channels);
        metadata->setProperty("inputDevice",setup.inputName);metadata->setProperty("activeInputChannelIndex",setup.inputChannel);metadata->setProperty("inputChannelName",setup.inputChannelName);metadata->setProperty("insertStateAtArmXml",setup.insertStateXml);
        metadata->setProperty("firstCallbackTimelineSeconds",firstTimeline.load());
        const double delay=(setup.inputLatency+setup.outputLatency+setup.backingLatency+setup.printedLatency)/setup.rate;
        metadata->setProperty("estimatedPlacementSeconds",firstTimeline.load()-delay+setup.manualOffsetMs*.001);
        metadata->setProperty("inputLatencySamples",setup.inputLatency);metadata->setProperty("outputLatencySamples",setup.outputLatency);metadata->setProperty("backingDspLatencySamples",setup.backingLatency);metadata->setProperty("printedInsertLatencySamples",setup.printedLatency);
        metadata->setProperty("manualOffsetMs",setup.manualOffsetMs);metadata->setProperty("nonFiniteSamplesReplaced",juce::int64(invalidSamples.load()));
        metadata->setProperty("alignmentNote","Driver-reported estimate; verify with a loopback test. Positive manual offset places audio later.");
        const auto sidecar=setup.file.getSiblingFile(setup.file.getFileName()+".take.json");
        metadataPartial=sidecar.getSiblingFile(sidecar.getFileName()+".partial-"+juce::Uuid().toString());
        if(!noReplaceFile::available(metadataPartial,inspectError))throw std::runtime_error("Temporary metadata path is already in use or inaccessible");
        {
            auto metadataStream=metadataPartial.createOutputStream();
            if(!metadataStream||!metadataStream->getStatus().wasOk())throw std::runtime_error("Cannot create placement metadata");
            if(!metadataStream->writeText(juce::JSON::toString(juce::var(metadata.get()),false),false,false,"\n"))
                throw std::runtime_error("Cannot write placement metadata");
            metadataStream->flush();
            if(metadataStream->getStatus().failed())throw std::runtime_error("Cannot flush placement metadata");
        }
        if(const auto error=noReplaceFile::publish(metadataPartial,sidecar))
            throw std::runtime_error("Cannot publish placement metadata without overwriting (destination may exist or hard links may be unsupported): "+error.message());
        metadataPartial.deleteFile();
        outcome="Saved "+setup.file.getFileName()+" with placement metadata";
        if(partial.existsAsFile())outcome+="; duplicate partial WAV retained: "+partial.getFullPathName();
        if(metadataPartial.existsAsFile())outcome+="; duplicate partial metadata retained: "+metadataPartial.getFullPathName();
        status.store(State::Complete,std::memory_order_release);
    }catch(const std::exception& e){
        outcome=wavPublished?"Saved "+setup.file.getFileName()+"; placement metadata not saved: "+juce::String(e.what()):juce::String(e.what());
        if(partial.existsAsFile())outcome+=" / "+partial.getFullPathName();
        if(metadataPartial.existsAsFile())outcome+=" / metadata retained: "+metadataPartial.getFullPathName();
        status.store(wavPublished?State::Complete:State::Failed,std::memory_order_release);
    }
}
}

```

## Source/Systems/TakeRecorder.h

```cpp
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
namespace hem {
// Audio thread is the only FIFO producer. The disk worker is its only reader.
// Observe a producer-quiescent callback barrier before start. Launch start()
// OUTSIDE the device lock; no old take may be active. stop/destruction require
// producer exclusion. The host generation check rejects device changes at arm.
class TakeRecorder final : private juce::Thread {
public:
    enum class State {Idle,Opening,Armed,Recording,Stopping,Complete,Failed};
    struct Setup {
        juce::File file;double rate=96000;
        int channels=1,inputLatency=0,outputLatency=0,backingLatency=0,printedLatency=0;
        juce::String inputName,inputChannelName,insertStateXml;int inputChannel=0;
        double manualOffsetMs=0;
    };
    TakeRecorder();~TakeRecorder() override;
    bool start(const Setup&); // New file only, never overwrites a take.
    void stop() noexcept;
    void push(const float*,const float*,int,double timelineSeconds) noexcept;
    State state() const noexcept {return status.load(std::memory_order_acquire);}
    bool busy() const noexcept {auto s=state();return isThreadRunning()||s==State::Opening||s==State::Armed||s==State::Recording||s==State::Stopping;}
    juce::String result() const; // Message thread only, after !busy().
    juce::File completedFile() const {return state()==State::Complete?setup.file:juce::File();}
    std::uint64_t framesWritten() const noexcept {return written.load();}
private:
    static constexpr int packetFrames=256,capacity=512;
    struct Packet {std::array<float,packetFrames> l{},r{};int frames=0;};
    juce::AbstractFifo fifo{capacity};std::array<Packet,capacity> packets;
    void run() override;
    Setup setup;juce::String outcome;juce::File partial;
    std::atomic<State> status{State::Idle};
    std::atomic<bool> stopRequested{false},overflow{false};
    std::atomic<std::uint64_t> written{0},invalidSamples{0};
    std::atomic<double> firstTimeline{-1};
};
}

```

## Source/Systems/VocalTrackingPanel.cpp

```cpp
#include "VocalTrackingPanel.h"
namespace hem {
VocalTrackingPanel::VocalTrackingPanel(LiveVocalManager& m,juce::AudioDeviceManager& d,std::function<void()> setup,std::function<int()> delay,std::function<int()> master,std::function<juce::String()> check):manager(m),device(d),setupDevice(std::move(setup)),backingDelay(std::move(delay)),masterDelay(std::move(master)),recordCheck(std::move(check)){
 setLookAndFeel(&look);for(auto* c:std::array<juce::Component*,12>{&input,&monitor,&plugin,&hardware,&record,&stop,&print,&level,&offset,&status,&help,&monitoringNotice})addAndMakeVisible(*c);
 monitor.addItem("MONITOR OFF",1);monitor.addItem("DRY SOFTWARE MONITOR",2);monitor.addItem("INSERT SOFTWARE MONITOR",3);monitor.addItem("INTERFACE DIRECT / SOFTWARE OFF",4);monitor.setSelectedId(1);
 monitor.onChange=[this]{manager.setMonitor(LiveVocalManager::Monitor(monitor.getSelectedId()-1));updateMonitoringNotice();};
 input.onChange=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());if(!manager.recorder.busy())manager.setInput(input.getSelectedId()-1);};
 hardware.onClick=setupDevice;record.onClick=[this]{chooseTake();};stop.onClick=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());manager.stopTake();};
 print.onClick=[this]{const juce::ScopedLock lock(device.getAudioCallbackLock());if(!manager.recorder.busy())manager.setPrintInserts(print.getToggleState());};
 level.setRange(-60,6,.1);level.setValue(0);level.setTextValueSuffix(" dB / vocal monitor");level.onValueChange=[this]{manager.setMonitorGain(float(level.getValue()));};
 offset.setRange(-5000,5000,.1);offset.setValue(0);offset.setTextValueSuffix(" ms / placement correction");
 const auto& entries=PluginRegistry::entries();for(size_t i=0;i<entries.size();++i)if(!entries[i].monitoringOnly)plugin.addItem(entries[i].name,int(i)+1);plugin.setSelectedId(1);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];for(auto* b:{&row.name,&row.load,&row.remove,&row.bypass})addAndMakeVisible(*b);row.name.onClick=[this,i]{open(i);};row.load.onClick=[this,i]{insert(i);};row.remove.onClick=[this,i]{insert(i,true);};row.bypass.onClick=[this,i]{const juce::ScopedLock lock(device.getAudioCallbackLock());manager.rack.bypass(i,!manager.rack.bypassed(i));};}
 help.setText("Interface Direct disables only this app's vocal return. Enable direct monitoring on your interface itself. Dry software monitoring still has input/output latency plus the final master limiter. Takes are raw mono by default; PRINT records the stereo insert output. Use headphones to avoid mic/speaker feedback.",juce::dontSendNotification);
 monitoringNotice.setColour(juce::Label::backgroundColourId,juce::Colour(0xff101923));
 monitoringNotice.setColour(juce::Label::textColourId,juce::Colour(0xffffbb55));
 monitoringNotice.setColour(juce::Label::outlineColourId,juce::Colour(0xff00cddd));
 monitoringNotice.setFont(juce::Font(juce::FontOptions(16.0f)));
 monitoringNotice.setJustificationType(juce::Justification::centredLeft);
 monitoringNotice.setBorderSize(juce::BorderSize<int>(10));
 monitoringNotice.setMinimumHorizontalScale(1.0f);
 updateMonitoringNotice();
 setSize(1050,830);startTimerHz(15);
}
// Always-visible guidance follows the actual engine state, including device resets.
void VocalTrackingPanel::updateMonitoringNotice(){
 juce::String text;
 switch(manager.monitoring()){
 case LiveVocalManager::Monitor::Off:
  text="MIC MONITORING OFF  -  Recording still works. Choose dry/insert software monitoring, or enable Direct Monitor on your audio interface for hardware monitoring.";break;
 case LiveVocalManager::Monitor::DrySoftware:
  text="DRY SOFTWARE MONITORING  -  You hear the mic through the app. Delay includes input/output buffers and the final master limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InsertsSoftware:
  text="INSERT SOFTWARE MONITORING  -  You hear the mic through its effects. Delay includes device buffers, plugin latency and the final master limiter. Turn interface Direct Monitor off to avoid a doubled vocal.";break;
 case LiveVocalManager::Monitor::InterfaceDirect:
  text="ACTION REQUIRED: Enable Direct Monitor on your audio interface. This mode only turns OFF the app's vocal return; it cannot enable hardware monitoring. Recording remains available.";break;
 }
 if(monitoringNotice.getText()!=text)monitoringNotice.setText(text,juce::dontSendNotification);
}
void VocalTrackingPanel::chooseTake(){
 const auto problem=recordCheck();if(problem.isNotEmpty()){notice=problem;return;}
 if(manager.recorder.busy())return;
 chooser=std::make_unique<juce::FileChooser>("New vocal take  -  choose a new WAV filename",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("HEM-Vocal-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+".wav"),"*.wav");
 juce::Component::SafePointer<VocalTrackingPanel> safe(this);
 chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& f){
    if(!safe||f.getResult()==juce::File())return;const auto problem=safe->recordCheck();if(problem.isNotEmpty()){safe->notice=problem;return;}
    TakeRecorder::Setup options;options.file=f.getResult().withFileExtension("wav");options.manualOffsetMs=safe->offset.getValue();options.inputChannelName=safe->input.getText();
    juce::ValueTree rackState("VOCAL_INSERTS");for(int i=0;i<8;++i){juce::ValueTree slot("SLOT");slot.setProperty("index",i,nullptr);slot.setProperty("bypassed",safe->manager.rack.bypassed(i),nullptr);if(auto* p=safe->manager.rack.processor(i)){slot.setProperty("pluginId",PluginRegistry::idFor(*p),nullptr);slot.addChild(p->state.copyState(),-1,nullptr);}rackState.addChild(slot,-1,nullptr);}options.insertStateXml=rackState.toXmlString();
    bool started=false;{const juce::ScopedLock lock(safe->device.getAudioCallbackLock());if(auto* dev=safe->device.getCurrentAudioDevice()){
        options.inputName=safe->device.getAudioDeviceSetup().inputDeviceName;if(options.inputName.isEmpty())options.inputName=dev->getName();options.inputChannel=safe->manager.selectedInput();
        options.inputLatency=dev->getInputLatencyInSamples();options.outputLatency=dev->getOutputLatencyInSamples();options.backingLatency=safe->backingDelay();started=safe->manager.configureTake(options);}}
    // Thread creation and file preparation must not hold the device lock.
    if(started)started=safe->manager.recorder.start(options);
    safe->notice=started?juce::String():juce::String("No enabled input or recorder busy");
 });
}
void VocalTrackingPanel::insert(int slot,bool remove){
 if(manager.recorder.busy()||(manager.monitoring()!=LiveVocalManager::Monitor::Off&&manager.monitoring()!=LiveVocalManager::Monitor::InterfaceDirect))return;
 double rate=0;int block=0;juce::AudioIODevice* observed=nullptr;
 {const juce::ScopedLock lock(device.getAudioCallbackLock());observed=device.getCurrentAudioDevice();if(!observed)return;rate=observed->getCurrentSampleRate();block=observed->getCurrentBufferSizeSamples();}
 try{auto node=manager.rack.makeNode(remove?juce::String():juce::String(PluginRegistry::entries()[size_t(plugin.getSelectedId()-1)].id),rate,block);
    editor.reset();edited=-1;std::unique_ptr<TrackingInsertChain::Node> retired;
    {const juce::ScopedLock lock(device.getAudioCallbackLock());auto* dev=device.getCurrentAudioDevice();if(dev!=observed||!dev||dev->getCurrentSampleRate()!=rate||dev->getCurrentBufferSizeSamples()!=block)return;retired=manager.rack.exchange(slot,std::move(node));}
    // Ownership deletion takes place on the message thread after callback lock.
 }catch(const std::exception& e){notice=e.what();}
}
void VocalTrackingPanel::open(int i){auto* p=manager.rack.processor(i);if(!p)return;editor.reset();edited=i;editor=std::make_unique<StudioWindow>("LIVE VOCAL / "+p->getName(),p->createEditor());}
void VocalTrackingPanel::timerCallback(){
 updateMonitoringNotice();
 const bool busy=manager.recorder.busy();input.setEnabled(!busy);print.setEnabled(!busy);record.setEnabled(!busy);offset.setEnabled(!busy);hardware.setEnabled(!busy);
 monitor.setSelectedId(int(manager.monitoring())+1,juce::dontSendNotification);
 juce::StringArray names;int inputLatency=0,outputLatency=0;double rate=0;
 {const juce::ScopedLock lock(device.getAudioCallbackLock());if(auto* d=device.getCurrentAudioDevice()){auto all=d->getInputChannelNames();auto active=d->getActiveInputChannels();for(int i=0;i<all.size();++i)if(active[i])names.add(juce::String(i+1)+" / "+all[i]);rate=d->getCurrentSampleRate();inputLatency=d->getInputLatencyInSamples();outputLatency=d->getOutputLatencyInSamples();}}
 const auto key=device.getCurrentAudioDeviceType()+"/"+device.getAudioDeviceSetup().inputDeviceName+"/"+names.joinIntoString("|");if(key!=lastInputs){lastInputs=key;input.clear(juce::dontSendNotification);for(int i=0;i<names.size();++i)input.addItem(names[i],i+1);input.setSelectedId(names.isEmpty()?0:1,juce::dontSendNotification);const juce::ScopedLock lock(device.getAudioCallbackLock());if(!busy)manager.setInput(0);}
 input.setSelectedId(names.isEmpty()?0:manager.selectedInput()+1,juce::dontSendNotification);
 const bool editable=!busy&&(manager.monitoring()==LiveVocalManager::Monitor::Off||manager.monitoring()==LiveVocalManager::Monitor::InterfaceDirect);
 for(int i=0;i<8;++i){auto& row=rows[size_t(i)];auto* p=manager.rack.processor(i);row.name.setButtonText(juce::String(i+1)+" / "+(p?p->getName():juce::String("EMPTY")));row.load.setEnabled(editable);row.remove.setEnabled(editable&&p);row.bypass.setEnabled(p);row.bypass.setToggleState(manager.rack.bypassed(i),juce::dontSendNotification);}
 juce::String state;
 switch(manager.recorder.state()){case TakeRecorder::State::Idle:state="READY";break;case TakeRecorder::State::Opening:state="OPENING";break;case TakeRecorder::State::Armed:state="ARMED";break;case TakeRecorder::State::Recording:state="RECORDING";break;case TakeRecorder::State::Stopping:state="FINALIZING";break;default:state=manager.recorder.result();break;}
 const auto latency=rate>0?1000.0*(inputLatency+outputLatency+manager.monitorLatency()+masterDelay())/rate:0;
 status.setText((notice.isEmpty()?juce::String():notice+" / ")+state+" / MIC "+juce::String(decibels(manager.inputPeak()),1)+" dBFS / frames "+juce::String(juce::int64(manager.recorder.framesWritten()))+" / software path estimate "+juce::String(latency,1)+" ms (driver + DSP)",juce::dontSendNotification);
}
void VocalTrackingPanel::resized(){auto b=getLocalBounds().reduced(16);monitoringNotice.setBounds(b.removeFromTop(76).reduced(3));auto row=b.removeFromTop(42);hardware.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));input.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));monitor.setBounds(row.reduced(3));
 row=b.removeFromTop(42);record.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(3));stop.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));print.setBounds(row.reduced(3));
 row=b.removeFromTop(42);level.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(3));offset.setBounds(row.reduced(3));plugin.setBounds(b.removeFromTop(40).reduced(3));
 help.setBounds(b.removeFromBottom(90));status.setBounds(b.removeFromBottom(65));for(auto& controls:rows){row=b.removeFromTop(std::min(42,b.getHeight()));controls.name.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.load.setBounds(row.removeFromLeft(row.getWidth()/3).reduced(2));controls.remove.setBounds(row.removeFromLeft(row.getWidth()/2).reduced(2));controls.bypass.setBounds(row.reduced(2));}}
}

```

## Source/Systems/VocalTrackingPanel.h

```cpp
#pragma once
#include "LiveVocalManager.h"
#include "../StudioWindow.h"
namespace hem {
class VocalTrackingPanel final:public juce::Component,private juce::Timer {
public:
    VocalTrackingPanel(LiveVocalManager&,juce::AudioDeviceManager&,std::function<void()> setup,std::function<int()> backingLatency,std::function<int()> masterLatency,std::function<juce::String()> mayRecord);
    ~VocalTrackingPanel() override {stopTimer();editor.reset();setLookAndFeel(nullptr);}
    void resized() override;
private:
    void updateMonitoringNotice();
    void timerCallback() override;void chooseTake();void insert(int,bool remove=false);void open(int);
    LiveVocalManager& manager;juce::AudioDeviceManager& device;
    std::function<void()> setupDevice;std::function<int()> backingDelay,masterDelay;std::function<juce::String()> recordCheck;
    HardwareLookAndFeel look;
    juce::ComboBox input,monitor,plugin;
    juce::TextButton hardware{"INPUT / OUTPUT DEVICE"},record{"RECORD NEW TAKE"},stop{"STOP / FINALIZE TAKE"};
    juce::ToggleButton print{"PRINT INSERTS TO STEREO TAKE"};juce::Slider level,offset;
    juce::Label status,help,monitoringNotice;
    struct Row{juce::TextButton name,load{"INSERT"},remove{"REMOVE"},bypass{"BYP"};};
    std::array<Row,8> rows;
    std::unique_ptr<juce::FileChooser> chooser;std::unique_ptr<StudioWindow> editor;
    juce::String lastInputs,notice;int edited=-1;
};
}

```

## Source/TrackEngine.h

```cpp
#pragma once
#include "InsertChain.h"
#include "StemPlayback.h"
#include "Systems/AudioMeters.h"
namespace hem {
// A bounded set of stereo tracks. Readers stream through JUCE read-ahead;
// each track has its own processor ownership and latency-compensated insert bus.
struct TrackEngine {
    std::unique_ptr<StemPlayback> playback=std::make_unique<StemPlayback>();
    juce::AudioTransportSource& transport() noexcept {return playback->transport;}
    const juce::AudioTransportSource& transport() const noexcept {return playback->transport;}
    InsertChain chain;
    juce::AudioBuffer<float> scratch;
    juce::String name{"EMPTY TRACK"};
    double sourceRate=0;
    std::atomic<float> trimDb{0};
    std::atomic<bool> muted{false},solo{false};
    Ramp gainRamp;
    AudioMeters signalMeters;
    bool loaded=false;
    void prepare(double rate,int block){
        scratch.setSize(2,block);chain.prepare(rate,block);signalMeters.prepare(rate);
        gainRamp.reset(rate,gain(trimDb.load()));transport().prepareToPlay(block,rate);
    }
    void release(){transport().releaseResources();chain.release();signalMeters.prepare(0);}
    void render(juce::AudioBuffer<float>& sum,int n,bool anySolo,const float* inputGains){
        juce::AudioSourceChannelInfo region(&scratch,0,n);region.clearActiveBufferRegion();
        transport().getNextAudioBlock(region);
        auto* l=scratch.getWritePointer(0);auto* r=scratch.getWritePointer(1);
        for(int i=0;i<n;++i){l[i]*=inputGains[i];r[i]*=inputGains[i];}
        chain.process(l,r,n);
        const bool audible=!muted.load(std::memory_order_relaxed)&&(!anySolo||solo.load(std::memory_order_relaxed));
        gainRamp.set(audible?gain(trimDb.load(std::memory_order_relaxed)):0.f);
        auto* sl=sum.getWritePointer(0);auto* sr=sum.getWritePointer(1);
        for(int i=0;i<n;++i){const float g=gainRamp.next();l[i]*=g;r[i]*=g;sl[i]+=l[i];sr[i]+=r[i];}
        signalMeters.push(l,r,n);
    }
};
}

```

## Source/UI/ControlGeometry.h

```cpp
#pragma once
#include <algorithm>
#include <cmath>

namespace hem::ui {
// Pure local-coordinate geometry shared by the renderer and dependency-free tests.
// Host letterboxing belongs to the component layout, never to the look-and-feel.
struct DialGeometry {
    float centreX{}, centreY{}, radius{}, ringRadius{}, angle{};
    float pointerStartX{}, pointerStartY{}, pointerEndX{}, pointerEndY{};
    bool drawable{};
};

inline DialGeometry dialGeometry(float x, float y, float width, float height,
                                 float position, float start, float end) noexcept {
    DialGeometry d;
    if (!(width > 0.0f && height > 0.0f) || !std::isfinite(width)
        || !std::isfinite(height) || !std::isfinite(start) || !std::isfinite(end))
        return d;
    const float size = std::min(width, height);
    const float fraction = std::isfinite(position) ? std::clamp(position, 0.0f, 1.0f) : 0.0f;
    d.centreX = x + width * 0.5f;
    d.centreY = y + height * 0.5f;
    d.radius = size * 0.36f;
    d.ringRadius = size * 0.43f;
    d.angle = start + fraction * (end - start);
    const auto pointX = [&](float radius) { return d.centreX + std::sin(d.angle) * radius; };
    const auto pointY = [&](float radius) { return d.centreY - std::cos(d.angle) * radius; };
    d.pointerStartX = pointX(d.radius * 0.34f);
    d.pointerStartY = pointY(d.radius * 0.34f);
    d.pointerEndX = pointX(d.radius * 0.84f);
    d.pointerEndY = pointY(d.radius * 0.84f);
    d.drawable = true;
    return d;
}

inline float boundedSliderPosition(float position, float origin, float length) noexcept {
    if (!std::isfinite(position)) return origin;
    return std::clamp(position, origin, origin + std::max(0.0f, length));
}
} // namespace hem::ui

```

## Source/UI/NativeWorkspace.cpp

```cpp
#include "NativeWorkspace.h"
#include "../HardwareLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace hem {
namespace {
const juce::Colour cyan(0xff71ebf5), amber(0xffffbb60), ink(0xff071018), dim(0xff8da8b3), edge(0xff25434e);
void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds,
          float size = 13.0f, juce::Colour colour = cyan, int lines = 1,
          juce::Justification alignment = juce::Justification::centredLeft) {
    g.setColour(colour); g.setFont(juce::Font(juce::FontOptions(size)));
    g.drawFittedText(value, bounds, alignment, lines, 0.85f);
}
juce::String db(float value) { return value <= AudioMeters::silenceFloorDb ? "-inf" : juce::String(value, 1); }
void panel(juce::Graphics& g, juce::Rectangle<int> bounds, bool selected = false) {
    auto r = bounds.toFloat().reduced(1);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff17242d),r.getX(),r.getY(),ink,r.getX(),r.getBottom(),false));
    g.fillRoundedRectangle(r,5); g.setColour(selected ? amber : edge); g.drawRoundedRectangle(r,5,1);
}
void identify(juce::Component& c, const juce::String& id, const juce::String& name) { c.setComponentID(id); c.setName(name); }
void setupButton(juce::TextButton& b, const juce::String& id, const juce::String& title) {
    identify(b,id,title); b.setButtonText(title); b.setWantsKeyboardFocus(true);
}
class StereoMeter final : public juce::Component, public juce::SettableTooltipClient {
public:
    AudioMeters::Snapshot value;
    std::function<void()> onReset;
    StereoMeter(){setWantsKeyboardFocus(true);setTooltip("Actual stereo sample peak / RMS / 1 s hold. Click, Enter or Space to reset clip latches; reset is consumed by the next audio block.");}
    void mouseDown(const juce::MouseEvent&) override {if(onReset)onReset();}
    bool keyPressed(const juce::KeyPress& key) override {if(key==juce::KeyPress::returnKey||key==juce::KeyPress::spaceKey){if(onReset)onReset();return true;}return false;}
    void paint(juce::Graphics& g) override {
        auto plot = getLocalBounds().reduced(1); plot.removeFromTop(13); plot.removeFromBottom(36);
        if (plot.getHeight() < 1 || plot.getWidth() < 1) return;
        text(g,"dBFS",{0,0,getWidth(),13},10,dim,1,juce::Justification::centred);
        const int gutter = 21, laneWidth = juce::jmax(3,(plot.getWidth()-gutter-4)/2);
        auto y = [&](float v){return float(plot.getBottom())-float(plot.getHeight())*juce::jlimit(0.0f,1.0f,(v+60)/60);};
        for(float v : {0.f,-12.f,-24.f,-48.f,-60.f}) {
            g.setColour(edge); g.drawHorizontalLine(int(y(v)),float(gutter),float(getWidth()));
            text(g,juce::String(int(v)),{0,int(y(v))-6,gutter-2,12},9,dim,1,juce::Justification::centredRight);
        }
        int x = gutter;int channelIndex=0;
        for(const auto& c : {value.left,value.right}) {
            auto lane = juce::Rectangle<float>(float(x),float(plot.getY()),float(laneWidth),float(plot.getHeight()));
            g.setColour(juce::Colour(0xff020609));g.fillRect(lane);
            if(value.ready) {
                g.setColour(cyan.withAlpha(0.26f));g.fillRect(lane.withTop(y(c.peakDb)));
                g.setColour(juce::Colour(0xff3dc4aa));g.fillRect(lane.reduced(2,0).withTop(y(c.rmsDb)));
                if(c.peakDb>-60){g.setColour(cyan);g.drawLine(lane.getX(),y(c.peakDb),lane.getRight(),y(c.peakDb),2);}
                if(c.heldPeakDb>-60){g.setColour(amber);g.drawLine(lane.getX(),y(c.heldPeakDb),lane.getRight(),y(c.heldPeakDb),1);}
            }
            g.setColour(c.clipped?juce::Colour(0xffff5a48):edge);g.fillRect(x,plot.getY()-3,laneWidth,2);
            text(g,channelIndex++==0?"L":"R",{x,plot.getBottom()+2,laneWidth,13},10,cyan,1,juce::Justification::centred);
            x += laneWidth+3;
        }
        text(g,value.ready ? db(juce::jmax(value.left.peakDb,value.right.peakDb)) : "NO AUDIO",
             {0,getHeight()-17,getWidth(),16},10,value.ready?cyan:dim,1,juce::Justification::centred);
        if(hasKeyboardFocus(true)){g.setColour(cyan);g.drawRect(getLocalBounds(),1);}
    }
};
}

struct NativeWorkspace::Impl {
    struct PluginPage;
    struct MixerPage;
    struct VaultPage;
    struct RoutingPage;
    NativeWorkspace& owner;
    Callbacks cb;
    HardwareLookAndFeel look;
    Page active = Page::plugin;
    std::unique_ptr<PluginPage> plugin;
    std::unique_ptr<MixerPage> mixer;
    std::unique_ptr<VaultPage> vault;
    std::unique_ptr<RoutingPage> routing;
    explicit Impl(NativeWorkspace&,Callbacks);
    ~Impl();
    TrackBank& bank() const {return cb.bank();}
    int track() const {return juce::jlimit(0,12,cb.getSelectedTrack ? cb.getSelectedTrack():0);}
    int slot() const {return juce::jlimit(0,7,cb.getSelectedSlot ? cb.getSelectedSlot():0);}
    void chooseTrack(int i) {if(cb.setSelectedTrack)cb.setSelectedTrack(juce::jlimit(0,12,i));owner.refresh();}
    void openSlot(int i) {if(cb.setSelectedSlot)cb.setSelectedSlot(i);if(cb.onOpenSlot)cb.onOpenSlot(i);owner.refresh();}
};

struct NativeWorkspace::Impl::PluginPage final : juce::Component {
    struct Divider final : juce::Component {
        PluginPage& page;
        explicit Divider(PluginPage& p):page(p){identify(*this,"plugin-divider","Drag to resize stem and plugin panes");setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);}
        void paint(juce::Graphics& g) override {g.setColour(edge);g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3,0),2);g.setColour(cyan);for(int dy:{-9,0,9})g.fillEllipse(float(getWidth()/2-1),float(getHeight()/2+dy),2,2);}
        void mouseDrag(const juce::MouseEvent& e) override {page.host.owner.setSplitProportion(float(e.getEventRelativeTo(&page).x)/float(juce::jmax(1,page.getWidth())));}
    };
    Impl& host;
    juce::Component left, editorCanvas;
    juce::Viewport leftView, editorView;
    juce::ComboBox trackChoice;
    std::array<juce::TextButton,8> slots, bypass;
    juce::TextButton expand, browse;
    Divider divider{*this};
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Label leftTitle, editorTitle, editorHint, summary;
    StereoMeter meter;
    float split=.32f;
    bool expanded=false;juce::Point<int> observedEditorSize;
    explicit PluginPage(Impl& h):host(h) {
        identify(*this,"workspace-plugin","Selected stem and plugin editor");
        leftView.setViewedComponent(&left,false);leftView.setScrollBarsShown(true,true);
        editorView.setViewedComponent(&editorCanvas,false);editorView.setScrollBarsShown(true,true);
        identify(leftView,"plugin-stem-viewport","Selected stem and eight insert slots");
        identify(editorView,"plugin-editor-viewport","Native plugin editor with scrolling");
        for(auto* c : std::initializer_list<juce::Component*>{&leftView,&editorView,&divider,&expand,&browse,&editorTitle})addAndMakeVisible(c);
        for(auto* c : std::initializer_list<juce::Component*>{&leftTitle,&trackChoice,&summary,&meter})left.addAndMakeVisible(c);
        editorCanvas.addAndMakeVisible(editorHint);
        leftTitle.setText("SELECTED STEM / 8 SERIAL INSERTS",juce::dontSendNotification);
        leftTitle.setFont(juce::Font(juce::FontOptions(13).withStyle("Bold")));
        identify(trackChoice,"plugin-track-select","Selected audio track");
        identify(meter,"plugin-signal-meter","Selected track stereo meter and clip reset");meter.onReset=[this]{host.bank().tracks[size_t(host.track())].signalMeters.resetClip();};
        trackChoice.onChange=[this]{host.chooseTrack(trackChoice.getSelectedId()-1);};
        for(int i=0;i<8;++i){
            setupButton(slots[size_t(i)],"plugin-slot-"+juce::String(i),"SLOT "+juce::String(i+1));
            setupButton(bypass[size_t(i)],"plugin-bypass-"+juce::String(i),"BYP");
            bypass[size_t(i)].setTooltip("Bypass this insert. Delay compensation stays active.");
            slots[size_t(i)].onClick=[this,i]{host.openSlot(i);};
            bypass[size_t(i)].onClick=[this,i]{auto& chain=host.bank().tracks[size_t(host.track())].chain;chain.bypass(i,!chain.isBypassed(i));host.owner.refresh();};
            left.addAndMakeVisible(slots[size_t(i)]);left.addAndMakeVisible(bypass[size_t(i)]);
        }
        setupButton(expand,"plugin-expand","EXPAND PLUGIN");expand.onClick=[this]{host.owner.setPluginExpanded(!expanded);};
        setupButton(browse,"plugin-browse","PLUGIN VAULT");browse.onClick=[this]{host.owner.setPage(Page::vault);};
        editorHint.setJustificationType(juce::Justification::centred);
        editorHint.setColour(juce::Label::textColourId,dim);editorHint.setFont(juce::Font(juce::FontOptions(17)));
        summary.setFont(juce::Font(juce::FontOptions(11)));summary.setColour(juce::Label::textColourId,dim);
    }
    ~PluginPage() override {clearEditor();editorView.setViewedComponent(nullptr,false);leftView.setViewedComponent(nullptr,false);}
    void clearEditor(){editorView.setViewPosition(0,0);editor.reset();editorHint.setVisible(true);resized();}
    void setEditor(std::unique_ptr<juce::AudioProcessorEditor> next){clearEditor();editor=std::move(next);if(editor){editorCanvas.addAndMakeVisible(*editor);editor->setTopLeftPosition(0,0);editorHint.setVisible(false);}resized();}
    void refresh(){
        if(editor&&observedEditorSize!=juce::Point<int>(editor->getWidth(),editor->getHeight())){observedEditorSize={editor->getWidth(),editor->getHeight()};resized();}
        auto& track=host.bank().tracks[size_t(host.track())];
        const auto old=trackChoice.getSelectedId();bool rebuild=trackChoice.getNumItems()!=13;
        for(int i=0;i<13&&!rebuild;++i)rebuild=trackChoice.getItemText(i)!=juce::String(i+1).paddedLeft('0',2)+"  "+host.bank().tracks[size_t(i)].name;
        if(rebuild){trackChoice.clear(juce::dontSendNotification);for(int i=0;i<13;++i)trackChoice.addItem(juce::String(i+1).paddedLeft('0',2)+"  "+host.bank().tracks[size_t(i)].name,i+1);}
        if(old!=host.track()+1||rebuild)trackChoice.setSelectedId(host.track()+1,juce::dontSendNotification);
        for(int i=0;i<8;++i){
            auto* p=track.chain.activeProcessor(i);const auto name=p?(p->isExternal()&&!p->processingReady()?juce::String("PAUSED / "):juce::String())+p->getName():juce::String("EMPTY SLOT");
            slots[size_t(i)].setButtonText(juce::String(i+1).paddedLeft('0',2)+"  "+name);
            slots[size_t(i)].setToggleState(i==host.slot(),juce::dontSendNotification);
            slots[size_t(i)].setTooltip("Slot "+juce::String(i+1)+": "+name+(track.chain.isBusy(i)?" (transitioning)":"")+(p&&p->isExternal()?" / "+p->externalDiagnostic():juce::String()));
            bypass[size_t(i)].setEnabled(p!=nullptr&&!track.chain.isBusy(i));
            bypass[size_t(i)].setToggleState(p!=nullptr&&track.chain.isBypassed(i),juce::dontSendNotification);
        }
        meter.value=track.signalMeters.snapshot();meter.repaint();
        summary.setText((track.loaded?"SOURCE LOADED":"NO FILE LOADED")+juce::String("  |  Trim ")+juce::String(track.trimDb.load(),1)+" dB\nPost-insert / post-trim signal\n8-slot compensated latency: "+juce::String(track.chain.latencySamples())+" samples",juce::dontSendNotification);
        auto* p=track.chain.activeProcessor(host.slot());
        editorTitle.setText(editor?editor->getAudioProcessor()->getName():"SLOT "+juce::String(host.slot()+1)+" / "+(p?p->getName():"EMPTY"),juce::dontSendNotification);
        editorHint.setText(p?"Open the selected insert to view its native controls":"Select an insert, or choose a module in Plugin Vault\nThe shared transport stays active while you navigate",juce::dontSendNotification);
        expand.setButtonText(expanded?"RESTORE SPLIT":"EXPAND PLUGIN");expand.setToggleState(expanded,juce::dontSendNotification);
        repaint();
    }
    void paint(juce::Graphics& g) override {panel(g,getLocalBounds());}
    void resized() override {
        auto r=getLocalBounds().reduced(8);const int header=38;
        const int leftWidth=expanded?0:juce::jlimit(200,juce::jmax(200,r.getWidth()-280),int(float(r.getWidth())*split));
        leftView.setVisible(!expanded);divider.setVisible(!expanded);
        if(!expanded){leftView.setBounds(r.removeFromLeft(leftWidth));divider.setBounds(r.removeFromLeft(12));}
        auto top=r.removeFromTop(header);expand.setBounds(top.removeFromRight(142).reduced(2));browse.setBounds(top.removeFromRight(132).reduced(2));editorTitle.setBounds(top.reduced(4,0));
        editorView.setBounds(r);
        const int lw=juce::jmax(244,leftView.getWidth()-leftView.getScrollBarThickness());
        const int lh=juce::jmax(450,leftView.getHeight());left.setSize(lw,lh);
        leftTitle.setBounds(8,0,lw-16,25);trackChoice.setBounds(8,27,lw-16,32);
        const int meterWidth=72, buttonWidth=juce::jmax(96,lw-meterWidth-76);
        for(int i=0;i<8;++i){slots[size_t(i)].setBounds(6,70+i*38,buttonWidth,34);bypass[size_t(i)].setBounds(buttonWidth+8,70+i*38,48,34);}
        meter.setBounds(lw-meterWidth-4,72,meterWidth,300);
        summary.setBounds(8,382,lw-16,60);
        const int ew=juce::jmax(1,editorView.getWidth()-editorView.getScrollBarThickness());
        const int eh=juce::jmax(1,editorView.getHeight()-editorView.getScrollBarThickness());
        editorCanvas.setSize(juce::jmax(ew,editor?editor->getWidth():0),juce::jmax(eh,editor?editor->getHeight():0));
        editorHint.setBounds(editorCanvas.getLocalBounds().reduced(20));
    }
};

struct NativeWorkspace::Impl::MixerPage final : juce::Component {
    struct Strip final : juce::Component {
        Impl& host;int index;juce::TextButton select,mute,solo;juce::Slider fader;juce::Label gainReadout;StereoMeter meter;juce::String trackName;bool loaded=false;
        Strip(Impl& h,int i):host(h),index(i){
            identify(*this,"mixer-strip-"+juce::String(i),"Mixer channel "+juce::String(i+1));
            setupButton(select,"mixer-select-"+juce::String(i),"EDIT "+juce::String(i+1));
            setupButton(mute,"mixer-mute-"+juce::String(i),"M");setupButton(solo,"mixer-solo-"+juce::String(i),"S");
            mute.setTooltip("Mute "+juce::String(i+1));solo.setTooltip("Solo "+juce::String(i+1));
            identify(fader,"mixer-gain-"+juce::String(i),"Track "+juce::String(i+1)+" gain in dB");
            identify(meter,"mixer-signal-meter-"+juce::String(i),"Track "+juce::String(i+1)+" stereo meter and clip reset");meter.onReset=[this]{host.bank().tracks[size_t(index)].signalMeters.resetClip();};
            fader.setSliderStyle(juce::Slider::LinearVertical);fader.setRange(-60,6,.1);fader.setValue(0,juce::dontSendNotification);
            fader.setDoubleClickReturnValue(true,0);fader.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);fader.setTextValueSuffix(" dB");
            identify(gainReadout,"mixer-readout-"+juce::String(i),"Actual track trim in dB");gainReadout.setJustificationType(juce::Justification::centred);gainReadout.setFont(juce::Font(juce::FontOptions(12)));gainReadout.setColour(juce::Label::textColourId,amber);
            fader.setTooltip("Post-insert trim: -60 to +6 dB. Double click for 0 dB.");
            fader.onValueChange=[this]{host.bank().tracks[size_t(index)].trimDb.store(float(fader.getValue()),std::memory_order_relaxed);};
            mute.onClick=[this]{auto& v=host.bank().tracks[size_t(index)].muted;v.store(!v.load());host.owner.refresh();};
            solo.onClick=[this]{auto& v=host.bank().tracks[size_t(index)].solo;v.store(!v.load());host.owner.refresh();};
            select.onClick=[this]{host.chooseTrack(index);host.owner.setPage(Page::plugin);host.openSlot(host.slot());};
            for(auto* c:std::initializer_list<juce::Component*>{&select,&mute,&solo,&fader,&gainReadout,&meter})addAndMakeVisible(c);
        }
        void refresh(){auto& t=host.bank().tracks[size_t(index)];trackName=t.name;loaded=t.loaded;gainReadout.setText(juce::String(t.trimDb.load(),1)+" dB",juce::dontSendNotification);select.setToggleState(host.track()==index,juce::dontSendNotification);mute.setToggleState(t.muted.load(),juce::dontSendNotification);solo.setToggleState(t.solo.load(),juce::dontSendNotification);if(!fader.isMouseButtonDown())fader.setValue(t.trimDb.load(),juce::dontSendNotification);meter.value=t.signalMeters.snapshot();meter.repaint();repaint();}
        void paint(juce::Graphics& g) override {panel(g,getLocalBounds(),host.track()==index);text(g,juce::String(index+1).paddedLeft('0',2),{4,5,getWidth()-8,18},12,amber,1,juce::Justification::centred);text(g,trackName,{5,26,getWidth()-10,34},12,cyan,2,juce::Justification::centred);text(g,loaded?"FILE LOADED":"EMPTY",{4,61,getWidth()-8,16},9,dim,1,juce::Justification::centred);}
        void resized() override {const int w=getWidth();auto bottom=getLocalBounds().removeFromBottom(76).reduced(5,4);select.setBounds(bottom.removeFromBottom(31));auto toggles=bottom.removeFromTop(30);mute.setBounds(toggles.removeFromLeft(toggles.getWidth()/2).reduced(1));solo.setBounds(toggles.reduced(1));gainReadout.setBounds(3,79,w-6,24);const int area=juce::jmax(60,getHeight()-199);fader.setBounds(2,113,36,area);meter.setBounds(37,107,juce::jmax(43,w-40),area+10);}
    };
    Impl& host;juce::Viewport viewport;juce::Component content;std::array<std::unique_ptr<Strip>,13> strips;
    explicit MixerPage(Impl& h):host(h){identify(*this,"workspace-mixer","13 track mixer");identify(viewport,"mixer-viewport","Horizontally scrollable mixer strips");viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);for(int i=0;i<13;++i){strips[size_t(i)]=std::make_unique<Strip>(host,i);content.addAndMakeVisible(*strips[size_t(i)]);}}
    ~MixerPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {text(g,"13 STEM MIXER",{12,2,getWidth()-24,24},16,amber);text(g,"Post-insert trim -60 to +6 dB  |  Mute / Solo  |  Stereo sample peak, RMS and 1 s hold  |  Meter scale -60 to 0 dBFS",{12,28,getWidth()-24,22},12,dim);}
    void refresh(){for(auto& s:strips)s->refresh();}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(58).reduced(8,0));const int w=juce::jmax(13*90,viewport.getWidth()-viewport.getScrollBarThickness());const int h=juce::jmax(270,viewport.getHeight()-viewport.getScrollBarThickness());content.setSize(w,h);for(int i=0;i<13;++i){const int x=w*i/13,next=w*(i+1)/13;strips[size_t(i)]->setBounds(x+2,0,next-x-4,h);}}
};

struct NativeWorkspace::Impl::VaultPage final : juce::Component {
    struct Card final : juce::Component {
        Impl& host;int index;juce::TextButton action;
        Card(Impl& h,int i):host(h),index(i){identify(*this,"vault-card-"+juce::String(i),PluginRegistry::entries()[size_t(i)].name);setupButton(action,"vault-install-"+juce::String(i),"");addAndMakeVisible(action);action.onClick=[this]{const auto& entry=PluginRegistry::entries()[size_t(index)];if(entry.monitoringOnly){if(host.cb.onMonitor)host.cb.onMonitor();}else if(host.cb.onInstallModule)host.cb.onInstallModule(index);host.owner.refresh();};}
        void paint(juce::Graphics& g) override {panel(g,getLocalBounds());const auto& e=PluginRegistry::entries()[size_t(index)];text(g,e.category,{12,8,getWidth()-24,18},11,amber);text(g,e.name,{12,30,getWidth()-24,43},16,cyan,2);text(g,e.monitoringOnly?"Listening path only / excluded from export":"Native processor / serial stem insert",{12,76,getWidth()-24,25},11,dim);}
        void refresh(){const auto& e=PluginRegistry::entries()[size_t(index)];action.setButtonText(e.monitoringOnly?"OPEN MONITOR EQ":"INSERT / REPLACE SLOT "+juce::String(host.slot()+1));action.setTooltip(e.monitoringOnly?"Open the existing listening-only monitor EQ":"Install "+juce::String(e.name)+" on track "+juce::String(host.track()+1)+", slot "+juce::String(host.slot()+1));action.setEnabled(e.monitoringOnly?bool(host.cb.onMonitor):bool(host.cb.onInstallModule)&&!host.bank().tracks[size_t(host.track())].chain.isBusy(host.slot()));}
        void resized() override {action.setBounds(10,getHeight()-42,getWidth()-20,32);}
    };
    Impl& host;juce::TextEditor search;juce::ComboBox category;juce::Label target,externalInfo;juce::TextButton loadExternal,reloadExternal;juce::Viewport viewport;juce::Component content;
    std::vector<std::unique_ptr<Card>> cards;std::vector<int> visible;
    explicit VaultPage(Impl& h):host(h){
        identify(*this,"workspace-vault","Plugin registry vault");identify(search,"vault-search","Search registered plugin names and categories");identify(category,"vault-category","Plugin category");
        search.setTextToShowWhenEmpty("Search "+juce::String(int(PluginRegistry::entries().size()))+" registered modules",dim);search.setColour(juce::TextEditor::backgroundColourId,ink);search.setColour(juce::TextEditor::textColourId,cyan);search.setColour(juce::TextEditor::outlineColourId,edge);
        category.addItem("All categories",1);juce::StringArray categories;for(const auto& e:PluginRegistry::entries())categories.addIfNotAlreadyThere(e.category);categories.sort(true);for(int i=0;i<categories.size();++i)category.addItem(categories[i],i+2);category.setSelectedId(1,juce::dontSendNotification);
        search.onTextChange=[this]{filter();};category.onChange=[this]{filter();};target.setFont(juce::Font(juce::FontOptions(12)));target.setColour(juce::Label::textColourId,dim);
        for(auto* c:std::initializer_list<juce::Component*>{&search,&category,&target,&viewport})addAndMakeVisible(c);
        setupButton(loadExternal,"vault-load-vst3","LOAD SELECTED VST3");setupButton(reloadExternal,"vault-reload-vst3","RELOAD CURRENT VST3");
        loadExternal.onClick=[this]{if(host.cb.onLoadVst3)host.cb.onLoadVst3();};reloadExternal.onClick=[this]{if(host.cb.onReloadVst3)host.cb.onReloadVst3();};
        externalInfo.setText("Stereo VST3 effects / 6 ms per-slot limit / native code: load only trusted files",juce::dontSendNotification);externalInfo.setColour(juce::Label::textColourId,dim);externalInfo.setFont(juce::Font(juce::FontOptions(11)));
        if(host.cb.onLoadVst3){addAndMakeVisible(loadExternal);addAndMakeVisible(reloadExternal);addAndMakeVisible(externalInfo);}
        viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);
        for(size_t i=0;i<PluginRegistry::entries().size();++i){cards.push_back(std::make_unique<Card>(host,int(i)));content.addAndMakeVisible(*cards.back());}
        filter();
    }
    ~VaultPage() override {viewport.setViewedComponent(nullptr,false);}
    void filter(){visible.clear();const auto query=search.getText().trim();for(int i=0;i<int(PluginRegistry::entries().size());++i){const auto& e=PluginRegistry::entries()[size_t(i)];const bool match=(category.getSelectedId()==1||category.getText()==e.category)&&(query.isEmpty()||(juce::String(e.name)+" "+e.category+" "+e.id).containsIgnoreCase(query));cards[size_t(i)]->setVisible(match);if(match)visible.push_back(i);}resized();repaint();}
    void refresh(){int monitorCount=0;for(const auto& entry:PluginRegistry::entries())if(entry.monitoringOnly)++monitorCount;target.setText("TARGET: "+juce::String(host.track()+1).paddedLeft('0',2)+" "+host.bank().tracks[size_t(host.track())].name+" / SLOT "+juce::String(host.slot()+1)+"     |     "+juce::String(int(PluginRegistry::entries().size())-monitorCount)+" insert processors + "+juce::String(monitorCount)+" listening-only",juce::dontSendNotification);for(auto& c:cards)c->refresh();}
    void paint(juce::Graphics& g) override {text(g,"PLUGIN VAULT / "+juce::String(int(visible.size()))+" OF "+juce::String(int(PluginRegistry::entries().size()))+" REGISTERED",{12,0,getWidth()-24,28},16,amber);if(visible.empty())text(g,"No registered modules match this search",getLocalBounds().withTrimmedTop(130).reduced(20),16,dim,2,juce::Justification::centred);}
    void resized() override {auto r=getLocalBounds().reduced(10);r.removeFromTop(28);auto row=r.removeFromTop(34);category.setBounds(row.removeFromRight(200));row.removeFromRight(10);search.setBounds(row);r.removeFromTop(4);target.setBounds(r.removeFromTop(27));r.removeFromTop(8);if(host.cb.onLoadVst3){auto actions=r.removeFromBottom(60);externalInfo.setBounds(actions.removeFromBottom(23));loadExternal.setBounds(actions.removeFromLeft(actions.getWidth()/2).reduced(2));reloadExternal.setBounds(actions.reduced(2));}viewport.setBounds(r);const int w=juce::jmax(220,viewport.getWidth()-viewport.getScrollBarThickness());const int columns=juce::jmax(1,w/290),cardWidth=w/columns;const int rows=(int(visible.size())+columns-1)/columns;content.setSize(w,juce::jmax(r.getHeight(),rows*158));for(size_t n=0;n<visible.size();++n)cards[size_t(visible[n])]->setBounds(int(n)%columns*cardWidth+3,int(n)/columns*158,cardWidth-6,152);}
};

struct NativeWorkspace::Impl::RoutingPage final : juce::Component {
    struct Diagram final : juce::Component {
        Impl& host;RouteTelemetry telemetry;std::array<AudioMeters::Snapshot,13> levels;
        explicit Diagram(Impl& h):host(h){identify(*this,"routing-diagram","Fixed audio signal flow with measured levels");}
        void box(juce::Graphics& g,juce::Rectangle<int> r,const juce::String& title,const juce::String& detail,bool active=false){panel(g,r,active);text(g,title,r.reduced(9).removeFromTop(21),13,active?amber:cyan);text(g,detail,r.reduced(9).withTrimmedTop(23),11,dim,3);}
        void arrow(juce::Graphics& g,juce::Point<float> a,juce::Point<float> b,juce::Colour colour=edge){g.setColour(colour);g.drawArrow({a,b},1.5f,5,5);}
        void paint(juce::Graphics& g) override {
            g.fillAll(ink);text(g,"SOURCE > INPUT GAIN > 8 INSERTS > TRIM / MUTE / SOLO",{10,2,520,24},12,dim);
            constexpr int row=27;const int busX=535;const int sourceWidth=176;
            for(int i=0;i<13;++i){const int y=31+i*row;const auto& t=host.bank().tracks[size_t(i)];const auto& l=levels[size_t(i)];
                const float peak=juce::jmax(l.left.peakDb,l.right.peakDb);const bool signal=l.ready&&peak>-60;
                g.setColour(host.track()==i?amber.withAlpha(.12f):juce::Colour(0xff101e28));g.fillRoundedRectangle(6.f,float(y),514.f,23.f,3);
                text(g,juce::String(i+1).paddedLeft('0',2)+" "+t.name,{10,y,sourceWidth,23},11,t.loaded?cyan:dim);
                for(int s=0;s<8;++s){const int x=192+s*22;auto* p=t.chain.activeProcessor(s);g.setColour(p?((t.chain.isBypassed(s)||!p->processingReady())?amber.withAlpha(.20f):cyan.withAlpha(.22f)):edge.withAlpha(.3f));g.fillRoundedRectangle(float(x),float(y+3),19,17,2);text(g,juce::String(s+1),{x,y+3,19,17},10,p?cyan:dim,1,juce::Justification::centred);}
                text(g,juce::String(t.trimDb.load(),1)+" dB",{373,y,50,23},10,dim);
                const auto meter=juce::Rectangle<float>(429.f,float(y+9),42,5);g.setColour(edge);g.fillRect(meter);if(signal){g.setColour(cyan);g.fillRect(meter.withWidth(meter.getWidth()*juce::jlimit(0.f,1.f,(peak+60)/60)));}
                text(g,l.ready?db(peak):"N/A",{476,y,42,23},10,signal?cyan:dim);
                g.setColour(signal?cyan.withAlpha(juce::jlimit(.12f,.8f,(peak+60)/60)):edge);g.drawLine(521.f,float(y+12),float(busX),float(y+12),1);
            }
            g.setColour(edge);g.drawLine(float(busX),43.f,float(busX),367.f,2);
            arrow(g,{float(busX),207.f},{562.f,207.f});
            box(g,{562,164,156,87},"STEREO SUM","13 parallel tracks\nShared output gain\nAnalysis tap before listening");
            arrow(g,{640,251},{640,272});box(g,{562,273,156,82},"LISTENING COPY","+ vocal monitoring\nRecording/export bypass\nlistening correction");
            arrow(g,{718,313},{738,313});box(g,{740,274,176,83},telemetry.headphoneCorrection?"CORRECTED ROUTE":"DIRECT ROUTE",telemetry.headphoneCorrection?"Measured calibration\nMonitor EQ: "+juce::String(telemetry.monitorReady?"prepared":"not prepared"):"Calibration / monitor EQ\nbypassed at route blend",telemetry.headphoneCorrection);
            arrow(g,{828,274},{828,253});box(g,{740,163,176,89},"OUTPUT SAFETY",juce::String(telemetry.hardwareOutputs==1?"Mono fold-down first\n":telemetry.hardwareOutputs>1?"Stereo output\n":"No active output\n")+"Task 2B final limiter\n"+(telemetry.limiterLatencySamples>0?juce::String(telemetry.limiterLatencySamples)+" samples latency":"Latency unavailable")+(telemetry.limiterReductionDb?" / "+juce::String(*telemetry.limiterReductionDb,2)+" dB GR":""));
            arrow(g,{916,207},{938,207});
            const bool ready=telemetry.output.ready;box(g,{940,164,180,111},"ACTUAL DEVICE OUTPUT",ready?"L peak "+db(telemetry.output.left.peakDb)+" dBFS\nR peak "+db(telemetry.output.right.peakDb)+" dBFS\nL/R RMS "+db(telemetry.output.left.rmsDb)+" / "+db(telemetry.output.right.rmsDb):"Audio meter unavailable");
            text(g,"EXISTING FIXED CONNECTIONS",{565,30,540,22},16,amber);
            text(g,"Lines represent the implemented audio path. Track level bars measure post-trim contributions; final levels measure exact emitted samples. No free rewiring is implemented.",{565,62,548,69},13,dim,3);
            text(g,"Meters: sample peak / RMS, not LUFS or certified dBTP",{563,373,555,23},12,dim);
            text(g,"Insert cells 1-8: filled = installed; amber = bypassed/unavailable. Empty slots retain latency compensation.",{12,390,1105,25},12,dim);
            text(g,(telemetry.outputDevice.isEmpty()?"No output device reported":telemetry.outputDevice)+"  |  "+(telemetry.sampleRate>0?juce::String(telemetry.sampleRate,0)+" Hz / "+juce::String(telemetry.blockSize)+" samples":"Audio unavailable")+"  |  "+juce::String(telemetry.hardwareOutputs)+" active outputs",{12,423,1105,24},13,cyan);
            text(g,telemetry.calibrationName.isEmpty()?"No calibration profile reported":telemetry.calibrationName,{12,452,1105,25},12,dim);
        }
        void refresh(){for(int i=0;i<13;++i)levels[size_t(i)]=host.bank().tracks[size_t(i)].signalMeters.snapshot();telemetry=host.cb.routeTelemetry?host.cb.routeTelemetry():RouteTelemetry{};repaint();}
    };
    juce::Viewport viewport;Diagram diagram;
    explicit RoutingPage(Impl& h):diagram(h){identify(*this,"workspace-routing","Read-only routing telemetry");viewport.setViewedComponent(&diagram,false);viewport.setScrollBarsShown(true,true);addAndMakeVisible(viewport);}
    ~RoutingPage() override {viewport.setViewedComponent(nullptr,false);}
    void paint(juce::Graphics& g) override {text(g,"ROUTING / LIVE SIGNAL FLOW",{12,1,getWidth()-24,30},16,amber);}
    void resized() override {viewport.setBounds(getLocalBounds().withTrimmedTop(39).reduced(8,0));diagram.setSize(juce::jmax(1132,viewport.getWidth()-viewport.getScrollBarThickness()),juce::jmax(482,viewport.getHeight()-viewport.getScrollBarThickness()));}
};

NativeWorkspace::Impl::Impl(NativeWorkspace& o,Callbacks callbacks):owner(o),cb(std::move(callbacks)){
    jassert(cb.bank);owner.setLookAndFeel(&look);
    plugin=std::make_unique<PluginPage>(*this);mixer=std::make_unique<MixerPage>(*this);vault=std::make_unique<VaultPage>(*this);routing=std::make_unique<RoutingPage>(*this);
    owner.addAndMakeVisible(*plugin);owner.addChildComponent(*mixer);owner.addChildComponent(*vault);owner.addChildComponent(*routing);
}
NativeWorkspace::Impl::~Impl(){owner.setLookAndFeel(nullptr);}
NativeWorkspace::NativeWorkspace(Callbacks callbacks):impl(std::make_unique<Impl>(*this,std::move(callbacks))){identify(*this,"native-workspace","HEM native workspace");setOpaque(true);refresh();startTimerHz(20);}
NativeWorkspace::~NativeWorkspace(){stopTimer();clearEditor();}
void NativeWorkspace::setPage(Page page){impl->active=page;impl->plugin->setVisible(page==Page::plugin);impl->mixer->setVisible(page==Page::mixer);impl->vault->setVisible(page==Page::vault);impl->routing->setVisible(page==Page::routing);refresh();resized();}
NativeWorkspace::Page NativeWorkspace::page() const noexcept{return impl->active;}
void NativeWorkspace::refresh(){if(!impl->cb.bank)return;impl->plugin->refresh();if(impl->active==Page::mixer)impl->mixer->refresh();if(impl->active==Page::vault)impl->vault->refresh();if(impl->active==Page::routing)impl->routing->diagram.refresh();}
void NativeWorkspace::setEditor(std::unique_ptr<juce::AudioProcessorEditor> editor){impl->plugin->setEditor(std::move(editor));refresh();}
void NativeWorkspace::clearEditor(){impl->plugin->clearEditor();}
bool NativeWorkspace::hasEditor() const noexcept{return impl->plugin->editor!=nullptr;}
void NativeWorkspace::setPluginExpanded(bool expanded){impl->plugin->expanded=expanded;impl->plugin->refresh();impl->plugin->resized();}
bool NativeWorkspace::pluginExpanded() const noexcept{return impl->plugin->expanded;}
void NativeWorkspace::setSplitProportion(float proportion){if(!std::isfinite(proportion))return;impl->plugin->split=juce::jlimit(.22f,.60f,proportion);impl->plugin->resized();}
float NativeWorkspace::splitProportion() const noexcept{return impl->plugin->split;}
void NativeWorkspace::setVaultFilter(const juce::String& search,const juce::String& category){impl->vault->search.setText(search,false);auto& menu=impl->vault->category;int id=1;for(int i=0;i<menu.getNumItems();++i)if(menu.getItemText(i)==category)id=menu.getItemId(i);menu.setSelectedId(id,juce::dontSendNotification);impl->vault->filter();}
std::vector<int> NativeWorkspace::filteredRegistryIndices() const{return impl->vault->visible;}
void NativeWorkspace::paint(juce::Graphics& g){g.fillAll(ink);}
void NativeWorkspace::resized(){for(auto* child:std::initializer_list<juce::Component*>{impl->plugin.get(),impl->mixer.get(),impl->vault.get(),impl->routing.get()})child->setBounds(getLocalBounds());}
void NativeWorkspace::timerCallback(){if(isVisible())refresh();}
} // namespace hem

```

## Source/UI/NativeWorkspace.h

```cpp
#pragma once
#include <JuceHeader.h>
#include <optional>
#include "../SessionState.h"
#include "../Systems/AudioMeters.h"

namespace hem {
// Message-thread, single-window views over the host's existing engine. Navigation
// changes visibility only; no view starts/stops audio or owns a TrackBank.
class NativeWorkspace final : public juce::Component, private juce::Timer {
public:
    enum class Page { plugin, mixer, vault, routing };
    struct RouteTelemetry {
        AudioMeters::Snapshot output;
        bool headphoneCorrection = false;
        bool monitorReady = false;
        juce::String calibrationName, outputDevice;
        double sampleRate = 0;
        int blockSize = 0, hardwareOutputs = 0, limiterLatencySamples = 0;
        std::optional<float> limiterReductionDb; // absent unless the engine really publishes GR
    };
    struct Callbacks {
        std::function<TrackBank&()> bank;
        std::function<int()> getSelectedTrack, getSelectedSlot;
        std::function<void(int)> setSelectedTrack, setSelectedSlot;
        std::function<void(int)> onOpenSlot, onInstallModule;
        std::function<void()> onMonitor;
        std::function<void()> onLoadVst3, onReloadVst3;
        std::function<RouteTelemetry()> routeTelemetry;
    };
    explicit NativeWorkspace(Callbacks);
    ~NativeWorkspace() override;
    void setPage(Page);
    Page page() const noexcept;
    void refresh();
    // The editor borrows its processor. The HOST MUST clear it before removing,
    // replacing or retiring that processor or its TrackBank (even when hidden).
    void setEditor(std::unique_ptr<juce::AudioProcessorEditor>);
    void clearEditor();
    bool hasEditor() const noexcept;
    void setPluginExpanded(bool);
    bool pluginExpanded() const noexcept;
    void setSplitProportion(float); // bounded to .22 .. .60; left-pane fraction
    float splitProportion() const noexcept;
    void setVaultFilter(const juce::String& search, const juce::String& category = "All categories");
    std::vector<int> filteredRegistryIndices() const;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativeWorkspace)
};
} // namespace hem

```

## Source/UI/StudioBranding.h

```cpp
#pragma once
#include <JuceHeader.h>

namespace hem::ui {
// Approved artwork is preserved byte-for-byte. Only the native viewport crops
// its ghost badge; no additional controls or meter pixels enter the host.
struct StudioBranding {
    static juce::Rectangle<int> ghostSource() { return {10,702,244,196}; }
    static juce::Rectangle<float> mapped(juce::Rectangle<float> root,float x,float y,float w,float h) {
        return {root.getX()+x*root.getWidth()/1536,root.getY()+y*root.getHeight()/864,w*root.getWidth()/1536,h*root.getHeight()/864};
    }
    static void illuminatedText(juce::Graphics& g,const juce::String& text,juce::Rectangle<float> bounds,float height,juce::Colour accent) {
        const juce::Graphics::ScopedSaveState save(g);
        g.setFont(juce::Font(juce::FontOptions(height).withStyle("Bold")));
        const auto r=bounds.toNearestInt();
        g.setColour(accent.withAlpha(0.18f));
        for(const auto offset:std::array<juce::Point<int>,4>{{{-2,0},{2,0},{0,-2},{0,2}}})
            g.drawFittedText(text,r.translated(offset.x,offset.y),juce::Justification::centred,1,0.7f);
        g.setColour(accent.brighter(0.45f));g.drawFittedText(text,r,juce::Justification::centred,1,0.7f);
    }
    static void draw(juce::Graphics& g,juce::Rectangle<float> root,const juce::Image& ghost) {
        const float scale=root.getWidth()/1536;
        // Keep the original illuminated crown and perimeter metalwork visible.
        const auto title=mapped(root,605,13,415,42);
        g.setColour(juce::Colour(0xff02090d));g.fillRoundedRectangle(mapped(root,598,9,431,66),4*scale);
        illuminatedText(g,"HYBRID ELITE MAFIA",title,34*scale,juce::Colour(0xffffa735));
        illuminatedText(g,"S T U D I O   S U I T E",mapped(root,620,54,385,18),15*scale,juce::Colour(0xff00edff));
        const auto badge=mapped(root,1322,110,184,158);
        g.setColour(juce::Colour(0xff02080d));g.fillRoundedRectangle(badge,5*scale);
        if(ghost.isValid()) {
            const auto source=ghostSource();const auto destination=badge.reduced(4*scale).toNearestInt();
            const juce::Graphics::ScopedSaveState save(g);
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(ghost,destination.getX(),destination.getY(),destination.getWidth(),destination.getHeight(),source.getX(),source.getY(),source.getWidth(),source.getHeight());
        }
        g.setColour(juce::Colour(0x4500edff));g.drawRoundedRectangle(badge,5*scale,5*scale);
        g.setColour(juce::Colour(0xff00edff));g.drawRoundedRectangle(badge,5*scale,1.2f*scale);
    }
};
}

```

## Tests/AnalysisTests.cpp

```cpp
#include "../Source/Systems/AnalysisWorker.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <new>
static thread_local bool checkingPush=false;
static std::atomic<int> callbackAllocations{0};
void* operator new(std::size_t n){if(checkingPush)++callbackAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* t){if(!b)throw std::runtime_error(t);}
int main(){try{
 auto worker=std::make_unique<hem::AnalysisWorker>();worker->prepare(96000);
 const int size=hem::AnalysisWorker::fftSize*2;std::vector<float> l(size),r(size);
 for(int i=0;i<size;++i){l[size_t(i)]=.5f*float(std::sin(juce::MathConstants<double>::twoPi*86*i/hem::AnalysisWorker::fftSize));r[size_t(i)]=.25f*float(std::sin(juce::MathConstants<double>::twoPi*172*i/hem::AnalysisWorker::fftSize));}
 checkingPush=true;worker->push(l.data(),r.data(),size);checkingPush=false;
 auto frame=std::make_unique<hem::AnalysisWorker::Snapshot>();
 auto wait=[&](double rate,std::uint64_t end){for(int i=0;i<500;++i){if(worker->copyLatest(*frame)&&frame->sampleRate==rate&&frame->frameEnd>=end)return true;juce::Thread::sleep(5);}return false;};
 require(wait(96000,size),"Analysis worker did not publish");
 require(std::abs(frame->leftDb[86]+6.0206f)<.02f,"Left FFT amplitude incorrect");require(std::abs(frame->rightDb[172]+12.0412f)<.02f,"Right FFT amplitude incorrect");
 require(frame->validColumns==size/64,"Peak bucket count incorrect");
 float peak=0;for(int i=0;i<frame->validColumns;++i)peak=std::max(peak,frame->waveform[size_t(i)].highL);require(std::abs(peak-.5f)<.002,"Waveform extrema inaccurate");
 for(int i=0;i<100;++i)worker->push(l.data(),r.data(),size);
 juce::Thread::sleep(100);worker->prepare(48000);
 // Wait until the bounded backlog drains; no audio thread ever waits.
 juce::Thread::sleep(100);worker->push(l.data(),r.data(),size);require(wait(48000,size),"Device-rate epoch did not reset analysis");require(frame->droppedFrames>0,"Overflow did not report dropped display data");
 require(callbackAllocations.load()==0,"Audio analysis push allocated memory");
 std::cout<<"PASS: allocation-free audio push; real JUCE FFT amplitudes, stereo bins, waveform extrema, bounded overflow and rate epochs\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/AudioMetersTests.cpp

```cpp
#include "../Source/Systems/AudioMeters.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool checkAllocations = false;
std::uint64_t allocations = 0;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void push(hem::AudioMeters& meter, const float* left, const float* right, int count) {
    checkAllocations = true;
    meter.push(left, right, count);
    checkAllocations = false;
}
void sameChannel(const hem::AudioMeters::ChannelSnapshot& a, const hem::AudioMeters::ChannelSnapshot& b) {
    require(a.peakDb == b.peakDb && a.rmsDb == b.rmsDb && a.heldPeakDb == b.heldPeakDb
            && a.clipped == b.clipped, "Meter result depends on callback partitioning");
}
}
void* operator new(std::size_t count) {
    if (checkAllocations) ++allocations;
    if (auto* ptr = std::malloc(count ? count : 1)) return ptr;
    throw std::bad_alloc();
}
void* operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
    try {
        hem::AudioMeters meter;
        require(!meter.snapshot().ready, "Unprepared meter is marked ready");
        require(!meter.prepare(0) && !meter.prepare(-48000)
                && !meter.prepare(std::numeric_limits<double>::infinity()), "Invalid rate was accepted");
        push(meter, nullptr, nullptr, 16);
        require(!meter.snapshot().ready, "Invalid rate does not keep meter unavailable");

        for (const int rate : {44100, 48000, 96000}) {
            require(meter.prepare(rate), "Valid sample rate rejected");
            std::vector<float> left(std::size_t(rate) * 3, 0.5f), right(left.size(), 0.25f);
            push(meter, left.data(), right.data(), int(left.size()));
            auto s = meter.snapshot();
            require(s.ready && !s.left.clipped && !s.right.clipped, "Steady DC incorrectly clips");
            near(s.left.peakDb, -6.0205999, 0.00001, "Left DC sample peak calibration incorrect");
            near(s.right.peakDb, -12.0411998, 0.00001, "Right DC sample peak calibration incorrect");
            near(s.left.rmsDb, -6.0205999, 0.001, "Left steady DC RMS calibration incorrect");
            near(s.right.rmsDb, -12.0411998, 0.001, "Independent right RMS calibration incorrect");

            meter.prepare(rate);
            for (std::size_t i = 0; i < left.size(); ++i) {
                const double wave = std::sin(6.28318530717958647692 * 1000.0 * double(i) / rate);
                left[i] = float(0.5 * wave);
                right[i] = float(0.125 * wave);
            }
            push(meter, left.data(), right.data(), int(left.size()));
            s = meter.snapshot();
            near(s.left.peakDb, -6.0205999, 0.03, "Sine sample peak calibration incorrect");
            near(s.left.rmsDb, -9.0308999, 0.01, "Sine RMS is not amplitude divided by sqrt(2)");
            near(s.right.rmsDb, -21.0720997, 0.01, "Independent right sine RMS incorrect");
            near(s.left.heldPeakDb, -6.0205999, 0.001, "Sine held peak incorrect");

            meter.prepare(rate);
            push(meter, nullptr, nullptr, rate);
            s = meter.snapshot();
            require(s.left.peakDb == hem::AudioMeters::silenceFloorDb
                    && s.left.rmsDb == hem::AudioMeters::silenceFloorDb
                    && s.right.heldPeakDb == hem::AudioMeters::silenceFloorDb,
                    "Silence does not produce the documented meter floor");
        }

        // Exactly the same samples through different callback sizes must produce
        // exactly the same detectors, hold timing, counters and clip latches.
        std::vector<float> left(20000), right(left.size());
        for (std::size_t i = 0; i < left.size(); ++i) {
            left[i] = float(0.8 * std::sin(double(i) * 0.013));
            right[i] = float(1.05 * std::cos(double(i) * 0.027));
        }
        hem::AudioMeters whole, partitioned;
        whole.prepare(48000); partitioned.prepare(48000);
        push(whole, left.data(), right.data(), int(left.size()));
        const int sizes[] {1, 17, 256, 1024, 3, 819};
        int offset = 0, part = 0;
        while (offset < int(left.size())) {
            const int n = std::min(sizes[part++ % 6], int(left.size()) - offset);
            push(partitioned, left.data() + offset, right.data() + offset, n);
            offset += n;
        }
        sameChannel(whole.snapshot().left, partitioned.snapshot().left);
        sameChannel(whole.snapshot().right, partitioned.snapshot().right);

        // The 1 kHz test rate makes exact hold durations easy to inspect.
        meter.prepare(1000);
        const float fullScale = 1.0f, belowFullScale = 0.999f;
        push(meter, &fullScale, &belowFullScale, 1);
        require(meter.snapshot().left.clipped && !meter.snapshot().right.clipped,
                "Clip threshold or stereo independence incorrect");
        push(meter, nullptr, nullptr, 1000);
        auto s = meter.snapshot();
        near(s.left.peakDb, -24.0, 0.0001, "Sample peak release is not 24 dB per second");
        near(s.left.heldPeakDb, 0.0, 0.0001, "Held peak decays before one full second");
        require(s.left.clipped, "Clip latch vanished during silence");
        push(meter, nullptr, nullptr, 1000);
        near(meter.snapshot().left.heldPeakDb, -18.0, 0.0001, "Hold decay is not 18 dB per second");
        meter.resetClip();
        push(meter, nullptr, nullptr, 0);
        require(!meter.snapshot().left.clipped, "Audio-thread reset did not clear clip latch");
        meter.resetClip();
        push(meter, &fullScale, nullptr, 1);
        require(meter.snapshot().left.clipped, "Fresh clipping after reset did not re-latch");

        meter.prepare(48000);
        const float invalidL[] {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.5f};
        const float invalidR[] {0.0f, -std::numeric_limits<float>::infinity(), 0.25f};
        push(meter, invalidL, invalidR, 3);
        s = meter.snapshot();
        require(s.nonFiniteSamples == 3, "Non-finite samples were not counted independently");
        require(std::isfinite(s.left.rmsDb) && std::isfinite(s.right.rmsDb), "Non-finite input poisoned RMS");
        require(!s.left.clipped && !s.right.clipped, "Non-finite input was mislabeled as finite clipping");
        meter.resetClip();
        push(meter, nullptr, nullptr, 0);
        require(meter.snapshot().nonFiniteSamples == 0, "Reset did not clear invalid-sample counter");
        const float large = std::numeric_limits<float>::max();
        push(meter, &large, &large, 1);
        s = meter.snapshot();
        require(std::isfinite(s.left.rmsDb) && std::isfinite(s.left.peakDb) && s.left.clipped,
                "Largest finite float overflowed meter math");
        meter.prepare(96000);
        s = meter.snapshot();
        require(s.left.peakDb == hem::AudioMeters::silenceFloorDb && !s.left.clipped && s.nonFiniteSamples == 0,
                "Device-rate prepare did not reset old measurements");
        require(allocations == 0, "Meter allocated on the audio callback");
        std::cout << "PASS: AudioMeters DC/sine calibration at 44.1/48/96 kHz, independent stereo, silence, "
                     "partition invariance, peak release/hold, clip/reset, invalid samples, finite bounds, "
                     "rate reset and zero callback allocations\n";
    } catch (const std::exception& error) {
        checkAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

```

## Tests/ChainTests.cpp

```cpp
#include <JuceHeader.h>
#include "../Source/InsertChain.h"
#include "../Source/Plugins/PluginRegistry.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){juce::ScopedJuceInitialiser_GUI init;try{
 hem::InsertChain chain;constexpr int block=256;chain.prepare(96000,block);std::array<float,block> l{},r{};
 auto render=[&](int blocks){for(int k=0;k<blocks;++k){l.fill(.05f);r.fill(.05f);chain.process(l.data(),r.data(),block);for(float x:l)require(std::isfinite(x),"Non-finite rack output");}};
 render(40);require(std::abs(l.back()-.05f)<1e-6f,"Empty rack is not delayed unity");
 for(int slot=0;slot<8;++slot){auto p=chain.stageNode(hem::createModule(slot),96000,block,chain.slotLatency());require(chain.publish(slot,std::move(p)),"Insertion rejected");render(40);require(!chain.isBusy(slot),"Insertion never completed");require(chain.activeProcessor(slot)!=nullptr,"Processor missing");}
 render(40);require(std::abs(l.back()-.05f)<1e-5f,"Neutral loaded chain not unity");
 for(int i=0;i<8;++i)chain.bypass(i,true);render(40);require(std::abs(l.back()-.05f)<1e-6f,"Bypass altered gain");
 auto* eq=chain.activeProcessor(1);auto* parameter=eq->state.getParameter("mudGain");parameter->setValueNotifyingHost(parameter->convertTo0to1(-7));juce::MemoryBlock state;eq->getStateInformation(state);
 auto copy=hem::createModule(1);copy->setStateInformation(state.getData(),int(state.getSize()));require(std::abs(copy->state.getRawParameterValue("mudGain")->load()+7)<1e-6f,"State roundtrip failed");
 require(chain.publish(0,chain.stageNode(std::move(copy),96000,block,chain.slotLatency())),"Replacement rejected");render(40);auto retired=chain.collectRetired(0);require(retired!=nullptr,"Old node not retired");retired.reset();
 require(chain.publish(0,nullptr),"Remove rejected");render(40);retired=chain.collectRetired(0);require(retired!=nullptr&&!chain.activeProcessor(0),"Removal failed");retired.reset();
 auto* before=chain.activeProcessor(4);auto* other=chain.activeProcessor(5);
 require(chain.swapSlotsStopped(4,5),"Stopped reorder rejected");require(chain.activeProcessor(5)==before&&chain.activeProcessor(4)==other,"Reorder lost processor identity");
 for(const auto& entry:hem::PluginRegistry::entries()){
 auto p=hem::PluginRegistry::create(entry.id);require(p!=nullptr,"Registry factory failed");
 juce::MemoryBlock bytes;p->getStateInformation(bytes);p->setStateInformation(bytes.getData(),int(bytes.getSize()));
 }

 for(int module=8;module<=10;++module){
 require(chain.publish(0,chain.stageNode(hem::createModule(module),96000,block,chain.slotLatency())),"New finishing insert rejected");
 chain.bypass(0,false);render(80);auto old=chain.collectRetired(0);old.reset();
 require(chain.activeProcessor(0)!=nullptr,"Finishing processor missing");
 require(std::abs(l.back()-.05f)<1.e-5f,"Finishing processor not latency-compensated unity");
 }
 bool rejected=false;try{chain.stageNode(hem::PluginRegistry::create("monitor-eq"),96000,block,chain.slotLatency());}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"Monitor EQ entered a track insert");
 chain.release();chain.prepare(48000,128);render(80);require(std::isfinite(l.back()),"Rate restart failed");
 std::cout<<"PASS: eight inserts, unity, latency-aligned bypass, replacement, retirement, state roundtrip, removal, device-rate restart.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/ControlGeometryTests.cpp

```cpp
#include "../Source/UI/ControlGeometry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}
int main() {
    try {
        constexpr float pi = 3.14159265358979323846f;
        const float start = pi * 1.2f, end = pi * 2.8f;
        for (const float scale : {0.50f, 0.75f, 1.0f, 1.25f, 2.0f}) {
            const float x = 23 * scale, y = 41 * scale, w = 94 * scale, h = 98 * scale;
            const auto first = hem::ui::dialGeometry(x, y, w, h, 0, start, end);
            const auto middle = hem::ui::dialGeometry(x, y, w, h, 0.5f, start, end);
            const auto last = hem::ui::dialGeometry(x, y, w, h, 1, start, end);
            require(first.drawable && middle.drawable && last.drawable, "Valid dial disappeared");
            require(near(middle.centreX, x + w * 0.5f) && near(middle.centreY, y + h * 0.5f), "Dial ignores local coordinate offsets");
            require(near(first.angle, start) && near(last.angle, end), "Dial endpoint mapping incorrect");
            require(middle.pointerEndY < middle.centreY, "Midpoint indicator does not point up");
            require(first.pointerEndX < first.centreX && last.pointerEndX > last.centreX, "Dial pointer does not rotate left to right");
            require(first.centreX - first.ringRadius >= x && first.centreX + first.ringRadius <= x + w
                    && first.centreY - first.ringRadius >= y && first.centreY + first.ringRadius <= y + h,
                    "Dial ring escapes its mapped hitbox");
            const auto scaled = hem::ui::dialGeometry(0, 0, 94, 98, 0.5f, start, end);
            require(near(middle.pointerEndX, x + scaled.pointerEndX * scale)
                    && near(middle.pointerEndY, y + scaled.pointerEndY * scale), "Scaled pointer drifts from artwork");
        }
        require(!hem::ui::dialGeometry(0, 0, 0, 10, 0, start, end).drawable, "Zero-width dial is drawable");
        require(!hem::ui::dialGeometry(0, 0, 10, -1, 0, start, end).drawable, "Negative-height dial is drawable");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, -4, start, end).angle, start), "Negative position is not clamped");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, 4, start, end).angle, end), "Oversized position is not clamped");
        require(near(hem::ui::dialGeometry(0, 0, 50, 50, std::numeric_limits<float>::quiet_NaN(), start, end).angle, start), "NaN position produces invalid pointer geometry");
        require(near(hem::ui::boundedSliderPosition(50, 20, 100), 50), "Fader does not retain its actual thumb position");
        require(near(hem::ui::boundedSliderPosition(-500, 20, 100), 20), "Fader minimum bounds incorrect");
        require(near(hem::ui::boundedSliderPosition(500, 20, 100), 120), "Fader maximum bounds incorrect");
        std::cout << "PASS: local-coordinate dial rotation, bounds, scale/offset preservation, finite input and fader positions\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

```

## Tests/CoreMasterStageTests.cpp

```cpp
#include "../Source/CoreMasterStage.h"
#include <cstdlib>
#include <iostream>
#include <new>

static bool watchAllocations = false;
static size_t allocationCount = 0;
void* operator new(std::size_t bytes) {
    if (watchAllocations) ++allocationCount;
    if (auto* p = std::malloc(bytes == 0 ? 1 : bytes)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static void require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
int main() {
    try {
        for (double rate : {44100., 48000., 96000.}) {
            constexpr int block = 257;
            hem::CoreMasterStage adapter;
            hem::MasterTruePeakLimiter reference;
            adapter.prepare(rate, block);
            reference.prepare(rate, block, 2);
            require(adapter.latencySamples() == reference.latency(), "Latency mismatch");
            if (rate == 48000.) require(adapter.latencySamples() == 253, "48k baseline latency changed");
            juce::AudioBuffer<float> expected(2, block), actual(2, block);
            int64_t time = 0;
            for (int k = 0; k < 180; ++k) {
                const int n = (k % 3 == 0 ? 1 : (k % 3 == 1 ? 127 : block));
                for (int i = 0; i < n; ++i, ++time) {
                    const float level = k < 10 ? .02f : 1.8f;
                    const float left = level * std::sin(float(2 * juce::MathConstants<double>::pi * 997 * time / rate));
                    const float right = -.63f * left;
                    expected.setSample(0, i, left); expected.setSample(1, i, right);
                    actual.setSample(0, i, left); actual.setSample(1, i, right);
                }
                if (k == 20) {
                    expected.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
                    actual.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
                }
                reference.process(expected, n);
                watchAllocations = true;
                adapter.process(actual.getWritePointer(0), actual.getWritePointer(1), n);
                watchAllocations = false;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        require(actual.getSample(ch, i) == expected.getSample(ch, i), "Adapter changed baseline samples");
                require(std::isfinite(adapter.samplePeakDb()), "Invalid sample-peak meter");
            }
            require(allocationCount == 0, "Adapter allocates in processing");
            juce::AudioBuffer<float> oversized(2, block + 1);
            oversized.clear(); oversized.setSample(0, 0, 1);
            adapter.process(oversized.getWritePointer(0), oversized.getWritePointer(1), block + 1);
            require(oversized.getMagnitude(0, block + 1) == 0, "Oversized block was not safely silenced");
            std::cout << "PASS " << rate << " Hz: exact Task2B output, actual latency "
                      << adapter.latencySamples() << ", irregular blocks, stereo, NaN, sample meter, zero new/new[]\n";
        }
        std::cout << "PASS: unchanged baseline integration only; this test does not certify reconstructed true peaks.\n";
        return 0;
    } catch (const std::exception& e) {
        watchAllocations = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}

```

## Tests/DeviceInputTests.cpp

```cpp
#include "../Source/Systems/LiveVocalManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
 auto m=std::make_unique<hem::LiveVocalManager>();
 juce::AudioBuffer<float> input(16,256),bus(2,256);
 for(int c=0;c<16;++c)for(int i=0;i<256;++i)input.setSample(c,i,float(c+1)/32);
 m->prepare(96000,256,16);
 require(m->setInput(15),"Input 16 unavailable");require(!m->setInput(16),"Invalid index accepted");
 m->setMonitor(hem::LiveVocalManager::Monitor::DrySoftware);
 for(int k=0;k<12;++k){m->capture(input,0,256,0);bus.clear();m->addMonitor(bus,256);}
 require(std::abs(bus.getSample(0,255)-.5f)<1e-6f,"Input 16 mapping failed");
 require(m->setInput(4),"Input 5 unavailable");
 for(int k=0;k<12;++k){m->capture(input,0,256,0);bus.clear();m->addMonitor(bus,256);}
 require(std::abs(bus.getSample(1,255)-5.f/32)<1e-6f,"Channel switch failed");
 m->prepare(48000,256,0);require(!m->setInput(0),"Missing input accepted");m->capture(input,0,256,0);require(m->inputPeak()==0,"Disconnected input leaked");
 require(m->monitoring()==hem::LiveVocalManager::Monitor::Off,"Device reset enabled monitoring");
 m->prepare(48000,256,1);require(m->setInput(0)&&!m->setInput(1),"Mono input bounds failed");
 std::cout<<"PASS: 16-channel capture, channel switching, disconnect silence and mono bounds\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/DspTests.cpp

```cpp
#include "DSP/DspCore.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <cstdlib>
#include <new>
// Detect allocations while the kernels execute, including in Release builds.
static bool checking=false;static size_t allocations=0;
void* operator new(std::size_t n){if(checking)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p) noexcept {::operator delete(p);}void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static float rms(const std::vector<float>& v,int from=2000){double e=0;for(size_t i=size_t(from);i<v.size();++i)e+=v[i]*v[i];return float(std::sqrt(e/double(v.size()-size_t(from))));}
int main(){try {
 constexpr double sr=96000;constexpr int n=24000;std::vector<float> l(n),r(n),original(n);
 auto tone=[&](double hz,float amp){for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=original[size_t(i)]=amp*float(std::sin(2*hem::pi*hz*i/sr));};
 hem::StereoCore stereo;hem::StereoParameters sp;stereo.prepare(sr);tone(1000,.2f);checking=true;stereo.process(l.data(),r.data(),n,sp);checking=false;
 for(int i=0;i<n;++i)require(std::abs(l[size_t(i)]-original[size_t(i)])<1e-6f,"SIPA neutral changed mono signal");
 stereo.prepare(sr);tone(300,.2f);for(auto& v:r)v=-v;sp.width=0;stereo.process(l.data(),r.data(),n,sp);require(rms(l)<1e-7f,"SIPA width zero did not collapse side signal");
 stereo.prepare(sr);tone(30,.2f);for(auto& v:r)v=-v;sp.width=100;sp.bassMono=true;sp.monoBass=120;stereo.process(l.data(),r.data(),n,sp);require(rms(l)<rms(original)*.1f,"Mono bass did not suppress low side energy");
 hem::EqCore eq;hem::EqParameters ep;eq.prepare(sr);tone(300,.2f);checking=true;eq.process(l.data(),r.data(),n,ep);checking=false;require(std::abs(rms(l)/rms(original)-1)<1e-5f,"Neutral EQ changed level");
 eq.prepare(sr);tone(300,.2f);ep.db[0]=-6;eq.process(l.data(),r.data(),n,ep);require(std::abs(hem::decibels(rms(l)/rms(original))+6)<.03f,"EQ notch gain inaccurate");
 require(std::abs(hem::EqCore::response(300,sr,ep)+6)<.01f,"EQ graph response inaccurate");
 hem::CompressorCore comp;hem::CompressorParameters cp;comp.prepare(sr);std::fill(l.begin(),l.end(),.4f);r=l;checking=true;comp.process(l.data(),r.data(),n,cp);checking=false;require(std::abs(l.back()-.4f)<1e-6f,"Neutral compressor changed gain");
 comp.prepare(sr);std::fill(l.begin(),l.end(),.4f);r=l;cp.threshold=-18;cp.ratio=4;cp.attack=1;comp.process(l.data(),r.data(),n,cp);
 const float expected=(hem::decibels(.4f)+18)*.75f;require(std::abs(-hem::decibels(l.back()/.4f)-expected)<.03f,"Compressor ratio/threshold inaccurate");
 // Random wide-band transients + a swept near-Nyquist tone. Check a separate
 // convolution of emitted output, not merely the processor's displayed meter.
 hem::Reconstruction filter;hem::LimiterCore limiter;hem::LimiterParameters lp;
 for(double rate:{44100.0,48000.0,96000.0}){
  limiter.prepare(rate);std::mt19937 rng(83);std::uniform_real_distribution<float> random(-2,2);
  for(int i=0;i<n;++i){l[size_t(i)]=random(rng);r[size_t(i)]=1.7f*float(std::sin(2*hem::pi*(.4+.09*i/n)*i));}
  checking=true;limiter.process(l.data(),r.data(),n,lp);checking=false;
  double truePeak=0;for(int i=hem::Reconstruction::taps;i<n;++i)for(const auto& h:filter.h){double a=0,b=0;for(int k=0;k<hem::Reconstruction::taps;++k){a+=h[size_t(k)]*l[size_t(i-k)];b+=h[size_t(k)]*r[size_t(i-k)];}truePeak=std::max({truePeak,std::abs(a),std::abs(b)});}
  require(truePeak<=hem::gain(-1)+1e-6,"Limiter exceeded its 8x reconstruction ceiling");
  std::cout<<"Limiter "<<rate<<" Hz / measured "<<hem::decibels(float(truePeak))<<" dBTP\n";
 }
 limiter.prepare(sr);tone(997,.05f);limiter.process(l.data(),r.data(),n,lp);for(int i=limiter.latency();i<n;++i)require(std::abs(l[size_t(i)]-original[size_t(i-limiter.latency())])<1e-6f,"Limiter altered quiet signal beyond delay");
 // Stress live controls: no NaN/Inf and no allocations in any DSP block.
 eq.prepare(sr);for(int block=0;block<100;++block){ep.hz[0]=block%2?150.f:600.f;ep.db[0]=block%2?-12.f:6.f;ep.q[0]=block%2?.5f:10.f;tone(300,.2f);checking=true;eq.process(l.data(),r.data(),256,ep);checking=false;for(int i=0;i<256;++i)require(std::isfinite(l[size_t(i)]),"EQ automation became unstable");}
 require(allocations==0,"Allocation occurred inside a DSP kernel");
 hem::Delay delay;delay.prepare(13);std::fill(l.begin(),l.end(),0);r=l;l[0]=r[0]=1;delay.process(l.data(),r.data(),n);require(l[13]==1&&l[0]==0,"Delay latency mismatch");
 std::cout<<"PASS: neutral paths, M/S, mono bass, EQ response, compression law, 8x ceiling, limiter latency, automation, zero kernel allocations.\n";
 return 0;
 }catch(const std::exception& e){checking=false;std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

```

## Tests/ExternalHostTests.cpp

```cpp
#include <JuceHeader.h>
#include "../Source/Plugins/ExternalPluginHost.h"
#include "../Source/InsertChain.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const juce::String& text) {
    if (!condition) throw std::runtime_error(text.toStdString());
}
void close(float actual, float expected, const char* text) {
    require(std::abs(actual - expected) < 2.0e-5f, juce::String(text) + ": " + juce::String(actual, 8));
}
constexpr double rate = 48000;
constexpr int block = 128, budget = 288;

// Deliberately knows only the subsequently compiled in-house fixture's state
// contract. It is NOT an attempted decoder/validator for arbitrary VST3 state.
hem::ExternalPluginState fixtureState(const juce::PluginDescription& description, float gain, uint32_t mode) {
    std::array<uint8_t, 24> bytes{{'H','E','M','F','X','0','0','1'}};
    const auto put = [&bytes](size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    put(8, std::bit_cast<uint32_t>(gain)); put(12, mode);
    uint32_t checksum = 2166136261u;
    for (int i = 0; i < 20; ++i) checksum = (checksum ^ bytes[size_t(i)]) * 16777619u;
    put(20, checksum);
    juce::XmlElement xml("VST3PluginState");
    xml.createNewChildElement("IComponent")->addTextElement(juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding());
    hem::ExternalPluginState state;
    state.descriptionXml = description.createXml()->toString();
    juce::AudioProcessor::copyXmlToBinary(xml, state.state);
    return state;
}

std::unique_ptr<hem::ModuleProcessor> stage(hem::ExternalPluginHost& host,
                                          const hem::ExternalPluginState& state, bool offline = false) {
    juce::String error;
    auto result = host.stage(state, rate, block, budget, offline, error);
    require(result != nullptr, "Fixture stage failed: " + error);
    require(result->isExternal() && result->processingReady(), "Staged fixture is not a ready external processor");
    return result;
}

void processConstant(hem::ModuleProcessor& processor, float left, float right, int blocks = 3) {
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    for (int n = 0; n < blocks; ++n) {
        for (int i = 0; i < block; ++i) {
            audio.setSample(0, i, left); audio.setSample(1, i, right);
        }
        processor.processBlock(audio, midi);
    }
    require(processor.processingReady(), "Fixture became unavailable during normal processing");
}

void requireGain(hem::ModuleProcessor& processor, float gain) {
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    for (int n = 0; n < 3; ++n) {
        for (int i = 0; i < block; ++i) {
            audio.setSample(0, i, .2f); audio.setSample(1, i, -.3f);
        }
        processor.processBlock(audio, midi);
    }
    require(processor.processingReady(), "Fixture unexpectedly faulted");
    close(audio.getSample(0, block - 1), .2f * gain, "Left fixture gain");
    close(audio.getSample(1, block - 1), -.3f * gain, "Right fixture gain");
}

std::unique_ptr<juce::AudioProcessorEditor> openEditor(hem::ModuleProcessor& processor) {
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr && editor->getNumChildComponents() == 1,
            "External editor must contain exactly one native or generic child");
    require(editor->getAudioProcessor() != &processor,
            "External editor must retain/bind the actual instance rather than an empty adapter APVTS");
    return editor;
}

void requireImpulse(hem::ExternalPluginHost& host, const hem::ExternalPluginState& state,
                    bool bypassed, bool faulted, bool sameContextRestart = false) {
    hem::InsertChain chain;
    chain.prepare(rate, block);
    auto processor = stage(host, state);
    require(processor->getLatencySamples() == 64, "Fixture did not restore 64-sample latency");
    auto* pointer = processor.get();
    chain.installInitial(0, chain.stageNode(std::move(processor), rate, block, budget), bypassed);
    require(pointer->processingReady(), "First staged adapter validation must remain ready");
    if (sameContextRestart) {
        // Leave nonzero samples in the actual fixture's 64-sample delay before
        // resetting the host compensation at the identical device context.
        std::array<float, block> historyLeft{}, historyRight{};
        historyLeft.fill(.37f); historyRight.fill(-.29f);
        chain.process(historyLeft.data(), historyRight.data(), block);
        const auto priorRevision = pointer->externalStateRevision();
        chain.release();
        require(!pointer->processingReady(), "Release did not immediately quarantine stale VST3 history");
        chain.prepare(rate, block);
        require(!pointer->processingReady() && pointer->getLatencySamples() == 64
                && pointer->externalStateRevision() > priorRevision
                && pointer->externalDiagnostic().contains("Reload VST3"),
                "Same-context device restart incorrectly reused an external instance");
    }
    if (faulted) pointer->prepareToPlay(rate + 1, block);
    const int expectedPosition = budget * hem::InsertChain::slotCount;
    const int total = expectedPosition + 2 * block;
    std::vector<float> left(size_t(total), 0), right(size_t(total), 0);
    left[0] = .8f; right[0] = -.4f;
    chain.process(left.data(), right.data(), total);
    const float gain = bypassed || faulted || sameContextRestart ? 1.0f : .5f;
    for (int i = 0; i < total; ++i) {
        close(left[size_t(i)], i == expectedPosition ? .8f * gain : 0, "Compensated left impulse");
        close(right[size_t(i)], i == expectedPosition ? -.4f * gain : 0, "Compensated right impulse");
    }
    require(chain.latencySamples() == expectedPosition, "External insert changed the eight-slot fixed delay");
}

class MidBlockFault final : public hem::ModuleProcessor {
public:
    MidBlockFault() : ModuleProcessor("HOST_FAULT_TEST", "Host post-process readiness test", {}) {}
    bool isExternal() const noexcept override {return true;}
    bool processingReady() const noexcept override {return ready;}
    void prepareToPlay(double, int) override {setLatencySamples(0);}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
        for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.applyGain(ch, 0, audio.getNumSamples(), 100.0f);
        ready = false;
    }
    using ModuleProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    float meter() const noexcept override {return 0;}
    juce::String meterLabel() const override {return "TEST";}
private: bool ready = true;
};
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        require(argc == 2, "Usage: hem_external_host_tests <explicit in-house HEM VST3 Fixture.vst3 path>");
        const auto fixture = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        require(fixture.getFileName() == "HEM VST3 Fixture.vst3", "Only the explicitly supplied in-house fixture may be tested");
        hem::ExternalPluginHost host;
        juce::String error;
        const auto found = host.discoverSelected(fixture, error);
        require(found.size() == 1, "Selected-file discovery failed: " + error);
        const auto description = found.getFirst();
        require(description.name == "HEM VST3 Fixture" && description.pluginFormatName == "VST3"
                && !description.isInstrument, "Fixture descriptor mismatch");
        auto saved = fixtureState(description, .5f, 1);
        require(saved.validate(error), "Valid fixture container rejected: " + error);

        // Message-only API failures cannot instantiate or mutate an old node.
        bool offThreadRejected = false;
        std::thread invalidCaller([&] {
            juce::String threadError;
            offThreadRejected = host.stage(saved, rate, block, budget, false, threadError) == nullptr
                && threadError.contains("message thread");
        });
        invalidCaller.join();
        require(offThreadRejected && host.retainedInstanceCount() == 0, "Worker performed VST3 creation");

        auto live = stage(host, saved);
        require(live->getLatencySamples() == 64, "Saved fixture latency not restored");
        requireGain(*live, .5f);
        const auto meter = live->meterSnapshot();
        require(meter.hasProcessedAudio && meter.valueAvailable, "External sample-peak telemetry missing");
        close(meter.value, .15f, "External meter must measure actual output sample peak");
        juce::MemoryBlock notApvts;
        live->getStateInformation(notApvts);
        require(notApvts.getSize() == 0, "Ungated external state capture leaked the empty adapter APVTS");
        hem::ExternalPluginState captured;
        require(!hem::ExternalPluginHost::capture(*live, captured, error), "Ungated VST3 capture was accepted");
        require(!live->gateForStateCapture(true) && !live->processingReady(), "Capture gate failed");
        require(hem::ExternalPluginHost::capture(*live, captured, error), "Gated state capture failed: " + error);
        require(captured.state.getSize() != 0 && captured.descriptionXml == saved.descriptionXml,
                "Opaque capture lost descriptor or vendor state");
        require(live->gateForStateCapture(false) && live->processingReady(), "Gate restoration failed");
        auto offline = stage(host, captured, true);
        require(offline->externalDescriptionXml() == live->externalDescriptionXml()
                && offline->externalStateRevision() != live->externalStateRevision()
                && offline->externalStateRevision() != 0 && live->externalStateRevision() != 0,
                "Separate same-descriptor instances reused a dirty-state revision token");
        requireGain(*offline, .5f);
        offline->setNonRealtime(true);
        require(offline->processingReady(), "Matching offline mode unexpectedly invalidated fixture");

        // Opaque vendor restore has no universal success status. Our fixture's
        // known corrupt checksum is refused, leaving its fresh default gain.
        auto corrupt = fixtureState(description, .5f, 0);
        auto xml = juce::AudioProcessor::getXmlFromBinary(corrupt.state.getData(), int(corrupt.state.getSize()));
        auto* component = xml->getChildByName("IComponent");
        juce::MemoryBlock bytes;
        require(bytes.fromBase64Encoding(component->getAllSubText()), "Fixture component framing invalid");
        static_cast<uint8_t*>(bytes.getData())[20] ^= 1;
        component->deleteAllTextElements(); component->addTextElement(bytes.toBase64Encoding());
        juce::AudioProcessor::copyXmlToBinary(*xml, corrupt.state);
        auto refusedVendorState = stage(host, corrupt);
        requireGain(*refusedVendorState, 1.0f);

        const auto retainedBeforeFailure = host.retainedInstanceCount();
        auto excessive = fixtureState(description, .5f, 2);
        require(!host.stage(excessive, rate, block, budget, false, error) && error.contains("latency"),
                "Over-budget VST3 latency accepted");
        require(host.retainedInstanceCount() == retainedBeforeFailure && live->processingReady(),
                "Failed stage mutated an old instance or retained an unusable one");
        auto missing = saved;
        auto missingXml = juce::XmlDocument::parse(saved.descriptionXml);
        missingXml->setAttribute("file", fixture.getParentDirectory().getChildFile("Missing-Owned-Fixture.vst3").getFullPathName());
        missing.descriptionXml = missingXml->toString();
        require(missing.validate(error), "Missing binary must remain representable in saved metadata");
        require(!host.stage(missing, rate, block, budget, false, error) && error.contains("missing"),
                "Missing VST3 was not rejected before loading");
        require(!host.stage(saved, rate, block, budget + 1, false, error), "Non-6-ms slot budget accepted");

        auto editor = openEditor(*live);
        require(live->createEditor() == nullptr, "A second owned editor was allowed");
        auto* actual = editor->getAudioProcessor();
        auto* child = editor->getChildComponent(0);
        editor->setSize(600, 350);
        require(child->getBounds() == editor->getLocalBounds(), "Host-initiated editor resize not propagated");
        child->setSize(530, 310);
        require(editor->getWidth() == 530 && editor->getHeight() == 310, "Plugin-initiated editor resize not propagated");
        const auto oldRevision = live->externalStateRevision();
        bool gainFound = false;
        for (auto* parameter : actual->getParameters())
            if (parameter->getName(128).containsIgnoreCase("fixture gain")) {
                parameter->setValueNotifyingHost(.375f); gainFound = true; break;
            }
        require(gainFound && live->externalStateRevision() > oldRevision,
                "Actual hosted parameter callback did not update dirtiness revision");
        requireGain(*live, .75f);
        editor.reset();
        editor = openEditor(*live); // Closing and reopening cannot orphan activeEditor.
        actual = editor->getAudioProcessor();
        const int immutableLatency = live->getLatencySamples();
        // Exercise the actual instance's synchronous latency notification; no
        // inspection of a concurrently changing non-atomic JUCE latency field.
        actual->setLatencySamples(immutableLatency + 1);
        require(!live->processingReady() && live->getLatencySamples() == immutableLatency
                && live->externalDiagnostic().contains("latency"), "Dynamic latency was not safely faulted");
        live->gateForStateCapture(true);
        require(hem::ExternalPluginHost::capture(*live, captured, error), "Faulted instance state could not be preserved");
        live->gateForStateCapture(false);
        require(!live->processingReady(), "Removing temporary capture gate cleared a permanent fault");
        editor.reset();
        auto reloaded = stage(host, captured);
        requireGain(*reloaded, .75f);
        require(reloaded->getLatencySamples() == 64, "Reload retained a false mutable wrapper latency");
        reloaded->prepareToPlay(44100, block);
        require(!reloaded->processingReady() && reloaded->getLatencySamples() == 64
                && reloaded->externalDiagnostic().contains("sample rate"), "Device context change did not fail closed");
        reloaded->prepareToPlay(rate, block);
        require(!reloaded->processingReady(), "A worker prepare incorrectly cleared a reload-required fault");

        requireImpulse(host, saved, false, false);
        requireImpulse(host, saved, true, false);
        requireImpulse(host, saved, false, true);
        requireImpulse(host, saved, false, false, true);
        {
            auto repeatedPrepare = stage(host, saved);
            repeatedPrepare->prepareToPlay(rate, block);
            require(repeatedPrepare->processingReady(), "Initial same-context validation was rejected");
            repeatedPrepare->prepareToPlay(rate, block);
            require(!repeatedPrepare->processingReady()
                    && repeatedPrepare->externalDiagnostic().contains("Reload VST3"),
                    "Repeated same-context prepare without release reused stale VST3 history");
        }
        {
            hem::InsertChain chain;
            chain.prepare(rate, block);
            chain.installInitial(0, chain.stageNode(std::make_unique<MidBlockFault>(), rate, block, budget), false);
            std::vector<float> left(size_t(budget * 8 + block), 0), right(left.size(), 0);
            left[0] = .4f; right[0] = -.2f;
            chain.process(left.data(), right.data(), int(left.size()));
            for (size_t i = 0; i < left.size(); ++i) {
                close(left[i], i == size_t(budget * 8) ? .4f : 0, "Post-process fault must choose aligned dry");
                close(right[i], i == size_t(budget * 8) ? -.2f : 0, "Post-process fault right aligned dry");
            }
        }

        live.reset(); offline.reset(); refusedVendorState.reset(); reloaded.reset();
        host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Main-thread collection failed after adapters were retired");
        auto workerOwned = stage(host, saved, true);
        std::exception_ptr workerFailure;
        std::thread worker([processor = std::move(workerOwned), &workerFailure]() mutable {
            try {
                processor->prepareToPlay(rate, block); // Validation only, never actual VST3 prepare.
                processConstant(*processor, .1f, .2f);
                processor.reset(); // Does not run the real VST3 destructor on this worker.
            } catch (...) {workerFailure = std::current_exception();}
        });
        worker.join();
        if (workerFailure) std::rethrow_exception(workerFailure);
        require(host.retainedInstanceCount() == 1, "Worker destroyed the host-retained actual instance");
        host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Joined worker owner was not collected on message thread");
        auto editorOwned = stage(host, saved);
        auto heldEditor = openEditor(*editorOwned);
        editorOwned.reset();
        host.collectGarbage();
        require(host.retainedInstanceCount() == 1, "Editor did not keep the actual VST3 alive");
        heldEditor.reset(); host.collectGarbage();
        require(host.retainedInstanceCount() == 0, "Editor-close final ownership did not collect");
        std::cout << "PASS: explicit in-house VST3 discovery; stereo gain; opaque capture/reload; 64-sample latency and fixed aligned bypass; 6 ms rejection; callback dirtiness; editor reopen/resize/lifetime; worker destruction/main collection; context/latency faults; same-context release/reprepare quarantine; post-process dry fallback.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}

```

## Tests/ExternalIntegrationTests.cpp

```cpp
#include "../Source/MainComponent.h"
#include <future>
#include <thread>
#include <iostream>
#include <stdexcept>

struct MainComponentTestAccess {
 static void offline(MainComponent& h){h.deviceManager.closeAudioDevice();if(h.prepared)h.releaseResources();h.timerCallback();}
 static hem::TrackBank& bank(MainComponent& h){return *h.bank;}
 static hem::ExternalPluginHost& external(MainComponent& h){return h.externalHost;}
 static void import(MainComponent& h,const juce::StringArray& files){h.importer->submitFiles(files);}
 static bool pending(MainComponent& h){return h.importer->hasPendingWork();}
 static bool sessionBusy(MainComponent& h){return h.sessionWorker->isBusy();}
 static bool exportBusy(MainComponent& h){return h.exportWorker->isBusy();}
 static juce::String exportResult(MainComponent& h){return h.exportWorker->result();}
 static juce::String status(MainComponent& h){return h.message.getText();}
 static juce::Result install(MainComponent& h,const hem::ExternalPluginState& state,unsigned generation){return h.installExternal(state,0,0,generation);}
 static unsigned generation(MainComponent& h){return h.generation;}
 static hem::SessionSnapshot snapshot(MainComponent& h){return h.captureSession();}
 static juce::Result save(MainComponent& h,const juce::File& file){return h.saveSessionFile(file);}
 static juce::Result load(MainComponent& h,const juce::File& file){return h.loadSessionFile(file);}
 static juce::Result renderOptions(MainComponent& h,const juce::File& file,const hem::OfflineMixOptions& options){return h.startExport(file,options);}
 static juce::Result render(MainComponent& h,const juce::File& file,bool slow=false){hem::OfflineMixOptions o;o.tailSeconds=slow?30:0;o.blockSize=slow?1:256;return h.startExport(file,o);}
 static void open(MainComponent& h){h.openPlugin(0);}
 static juce::AudioProcessor* realEditorProcessor(MainComponent& h){
  std::function<juce::AudioProcessor*(juce::Component&)> find=[&](juce::Component& c)->juce::AudioProcessor*{if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(&c))return e->getAudioProcessor();for(int i=0;i<c.getNumChildComponents();++i)if(auto* p=find(*c.getChildComponent(i)))return p;return nullptr;};return find(*h.workspace);
 }
 static bool hasEditor(MainComponent& h){return h.editedProcessor!=nullptr;}
 static void closeChoice(MainComponent& h,MainComponent::CloseChoice choice){h.closePrompt=[choice](bool,bool,std::function<void(MainComponent::CloseChoice)> fn){fn(choice);};}
 static bool closePending(MainComponent& h){return h.closePhase!=MainComponent::ClosePhase::idle;}
 static void nativeRemove(MainComponent& h){h.installSelected(-1);}
 static void reload(MainComponent& h){h.reloadExternalPlugin();}
 static void select(MainComponent& h,int track){h.selectTrack(track);}
 static void context(MainComponent& h,double rate,int block){h.releaseResources();h.prepareToPlay(block,rate);h.timerCallback();}
};
namespace {
void require(bool b,const std::string& message){if(!b)throw std::runtime_error(message);}
template<class F>auto onMain(F fn){using R=decltype(fn());auto promise=std::make_shared<std::promise<R>>();auto result=promise->get_future();require(juce::MessageManager::callAsync([promise,fn=std::move(fn)]()mutable{try{if constexpr(std::is_void_v<R>){fn();promise->set_value();}else promise->set_value(fn());}catch(...){promise->set_exception(std::current_exception());}}),"Cannot post main-thread test step");return result.get();}
template<class F>void until(F done,const char* failure){for(int i=0;i<800;++i){if(onMain(done))return;juce::Thread::sleep(10);}throw std::runtime_error(failure);}
juce::TextButton* findButton(juce::Component& c,const juce::String& text){if(auto* b=dynamic_cast<juce::TextButton*>(&c);b&&b->getButtonText()==text)return b;for(int i=0;i<c.getNumChildComponents();++i)if(auto* b=findButton(*c.getChildComponent(i),text))return b;return nullptr;}
void writeFixture(const juce::File& file){std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat format;auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));require(writer!=nullptr,"Fixture writer failed");juce::AudioBuffer<float> b(2,4800);for(int i=0;i<4800;++i){b.setSample(0,i,.005f*std::sin(float(i)*.13f));b.setSample(1,i,.003f*std::cos(float(i)*.11f));}require(writer->writeFromAudioSampleBuffer(b,0,4800),"Fixture write failed");}
juce::AudioBuffer<float> readWav(const juce::File& f){juce::WavAudioFormat format;auto stream=f.createInputStream();require(stream!=nullptr,"Export file missing");std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(),true));require(reader&&reader->lengthInSamples==4800&&reader->sampleRate==48000&&reader->numChannels==2&&reader->usesFloatingPointData,"Export format/length mismatch");juce::AudioBuffer<float> b(2,4800);require(reader->read(&b,0,4800,0,true,true),"Export read failed");return b;}
void compare(const juce::AudioBuffer<float>& actual,const juce::AudioBuffer<float>& expected,float ratio,float tolerance){for(int ch=0;ch<2;++ch)for(int i=0;i<4800;++i)require(std::abs(actual.getSample(ch,i)-expected.getSample(ch,i)*ratio)<=tolerance,"Export audio does not match independently rendered native path");}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Pass the exact in-house VST3 fixture bundle, never a scan directory\n";return 2;}
 juce::ScopedJuceInitialiser_GUI gui;int result=1;const juce::File plugin(juce::String::fromUTF8(argv[1]));
 std::thread driver([&]{std::unique_ptr<MainComponent> owner;try{
  const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-vst3-integration-"+juce::Uuid().toString());require(directory.createDirectory().wasOk(),"Temporary directory failed");struct Cleanup{juce::File path;~Cleanup(){path.deleteRecursively();}}cleanup{directory};
  const auto source=directory.getChildFile("fixture.wav");writeFixture(source);juce::MemoryBlock original;require(source.loadFileAsData(original),"Fixture snapshot failed");
  onMain([&]{owner=std::make_unique<MainComponent>();MainComponentTestAccess::offline(*owner);MainComponentTestAccess::import(*owner,{source.getFullPathName()});});
  until([&]{auto* apply=findButton(*owner,"APPLY WHEN STOPPED");return apply&&apply->isEnabled();},"Import not ready");
  onMain([&]{findButton(*owner,"APPLY WHEN STOPPED")->onClick();});until([&]{return !MainComponentTestAccess::pending(*owner);},"Import did not settle");
  const auto reference=directory.getChildFile("native.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,reference).wasOk(),"Native reference export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Native reference export stalled");auto expected=readWav(reference);
  auto state=onMain([&]{juce::String error;auto descriptions=MainComponentTestAccess::external(*owner).discoverSelected(plugin,error);require(descriptions.size()==1,error.toStdString());hem::ExternalPluginState saved;saved.descriptionXml=descriptions[0].createXml()->toString();return saved;});
  onMain([&]{auto r=MainComponentTestAccess::install(*owner,state,MainComponentTestAccess::generation(*owner));require(r.wasOk(),r.getErrorMessage().toStdString());require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->isExternal(),"External node absent");});
  onMain([&]{hem::OfflineMixOptions invalid;invalid.sampleRate=std::numeric_limits<double>::quiet_NaN();require(MainComponentTestAccess::renderOptions(*owner,directory.getChildFile("invalid.wav"),invalid).failed(),"Nonfinite external export rate accepted before preparation");require(!MainComponentTestAccess::exportBusy(*owner),"Invalid options started export");invalid.sampleRate=48000;invalid.blockSize=0;require(MainComponentTestAccess::renderOptions(*owner,directory.getChildFile("invalid.wav"),invalid).failed(),"Zero external export block accepted");});
  const auto unity=directory.getChildFile("unity.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,unity).wasOk(),"VST3 export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Private VST3 export stalled");compare(readWav(unity),expected,1,0);
  std::cout<<"PASS actual native host selected VST3 load and independent export with bit-exact unity audio\n";
  const auto xml=directory.getChildFile("external.xml"),json=directory.getChildFile("external.json");
  onMain([&]{require(MainComponentTestAccess::save(*owner,xml).wasOk(),"External XML save admission failed");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"XML save stalled");
  onMain([&]{require(!owner->hasUnsavedSessionChanges(),"Successful opaque save left dirty state");MainComponentTestAccess::open(*owner);auto* actual=MainComponentTestAccess::realEditorProcessor(*owner);require(actual!=nullptr,"VST3 real editor missing");bool changed=false;for(auto* p:actual->getParameters())if(p->getName(100)=="Fixture gain"){p->setValueNotifyingHost(.25f);changed=true;}require(changed,"Fixture gain parameter absent");juce::AudioBuffer<float> b(2,256);b.clear();MainComponentTestAccess::bank(*owner).tracks[0].chain.process(b.getWritePointer(0),b.getWritePointer(1),256);});
  until([&]{return owner->hasUnsavedSessionChanges();},"External parameter change did not mark dirty");
  onMain([&]{MainComponentTestAccess::reload(*owner);require(owner->hasUnsavedSessionChanges(),"Reload of changed external state erased the unsaved-session marker");});
  onMain([&]{require(MainComponentTestAccess::save(*owner,json).wasOk(),"External JSON save admission failed");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"JSON save stalled");
  onMain([&]{require(MainComponentTestAccess::load(*owner,json).wasOk(),"External JSON recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"External JSON recall stalled");
  onMain([&]{require(!MainComponentTestAccess::hasEditor(*owner),"Recall retained editor to retired instance");auto snap=MainComponentTestAccess::snapshot(*owner);require(snap.tracks[0].slots[0].pluginId=="external-vst3"&&snap.tracks[0].slots[0].external.state.getSize()>0,"Recall lost opaque state");});
  const auto half=directory.getChildFile("half.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,half).wasOk(),"Restored export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Restored export stalled");compare(readWav(half),expected,.5f,1.e-7f);
  onMain([&]{require(MainComponentTestAccess::load(*owner,xml).wasOk(),"External XML recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"XML recall stalled");
  std::cout<<"PASS opaque XML/JSON recall, live parameter dirtiness, editor retirement and restored gain in export\n";
  auto changeLatency=[&](float normalized){onMain([&]{MainComponentTestAccess::open(*owner);auto* actual=MainComponentTestAccess::realEditorProcessor(*owner);require(actual!=nullptr,"Actual latency editor missing");bool changed=false;for(auto* p:actual->getParameters())if(p->getName(100)=="Latency mode"){p->setValueNotifyingHost(normalized);changed=true;}require(changed,"Latency parameter missing");juce::AudioBuffer<float> block(2,256);block.clear();MainComponentTestAccess::bank(*owner).tracks[0].chain.process(block.getWritePointer(0),block.getWritePointer(1),256);});onMain([&]{MainComponentTestAccess::select(*owner,1);});until([&]{return !MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady();},"Actual asynchronous VST3 latency restart did not gate processing");until([&]{return MainComponentTestAccess::status(*owner).contains("TRACK 1 / SLOT 1");},"Unselected external fault did not produce a visible global warning");onMain([&]{MainComponentTestAccess::select(*owner,0);});};
  changeLatency(.5f);
  onMain([&]{auto& chain=MainComponentTestAccess::bank(*owner).tracks[0].chain;juce::AudioBuffer<float> zero(2,256);for(int i=0;i<12;++i){zero.clear();chain.process(zero.getWritePointer(0),zero.getWritePointer(1),256);}juce::AudioBuffer<float> impulse(2,3000);impulse.clear();impulse.setSample(0,0,.125f);impulse.setSample(1,0,-.0625f);chain.process(impulse.getWritePointer(0),impulse.getWritePointer(1),3000);for(int i=0;i<3000;++i){require(impulse.getSample(0,i)==(i==2304?.125f:0.f),"Faulted external latency silently misaligned left dry signal");require(impulse.getSample(1,i)==(i==2304?-.0625f:0.f),"Faulted external latency silently misaligned right dry signal");}MainComponentTestAccess::reload(*owner);auto* p=chain.activeProcessor(0);require(p->processingReady()&&p->getLatencySamples()==64,"Bounded latency reload did not revalidate64 samples");});
  const auto delayed=directory.getChildFile("delayed.wav");onMain([&]{require(MainComponentTestAccess::render(*owner,delayed).wasOk(),"Delayed VST3 export rejected");});until([&]{return !MainComponentTestAccess::exportBusy(*owner);},"Delayed export stalled");compare(readWav(delayed),expected,1,0);
  onMain([&]{MainComponentTestAccess::context(*owner,96000,512);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Device context change failed to gate external instance");MainComponentTestAccess::reload(*owner);auto* p=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);require(p->processingReady()&&p->actualRate.load()==96000,"Reload did not use actual new context");MainComponentTestAccess::offline(*owner);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Offline reprepare did not invalidate mismatched instance");MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Offline reload failed");MainComponentTestAccess::context(*owner,48000,256);require(!MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Same-context restart silently retained vendor history");MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)->processingReady(),"Same-context restart reload failed");});
  changeLatency(1.f);
  onMain([&]{auto* old=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);MainComponentTestAccess::reload(*owner);require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)==old&&!old->processingReady(),"Excess-latency rejection replaced or re-enabled old faulted state");require(MainComponentTestAccess::status(*owner).contains("6 ms"),"6ms rejection not visible to user");require(MainComponentTestAccess::load(*owner,xml).wasOk(),"Recovery recall rejected");});until([&]{return !MainComponentTestAccess::sessionBusy(*owner);},"Recovery recall stalled");
  std::cout<<"PASS real asynchronous VST3 latency notification, exact aligned dry fallback, 64-sample reload/export, actual-context reprepare and visible 6 ms rejection with state retained\n";

  onMain([&]{auto* old=MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0);require(MainComponentTestAccess::install(*owner,state,MainComponentTestAccess::generation(*owner)-1).failed(),"Stale generation accepted");auto missing=state;juce::PluginDescription d;juce::String error;require(missing.getDescription(d,error),"Descriptor decode failed");d.fileOrIdentifier=directory.getChildFile("missing.vst3").getFullPathName();missing.descriptionXml=d.createXml()->toString();require(MainComponentTestAccess::install(*owner,missing,MainComponentTestAccess::generation(*owner)).failed(),"Missing binary accepted");require(MainComponentTestAccess::bank(*owner).tracks[0].chain.activeProcessor(0)==old,"Failed load replaced old insert");});
  int closes=0;const auto cancelled=directory.getChildFile("cancelled.wav");
  onMain([&]{require(MainComponentTestAccess::render(*owner,cancelled,true).wasOk(),"Cancellation export rejected");MainComponentTestAccess::closeChoice(*owner,MainComponent::CloseChoice::cancel);owner->requestClose([&]{++closes;});require(closes==0&&MainComponentTestAccess::exportBusy(*owner),"Keep open cancelled external export");MainComponentTestAccess::closeChoice(*owner,MainComponent::CloseChoice::discard);owner->requestClose([&]{++closes;});});
  until([&]{return closes==1;},"Close during external preparation/export deadlocked");require(!cancelled.exists(),"Cancelled external export published output");
  onMain([&]{require(MainComponentTestAccess::load(*owner,json).wasOk(),"Pending recall admission failed");owner->requestClose([&]{++closes;});});until([&]{return closes==2;},"Close during pending external recall deadlocked");
  until([&]{MainComponentTestAccess::external(*owner).collectGarbage();return MainComponentTestAccess::external(*owner).retainedInstanceCount()==1;},"Retired/export VST3 owners leaked");
  std::cout<<"PASS missing/stale load preserves slot, close cancellation, pending export/recall teardown and owner collection\n";
  juce::MemoryBlock after;require(source.loadFileAsData(after)&&after==original,"Source WAV bytes changed");
  onMain([&]{require(MainComponentTestAccess::render(*owner,directory.getChildFile("shutdown.wav"),true).wasOk(),"Shutdown export rejected");owner.reset();});
  std::cout<<"PASS synchronous host destruction with pending private VST3 preparation, source bytes preserved\n";result=0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';try{onMain([&]{owner.reset();});}catch(...){} }
 juce::MessageManager::callAsync([]{juce::MessageManager::getInstance()->stopDispatchLoop();});});
 juce::MessageManager::getInstance()->runDispatchLoop();driver.join();return result;
}

```

## Tests/ExternalSessionTests.cpp

```cpp
#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Action>bool rejected(Action action){try{action();}catch(const std::exception&){return true;}return false;}
template<class Condition>void until(Condition done,const char* message){
    for(int i=0;i<2000&&!done();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
    require(done(),message);
}
hem::SessionSnapshot nativeSnapshot(){
    hem::SessionSnapshot state;hem::MonitorProcessor monitor;
    state.monitorParameters=hem::SessionState::captureParameters(monitor);
    auto& native=state.tracks[0].slots[1];native.pluginId="sipa";
    hem::SipaProcessor processor;native.parameters=hem::SessionState::captureParameters(processor);
    return state;
}
hem::ExternalPluginState fixtureState(const juce::File& file){
    juce::PluginDescription description;description.name="Container-only test effect";
    description.descriptiveName=description.name;description.pluginFormatName="VST3";
    description.category="Fx";description.manufacturerName="HEM tests";description.version="1";
    description.fileOrIdentifier=file.getFullPathName();description.uniqueId=0x12345678;
    description.numInputChannels=2;description.numOutputChannels=2;description.isInstrument=false;
    hem::ExternalPluginState state;state.descriptionXml=description.createXml()->toString();
    const unsigned char opaque[]={0,0xff,0x10,0,0x7f,0x80,0x42,0x42,1};
    state.state=juce::MemoryBlock(opaque,sizeof(opaque));return state;
}
juce::ValueTree slot(juce::ValueTree root){return root.getChild(1).getChild(0);}
juce::ValueTree external(juce::ValueTree root){return slot(root).getChildWithName("EXTERNAL");}
// This is a test-owned ModuleProcessor, not a dynamically loaded plug-in. It
// proves staging/capture routing without scanning, loading, or running a binary.
class MetadataOnlyProcessor final:public hem::ModuleProcessor {
public:
    explicit MetadataOnlyProcessor(juce::String xml):ModuleProcessor("test-external","Metadata only",{}),description(std::move(xml)){}
    bool isExternal() const noexcept override{return true;}
    juce::String externalDescriptionXml() const override{return description;}
    uint64_t externalStateRevision() const noexcept override{return 73;}
    bool processingReady() const noexcept override{return prepared;}
    void prepareToPlay(double,int) override{prepared=true;}
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override{}
    juce::AudioProcessorEditor* createEditor() override{return nullptr;}
    float meter() const noexcept override{return 0;}
    juce::String meterLabel() const override{return {};}
private:
    juce::String description;bool prepared=false;
};
void workerTests(const juce::File& file,const hem::SessionSnapshot& expected){
    juce::TimeSliceThread disk("Session container tests");require(disk.startThread(),"Cannot start test read-ahead thread");
    auto live=std::make_unique<hem::TrackBank>();live->tracks[0].name="UNCHANGED";auto* original=live.get();
    int commits=0,completions=0,stages=0;bool completedOk=false;
    auto commit=[&](std::unique_ptr<hem::TrackBank>& bank,const hem::SessionSnapshot&,hem::SessionWorker::Context){
        ++commits;live.swap(bank);return juce::Result::ok();
    };
    auto complete=[&](bool saving,const juce::File&,const juce::Result& result){require(!saving,"Unexpected save callback");++completions;completedOk=result.wasOk();};
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete);
        require(worker.load(file,{48000,256,1}),"Missing-callback load not admitted");
        until([&]{return !worker.isBusy();},"Missing-callback load did not settle");
        require(completions==1&&!completedOk&&commits==0&&live.get()==original,"Missing staging callback changed live bank");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [&](hem::TrackBank& bank,const hem::SessionSnapshot& state,hem::SessionWorker::Context)->juce::Result{
                ++stages;require(juce::MessageManager::getInstance()->isThisTheMessageThread(),"Staging ran off main thread");
                require(bank.tracks[0].chain.activeProcessor(0)==nullptr,"Worker instantiated an external slot");
                require(bank.tracks[0].chain.activeProcessor(1)!=nullptr,"Worker omitted native staging");
                require(state.tracks[0].slots[0].external.state==expected.tracks[0].slots[0].external.state,"Opaque state lost before callback");
                throw std::runtime_error("Intentional test staging failure");
            });
        require(worker.load(file,{48000,256,1}),"Throwing-stager load not admitted");
        until([&]{return !worker.isBusy();},"Throwing-stager load did not settle");
        require(completions==2&&!completedOk&&commits==0&&stages==1&&live.get()==original,"Staging exception changed live bank");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [](hem::TrackBank&,const hem::SessionSnapshot&,hem::SessionWorker::Context){return juce::Result::ok();});
        require(worker.load(file,{48000,256,1}),"Empty-stager load not admitted");
        until([&]{return !worker.isBusy();},"Empty-stager load did not settle");
        require(completions==3&&!completedOk&&commits==0&&live.get()==original,"Empty staging silently dropped external state");
    }
    {
        hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete,
            [&](hem::TrackBank& bank,const hem::SessionSnapshot& state,hem::SessionWorker::Context context){
                ++stages;auto& track=bank.tracks[0];
                auto processor=std::make_unique<MetadataOnlyProcessor>(state.tracks[0].slots[0].external.descriptionXml);
                auto node=track.chain.stageNode(std::move(processor),context.sampleRate,context.blockSize,track.chain.slotLatency());
                track.chain.installInitial(0,std::move(node),state.tracks[0].slots[0].bypassed);
                return juce::Result::ok();
            });
        require(worker.load(file,{48000,256,1}),"Successful staging load not admitted");
        until([&]{return !worker.isBusy();},"Successful staging load did not settle");
        require(completions==4&&completedOk&&commits==1&&stages==2&&live.get()!=original,"Staged external bank did not commit");
        require(live->tracks[0].chain.isBypassed(0),"External bypass state lost");
        hem::MonitorProcessor monitor;
        const auto fast=hem::SessionState::capture(*live,0,0,0,false,monitor,false);
        const auto& captured=fast.tracks[0].slots[0];
        require(captured.pluginId==hem::SessionState::externalPluginId&&captured.externalRevision==73,"Fast external metadata capture failed");
        require(captured.external.descriptionXml==expected.tracks[0].slots[0].external.descriptionXml&&captured.external.state.getSize()==0
                &&captured.parameters.getNumChildren()==0,"Fast capture fabricated state or omitted descriptor");
    }
    // Destroying a just-admitted load joins its worker without waiting for any
    // message-thread staging callback. The old/live bank remains intact.
    const int before=commits;
    {hem::SessionWorker worker(disk,commit,[](const juce::String&){},complete);require(worker.load(file,{48000,256,1}),"Cancellation load not admitted");}
    require(commits==before,"Destructor cancellation committed a session");
    live.reset();disk.stopThread(-1);
}
}

int main(){juce::ScopedJuceInitialiser_GUI runtime;try{
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-external-session-"+juce::Uuid().toString());
    require(directory.createDirectory().wasOk(),"Cannot create test directory");
    struct Cleanup{juce::File directory;~Cleanup(){directory.deleteRecursively();}} cleanup{directory};
    const auto plugin=directory.getChildFile("container-only.vst3");
    require(plugin.replaceWithText("Container-only descriptor fixture. This is not a plug-in binary."),"Cannot create inert file fixture");
    const auto xml=directory.getChildFile("session.xml"),json=directory.getChildFile("session.json");
    auto native=nativeSnapshot();
    require(int(hem::SessionState::encode(native,xml)["version"])==1,"Native-only sessions no longer write v1");
    hem::PresetManager::save(native,xml);require(!hem::SessionState::hasExternal(hem::PresetManager::load(xml)),"Native v1 recall failed");
    auto state=native;auto& ext=state.tracks[0].slots[0];ext.pluginId=hem::SessionState::externalPluginId;
    ext.external=fixtureState(plugin);ext.externalRevision=901;ext.bypassed=true;
    auto base=hem::SessionState::encode(state,xml);require(int(base["version"])==2,"External session did not write v2");
    require(slot(base).getNumChildren()==2&&slot(base).getChildWithName("PARAMETERS").getNumChildren()==0,"External schema fabricated native parameters");
    require(!slot(base).hasProperty("externalRevision"),"Transient revision leaked into session");
    for(const auto& file:{xml,json}){
        hem::PresetManager::save(state,file);const auto loaded=hem::PresetManager::load(file);const auto& restored=loaded.tracks[0].slots[0];
        require(restored.external.state==ext.external.state&&restored.external.descriptionXml==ext.external.descriptionXml&&restored.bypassed,"External opaque round trip failed");
        require(restored.externalRevision==0&&restored.parameters.getNumChildren()==0,"Recall persisted a revision or fabricated APVTS");
    }
    auto rejects=[&](juce::ValueTree tree){return rejected([&]{hem::SessionState::decode(tree,xml);});};
    auto tree=base.createCopy();tree.setProperty("version",1,nullptr);require(rejects(tree),"v1 accepted external state");
    tree=base.createCopy();tree.setProperty("version",3,nullptr);require(rejects(tree),"Unknown schema accepted");
    tree=base.createCopy();slot(tree).setProperty("pluginId","sipa",nullptr);require(rejects(tree),"Native ID accepted external container");
    tree=base.createCopy();slot(tree).setProperty("pluginId","",nullptr);require(rejects(tree),"Empty ID accepted external container");
    tree=base.createCopy();slot(tree).removeChild(external(tree),nullptr);require(rejects(tree),"External ID accepted absent state");
    tree=base.createCopy();slot(tree).getChildWithName("PARAMETERS").setProperty("notEmpty",1,nullptr);require(rejects(tree),"External slot accepted native parameter properties");
    tree=base.createCopy();slot(tree).getChildWithName("PARAMETERS").addChild(juce::ValueTree("PARAM"),-1,nullptr);require(rejects(tree),"External slot accepted native parameter children");
    tree=base.createCopy();external(tree).setProperty("unknown",1,nullptr);require(rejects(tree),"Unknown external property accepted");
    tree=base.createCopy();external(tree).removeProperty("stateBase64",nullptr);require(rejects(tree),"Missing external property accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",3,nullptr);external(tree).setProperty("stateBase64",1234,nullptr);require(rejects(tree),"Numeric Base64 property accepted");
    tree=base.createCopy();external(tree).addChild(juce::ValueTree("PARAMETERS"),-1,nullptr);require(rejects(tree),"External child accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",100,nullptr);require(rejects(tree),"Mismatched state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",-1,nullptr);require(rejects(tree),"Negative state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1.5,nullptr);require(rejects(tree),"Fractional state length accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",juce::int64(hem::ExternalPluginState::maxStateBytes+1),nullptr);require(rejects(tree),"Oversize declared state accepted");
    tree=base.createCopy();external(tree).setProperty("stateBase64","!!!!!!!!!!!!",nullptr);require(rejects(tree),"Invalid Base64 accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1,nullptr);external(tree).setProperty("stateBase64","AB==",nullptr);require(rejects(tree),"Noncanonical Base64 padding bits accepted");
    tree=base.createCopy();external(tree).setProperty("stateBytes",1,nullptr);external(tree).setProperty("stateBase64","AA= ",nullptr);require(rejects(tree),"Whitespace Base64 accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml","<OTHER/>",nullptr);require(rejects(tree),"Malformed descriptor accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml",juce::String::repeatedString("x",int(hem::ExternalPluginState::maxDescriptionBytes+1)),nullptr);require(rejects(tree),"Oversized descriptor accepted");
    tree=base.createCopy();external(tree).setProperty("descriptionXml","<!DOCTYPE PLUGIN [<!ENTITY test 'bad'>]>"+ext.external.descriptionXml,nullptr);require(rejects(tree),"Descriptor declaration accepted");
    auto badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);require(badDescription!=nullptr,"Fixture descriptor parse failed");
    badDescription->setAttribute("format","AudioUnit");tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);require(rejects(tree),"Non-VST3 descriptor accepted");
    for(const auto& field:{"uniqueId","numInputs","numOutputs","isInstrument","isShell","hasARAExtension","uid","fileTime","infoUpdateTime"}){
        badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);badDescription->setAttribute(field,"junk");
        tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);
        require(rejects(tree),"Malformed descriptor number accepted");
    }
    for(const auto& field:{"uniqueId","numInputs","numOutputs","isInstrument"}){
        badDescription=juce::XmlDocument::parse(ext.external.descriptionXml);badDescription->removeAttribute(field);
        tree=base.createCopy();external(tree).setProperty("descriptionXml",badDescription->toString(),nullptr);
        require(rejects(tree),"Missing descriptor number accepted");
    }
    auto noBlob=state;noBlob.tracks[0].slots[0].external.state.reset();
    for(const auto& file:{xml,json}){hem::PresetManager::save(noBlob,file);require(hem::PresetManager::load(file).tracks[0].slots[0].external.state.getSize()==0,"Valid zero-length state rejected");}
    // Oversize output must fail before replacing either existing file.
    auto tooLarge=state;tooLarge.tracks[0].slots[0].external.state.setSize(7*1024*1024,true);
    for(const auto& file:{xml,json}){
        require(file.replaceWithText("KEEP EXISTING SESSION"),"Cannot seed previous session");
        require(rejected([&]{hem::PresetManager::save(tooLarge,file);}),"Oversize output accepted");
        require(file.loadFileAsString()=="KEEP EXISTING SESSION","Oversize save replaced previous session");
    }
    auto tooLargeJson=native;tooLargeJson.tracks[0].name=juce::String::repeatedString("\\",4*1024*1024);
    require(rejected([&]{hem::PresetManager::save(tooLargeJson,json);}),"JSON-specific escaping exceeded size limit");
    require(json.loadFileAsString()=="KEEP EXISTING SESSION","Oversize JSON replaced previous session");
    const auto largeText=juce::String::repeatedString(" ",int(hem::SessionState::maxSerializedBytes+1));
    for(const auto& file:{xml,json}){require(file.replaceWithText(largeText),"Cannot write oversized test input");require(rejected([&]{hem::PresetManager::load(file);}),"Oversize input accepted");}
    // Persist while a selected binary is gone is allowed; recall must fail
    // before constructing any processors or staging into the live graph.
    require(plugin.deleteFile(),"Cannot remove inert fixture");
    for(const auto& file:{xml,json}){hem::PresetManager::save(state,file);require(rejected([&]{hem::PresetManager::load(file);}),"Missing binary accepted on recall");}
    require(plugin.replaceWithText("Inert fixture restored"),"Cannot restore inert fixture");
    hem::PresetManager::save(state,xml);workerTests(xml,state);
    std::cout<<"PASS external session XML/JSON: opaque bytes, v1 compatibility, malformed/mixed/oversize rejection, missing-file recall, atomic save preservation, main-thread staging transaction and cancellation; no plug-in binary loaded\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

```

## Tests/FinishingTests.cpp

```cpp
#include "../Source/DSP/FinishingCore.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool counting=false;static size_t allocations=0;
void* operator new(std::size_t n){if(counting)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
constexpr int block=128;using Buffer=std::array<float,block>;
template<class Core,class Settings>void neutral(Core& core,const Settings& p,double sr,int latency,bool settle=false){
 core.prepare(sr);Buffer l{},r{};
 if(settle)for(int b=0;b<100;++b){l.fill(0);r.fill(0);core.process(l.data(),r.data(),block,p);}
 for(int b=0;b<30;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.2f*std::sin(float((b*block+i)*.037));
 counting=true;core.process(l.data(),r.data(),block,p);counting=false;
 for(int i=0;i<block;++i){const int n=b*block+i-latency;const float expected=n<0?0:.2f*std::sin(float(n*.037));if(std::abs(l[size_t(i)]-expected)>=2.e-6f){std::cerr<<typeid(Core).name()<<" block "<<b<<" sample "<<i<<" got "<<l[size_t(i)]<<" expected "<<expected<<"\n";throw std::runtime_error("Neutral or latency test failed");}}}
}
template<class Core,class Settings>std::array<double,4> tone(Core& c,const Settings& p,double sr,double hz){
 c.prepare(sr);Buffer l{},r{};double in=0,out=0,s2=0,c2=0,s3=0,c3=0;int count=0;
 const int end=int(sr),begin=int(sr/2);
 for(int start=0;start<end;start+=block){const int n=std::min(block,end-start);
 for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.25f*float(std::sin(2*hem::pi*hz*(start+i)/sr));
 counting=true;c.process(l.data(),r.data(),n,p);counting=false;
 for(int i=0;i<n;++i)if(start+i>=begin){const double phase=2*hem::pi*hz*(start+i)/sr,x=.25*std::sin(phase),y=l[size_t(i)];in+=x*x;out+=y*y;s2+=y*std::sin(phase*2);c2+=y*std::cos(phase*2);s3+=y*std::sin(phase*3);c3+=y*std::cos(phase*3);++count;}}
 return {10*std::log10(out/in),2*std::hypot(s2,c2)/count,2*std::hypot(s3,c3)/count,std::sqrt(out/count)};
}
int main(){try{for(double sr:{44100.,48000.,96000.}){
 hem::VocalCore v;hem::TubeCore u;hem::TapeCore t;hem::MonitorCore m;
 neutral(v,hem::VocalSettings{},sr,0);neutral(u,hem::TubeSettings{},sr,16);neutral(t,hem::TapeSettings{},sr,16);neutral(m,hem::MonitorSettings{},sr,0);
 hem::VocalSettings voice;voice.body=6;auto vocal=tone(v,voice,sr,150);require(std::abs(vocal[0]-6)<.02,"Vocal body EQ not 6 dB");
 voice.body=0;voice.resonance=6800;voice.threshold=-45;voice.range=6;auto notch=tone(v,voice,sr,6800);require(std::abs(notch[0]+6)<.03,"Vocal resonance reduction wrong");
 hem::TubeSettings tube;tube.drive=12;tube.bias=30;auto tubeResult=tone(u,tube,sr,1000);require(tubeResult[1]>.002&&tubeResult[2]>.002,"Tube did not create even and odd harmonics");
 tube.mix=0;neutral(u,tube,sr,16,true);
 hem::TapeSettings tape;tape.drive=12;auto tapeResult=tone(t,tape,sr,1000);require(tapeResult[2]>.002,"Tape saturation harmonic missing");tape.mix=0;neutral(t,tape,sr,16,true);
 hem::MonitorSettings monitor;monitor.body=-6;monitor.enabled=true;auto correction=tone(m,monitor,sr,250);require(std::abs(correction[0]+6)<.02,"Monitor EQ attenuation wrong");monitor.enabled=false;neutral(m,monitor,sr,0);
 Buffer l{},r{};for(int b=0;b<100;++b){voice.cutEnabled=b%2;voice.body=b%2?12:-12;voice.range=b%2?18:0;voice.resonance=b%2?1000:10000;tube.drive=b%2?24:0;tube.bias=b%2?50:-50;tube.mix=100;tape.drive=b%2?24:0;tape.rolloff=b%2?-12:0;tape.mix=100;monitor.enabled=b%2;monitor.air=b%2?6:-12;
 for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.1f*std::sin(float(b*block+i));
 counting=true;v.process(l.data(),r.data(),block,voice);u.process(l.data(),r.data(),block,tube);t.process(l.data(),r.data(),block,tape);m.process(l.data(),r.data(),block,monitor);counting=false;
 for(float x:l)require(std::isfinite(x)&&std::abs(x)<10,"Automation unstable");}
 std::cout<<"PASS "<<sr<<" Hz / vocal "<<vocal[0]<<" dB / dynamic "<<notch[0]<<" dB / tube H2 "<<tubeResult[1]<<" H3 "<<tubeResult[2]<<" / tape H3 "<<tapeResult[2]<<" / monitor "<<correction[0]<<" dB\n";
 }require(allocations==0,"Audio DSP allocated");std::cout<<"PASS neutral startup, 16-sample saturation latency, dry mix, disabled monitor, finite automation, zero DSP allocations\n";return 0;
 }catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/HardwareLookAndFeelTests.cpp

```cpp
#include "../Source/HardwareLookAndFeel.h"
#include "../Source/Systems/CalibratedMeterView.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int differences(const juce::Image& first, const juce::Image& second) {
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            if (first.getPixelAt(x, y) != second.getPixelAt(x, y)) ++count;
    return count;
}
juce::Image dial(bool artwork, float position, bool enabled = true) {
    HardwareLookAndFeel look(artwork);
    juce::Slider slider;
    slider.setBounds(0, 0, 124, 136);
    slider.setEnabled(enabled);
    juce::Image image(juce::Image::ARGB, 124, 136, true);
    juce::Graphics g(image);
    look.drawRotarySlider(g, 13, 17, 94, 98, position,
                          juce::MathConstants<float>::pi * 1.2f,
                          juce::MathConstants<float>::pi * 2.8f, slider);
    return image;
}
bool hasAmber(const juce::Image& image, float x, float y) {
    for (int iy = int(y) - 2; iy <= int(y) + 2; ++iy)
        for (int ix = int(x) - 2; ix <= int(x) + 2; ++ix) {
            const auto colour = image.getPixelAt(ix, iy);
            if (colour.getFloatRed() > 0.75f && colour.getFloatGreen() > 0.35f
                && colour.getFloatBlue() < 0.5f && colour.getFloatAlpha() > 0.8f) return true;
        }
    return false;
}
juce::Image button(bool artwork, bool active, bool down, bool enabled = true) {
    HardwareLookAndFeel look(artwork);
    juce::TextButton control("PLAY");
    control.setBounds(0, 0, 102, 55);
    control.setLookAndFeel(&look);
    control.setEnabled(enabled);
    control.setToggleState(active, juce::dontSendNotification);
    juce::Image image(juce::Image::ARGB, 102, 55, true);
    juce::Graphics g(image);
    look.drawButtonBackground(g, control, control.findColour(juce::TextButton::buttonColourId), false, down);
    look.drawButtonText(g, control, false, down);
    control.setLookAndFeel(nullptr);
    return image;
}
juce::Image fader(bool artwork, bool horizontal, float position) {
    HardwareLookAndFeel look(artwork);
    juce::Slider slider;
    slider.setBounds(0, 0, horizontal ? 200 : 50, horizontal ? 50 : 200);
    slider.setSliderStyle(horizontal ? juce::Slider::LinearHorizontal : juce::Slider::LinearVertical);
    juce::Image image(juce::Image::ARGB, slider.getWidth(), slider.getHeight(), true);
    juce::Graphics g(image);
    look.drawLinearSlider(g, horizontal ? 10 : 5, horizontal ? 5 : 10,
                          horizontal ? 180 : 40, horizontal ? 40 : 180,
                          position, 10, 190, slider.getSliderStyle(), slider);
    return image;
}

juce::Image meterImage(hem::AudioMeters& meters, int width = 190, int height = 310) {
    // Use the production Component and actual post-push atomic meter state.
    // No test-only state injection, painted substitute or timer approximation.
    hem::CalibratedMeterView view(meters);
    view.setBounds(0, 0, width, height);
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    view.paint(g);
    return image;
}
int firstPeakFill(const juce::Image& image, int x, int top, int bottom) {
    const juce::Colour peakFill(0xff235767);
    for (int y = top; y < bottom; ++y)
        if (image.getPixelAt(x, y) == peakFill) return y;
    return -1;
}
void requireOpaque(const juce::Image& image) {
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            require(image.getPixelAt(x, y).getAlpha() == 255,
                    "Meter lets decorative faceplate pixels show through");
}
void testMeterRaster() {
    hem::AudioMeters source;
    require(source.prepare(1000), "Meter test source failed to prepare");
    std::vector<float> left(3000, 0.5f), right(3000, 0.25f);
    source.push(left.data(), right.data(), int(left.size()));
    const auto actual = source.snapshot();
    require(std::abs(actual.left.peakDb + 6.0206f) < 0.001f
            && std::abs(actual.right.peakDb + 12.0412f) < 0.001f,
            "Meter raster input is not a real calibrated detector snapshot");
    for (const int scale : {1, 2}) {
        const auto image = meterImage(source, 190 * scale, 310 * scale);
        requireOpaque(image);
        const auto yFor = [scale](float db) {
            return float(scale) * (238.0f - 186.0f * (db + 60.0f) / 60.0f);
        };
        const int leftFill = firstPeakFill(image, 55 * scale, 52 * scale, 238 * scale);
        const int rightFill = firstPeakFill(image, 125 * scale, 52 * scale, 238 * scale);
        require(leftFill >= 0 && rightFill >= 0, "Prepared audio has no visible peak bars on first paint");
        require(std::abs(float(leftFill) - yFor(actual.left.peakDb)) <= 3.0f
                && std::abs(float(rightFill) - yFor(actual.right.peakDb)) <= 3.0f,
                "Meter bar height is not calibrated to the fixed -60..0 dBFS scale");
        require(rightFill > leftFill + 12 * scale, "Independent L/R levels are visually merged");
        require(image.getPixelAt(55 * scale, 80 * scale) == juce::Colour(0xff235767)
                && image.getPixelAt(125 * scale, 80 * scale) == juce::Colour(0xff02070b),
                "A -6 dBFS left and -12 dBFS right signal do not render separately");
        require(image.getPixelAt(70 * scale, 110 * scale) == juce::Colour(0xff35baa9),
                "Real RMS level is missing from the inside of the channel bar");
        require(image.getPixelAt(55 * scale, 60 * scale) == juce::Colour(0xff02070b),
                "Meter normalizes a quiet signal to the top of its scale");
        // Between the independent channel lanes, every documented dBFS tick must
        // remain at its absolute calibration coordinate. Tolerate antialiasing.
        const auto background = juce::Colour(0xff061017);
        for (const float tick : {0.0f, -6.0f, -12.0f, -24.0f, -48.0f, -60.0f}) {
            const int y = int(std::round(yFor(tick)));
            bool gridFound = false;
            for (int offset = -1; offset <= 1; ++offset)
                gridFound = gridFound || image.getPixelAt(104 * scale, y + offset) != background;
            require(gridFound, "A fixed dBFS calibration grid line is missing or misplaced");
        }
    }

    // Swapping real channel samples must swap the bars, without changing scale.
    source.prepare(1000);
    source.push(right.data(), left.data(), int(left.size()));
    const auto swapped = meterImage(source);
    require(swapped.getPixelAt(55, 80) == juce::Colour(0xff02070b)
            && swapped.getPixelAt(125, 80) == juce::Colour(0xff235767),
            "Meter channel swap does not follow actual source samples");

    source.prepare(48000);
    source.push(nullptr, nullptr, 256);
    const auto silent = meterImage(source);
    requireOpaque(silent);
    require(firstPeakFill(silent, 55, 52, 238) < 0 && firstPeakFill(silent, 125, 52, 238) < 0,
            "Silent audio produces a fictitious peak bar");
    require(silent.getPixelAt(70, 150) == juce::Colour(0xff02070b)
            && silent.getPixelAt(140, 150) == juce::Colour(0xff02070b),
            "Silent audio produces a fictitious RMS bar");

    const float fullScale = 1.0f, halfScale = 0.5f;
    source.push(&fullScale, &halfScale, 1);
    const auto clipped = meterImage(source);
    require(clipped.getPixelAt(60, 46) == juce::Colour(0xffff504a)
            && clipped.getPixelAt(130, 46) == juce::Colour(0xff28353d),
            "Clip lamps do not reflect independent actual channel latches");
    source.resetClip();
    source.push(nullptr, nullptr, 0);
    const auto reset = meterImage(source);
    require(reset.getPixelAt(60, 46) == juce::Colour(0xff28353d),
            "Actual audio-thread clip reset is not reflected on first paint");
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        for (const bool artwork : {false, true}) {
            const auto minimum = dial(artwork, 0.0f), middle = dial(artwork, 0.5f), maximum = dial(artwork, 1.0f);
            require(differences(minimum, middle) > 30 && differences(middle, maximum) > 30,
                    "Dial pixels do not respond to normalized parameter position");
            require(differences(middle, dial(artwork, 0.5f, false)) > 30,
                    "Disabled dial has no visible disabled state");
            require(middle.getPixelAt(0, 0).getAlpha() == 0 && middle.getPixelAt(123, 135).getAlpha() == 0,
                    "Dial paint escapes its local bounds");
            const auto expected = hem::ui::dialGeometry(13, 17, 94, 98, 0.5f,
                juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f);
            require(hasAmber(middle, expected.pointerEndX, expected.pointerEndY),
                    "Actual raster dial pointer is not at its computed position");
            const auto off = button(artwork, false, false), on = button(artwork, true, false);
            require(differences(off, on) > 100, "Actual toggle state does not illuminate the button");
            require(differences(off, button(artwork, false, true)) > 100,
                    "Momentary press does not illuminate the button");
            require(differences(off, button(artwork, false, false)) == 0,
                    "Momentary release does not return to stable off appearance");
            require(differences(on, button(artwork, true, false, false)) > 30,
                    "Disabled button has no visible disabled state");
            require(differences(fader(artwork, true, 10), fader(artwork, true, 190)) > 50,
                    "Horizontal fader pixels do not follow the thumb position");
            require(differences(fader(artwork, false, 10), fader(artwork, false, 190)) > 50,
                    "Vertical fader pixels do not follow the thumb position");
        }
        testMeterRaster();
        std::cout << "PASS: native JUCE dial/slider raster changes, bounds, pointer position, "
                     "button active/press/release and disabled states in standard/artwork modes; "
                     "actual meter first-paint snapshots, fixed dBFS scale/bars at 1x/2x, stereo "
                     "separation/swap, RMS, opaque silence and clip/reset lamps\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

```

## Tests/ImportTests.cpp

```cpp
#include <JuceHeader.h>
#include "../Source/StemFileImporter.h"
#include <future>
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){juce::ScopedJuceInitialiser_GUI gui;juce::TimeSliceThread disk("Importer test read-ahead");disk.startThread();
 const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-import-test","",false);folder.createDirectory();
 try{
    hem::StemFileImporter zone(disk,[]{return hem::StemFileImporter::Context{96000,256,1,false};},[](hem::StemFileImporter::Batch&){return juce::Result::ok();});
    juce::StringArray names;for(int i=0;i<13;++i)names.add("stem"+juce::String(i)+".WAV");require(zone.isInterestedInFileDrag(names),"13 WAV files rejected");names.add("extra.wav");require(!zone.isInterestedInFileDrag(names),"14 files accepted");require(!zone.isInterestedInFileDrag({"audio.mp3"}),"Non-WAV accepted");
    for(double rate:{44100.,48000.,96000.}){
        const auto file=folder.getChildFile(juce::String(int(rate))+".wav");
        juce::AudioBuffer<float> pcm(1,int(rate*.2));for(int i=0;i<pcm.getNumSamples();++i)pcm.setSample(0,i,.2f*float(std::sin(juce::MathConstants<double>::twoPi*1000*i/rate)));
        juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),rate,1,24,{},0));if(writer)stream.release();require(writer!=nullptr,"WAV writer failed");require(writer->writeFromAudioSampleBuffer(pcm,0,pcm.getNumSamples()),"WAV write failed");writer.reset();
        auto task=std::async(std::launch::async,[&disk,file]{juce::AudioFormatManager formats;formats.registerFormat(new juce::WavAudioFormat(),true);auto source=std::make_unique<hem::StemPlayback>();source->prepareFile(file,formats,disk,96000,256);return source;});
        auto source=task.get();require(source->channels==1,"Mono metadata lost");require(std::abs(source->sourceRate-rate)<1,"Source sample rate incorrect");require(std::abs(source->transport.getLengthInSeconds()-.2)<1.e-4,"Resampled duration incorrect");
        source->transport.start();juce::AudioBuffer<float> block(2,256);int crossings=0;float previous=0;double energy=0;
        for(int pos=0;pos<16384;pos+=256){juce::AudioSourceChannelInfo info(&block,0,256);source->transport.getNextAudioBlock(info);
            for(int i=0;i<256;++i)if(pos+i>=4096){const float sample=block.getSample(0,i);if(previous<=0&&sample>0)++crossings;previous=sample;energy+=sample*sample;}}
        require(crossings>=126&&crossings<=130,"Resampling changed pitch");require(energy>100,"Resampled audio silent");source.reset();
    }
    auto bad=folder.getChildFile("broken.wav");bad.replaceWithText("invalid wav");bool rejected=false;
    try{juce::AudioFormatManager f;f.registerFormat(new juce::WavAudioFormat(),true);hem::StemPlayback source;source.prepareFile(bad,f,disk,96000,256);}catch(const std::exception&){rejected=true;}require(rejected,"Corrupt WAV accepted");
    zone.shutdown();disk.stopThread(5000);folder.deleteRecursively();std::cout<<"PASS batch limit, extension validation, corrupt WAV, background readers and 44.1/48/96 -> 96 kHz resampling\n";return 0;
 }catch(const std::exception& e){disk.stopThread(5000);folder.deleteRecursively();std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/MasterLimiterTests.cpp

```cpp
#include "../Source/DSP/MasterLimiter.h"
#include <iostream>
#include <random>
#include <cstdlib>
#include <new>
static bool inProcess=false;static int allocations=0;
void* operator new(std::size_t n){if(inProcess)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main(){try{
 hem::Reconstruction reconstruction;
 for(double rate:{44100.,48000.,96000.}){
    hem::MasterLimiter limiter;limiter.prepare(rate);
    const int n=24000;std::vector<float> left(n),right(n);std::mt19937 generator(71);std::uniform_real_distribution<float> random(-1,1);
    for(int i=0;i<n/2;++i){
        // Thirteen coincident full-level stems followed by a +12 dB monitor
        // boost, including independent stereo noise and high-frequency peaks.
        left[size_t(i)]=13*hem::gain(12)*random(generator);
        right[size_t(i)]=13*hem::gain(12)*float(std::sin(2*hem::pi*.477*i));
    }
    left[100]=std::numeric_limits<float>::quiet_NaN();right[500]=std::numeric_limits<float>::infinity();
    for(int start=0;start<n;start+=137){inProcess=true;limiter.process(left.data()+start,right.data()+start,std::min(137,n-start));inProcess=false;}
    require(limiter.invalidSampleCount()==2,"Non-finite input not contained");
    double maximum=0;
    for(int i=0;i<n;++i){require(std::isfinite(left[size_t(i)])&&std::isfinite(right[size_t(i)]),"Non-finite output");
        maximum=std::max({maximum,double(std::abs(left[size_t(i)])),double(std::abs(right[size_t(i)]))});
        for(const auto& h:reconstruction.h){double l=0,r=0;for(int k=0;k<32;++k)if(i>=k){l+=h[size_t(k)]*left[size_t(i-k)];r+=h[size_t(k)]*right[size_t(i-k)];}maximum=std::max({maximum,std::abs(l),std::abs(r)});}
    }
    require(maximum<=hem::gain(-1)+1.e-6,"Master ceiling exceeded");
    std::cout<<rate<<" Hz: 13-stem +12 dB overload <= "<<hem::decibels(float(maximum))<<" dBTP\n";
    limiter.prepare(rate);std::fill(left.begin(),left.end(),0);std::fill(right.begin(),right.end(),0);left[0]=right[0]=.05f;
    limiter.process(left.data(),right.data(),n);require(std::abs(left[size_t(limiter.latencySamples())]-.05f)<1.e-6,"Quiet impulse/latency failure");
 }
 require(allocations==0,"Audio callback allocated memory");std::cout<<"PASS: linked final master, 8x ceiling, non-finite containment, latency, zero process allocations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/MonitorProfileTests.cpp

```cpp
#include "../Source/Systems/MonitorProfile.h"
#include <iostream>
#include <stdexcept>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
 hem::MonitorProfile p;p.name="Numerical test curve";p.device="Synthetic test signal";p.connection="Test";p.deviceMode="Test";p.measurementSource="Generated verification case, not a headphone measurement";p.preampDb=-3;
 p.bands={{hem::MonitorProfile::Filter::Bell,1000,.7071f,-6},{hem::MonitorProfile::Filter::LowShelf,100,.7071f,3},{hem::MonitorProfile::Filter::HighShelf,10000,.7071f,-2}};
 auto f=juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("hem-profile",".json");hem::MonitorProfile::save(p,f);auto restored=hem::MonitorProfile::load(f);auto curve=restored.prepare(96000);
 require(curve.count==3&&std::abs(curve.preamp-hem::gain(-3))<1.e-6,"Profile round trip");
 require(std::abs(hem::decibels(curve.filters[0].magnitude(1000,96000))+6)<.01,"Bell coefficients");
 require(std::abs(hem::decibels(curve.filters[1].magnitude(1,96000))-3)<.01,"Low shelf coefficients");
 require(std::abs(hem::decibels(curve.filters[2].magnitude(47000,96000))+2)<.01,"High shelf coefficients");
 restored.bands[2].q=.4f;auto wide=restored.prepare(96000);restored.bands[2].q=1.5f;auto sharp=restored.prepare(96000);
 require(std::abs(wide.filters[2].magnitude(7000,96000)-sharp.filters[2].magnitude(7000,96000))>.01,"Shelf Q ignored");
 bool rejected=false;try{restored.prepare(8000);}catch(...){rejected=true;}require(rejected,"Above-Nyquist calibration accepted");
 f.replaceWithText("{bad json}");rejected=false;try{hem::MonitorProfile::load(f);}catch(...){rejected=true;}require(rejected,"Malformed profile accepted");f.deleteFile();
 std::cout<<"PASS: measured-profile schema round trip, coefficient responses, invalid-rate/file rejection\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/MonitoringRouterTests.cpp

```cpp
#include "../Source/DSP/MonitoringRouter.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* t){if(!b)throw std::runtime_error(t);}
static double rms(const std::vector<float>& x){double s=0;for(size_t i=x.size()/2;i<x.size();++i)s+=double(x[i])*x[i];return std::sqrt(s/(x.size()-x.size()/2));}
int main(){try{
 for(double sr:{44100.,48000.,96000.}){
    hem::MonitoringRouter router;router.prepare(sr);hem::MonitoringRouter::Curve curve;curve.count=1;curve.preamp=hem::gain(-3);curve.filters[0].configure(hem::Biquad::Bell,sr,1000,.7071f,-6);
    require(router.installPrepared(curve),"Install failed");require(!router.installPrepared(curve),"Overlapping transition accepted");
    const int n=int(sr*.3);std::vector<float> l(n),r(n),a(n),b(n);for(int i=0;i<n;++i)l[size_t(i)]=r[size_t(i)]=.25f*float(std::sin(2*hem::pi*1000*i/sr));const auto original=l;
    router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);require(a==l&&b==r,"Direct monitoring altered master");
    router.setHeadphoneMode(true);router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);
    const auto db=20*std::log10(rms(a)/rms(l));require(std::abs(db+9)<.02,"Calibration gain incorrect");require(l==original,"Calibration modified master source");
    router.setHeadphoneMode(false);router.renderCalibration(l.data(),r.data(),a.data(),b.data(),n);router.mixRoute(l.data(),r.data(),a.data(),b.data(),n);require(a.back()==l.back(),"Direct return did not reach unity");
    router.prepare(sr);require(!router.headphoneMode(),"Device restart retained correction");std::cout<<sr<<" Hz: direct exact / calibration "<<db<<" dB / source unchanged\n";
 }
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/NativeHostLifecycleTests.cpp

```cpp
#include "../Source/MainComponent.h"
#include <iostream>
#include <stdexcept>
#if JUCE_LINUX
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#endif

struct MainComponentTestAccess {
 static void offline(MainComponent& host){host.deviceManager.closeAudioDevice();if(host.prepared)host.releaseResources();host.timerCallback();}
 static bool ready(MainComponent& host){return host.prepared&&host.sampleRate==48000&&host.blockSize==256&&!host.outputMeters.snapshot().ready;}
 static hem::TrackBank& bank(MainComponent& host){return *host.bank;}
 static void import(MainComponent& host,const juce::StringArray& files){host.importer->submitFiles(files);}
 static bool pending(MainComponent& host){return host.importer->hasPendingWork();}
 static bool sessionBusy(MainComponent& host){return host.sessionWorker->isBusy();}
 static bool exportBusy(MainComponent& host){return host.exportWorker->isBusy();}
 static juce::String exportResult(MainComponent& host){return host.exportWorker->result();}
 static void install(MainComponent& host,int module){host.installSelected(module);}
 static void tick(MainComponent& host){host.timerCallback();}
 static void open(MainComponent& host){host.openPlugin(0);}
 static bool hasEditor(MainComponent& host){return host.editedProcessor!=nullptr;}
 static hem::SessionSnapshot snapshot(MainComponent& host){return hem::SessionState::capture(*host.bank,host.inputDb,host.outputDb,host.sessionPosition(),host.looping,host.monitorProcessor);}
 static juce::Result save(MainComponent& host,const juce::File& file){return host.saveSessionFile(file);}
 static juce::Result load(MainComponent& host,const juce::File& file){return host.loadSessionFile(file);}
 static juce::Result clear(MainComponent& host,bool all){return host.clearTrackSources(all);}
 static juce::Result reset(MainComponent& host){return host.resetSessionConfirmed();}
 static juce::Result render(MainComponent& host,const juce::File& file){hem::OfflineMixOptions o;o.tailSeconds=0;return host.startExport(file,o);}
 static void play(MainComponent& host){host.playAll();}
 static bool playing(MainComponent& host){return host.playing();}
 static unsigned generation(MainComponent& host){return host.generation;}
 static juce::Result staleImport(MainComponent& host,hem::StemFileImporter::Batch& batch){return host.commitImport(batch);}
 static juce::TimeSliceThread& disk(MainComponent& host){return host.diskThread;}
 static void closeChoice(MainComponent& host,MainComponent::CloseChoice choice){host.closePrompt=[choice](bool,bool,std::function<void(MainComponent::CloseChoice)> resolved){resolved(choice);};}
 static bool closePending(MainComponent& host){return host.closePhase!=MainComponent::ClosePhase::idle;}
 static void saveTarget(MainComponent& host,const juce::File& file){host.currentSessionFile=file;}
 static juce::String status(MainComponent& host){return host.message.getText();}
 static juce::Result slowExport(MainComponent& host,const juce::File& file){hem::OfflineMixOptions options;options.blockSize=1;options.tailSeconds=30;return host.startExport(file,options);}
};
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
juce::TextButton* button(juce::Component& parent,const juce::String& text){
 if(auto* b=dynamic_cast<juce::TextButton*>(&parent);b&&b->getButtonText()==text)return b;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* found=button(*parent.getChildComponent(i),text))return found;
 return nullptr;
}
template<class Condition>void until(Condition done,const char* failure){
 for(int i=0;i<3000&&!done();++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
 require(done(),failure);
}
void fixture(const juce::File& file,int frames){
 std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();juce::WavAudioFormat format;
 auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
 require(writer!=nullptr,"Fixture writer failed");juce::AudioBuffer<float> data(2,frames);
 for(int i=0;i<frames;++i){data.setSample(0,i,.005f*std::sin(float(i)*.13f));data.setSample(1,i,.003f*std::cos(float(i)*.11f));}
 require(writer->writeFromAudioSampleBuffer(data,0,frames),"Fixture write failed");
}
void applyImport(MainComponent& host,const juce::StringArray& files){
 MainComponentTestAccess::import(host,files);auto* apply=button(host,"APPLY WHEN STOPPED");require(apply!=nullptr,"Real importer Apply control missing");
 until([&]{return apply->isEnabled();},"Importer never became applicable without hardware");apply->onClick();
 until([&]{return !MainComponentTestAccess::pending(host);},"Import cleanup failed");
}
}
int main(){
#if JUCE_LINUX
 std::signal(SIGSEGV,[](int signal){void* frames[64];int count=backtrace(frames,64);backtrace_symbols_fd(frames,count,2);_exit(128+signal);});
#endif
juce::ScopedJuceInitialiser_GUI gui;try{
 const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-native-lifecycle-"+juce::Uuid().toString());require(directory.createDirectory().wasOk(),"Fixture directory failed");
 struct Cleanup{juce::File directory;~Cleanup(){directory.deleteRecursively();}} cleanup{directory};
 const auto source=directory.getChildFile("source.wav");fixture(source,4800);juce::MemoryBlock originalBytes;require(source.loadFileAsData(originalBytes),"Cannot retain original fixture bytes");
 std::cerr<<"stage: construct native host\n";auto owner=std::make_unique<MainComponent>();auto& host=*owner;std::cerr<<"stage: force offline\n";MainComponentTestAccess::offline(host);require(MainComponentTestAccess::ready(host),"No-device graph fallback did not prepare 48k/256 or faked output meter readiness");
 juce::StringArray thirteen;for(int i=0;i<13;++i)thirteen.add(source.getFullPathName());std::cerr<<"stage: import 13\n";applyImport(host,thirteen);
 for(const auto& track:MainComponentTestAccess::bank(host).tracks)require(track.loaded,"13-track offline import did not commit");
 MainComponentTestAccess::play(host);require(!MainComponentTestAccess::playing(host),"No-device PLAY left transport running without callbacks");
 std::cerr<<"stage: edit inserts\n";MainComponentTestAccess::install(host,0);auto snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="sipa","Offline insert did not become capturable immediately");
 std::cerr<<"stage: opening editor\n";MainComponentTestAccess::open(host);require(MainComponentTestAccess::hasEditor(host),"Real plugin editor failed to open offline");
 std::cerr<<"stage: replacing editor\n";MainComponentTestAccess::install(host,2);require(!MainComponentTestAccess::hasEditor(host),"Retired processor editor was not closed");
 snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="vca","Offline replacement did not settle without audio");
 MainComponentTestAccess::install(host,-1);snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId.isEmpty(),"Offline removal did not settle");
 MainComponentTestAccess::install(host,0);
 auto& track=MainComponentTestAccess::bank(host).tracks[0];auto pending=track.chain.stageNode(std::make_unique<hem::VcaProcessor>(),48000,256,track.chain.slotLatency());require(track.chain.publish(0,std::move(pending)),"Could not stage simulated pre-disconnect transition");
 track.transport().start();MainComponentTestAccess::offline(host);require(!MainComponentTestAccess::playing(host),"Device loss left source playing");
 snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].slots[0].pluginId=="vca","Pending insert failed to settle on device loss");
 std::cerr<<"stage: save recall\n";const auto session=directory.getChildFile("session.xml");require(MainComponentTestAccess::save(host,session).wasOk(),"Offline save admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline save did not complete");require(session.existsAsFile(),"Offline session file missing");
 require(MainComponentTestAccess::reset(host).wasOk(),"New session reset failed");for(const auto& t:MainComponentTestAccess::bank(host).tracks)require(!t.loaded,"New session retained audio");
 require(MainComponentTestAccess::load(host,session).wasOk(),"Offline recall admission failed");until([&]{return !MainComponentTestAccess::sessionBusy(host);},"Offline recall did not complete");snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[12].source==source&&snap.tracks[0].slots[0].pluginId=="vca","Offline recall lost audio or insert state");
 std::cerr<<"stage: export\n";const auto output=directory.getChildFile("mix.wav");require(MainComponentTestAccess::render(host,output).wasOk(),"Actual host export admission failed");until([&]{return !MainComponentTestAccess::exportBusy(host);},"Host export did not finish");require(output.existsAsFile()&&MainComponentTestAccess::exportResult(host).startsWith("Saved"),"Host did not publish WAV");
 juce::WavAudioFormat format;auto input=output.createInputStream();std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(),true));require(reader&&reader->lengthInSamples==4800&&reader->sampleRate==48000&&reader->usesFloatingPointData,"Host export format/duration incorrect");
 std::cerr<<"stage: clear/reimport\n";require(MainComponentTestAccess::clear(host,true).wasOk(),"All-source unload failed");snap=MainComponentTestAccess::snapshot(host);require(snap.tracks[0].source==juce::File()&&snap.tracks[0].slots[0].pluginId=="vca","Source unload failed to retain inserts");
 juce::StringArray three;for(int i=0;i<3;++i)three.add(source.getFullPathName());applyImport(host,three);for(int i=0;i<13;++i)require(MainComponentTestAccess::bank(host).tracks[size_t(i)].loaded==(i<3),"13 to 3 session replacement retained stale source audio after explicit unload");
 hem::StemFileImporter::Batch stale;stale.firstTrack=0;stale.count=1;stale.context={48000,256,MainComponentTestAccess::generation(host),false};juce::AudioFormatManager formats;formats.registerBasicFormats();stale.stems[0]=std::make_unique<hem::StemPlayback>();stale.stems[0]->prepareFile(source,formats,MainComponentTestAccess::disk(host),48000,256,false);
 MainComponentTestAccess::offline(host);require(MainComponentTestAccess::staleImport(host,stale).failed(),"Stale device-generation import was accepted");
 std::cerr<<"stage: close safeguard\n";
 int closes=0;require(host.hasUnsavedSessionChanges(),"Source edit did not mark session dirty");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::cancel);host.requestClose([&]{++closes;});
 require(closes==0&&!MainComponentTestAccess::closePending(host)&&host.isEnabled(),"Cancel close disabled or destroyed the session");
 require(MainComponentTestAccess::bank(host).tracks[0].loaded,"Cancel close unloaded source data");
 MainComponentTestAccess::saveTarget(host,directory.getChildFile("missing-parent/session.xml"));MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::save);host.requestClose([&]{++closes;});
 until([&]{return !MainComponentTestAccess::closePending(host);},"Failed save left close request stuck");
 require(closes==0&&host.isEnabled()&&host.hasUnsavedSessionChanges()&&MainComponentTestAccess::status(host).contains("Save failed"),"Failed close-save silently lost edits or closed the app");
 const auto closeSave=directory.getChildFile("close-save.xml");MainComponentTestAccess::saveTarget(host,closeSave);host.requestClose([&]{++closes;});
 until([&]{return closes==1;},"Successful save did not complete deferred close");
 require(closeSave.existsAsFile()&&!host.hasUnsavedSessionChanges(),"Close approval occurred before a successful save");
 MainComponentTestAccess::bank(host).tracks[0].trimDb=-1;
 const auto cancelledExport=directory.getChildFile("close-cancelled-export.wav");require(MainComponentTestAccess::slowExport(host,cancelledExport).wasOk(),"Active close-export test could not start");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::cancel);host.requestClose([&]{++closes;});
 require(closes==1&&MainComponentTestAccess::exportBusy(host),"Cancelling close cancelled the active export or closed Studio");
 MainComponentTestAccess::closeChoice(host,MainComponent::CloseChoice::discard);host.requestClose([&]{++closes;});
 until([&]{return closes==2;},"Active export cancellation deadlocked close");
 require(!MainComponentTestAccess::exportBusy(host)&&!cancelledExport.exists()&&MainComponentTestAccess::exportResult(host).containsIgnoreCase("cancel"),"Approved close did not safely cancel unpublished export");
 juce::MemoryBlock finalBytes;require(source.loadFileAsData(finalBytes)&&finalBytes==originalBytes,"Session/clear/export altered source WAV bytes");
 std::cout<<"PASS real no-device MainComponent lifecycle: 13-track async import/apply, immediate insert/replace/remove, editor retirement, disconnect settlement, save/recall, float32 export, source unload/new session, stale-generation rejection, cancel-close, save failure, save-before-close and active-export cancellation\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

```

## Tests/NativeWorkspaceTests.cpp

```cpp
#include "../Source/UI/NativeWorkspace.h"
#include <iostream>
#include <stdexcept>
#include <limits>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* child:root.getChildren())if(auto* result=find(*child,id))return result;return nullptr;}
template<class T>T& control(juce::Component& root,const juce::String& id){auto* c=dynamic_cast<T*>(find(root,id));require(c!=nullptr,("Missing production control: "+id).toRawUTF8());return *c;}
void click(juce::Component& root,const juce::String& id){auto& b=control<juce::TextButton>(root,id);require(b.isEnabled(),"Production button was unexpectedly disabled");require(bool(b.onClick),"Button has no production callback");b.onClick();}
int differences(const juce::Image& a,const juce::Image& b){int count=0;for(int y=0;y<a.getHeight();++y)for(int x=0;x<a.getWidth();++x)if(a.getPixelAt(x,y)!=b.getPixelAt(x,y))++count;return count;}
int readablePixels(const juce::Image& image){int n=0;for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x){auto c=image.getPixelAt(x,y);if((c.getGreen()>130&&c.getBlue()>140)||(c.getRed()>180&&c.getGreen()>100))++n;}return n;}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    const juce::File snapshots=argc>1?juce::File(argv[1]):juce::File{};
    auto saveRaster=[&](const juce::Image& image,const juce::String& name){if(snapshots==juce::File{})return;require(snapshots.createDirectory().wasOk(),"Cannot create workspace snapshot directory");juce::MemoryOutputStream png;require(juce::PNGImageFormat().writeImageToStream(image,png),"Cannot encode workspace raster");require(snapshots.getChildFile(name+".png").replaceWithData(png.getData(),png.getDataSize()),"Cannot save workspace raster");};
    // No AudioDeviceManager or hardware. Real engine, registry and UI components.
    auto bank=std::make_unique<hem::TrackBank>();
    juce::AudioBuffer<float> sourceAudio(2,96000);sourceAudio.clear();
    for(int ch=0;ch<2;++ch)for(int i=0;i<sourceAudio.getNumSamples();++i)sourceAudio.setSample(ch,i,.0316228f);
    juce::MemoryAudioSource source(sourceAudio,true,true);
    for(int i=0;i<13;++i){auto& track=bank->tracks[size_t(i)];track.name="STEM "+juce::String(i+1);track.prepare(96000,128);}
    auto& first=bank->tracks[0];first.transport().setSource(&source,0,nullptr,96000,2);first.loaded=true;first.transport().start();
    int selectedTrack=0,selectedSlot=0,installed=-1,opened=-1,monitorCalls=0;
    hem::MonitorProcessor monitor;monitor.prepareToPlay(96000,128);
    hem::NativeWorkspace* target=nullptr;
    hem::NativeWorkspace::Callbacks callbacks;
    callbacks.bank=[&]() -> hem::TrackBank& {return *bank;};
    callbacks.getSelectedTrack=[&]{return selectedTrack;};callbacks.getSelectedSlot=[&]{return selectedSlot;};
    callbacks.setSelectedTrack=[&](int i){if(target)target->clearEditor();selectedTrack=i;};
    callbacks.setSelectedSlot=[&](int i){if(target)target->clearEditor();selectedSlot=i;};
    callbacks.onOpenSlot=[&](int slot){opened=slot;if(auto* p=bank->tracks[size_t(selectedTrack)].chain.activeProcessor(slot))target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor()));};
    callbacks.onInstallModule=[&](int index){installed=index;auto& chain=bank->tracks[size_t(selectedTrack)].chain;require(chain.activeProcessor(selectedSlot)==nullptr,"Test attempted to overwrite an occupied slot");auto node=chain.stageNode(hem::PluginRegistry::entries()[size_t(index)].create(),96000,128,chain.slotLatency());chain.installInitial(selectedSlot,std::move(node),false);};
    callbacks.onMonitor=[&]{++monitorCalls;target->setEditor(std::unique_ptr<juce::AudioProcessorEditor>(monitor.createEditor()));};
    callbacks.routeTelemetry=[] {hem::NativeWorkspace::RouteTelemetry t;t.sampleRate=96000;t.blockSize=128;t.hardwareOutputs=2;t.limiterLatencySamples=384;return t;};
    hem::NativeWorkspace workspace(std::move(callbacks));target=&workspace;workspace.setSize(1255,489);
    require(workspace.page()==hem::NativeWorkspace::Page::plugin,"Initial workspace page is incorrect");
    for(int i=0;i<8;++i)require(find(workspace,"plugin-slot-"+juce::String(i))!=nullptr,"One of eight serial slots is absent");
    require(workspace.filteredRegistryIndices().size()==12,"Vault did not use all 12 real registry entries");
    workspace.setVaultFilter("compressor");require(workspace.filteredRegistryIndices()==std::vector<int>{2},"Text search did not find real VCA registry entry");
    workspace.setVaultFilter("","Dynamics");require(workspace.filteredRegistryIndices()==std::vector<int>({2,4,5}),"Category filter does not match registry categories");
    workspace.setVaultFilter("tape","Dynamics");require(workspace.filteredRegistryIndices().empty(),"Search/category conjunction is incorrect");
    workspace.setVaultFilter("TaPe");require(workspace.filteredRegistryIndices()==std::vector<int>{10},"Search should ignore case");
    workspace.setVaultFilter("","All categories");
    control<juce::TextEditor>(workspace,"vault-search").setText("surgical",false);control<juce::TextEditor>(workspace,"vault-search").onTextChange();
    require(workspace.filteredRegistryIndices()==std::vector<int>{1},"Production text editor was not connected to filter");workspace.setVaultFilter("");
    workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-0");
    require(installed==0&&bank->tracks[0].chain.activeProcessor(0)!=nullptr,"Vault install did not reach the actual registry/track chain");
    workspace.setPage(hem::NativeWorkspace::Page::plugin);click(workspace,"plugin-slot-0");
    require(opened==0&&workspace.hasEditor(),"Real slot did not open its native editor");
    auto& viewport=control<juce::Viewport>(workspace,"plugin-editor-viewport");
    auto* canvas=viewport.getViewedComponent();require(canvas!=nullptr,"Editor viewport has no canvas");
    juce::AudioProcessorEditor* embedded=nullptr;for(auto* c:canvas->getChildren())if(auto* e=dynamic_cast<juce::AudioProcessorEditor*>(c))embedded=e;
    require(embedded!=nullptr&&embedded->getParentComponent()==canvas,"Native editor was not embedded in this window");
    const int nativeWidth=embedded->getWidth(),nativeHeight=embedded->getHeight();
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-native-editor");
    juce::Component::SafePointer<juce::AudioProcessorEditor> editorObserver(embedded);
    require(canvas->getWidth()>=nativeWidth&&canvas->getHeight()>=nativeHeight,"Original editor dimensions were cropped without scrolling");
    workspace.setPluginExpanded(true);require(workspace.pluginExpanded()&&!control<juce::Viewport>(workspace,"plugin-stem-viewport").isVisible(),"Plugin expansion did not hide stem pane");
    require(embedded->getWidth()==nativeWidth&&embedded->getHeight()==nativeHeight,"Expanding changed original native editor dimensions");
    saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"plugin-expanded");
    click(workspace,"plugin-expand");require(!workspace.pluginExpanded(),"Production restore-split control was not wired");
    workspace.setSplitProportion(.52f);const int largeLeft=control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth();
    workspace.setSplitProportion(.24f);require(control<juce::Viewport>(workspace,"plugin-stem-viewport").getWidth()<largeLeft,"Split proportion did not adjust real geometry");
    workspace.setSplitProportion(2);require(workspace.splitProportion()==.60f,"Split upper bound not enforced");workspace.setSplitProportion(std::numeric_limits<float>::quiet_NaN());require(workspace.splitProportion()==.60f,"Non-finite split corrupted geometry");workspace.setSplitProportion(.32f);
    click(workspace,"plugin-bypass-0");require(bank->tracks[0].chain.isBypassed(0)&&control<juce::TextButton>(workspace,"plugin-bypass-0").getToggleState(),"Bypass did not update real insert and illumination");
    click(workspace,"plugin-bypass-0");require(!bank->tracks[0].chain.isBypassed(0),"Repeated bypass failed");
    const float clippedSample=1.1f;first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(first.signalMeters.snapshot().left.clipped,"Actual meter failed to latch a clipped test sample");
    require(control<juce::Component>(workspace,"plugin-signal-meter").keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),"Plugin meter has no keyboard reset");require(first.signalMeters.snapshot().left.clipped,"Clip reset was not deferred to audio publication");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped,"Plugin meter reset did not reach actual clip detector");
    workspace.setPage(hem::NativeWorkspace::Page::mixer);
    first.signalMeters.push(&clippedSample,&clippedSample,1);workspace.refresh();require(control<juce::Component>(workspace,"mixer-signal-meter-0").keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),"Mixer meter has no keyboard reset");first.signalMeters.push(nullptr,nullptr,0);require(!first.signalMeters.snapshot().left.clipped&&!first.signalMeters.snapshot().right.clipped,"Mixer meter reset did not clear both actual clip latches");
    for(int i=0;i<13;++i){auto& gain=control<juce::Slider>(workspace,"mixer-gain-"+juce::String(i));require(gain.getMinimum()==-60&&gain.getMaximum()==6,"Mixer gain range is not -60..+6 dB");require(!control<juce::TextButton>(workspace,"mixer-mute-"+juce::String(i)).getToggleState(),"Mute lit without engine state");require(!control<juce::TextButton>(workspace,"mixer-solo-"+juce::String(i)).getToggleState(),"Solo lit without engine state");}
    auto& gain=control<juce::Slider>(workspace,"mixer-gain-3");gain.setValue(-11.2,juce::sendNotificationSync);require(std::abs(bank->tracks[3].trimDb.load()+11.2f)<.001f,"Mixer fader did not change real atomic gain");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");require(bank->tracks[3].muted.load()&&bank->tracks[3].solo.load(),"Mute/solo not connected to real track state");
    click(workspace,"mixer-mute-3");click(workspace,"mixer-solo-3");
    click(workspace,"mixer-select-12");require(selectedTrack==12&&workspace.page()==hem::NativeWorkspace::Page::plugin,"Mixer edit did not select thirteenth track");require(editorObserver==nullptr&&!workspace.hasEditor(),"Host selection did not destroy borrowed editor");
    // Navigation alone must preserve running transports, insert and gain state.
    auto render=[&]{juce::AudioBuffer<float> sum(2,128);sum.clear();std::array<float,128> gains;gains.fill(1);for(auto& track:bank->tracks)track.render(sum,128,false,gains.data());};
    for(int i=0;i<60;++i)render();workspace.setPage(hem::NativeWorkspace::Page::mixer);workspace.refresh();
    const double position=first.transport().getCurrentPosition();const bool playing=first.transport().isPlaying();
    for(auto page:{hem::NativeWorkspace::Page::routing,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer})workspace.setPage(page);
    require(first.transport().isPlaying()==playing&&playing,"Workspace navigation interrupted playback");require(first.transport().getCurrentPosition()==position,"Workspace navigation performed a seek");require(bank->tracks[0].chain.activeProcessor(0)!=nullptr&&bank->tracks[3].trimDb.load()==float(gain.getValue()),"Navigation changed audio state");
    render();require(first.transport().getCurrentPosition()>position,"Transport no longer advanced after navigation");
    workspace.refresh();auto withSignal=workspace.createComponentSnapshot(workspace.getLocalBounds());
    first.signalMeters.prepare(0);workspace.refresh();auto noMeter=workspace.createComponentSnapshot(workspace.getLocalBounds());require(differences(withSignal,noMeter)>25,"Mixer did not paint actual atomic signal telemetry");
    // Real production raster at multiple window sizes: every strip reachable,
    // readable geometry retained; narrow layouts use viewport scrolling.
    for(auto size:{juce::Point<int>(1255,489),juce::Point<int>(1046,395),juce::Point<int>(960,420),juce::Point<int>(1882,733)}){
        workspace.setSize(size.x,size.y);workspace.setPage(hem::NativeWorkspace::Page::mixer);
        for(int i=0;i<13;++i){auto& strip=control<juce::Component>(workspace,"mixer-strip-"+juce::String(i));require(strip.getWidth()>=86&&strip.getHeight()>=270,"Mixer compressed a strip below readable minimum");require(strip.getBounds().getRight()<=strip.getParentComponent()->getWidth(),"Thirteenth strip fell outside scrollable content");}
        // The default 1280x720 shell exposes only 1046x395 of workspace.
        // Test the visible viewport (including horizontal scrollbar loss), not
        // just content bounds, so mute/solo/edit can never hide below it.
        auto& mixerViewport=control<juce::Viewport>(workspace,"mixer-viewport");
        mixerViewport.setViewPosition(0,0);const auto visibleArea=mixerViewport.getViewArea();
        require(!mixerViewport.getVerticalScrollBar().isVisible(),"Mixer requires vertical scrolling at a supported workspace size");
        for(int i=0;i<13;++i){
            for(const auto* prefix:{"mixer-gain-","mixer-readout-","mixer-mute-","mixer-solo-","mixer-select-"}){
                auto& item=control<juce::Component>(workspace,juce::String(prefix)+juce::String(i));
                const auto visibleBounds=mixerViewport.getViewedComponent()->getLocalArea(&item,item.getLocalBounds());
                require(visibleBounds.getY()>=visibleArea.getY()&&visibleBounds.getBottom()<=visibleArea.getBottom(),"Mixer control is vertically clipped by the actual viewport");
            }
        }
        if(size.x==1046&&size.y==395){
            require(mixerViewport.getHorizontalScrollBar().isVisible(),"Default shell should preserve strip width with horizontal scrolling");
            mixerViewport.setViewPosition(mixerViewport.getViewedComponent()->getWidth(),0);
            const auto finalArea=mixerViewport.getViewArea();auto& finalButton=control<juce::Component>(workspace,"mixer-select-12");
            require(finalArea.contains(mixerViewport.getViewedComponent()->getLocalArea(&finalButton,finalButton.getLocalBounds())),"The thirteenth mixer edit button is not fully reachable by horizontal scrolling");
            saveRaster(workspace.createComponentSnapshot(workspace.getLocalBounds()),"mixer-last-strip-1046x395");
            mixerViewport.setViewPosition(0,0);
        }
        for(auto page:{hem::NativeWorkspace::Page::plugin,hem::NativeWorkspace::Page::mixer,hem::NativeWorkspace::Page::vault,hem::NativeWorkspace::Page::routing}){workspace.setPage(page);auto image=workspace.createComponentSnapshot(workspace.getLocalBounds());require(image.isValid()&&image.getWidth()==size.x&&image.getHeight()==size.y,"Workspace raster geometry is invalid");require(readablePixels(image)>1000,"Workspace page lacks readable live UI pixels");saveRaster(image,"workspace-page-"+juce::String(int(page))+"-"+juce::String(size.x)+"x"+juce::String(size.y));}
    }
    workspace.setSize(1255,489);workspace.setPage(hem::NativeWorkspace::Page::vault);click(workspace,"vault-install-11");require(monitorCalls==1&&workspace.hasEditor()&&installed==0,"Monitoring-only registry entry incorrectly installed a stem insert");workspace.clearEditor();require(!workspace.hasEditor(),"Editor cleanup failed");
    // A replacement bank is resolved through the getter, never a stale reference.
    first.transport().setSource(nullptr);for(auto& track:bank->tracks)track.release();
    auto replacement=std::make_unique<hem::TrackBank>();replacement->tracks[12].name="RESTORED STEM";bank.swap(replacement);workspace.refresh();
    workspace.setPage(hem::NativeWorkspace::Page::plugin);require(control<juce::ComboBox>(workspace,"plugin-track-select").getText().contains("RESTORED STEM"),"Workspace retained a retired bank reference");
    std::cout<<"PASS production 13-track mixer controls, real metering, 12-entry search/categories, registry install/monitor routing, split/expand and native editor lifetime, multi-size rasters, bank replacement and uninterrupted navigation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/OfflineMixTests.cpp

```cpp
#include "../Source/Systems/OfflineMixRenderer.h"
#include "../Source/CoreMasterStage.h"
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

struct Fixtures {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hem-offline-mix-" + juce::Uuid().toString());
    Fixtures() { require(directory.createDirectory().wasOk(), "Cannot create fixture directory"); }
    ~Fixtures() { directory.deleteRecursively(); }
    juce::File file(const juce::String& name) const { return directory.getChildFile(name); }
};

void writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double rate,
              bool floating = true) {
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require(stream != nullptr, "Cannot create fixture WAV");
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream,
        juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(audio.getNumChannels())
            .withBitsPerSample(floating ? 32 : 24)
            .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral));
    require(writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Cannot write fixture WAV");
    require(writer->flush(), "Cannot finalize fixture WAV");
}

juce::AudioBuffer<float> readWav(const juce::File& file, double rate, juce::int64 frames) {
    auto stream = file.createInputStream();
    require(stream && stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("RIFF")),
            "Output is not RIFF");
    const auto riffSize = static_cast<juce::uint32>(stream->readInt());
    require(static_cast<juce::int64>(riffSize) + 8 == file.getSize(), "RIFF size field is incorrect");
    require(stream->readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("WAVE")), "Output is not WAVE");
    bool floatFormat = false, dataFound = false;
    while (stream->getPosition() + 8 <= file.getSize()) {
        const auto id = stream->readInt();
        const auto length = static_cast<juce::uint32>(stream->readInt());
        const auto dataStart = stream->getPosition();
        if (id == static_cast<int>(juce::ByteOrder::littleEndianInt("fmt "))) {
            const auto tag = static_cast<juce::uint16>(stream->readShort());
            require(stream->readShort() == 2, "WAV header is not stereo");
            require(stream->readInt() == static_cast<int>(rate), "WAV header rate mismatch");
            require(stream->readInt() == static_cast<int>(rate * 8), "WAV byte-rate mismatch");
            require(stream->readShort() == 8 && stream->readShort() == 32, "WAV alignment/bit depth mismatch");
            if (tag == 3) floatFormat = true;
            if (tag == 0xfffe && length >= 40) {
                stream->setPosition(dataStart + 24);
                floatFormat = stream->readInt() == 3;
            }
        }
        if (id == static_cast<int>(juce::ByteOrder::littleEndianInt("data"))) {
            dataFound = true;
            require(static_cast<juce::int64>(length) == frames * 8, "WAV data length mismatch");
        }
        require(stream->setPosition(dataStart + length + (length & 1)), "Invalid RIFF chunk");
    }
    require(floatFormat && dataFound, "Missing IEEE float format or data chunk");
    stream->setPosition(0);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    require(reader && reader->sampleRate == rate && reader->numChannels == 2
            && reader->bitsPerSample == 32 && reader->usesFloatingPointData
            && reader->lengthInSamples == frames, "Decoded WAV format/frame count mismatch");
    require(frames <= 1000000, "Test fixture is unexpectedly large");
    juce::AudioBuffer<float> result(2, static_cast<int>(frames));
    require(reader->read(&result, 0, static_cast<int>(frames), 0, true, true), "Cannot decode export");
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < result.getNumSamples(); ++i)
            require(std::isfinite(result.getSample(channel, i)), "Non-finite export sample");
    return result;
}

juce::AudioBuffer<float> constantAudio(int frames, float left, float right, int channels = 2) {
    juce::AudioBuffer<float> audio(channels, frames);
    juce::FloatVectorOperations::fill(audio.getWritePointer(0), left, frames);
    if (channels == 2) juce::FloatVectorOperations::fill(audio.getWritePointer(1), right, frames);
    return audio;
}

juce::AudioBuffer<float> exportAudio(const hem::SessionSnapshot& snapshot, const juce::File& file,
                                   const hem::OfflineMixOptions& options, juce::int64 frames) {
    double lastProgress = -1;
    int reports = 0;
    bool progressValid = true;
    const auto result = hem::OfflineMixRenderer::render(snapshot, file, options, {}, [&](double p) {
        progressValid = progressValid && std::isfinite(p) && p >= lastProgress && p <= 1;
        lastProgress = p;
        ++reports;
    });
    require(result.status.wasOk(), "Render failed: " + result.status.getErrorMessage());
    require(result.frames == frames, "Returned frame count mismatch");
    hem::MasterTruePeakLimiter baseline;
    baseline.prepare(options.sampleRate, options.blockSize, 2);
    require(result.latencyRemoved == static_cast<int>(std::ceil(options.sampleRate * .006)) * 8 + baseline.latency(),
            "Reported latency does not use actual baseline latency");
    if (options.sampleRate == 48000) require(result.latencyRemoved == 2557, "48 kHz latency unexpectedly changed");
    require(progressValid && reports > 1 && lastProgress == 1, "Progress regressed, escaped [0, 1], or did not complete");
    return readWav(file, options.sampleRate, frames);
}

void compare(const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected,
             float tolerance, const juce::String& label) {
    require(actual.getNumChannels() == expected.getNumChannels()
            && actual.getNumSamples() == expected.getNumSamples(), label + " dimensions mismatch");
    float maximum = 0;
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int i = 0; i < actual.getNumSamples(); ++i)
            maximum = std::max(maximum, std::abs(actual.getSample(channel, i) - expected.getSample(channel, i)));
    require(maximum <= tolerance, label + " maximum difference " + juce::String(maximum, 10));
}

// Independent reference: feed the unchanged baseline with the expected mix,
// offset by the host's fixed eight-slot delay, then trim its actual latency.
// No OfflineMixRenderer code or CoreMasterStage adapter is used here.
juce::AudioBuffer<float> baselineReference(const juce::AudioBuffer<float>& input,
                                         const hem::OfflineMixOptions& options, int outputFrames) {
    hem::MasterTruePeakLimiter baseline;
    baseline.prepare(options.sampleRate, options.blockSize, 2);
    const int chainLatency = static_cast<int>(std::ceil(options.sampleRate * .006)) * 8;
    const int latency = chainLatency + baseline.latency();
    juce::AudioBuffer<float> result(2, outputFrames), block(2, options.blockSize);
    result.clear();
    for (int position = 0; position < outputFrames + latency;) {
        const int count = std::min(options.blockSize, outputFrames + latency - position);
        block.clear();
        for (int i = 0; i < count; ++i) {
            const int source = position + i - chainLatency;
            if (source >= 0 && source < input.getNumSamples())
                for (int channel = 0; channel < 2; ++channel)
                    block.setSample(channel, i, input.getSample(channel, source));
        }
        baseline.process(block, count);
        for (int i = 0; i < count; ++i) {
            const int destination = position + i - latency;
            if (destination >= 0)
                for (int channel = 0; channel < 2; ++channel)
                    result.setSample(channel, destination, block.getSample(channel, i));
        }
        position += count;
    }
    return result;
}

void setParameter(hem::SlotSnapshot& slot, const juce::String& id, double value) {
    for (auto child : slot.parameters) {
        if (child["id"].toString() == id) { child.setProperty("value", value, nullptr); return; }
    }
    throw std::runtime_error("Test parameter not found");
}

void install(hem::SlotSnapshot& slot, const juce::String& plugin) {
    slot.pluginId = plugin;
    auto processor = hem::PluginRegistry::create(plugin);
    require(processor != nullptr, "Unknown test insert");
    slot.parameters = hem::SessionState::captureParameters(*processor);
}

void failure(const hem::SessionSnapshot& snapshot, const juce::File& file,
             const hem::OfflineMixOptions& options, const juce::String& label,
             const std::function<bool()>& cancelled = {}) {
    const int before = file.getParentDirectory().getNumberOfChildFiles(juce::File::findFilesAndDirectories);
    const auto result = hem::OfflineMixRenderer::render(snapshot, file, options, cancelled);
    require(result.status.failed(), label + " was accepted");
    require(result.frames == 0 && result.latencyRemoved == 0, label + " reports a committed export");
    require(!file.exists(), label + " left a final file");
    require(file.getParentDirectory().getNumberOfChildFiles(juce::File::findFilesAndDirectories) == before,
            label + " leaked a temporary file");
}

void checkBasicMix(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.blockSize = 257; // The latency boundary and final write are unaligned.
    options.tailSeconds = .0125;
    constexpr int frames = 4801, tail = 600;
    hem::SessionSnapshot snapshot;
    snapshot.inputDb = -3;
    snapshot.outputDb = -6;
    snapshot.positionSeconds = 918.5; // Explicitly ignored.
    snapshot.looping = true; // Explicitly ignored; tail must not loop sources.
    std::array<juce::AudioBuffer<float>, 13> tracks;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const int length = frames - static_cast<int>(i) * 37;
        tracks[i] = constantAudio(length, .004f * static_cast<float>(i + 1), -.002f * static_cast<float>(i + 1));
        snapshot.tracks[i].source = fixtures.file("stem-" + juce::String(static_cast<int>(i)) + ".wav");
        snapshot.tracks[i].gainDb = -static_cast<float>(i) * .5f;
        writeWav(snapshot.tracks[i].source, tracks[i], 48000);
    }
    const auto expected = [&](const hem::SessionSnapshot& s) {
        juce::AudioBuffer<float> sum(2, frames);
        sum.clear();
        const bool anySolo = std::any_of(s.tracks.begin(), s.tracks.end(), [](const auto& t) { return t.solo; });
        for (size_t t = 0; t < tracks.size(); ++t) {
            if (s.tracks[t].muted || (anySolo && !s.tracks[t].solo)) continue;
            auto audio = tracks[t];
            audio.applyGain(hem::gain(s.inputDb));
            for (int channel = 0; channel < 2; ++channel)
                sum.addFrom(channel, 0, audio, channel, 0, audio.getNumSamples(), hem::gain(s.tracks[t].gainDb));
        }
        sum.applyGain(hem::gain(s.outputDb));
        return baselineReference(sum, options, frames + tail);
    };
    const auto all = exportAudio(snapshot, fixtures.file("sum.wav"), options, frames + tail);
    compare(all, expected(snapshot), 1.e-7f, "13 stereo stems, unequal lengths, input/track/output gain");
    require(all.getSample(0, 2000) > 0 && all.getSample(1, 2000) < 0, "Stereo channels lost independence");
    require(all.getMagnitude(0, frames + 200, 400) < 1.e-7f, "Loop flag leaked audio into tail");
    snapshot.tracks[0].muted = true;
    compare(exportAudio(snapshot, fixtures.file("mute.wav"), options, frames + tail), expected(snapshot), 1.e-7f, "Mute");
    snapshot.tracks[2].solo = true;
    snapshot.tracks[7].solo = true;
    snapshot.tracks[7].muted = true;
    compare(exportAudio(snapshot, fixtures.file("solo.wav"), options, frames + tail), expected(snapshot), 1.e-7f, "Solo and mute precedence");
    for (auto& t : snapshot.tracks) t.muted = true;
    const auto silence = exportAudio(snapshot, fixtures.file("silence.wav"), options, frames + tail);
    require(silence.getMagnitude(0, frames + tail) == 0, "All-muted export is not silent");
    std::cout << "PASS: 13 stereo stems, exact longest-source/tail length, all gains, mute/solo, zero start, loop ignored\n";
}

void checkLatencyAndBaseline(Fixtures& fixtures) {
    for (double rate : {44100., 48000., 96000.}) {
        hem::OfflineMixOptions options;
        options.sampleRate = rate;
        options.blockSize = 127;
        options.tailSeconds = 128. / rate;
        constexpr int sourceFrames = 1003;
        const int outputFrames = sourceFrames + static_cast<int>(std::ceil(options.tailSeconds * rate));
        juce::AudioBuffer<float> source(2, sourceFrames);
        source.clear();
        source.setSample(0, 0, .3f);
        source.setSample(1, 0, -.15f);
        source.setSample(0, 499, 1.8f);
        source.setSample(1, 500, -.8f);
        source.setSample(0, sourceFrames - 1, .1f);
        source.setSample(1, sourceFrames - 1, -.2f);
        hem::SessionSnapshot snapshot;
        snapshot.tracks[0].source = fixtures.file("impulse-" + juce::String(rate, 0) + ".wav");
        writeWav(snapshot.tracks[0].source, source, rate);
        auto actual = exportAudio(snapshot, fixtures.file("impulse-mix-" + juce::String(rate, 0) + ".wav"), options, outputFrames);
        compare(actual, baselineReference(source, options, outputFrames), 0.f, "Exact byte-preserved master parity");
        require(std::abs(actual.getSample(0, 0)) > .1f, "Start impulse lost during latency trim");
        require(actual.getMagnitude(0, sourceFrames, outputFrames - sourceFrames) > 1.e-7f, "Final impulse tail was discarded");
        std::cout << "PASS: " << rate << " Hz exact baseline impulse/overload parity, actual latency alignment and final-sample tail\n";
    }
    // A single-frame source is shorter than both the chain and limiter delay.
    hem::SessionSnapshot shortSnapshot;
    shortSnapshot.tracks[0].source = fixtures.file("one-frame.wav");
    auto single = constantAudio(1, .2f, -.1f);
    writeWav(shortSnapshot.tracks[0].source, single, 48000);
    hem::OfflineMixOptions tiny;
    tiny.blockSize = 1;
    tiny.tailSeconds = 0;
    compare(exportAudio(shortSnapshot, fixtures.file("one-frame-mix.wav"), tiny, 1), baselineReference(single, tiny, 1), 0.f,
            "One-sample blocks, zero tail, source shorter than latency");
    auto isolatedImpulse = constantAudio(4096, 0, 0);
    isolatedImpulse.setSample(0, 2000, .1f);
    isolatedImpulse.setSample(1, 2000, -.2f);
    writeWav(fixtures.file("isolated-impulse.wav"), isolatedImpulse, 48000);
    shortSnapshot.tracks[0].source = fixtures.file("isolated-impulse.wav");
    tiny.blockSize = 113;
    const auto aligned = exportAudio(shortSnapshot, fixtures.file("isolated-impulse-mix.wav"), tiny, 4096);
    for (int channel = 0; channel < 2; ++channel) {
        int peak = 0;
        for (int i = 1; i < aligned.getNumSamples(); ++i)
            if (std::abs(aligned.getSample(channel, i)) > std::abs(aligned.getSample(channel, peak))) peak = i;
        require(peak == 2000, "Latency compensation did not place impulse on exact source frame 2000");
    }
    std::cout << "PASS: one-frame source, one-sample blocks, zero-tail exact output length\n";
}

void checkInsertState(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.tailSeconds = 0;
    constexpr int frames = 4096;
    hem::SessionSnapshot snapshot;
    snapshot.tracks[0].source = fixtures.file("insert-source.wav");
    writeWav(snapshot.tracks[0].source, constantAudio(frames, .1f, -.04f), 48000);
    const auto dry = exportAudio(snapshot, fixtures.file("insert-dry.wav"), options, frames);
    for (auto& slot : snapshot.tracks[0].slots) {
        install(slot, "sipa");
        setParameter(slot, "output", -1.0);
    }
    const auto wet = exportAudio(snapshot, fixtures.file("insert-wet.wav"), options, frames);
    const float expectedGain = hem::gain(-8.f);
    require(std::abs(wet.getSample(0, 3000) / dry.getSample(0, 3000) - expectedGain) < 1.e-5f,
            "Eight restored inserts did not process in order");
    snapshot.tracks[0].slots[0].bypassed = true;
    snapshot.tracks[0].slots[7].bypassed = true;
    const auto bypass = exportAudio(snapshot, fixtures.file("insert-bypass.wav"), options, frames);
    require(std::abs(bypass.getSample(0, 3000) / dry.getSample(0, 3000) - hem::gain(-6.f)) < 1.e-5f,
            "Saved bypass states were not installed immediately");
    for (auto& slot : snapshot.tracks[0].slots) slot.bypassed = true;
    compare(exportAudio(snapshot, fixtures.file("insert-all-bypass.wav"), options, frames), dry, 0.f,
            "All-bypass maintains fixed latency with no initial fade");
    // Each registered non-monitor processor stages through the actual chain.
    // Bypassing tests its compensated dry path without expensive output claims.
    for (size_t i = 0; i < 11; ++i) {
        auto& slot = snapshot.tracks[i + 1].slots[i % 8];
        install(slot, hem::PluginRegistry::entries()[i].id);
        slot.bypassed = true;
    }
    compare(exportAudio(snapshot, fixtures.file("registry-bypass.wav"), options, frames), dry, 0.f,
            "Registry stages every non-monitor processor");
    // Monitor settings are deliberately not validated, copied into processors,
    // or rendered. Even extreme/malformed monitor state cannot alter a mix.
    snapshot.monitorParameters = juce::ValueTree("IGNORED_MONITOR_CALIBRATION");
    snapshot.monitorParameters.setProperty("gain", std::numeric_limits<double>::infinity(), nullptr);
    compare(exportAudio(snapshot, fixtures.file("monitor-excluded.wav"), options, frames), dry, 0.f,
            "Monitor EQ/calibration exclusion");
    hem::SessionSnapshot ordered;
    ordered.tracks[0].source = snapshot.tracks[0].source;
    install(ordered.tracks[0].slots[0], "sipa");
    install(ordered.tracks[0].slots[7], "sipa");
    setParameter(ordered.tracks[0].slots[0], "invertL", 1);
    setParameter(ordered.tracks[0].slots[7], "swap", 1);
    const auto orderedAudio = exportAudio(ordered, fixtures.file("insert-order.wav"), options, frames);
    require(std::abs(orderedAudio.getSample(0, 3000) - dry.getSample(1, 3000)) < 1.e-6f
            && std::abs(orderedAudio.getSample(1, 3000) + dry.getSample(0, 3000)) < 1.e-6f,
            "Noncommuting invert/swap inserts were reordered");
    std::cout << "PASS: eight restored insert parameters, saved bypass, all eleven insert types staged, monitor-state isolation\n";
}

void checkRateConversion(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.blockSize = 113;
    options.tailSeconds = 0;
    constexpr int outputFrames = 9600;
    for (double rate : {24000., 44100., 96000.}) {
        const int sourceFrames = static_cast<int>(rate / 5);
        auto audio = constantAudio(sourceFrames, .08f, 0.f, 1);
        for (int i = 0; i < sourceFrames; ++i)
            audio.setSample(0, i, .08f * std::sin(static_cast<float>(2 * juce::MathConstants<double>::pi * 997 * i / rate)));
        hem::SessionSnapshot snapshot;
        snapshot.tracks[0].source = fixtures.file("rate-" + juce::String(rate, 0) + ".wav");
        writeWav(snapshot.tracks[0].source, audio, rate, false);
        const auto actual = exportAudio(snapshot, fixtures.file("converted-" + juce::String(rate, 0) + ".wav"), options, outputFrames);
        int crossings = 0;
        double square = 0;
        for (int i = 1001; i < 8500; ++i) {
            require(actual.getSample(0, i) == actual.getSample(1, i), "Mono was not duplicated to stereo");
            if (actual.getSample(0, i - 1) < 0 && actual.getSample(0, i) >= 0) ++crossings;
            square += actual.getSample(0, i) * actual.getSample(0, i);
        }
        require(crossings >= 155 && crossings <= 157, "Source rate was relabelled instead of resampled");
        require(std::sqrt(square / 7499.) > .05 && std::sqrt(square / 7499.) < .061,
                "Resampled tone level is invalid");
    }
    // Fractional duration rounds upward rather than dropping its last frame.
    hem::SessionSnapshot fractional;
    fractional.tracks[0].source = fixtures.file("fractional.wav");
    writeWav(fractional.tracks[0].source, constantAudio(101, .1f, -.02f), 44100);
    options.tailSeconds = .00001;
    const auto expected = static_cast<juce::int64>(std::ceil(101.L * 48000 / 44100)) + 1;
    exportAudio(fractional, fixtures.file("fractional-mix.wav"), options, expected);
    // Three different source rates are loaded at once. Duration follows the
    // longest source even when that track is muted or excluded by solo.
    hem::SessionSnapshot mixed;
    const std::array<double, 3> rates{24000., 44100., 96000.};
    const std::array<int, 3> lengths{101, 200, 400};
    for (size_t i = 0; i < rates.size(); ++i) {
        mixed.tracks[i].source = fixtures.file("mixed-rate-" + juce::String(static_cast<int>(i)) + ".wav");
        writeWav(mixed.tracks[i].source, constantAudio(lengths[i], .01f, -.02f), rates[i]);
    }
    mixed.tracks[0].solo = true;
    mixed.tracks[1].muted = true;
    exportAudio(mixed, fixtures.file("mixed-rates-mix.wav"), options,
                static_cast<juce::int64>(std::ceil(200.L * 48000 / 44100)) + 1);
    std::cout << "PASS: synchronous 24/44.1/96 kHz resampling to 48 kHz, PCM decoding, mono-to-stereo, fractional frame rounding\n";
}

void checkFailureAndTransaction(Fixtures& fixtures) {
    hem::OfflineMixOptions options;
    options.tailSeconds = .001;
    hem::SessionSnapshot snapshot;
    snapshot.tracks[0].source = fixtures.file("failure-source.wav");
    writeWav(snapshot.tracks[0].source, constantAudio(2000, .03f, -.01f), 48000);
    failure({}, fixtures.file("empty.wav"), options, "Empty session");
    auto bad = snapshot;
    bad.tracks[0].source = fixtures.file("does-not-exist.wav");
    failure(bad, fixtures.file("missing.wav"), options, "Missing source");
    const auto corrupt = fixtures.file("not-a-wav.wav");
    require(corrupt.replaceWithText("This is not a WAV"), "Cannot create corrupt fixture");
    bad.tracks[0].source = corrupt;
    failure(bad, fixtures.file("corrupt-result.wav"), options, "Corrupt source");
    bad.tracks[0].source = fixtures.file("empty-source.wav");
    writeWav(bad.tracks[0].source, constantAudio(0, 0, 0), 48000);
    failure(bad, fixtures.file("empty-source-result.wav"), options, "Zero-frame source");
    juce::AudioBuffer<float> multichannel(3, 32);
    multichannel.clear();
    bad.tracks[0].source = fixtures.file("three-channel-source.wav");
    writeWav(bad.tracks[0].source, multichannel, 48000);
    failure(bad, fixtures.file("multichannel-result.wav"), options, "More-than-stereo source");
    bad = snapshot;
    bad.inputDb = std::numeric_limits<float>::quiet_NaN();
    failure(bad, fixtures.file("nan-gain.wav"), options, "Non-finite gain");
    bad = snapshot;
    bad.outputDb = 7;
    failure(bad, fixtures.file("large-gain.wav"), options, "Out-of-range gain");
    bad = snapshot;
    bad.tracks[12].gainDb = -61;
    failure(bad, fixtures.file("empty-track-gain.wav"), options, "Invalid unloaded-track gain");
    for (double rate : {0., 7999., 48000.5, 384001., std::numeric_limits<double>::infinity()}) {
        auto invalid = options;
        invalid.sampleRate = rate;
        failure(snapshot, fixtures.file("invalid-rate.wav"), invalid, "Invalid sample rate");
    }
    for (int block : {0, -1, 8193}) {
        auto invalid = options;
        invalid.blockSize = block;
        failure(snapshot, fixtures.file("invalid-block.wav"), invalid, "Invalid block size");
    }
    for (double tail : {-1., 3601., std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = options;
        invalid.tailSeconds = tail;
        failure(snapshot, fixtures.file("invalid-tail.wav"), invalid, "Invalid tail");
    }
    bad = snapshot;
    bad.tracks[12].slots[7].pluginId = "unknown-plugin";
    bad.tracks[12].slots[7].bypassed = true;
    failure(bad, fixtures.file("unknown-plugin.wav"), options, "Unknown bypassed insert on empty track");
    install(bad.tracks[12].slots[7], "monitor-eq");
    failure(bad, fixtures.file("monitor-insert.wav"), options, "Prohibited monitor insert");
    bad = snapshot;
    install(bad.tracks[0].slots[0], "sipa");
    setParameter(bad.tracks[0].slots[0], "width", std::numeric_limits<double>::infinity());
    failure(bad, fixtures.file("bad-parameter.wav"), options, "Non-finite insert parameter");
    bad = snapshot;
    install(bad.tracks[0].slots[0], "sipa");
    bad.tracks[0].slots[0].parameters.removeChild(0, nullptr);
    failure(bad, fixtures.file("missing-parameter.wav"), options, "Missing insert parameter");
    bad = snapshot;
    juce::ValueTree value("PARAM");
    bad.tracks[0].slots[0].parameters = juce::ValueTree("PARAMETERS");
    bad.tracks[0].slots[0].parameters.addChild(value, -1, nullptr);
    failure(bad, fixtures.file("empty-slot-parameter.wav"), options, "Parameters on empty slot");

    auto nonfinite = constantAudio(2000, .02f, .01f);
    nonfinite.setSample(1, 1000, std::numeric_limits<float>::quiet_NaN());
    bad = snapshot;
    bad.tracks[0].source = fixtures.file("nonfinite-source.wav");
    writeWav(bad.tracks[0].source, nonfinite, 48000);
    failure(bad, fixtures.file("nonfinite-result.wav"), options, "Non-finite source samples");

    failure(snapshot, fixtures.file("cancel-before.wav"), options, "Cancellation before render", [] { return true; });
    failure(snapshot, fixtures.file("cancel-callback-throws.wav"), options, "Throwing cancellation callback",
            []() -> bool { throw std::runtime_error("Cancellation callback failed"); });
    double progress = 0;
    const auto cancelledFile = fixtures.file("cancel-midway.wav");
    const int before = fixtures.directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories);
    const auto cancelled = hem::OfflineMixRenderer::render(snapshot, cancelledFile, options,
        [&] { return progress > .15; }, [&](double p) { progress = p; });
    require(cancelled.status.failed() && cancelled.frames == 0 && !cancelledFile.exists(), "Mid-render cancel published a file");
    require(fixtures.directory.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == before,
            "Mid-render cancellation leaked a temporary file");

    // Cancellation after every audio frame is written must still prevent commit.
    progress = 0;
    const auto lateFile = fixtures.file("cancel-after-write.wav");
    const auto late = hem::OfflineMixRenderer::render(snapshot, lateFile, options,
        [&] { return progress >= .989; }, [&](double p) { progress = p; });
    require(late.status.failed() && !lateFile.exists(), "Late cancellation published a final file");

    const auto existing = fixtures.file("existing.wav");
    require(existing.replaceWithText("precious existing bytes"), "Cannot create protected destination");
    const auto existsResult = hem::OfflineMixRenderer::render(snapshot, existing, options);
    require(existsResult.status.failed() && existing.loadFileAsString() == "precious existing bytes",
            "Existing destination was overwritten");

    // A destination created after validation is protected by the atomic
    // no-replace publication, rather than only an early exists() check.
    const auto raced = fixtures.file("publication-race.wav");
    bool created = false;
    const auto racedResult = hem::OfflineMixRenderer::render(snapshot, raced, options, {}, [&](double p) {
        if (p > 0 && !created) { created = true; require(raced.replaceWithText("concurrent writer"), "Cannot create race fixture"); }
    });
    require(created && racedResult.status.failed() && raced.loadFileAsString() == "concurrent writer",
            "Publication race replaced a concurrent file");

    // A dangling symlink is an existing destination too.
    const auto symlink = fixtures.file("dangling.wav");
    std::error_code linkError;
    std::filesystem::create_symlink(std::filesystem::u8path(fixtures.file("absent-target.wav").getFullPathName().toStdString()),
                                   std::filesystem::u8path(symlink.getFullPathName().toStdString()), linkError);
    if (!linkError) {
        const auto symlinkResult = hem::OfflineMixRenderer::render(snapshot, symlink, options);
        require(symlinkResult.status.failed() && symlink.isSymbolicLink(), "Dangling symlink destination changed");
    }

    // The RIFF check is arithmetic and runs before opening the writer. This
    // large requested tail at 384 kHz exceeds the bounded 32-bit data capacity.
    auto tooLarge = options;
    tooLarge.sampleRate = 384000;
    tooLarge.tailSeconds = 3600;
    failure(snapshot, fixtures.file("riff-overflow.wav"), tooLarge, "Classic RIFF capacity overflow");
    // Progress callbacks are observational and cannot sabotage a valid commit.
    const auto progressThrows = fixtures.file("throwing-progress.wav");
    const auto observed = hem::OfflineMixRenderer::render(snapshot, progressThrows, options, {},
        [](double) { throw std::runtime_error("UI callback unavailable"); });
    require(observed.status.wasOk() && progressThrows.existsAsFile(), "Progress callback invalidated export");
    std::cout << "PASS: invalid config/source/insert rejection, no-file cancellation, existing bytes and race protection, RIFF guard, cleanup\n";
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI runtime;
    try {
        Fixtures fixtures;
        checkBasicMix(fixtures);
        checkLatencyAndBaseline(fixtures);
        checkInsertState(fixtures);
        checkRateConversion(fixtures);
        checkFailureAndTransaction(fixtures);
        std::cout << "PASS: offline rendering uses no physical audio device, live mic, monitor EQ or calibration.\n"
                     "NOTE: unchanged baseline parity is not reconstructed true-peak certification.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}

```

## Tests/OfflineMixWorkerTests.cpp

```cpp
#include "../Source/Systems/OfflineMixPanel.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void wait(hem::OfflineMixWorker& worker){
    for(int i=0;worker.isBusy()&&i<2000;++i){juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
    require(!worker.isBusy(),"Worker completion failed to reach message thread");
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HEM-export-worker-"+juce::Uuid().toString());
    require(dir.createDirectory().wasOk(),"Test directory failed");
    struct Clean{juce::File path;~Clean(){path.deleteRecursively();}} clean{dir};
    int reports=0;juce::String last;
    hem::OfflineMixWorker worker([&](const juce::String& value){++reports;last=value;});
    hem::OfflineMixOptions options;options.tailSeconds=0;
    require(worker.start({},dir.getChildFile("empty.wav"),options),"Empty snapshot was not admitted");
    require(worker.isBusy(),"Worker not immediately busy");
    require(!worker.start({},dir.getChildFile("duplicate.wav"),options),"Concurrent export admitted");
    wait(worker);require(reports==1&&last.contains("Export unchanged"),"Failure was not reported exactly once");
    require(!dir.getChildFile("empty.wav").exists()&&!dir.getChildFile("duplicate.wav").exists(),"Failed export created final file");

    const auto source=dir.getChildFile("source.wav");
    std::unique_ptr<juce::OutputStream> stream=source.createOutputStream();
    juce::WavAudioFormat format;
    const auto writerOptions=juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer=format.createWriterFor(stream,writerOptions);require(writer!=nullptr,"Fixture writer failed");
    juce::AudioBuffer<float> data(2,4800);data.clear();data.setSample(0,1000,.05f);data.setSample(1,1000,-.025f);
    require(writer->writeFromAudioSampleBuffer(data,0,data.getNumSamples()),"Fixture write failed");writer.reset();
    hem::SessionSnapshot snapshot;snapshot.tracks[0].source=source;
    require(worker.start(snapshot,dir.getChildFile("complete.wav"),options),"Worker could not restart after failure");
    wait(worker);require(reports==2&&last.startsWith("Saved complete.wav"),"Success did not reach UI");
    require(dir.getChildFile("complete.wav").existsAsFile(),"Successful worker has no output");

    options.tailSeconds=30;
    require(worker.start(snapshot,dir.getChildFile("cancelled.wav"),options),"Cancel test was not admitted");worker.cancel();
    wait(worker);require(reports==3&&last.containsIgnoreCase("cancel"),"Cancellation did not reach UI");
    require(!dir.getChildFile("cancelled.wav").exists(),"Cancelled worker published final audio");
    {hem::OfflineMixWorker interrupted([](const juce::String&){});require(interrupted.start(snapshot,dir.getChildFile("closed.wav"),options),"Shutdown test not admitted");}
    require(!dir.getChildFile("closed.wav").exists(),"Shutdown published interrupted audio");

    hem::OfflineMixPanel panel(worker,[](const juce::File&,const hem::OfflineMixOptions&){return juce::Result::ok();});
    for(const auto size:std::array<juce::Point<int>,2>{{{800,470},{1600,940}}}){
        panel.setSize(size.x,size.y);juce::Image image(juce::Image::ARGB,size.x,size.y,true);juce::Graphics graphics(image);panel.paintEntireComponent(graphics,true);
        for(int i=0;i<panel.getNumChildComponents();++i){auto* child=panel.getChildComponent(i);require(child->getWidth()>0&&child->getHeight()>0&&panel.getLocalBounds().contains(child->getBounds()),"Export control exceeds panel bounds");}
        require(image.getPixelAt(0,0).getAlpha()==255,"Export panel background is not opaque");
    }
    std::cout<<"PASS offline worker busy exclusion, UI-thread completion, restart, cancel, shutdown cleanup and actual export panel raster at 1x/2x\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/PluginMeterDisplayTests.cpp

```cpp
#include "../Source/Plugins/PluginRegistry.h"
#include "../Source/Plugins/PluginMeterReadout.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace hem;
void require(bool value,const juce::String& message){if(!value)throw std::runtime_error(message.toStdString());}
void near(float actual,float expected,float tolerance,const char* message){require(std::abs(actual-expected)<=tolerance,message);}
void prepare(ModuleProcessor& p,double rate=48000,int block=256){p.setPlayConfigDetails(2,2,rate,block);p.prepareToPlay(rate,block);}
void parameter(ModuleProcessor& p,const char* id,float value){auto* v=p.state.getParameter(id);require(v!=nullptr,"Missing test parameter");v->setValueNotifyingHost(v->convertTo0to1(value));}
void process(ModuleProcessor& p,juce::AudioBuffer<float>& b){juce::MidiBuffer midi;p.processBlock(b,midi);}
void constant(juce::AudioBuffer<float>& b,float l,float r){for(int i=0;i<b.getNumSamples();++i){b.setSample(0,i,l);b.setSample(1,i,r);}}
float peak(const juce::AudioBuffer<float>& b){float result=0;for(int ch=0;ch<2;++ch)for(int i=0;i<b.getNumSamples();++i)result=std::max(result,std::abs(b.getSample(ch,i)));return result;}
juce::Rectangle<float> meterArea(ModuleProcessor& p){juce::Rectangle<float> area(16,42,928,77);return dynamic_cast<SurgicalEqProcessor*>(&p)?area.withTrimmedLeft(area.getWidth()*.52f+8):area;}
juce::Image raster(ModuleProcessor& p,int scale){
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    juce::Image image(juce::Image::ARGB,editor->getWidth()*scale,editor->getHeight()*scale,true);
    juce::Graphics g(image);g.addTransform(juce::AffineTransform::scale(float(scale)));editor->paintEntireComponent(g,true);return image;
}
juce::File artifacts;
void save(const juce::Image& image,const juce::String& name){if(artifacts==juce::File())return;auto out=artifacts.getChildFile(name+".png").createOutputStream();require(out!=nullptr,"Cannot write raster artifact");require(juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot encode raster artifact");}
int fillPixels(const juce::Image& image,juce::Rectangle<float> track,int scale){int count=0;const int y=int(track.getCentreY()*float(scale));for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x){const auto c=image.getPixelAt(x,y);if(c==juce::Colour(ui::pluginMeterPositiveColour)||c==juce::Colour(ui::pluginMeterNegativeColour))++count;}return count;}
void noBar(ModuleProcessor& p,const juce::String& name){for(int scale:{1,2}){const auto image=raster(p,scale);require(fillPixels(image,ui::pluginMeterTrack(meterArea(p)),scale)==0,name+" displays a fictitious level bar");save(image,name+"-"+juce::String(scale)+"x");}}
void signedRaster(ModuleProcessor& p,const juce::String& name){
    const auto readout=ui::pluginMeterReadout(p);require(readout.signedScale&&readout.available,"Expected available signed measurement");
    const auto track=ui::pluginMeterTrack(meterArea(p));
    for(int scale:{1,2}){
        const auto image=raster(p,scale);const int y=int(track.getCentreY()*float(scale));
        const auto fill=juce::Colour(readout.value<0?ui::pluginMeterNegativeColour:ui::pluginMeterPositiveColour);
        const float start=track.getX()+track.getWidth()*readout.fillStart(),end=track.getX()+track.getWidth()*readout.fillEnd();
        int first=-1,last=-1;
        for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x)if(image.getPixelAt(x,y)==fill){if(first<0)first=x;last=x;}
        if(readout.value==0)require(first<0,"Signed zero drew a half-full bar");
        else {require(first>=0,"Signed measurement has no raster bar");require(std::abs(float(first)-start*float(scale))<=3*scale&&std::abs(float(last)-end*float(scale))<=3*scale,"Signed raster endpoints are not relative to zero");}
        const int zero=int(std::round((track.getX()+track.getWidth()*.5f)*float(scale)));
        require(image.getPixelAt(zero,y)==juce::Colour(ui::pluginMeterZeroColour),"Signed meter has no explicit zero-reference raster");
        for(int i=0;i<readout.tickCount;++i){const int x=int(std::round(track.getX()+track.getWidth()*readout.position(readout.ticks[size_t(i)])))*scale;require(image.getPixelAt(x,int(track.getBottom()+1)*scale)!=juce::Colour(0xff021018),"Signed scale tick is missing from actual raster");}
        save(image,name+"-"+juce::String(scale)+"x");
    }
}
void testLifecycleAndSilence(){
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();
        auto readout=ui::pluginMeterReadout(*p);require(!readout.available&&!readout.snapshot.hasProcessedAudio&&readout.valueText=="NO PROCESSED AUDIO",juce::String(entry.id)+" claims audio before prepare");
        noBar(*p,juce::String(entry.id)+"-unprepared");
        prepare(*p);readout=ui::pluginMeterReadout(*p);require(!readout.snapshot.hasProcessedAudio,"prepare fabricated meter readiness");
        require(readout.snapshot.isPrepared&&readout.snapshot.sampleRate==48000&&readout.snapshot.latencySamples==p->getLatencySamples(),"Prepared metadata was not coherently published");
        noBar(*p,juce::String(entry.id)+"-prepared");
        juce::AudioBuffer<float> empty(2,0),mono(1,256);mono.clear();process(*p,empty);process(*p,mono);
        require(!p->meterSnapshot().hasProcessedAudio,"Skipped buffer fabricated processed audio");
        juce::AudioBuffer<float> b(2,256);b.clear();process(*p,b);
        auto snapshot=p->meterSnapshot();require(snapshot.hasProcessedAudio&&snapshot.processedBlocks==1&&snapshot.blockSamples==256,"Actual silent block did not publish readiness");
        require(!snapshot.signalPresent,"Actual silence reports signal activity");
        readout=ui::pluginMeterReadout(*p);
        if(p->meterKind()==PluginMeterKind::correlation)require(!readout.available&&readout.valueText.startsWith("UNDEFINED"),"Silent correlation is fabricated as zero");
        else {require(readout.available,"A valid zero/negative-infinity meter was suppressed");near(snapshot.value,0,1.e-9f,"Fresh silence has nonzero detector value");}
        noBar(*p,juce::String(entry.id)+"-silence");
        constant(b,.25f,.125f);process(*p,b);require(p->meterSnapshot().processedBlocks==2,"Processed block counter did not advance");
        process(*p,empty);require(p->meterSnapshot().processedBlocks==2,"Zero-length callback replaced last real block");
        prepare(*p,96000);require(!p->meterSnapshot().hasProcessedAudio&&!p->meterSnapshot().valueAvailable,"Reprepare exposed stale core meter");
        require(ui::pluginMeterReadout(*p).valueText=="NO PROCESSED AUDIO","Reprepare did not restore explicit waiting state");
        require(p->meterSnapshot().isPrepared&&p->meterSnapshot().sampleRate==96000&&p->meterSnapshot().latencySamples==p->getLatencySamples(),"Reprepare retained stale rate/latency metadata");
    }
}
void testCorrelation(){
    SipaProcessor p;prepare(p);juce::AudioBuffer<float> b(2,256);
    constant(b,.5f,.5f);process(p,b);near(p.meterSnapshot().value,1,1.e-6f,"Correlated signal does not measure +1");signedRaster(p,"correlation-positive");
    constant(b,.5f,-.5f);process(p,b);near(p.meterSnapshot().value,-1,1.e-6f,"Anticorrelated signal does not measure -1");signedRaster(p,"correlation-negative");
    for(int i=0;i<256;++i){const float left[]={.5f,0,-.5f,0},right[]={0,.5f,0,-.5f};b.setSample(0,i,left[i%4]);b.setSample(1,i,right[i%4]);}
    process(p,b);near(p.meterSnapshot().value,0,1.e-6f,"Orthogonal signal does not measure zero correlation");require(p.meterSnapshot().valueAvailable,"Measured zero correlation was treated as silence");signedRaster(p,"correlation-zero");
    constant(b,.5f,0);process(p,b);require(!p.meterSnapshot().valueAvailable,"One silent channel has a fabricated correlation");noBar(p,"correlation-one-channel");
    constant(b,1.e-7f,1.e-7f);process(p,b);require(!p.meterSnapshot().valueAvailable,"StereoCore denominator guard is not respected");
    constant(b,1.e-5f,1.e-5f);process(p,b);require(p.meterSnapshot().valueAvailable,"Usable quiet stereo energy was discarded");
}
void testSamplePeaks(){
    for(const auto& entry:PluginRegistry::entries()){
        auto p=entry.create();if(p->meterKind()!=PluginMeterKind::samplePeak)continue;prepare(*p);juce::AudioBuffer<float> b(2,512);
        constant(b,.5f,.25f);process(*p,b);const auto s=p->meterSnapshot();near(s.value,peak(b),1.e-6f,"Published sample peak differs from actual output");require(s.signalPresent&&s.valueAvailable,"Real sample peak not marked available");
    }
    MonitorProcessor p;prepare(p);juce::AudioBuffer<float> b(2,256);constant(b,.5f,.25f);process(p,b);
    const auto readout=ui::pluginMeterReadout(p);near(readout.value,-6.0206f,.0001f,"Half-scale output is not calibrated to -6.0206 dBFS");
    require(readout.minimum==-60&&readout.maximum==0,"Sample peak display scale changed");
    const auto track=ui::pluginMeterTrack(meterArea(p));
    for(int scale:{1,2}){const auto image=raster(p,scale);int last=-1;const int y=int(track.getCentreY()*float(scale));for(int x=int(track.getX()*float(scale));x<int(track.getRight()*float(scale));++x)if(image.getPixelAt(x,y)==juce::Colour(ui::pluginMeterPositiveColour))last=x;
        const float expected=(track.getX()+track.getWidth()*readout.position(readout.value))*float(scale);
        require(std::abs(float(last)-expected)<=2*scale,"Actual output-peak raster is not calibrated to -60..0 dBFS");
        for(int i=0;i<readout.tickCount;++i){const int x=int(std::round(track.getX()+track.getWidth()*readout.position(readout.ticks[size_t(i)])))*scale;require(image.getPixelAt(x,int(track.getBottom()+1)*scale)!=juce::Colour(0xff021018),"A dBFS raster tick is missing");}
        save(image,"sample-peak-half-scale-"+juce::String(scale)+"x");}
    constant(b,0,0);process(p,b);const auto silent=ui::pluginMeterReadout(p);require(silent.available&&std::isinf(silent.value)&&silent.value<0&&silent.valueText.contains("-inf dBFS"),"Silent peak is displayed as a fabricated finite floor");
    constant(b,2,1);process(p,b);const auto over=ui::pluginMeterReadout(p);near(over.value,6.0206f,.0001f,"Over-scale numeric peak was silently clamped");require(over.valueText.contains("ABOVE SCALE"),"Over-scale peak lacks explicit indication");
}
void testGainReduction(){
    VcaProcessor p;prepare(p);parameter(p,"threshold",-24);parameter(p,"ratio",4);parameter(p,"attack",1);
    juce::AudioBuffer<float> b(2,256);for(int i=0;i<50;++i){constant(b,.5f,.5f);process(p,b);}
    const auto s=p.meterSnapshot();const float expected=(20.f*std::log10(.5f)+24)*.75f;
    near(s.value,expected,.015f,"VCA GR does not match calibrated 4:1 compression");near(s.value,-20.f*std::log10(peak(b)/.5f),.015f,"VCA GR does not describe actual full-wet output attenuation");
    auto readout=ui::pluginMeterReadout(p);require(readout.minimum==0&&readout.maximum==24&&readout.fillStart()==0,"GR scale is not 0..24 dB from zero");
    for(int scale:{1,2}){const auto image=raster(p,scale);const auto track=ui::pluginMeterTrack(meterArea(p));near(float(fillPixels(image,track,scale)),track.getWidth()*readout.position(s.value)*float(scale),2.f*float(scale),"Actual GR bar width differs from measured reduction");save(image,"vca-real-gr-"+juce::String(scale)+"x");}
    b.clear();process(p,b);readout=ui::pluginMeterReadout(p);require(readout.available&&!readout.snapshot.signalPresent&&readout.value>0&&readout.valueText.contains("SILENT OUTPUT"),"Real detector release into silence is mislabeled");
}
void testSignedGain(){
    for(const float amount:{-12.f,0.f,12.f}){
        TransientProcessor p;prepare(p);parameter(p,"attack",amount);juce::AudioBuffer<float> b(2,1024);b.clear();process(p,b);constant(b,.25f,.25f);process(p,b);
        const auto readout=ui::pluginMeterReadout(p);float measured=0;for(int i=0;i<b.getNumSamples();++i){const float change=20.f*std::log10(b.getSample(0,i)/.25f);if(std::abs(change)>std::abs(measured))measured=change;}
        near(readout.value,measured,.001f,"Transient pre-mix envelope reading differs from actual full-wet gain");
        require(readout.minimum==-12&&readout.maximum==12&&readout.zeroPosition()==.5f,"Signed transient calibration lacks zero centre");
        require(readout.heading.contains("PRE-MIX")&&readout.heading.contains("PRE-OUTPUT"),"Transient meter suggests it includes mix/output trim");
        if(amount==0)require(readout.fillStart()==.5f&&readout.fillEnd()==.5f,"Zero gain has a half-full geometry");
        else require(readout.value*amount>0&&std::abs(readout.value)<=12,"Transient gain change lost its sign or bounds");
        signedRaster(p,"transient-"+juce::String(amount,0));
    }
}
void testFirEstimate(){
    TruePeakProcessor p;prepare(p,11025,256);parameter(p,"ceiling",-6);
    require(p.getLatencySamples()==56,"Limiter is not reporting ceil(rate*5ms) actual latency");
    auto readout=ui::pluginMeterReadout(p);require(readout.heading.contains("ESTIMATE")&&readout.heading.contains("8x, 32-TAP")&&readout.detail.contains("56 samples / 5.079 ms"),"Limiter uses an unqualified TP claim or hardcoded 5 ms latency");
    Reconstruction filter;std::array<std::array<float,Reconstruction::taps>,2> history{};int position=0;juce::AudioBuffer<float> b(2,256);
    float reference=0;for(int block=0;block<12;++block){for(int i=0;i<256;++i){const float sample=.95f*std::sin(float(2*pi*1000*(block*256+i)/11025));b.setSample(0,i,sample);b.setSample(1,i,-sample);}process(p,b);reference=0;
        for(int i=0;i<256;++i){for(int ch=0;ch<2;++ch){history[size_t(ch)][size_t(position)]=b.getSample(ch,i);for(const auto& phase:filter.h){double v=0;for(int k=0;k<Reconstruction::taps;++k)v+=phase[size_t(k)]*history[size_t(ch)][size_t((position-k+Reconstruction::taps)%Reconstruction::taps)];reference=std::max(reference,float(std::abs(v)));}}position=(position+1)%Reconstruction::taps;}
        near(p.meterSnapshot().value,reference,1.e-6f,"Limiter meter does not match the actual output's defined 8x FIR reconstruction");
    }
    readout=ui::pluginMeterReadout(p);require(readout.available&&readout.snapshot.auxiliary>0&&readout.valueText.contains("dBTP EST.")&&readout.valueText.contains("GR"),"Measured limiter estimate/reduction is missing");require(reference<=gain(-6)+1.e-5f,"Defined FIR estimate exceeds settled limiter ceiling");
    for(int scale:{1,2})save(raster(p,scale),"fir-estimate-"+juce::String(scale)+"x");
}
void testSnapshotCoherence(){
    MonitorProcessor p;prepare(p);juce::AudioBuffer<float> a(2,16),b(2,32);constant(a,.25f,.25f);constant(b,.5f,.5f);
    std::atomic<bool> done{false};std::atomic<bool> coherent{true};
    std::thread writer([&]{for(int i=1;i<=3000;++i)process(p,i%2?a:b);done.store(true);});
    while(!done.load()){const auto s=p.meterSnapshot();if(!s.hasProcessedAudio)continue;const bool odd=(s.processedBlocks%2)!=0;if(s.blockSamples!=(odd?16u:32u)||s.value!=(odd?.25f:.5f)||!s.valueAvailable||!s.signalPresent||!s.isPrepared||s.sampleRate!=48000||s.latencySamples!=0)coherent.store(false);}
    writer.join();require(coherent.load()&&p.meterSnapshot().processedBlocks==3000,"GUI observed a torn audio-thread telemetry snapshot");
}
void testReprepareMetadataCoherence(){
    TruePeakProcessor p;
    const auto initial=ui::pluginMeterReadout(p);
    require(!initial.snapshot.isPrepared&&initial.snapshot.sampleRate==0&&initial.snapshot.latencySamples==0
            &&initial.detail.contains("LATENCY UNAVAILABLE"),"Unprepared limiter invented latency metadata");
    std::atomic<bool> done{false},go{false},coherent{true};
    std::thread writer([&]{
        juce::AudioBuffer<float> silence(2,16);silence.clear();
        while(!go.load())std::this_thread::yield();
        for(int i=0;i<1200;++i){
            constexpr double rates[]{44100,96000,11025,48000};
            // Same serialized host writer reconfigures then processes. Only the
            // actual GUI readout runs concurrently, never core DSP with prepare.
            prepare(p,rates[i%4],16);process(p,silence);
        }
        done.store(true);
    });
    unsigned observations=0;go.store(true);
    while(!done.load()){
        ++observations;const auto readout=ui::pluginMeterReadout(p);const auto& s=readout.snapshot;
        if(!s.isPrepared){
            if(s.hasProcessedAudio||s.sampleRate!=0||s.latencySamples!=0||!readout.detail.contains("LATENCY UNAVAILABLE"))coherent.store(false);
            continue;
        }
        const bool knownRate=s.sampleRate==44100||s.sampleRate==96000||s.sampleRate==11025||s.sampleRate==48000;
        const int expectedLatency=std::max(Reconstruction::taps,int(std::ceil(s.sampleRate*.005)));
        const auto expectedText=juce::String(expectedLatency)+" samples / "+juce::String(1000.0*double(expectedLatency)/s.sampleRate,3)+" ms";
        if(!knownRate||s.latencySamples!=expectedLatency||!readout.detail.contains(expectedText))coherent.store(false);
        if(s.hasProcessedAudio){
            const auto expectedWindow="16 samples / "+juce::String(16000.0/s.sampleRate,2)+" ms";
            if(s.processedBlocks!=1||s.blockSamples!=16||!readout.detail.contains(expectedWindow))coherent.store(false);
        }else if(s.processedBlocks!=0||s.blockSamples!=0)coherent.store(false);
    }
    writer.join();
    require(observations>0&&coherent.load(),"GUI mixed sample rate, latency, or block metadata across concurrent reprepare");
    const auto final=p.meterSnapshot();require(final.isPrepared&&final.hasProcessedAudio&&final.sampleRate==48000&&final.latencySamples==240,"Final prepared metadata is stale");
}

}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;try{
    if(argc>1){artifacts=juce::File(argv[1]);require(artifacts.createDirectory().wasOk(),"Cannot create artifact directory");}
    testLifecycleAndSilence();testCorrelation();testSamplePeaks();testGainReduction();testSignedGain();testFirEstimate();testSnapshotCoherence();testReprepareMetadataCoherence();
    std::cout<<"PASS: 12 native processor readiness/reset/real-silence states; correlation +/-1/zero/undefined; actual sample peaks and VCA GR; signed transient gain; defined 8x FIR output estimate and real latency; coherent atomic snapshots and concurrent reprepare rate/latency metadata; actual native editor 1x/2x calibrated raster\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

```

## Tests/ProcessorControlBindingTests.cpp

```cpp
#include "../Source/Plugins/PluginRegistry.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const juce::String& message){if(!condition)throw std::runtime_error(message.toStdString());}
juce::Component* find(juce::Component& parent,const juce::String& id){
 if(parent.getComponentID()==id)return &parent;
 for(int i=0;i<parent.getNumChildComponents();++i)if(auto* c=find(*parent.getChildComponent(i),id))return c;
 return nullptr;
}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 int parameters=0,toggles=0,knobs=0;
 for(const auto& entry:hem::PluginRegistry::entries()){
  auto processor=entry.create();processor->setPlayConfigDetails(2,2,48000,256);processor->prepareToPlay(48000,256);
  std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());require(editor!=nullptr,"Missing native editor");
  for(const auto& spec:processor->parameterSpecs()){
   ++parameters;auto* raw=processor->state.getRawParameterValue(spec.id);require(raw!=nullptr,"Missing APVTS parameter");
   require(std::abs(raw->load()-spec.initial)<1.e-4f,juce::String(entry.id)+" startup parameter mismatch: "+spec.id);
   auto* component=find(*editor,spec.id);require(component!=nullptr,juce::String(entry.id)+" missing actual control: "+spec.id);
   require(component->getName()==spec.name,"Accessible control identity differs from parameter contract");
   if(spec.toggle){
    ++toggles;auto* button=dynamic_cast<juce::ToggleButton*>(component);require(button!=nullptr,"Toggle mapped to wrong control");
    button->setToggleState(true,juce::sendNotificationSync);require(raw->load()==1.f,"Native toggle ON did not reach DSP parameter");
    button->setToggleState(false,juce::sendNotificationSync);require(raw->load()==0.f,"Native toggle OFF did not reach DSP parameter");
   }else{
    ++knobs;auto* slider=dynamic_cast<juce::Slider*>(component);require(slider!=nullptr,"Numeric parameter mapped to wrong control");
    require(std::abs(slider->getMinimum()-spec.minimum)<1.e-4&&std::abs(slider->getMaximum()-spec.maximum)<1.e-4,"Native knob range differs from DSP parameter range");
    for(const auto value:{spec.minimum,spec.maximum,spec.initial}){
     slider->setValue(value,juce::sendNotificationSync);require(std::abs(raw->load()-value)<=std::max(1.e-4f,spec.step*.51f),juce::String(entry.id)+" knob failed to update "+spec.id);
    }
   }
   auto* parameter=processor->state.getParameter(spec.id);parameter->setValueNotifyingHost(parameter->convertTo0to1(spec.initial));
  }
  // Real GUI composition, with all native parameter controls inside the editor.
  juce::Image pixels(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);juce::Graphics g(pixels);editor->paintEntireComponent(g,true);
  for(const auto& spec:processor->parameterSpecs()){auto* c=find(*editor,spec.id);require(c&&c->getWidth()>0&&c->getHeight()>0&&editor->getLocalBounds().contains(c->getBounds()),"Parameter control lies outside its native editor");}
  editor.reset();processor->releaseResources();
 }
 std::cout<<"PASS "<<hem::PluginRegistry::entries().size()<<" real native editors, "<<parameters<<" parameter bindings ("<<knobs<<" numeric, "<<toggles<<" toggles), startup defaults, min/max/reset, accessible identities and raster bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

```

## Tests/SessionTests.cpp

```cpp
#include "../Source/SessionState.h"
#include "../Source/Systems/PresetManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){juce::ScopedJuceInitialiser_GUI juceRuntime;
 try{
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hem-session-test",".hem.xml");
    hem::SessionSnapshot s;s.inputDb=-3;s.outputDb=-6;s.positionSeconds=123.5;s.looping=true;
    hem::MonitorProcessor monitor;s.monitorParameters=hem::SessionState::captureParameters(monitor);
    for(size_t t=0;t<13;++t){auto& track=s.tracks[t];track.gainDb=-float(t);track.muted=(t%2)==0;track.solo=t==4;track.name="Track "+juce::String(int(t)+1);
        for(size_t j=0;j<8;++j){auto& slot=track.slots[j];const auto& entry=hem::PluginRegistry::entries()[(t+j)%11];slot.pluginId=entry.id;slot.bypassed=(j%2)==1;auto p=entry.create();
            require(hem::PluginRegistry::idFor(*p)==entry.id,"Registry persistence ID mismatch");
            slot.parameters=hem::SessionState::captureParameters(*p);
        }
    }
    hem::SessionState::save(s,file);auto recalled=hem::SessionState::load(file);
    require(recalled.inputDb==s.inputDb&&recalled.positionSeconds==123.5&&recalled.looping,"Master state round trip");
    for(size_t t=0;t<13;++t){const auto& a=s.tracks[t];const auto& b=recalled.tracks[t];require(a.gainDb==b.gainDb&&a.muted==b.muted&&a.solo==b.solo,"Track state round trip");
        for(size_t j=0;j<8;++j){require(a.slots[j].pluginId==b.slots[j].pluginId&&a.slots[j].bypassed==b.slots[j].bypassed,"Insert order round trip");auto p=hem::PluginRegistry::create(b.slots[j].pluginId);hem::SessionState::restoreParameters(*p,b.slots[j].parameters);require(hem::SessionState::captureParameters(*p).isEquivalentTo(a.slots[j].parameters),"Parameter round trip");}}
    auto rejected=[&](juce::ValueTree tree){try{hem::SessionState::decode(tree,file);}catch(const std::exception&){return true;}return false;};
    auto tree=hem::SessionState::encode(s,file);tree.setProperty("version",999,nullptr);require(rejected(tree),"Unknown version accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).setProperty("gainDb",999,nullptr);require(rejected(tree),"Invalid gain accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(2).setProperty("index",0,nullptr);require(rejected(tree),"Duplicate track accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).getChild(0).setProperty("pluginId","missing-plugin",nullptr);require(rejected(tree),"Unknown plugin accepted");
    tree=hem::SessionState::encode(s,file);tree.getChild(1).setProperty("source",file.getFileName()+"-missing.wav",nullptr);require(rejected(tree),"Missing stem silently accepted");
    hem::SipaProcessor processor;auto values=hem::SessionState::captureParameters(processor);values.getChild(0).setProperty("value",999999,nullptr);bool bad=false;try{hem::SessionState::restoreParameters(processor,values);}catch(const std::exception&){bad=true;}require(bad,"Invalid plugin value accepted");
    hem::InsertChain chain;chain.prepare(96000,256);auto node=chain.stageNode(std::make_unique<hem::SipaProcessor>(),96000,256,chain.slotLatency());chain.installInitial(7,std::move(node),true);require(chain.activeProcessor(7)&&chain.isBypassed(7)&&!chain.isBusy(7),"Staged chain installation failed");
    auto json=file.withFileExtension("json");hem::PresetManager::save(s,json);auto jsonState=hem::PresetManager::load(json);
    require(jsonState.outputDb==s.outputDb&&jsonState.tracks[12].slots[7].pluginId==s.tracks[12].slots[7].pluginId,"JSON session round trip");
    for(size_t t=0;t<13;++t)for(size_t j=0;j<8;++j){auto p=hem::PluginRegistry::create(jsonState.tracks[t].slots[j].pluginId);hem::SessionState::restoreParameters(*p,jsonState.tracks[t].slots[j].parameters);require(hem::SessionState::captureParameters(*p).isEquivalentTo(s.tracks[t].slots[j].parameters),"JSON parameter round trip");}
    require(hem::PresetManager::list(json.getParentDirectory()).contains(json),"Preset listing omitted JSON");
    require(hem::PresetManager::rename(json,"../bad").failed(),"Invalid rename accepted");
    require(hem::PresetManager::remove(json).wasOk()&&!json.existsAsFile(),"Preset deletion failed");
    file.deleteFile();std::cout<<"PASS: 13 x 8 XML state, processor parameters, schema rejection, missing-file rejection, staged routing\n";
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/SpecialtyTests.cpp

```cpp
#include "../Source/DSP/SpecialtyCore.h"
#include "../Source/DSP/RestorerCore.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static bool countAllocations=false;static size_t allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
constexpr int block=128;
using Buffer=std::array<float,block>;
template<class Core,class Settings> void neutral(Core& c,const Settings& p,double rate){
 c.prepare(rate);Buffer l{},r{},original{};
 for(int b=0;b<40;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=original[size_t(i)]=.2f*std::sin(float((b*block+i)*.7));
 countAllocations=true;c.process(l.data(),r.data(),block,p);countAllocations=false;
 for(int i=0;i<block;++i)require(std::abs(l[size_t(i)]-original[size_t(i)])<2.e-7f,"Neutral module altered samples");}
}
int main(){try{for(double rate:{44100.,48000.,96000.}){
 hem::DhtaCore d;hem::TransientCore t;hem::SubBassCore s;hem::RestorerCore a;
 neutral(d,hem::DhtaSettings{},rate);neutral(t,hem::TransientSettings{},rate);neutral(s,hem::SubBassSettings{},rate);neutral(a,hem::RestorerSettings{},rate);
 Buffer l{},r{},dl{},dr{};hem::DhtaCore delta;d.prepare(rate);delta.prepare(rate);hem::DhtaSettings p;p.threshold=-45;p.range=6;p.attack=.1f;p.release=80;
 double inputEnergy=0,outputEnergy=0;int n=0;
 for(int b=0;b<500;++b){for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=dl[size_t(i)]=dr[size_t(i)]=.2f*float(std::sin(2*hem::pi*6800*(n+i)/rate));
 countAllocations=true;p.delta=false;d.process(l.data(),r.data(),block,p);p.delta=true;delta.process(dl.data(),dr.data(),block,p);countAllocations=false;
 if(b>400)for(int i=0;i<block;++i){const float x=.2f*float(std::sin(2*hem::pi*6800*(n+i)/rate));inputEnergy+=x*x;outputEnergy+=l[size_t(i)]*l[size_t(i)];require(std::abs(l[size_t(i)]+dl[size_t(i)]-x)<2.e-6f,"Delta and processed do not reconstruct dry");}n+=block;}
 const double reduction=-10*std::log10(outputEnergy/inputEnergy);require(reduction>5.7&&reduction<6.1,"DHTA centre-frequency attenuation wrong");
 s.prepare(rate);hem::SubBassSettings bass;bass.lowGain=6;
 for(int b=0;b<500;++b){l.fill(.05f);r.fill(.05f);s.process(l.data(),r.data(),block,bass);}
 require(std::abs(l.back()-.05f*hem::gain(6))<1.e-5f,"Bass low-band gain wrong");
 bass.lowGain=0;bass.mono=100;double sideEnergy=0,sideInput=0;n=0;
 for(int b=0;b<500;++b){for(int i=0;i<block;++i){l[size_t(i)]=.1f*float(std::sin(2*hem::pi*20*(n+i)/rate));r[size_t(i)]=-l[size_t(i)];}
 s.process(l.data(),r.data(),block,bass);if(b>400)for(int i=0;i<block;++i){const float x=.1f*float(std::sin(2*hem::pi*20*(n+i)/rate));sideEnergy+=l[size_t(i)]*l[size_t(i)];sideInput+=x*x;}n+=block;}
 require(sideEnergy/sideInput<.3,"Low mono failed to reduce low side energy");
 t.prepare(rate);hem::TransientSettings trans;trans.attack=12;
 for(int b=0;b<100;++b){l.fill(0);r.fill(0);t.process(l.data(),r.data(),block,trans);}l.fill(.1f);r.fill(.1f);t.process(l.data(),r.data(),block,trans);require(l[20]>.15f,"Transient attack did not enhance onset");
 a.prepare(rate);hem::RestorerSettings rest;rest.amount=10;rest.floor=-20;rest.protect=0;
 for(int b=0;b<500;++b){l.fill(.001f);r.fill(.001f);a.process(l.data(),r.data(),block,rest);}
 require(l.back()<.0005f&&l.back()>0,"Spectral low-level reduction failed");
 // Exercise changing automation targets; all settings stay inside the UI ranges.
 for(int b=0;b<250;++b){p.frequency=b%2?1000:16000;p.q=b%2?.5f:8;p.delta=false;p.wide=b%3==0;
 for(int i=0;i<block;++i)l[size_t(i)]=r[size_t(i)]=.15f*std::sin(float(b*block+i));
 countAllocations=true;d.process(l.data(),r.data(),block,p);t.process(l.data(),r.data(),block,trans);s.process(l.data(),r.data(),block,bass);a.process(l.data(),r.data(),block,rest);countAllocations=false;
 for(float x:l)require(std::isfinite(x),"Automation produced non-finite output");}
 std::cout<<"PASS "<<rate<<" Hz / DHTA reduction "<<reduction<<" dB / neutral, delta, bass, mono, transient, restoration, automation\n";
 }require(allocations==0,"Process allocated memory");std::cout<<"PASS zero allocations in instrumented specialty DSP calls\n";return 0;
 }catch(const std::exception& e){countAllocations=false;std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/StemMixerTests.cpp

```cpp
#include <JuceHeader.h>
#include "../Source/TrackEngine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 constexpr int count=13,capacity=128;constexpr double rate=96000;
 juce::AudioBuffer<float> input(2,96000);for(int ch=0;ch<2;++ch)for(int i=0;i<input.getNumSamples();++i)input.setSample(ch,i,.01f);
 std::array<std::unique_ptr<juce::MemoryAudioSource>,count> sources;
 std::array<hem::TrackEngine,count> stems;
 juce::AudioBuffer<float> sum(2,capacity);std::array<float,capacity> gain{};gain.fill(1);
 for(int i=0;i<count;++i){sources[size_t(i)]=std::make_unique<juce::MemoryAudioSource>(input,true,false);auto& t=stems[size_t(i)];t.transport().setSource(sources[size_t(i)].get(),0,nullptr,rate,2);t.prepare(rate,capacity);t.transport().start();}
 auto render=[&](int requested){
    bool anySolo=false;for(auto& t:stems)anySolo|=t.solo.load();
    for(int offset=0;offset<requested;offset+=capacity){int n=std::min(capacity,requested-offset);sum.clear();for(auto& t:stems)t.render(sum,n,anySolo,gain.data());}
 };
 for(int i=0;i<30;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.13f)<1.e-5f,"13-stem unity sum incorrect");
 for(const auto& stem:stems){const auto meter=stem.signalMeters.snapshot();require(meter.ready&&std::abs(meter.left.peakDb+40.f)<.002f&&std::abs(meter.right.peakDb+40.f)<.002f,"Per-track meters do not measure actual post-trim stereo contribution");}
 stems[0].muted.store(true);for(int i=0;i<10;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.12f)<1.e-5f,"Mute summing incorrect");
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-stems[0].transport().getCurrentPosition())<1.e-9,"Muted stem lost sync");
 stems[1].solo.store(true);for(int i=0;i<10;++i)render(512);
 require(std::abs(sum.getSample(0,127)-.01f)<1.e-5f,"Solo exclusion incorrect");
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-stems[1].transport().getCurrentPosition())<1.e-9,"Solo-excluded stem lost sync");
 // JUCE 8.0.10 ResamplingAudioSource requests round(n * ratio) + 3 source
 // samples after flush. AudioTransportSource::getCurrentPosition reports that
 // source read head, not the sample currently emitted by the resampler.
 for(auto& t:stems){t.transport().setPosition(.5);require(std::abs(t.transport().getCurrentPosition()-.5)<1.e-9,"Common seek boundary failed");}
 render(128);
 constexpr int unityRatioSourceReadAhead=3;
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-(.5+(128+unityRatioSourceReadAhead)/rate))<1.e-9,"Common source read head failed");
 const auto firstPosition=stems[0].transport().getCurrentPosition();render(128);
 for(auto& t:stems)require(std::abs(t.transport().getCurrentPosition()-(firstPosition+128/rate))<1.e-9,"Common steady rendered-frame advancement failed");
 for(auto& t:stems){t.transport().setSource(nullptr);t.release();}
 std::cout<<"PASS 13-stem sum, bounded chunking, mute, solo, continuous source positions and shared seek\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/StudioBrandingTests.cpp

```cpp
#include "../Source/UI/StudioBranding.h"
#include <BinaryData.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int cyanPixels(const juce::Image& image,juce::Rectangle<int> bounds){int n=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x){const auto c=image.getPixelAt(x,y);if(c.getGreen()>150&&c.getBlue()>150&&c.getRed()<130)++n;}return n;}
int amberPixels(const juce::Image& image,juce::Rectangle<int> bounds){int n=0;for(int y=bounds.getY();y<bounds.getBottom();++y)for(int x=bounds.getX();x<bounds.getRight();++x){const auto c=image.getPixelAt(x,y);if(c.getRed()>190&&c.getGreen()>100&&c.getBlue()<150)++n;}return n;}
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
 const auto ghost=juce::ImageCache::getFromMemory(BinaryData::ghost_brand_reference_png,BinaryData::ghost_brand_reference_pngSize);
 require(ghost.isValid()&&ghost.getWidth()==1672&&ghost.getHeight()==941,"Approved embedded ghost is unavailable");
 const auto crop=hem::ui::StudioBranding::ghostSource();require(crop==juce::Rectangle<int>(10,702,244,196),"Approved ghost viewport changed");
 require(cyanPixels(ghost,juce::Rectangle<int>(85,755,50,30))>20,"Approved ghost eyes are absent from source");
 for(int scale:{1,2}){
  juce::Image image(juce::Image::ARGB,1536*scale,864*scale,true);juce::Graphics g(image);const auto root=image.getBounds().toFloat();
  hem::ui::StudioBranding::draw(g,root,ghost);
  const auto mapped=[&](float x,float y,float w,float h){return hem::ui::StudioBranding::mapped(root,x,y,w,h).toNearestInt();};
  require(cyanPixels(image,mapped(1326,114,176,150))>150*scale*scale,"Ghost identity/signature is not visible at startup");
  require(amberPixels(image,mapped(605,13,415,42))>500*scale*scale,"Top brand is not illuminated amber");
  require(cyanPixels(image,mapped(620,54,385,18))>150*scale*scale,"Studio Suite subtitle is not illuminated cyan");
  require(image.getPixelAt(1350*scale,150*scale).getAlpha()==255,"Ghost badge is not opaque");
  require(image.getPixelAt(500*scale,300*scale).getAlpha()==0,"Branding paints unrelated controls or meters");
 }
 std::cout<<"PASS embedded approved ghost identity, startup illuminated brand/subtitle, cropped badge and isolated painting at 1x/2x\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

```

## Tests/TakeRecorderTests.cpp

```cpp
#include "../Source/Systems/TakeRecorder.h"
#include "../Source/Systems/NoReplaceFile.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <thread>

static thread_local bool callback=false;
static std::atomic<int> allocations{0};
void* operator new(std::size_t n){if(callback)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}

namespace {
using Recorder=hem::TakeRecorder;
namespace publication=hem::noReplaceFile;
constexpr int blockFrames=256,blocks=40,totalFrames=blockFrames*blocks;

struct Fixture {
    juce::File directory=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hem-take-publication-"+juce::Uuid().toString());
    juce::File wav=directory.getChildFile("vocal.wav");
    juce::File sidecar=directory.getChildFile("vocal.wav.take.json");
    Fixture(){require(directory.createDirectory().wasOk(),"Cannot create fixture directory");}
    ~Fixture(){directory.deleteRecursively();}
    Recorder::Setup setup(int channels=1) const {
        Recorder::Setup s;s.file=wav;s.channels=channels;s.rate=96000;
        s.inputLatency=128;s.outputLatency=128;s.backingLatency=5088;
        s.inputName="Synthetic input";return s;
    }
    juce::Array<juce::File> partials() const {
        return directory.findChildFiles(juce::File::findFiles,false,"*.partial-*.wav");
    }
    juce::Array<juce::File> metadataPartials() const {
        return directory.findChildFiles(juce::File::findFiles,false,"*.take.json.partial-*");
    }
};

void waitForFinish(Recorder& recorder){
    for(int i=0;i<2500&&recorder.busy();++i)juce::Thread::sleep(2);
    require(!recorder.busy(),"Take worker timed out");
}
void arm(Recorder& recorder,const Recorder::Setup& setup){
    require(recorder.start(setup),"Cannot start take");
    for(int i=0;i<2500&&recorder.state()==Recorder::State::Opening;++i)juce::Thread::sleep(2);
    if(recorder.state()!=Recorder::State::Armed)std::cerr<<recorder.result()<<'\n';
    require(recorder.state()==Recorder::State::Armed,"Cannot arm take");
}
std::array<float,blockFrames> samples(){
    std::array<float,blockFrames> result{};
    for(int i=0;i<blockFrames;++i)result[size_t(i)]=.2f*std::sin(float(i)*.1f);
    return result;
}
void pushAudio(Recorder& recorder){
    const auto left=samples();auto right=left;for(auto& x:right)x=-x;
    for(int i=0;i<blocks;++i){
        callback=true;recorder.push(left.data(),right.data(),blockFrames,10.+i*blockFrames/96000.);callback=false;
    }
}
void stopAndWait(Recorder& recorder){recorder.stop();waitForFinish(recorder);}
void verifyWav(const juce::File& file,int channels){
    juce::WavAudioFormat format;
    auto reader=std::unique_ptr<juce::AudioFormatReader>(format.createReaderFor(file.createInputStream().release(),true));
    require(reader&&reader->sampleRate==96000&&reader->numChannels==unsigned(channels)
        &&reader->lengthInSamples==totalFrames&&reader->bitsPerSample==32&&reader->usesFloatingPointData,
        "Incorrect WAV format or frame count");
    juce::AudioBuffer<float> decoded(channels,totalFrames);
    require(reader->read(&decoded,0,totalFrames,0,true,true),"Cannot read take");
    const auto left=samples();
    for(int i=0;i<totalFrames;++i){
        require(std::abs(decoded.getSample(0,i)-left[size_t(i%blockFrames)])<1.e-7,"Dry vocal samples changed");
        if(channels==2)require(std::abs(decoded.getSample(1,i)+left[size_t(i%blockFrames)])<1.e-7,"Stereo print samples changed");
    }
}
void verifyMetadata(const juce::File& file,int channels){
    const auto json=juce::JSON::parse(file);
    require(int(json["frames"])==totalFrames&&int(json["channels"])==channels,"Metadata frame/channel mismatch");
    require(double(json["sampleRate"])==96000.,"Metadata rate mismatch");
    require(std::abs(double(json["estimatedPlacementSeconds"])-(10.-5344./96000))<1.e-9,"Latency metadata mismatch");
}
void verifySentinel(const juce::File& file,const juce::String& contents){
    juce::MemoryBlock actual;
    const auto bytes=contents.getNumBytesAsUTF8();
    const bool identical=file.loadFileAsData(actual)&&actual.getSize()==bytes
        &&std::memcmp(actual.getData(),contents.toRawUTF8(),bytes)==0;
    if(!identical)std::cerr<<file.getFullPathName()<<": expected "<<bytes<<" bytes, found "<<actual.getSize()<<'\n';
    require(identical,"Existing file bytes changed");
}
void writeSentinel(const juce::File& file,const juce::String& contents){
    // replaceWithText defaults to CRLF, even on Linux. Use raw UTF-8 bytes so
    // the fixture itself cannot alter newlines before the preservation test.
    require(file.replaceWithData(contents.toRawUTF8(),contents.getNumBytesAsUTF8()),"Cannot create competing file");
    verifySentinel(file,contents);
}
bool makeDanglingLink(const juce::File& link,const juce::File& target){
    std::error_code error;
    std::filesystem::create_symlink(publication::nativePath(target),publication::nativePath(link),error);
   #if JUCE_WINDOWS
    if(error){std::cout<<"SKIP: Windows symlink privilege unavailable: "<<error.message()<<'\n';return false;}
   #endif
    require(!error,"Cannot create dangling symlink fixture");return true;
}
void verifyDanglingLink(const juce::File& link,const juce::File& target){
    std::error_code error;
    require(std::filesystem::is_symlink(std::filesystem::symlink_status(publication::nativePath(link),error))&&!error,
        "Existing dangling symlink was replaced");
    require(std::filesystem::read_symlink(publication::nativePath(link))==publication::nativePath(target),"Symlink target changed");
    require(!target.exists(),"Dangling symlink target was created");
}

void normalTakes(){
    auto recorder=std::make_unique<Recorder>(); // Also cover reuse after completion.
    for(int channels:{1,2}){
        Fixture fixture;arm(*recorder,fixture.setup(channels));pushAudio(*recorder);stopAndWait(*recorder);
        if(recorder->state()!=Recorder::State::Complete)std::cerr<<recorder->result()<<'\n';
        require(recorder->state()==Recorder::State::Complete,"Take did not finalize");
        require(recorder->completedFile()==fixture.wav,"Completed take path missing");
        verifyWav(fixture.wav,channels);verifyMetadata(fixture.sidecar,channels);
        require(fixture.partials().isEmpty()&&fixture.metadataPartials().isEmpty(),"Successful take left a partial file");
    }
}
void preexistingDestination(bool danglingLink){
    Fixture fixture;const auto absent=fixture.directory.getChildFile("missing-source.wav");
    const juce::String sentinel="ORIGINAL WAV bytes must survive\n";
    if(danglingLink){if(!makeDanglingLink(fixture.wav,absent))return;}else writeSentinel(fixture.wav,sentinel);
    auto recorder=std::make_unique<Recorder>();require(recorder->start(fixture.setup()),"Cannot start rejection test");waitForFinish(*recorder);
    require(recorder->state()==Recorder::State::Failed,"Existing destination was not rejected");
    require(recorder->result().contains("already exists"),"Existing destination rejection was unclear");
    require(recorder->completedFile()==juce::File()&&recorder->framesWritten()==0,"Rejected take reported audio saved");
    require(fixture.partials().isEmpty()&&!fixture.sidecar.exists(),"Preflight rejection created take data");
    if(danglingLink)verifyDanglingLink(fixture.wav,absent);else verifySentinel(fixture.wav,sentinel);
}
void armedDestinationConflict(bool danglingLink){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();arm(*recorder,fixture.setup(2));pushAudio(*recorder);
    // Another writer claims the destination after recorder preflight, before
    // finalization. Every byte of our recorded take must remain recoverable.
    const auto absent=fixture.directory.getChildFile("missing-raced-source.wav");
    const juce::String sentinel="RACING WAV owner keeps these bytes\n";
    if(danglingLink){if(!makeDanglingLink(fixture.wav,absent)){stopAndWait(*recorder);return;}}else writeSentinel(fixture.wav,sentinel);
    stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Failed,"Raced destination did not fail publication");
    require(recorder->completedFile()==juce::File(),"Unpublished take claimed completion");
    require(recorder->result().contains("without overwriting")&&recorder->result().contains("partial WAV retained"),"Publication failure did not explain recovery");
    require(recorder->framesWritten()==totalFrames,"Publication conflict lost frame count");
    const auto partials=fixture.partials();require(partials.size()==1,"Publication conflict did not retain one partial WAV");
    require(recorder->result().contains(partials[0].getFullPathName()),"Partial recovery path missing");
    verifyWav(partials[0],2);require(!fixture.sidecar.exists(),"Failed WAV publication wrote metadata");
    if(danglingLink)verifyDanglingLink(fixture.wav,absent);else verifySentinel(fixture.wav,sentinel);
}
void sidecarConflict(bool createWhileArmed,bool danglingLink){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();
    const auto absent=fixture.directory.getChildFile("missing-metadata.json");
    const juce::String sentinel="{\"owner\":\"previous take, preserve byte for byte\"}\n";
    const auto makeConflict=[&]{if(danglingLink)return makeDanglingLink(fixture.sidecar,absent);writeSentinel(fixture.sidecar,sentinel);return true;};
    if(!createWhileArmed&&!makeConflict())return;
    arm(*recorder,fixture.setup());pushAudio(*recorder);
    if(createWhileArmed&&!makeConflict()){stopAndWait(*recorder);return;}
    stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Complete&&recorder->completedFile()==fixture.wav,"Saved WAV was incorrectly reported as failed");
    require(recorder->result().startsWith("Saved vocal.wav")&&recorder->result().contains("placement metadata not saved"),"Metadata conflict concealed saved WAV or missing metadata");
    verifyWav(fixture.wav,1);
    if(danglingLink)verifyDanglingLink(fixture.sidecar,absent);else verifySentinel(fixture.sidecar,sentinel);
    const auto retained=fixture.metadataPartials();require(retained.size()==1,"Conflicting metadata was not retained for recovery");
    verifyMetadata(retained[0],1);require(recorder->result().contains(retained[0].getFullPathName()),"Metadata recovery path missing");
    require(fixture.partials().isEmpty(),"Saved WAV retained an unnecessary audio duplicate");
}
void finalCommitConflict(){
    Fixture fixture;const auto source=fixture.directory.getChildFile("staged.wav");
    writeSentinel(source,"NEW CLOSED WAV");std::error_code error;
    require(publication::available(fixture.wav,error)&&!error,"Unexpected occupied commit fixture");
    // Deterministically occupy the name in the exact check/commit window that
    // JUCE moveFileTo used to clobber. This is the recorder's actual commit API.
    writeSentinel(fixture.wav,"WINNER AFTER FINAL CHECK");
    require(bool(publication::publish(source,fixture.wav)),"No-replace commit overwrote a race winner");
    verifySentinel(fixture.wav,"WINNER AFTER FINAL CHECK");verifySentinel(source,"NEW CLOSED WAV");
}
void simultaneousCommits(){
    Fixture fixture;const auto a=fixture.directory.getChildFile("a.wav"),b=fixture.directory.getChildFile("b.wav");
    writeSentinel(a,"FIRST COMPLETE TAKE");writeSentinel(b,"SECOND COMPLETE TAKE");
    std::atomic<int> ready{0};std::atomic<bool> go{false};std::error_code first,second;
    const auto publish=[&](const juce::File& source,std::error_code& result){
        ready.fetch_add(1,std::memory_order_release);while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        result=publication::publish(source,fixture.wav);
    };
    std::thread one([&]{publish(a,first);}),two([&]{publish(b,second);});
    while(ready.load(std::memory_order_acquire)!=2)std::this_thread::yield();
    go.store(true,std::memory_order_release);one.join();two.join();
    require(bool(first)!=bool(second),"Atomic publication did not select exactly one winner");
    verifySentinel(fixture.wav,first?"SECOND COMPLETE TAKE":"FIRST COMPLETE TAKE");
    verifySentinel(a,"FIRST COMPLETE TAKE");verifySentinel(b,"SECOND COMPLETE TAKE");
}
void emptyTake(){
    Fixture fixture;auto recorder=std::make_unique<Recorder>();arm(*recorder,fixture.setup());stopAndWait(*recorder);
    require(recorder->state()==Recorder::State::Failed&&recorder->result().contains("No input frames"),"Empty take did not fail clearly");
    require(!fixture.wav.exists()&&!fixture.sidecar.exists()&&fixture.partials().isEmpty(),"Empty take left output files");
}
} // namespace

int main(){try{
    normalTakes();preexistingDestination(false);preexistingDestination(true);
    armedDestinationConflict(false);armedDestinationConflict(true);
    sidecarConflict(false,false);sidecarConflict(true,false);
    sidecarConflict(false,true);sidecarConflict(true,true);
    finalCommitConflict();simultaneousCommits();emptyTake();
    require(allocations==0,"Audio capture allocated memory");
    std::cout<<"PASS: mono/stereo float WAV, exact frames/samples/placement, allocation-free push, existing and raced destination/sidecar preservation, dangling symlinks, retained recovery files, atomic competing commits, truthful saved-WAV metadata warnings, empty-take cleanup\n";
}catch(const std::exception& e){callback=false;std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

## Tests/VST3Fixture/CMakeLists.txt

```cmake
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

```

## Tests/VST3Fixture/FixtureProcessor.cpp

```cpp
#include "FixtureProcessor.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace hem::test
{
namespace
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "This test fixture requires a lock-free 64-bit atomic target");
constexpr std::array<std::uint8_t, 8> magic { 'H', 'E', 'M', 'F', 'X', '0', '0', '1' };

void write32(std::uint8_t* output, std::uint32_t value) noexcept
{
    for (unsigned n = 0; n != 4; ++n)
        output[n] = static_cast<std::uint8_t>(value >> (8 * n));
}

std::uint32_t read32(const std::uint8_t* input) noexcept
{
    std::uint32_t result = 0;
    for (unsigned n = 0; n != 4; ++n)
        result |= static_cast<std::uint32_t>(input[n]) << (8 * n);
    return result;
}

std::uint32_t checksum(const std::uint8_t* bytes) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (int n = 0; n != 20; ++n)
        hash = (hash ^ bytes[n]) * 16777619u;
    return hash;
}
}

class FixtureProcessor::Parameter final : public juce::RangedAudioParameter
{
public:
    Parameter(FixtureProcessor& ownerIn, bool latencyIn)
        : RangedAudioParameter(juce::ParameterID { latencyIn ? latencyParameterID : gainParameterID, 1 },
                               latencyIn ? "Latency mode" : "Fixture gain"),
          owner(ownerIn), latency(latencyIn), range(0.0f, 2.0f, latencyIn ? 1.0f : 0.0f) {}

    float getValue() const override
    {
        const auto value = owner.readSettings();
        return latency ? static_cast<float>(unpackMode(value)) * 0.5f : unpackGain(value) * 0.5f;
    }
    void setValue(float normalized) override { owner.changeParameter(latency, normalized); }
    float getDefaultValue() const override { return latency ? 0.0f : 0.5f; }
    bool isDiscrete() const override { return latency; }
    int getNumSteps() const override { return latency ? 3 : juce::AudioProcessor::getDefaultNumParameterSteps(); }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }
    juce::String getText(float normalized, int maximumLength) const override
    {
        const auto finite = std::isfinite(normalized) ? juce::jlimit(0.0f, 1.0f, normalized) : 0.0f;
        auto result = latency ? juce::String(latencyForMode(static_cast<int>(std::lround(finite * 2.0f)))) + " samples"
                              : juce::String(finite * 2.0f, 6);
        return maximumLength > 0 ? result.substring(0, maximumLength) : result;
    }
    float getValueForText(const juce::String& text) const override
    {
        if (latency)
        {
            const auto samples = text.getIntValue();
            return samples == 64 ? 0.5f : (samples == oversizedLatency ? 1.0f : 0.0f);
        }
        const auto value = text.getFloatValue();
        return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value * 0.5f) : 0.5f;
    }
    juce::StringArray getAllValueStrings() const override
    {
        return latency ? juce::StringArray { "0 samples", "64 samples", "262144 samples" } : juce::StringArray {};
    }

private:
    FixtureProcessor& owner;
    const bool latency;
    const juce::NormalisableRange<float> range;
};

std::uint64_t FixtureProcessor::pack(float gain, int mode) noexcept
{
    return std::bit_cast<std::uint32_t>(gain) | (static_cast<std::uint64_t>(mode) << 32);
}

float FixtureProcessor::unpackGain(std::uint64_t value) noexcept
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(value));
}

int FixtureProcessor::unpackMode(std::uint64_t value) noexcept { return static_cast<int>(value >> 32); }

int FixtureProcessor::latencyForMode(int mode) noexcept
{
    return mode == 1 ? 64 : (mode == 2 ? oversizedLatency : 0);
}

FixtureProcessor::FixtureProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      settings(pack(1.0f, 0))
{
    addParameter(new Parameter(*this, false));
    addParameter(new Parameter(*this, true));
}

bool FixtureProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    return layout.inputBuses.size() == 1 && layout.outputBuses.size() == 1
        && layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

double FixtureProcessor::getTailLengthSeconds() const
{
    return static_cast<double>(latencyForMode(unpackMode(readSettings()))) / std::max(1.0, getSampleRate());
}

void FixtureProcessor::publishLatency(int mode)
{
    // JUCE owns and transports this host notification; there is no fixture worker.
    setLatencySamples(latencyForMode(mode));
}

void FixtureProcessor::changeParameter(bool isLatency, float normalized) noexcept
{
    if (!std::isfinite(normalized))
        return;
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    auto previous = readSettings();
    std::uint64_t next;
    do
    {
        next = isLatency ? pack(unpackGain(previous), static_cast<int>(std::lround(normalized * 2.0f)))
                         : pack(normalized * 2.0f, unpackMode(previous));
    }
    while (!settings.compare_exchange_weak(previous, next, std::memory_order_release, std::memory_order_acquire));
    if (isLatency)
        publishLatency(unpackMode(next));
}

void FixtureProcessor::prepareToPlay(double, int)
{
    // Fixed 2 MiB allocation outside processBlock, independent of host block size.
    delay = std::make_unique<float[]>(2 * capacity);
    reset();
    publishLatency(unpackMode(readSettings()));
}

void FixtureProcessor::releaseResources()
{
    delay.reset();
    reset();
}

void FixtureProcessor::reset()
{
    writeHead = 0;
    available = 0;
    activeMode = -1;
}

void FixtureProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    if (delay == nullptr || audio.getNumChannels() != 2)
    {
        audio.clear();
        return;
    }

    const auto blockSettings = readSettings();
    const auto mode = unpackMode(blockSettings);
    const auto gain = unpackGain(blockSettings);
    const auto latency = latencyForMode(mode);
    if (activeMode != mode)
    {
        // No buffer clearing on the audio thread. Old samples stay inaccessible
        // until the new delay has primed entirely with samples from this mode.
        activeMode = mode;
        available = 0;
        writeHead = 0;
    }

    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    for (int n = 0; n < audio.getNumSamples(); ++n)
    {
        const auto readHead = (writeHead + capacity - latency) % capacity;
        const auto inputL = left[n];
        const auto inputR = right[n];
        delay[writeHead] = inputL;
        delay[capacity + writeHead] = inputR;
        left[n] = available >= latency ? delay[readHead] * gain : 0.0f;
        right[n] = available >= latency ? delay[capacity + readHead] * gain : 0.0f;
        writeHead = (writeHead + 1) % capacity;
        available = std::min(available + 1, capacity);
    }
}

juce::AudioProcessorEditor* FixtureProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void FixtureProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const auto value = readSettings();
    std::array<std::uint8_t, stateSize> bytes {};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write32(bytes.data() + 8, std::bit_cast<std::uint32_t>(unpackGain(value)));
    write32(bytes.data() + 12, static_cast<std::uint32_t>(unpackMode(value)));
    write32(bytes.data() + 20, checksum(bytes.data()));
    destination.replaceAll(bytes.data(), bytes.size());
}

void FixtureProcessor::setStateInformation(const void* data, int size)
{
    const auto reject = [this] { rejectedStates.fetch_add(1, std::memory_order_relaxed); };
    if (data == nullptr || size != stateSize)
    {
        reject();
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto gain = std::bit_cast<float>(read32(bytes + 8));
    const auto mode = read32(bytes + 12);
    if (std::memcmp(bytes, magic.data(), magic.size()) != 0 || read32(bytes + 16) != 0
        || read32(bytes + 20) != checksum(bytes) || !std::isfinite(gain)
        || gain < 0.0f || gain > 2.0f || mode > 2)
    {
        reject();
        return;
    }
    settings.store(pack(gain, static_cast<int>(mode)), std::memory_order_release);
    publishLatency(static_cast<int>(mode));
    // Report the new controls without calling their setters (which would split
    // the atomic state publication). Editor refresh uses these notifications.
    getParameters()[0]->sendValueChangedMessageToListeners(gain * 0.5f);
    getParameters()[1]->sendValueChangedMessageToListeners(static_cast<float>(mode) * 0.5f);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hem::test::FixtureProcessor();
}

```

## Tests/VST3Fixture/FixtureProcessor.h

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>
#include <memory>

namespace hem::test
{
// Deliberately in-house, deterministic integration fixture. Not a production DSP.
class FixtureProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int oversizedLatency = 262144;
    static constexpr int stateSize = 24;
    static constexpr const char* gainParameterID = "fixtureGain";
    static constexpr const char* latencyParameterID = "fixtureLatency";

    FixtureProcessor();
    ~FixtureProcessor() override = default;

    const juce::String getName() const override { return "HEM VST3 Fixture"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsDoublePrecisionProcessing() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Deterministic fixture"; }
    void changeProgramName(int, const juce::String&) override {}

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Read-only observability for source-level self tests. These are not parameters.
    std::uint32_t rejectedStateCount() const noexcept { return rejectedStates.load(); }
    static int latencyForMode(int) noexcept;

private:
    class Parameter;
    friend class Parameter;
    std::uint64_t readSettings() const noexcept { return settings.load(std::memory_order_acquire); }
    void changeParameter(bool isLatency, float normalized) noexcept;
    static std::uint64_t pack(float gain, int mode) noexcept;
    static float unpackGain(std::uint64_t) noexcept;
    static int unpackMode(std::uint64_t) noexcept;
    void publishLatency(int mode);

    // One word makes gain+mode state restores transactional for the audio thread.
    std::atomic<std::uint64_t> settings;
    std::atomic<std::uint32_t> rejectedStates { 0 };
    std::unique_ptr<float[]> delay;
    int writeHead = 0;
    int available = 0;
    int activeMode = -1;
    static constexpr int capacity = oversizedLatency + 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FixtureProcessor)
};
}

```

## Tests/VST3Fixture/FixtureSelfTests.cpp

```cpp
#include "FixtureProcessor.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool result, const char* what)
{
    if (!result) throw std::runtime_error(what);
}

void set(hem::test::FixtureProcessor& p, int index, float normalized)
{
    p.getParameters()[index]->setValueNotifyingHost(normalized);
}

void putWord(juce::MemoryBlock& state, int offset, std::uint32_t word)
{
    auto* bytes = static_cast<std::uint8_t*>(state.getData());
    for (int n = 0; n < 4; ++n) bytes[offset + n] = static_cast<std::uint8_t>(word >> (8 * n));
}

void repairChecksum(juce::MemoryBlock& state)
{
    const auto* bytes = static_cast<const std::uint8_t*>(state.getData());
    std::uint32_t hash = 2166136261u;
    for (int n = 0; n < 20; ++n) hash = (hash ^ bytes[n]) * 16777619u;
    putWord(state, 20, hash);
}

void checkDelay(hem::test::FixtureProcessor& p, int mode)
{
    set(p, 0, 0.25f); // linear gain = 0.5
    set(p, 1, static_cast<float>(mode) * 0.5f);
    p.reset();
    const auto latency = hem::test::FixtureProcessor::latencyForMode(mode);
    require(p.getLatencySamples() == latency, "reported latency");
    juce::MidiBuffer midi;
    int offset = 0;
    constexpr std::array<int, 6> sizes { 1, 17, 64, 127, 256, 513 };
    for (int block = 0; offset < latency + 4096; ++block)
    {
        const auto count = sizes[static_cast<std::size_t>(block) % sizes.size()];
        juce::AudioBuffer<float> audio(2, count);
        for (int n = 0; n < count; ++n)
        {
            // Exactly representable samples make this an exact equality test.
            audio.setSample(0, n, static_cast<float>((offset + n) % 1024) / 1024.0f);
            audio.setSample(1, n, -audio.getSample(0, n));
        }
        p.processBlock(audio, midi);
        for (int n = 0; n < count; ++n)
        {
            const auto time = offset + n - latency;
            const auto expected = time < 0 ? 0.0f : static_cast<float>(time % 1024) / 2048.0f;
            require(audio.getSample(0, n) == expected, "exact left delayed gain");
            require(audio.getSample(1, n) == -expected, "exact right delayed gain");
        }
        offset += count;
    }
}

void checkState(hem::test::FixtureProcessor& p)
{
    set(p, 0, 0.375f);
    set(p, 1, 0.5f);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    require(state.getSize() == hem::test::FixtureProcessor::stateSize, "state byte count");
    set(p, 0, 0.9f);
    set(p, 1, 1.0f);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(p.getParameters()[0]->getValue() == 0.375f, "state restored gain");
    require(p.getParameters()[1]->getValue() == 0.5f, "state restored latency mode");
    require(p.getLatencySamples() == 64, "state restored latency publication");
    juce::MemoryBlock roundtrip;
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "bit-exact state roundtrip");
    const auto rejected = p.rejectedStateCount();
    for (std::size_t byte = 0; byte < state.getSize(); ++byte)
    {
        auto corrupt = state;
        static_cast<unsigned char*>(corrupt.getData())[byte] ^= 0x01;
        p.setStateInformation(corrupt.getData(), static_cast<int>(corrupt.getSize()));
        p.getStateInformation(roundtrip);
        require(roundtrip == state, "corrupt byte changed state");
    }
    for (int size = 0; size < hem::test::FixtureProcessor::stateSize; ++size)
        p.setStateInformation(state.getData(), size);
    auto trailing = state;
    trailing.append("X", 1);
    p.setStateInformation(trailing.getData(), static_cast<int>(trailing.getSize()));
    p.setStateInformation(nullptr, hem::test::FixtureProcessor::stateSize);
    // Even a syntactically checksummed state must meet semantic constraints.
    for (const auto gain : { -0.1f, 2.5f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity() })
    {
        auto malformed = state;
        putWord(malformed, 8, std::bit_cast<std::uint32_t>(gain));
        repairChecksum(malformed);
        p.setStateInformation(malformed.getData(), static_cast<int>(malformed.getSize()));
    }
    for (const auto offset : { 0, 12, 16 })
    {
        auto malformed = state;
        putWord(malformed, offset, 3);
        repairChecksum(malformed);
        p.setStateInformation(malformed.getData(), static_cast<int>(malformed.getSize()));
    }
    require(p.rejectedStateCount() == rejected + 57, "all malformed states rejected");
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "invalid sizes changed state");
    set(p, 0, std::numeric_limits<float>::quiet_NaN());
    set(p, 1, std::numeric_limits<float>::infinity());
    p.getStateInformation(roundtrip);
    require(roundtrip == state, "non-finite parameter changed state");
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        hem::test::FixtureProcessor p;
        require(p.getTotalNumInputChannels() == 2 && p.getTotalNumOutputChannels() == 2, "stereo layout");
        require(!p.supportsDoublePrecisionProcessing(), "float-only precision");
        require(p.getParameters().size() == 2, "parameter count");
        require(p.getParameters()[0]->getValue() == 0.5f && p.getParameters()[1]->getValue() == 0.0f, "parameter defaults");
        auto layout = p.getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::mono());
        require(!p.isBusesLayoutSupported(layout), "reject mono layout");
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            p.setRateAndBufferSizeDetails(rate, 256);
            p.prepareToPlay(rate, 256);
            for (const auto mode : { 0, 1, 2, 0, 1 }) checkDelay(p, mode);
        }
        checkState(p);
        p.releaseResources();
        juce::AudioBuffer<float> silent(2, 16);
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < 16; ++n) silent.setSample(channel, n, 1.0f);
        juce::MidiBuffer midi;
        p.processBlock(silent, midi);
        require(silent.getMagnitude(0, 16) == 0.0f, "unprepared silence");
        if (argc > 1 && juce::String(argv[1]) == "--editor")
        {
            for (int n = 0; n < 32; ++n)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
                require(editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0, "generic editor dimensions");
                editor->addToDesktop(0);
                editor->setVisible(true);
                editor->setVisible(false);
                editor->removeFromDesktop();
            }
            std::cout << "PASS: 32 native generic-editor create/open/close/delete cycles\n";
        }
        std::cout << "PASS: stereo float, parameter defaults, exact gain/delay at 3 rates and variable blocks,\n"
                     "      0/64/262144 dynamic latency, bit-exact state, 57 corrupt-state rejections, unprepared silence\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

```

## Tests/VocalPipelineTests.cpp

```cpp
#include "../Source/Systems/LiveVocalManager.h"
#include <iostream>
#include <stdexcept>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){juce::ScopedJuceInitialiser_GUI runtime;try{
 auto manager=std::make_unique<hem::LiveVocalManager>();manager->prepare(96000,256,2);
 juce::AudioBuffer<float> hardware(2,256),bus(2,256);for(int i=0;i<256;++i){hardware.setSample(0,i,.25f);hardware.setSample(1,i,-.5f);}
 manager->setMonitor(hem::LiveVocalManager::Monitor::DrySoftware);
 for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,block*256./96000);manager->addMonitor(bus,256);}
 require(std::abs(bus.getSample(0,255)-.25f)<1.e-6,"Dry input mapping");require(manager->rack.latency()==0,"Empty tracking slots added latency");
 manager->setInput(1);for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(std::abs(bus.getSample(0,255)+.5f)<1.e-6,"Input channel switch");
 manager->setMonitor(hem::LiveVocalManager::Monitor::InterfaceDirect);for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(bus.getMagnitude(0,256)==0,"Hardware direct mode duplicated vocal return");
 auto node=manager->rack.makeNode("sipa",96000,256);manager->rack.exchange(0,std::move(node));require(manager->rack.latency()==0,"Zero-latency SIPA added delay");manager->setMonitor(hem::LiveVocalManager::Monitor::InsertsSoftware);
 for(int block=0;block<10;++block){bus.clear();manager->capture(hardware,0,256,0);manager->addMonitor(bus,256);}require(std::abs(bus.getSample(0,255)+.5f)<1.e-5,"Neutral insert path changed vocal");
 auto limiter=manager->rack.makeNode("true-peak",96000,256);manager->rack.exchange(7,std::move(limiter));require(manager->rack.latency()==480,"Lookahead latency not reported");
 require(hardware.getSample(0,0)==.25f&&hardware.getSample(1,0)==-.5f,"Capture overwrote hardware buffer");
 std::cout<<"PASS: input selection, dry capture, interface-direct software mute, natural rack latency and neutral insert routing\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}

```

