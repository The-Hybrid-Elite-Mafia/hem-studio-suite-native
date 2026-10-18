#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
namespace hem {
// Owns devices created through AudioIODeviceType. No manager or source-player
// dispatch. Public lifecycle/settings calls are serialized on the control thread.
// Backend pointer arrays and advertised buffer lengths must be valid. Invalid
// shapes stop rendering: inspect at most 4096 pointers and clear at most two
// non-null outputs x maximumFrames. Invalid excess channels/tails are not cleared.
// The source must outlive this owner and gate graph mutations at render entry.
class NativeAudioDeviceHost final:private juce::AudioIODeviceCallback {
public:
    static constexpr int maximumInputs=128,maximumOutputs=2,maximumFrames=65536;
    struct AudioDeviceSetup {
        juce::String outputDeviceName,inputDeviceName;
        double sampleRate=0;int bufferSize=0;
        juce::BigInteger inputChannels,outputChannels;
        bool useDefaultInputChannels=true,useDefaultOutputChannels=true;
    };
    explicit NativeAudioDeviceHost(juce::AudioSource&,bool createSystemTypes=true);
    ~NativeAudioDeviceHost();
    juce::String initialise(int inputs=0,int outputs=2);
    void closeAudioDevice();
    juce::String restartLastAudioDevice();
    juce::String setAudioDeviceSetup(const AudioDeviceSetup&,bool treatAsChosenDevice=true);
    AudioDeviceSetup getAudioDeviceSetup()const;
    juce::String setCurrentAudioDeviceType(const juce::String&,bool treatAsChosenDevice=true);
    juce::String getCurrentAudioDeviceType()const;
    const juce::OwnedArray<juce::AudioIODeviceType>& getAvailableDeviceTypes()const noexcept;
    juce::AudioIODevice* getCurrentAudioDevice()const noexcept;
    // Control-thread polling handles backend stop/error; never cleanup from IO.
    bool pollDeviceState();
    juce::String getLastError()const;
    uint64_t oversizedCallbacks()const noexcept;
    uint64_t invalidChannelCallbacks()const noexcept;
    // Cumulative backend notifications, unexpected restarts, malformed callbacks,
    // and source render exceptions; survives recovery.
    uint64_t callbackErrors()const noexcept;
    uint64_t silentCallbacks()const noexcept;
private:
    friend struct NativeAudioDeviceHostTestAccess;
    void addDeviceTypeForTesting(std::unique_ptr<juce::AudioIODeviceType>);
    void audioDeviceIOCallbackWithContext(const float* const*,int,float* const*,int,int,const juce::AudioIODeviceCallbackContext&)override;
    void audioDeviceAboutToStart(juce::AudioIODevice*)override;
    void audioDeviceStopped()override;
    void audioDeviceError(const juce::String&)override;
    void recordError(const juce::String&)noexcept;
    struct Impl;std::unique_ptr<Impl> impl;
};
}
