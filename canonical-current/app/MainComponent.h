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
    static constexpr int kMaxChannels = 48;
    static constexpr int kBuses = 8;
    static constexpr int kDcas = 8;
    static constexpr int kIemMixes = 16;

    class MixerStrip final : public juce::Component
    {
    public:
        MixerStrip(int index, const juce::String& title,
                   std::atomic<float>& gain, std::atomic<float>& pan,
                   std::atomic<bool>& muted, std::atomic<float>& meter,
                   std::atomic<int>& bus, std::atomic<int>& dca);
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
        std::atomic<int>& bus_;
        std::atomic<int>& dca_;
        juce::Label title_;
        juce::Slider fader_;
        juce::Slider panSlider_;
        juce::ToggleButton muteButton_ { "MUTE" };
        juce::ComboBox busBox_;
        juce::ComboBox dcaBox_;
        double meterValue_ { 0.0 };
        juce::ProgressBar meterBar_;
    };

    class GroupStrip final : public juce::Component
    {
    public:
        GroupStrip(const juce::String& title, std::atomic<float>& gain, std::atomic<bool>& muted);
        void paint(juce::Graphics&) override;
        void resized() override;
        void syncFromModel();

    private:
        std::atomic<float>& gain_;
        std::atomic<bool>& muted_;
        juce::Label title_;
        juce::Slider fader_;
        juce::ToggleButton muteButton_ { "MUTE" };
    };

    class IemSendStrip final : public juce::Component
    {
    public:
        IemSendStrip(int sourceIndex, const juce::String& title,
                     std::atomic<float>& gain, std::atomic<float>& pan);
        void paint(juce::Graphics&) override;
        void resized() override;
        void syncFromModel();

    private:
        int sourceIndex_{};
        std::atomic<float>& gain_;
        std::atomic<float>& panValue_;
        juce::Label title_;
        juce::Slider level_;
        juce::Slider panSlider_;
    };

    void configureAudio();
    void preferAsioWhenAvailable(bool onlyIfNoSavedState);
    void scanAvailableDevices();
    void saveAudioState();
    juce::File getAudioStateFile() const;
    void openAudioSettings();
    void startStopRecording();
    void stopRecordingAfterDeviceLoss(const juce::String& reason);
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
    juce::String inputChannelName(int channel) const;
    void setLiveSection(const juce::String& name, j3::SectionKind kind, int bars = 4);
    void refreshLiveLabels();
    void updateClickUi();
    void handleTapTempo();
    bool routeIsSafe(int paLeft, int paRight, int clickOutput) const noexcept;
    bool iemRouteIsSafe(int mix, int left, int right) const noexcept;
    void scheduleReconnect();

    void rebuildMixerBank();
    void setMixerBank(int firstChannel);
    void rebuildIemBank();
    void refreshIemUi();
    void applyIemRoutingFromControls();
    bool anyIemRouted() const noexcept;

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
    std::array<int, j3::kRecordMaxChannels> recordInputIndices_ {};

    std::array<std::atomic<float>, kMaxChannels> channelGain_{};
    std::array<std::atomic<float>, kMaxChannels> channelPan_{};
    std::array<std::atomic<bool>, kMaxChannels> channelMute_{};
    std::array<std::atomic<float>, kMaxChannels> channelMeter_{};
    std::array<std::atomic<int>, kMaxChannels> channelBus_{};
    std::array<std::atomic<int>, kMaxChannels> channelDca_{};
    std::array<j3::ChannelDsp, kMaxChannels> channelDsp_{};

    std::array<std::atomic<float>, kBuses> busGain_{};
    std::array<std::atomic<bool>, kBuses> busMute_{};
    std::array<std::atomic<float>, kDcas> dcaGain_{};
    std::array<std::atomic<bool>, kDcas> dcaMute_{};
    juce::AudioBuffer<float> busScratch_;

    std::array<std::array<std::atomic<float>, kMaxChannels>, kIemMixes> iemSendGain_{};
    std::array<std::array<std::atomic<float>, kMaxChannels>, kIemMixes> iemSendPan_{};
    std::array<std::atomic<float>, kIemMixes> iemMaster_{};
    std::array<std::atomic<bool>, kIemMixes> iemMute_{};
    std::array<std::atomic<int>, kIemMixes> iemOutLeft_{};
    std::array<std::atomic<int>, kIemMixes> iemOutRight_{};

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
    juce::TextButton mixerPrevButton_ { "< 8 CH" };
    juce::TextButton mixerNextButton_ { "8 CH >" };
    juce::Label mixerBankLabel_;
    int mixerBankStart_ { 0 };
    std::array<std::unique_ptr<MixerStrip>, kVisibleChannels> strips_;

    juce::Component groupsPage_;
    juce::Label groupsTitle_;
    std::array<std::unique_ptr<GroupStrip>, kBuses> busStrips_;
    std::array<std::unique_ptr<GroupStrip>, kDcas> dcaStrips_;

    juce::Component iemPage_;
    juce::Label iemTitle_;
    juce::ComboBox iemMixBox_;
    juce::Label iemRouteLabel_;
    juce::ComboBox iemOutLeftBox_;
    juce::ComboBox iemOutRightBox_;
    juce::Slider iemMasterSlider_;
    juce::ToggleButton iemMuteButton_ { "MUTE MIX" };
    juce::TextButton iemPrevButton_ { "< 8 CH" };
    juce::TextButton iemNextButton_ { "8 CH >" };
    juce::Label iemBankLabel_;
    int selectedIemMix_ { 0 };
    int iemBankStart_ { 0 };
    std::array<std::unique_ptr<IemSendStrip>, kVisibleChannels> iemStrips_;

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
