#pragma once

#include <JuceHeader.h>
#include "j3/Recording.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class DawWorkspace final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    DawWorkspace();
    ~DawWorkspace() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void prepare(double sampleRate, int maximumBlockSize);
    void renderToMaster(float* left, float* right, int numSamples) noexcept;
    void captureInputBlock(const float* const* inputChannelData, int numInputChannels, int numSamples) noexcept;

    bool isPlaying() const noexcept { return playing_.load(std::memory_order_acquire); }
    double bpm() const noexcept { return bpm_.load(std::memory_order_relaxed); }
    void setTempoFromHost(double bpm);

    std::function<void(double)> onBpmChanged;
    std::function<void(bool)> onPlayStateChanged;
    std::function<void()> onRecordToggle;
    std::function<bool()> isRecording;

private:
    static constexpr int kMaxTracks = 32;
    static constexpr int kMaxClips = 128;
    static constexpr int kRenderBuffers = 3;

    struct ClipAudioData
    {
        juce::AudioBuffer<float> samples;
        double sampleRate { 48000.0 };
        juce::String path;
        double durationSeconds { 0.0 };
    };

    struct Track
    {
        juce::String name;
        juce::Colour colour;
        float gain { 1.0f };
        float pan { 0.0f };
        bool mute { false };
        bool solo { false };
        bool armed { false };
        bool monitor { false };
    };

    struct Clip
    {
        int id { 0 };
        int track { 0 };
        double startBeat { 0.0 };
        double lengthBeats { 4.0 };
        double sourceOffsetSeconds { 0.0 };
        float gain { 1.0f };
        bool muted { false };
        bool loop { false };
        double fadeInBeats { 0.0 };
        double fadeOutBeats { 0.0 };
        juce::Colour colour;
        ClipAudioData* audio { nullptr };
    };

    struct RenderTrack
    {
        float gain { 1.0f };
        float pan { 0.0f };
        bool mute { false };
        bool solo { false };
    };

    struct RenderClip
    {
        const ClipAudioData* audio { nullptr };
        int track { 0 };
        std::int64_t startSample { 0 };
        std::int64_t lengthSamples { 0 };
        double sourceOffsetSeconds { 0.0 };
        float gain { 1.0f };
        bool muted { false };
        bool loop { false };
        std::int64_t fadeInSamples { 0 };
        std::int64_t fadeOutSamples { 0 };
    };

    struct RenderState
    {
        int clipCount { 0 };
        int trackCount { 0 };
        std::array<RenderClip, kMaxClips> clips {};
        std::array<RenderTrack, kMaxTracks> tracks {};
        double bpm { 120.0 };
        double sampleRate { 48000.0 };
        std::int64_t endSample { 0 };
        bool anySolo { false };
    };

    enum class DragMode { none, move, trimLeft, trimRight };

    void timerCallback() override;
    void configureControls();
    void syncInspector();
    void refreshStatus(const juce::String& text);

    juce::Rectangle<int> timelineBounds() const;
    juce::Rectangle<int> rulerBounds() const;
    juce::Rectangle<int> trackHeaderBounds(int track) const;
    juce::Rectangle<float> clipBounds(const Clip& clip) const;
    int trackAtY(int y) const;
    Clip* clipAt(juce::Point<int> point);
    const Clip* clipAt(juce::Point<int> point) const;

    double pixelsPerBeat() const noexcept;
    double beatAtX(float x) const noexcept;
    float xForBeat(double beat) const noexcept;
    double snapBeat(double beat) const noexcept;
    double projectEndBeat() const noexcept;

    void addTrack();
    void importFiles(const juce::StringArray& files, int targetTrack, double startBeat);
    ClipAudioData* loadAudioFile(const juce::File& file, juce::String& error);
    ClipAudioData* audioForPath(const juce::String& path) const;
    void deleteSelectedClip();
    void duplicateSelectedClip();
    void splitSelectedClipAtPlayhead();
    void stopTransport(bool returnToStart);
    void togglePlay();
    void setTransportBeat(double beat) noexcept;
    void toggleTrackRecording();
    bool startTrackRecording();
    void stopTrackRecording(bool importTake);
    juce::File trackRecordingRoot() const;
    void importRecordedTake();

    void checkpointUndo();
    void pushUndoSnapshot(const juce::String& snapshot);
    void undo();
    void redo();
    juce::String serializeProject() const;
    bool restoreProject(const juce::String& xmlText, bool updateProjectFile, const juce::File& sourceFile = {});
    bool saveProject(const juce::File& file);
    void saveProjectInteractive(bool saveAs);
    void openProjectInteractive();
    void newProject();
    juce::File recoveryFile() const;
    void autosaveRecovery();

    void rebuildRenderState();
    void markRenderDirty();

    juce::AudioFormatManager formatManager_;
    std::vector<std::unique_ptr<ClipAudioData>> audioPool_;
    std::array<Track, kMaxTracks> tracks_ {};
    int trackCount_ { 8 };
    std::vector<Clip> clips_;
    int nextClipId_ { 1 };
    int selectedTrack_ { 0 };
    int selectedClipId_ { -1 };

    std::array<RenderState, kRenderBuffers> renderStates_ {};
    std::array<std::atomic<int>, kRenderBuffers> renderReaders_ {};
    std::atomic<int> activeRenderState_ { 0 };
    std::atomic<bool> renderDirty_ { true };
    std::atomic<double> renderSampleRate_ { 48000.0 };

    std::atomic<double> bpm_ { 120.0 };
    std::atomic<bool> playing_ { false };
    std::atomic<bool> loopEnabled_ { false };
    std::atomic<std::int64_t> transportSamples_ { 0 };
    std::atomic<std::int64_t> loopStartSamples_ { 0 };
    std::atomic<std::int64_t> loopEndSamples_ { 0 };

    j3::MultiTrackRecorder trackRecorder_;
    std::atomic<bool> trackRecording_ { false };
    std::array<int, kMaxTracks> recordInputMap_ {};
    std::array<int, kMaxTracks> recordTrackMap_ {};
    int recordArmedCount_ { 0 };
    double recordStartBeat_ { 0.0 };
    juce::File currentTakeDirectory_;
    juce::StringArray currentTakeFiles_;

    double viewStartBeat_ { 0.0 };
    double zoom_ { 1.0 };
    int trackHeight_ { 82 };
    int headerWidth_ { 190 };
    int rulerHeight_ { 30 };
    int toolbarHeight_ { 48 };
    int inspectorHeight_ { 76 };
    double snapBeats_ { 0.25 };

    DragMode dragMode_ { DragMode::none };
    juce::Point<int> dragStartPoint_;
    double dragStartBeat_ { 0.0 };
    double dragStartLength_ { 0.0 };
    double dragStartOffsetSeconds_ { 0.0 };
    int dragStartTrack_ { 0 };
    juce::String dragUndoSnapshot_;
    bool dragChanged_ { false };

    std::vector<juce::String> undoStack_;
    std::vector<juce::String> redoStack_;
    static constexpr int kUndoLimit = 40;

    juce::File projectFile_;
    bool projectDirty_ { false };
    int autosaveTicks_ { 0 };
    bool lastReportedPlaying_ { false };

    juce::TextButton newButton_ { "NUEVO" };
    juce::TextButton openButton_ { "ABRIR" };
    juce::TextButton saveButton_ { "GUARDAR" };
    juce::TextButton importButton_ { "IMPORTAR AUDIO" };
    juce::TextButton addTrackButton_ { "+ PISTA" };
    juce::TextButton playButton_ { "PLAY" };
    juce::TextButton stopButton_ { "STOP" };
    juce::TextButton recordButton_ { "REC" };
    juce::ToggleButton loopButton_ { "LOOP" };
    juce::TextButton splitButton_ { "DIVIDIR" };
    juce::TextButton duplicateButton_ { "DUPLICAR" };
    juce::TextButton deleteButton_ { "BORRAR" };
    juce::Slider bpmSlider_;
    juce::Slider zoomSlider_;
    juce::ComboBox snapBox_;

    juce::TextEditor trackNameEditor_;
    juce::Slider trackVolumeSlider_;
    juce::Slider trackPanSlider_;
    juce::ToggleButton trackMuteButton_ { "M" };
    juce::ToggleButton trackSoloButton_ { "S" };
    juce::ToggleButton trackArmButton_ { "REC" };
    juce::ToggleButton trackMonitorButton_ { "MON" };
    juce::Slider clipGainSlider_;
    juce::ToggleButton clipMuteButton_ { "CLIP MUTE" };
    juce::ToggleButton clipLoopButton_ { "CLIP LOOP" };
    juce::Slider fadeInSlider_;
    juce::Slider fadeOutSlider_;
    juce::Label statusLabel_;

    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawWorkspace)
};
