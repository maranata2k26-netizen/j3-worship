#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class DawWorkspace final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer,
                           private juce::KeyListener
{
public:
    static constexpr int kMaxTracks = 32;
    static constexpr int kMaxClips = 128;
    static constexpr int kMaxAutomationPoints = 64;
    static constexpr int kPatternLanes = 8;
    static constexpr int kPatternSteps = 64;
    static constexpr int kMaxMidiNotes = 256;

    DawWorkspace();
    ~DawWorkspace() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void prepare(double sampleRate, int blockSize);
    void releaseResources();
    void processAudio(float* left, float* right, int numSamples) noexcept;

    bool isPlaying() const noexcept { return playing_.load(std::memory_order_acquire); }
    bool isRecordingRequested() const noexcept { return recordRequested_.load(std::memory_order_acquire); }
    void setExternalRecordState(bool recording) noexcept;
    void setThemeColours(juce::Colour background, juce::Colour panel, juce::Colour accent,
                         juce::Colour text, juce::Colour border);

    std::function<void()> onRecordRequested;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct AutomationPoint
    {
        std::atomic<bool> active { false };
        std::atomic<double> beat { 0.0 };
        std::atomic<float> value { 0.0f };
    };

    struct TrackSlot
    {
        juce::String name;
        juce::Colour colour { 0xff2f8cff };
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> mute { false };
        std::atomic<bool> solo { false };
        std::atomic<bool> arm { false };
        std::atomic<bool> monitor { false };
        std::array<AutomationPoint, kMaxAutomationPoints> volumeAutomation;
        std::array<AutomationPoint, kMaxAutomationPoints> panAutomation;
    };

    struct ClipSlot
    {
        std::atomic<bool> active { false };
        std::atomic<int> track { 0 };
        std::atomic<double> startSeconds { 0.0 };
        std::atomic<double> sourceStartSeconds { 0.0 };
        std::atomic<double> lengthSeconds { 0.0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<double> fadeInSeconds { 0.0 };
        std::atomic<double> fadeOutSeconds { 0.0 };
        std::atomic<bool> loop { false };
        std::atomic<bool> mute { false };
        std::atomic<juce::AudioBuffer<float>*> buffer { nullptr };
        double sourceSampleRate { 48000.0 };
        double sourceDurationSeconds { 0.0 };
        juce::File sourceFile;
        juce::String name;
        juce::Colour colour { 0xff2f8cff };
    };

    struct MidiNote
    {
        std::atomic<bool> active { false };
        std::atomic<int> note { 60 };
        std::atomic<double> startBeat { 0.0 };
        std::atomic<double> lengthBeats { 1.0 };
        std::atomic<float> velocity { 0.8f };
    };

    struct DrumVoice
    {
        bool active { false };
        int lane { 0 };
        double phase { 0.0 };
        double env { 0.0 };
        std::uint32_t noise { 0x12345678u };
    };

    class TimelineView final : public juce::Component
    {
    public:
        explicit TimelineView(DawWorkspace& owner);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        int trackFromY(int y) const;
        double secondsFromX(int x) const;
        int xFromSeconds(double seconds) const;
        juce::Rectangle<int> clipBounds(int clipIndex) const;
    private:
        enum class DragMode { None, Move, TrimLeft, TrimRight, Automation };
        DawWorkspace& owner_;
        DragMode dragMode_ { DragMode::None };
        int dragClip_ { -1 };
        double dragOriginalStart_ { 0.0 };
        double dragOriginalSourceStart_ { 0.0 };
        double dragOriginalLength_ { 0.0 };
        int dragOriginalTrack_ { 0 };
        int mouseDownX_ { 0 };
        int mouseDownY_ { 0 };
    };

    class PatternView final : public juce::Component
    {
    public:
        explicit PatternView(DawWorkspace& owner);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
    private:
        DawWorkspace& owner_;
    };

    class PianoRollView final : public juce::Component
    {
    public:
        explicit PianoRollView(DawWorkspace& owner);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
    private:
        DawWorkspace& owner_;
        int draggingNote_ { -1 };
    };

    class PluginEditorHolder final : public juce::Component
    {
    public:
        explicit PluginEditorHolder(std::shared_ptr<juce::AudioPluginInstance> plugin);
        ~PluginEditorHolder() override;
        void resized() override;
    private:
        std::shared_ptr<juce::AudioPluginInstance> plugin_;
        std::unique_ptr<juce::AudioProcessorEditor> editor_;
    };

    void timerCallback() override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    void initialiseUi();
    void initialiseTracks();
    void play();
    void stop();
    void togglePlay();
    void rewind();
    void toggleLoop();
    void requestRecord();

    void importAudioFile(const juce::File&, int targetTrack, double startSeconds);
    int findFreeClip() const noexcept;
    int findClipAt(int x, int y) const;
    void selectClip(int index);
    void deleteSelectedClip();
    void duplicateSelectedClip();
    void splitSelectedClip();
    void copySelectedClip();
    void pasteClip();
    void nudgeFade(bool fadeIn, double amount);
    void toggleSelectedClipLoop();
    void toggleSelectedClipMute();
    double snapSeconds(double seconds) const noexcept;
    double beatToSeconds(double beat) const noexcept;
    double secondsToBeat(double seconds) const noexcept;

    void addTrack();
    void removeSelectedTrack();
    void toggleTrackFlag(int track, int flag);
    void setTrackGainPan(int track, float gain, float pan);
    void addAutomationPoint(int track, bool volume, double beat, float value);
    float evaluateAutomation(const std::array<AutomationPoint, kMaxAutomationPoints>& points,
                             double beat, float fallback) const noexcept;

    void togglePatternStep(int lane, int step);
    bool patternStepEnabled(int lane, int step) const noexcept;
    void triggerDrumVoice(int lane) noexcept;
    float renderDrums(double sampleRate) noexcept;

    void addMidiNote(int note, double startBeat, double lengthBeat, float velocity);
    void deleteMidiNote(int index);
    int noteAt(int x, int y) const;

    void scanInstrumentPlugins();
    void loadSelectedInstrument();
    void loadInstrumentPath(const juce::String& path, const juce::String& savedState = {});
    void openInstrumentEditor();
    void removeInstrument();
    void refreshInstrumentUi();

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    bool saveProjectTo(const juce::File&, bool isAutosave = false);
    bool loadProjectFrom(const juce::File&);
    std::unique_ptr<juce::XmlElement> createProjectXml(bool includePluginState);
    bool loadProjectXml(const juce::XmlElement&, const juce::File& projectFile);
    juce::File projectsRoot() const;
    juce::File autosaveFile() const;
    void pushUndoPoint();
    void undo();
    void redo();
    void markDirty();
    void updateProjectLabels();
    void refreshBrowserRoot();
    void addBrowserSelection();

    void resetRealtimeState() noexcept;
    void mixAudioClips(float* left, float* right, int numSamples,
                       double blockStartSeconds, double sr) noexcept;
    void mixInternalMidi(float* left, float* right, int numSamples,
                         double blockStartSeconds, double sr) noexcept;
    void processInstrument(float* left, float* right, int numSamples,
                           double blockStartSeconds, double sr) noexcept;
    void processPatterns(float* left, float* right, int numSamples,
                         double blockStartSeconds, double sr) noexcept;

    std::array<TrackSlot, kMaxTracks> tracks_;
    std::atomic<int> trackCount_ { 8 };
    std::atomic<int> selectedTrack_ { 0 };
    std::array<ClipSlot, kMaxClips> clips_;
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> ownedAudioBuffers_;
    int selectedClip_ { -1 };
    int copiedClip_ { -1 };

    std::array<std::array<std::atomic<bool>, kPatternSteps>, kPatternLanes> patternSteps_;
    std::atomic<int> patternLength_ { 16 };
    std::array<DrumVoice, 24> drumVoices_;
    int lastPatternStep_ { -1 };

    std::array<MidiNote, kMaxMidiNotes> midiNotes_;

    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<int> blockSize_ { 512 };
    std::atomic<bool> playing_ { false };
    std::atomic<bool> recordRequested_ { false };
    std::atomic<double> playheadSeconds_ { 0.0 };
    std::atomic<double> bpm_ { 120.0 };
    std::atomic<bool> loopEnabled_ { false };
    std::atomic<double> loopStartSeconds_ { 0.0 };
    std::atomic<double> loopEndSeconds_ { 16.0 };
    std::atomic<float> masterGain_ { 0.8f };

    juce::AudioFormatManager formatManager_;
    juce::AudioPluginFormatManager instrumentFormatManager_;
    struct InstrumentEntry { juce::String path; juce::PluginDescription description; };
    std::vector<InstrumentEntry> instrumentEntries_;
    std::vector<std::shared_ptr<juce::AudioPluginInstance>> instrumentOwners_;
    std::atomic<juce::AudioPluginInstance*> instrumentPlugin_ { nullptr };
    juce::String instrumentPath_;
    juce::String instrumentName_;
    juce::String instrumentStateBase64_;
    std::atomic<bool> instrumentBypass_ { false };
    juce::AudioBuffer<float> instrumentScratch_;
    juce::MidiBuffer instrumentMidi_;
    std::array<bool, 128> midiNoteWasOn_ {};

    juce::UndoManager unusedUndoManager_;
    std::vector<juce::String> undoXml_;
    std::vector<juce::String> redoXml_;
    bool restoringHistory_ { false };
    bool dirty_ { false };
    juce::File currentProject_;
    std::uint64_t lastAutosaveMs_ { 0 };

    juce::Colour bg_ { 0xff080d12 };
    juce::Colour panel_ { 0xff0e1820 };
    juce::Colour accent_ { 0xff158cff };
    juce::Colour text_ { 0xffeff6fc };
    juce::Colour border_ { 0xff263a47 };

    juce::TextButton newButton_ { "NUEVO" };
    juce::TextButton openButton_ { "ABRIR" };
    juce::TextButton saveButton_ { "GUARDAR" };
    juce::TextButton saveAsButton_ { "GUARDAR COMO" };
    juce::TextButton addTrackButton_ { "+ PISTA" };
    juce::TextButton removeTrackButton_ { "- PISTA" };
    juce::TextButton splitButton_ { "DIVIDIR" };
    juce::TextButton duplicateButton_ { "DUPLICAR" };
    juce::TextButton deleteButton_ { "BORRAR" };
    juce::TextButton loopClipButton_ { "LOOP CLIP" };
    juce::TextButton muteClipButton_ { "MUTE CLIP" };
    juce::TextButton fadeInButton_ { "FADE IN +" };
    juce::TextButton fadeOutButton_ { "FADE OUT +" };

    juce::TextButton rewindButton_ { "|<<" };
    juce::TextButton stopButton_ { "STOP" };
    juce::TextButton playButton_ { "PLAY" };
    juce::TextButton recordButton_ { "REC" };
    juce::ToggleButton loopButton_ { "LOOP" };
    juce::ToggleButton snapButton_ { "SNAP" };
    juce::Slider bpmSlider_;
    juce::Slider zoomSlider_;
    juce::Slider verticalZoomSlider_;
    juce::Label positionLabel_;
    juce::Label projectLabel_;
    juce::Label hintLabel_;

    juce::TabbedComponent editorTabs_ { juce::TabbedButtonBar::TabsAtTop };
    juce::Component arrangerPage_;
    juce::Component patternPage_;
    juce::Component pianoPage_;
    TimelineView timeline_;
    PatternView patternView_;
    PianoRollView pianoRollView_;

    juce::Viewport timelineViewport_;
    juce::Viewport patternViewport_;
    juce::Viewport pianoViewport_;

    juce::ComboBox patternLengthBox_;
    juce::ComboBox automationModeBox_;
    juce::Label automationLabel_;

    juce::ComboBox instrumentBox_;
    juce::TextButton scanInstrumentButton_ { "SCAN VST3 INSTR." };
    juce::TextButton loadInstrumentButton_ { "CARGAR" };
    juce::TextButton openInstrumentButton_ { "ABRIR UI" };
    juce::TextButton removeInstrumentButton_ { "QUITAR" };
    juce::ToggleButton bypassInstrumentButton_ { "BYPASS" };
    juce::Label instrumentStatus_;

    juce::TimeSliceThread browserThread_ { "J3 Browser" };
    std::unique_ptr<juce::DirectoryContentsList> directoryList_;
    std::unique_ptr<juce::FileTreeComponent> fileTree_;
    juce::ComboBox browserRootBox_;
    juce::TextButton browserAddButton_ { "AGREGAR AL ARRANGER" };

    std::unique_ptr<juce::FileChooser> chooser_;

    float horizontalZoom_ { 120.0f };
    float verticalZoom_ { 1.0f };
    double visibleStartSeconds_ { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawWorkspace)
};
