#pragma once

#include <JuceHeader.h>
#include "j3/Dsp.h"
#include "j3/AmbientPad.h"
#include "j3/LiveEngine.h"
#include "j3/ClickGenerator.h"
#include "j3/Recording.h"
#include "j3/Setlist.h"
#include "j3/PluginCatalog.h"
#include "J3Theme.h"
#include "UpdateService.h"
#include "DawWorkspace.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>
#include <utility>
#include <optional>
#include <vector>

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
    void mouseUp(const juce::MouseEvent&) override;

private:
    static constexpr int kVisibleChannels = 8;
    static constexpr int kMaxChannels = 48;
    static constexpr int kBuses = 8;
    static constexpr int kDcas = 8;
    static constexpr int kIemMixes = 16;
    static constexpr int kPluginSlots = 8;

    class MixerStrip final : public juce::Component
    {
    public:
        MixerStrip(int index, const juce::String& title,
                   std::atomic<float>& gain, std::atomic<float>& pan,
                   std::atomic<bool>& muted, std::atomic<float>& meter,
                   std::atomic<int>& bus, std::atomic<int>& dca,
                   std::function<void(int)> onOpenFx);
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
        juce::TextButton fxButton_ { "FX" };
        juce::ComboBox busBox_;
        juce::ComboBox dcaBox_;
        double meterValue_ { 0.0 };
        juce::ProgressBar meterBar_;
        std::array<float, 32> meterHistory_ {};
        std::function<void(int)> onOpenFx_;
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
    void saveAppState(bool capturePluginState = false);
    void updateRecordingUi();
    void openDriverControlPanel();
    void showAudioError(const juce::String&);
    void updateDiagnostics();
    void refreshRoutingControls();
    void applyRoutingFromControls();
    juce::String buildDeviceInventoryText() const;
    juce::String inputChannelName(int channel) const;
    void setLiveSection(const juce::String& name, j3::SectionKind kind, int bars = 4);
    void stopLiveTransport();
    void panicStopAll();
    void refreshLiveLabels();
    void updateClickUi();
    void handleTapTempo();
    void refreshPadUi();
    void refreshSetlistUi();
    void addSetlistSong();
    void removeSetlistSong();
    void loadSelectedSong();
    void scanVst3Plugins();
    void refreshPluginBrowser();
    void refreshPluginUi();
    void loadSelectedPlugin();
    void loadPluginPathIntoSlot(const juce::String& path, int channel, int slot);
    void removeSelectedPlugin();
    void moveSelectedPlugin(int delta);
    void openSelectedPluginEditor();
    void restoreSavedPluginsAfterScan();
    bool pluginMutationLocked() const noexcept;
    const float* processPluginChain(int channel, const float* input, int numSamples) noexcept;
    void processPluginChainStereo(int channel, float* left, float* right, int numSamples) noexcept;
    bool routeIsSafe(int paLeft, int paRight, int clickOutput) const noexcept;
    bool iemRouteIsSafe(int mix, int left, int right) const noexcept;
    void scheduleReconnect();
    void applyTheme(int themeId, bool persist = true);
    void refreshDashboard();
    void checkForUpdatesAsync();
    void showAvailableUpdatePrompt();
    void beginUpdateInstall();

    void rebuildMixerBank();
    void setMixerBank(int firstChannel);
    void applyDspParameters(int channel) noexcept;
    void markDspDirty(int channel) noexcept;
    void refreshDspUi();
    void applyDspPreset(int preset);
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
    j3::AmbientPad ambientPad_;
    std::atomic<bool> padToPa_ { true };
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
    std::array<std::atomic<float>, kMaxChannels> channelHpf_{};
    std::array<std::atomic<float>, kMaxChannels> channelLpf_{};
    std::array<std::atomic<float>, kMaxChannels> channelGate_{};
    std::array<std::atomic<float>, kMaxChannels> channelCompThreshold_{};
    std::array<std::atomic<float>, kMaxChannels> channelCompRatio_{};
    std::array<std::atomic<float>, kMaxChannels> channelDenoise_{};
    std::array<std::atomic<float>, kMaxChannels> channelDenoiseThreshold_{};
    std::array<std::array<std::atomic<float>, 4>, kMaxChannels> channelEqFreq_{};
    std::array<std::array<std::atomic<float>, 4>, kMaxChannels> channelEqGain_{};
    std::array<std::array<std::atomic<float>, 4>, kMaxChannels> channelEqQ_{};
    std::array<std::atomic<std::uint32_t>, kMaxChannels> dspRevision_{};
    std::array<std::uint32_t, kMaxChannels> dspAppliedRevision_{};

    std::array<std::atomic<float>, kBuses> busGain_{};
    std::array<std::atomic<bool>, kBuses> busMute_{};
    std::array<std::atomic<float>, kDcas> dcaGain_{};
    std::array<std::atomic<bool>, kDcas> dcaMute_{};
    juce::AudioBuffer<float> busScratch_;

    j3::PluginCatalog pluginCatalog_;
    juce::AudioPluginFormatManager pluginFormatManager_;
    std::array<std::array<std::atomic<std::shared_ptr<juce::AudioPluginInstance>>, kPluginSlots>, kMaxChannels> channelPlugins_{};
    std::array<std::array<std::atomic<bool>, kPluginSlots>, kMaxChannels> pluginBypass_{};
    std::array<std::array<std::atomic<std::uint32_t>, kPluginSlots>, kMaxChannels> pluginFaults_{};
    std::array<std::array<juce::String, kPluginSlots>, kMaxChannels> pluginPaths_{};
    std::array<std::array<juce::String, kPluginSlots>, kMaxChannels> pluginNames_{};
    std::array<std::array<juce::String, kPluginSlots>, kMaxChannels> pluginStateBase64_{};
    juce::AudioBuffer<float> pluginScratch_;
    juce::AudioBuffer<float> pluginGuardScratch_;
    juce::AudioBuffer<float> dawMixerScratch_;
    juce::MidiBuffer pluginMidiScratch_;
    bool pluginsScanned_ { false };
    std::vector<int> pluginBrowserIndices_;
    juce::StringArray favoritePluginPaths_;
    juce::StringArray recentPluginPaths_;

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
    std::atomic<std::uint64_t> outputSafetyEvents_ { 0 };
    juce::String lastAudioError_;
    juce::String deviceInventory_;

    std::unique_ptr<j3ui::LookAndFeel> lookAndFeel_;
    int themeId_ { 1 };
    std::optional<j3ui::AvailableUpdate> availableUpdate_;
    juce::File downloadedUpdateInstaller_;
    std::atomic<bool> updateBusy_ { false };
    bool updatePromptShown_ { false };
    std::atomic<bool> dawOutputFallbackActive_ { false };
    std::atomic<bool> dawOutputUnavailable_ { false };

    juce::Image brandLogo_;
    juce::Label brandLabel_;
    juce::Label versionLabel_;
    juce::Label statusLabel_;
    juce::Label safetyLabel_;
    juce::ComboBox themeBox_;
    juce::TextButton updateButton_ { "ACTUALIZAR" };
    juce::TextButton audioSettingsButton_ { "AUDIO / MIDI" };
    juce::ToggleButton liveMonitorButton_ { "LIVE INPUTS" };
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    DawWorkspace dawWorkspace_;

    juce::Component livePage_;
    juce::Label nowLabel_;
    juce::Label nextLabel_;
    juce::Label liveHint_;
    std::array<std::unique_ptr<juce::TextButton>, 8> liveButtons_;
    j3::LiveEngine liveEngine_;
    j3::Setlist setlist_ { "Worship Set" };
    bool liveStarted_ { false };

    juce::Component setlistPage_;
    juce::Label setlistTitle_;
    juce::ComboBox setlistSongBox_;
    juce::TextEditor songNameEditor_;
    juce::TextEditor songArtistEditor_;
    juce::TextEditor songKeyEditor_;
    juce::Slider songBpmSlider_;
    juce::TextButton addSongButton_ { "ADD SONG" };
    juce::TextButton removeSongButton_ { "REMOVE" };
    juce::TextButton loadSongButton_ { "LOAD INTO LIVE" };
    juce::Label setlistInfoLabel_;

    juce::Component mixerPage_;
    std::unique_ptr<juce::Component> dashboardLeftCard_;
    std::unique_ptr<juce::Component> dashboardRightCard_;
    std::unique_ptr<juce::Component> dashboardBottomCard_;
    std::unique_ptr<juce::Component> sessionGrid_;
    juce::Label dashboardSetlistTitle_;
    juce::ComboBox dashboardSongBox_;
    juce::TextButton dashboardLoadSongButton_ { "CARGAR EN LIVE" };
    juce::Label dashboardSongInfo_;
    juce::Label dashboardIemTitle_;
    juce::ComboBox dashboardIemMixBox_;
    juce::Slider dashboardIemMasterSlider_;
    juce::Label dashboardRecordTitle_;
    juce::TextButton dashboardRecordButton_ { "GRABAR" };
    juce::Label dashboardRecordInfo_;
    juce::Label dashboardPluginsTitle_;
    juce::Label dashboardPluginsInfo_;
    juce::Label dashboardLiveTitle_;
    juce::TextButton dashboardStopButton_ { "STOP" };
    juce::ToggleButton dashboardPadButton_ { "PAD" };
    juce::ToggleButton dashboardClickButton_ { "CLICK" };
    juce::Label dashboardTempoLabel_;
    std::array<std::unique_ptr<juce::TextButton>, 8> dashboardSectionButtons_;

    juce::TextButton mixerPrevButton_ { "< 8 CH" };
    juce::TextButton mixerNextButton_ { "8 CH >" };
    juce::Label mixerBankLabel_;
    int mixerBankStart_ { 0 };
    std::array<std::unique_ptr<MixerStrip>, kVisibleChannels> strips_;

    juce::Component dspPage_;
    juce::Label dspTitle_;
    juce::ComboBox dspChannelBox_;
    juce::Slider hpfSlider_;
    juce::Slider lpfSlider_;
    juce::Slider gateSlider_;
    juce::Slider compThresholdSlider_;
    juce::Slider compRatioSlider_;
    juce::Slider denoiseSlider_;
    juce::Slider denoiseThresholdSlider_;
    std::array<juce::Slider, 4> eqFreqSliders_;
    std::array<juce::Slider, 4> eqGainSliders_;
    std::array<juce::Label, 4> eqBandLabels_;
    juce::TextButton vocalPresetButton_ { "VOCAL" };
    juce::TextButton kickPresetButton_ { "KICK" };
    juce::TextButton snarePresetButton_ { "SNARE" };
    juce::TextButton guitarPresetButton_ { "GUITAR" };
    juce::TextButton bassPresetButton_ { "BASS" };
    juce::TextButton resetDspButton_ { "RESET" };
    int selectedDspChannel_ { 0 };

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

    juce::Component pluginsPage_;
    juce::Label pluginsTitle_;
    juce::ComboBox pluginChannelBox_;
    juce::ComboBox pluginSlotBox_;
    juce::TextEditor pluginSearch_;
    juce::ComboBox pluginCategoryBox_;
    juce::ComboBox pluginCatalogBox_;
    juce::ToggleButton favoritePluginButton_ { "★ FAVORITE" };
    juce::TextButton scanPluginsButton_ { "SCAN VST3" };
    juce::TextButton loadPluginButton_ { "LOAD INSERT" };
    juce::TextButton removePluginButton_ { "REMOVE" };
    juce::TextButton movePluginUpButton_ { "MOVE UP" };
    juce::TextButton movePluginDownButton_ { "MOVE DOWN" };
    juce::ToggleButton bypassPluginButton_ { "BYPASS" };
    juce::TextButton openPluginEditorButton_ { "OPEN PARAMETERS" };
    juce::Label pluginStatusLabel_;

    juce::Component padPage_;
    juce::Label padTitle_;
    juce::ComboBox padKeyBox_;
    juce::ToggleButton padMinorButton_ { "MINOR" };
    juce::ToggleButton padEnabledButton_ { "PAD ON" };
    juce::ToggleButton padToPaButton_ { "ROUTE TO PA" };
    juce::Slider padVolumeSlider_;
    juce::Label padInfoLabel_;

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
