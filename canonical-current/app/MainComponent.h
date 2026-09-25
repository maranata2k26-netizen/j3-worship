#pragma once

#include <JuceHeader.h>
#include "j3/Dsp.h"
#include "j3/LiveEngine.h"
#include "j3/ClickGenerator.h"
#include "j3/Recording.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kVisibleChannels = 8;
    static constexpr int kMaxChannels = 32;

    class MixerStrip final : public juce::Component
    {
    public:
        MixerStrip(int index, std::atomic<float>& gain, std::atomic<float>& pan,
                   std::atomic<bool>& muted, std::atomic<float>& meter);
        void paint(juce::Graphics&) override;
        void resized() override;
        void timerTick();
        void syncFromModel();

    private:
        int index_{};
        std::atomic<float>& gain_;
        std::atomic<float>& pan_;
        std::atomic<bool>& muted_;
        std::atomic<float>& meter_;
        juce::Label title_;
        juce::Slider fader_;
        juce::Slider panSlider_;
        juce::ToggleButton muteButton_ { "MUTE" };
        double meterValue_ { 0.0 };
        juce::ProgressBar meterBar_;
    };

    void configureAudio();
    void preferAsioWhenAvailable(bool onlyIfNoSavedState);
    void scanAvailableDevices();
    void saveAudioState();
    juce::File getAudioStateFile() const;
    void openAudioSettings();
    void startStopRecording();
    juce::File recordingsRoot() const;
    juce::File getAppStateFile() const;
    juce::File getRuntimeLockFile() const;
    void loadAppState();
    void saveAppState();
    void updateRecordingUi();
    void openDriverControlPanel();
    void showAudioError(const juce::String&);
    void updateDiagnostics();
    void refreshRoutingControls();
    void applyRoutingFromControls();
    juce::String buildDeviceInventoryText() const;
    void setLiveSection(const juce::String& name, j3::SectionKind kind, int bars = 4);
    void refreshLiveLabels();
    void updateClickUi();
    void handleTapTempo();
    bool routeIsSafe(int paLeft, int paRight, int clickOutput) const noexcept;
    void scheduleReconnect();

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    juce::AudioDeviceManager deviceManager_;
    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<int> bufferSize_ { 0 };
    std::atomic<bool> audioRunning_ { false };
    std::atomic<bool> liveMonitorEnabled_ { false };
    std::atomic<bool> transportRunning_ { false };
    std::atomic<bool> clickAudible_ { false };
    std::atomic<int> pendingBeatEvents_ { 0 };
    j3::ClickGenerator clickGenerator_;
    j3::MultiTrackRecorder recorder_;
    std::atomic<bool> recordingEnabled_ { false };
    std::atomic<int> recordChannelCount_ { 0 };
    std::array<std::atomic<float>, kMaxChannels> channelGain_{};
    std::array<std::atomic<float>, kMaxChannels> channelPan_{};
    std::array<std::atomic<bool>, kMaxChannels> channelMute_{};
    std::array<std::atomic<float>, kMaxChannels> channelMeter_{};
    std::array<j3::ChannelDsp, kMaxChannels> channelDsp_{};
    std::atomic<int> paLeft_ { 0 };
    std::atomic<int> paRight_ { 1 };
    std::atomic<int> clickOutput_ { -1 };
    std::atomic<float> masterGain_ { 0.8f };
    std::atomic<bool> shuttingDown_ { false };
    std::uint64_t lastReconnectAttemptMs_ { 0 };
    int reconnectAttempts_ { 0 };
    juce::String lastAudioError_;
    juce::String deviceInventory_;

    juce::Label brandLabel_;
    juce::Label versionLabel_;
    juce::Label statusLabel_;
    juce::TextButton audioSettingsButton_ { "AUDIO / MIDI" };
    juce::ToggleButton liveMonitorButton_ { "LIVE INPUTS" };
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };

    juce::Component livePage_;
    juce::Label nowLabel_;
    juce::Label nextLabel_;
    juce::Label liveHint_;
    std::array<std::unique_ptr<juce::TextButton>, 8> liveButtons_;
    j3::LiveEngine liveEngine_;
    bool liveStarted_ { false };

    juce::Component mixerPage_;
    std::array<std::unique_ptr<MixerStrip>, kVisibleChannels> strips_;

    juce::Component clickPage_;
    juce::Label clickTitle_;
    juce::ToggleButton clickEnabledButton_ { "CLICK ON" };
    juce::Slider bpmSlider_;
    juce::TextButton tapTempoButton_ { "TAP TEMPO" };
    juce::ComboBox timeSignatureBox_;
    juce::ComboBox subdivisionBox_;
    juce::ToggleButton accentButton_ { "ACCENT" };
    juce::Slider clickVolumeSlider_;
    juce::Label clickRouteLabel_;
    double lastTapMs_ { 0.0 };

    juce::Component recordingPage_;
    juce::Label recordingTitle_;
    juce::TextButton recordButton_ { "RECORD SERVICE" };
    juce::Label recordingStatusLabel_;
    juce::TextButton openRecordingsButton_ { "OPEN RECORDINGS FOLDER" };
    bool recoveredAfterUncleanExit_ { false };

    juce::Component setupPage_;
    juce::Label setupTitle_;
    juce::Label setupDeviceLabel_;
    juce::Label routingTitle_;
    juce::Label paLeftLabel_;
    juce::Label paRightLabel_;
    juce::Label clickLabel_;
    juce::ComboBox paLeftBox_;
    juce::ComboBox paRightBox_;
    juce::ComboBox clickBox_;
    juce::TextButton rescanButton_ { "RESCAN DEVICES" };
    juce::TextButton testOutputButton_ { "TEST OUTPUT" };
    juce::TextButton driverPanelButton_ { "DRIVER CONTROL PANEL" };

    juce::Component diagnosticsPage_;
    juce::Label diagnosticsLabel_;
    juce::TextButton runCheckButton_ { "RUN SYSTEM CHECK" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
