#include "DawWorkspace.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr std::uint32_t kBg = 0xff081018;
constexpr std::uint32_t kPanel = 0xff0e1a24;
constexpr std::uint32_t kPanel2 = 0xff132532;
constexpr std::uint32_t kBorder = 0xff274150;
constexpr std::uint32_t kText = 0xffedf6ff;
constexpr std::uint32_t kMuted = 0xff8fa5b6;
constexpr std::uint32_t kAccent = 0xff178dff;
constexpr std::uint32_t kGood = 0xff2ed47a;
constexpr std::uint32_t kDanger = 0xffff4d64;

float dbToGain(double db)
{
    return static_cast<float>(juce::Decibels::decibelsToGain(db, -100.0));
}

double gainToDb(float gain)
{
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-7f), -100.0f);
}

bool isAudioPath(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".mp3" || ext == ".flac"
        || ext == ".aif" || ext == ".aiff";
}

juce::Colour trackColour(int index)
{
    static const std::array<std::uint32_t, 12> colours {
        0xff178dff, 0xffef4f9b, 0xff32c982, 0xffffc247,
        0xffa85cff, 0xffff5869, 0xff1ec6d9, 0xffa8d64c,
        0xffff8a3d, 0xff6c8cff, 0xffe66db2, 0xff3db3ff
    };
    return juce::Colour(colours[static_cast<std::size_t>(index) % colours.size()]);
}
}

DawWorkspace::DawWorkspace()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    formatManager_.registerBasicFormats();
    for (auto& r : renderReaders_) r.store(0, std::memory_order_relaxed);

    for (int i = 0; i < kMaxTracks; ++i)
    {
        tracks_[i].name = "Pista " + juce::String(i + 1);
        tracks_[i].colour = trackColour(i);
    }
    tracks_[0].name = "Voz";
    tracks_[1].name = "Batería";
    tracks_[2].name = "Bajo";
    tracks_[3].name = "Guitarra";
    tracks_[4].name = "Teclado";
    tracks_[5].name = "Secuencias";
    tracks_[6].name = "Pads";
    tracks_[7].name = "FX";

    configureControls();
    syncInspector();
    rebuildRenderState();

    const auto recover = recoveryFile();
    if (recover.existsAsFile())
    {
        const auto xml = recover.loadFileAsString();
        if (xml.isNotEmpty() && restoreProject(xml, false))
            refreshStatus("Sesión recuperada automáticamente");
    }

    startTimerHz(30);
}

DawWorkspace::~DawWorkspace()
{
    stopTimer();
    if (trackRecording_.load(std::memory_order_acquire))
        stopTrackRecording(false);
    autosaveRecovery();
}

void DawWorkspace::configureControls()
{
    auto addButton = [this](juce::Button& b)
    {
        addAndMakeVisible(b);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(kPanel2));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    };
    for (auto* b : { &newButton_, &openButton_, &saveButton_, &importButton_, &addTrackButton_,
                     &addMidiTrackButton_, &patternButton_, &playButton_, &stopButton_, &recordButton_,
                     &splitButton_, &duplicateButton_, &deleteButton_ })
        addButton(*b);

    playButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccent).darker(0.2f));
    recordButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kDanger).darker(0.32f));
    addAndMakeVisible(loopButton_);
    loopButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kText));

    newButton_.onClick = [this] { newProject(); };
    openButton_.onClick = [this] { openProjectInteractive(); };
    saveButton_.onClick = [this] { saveProjectInteractive(false); };
    importButton_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Importar audio", juce::File{},
            "*.wav;*.mp3;*.flac;*.aif;*.aiff");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& c)
            {
                juce::StringArray paths;
                for (const auto& f : c.getResults()) paths.add(f.getFullPathName());
                if (!paths.isEmpty())
                    importFiles(paths, selectedTrack_, beatAtX(static_cast<float>(headerWidth_ + 12)));
            });
    };
    addTrackButton_.onClick = [this] { checkpointUndo(); addTrack(); };
    addMidiTrackButton_.onClick = [this] { checkpointUndo(); addMidiTrack(); };
    patternButton_.onClick = [this] { addPattern16(); };
    patternButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccent).darker(0.45f));
    playButton_.onClick = [this] { togglePlay(); };
    stopButton_.onClick = [this] { stopTransport(true); };
    recordButton_.onClick = [this] { toggleTrackRecording(); };
    loopButton_.onClick = [this]
    {
        loopEnabled_.store(loopButton_.getToggleState(), std::memory_order_release);
        const auto start = static_cast<std::int64_t>((viewStartBeat_ * 60.0 / bpm()) * renderSampleRate_.load());
        const auto endBeat = std::max(viewStartBeat_ + 4.0, projectEndBeat());
        const auto end = static_cast<std::int64_t>((endBeat * 60.0 / bpm()) * renderSampleRate_.load());
        loopStartSamples_.store(std::max<std::int64_t>(0, start));
        loopEndSamples_.store(std::max<std::int64_t>(start + 1, end));
    };
    splitButton_.onClick = [this] { splitSelectedClipAtPlayhead(); };
    duplicateButton_.onClick = [this] { duplicateSelectedClip(); };
    deleteButton_.onClick = [this] { deleteSelectedClip(); };

    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 22);
    bpmSlider_.setRange(40.0, 240.0, 0.1);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.onValueChange = [this]
    {
        const auto value = bpmSlider_.getValue();
        bpm_.store(value, std::memory_order_relaxed);
        markRenderDirty();
        if (onBpmChanged) onBpmChanged(value);
        projectDirty_ = true;
    };
    addAndMakeVisible(bpmSlider_);

    zoomSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider_.setRange(0.5, 4.0, 0.01);
    zoomSlider_.setValue(1.0, juce::dontSendNotification);
    zoomSlider_.onValueChange = [this] { zoom_ = zoomSlider_.getValue(); repaint(); };
    addAndMakeVisible(zoomSlider_);

    snapBox_.addItem("SNAP 1 BAR", 1);
    snapBox_.addItem("SNAP 1/2", 2);
    snapBox_.addItem("SNAP 1/4", 3);
    snapBox_.addItem("SNAP 1/8", 4);
    snapBox_.addItem("SNAP OFF", 5);
    snapBox_.setSelectedId(3, juce::dontSendNotification);
    snapBox_.onChange = [this]
    {
        switch (snapBox_.getSelectedId())
        {
            case 1: snapBeats_ = 4.0; break;
            case 2: snapBeats_ = 2.0; break;
            case 3: snapBeats_ = 1.0; break;
            case 4: snapBeats_ = 0.5; break;
            default: snapBeats_ = 0.0; break;
        }
    };
    addAndMakeVisible(snapBox_);

    trackNameEditor_.setSelectAllWhenFocused(true);
    trackNameEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(kPanel2));
    trackNameEditor_.setColour(juce::TextEditor::textColourId, juce::Colour(kText));
    trackNameEditor_.onTextChange = [this]
    {
        if (selectedTrack_ >= 0 && selectedTrack_ < trackCount_)
        {
            tracks_[selectedTrack_].name = trackNameEditor_.getText();
            projectDirty_ = true;
            repaint();
        }
    };
    addAndMakeVisible(trackNameEditor_);

    auto setupDb = [this](juce::Slider& s, double lo, double hi, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 20);
        s.setRange(lo, hi, 0.1);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    };
    setupDb(trackVolumeSlider_, -60.0, 12.0, " dB");
    trackPanSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    trackPanSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    trackPanSlider_.setRange(-1.0, 1.0, 0.01);
    addAndMakeVisible(trackPanSlider_);
    setupDb(clipGainSlider_, -36.0, 18.0, " dB");
    setupDb(fadeInSlider_, 0.0, 16.0, " b");
    setupDb(fadeOutSlider_, 0.0, 16.0, " b");

    trackVolumeSlider_.onValueChange = [this]
    {
        if (selectedTrack_ >= 0 && selectedTrack_ < trackCount_)
        {
            tracks_[selectedTrack_].gain = dbToGain(trackVolumeSlider_.getValue());
            markRenderDirty(); projectDirty_ = true;
        }
    };
    trackPanSlider_.onValueChange = [this]
    {
        if (selectedTrack_ >= 0 && selectedTrack_ < trackCount_)
        {
            tracks_[selectedTrack_].pan = static_cast<float>(trackPanSlider_.getValue());
            markRenderDirty(); projectDirty_ = true;
        }
    };

    for (auto* b : { &trackMuteButton_, &trackSoloButton_, &trackArmButton_,
                     &clipMuteButton_, &clipLoopButton_ })
    {
        addAndMakeVisible(*b);
        b->setColour(juce::ToggleButton::textColourId, juce::Colour(kText));
    }

    trackMuteButton_.onClick = [this]
    {
        tracks_[selectedTrack_].mute = trackMuteButton_.getToggleState(); markRenderDirty(); projectDirty_ = true; repaint();
    };
    trackSoloButton_.onClick = [this]
    {
        tracks_[selectedTrack_].solo = trackSoloButton_.getToggleState(); markRenderDirty(); projectDirty_ = true; repaint();
    };
    trackArmButton_.onClick = [this]
    {
        tracks_[selectedTrack_].armed = trackArmButton_.getToggleState(); projectDirty_ = true; repaint();
    };
    clipGainSlider_.onValueChange = [this]
    {
        if (auto* c = clipAt({ -9999, -9999 }); c != nullptr) juce::ignoreUnused(c);
        for (auto& c : clips_) if (c.id == selectedClipId_)
        {
            c.gain = dbToGain(clipGainSlider_.getValue());
            markRenderDirty(); projectDirty_ = true; repaint(); break;
        }
    };
    clipMuteButton_.onClick = [this]
    {
        for (auto& c : clips_) if (c.id == selectedClipId_)
        {
            c.muted = clipMuteButton_.getToggleState();
            markRenderDirty(); projectDirty_ = true; repaint(); break;
        }
    };
    clipLoopButton_.onClick = [this]
    {
        for (auto& c : clips_) if (c.id == selectedClipId_)
        {
            c.loop = clipLoopButton_.getToggleState();
            markRenderDirty(); projectDirty_ = true; repaint(); break;
        }
    };
    fadeInSlider_.onValueChange = [this]
    {
        for (auto& c : clips_) if (c.id == selectedClipId_)
        {
            c.fadeInBeats = juce::jlimit(0.0, c.lengthBeats, fadeInSlider_.getValue());
            markRenderDirty(); projectDirty_ = true; repaint(); break;
        }
    };
    fadeOutSlider_.onValueChange = [this]
    {
        for (auto& c : clips_) if (c.id == selectedClipId_)
        {
            c.fadeOutBeats = juce::jlimit(0.0, c.lengthBeats, fadeOutSlider_.getValue());
            markRenderDirty(); projectDirty_ = true; repaint(); break;
        }
    };

    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel_);
}

void DawWorkspace::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));

    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop(toolbarHeight_);
    g.setColour(juce::Colour(0xff0a151e));
    g.fillRect(toolbar);
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(toolbar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    auto inspector = bounds.removeFromBottom(inspectorHeight_);
    g.setColour(juce::Colour(0xff0b1720));
    g.fillRect(inspector);
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(inspector.getY(), 0.0f, static_cast<float>(getWidth()));

    const auto tl = timelineBounds();
    const auto ruler = rulerBounds();
    g.setColour(juce::Colour(0xff09131b));
    g.fillRect(tl);
    g.setColour(juce::Colour(0xff101e28));
    g.fillRect(ruler);

    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    const double ppb = pixelsPerBeat();
    const int firstBeat = std::max(0, static_cast<int>(std::floor(viewStartBeat_)));
    const int lastBeat = static_cast<int>(std::ceil(viewStartBeat_ + (tl.getWidth() - headerWidth_) / ppb)) + 1;
    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const float x = xForBeat(static_cast<double>(beat));
        if (x < headerWidth_ || x > getWidth()) continue;
        const bool bar = beat % 4 == 0;
        g.setColour(juce::Colour(bar ? 0xff365064 : 0xff1c2e3a));
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(ruler.getY()), static_cast<float>(tl.getBottom()));
        if (bar)
        {
            g.setColour(juce::Colour(kMuted));
            g.drawText(juce::String(beat / 4 + 1), static_cast<int>(x) + 4, ruler.getY(), 42, ruler.getHeight(),
                       juce::Justification::centredLeft);
        }
    }

    g.setColour(juce::Colour(0xff0b1720));
    g.fillRect(tl.withWidth(headerWidth_));
    g.setColour(juce::Colour(kBorder));
    g.drawVerticalLine(headerWidth_ - 1, static_cast<float>(tl.getY()), static_cast<float>(tl.getBottom()));

    for (int t = 0; t < trackCount_; ++t)
    {
        auto header = trackHeaderBounds(t);
        auto row = header.withX(headerWidth_).withWidth(std::max(0, getWidth() - headerWidth_));
        const bool selected = t == selectedTrack_;
        g.setColour(juce::Colour(selected ? 0xff142a38 : (t % 2 == 0 ? 0xff0b151e : 0xff0d1821)));
        g.fillRect(row);
        g.setColour(juce::Colour(kBorder).withAlpha(0.55f));
        g.drawHorizontalLine(row.getBottom() - 1, static_cast<float>(headerWidth_), static_cast<float>(getWidth()));

        g.setColour(juce::Colour(selected ? 0xff172a37 : 0xff101d27));
        g.fillRect(header);
        g.setColour(tracks_[t].colour);
        g.fillRect(header.removeFromLeft(5));
        g.setColour(juce::Colour(kText));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(juce::String(t + 1) + "  " + tracks_[t].name, header.reduced(9, 7).removeFromTop(23),
                   juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(10.5f));
        g.setColour(juce::Colour(kMuted));
        juce::String flags;
        if (tracks_[t].mute) flags << "M ";
        if (tracks_[t].solo) flags << "S ";
        if (tracks_[t].armed) flags << "REC ";
        if (tracks_[t].midi) flags << "MIDI · PIANO ROLL ";
        flags << juce::String(gainToDb(tracks_[t].gain), 1) << " dB";
        g.drawText(flags, header.reduced(9, 7).withTrimmedTop(25), juce::Justification::centredLeft);
    }

    for (const auto& clip : clips_)
    {
        auto cb = clipBounds(clip);
        if (!cb.intersects(tl.toFloat())) continue;
        const bool selected = clip.id == selectedClipId_;
        auto colour = clip.colour;
        if (clip.muted) colour = colour.withSaturation(0.15f).withBrightness(0.45f);
        g.setColour(colour.withAlpha(selected ? 0.88f : 0.68f));
        g.fillRoundedRectangle(cb, 5.0f);
        g.setColour(selected ? juce::Colours::white : colour.brighter(0.25f));
        g.drawRoundedRectangle(cb, 5.0f, selected ? 2.0f : 1.0f);

        auto textArea = cb.reduced(6.0f, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::File(clip.audio != nullptr ? clip.audio->path : juce::String()).getFileNameWithoutExtension(),
                   textArea.removeFromTop(17.0f), juce::Justification::centredLeft);

        if (clip.audio != nullptr && clip.audio->samples.getNumSamples() > 0)
        {
            auto wave = cb.reduced(5.0f, 20.0f);
            wave.setBottom(cb.getBottom() - 5.0f);
            if (wave.getHeight() > 6.0f && wave.getWidth() > 2.0f)
            {
                const auto& audio = clip.audio->samples;
                const int sourceSamples = audio.getNumSamples();
                const double sourceStart = clip.sourceOffsetSeconds * clip.audio->sampleRate;
                const double clipSeconds = clip.lengthBeats * 60.0 / std::max(1.0, bpm());
                const double wantedSource = clipSeconds * clip.audio->sampleRate;
                const int columns = std::max(1, static_cast<int>(wave.getWidth()));
                g.setColour(juce::Colours::white.withAlpha(0.72f));
                for (int px = 0; px < columns; ++px)
                {
                    double u0 = static_cast<double>(px) / columns;
                    double u1 = static_cast<double>(px + 1) / columns;
                    auto sampleFor = [&](double u)
                    {
                        double raw = sourceStart + u * wantedSource;
                        if (clip.loop && sourceSamples > 0)
                            raw = std::fmod(std::max(0.0, raw), static_cast<double>(sourceSamples));
                        return juce::jlimit(0, std::max(0, sourceSamples - 1), static_cast<int>(raw));
                    };
                    int s0 = sampleFor(u0);
                    int s1 = sampleFor(u1);
                    if (s1 < s0 && !clip.loop) std::swap(s0, s1);
                    s1 = std::max(s0 + 1, s1);
                    s1 = std::min(s1, sourceSamples);
                    float peak = 0.0f;
                    const int stride = std::max(1, (s1 - s0) / 12);
                    for (int s = s0; s < s1; s += stride)
                    {
                        float v = 0.0f;
                        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                            v = std::max(v, std::abs(audio.getSample(ch, s)));
                        peak = std::max(peak, v);
                    }
                    const float h = peak * wave.getHeight() * 0.46f;
                    const float x = wave.getX() + static_cast<float>(px);
                    g.drawVerticalLine(static_cast<int>(x), wave.getCentreY() - h, wave.getCentreY() + h);
                }
            }
        }

        if (clip.fadeInBeats > 0.0 || clip.fadeOutBeats > 0.0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            juce::Path env;
            const float left = cb.getX();
            const float right = cb.getRight();
            const float top = cb.getY() + 22.0f;
            const float bottom = cb.getBottom() - 4.0f;
            const float fi = static_cast<float>(clip.fadeInBeats / std::max(0.001, clip.lengthBeats)) * cb.getWidth();
            const float fo = static_cast<float>(clip.fadeOutBeats / std::max(0.001, clip.lengthBeats)) * cb.getWidth();
            env.startNewSubPath(left, clip.fadeInBeats > 0.0 ? bottom : top);
            env.lineTo(left + fi, top);
            env.lineTo(right - fo, top);
            env.lineTo(right, clip.fadeOutBeats > 0.0 ? bottom : top);
            g.strokePath(env, juce::PathStrokeType(1.0f));
        }
    }

    for (const auto& note : midiNotes_)
    {
        if (note.track < 0 || note.track >= trackCount_ || !tracks_[note.track].midi)
            continue;
        const auto nb = midiNoteBounds(note);
        if (!nb.intersects(tl.toFloat()))
            continue;

        const bool selected = note.id == selectedMidiNoteId_;
        auto colour = tracks_[note.track].colour.brighter(0.12f);
        g.setColour(colour.withAlpha(selected ? 0.98f : 0.78f));
        g.fillRoundedRectangle(nb, 2.5f);
        g.setColour(selected ? juce::Colours::white : colour.brighter(0.35f));
        g.drawRoundedRectangle(nb, 2.5f, selected ? 1.8f : 0.8f);

        if (nb.getWidth() > 34.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.drawText(juce::MidiMessage::getMidiNoteName(note.note, true, true, 3),
                       nb.reduced(4.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    const double posBeat = (static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed))) * bpm() / 60.0;
    const float playX = xForBeat(posBeat);
    if (playX >= headerWidth_ && playX <= getWidth())
    {
        g.setColour(juce::Colour(kAccent));
        g.drawVerticalLine(static_cast<int>(playX), static_cast<float>(ruler.getY()), static_cast<float>(tl.getBottom()));
        juce::Path marker;
        marker.addTriangle(playX - 6.0f, static_cast<float>(ruler.getY()),
                           playX + 6.0f, static_cast<float>(ruler.getY()),
                           playX, static_cast<float>(ruler.getY() + 8));
        g.fillPath(marker);
    }

    g.setColour(juce::Colour(kMuted));
    g.setFont(juce::FontOptions(10.5f));
    g.drawText("ARRANGER · audio + MIDI/piano roll · doble clic en pista MIDI agrega nota · PATTERN 16 · Space Play/Stop · Ctrl+S · Ctrl+Z/Y · Delete",
               8, getHeight() - 18, getWidth() - 16, 15, juce::Justification::centredLeft);
}

void DawWorkspace::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop(toolbarHeight_).reduced(6, 7);

    auto take = [&](juce::Component& c, int width)
    {
        c.setBounds(toolbar.removeFromLeft(width));
        toolbar.removeFromLeft(4);
    };
    take(newButton_, 58);
    take(openButton_, 58);
    take(saveButton_, 68);
    take(importButton_, 106);
    take(addTrackButton_, 66);
    take(addMidiTrackButton_, 62);
    take(patternButton_, 82);
    toolbar.removeFromLeft(6);
    take(stopButton_, 52);
    take(playButton_, 58);
    take(recordButton_, 46);
    take(loopButton_, 58);
    take(splitButton_, 64);
    take(duplicateButton_, 72);
    take(deleteButton_, 58);
    toolbar.removeFromLeft(5);
    take(bpmSlider_, 136);
    take(snapBox_, 102);
    take(zoomSlider_, std::max(0, toolbar.getWidth()));

    auto inspector = r.removeFromBottom(inspectorHeight_).reduced(8, 7);
    auto top = inspector.removeFromTop(30);
    trackNameEditor_.setBounds(top.removeFromLeft(155));
    top.removeFromLeft(6);
    trackMuteButton_.setBounds(top.removeFromLeft(36));
    trackSoloButton_.setBounds(top.removeFromLeft(36));
    trackArmButton_.setBounds(top.removeFromLeft(46));
    top.removeFromLeft(8);
    trackVolumeSlider_.setBounds(top.removeFromLeft(160));
    trackPanSlider_.setBounds(top.removeFromLeft(135));
    top.removeFromLeft(8);
    clipMuteButton_.setBounds(top.removeFromLeft(82));
    clipLoopButton_.setBounds(top.removeFromLeft(82));
    statusLabel_.setBounds(top);

    auto bottom = inspector.removeFromTop(30);
    clipGainSlider_.setBounds(bottom.removeFromLeft(180));
    bottom.removeFromLeft(6);
    fadeInSlider_.setBounds(bottom.removeFromLeft(170));
    fadeOutSlider_.setBounds(bottom.removeFromLeft(170));

    repaint();
}

juce::Rectangle<int> DawWorkspace::timelineBounds() const
{
    return { 0, toolbarHeight_ + rulerHeight_, getWidth(),
             std::max(0, getHeight() - toolbarHeight_ - rulerHeight_ - inspectorHeight_) };
}

juce::Rectangle<int> DawWorkspace::rulerBounds() const
{
    return { 0, toolbarHeight_, getWidth(), rulerHeight_ };
}

juce::Rectangle<int> DawWorkspace::trackHeaderBounds(int track) const
{
    const auto tl = timelineBounds();
    return { 0, tl.getY() + (track - firstVisibleTrack_) * trackHeight_, headerWidth_, trackHeight_ };
}

juce::Rectangle<float> DawWorkspace::clipBounds(const Clip& clip) const
{
    const auto tl = timelineBounds();
    const float x = xForBeat(clip.startBeat);
    const float w = std::max(8.0f, static_cast<float>(clip.lengthBeats * pixelsPerBeat()));
    const float y = static_cast<float>(tl.getY() + (clip.track - firstVisibleTrack_) * trackHeight_ + 7);
    return { x, y, w, static_cast<float>(trackHeight_ - 14) };
}

juce::Rectangle<float> DawWorkspace::midiNoteBounds(const MidiNote& note) const
{
    const auto tl = timelineBounds();
    const float x = xForBeat(note.startBeat);
    const float w = std::max(7.0f, static_cast<float>(note.lengthBeats * pixelsPerBeat()));
    const int lo = 36;
    const int hi = 84;
    const float laneTop = static_cast<float>(tl.getY() + (note.track - firstVisibleTrack_) * trackHeight_ + 5);
    const float laneHeight = static_cast<float>(trackHeight_ - 10);
    const float normalized = static_cast<float>(juce::jlimit(lo, hi, note.note) - lo)
        / static_cast<float>(hi - lo);
    const float y = laneTop + (1.0f - normalized) * (laneHeight - 6.0f);
    return { x, y, w, 6.0f };
}

DawWorkspace::MidiNote* DawWorkspace::midiNoteAt(juce::Point<int> point)
{
    for (auto it = midiNotes_.rbegin(); it != midiNotes_.rend(); ++it)
        if (midiNoteBounds(*it).expanded(1.0f, 2.0f).contains(point.toFloat()))
            return &*it;
    return nullptr;
}

const DawWorkspace::MidiNote* DawWorkspace::midiNoteAt(juce::Point<int> point) const
{
    for (auto it = midiNotes_.rbegin(); it != midiNotes_.rend(); ++it)
        if (midiNoteBounds(*it).expanded(1.0f, 2.0f).contains(point.toFloat()))
            return &*it;
    return nullptr;
}

int DawWorkspace::midiPitchAtY(int track, int y) const noexcept
{
    const auto tl = timelineBounds();
    const int lo = 36;
    const int hi = 84;
    const int top = tl.getY() + (track - firstVisibleTrack_) * trackHeight_ + 5;
    const int height = std::max(1, trackHeight_ - 10);
    const float normalized = 1.0f - juce::jlimit(0.0f, 1.0f,
        static_cast<float>(y - top) / static_cast<float>(height));
    return juce::jlimit(lo, hi, static_cast<int>(std::lround(lo + normalized * (hi - lo))));
}

int DawWorkspace::trackAtY(int y) const
{
    const auto tl = timelineBounds();
    if (y < tl.getY() || y >= tl.getBottom()) return -1;
    const int row = (y - tl.getY()) / trackHeight_;
    const int track = firstVisibleTrack_ + row;
    return track >= 0 && track < trackCount_ ? track : -1;
}

DawWorkspace::Clip* DawWorkspace::clipAt(juce::Point<int> point)
{
    if (point.x < 0)
    {
        for (auto& c : clips_) if (c.id == selectedClipId_) return &c;
        return nullptr;
    }
    for (auto it = clips_.rbegin(); it != clips_.rend(); ++it)
        if (clipBounds(*it).contains(point.toFloat()))
            return &*it;
    return nullptr;
}

const DawWorkspace::Clip* DawWorkspace::clipAt(juce::Point<int> point) const
{
    if (point.x < 0)
    {
        for (const auto& c : clips_) if (c.id == selectedClipId_) return &c;
        return nullptr;
    }
    for (auto it = clips_.rbegin(); it != clips_.rend(); ++it)
        if (clipBounds(*it).contains(point.toFloat()))
            return &*it;
    return nullptr;
}

double DawWorkspace::pixelsPerBeat() const noexcept
{
    return 44.0 * zoom_;
}

double DawWorkspace::beatAtX(float x) const noexcept
{
    return std::max(0.0, viewStartBeat_ + (static_cast<double>(x) - headerWidth_) / pixelsPerBeat());
}

float DawWorkspace::xForBeat(double beat) const noexcept
{
    return static_cast<float>(headerWidth_ + (beat - viewStartBeat_) * pixelsPerBeat());
}

double DawWorkspace::snapBeat(double beat) const noexcept
{
    if (snapBeats_ <= 0.0) return std::max(0.0, beat);
    return std::max(0.0, std::round(beat / snapBeats_) * snapBeats_);
}

double DawWorkspace::projectEndBeat() const noexcept
{
    double end = 16.0;
    for (const auto& c : clips_) end = std::max(end, c.startBeat + c.lengthBeats);
    for (const auto& n : midiNotes_) end = std::max(end, n.startBeat + n.lengthBeats);
    return end;
}

void DawWorkspace::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.y < toolbarHeight_) return;

    if (e.y >= timelineBounds().getY() && e.x < headerWidth_)
    {
        const int t = trackAtY(e.y);
        if (t >= 0)
        {
            selectedTrack_ = t;
            syncInspector();
            repaint();
        }
        return;
    }

    if (e.y >= rulerBounds().getY() && e.y < rulerBounds().getBottom() && e.x >= headerWidth_)
    {
        setTransportBeat(snapBeat(beatAtX(static_cast<float>(e.x))));
        repaint();
        return;
    }

    if (auto* n = midiNoteAt(e.getPosition()))
    {
        selectedMidiNoteId_ = n->id;
        selectedClipId_ = -1;
        selectedTrack_ = n->track;
        dragStartPoint_ = e.getPosition();
        dragStartBeat_ = n->startBeat;
        dragStartLength_ = n->lengthBeats;
        dragStartTrack_ = n->track;
        dragStartMidiPitch_ = n->note;
        dragUndoSnapshot_ = serializeProject();
        dragChanged_ = false;
        const auto nb = midiNoteBounds(*n);
        dragMode_ = std::abs(static_cast<float>(e.x) - nb.getRight()) <= 6.0f
            ? DragMode::midiResize : DragMode::midiMove;
        syncInspector();
        repaint();
        return;
    }

    if (auto* c = clipAt(e.getPosition()))
    {
        selectedMidiNoteId_ = -1;
        selectedClipId_ = c->id;
        selectedTrack_ = c->track;
        dragStartPoint_ = e.getPosition();
        dragStartBeat_ = c->startBeat;
        dragStartLength_ = c->lengthBeats;
        dragStartOffsetSeconds_ = c->sourceOffsetSeconds;
        dragStartTrack_ = c->track;
        dragUndoSnapshot_ = serializeProject();
        dragChanged_ = false;

        const auto cb = clipBounds(*c);
        if (std::abs(static_cast<float>(e.x) - cb.getX()) <= 7.0f)
            dragMode_ = DragMode::trimLeft;
        else if (std::abs(static_cast<float>(e.x) - cb.getRight()) <= 7.0f)
            dragMode_ = DragMode::trimRight;
        else
            dragMode_ = DragMode::move;
        syncInspector();
        repaint();
        return;
    }

    selectedClipId_ = -1;
    selectedMidiNoteId_ = -1;
    const int t = trackAtY(e.y);
    if (t >= 0) selectedTrack_ = t;
    syncInspector();
    repaint();
}

void DawWorkspace::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode_ == DragMode::none) return;
    const double deltaBeat = static_cast<double>(e.x - dragStartPoint_.x) / pixelsPerBeat();

    if (dragMode_ == DragMode::midiMove || dragMode_ == DragMode::midiResize)
    {
        for (auto& n : midiNotes_)
        {
            if (n.id != selectedMidiNoteId_) continue;
            if (dragMode_ == DragMode::midiMove)
            {
                n.startBeat = snapBeat(dragStartBeat_ + deltaBeat);
                const int newTrack = trackAtY(e.y);
                if (newTrack >= 0 && tracks_[newTrack].midi)
                    n.track = newTrack;
                n.note = midiPitchAtY(n.track, e.y);
            }
            else
            {
                const double raw = dragStartLength_ + deltaBeat;
                const double snapped = snapBeats_ > 0.0 ? snapBeat(raw) : raw;
                n.lengthBeats = std::max(0.125, snapped);
            }
            break;
        }
    }
    else
    {
        auto* c = clipAt({ -1, -1 });
        if (c == nullptr) return;
        const int newTrack = trackAtY(e.y);
        if (dragMode_ == DragMode::move)
        {
            c->startBeat = snapBeat(dragStartBeat_ + deltaBeat);
            if (newTrack >= 0 && !tracks_[newTrack].midi) c->track = newTrack;
        }
        else if (dragMode_ == DragMode::trimRight)
        {
            c->lengthBeats = std::max(snapBeats_ > 0.0 ? snapBeats_ : 0.05,
                                      snapBeat(dragStartLength_ + deltaBeat));
        }
        else if (dragMode_ == DragMode::trimLeft)
        {
            const double oldEnd = dragStartBeat_ + dragStartLength_;
            double newStart = snapBeat(dragStartBeat_ + deltaBeat);
            newStart = juce::jlimit(0.0, oldEnd - 0.05, newStart);
            const double shiftedBeats = newStart - dragStartBeat_;
            c->startBeat = newStart;
            c->lengthBeats = oldEnd - newStart;
            c->sourceOffsetSeconds = std::max(0.0, dragStartOffsetSeconds_ + shiftedBeats * 60.0 / bpm());
        }
    }

    dragChanged_ = true;
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
}

void DawWorkspace::mouseUp(const juce::MouseEvent&)
{
    if (dragChanged_ && dragUndoSnapshot_.isNotEmpty())
        pushUndoSnapshot(dragUndoSnapshot_);
    dragMode_ = DragMode::none;
    dragUndoSnapshot_.clear();
    dragChanged_ = false;
}

void DawWorkspace::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.x >= headerWidth_ && e.y >= timelineBounds().getY())
    {
        const int track = trackAtY(e.y);
        if (track >= 0)
        {
            const double beat = snapBeat(beatAtX(static_cast<float>(e.x)));
            if (tracks_[track].midi)
            {
                checkpointUndo();
                addMidiNote(track, beat, midiPitchAtY(track, e.y),
                            snapBeats_ > 0.0 ? std::max(0.25, snapBeats_) : 1.0);
                return;
            }

            chooser_ = std::make_unique<juce::FileChooser>(
                "Importar audio", juce::File{}, "*.wav;*.mp3;*.flac;*.aif;*.aiff");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                [this, track, beat](const juce::FileChooser& c)
                {
                    juce::StringArray paths;
                    for (const auto& f : c.getResults()) paths.add(f.getFullPathName());
                    if (!paths.isEmpty()) importFiles(paths, track, beat);
                });
        }
    }
}

void DawWorkspace::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        zoom_ = juce::jlimit(0.5, 4.0, zoom_ + wheel.deltaY * 0.35);
        zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    }
    else if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const float amount = std::abs(wheel.deltaX) > 0.001f ? wheel.deltaX : wheel.deltaY;
        viewStartBeat_ = std::max(0.0, viewStartBeat_ - amount * 8.0 / zoom_);
    }
    else
    {
        const auto tl = timelineBounds();
        const int visible = std::max(1, tl.getHeight() / std::max(1, trackHeight_));
        const int maxFirst = std::max(0, trackCount_ - visible);
        const int delta = wheel.deltaY > 0.0f ? -2 : (wheel.deltaY < 0.0f ? 2 : 0);
        firstVisibleTrack_ = juce::jlimit(0, maxFirst, firstVisibleTrack_ + delta);
    }
    repaint();
}

bool DawWorkspace::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }
    if (mods.isCommandDown() && code == 'S')
    {
        saveProjectInteractive(mods.isShiftDown()); return true;
    }
    if (mods.isCommandDown() && code == 'D') { duplicateSelectedClip(); return true; }
    if (mods.isCommandDown() && code == 'Z' && !mods.isShiftDown()) { undo(); return true; }
    if ((mods.isCommandDown() && code == 'Y') || (mods.isCommandDown() && mods.isShiftDown() && code == 'Z')) { redo(); return true; }
    return false;
}

bool DawWorkspace::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (isAudioPath(f)) return true;
    return false;
}

void DawWorkspace::filesDropped(const juce::StringArray& files, int x, int y)
{
    int track = trackAtY(y);
    if (track < 0) track = selectedTrack_;
    importFiles(files, track, snapBeat(beatAtX(static_cast<float>(x))));
}

void DawWorkspace::addTrack()
{
    if (trackCount_ >= kMaxTracks)
    {
        refreshStatus("Máximo de 48 pistas alcanzado");
        return;
    }
    const int i = trackCount_++;
    tracks_[i] = {};
    tracks_[i].name = "Pista " + juce::String(i + 1);
    tracks_[i].colour = trackColour(i);
    selectedTrack_ = i;
    const int visible = std::max(1, timelineBounds().getHeight() / std::max(1, trackHeight_));
    firstVisibleTrack_ = std::max(0, i - visible + 1);
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
}

void DawWorkspace::addMidiTrack()
{
    if (trackCount_ >= kMaxTracks)
    {
        refreshStatus("Máximo de 48 pistas alcanzado");
        return;
    }
    const int i = trackCount_++;
    tracks_[i] = {};
    tracks_[i].midi = true;
    tracks_[i].name = "MIDI " + juce::String(i + 1);
    tracks_[i].colour = trackColour(i);
    selectedTrack_ = i;
    selectedClipId_ = -1;
    selectedMidiNoteId_ = -1;
    const int visible = std::max(1, timelineBounds().getHeight() / std::max(1, trackHeight_));
    firstVisibleTrack_ = std::max(0, i - visible + 1);
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus("Pista MIDI creada · doble clic para dibujar notas");
}

void DawWorkspace::addMidiNote(int track, double startBeat, int note, double lengthBeats, float velocity)
{
    if (static_cast<int>(midiNotes_.size()) >= kMaxMidiNotes || track < 0 || track >= trackCount_)
        return;
    MidiNote n;
    n.id = nextMidiNoteId_++;
    n.track = track;
    n.startBeat = std::max(0.0, startBeat);
    n.lengthBeats = std::max(0.125, lengthBeats);
    n.note = juce::jlimit(0, 127, note);
    n.velocity = juce::jlimit(0.01f, 1.0f, velocity);
    midiNotes_.push_back(n);
    selectedTrack_ = track;
    selectedClipId_ = -1;
    selectedMidiNoteId_ = n.id;
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
}

void DawWorkspace::addPattern16()
{
    if (selectedTrack_ < 0 || selectedTrack_ >= trackCount_ || !tracks_[selectedTrack_].midi)
    {
        checkpointUndo();
        addMidiTrack();
    }
    if (selectedTrack_ < 0 || selectedTrack_ >= trackCount_ || !tracks_[selectedTrack_].midi)
        return;

    checkpointUndo();
    const double start = snapBeat((static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed))) * bpm() / 60.0);
    constexpr std::array<int, 16> pattern { 60, 64, 67, 72, 67, 64, 60, 67,
                                            60, 64, 67, 74, 72, 67, 64, 67 };
    for (int step = 0; step < 16; ++step)
        addMidiNote(selectedTrack_, start + step * 0.25, pattern[static_cast<std::size_t>(step)],
                    0.22, step % 4 == 0 ? 0.92f : 0.70f);
    refreshStatus("Pattern MIDI de 16 pasos creado · editable en el piano roll");
}

DawWorkspace::ClipAudioData* DawWorkspace::audioForPath(const juce::String& path) const
{
    for (const auto& item : audioPool_)
        if (item != nullptr && item->path == path)
            return item.get();
    return nullptr;
}

DawWorkspace::ClipAudioData* DawWorkspace::loadAudioFile(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "Archivo no encontrado: " + file.getFileName();
        return nullptr;
    }
    if (auto* existing = audioForPath(file.getFullPathName()))
        return existing;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Formato no compatible: " + file.getFileName();
        return nullptr;
    }
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "El archivo es demasiado largo para cargarlo de forma segura.";
        return nullptr;
    }

    auto data = std::make_unique<ClipAudioData>();
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName();
    data->durationSeconds = static_cast<double>(reader->lengthInSamples) / std::max(1.0, reader->sampleRate);
    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int samples = static_cast<int>(reader->lengthInSamples);
    data->samples.setSize(channels, samples, false, true, false);
    if (!reader->read(&data->samples, 0, samples, 0, true, channels > 1))
    {
        error = "No se pudo decodificar " + file.getFileName();
        return nullptr;
    }

    auto* raw = data.get();
    audioPool_.push_back(std::move(data));
    return raw;
}

void DawWorkspace::importFiles(const juce::StringArray& files, int targetTrack, double startBeat)
{
    if (files.isEmpty()) return;
    checkpointUndo();
    double cursor = snapBeat(startBeat);
    int track = juce::jlimit(0, std::max(0, trackCount_ - 1), targetTrack);
    int imported = 0;
    juce::String lastError;

    for (const auto& path : files)
    {
        if (!isAudioPath(path)) continue;
        juce::String error;
        auto* data = loadAudioFile(juce::File(path), error);
        if (data == nullptr)
        {
            lastError = error;
            continue;
        }
        if (static_cast<int>(clips_.size()) >= kMaxClips)
        {
            lastError = "Máximo de 128 clips alcanzado";
            break;
        }

        Clip clip;
        clip.id = nextClipId_++;
        clip.track = track;
        clip.startBeat = cursor;
        clip.lengthBeats = std::max(0.25, data->durationSeconds * bpm() / 60.0);
        clip.colour = tracks_[track].colour;
        clip.audio = data;
        clips_.push_back(clip);
        selectedClipId_ = clip.id;
        selectedTrack_ = track;
        ++imported;
        cursor += clip.lengthBeats;
        if (files.size() > 1 && track + 1 < trackCount_) ++track;
    }

    if (imported > 0)
    {
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
        refreshStatus(juce::String(imported) + (imported == 1 ? " clip importado" : " clips importados"));
    }
    else if (lastError.isNotEmpty())
        refreshStatus(lastError);
}

void DawWorkspace::deleteSelectedClip()
{
    if (selectedMidiNoteId_ >= 0)
    {
        checkpointUndo();
        const auto before = midiNotes_.size();
        midiNotes_.erase(std::remove_if(midiNotes_.begin(), midiNotes_.end(),
            [this](const MidiNote& n) { return n.id == selectedMidiNoteId_; }), midiNotes_.end());
        if (midiNotes_.size() != before)
        {
            selectedMidiNoteId_ = -1;
            projectDirty_ = true;
            markRenderDirty();
            syncInspector();
            repaint();
        }
        return;
    }

    if (selectedClipId_ < 0) return;
    checkpointUndo();
    const auto before = clips_.size();
    clips_.erase(std::remove_if(clips_.begin(), clips_.end(),
        [this](const Clip& c) { return c.id == selectedClipId_; }), clips_.end());
    if (clips_.size() != before)
    {
        selectedClipId_ = -1;
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
    }
}

void DawWorkspace::duplicateSelectedClip()
{
    if (selectedMidiNoteId_ >= 0)
    {
        for (const auto& source : midiNotes_)
        {
            if (source.id != selectedMidiNoteId_) continue;
            if (static_cast<int>(midiNotes_.size()) >= kMaxMidiNotes) return;
            checkpointUndo();
            MidiNote copy = source;
            copy.id = nextMidiNoteId_++;
            copy.startBeat = snapBeat(source.startBeat + source.lengthBeats);
            midiNotes_.push_back(copy);
            selectedMidiNoteId_ = copy.id;
            projectDirty_ = true;
            markRenderDirty();
            repaint();
            return;
        }
    }

    for (const auto& source : clips_)
    {
        if (source.id != selectedClipId_) continue;
        if (static_cast<int>(clips_.size()) >= kMaxClips) return;
        checkpointUndo();
        Clip copy = source;
        copy.id = nextClipId_++;
        copy.startBeat = snapBeat(source.startBeat + source.lengthBeats);
        clips_.push_back(copy);
        selectedClipId_ = copy.id;
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
        return;
    }
}

void DawWorkspace::splitSelectedClipAtPlayhead()
{
    const double playBeat = (static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed))) * bpm() / 60.0;
    for (auto& c : clips_)
    {
        if (c.id != selectedClipId_) continue;
        if (playBeat <= c.startBeat + 0.02 || playBeat >= c.startBeat + c.lengthBeats - 0.02) return;
        if (static_cast<int>(clips_.size()) >= kMaxClips) return;

        checkpointUndo();
        const double leftBeats = playBeat - c.startBeat;
        Clip right = c;
        right.id = nextClipId_++;
        right.startBeat = playBeat;
        right.lengthBeats = c.lengthBeats - leftBeats;
        right.sourceOffsetSeconds = c.sourceOffsetSeconds + leftBeats * 60.0 / bpm();
        c.lengthBeats = leftBeats;
        c.fadeOutBeats = std::min(c.fadeOutBeats, c.lengthBeats);
        right.fadeInBeats = std::min(right.fadeInBeats, right.lengthBeats);
        clips_.push_back(right);
        selectedClipId_ = right.id;
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
        return;
    }
}

void DawWorkspace::togglePlay()
{
    const bool next = !playing_.load(std::memory_order_acquire);
    if (next)
    {
        const auto active = activeRenderState_.load(std::memory_order_acquire);
        const auto end = renderStates_[active].endSample;
        if (transportSamples_.load(std::memory_order_relaxed) >= end && end > 0)
            transportSamples_.store(0, std::memory_order_relaxed);
    }
    playing_.store(next, std::memory_order_release);
    playButton_.setButtonText(next ? "PAUSE" : "PLAY");
    if (onPlayStateChanged) onPlayStateChanged(next);
    repaint();
}

void DawWorkspace::stopTransport(bool returnToStart)
{
    playing_.store(false, std::memory_order_release);
    playButton_.setButtonText("PLAY");
    if (returnToStart) transportSamples_.store(0, std::memory_order_relaxed);
    if (onPlayStateChanged) onPlayStateChanged(false);
    repaint();
}

void DawWorkspace::setTransportBeat(double beat) noexcept
{
    const double sr = std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed));
    transportSamples_.store(static_cast<std::int64_t>(beat * 60.0 / std::max(1.0, bpm()) * sr),
                            std::memory_order_relaxed);
}

juce::File DawWorkspace::trackRecordingRoot() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("DAW Recordings");
}

void DawWorkspace::toggleTrackRecording()
{
    if (trackRecording_.load(std::memory_order_acquire))
    {
        stopTrackRecording(true);
        return;
    }

    startTrackRecording();
}

bool DawWorkspace::startTrackRecording()
{
    if (trackRecording_.load(std::memory_order_acquire))
        return true;

    recordArmedCount_ = 0;
    currentTakeFiles_.clear();

    for (int t = 0; t < trackCount_ && recordArmedCount_ < kMaxTracks; ++t)
    {
        if (!tracks_[t].armed || tracks_[t].midi)
            continue;
        recordTrackMap_[recordArmedCount_] = t;
        recordInputMap_[recordArmedCount_] = recordArmedCount_;
        ++recordArmedCount_;
    }

    if (recordArmedCount_ == 0 && selectedTrack_ >= 0 && selectedTrack_ < trackCount_
        && !tracks_[selectedTrack_].midi)
    {
        tracks_[selectedTrack_].armed = true;
        recordTrackMap_[0] = selectedTrack_;
        recordInputMap_[0] = 0;
        recordArmedCount_ = 1;
        syncInspector();
    }

    if (recordArmedCount_ == 0)
    {
        for (int t = 0; t < trackCount_; ++t)
        {
            if (tracks_[t].midi) continue;
            tracks_[t].armed = true;
            recordTrackMap_[0] = t;
            recordInputMap_[0] = 0;
            recordArmedCount_ = 1;
            selectedTrack_ = t;
            syncInspector();
            break;
        }
    }

    if (recordArmedCount_ <= 0)
    {
        refreshStatus("No hay pistas disponibles para grabar.");
        return false;
    }

    const auto sr = std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed));
    const auto now = juce::Time::getCurrentTime();
    currentTakeDirectory_ = trackRecordingRoot()
        .getChildFile(now.formatted("%Y-%m-%d"))
        .getChildFile(now.formatted("%H%M%S-DAW"));
    if (!currentTakeDirectory_.createDirectory())
    {
        refreshStatus("No se pudo crear la carpeta de grabación DAW.");
        return false;
    }

    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(recordArmedCount_));
    for (int i = 0; i < recordArmedCount_; ++i)
    {
        const int track = recordTrackMap_[i];
        const auto base = "Track" + juce::String(track + 1).paddedLeft('0', 2);
        names.push_back(base.toStdString());
        currentTakeFiles_.add(currentTakeDirectory_.getChildFile(base + ".wav").getFullPathName());
    }

    std::string error;
    if (!trackRecorder_.start(currentTakeDirectory_.getFullPathName().toStdString(),
                              static_cast<std::uint32_t>(std::llround(sr)), names, error))
    {
        refreshStatus("No se pudo iniciar REC DAW: " + juce::String(error));
        recordArmedCount_ = 0;
        currentTakeFiles_.clear();
        return false;
    }

    recordStartBeat_ = static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / sr * bpm() / 60.0;
    trackRecording_.store(true, std::memory_order_release);
    if (!playing_.load(std::memory_order_acquire))
        togglePlay();

    recordButton_.setButtonText("STOP REC");
    refreshStatus("REC DAW activo · " + juce::String(recordArmedCount_)
        + " pista(s) armada(s) · entradas activas en orden");
    repaint();
    return true;
}

void DawWorkspace::stopTrackRecording(bool importTake)
{
    if (!trackRecording_.exchange(false, std::memory_order_acq_rel))
        return;

    std::string error;
    const bool ok = trackRecorder_.stop(error);
    recordButton_.setButtonText("REC");

    if (!ok)
    {
        refreshStatus("La grabación terminó con error: " + juce::String(error));
        return;
    }

    if (importTake)
        importRecordedTake();
    else
        refreshStatus("Grabación DAW detenida.");
}

void DawWorkspace::importRecordedTake()
{
    if (recordArmedCount_ <= 0 || currentTakeFiles_.isEmpty())
        return;

    checkpointUndo();
    int imported = 0;
    juce::String lastError;

    const int count = std::min(recordArmedCount_, currentTakeFiles_.size());
    for (int i = 0; i < count; ++i)
    {
        const juce::File file(currentTakeFiles_[i]);
        juce::String error;
        auto* data = loadAudioFile(file, error);
        if (data == nullptr)
        {
            lastError = error;
            continue;
        }
        if (static_cast<int>(clips_.size()) >= kMaxClips)
        {
            lastError = "Máximo de 128 clips alcanzado";
            break;
        }

        const int track = juce::jlimit(0, trackCount_ - 1, recordTrackMap_[i]);
        Clip clip;
        clip.id = nextClipId_++;
        clip.track = track;
        clip.startBeat = std::max(0.0, recordStartBeat_);
        clip.lengthBeats = std::max(0.25, data->durationSeconds * bpm() / 60.0);
        clip.colour = tracks_[track].colour;
        clip.audio = data;
        clips_.push_back(clip);
        selectedTrack_ = track;
        selectedClipId_ = clip.id;
        ++imported;
    }

    if (imported > 0)
    {
        projectDirty_ = true;
        markRenderDirty();
        rebuildRenderState();
        syncInspector();
        autosaveRecovery();
        refreshStatus("Toma DAW importada · " + juce::String(imported) + " clip(s)");
    }
    else
    {
        refreshStatus(lastError.isNotEmpty() ? lastError : "No se pudo importar la toma grabada.");
    }

    repaint();
}

void DawWorkspace::syncInspector()
{
    selectedTrack_ = juce::jlimit(0, std::max(0, trackCount_ - 1), selectedTrack_);
    auto& t = tracks_[selectedTrack_];
    trackNameEditor_.setText(t.name, juce::dontSendNotification);
    trackVolumeSlider_.setValue(gainToDb(t.gain), juce::dontSendNotification);
    trackPanSlider_.setValue(t.pan, juce::dontSendNotification);
    trackMuteButton_.setToggleState(t.mute, juce::dontSendNotification);
    trackSoloButton_.setToggleState(t.solo, juce::dontSendNotification);
    trackArmButton_.setToggleState(t.armed, juce::dontSendNotification);

    const auto* c = clipAt({ -1, -1 });
    const bool clipSelected = c != nullptr;
    clipGainSlider_.setEnabled(clipSelected);
    clipMuteButton_.setEnabled(clipSelected);
    clipLoopButton_.setEnabled(clipSelected);
    fadeInSlider_.setEnabled(clipSelected);
    fadeOutSlider_.setEnabled(clipSelected);
    splitButton_.setEnabled(clipSelected);
    const bool midiSelected = selectedMidiNoteId_ >= 0;
    duplicateButton_.setEnabled(clipSelected || midiSelected);
    deleteButton_.setEnabled(clipSelected || midiSelected);
    splitButton_.setEnabled(clipSelected);
    patternButton_.setEnabled(t.midi);
    if (c != nullptr)
    {
        clipGainSlider_.setValue(gainToDb(c->gain), juce::dontSendNotification);
        clipMuteButton_.setToggleState(c->muted, juce::dontSendNotification);
        clipLoopButton_.setToggleState(c->loop, juce::dontSendNotification);
        fadeInSlider_.setValue(c->fadeInBeats, juce::dontSendNotification);
        fadeOutSlider_.setValue(c->fadeOutBeats, juce::dontSendNotification);
    }
}

void DawWorkspace::refreshStatus(const juce::String& text)
{
    statusLabel_.setText(text, juce::dontSendNotification);
}

void DawWorkspace::prepare(double sampleRate, int maximumBlockSize)
{
    juce::ignoreUnused(maximumBlockSize);
    renderSampleRate_.store(std::max(1.0, sampleRate), std::memory_order_release);
    markRenderDirty();
    rebuildRenderState();
}

void DawWorkspace::captureInputBlock(const float* const* inputChannelData,
                                     int numInputChannels,
                                     int numSamples) noexcept
{
    if (!trackRecording_.load(std::memory_order_acquire)
        || inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    std::array<const float*, kMaxTracks> activeInputs {};
    int activeCount = 0;
    for (int ch = 0; ch < numInputChannels && activeCount < kMaxTracks; ++ch)
        if (inputChannelData[ch] != nullptr)
            activeInputs[activeCount++] = inputChannelData[ch];

    const int wanted = recordArmedCount_;
    if (wanted <= 0)
        return;

    static constexpr std::array<float, j3::kRecordMaxFrames> silence {};
    int offset = 0;
    while (offset < numSamples)
    {
        const int frames = std::min<int>(static_cast<int>(j3::kRecordMaxFrames), numSamples - offset);
        std::array<const float*, kMaxTracks> block {};
        for (int i = 0; i < wanted; ++i)
            block[i] = i < activeCount ? activeInputs[i] + offset : silence.data();
        trackRecorder_.submit(block.data(), static_cast<std::size_t>(wanted),
                              static_cast<std::size_t>(frames));
        offset += frames;
    }
}

void DawWorkspace::markRenderDirty()
{
    renderDirty_.store(true, std::memory_order_release);
}

void DawWorkspace::rebuildRenderState()
{
    if (!renderDirty_.load(std::memory_order_acquire)) return;
    const int active = activeRenderState_.load(std::memory_order_acquire);
    int target = -1;
    for (int i = 0; i < kRenderBuffers; ++i)
    {
        if (i != active && renderReaders_[i].load(std::memory_order_acquire) == 0)
        {
            target = i;
            break;
        }
    }
    if (target < 0) return;

    auto& state = renderStates_[target];
    state = {};
    state.bpm = bpm();
    state.sampleRate = std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed));
    state.trackCount = trackCount_;
    state.anySolo = false;
    for (int t = 0; t < trackCount_; ++t)
    {
        state.tracks[t].gain = tracks_[t].gain;
        state.tracks[t].pan = tracks_[t].pan;
        state.tracks[t].mute = tracks_[t].mute;
        state.tracks[t].solo = tracks_[t].solo;
        state.anySolo = state.anySolo || tracks_[t].solo;
    }

    const double secondsPerBeat = 60.0 / std::max(1.0, state.bpm);
    for (const auto& c : clips_)
    {
        if (state.clipCount >= kMaxClips || c.audio == nullptr) break;
        auto& rc = state.clips[state.clipCount++];
        rc.audio = c.audio;
        rc.track = c.track;
        rc.startSample = static_cast<std::int64_t>(std::llround(c.startBeat * secondsPerBeat * state.sampleRate));
        rc.lengthSamples = std::max<std::int64_t>(1, static_cast<std::int64_t>(
            std::llround(c.lengthBeats * secondsPerBeat * state.sampleRate)));
        rc.sourceOffsetSeconds = c.sourceOffsetSeconds;
        rc.gain = c.gain;
        rc.muted = c.muted;
        rc.loop = c.loop;
        rc.fadeInSamples = static_cast<std::int64_t>(std::llround(c.fadeInBeats * secondsPerBeat * state.sampleRate));
        rc.fadeOutSamples = static_cast<std::int64_t>(std::llround(c.fadeOutBeats * secondsPerBeat * state.sampleRate));
        state.endSample = std::max(state.endSample, rc.startSample + rc.lengthSamples);
    }

    for (const auto& n : midiNotes_)
    {
        if (state.midiNoteCount >= kMaxMidiNotes) break;
        if (n.track < 0 || n.track >= trackCount_ || !tracks_[n.track].midi) continue;
        auto& rn = state.midiNotes[state.midiNoteCount++];
        rn.track = n.track;
        rn.startSample = static_cast<std::int64_t>(std::llround(n.startBeat * secondsPerBeat * state.sampleRate));
        rn.lengthSamples = std::max<std::int64_t>(1, static_cast<std::int64_t>(
            std::llround(n.lengthBeats * secondsPerBeat * state.sampleRate)));
        rn.note = juce::jlimit(0, 127, n.note);
        rn.velocity = juce::jlimit(0.0f, 1.0f, n.velocity);
        state.endSample = std::max(state.endSample, rn.startSample + rn.lengthSamples);
    }

    activeRenderState_.store(target, std::memory_order_release);
    renderDirty_.store(false, std::memory_order_release);
}

void DawWorkspace::renderToMaster(float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0
        || !playing_.load(std::memory_order_acquire))
        return;

    const int index = activeRenderState_.load(std::memory_order_acquire);
    renderReaders_[index].fetch_add(1, std::memory_order_acq_rel);
    const auto& state = renderStates_[index];
    const std::int64_t blockStart = transportSamples_.load(std::memory_order_relaxed);
    const std::int64_t blockEnd = blockStart + numSamples;

    for (int ci = 0; ci < state.clipCount; ++ci)
    {
        const auto& clip = state.clips[ci];
        if (clip.muted || clip.audio == nullptr || clip.track < 0 || clip.track >= state.trackCount)
            continue;
        const auto& track = state.tracks[clip.track];
        if (track.mute || (state.anySolo && !track.solo))
            continue;

        const std::int64_t clipStart = clip.startSample;
        const std::int64_t clipEnd = clip.startSample + clip.lengthSamples;
        const std::int64_t ovStart = std::max(blockStart, clipStart);
        const std::int64_t ovEnd = std::min(blockEnd, clipEnd);
        if (ovStart >= ovEnd) continue;

        const auto& src = clip.audio->samples;
        const int srcSamples = src.getNumSamples();
        const int srcChannels = src.getNumChannels();
        if (srcSamples <= 0 || srcChannels <= 0) continue;

        const double ratio = clip.audio->sampleRate / std::max(1.0, state.sampleRate);
        const double sourceOffset = clip.sourceOffsetSeconds * clip.audio->sampleRate;
        const float pan = juce::jlimit(-1.0f, 1.0f, track.pan);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float panL = std::cos(angle);
        const float panR = std::sin(angle);
        const float baseGain = clip.gain * track.gain;

        for (std::int64_t global = ovStart; global < ovEnd; ++global)
        {
            const std::int64_t local = global - clipStart;
            double srcPos = sourceOffset + static_cast<double>(local) * ratio;
            if (clip.loop)
            {
                srcPos = std::fmod(srcPos, static_cast<double>(srcSamples));
                if (srcPos < 0.0) srcPos += srcSamples;
            }
            else if (srcPos < 0.0 || srcPos >= srcSamples - 1)
                continue;

            const int i0 = juce::jlimit(0, srcSamples - 1, static_cast<int>(srcPos));
            const int i1 = std::min(srcSamples - 1, i0 + 1);
            const float frac = static_cast<float>(srcPos - i0);
            auto read = [&](int ch)
            {
                const int sourceCh = std::min(ch, srcChannels - 1);
                const float a = src.getSample(sourceCh, i0);
                const float b = src.getSample(sourceCh, i1);
                return a + (b - a) * frac;
            };

            float env = 1.0f;
            if (clip.fadeInSamples > 0 && local < clip.fadeInSamples)
                env = std::min(env, static_cast<float>(local) / static_cast<float>(clip.fadeInSamples));
            const auto remain = clip.lengthSamples - local;
            if (clip.fadeOutSamples > 0 && remain < clip.fadeOutSamples)
                env = std::min(env, static_cast<float>(remain) / static_cast<float>(clip.fadeOutSamples));
            env = juce::jlimit(0.0f, 1.0f, env);

            const int dst = static_cast<int>(global - blockStart);
            if (srcChannels == 1)
            {
                const float v = read(0) * baseGain * env;
                if (left == right) left[dst] += v;
                else { left[dst] += v * panL; right[dst] += v * panR; }
            }
            else
            {
                float l = read(0) * baseGain * env;
                float r = read(1) * baseGain * env;
                if (pan < 0.0f) r *= 1.0f + pan;
                else if (pan > 0.0f) l *= 1.0f - pan;
                if (left == right) left[dst] += (l + r) * 0.70710678f;
                else { left[dst] += l; right[dst] += r; }
            }
        }
    }

    for (int ni = 0; ni < state.midiNoteCount; ++ni)
    {
        const auto& note = state.midiNotes[ni];
        if (note.track < 0 || note.track >= state.trackCount) continue;
        const auto& track = state.tracks[note.track];
        if (track.mute || (state.anySolo && !track.solo)) continue;

        const std::int64_t noteStart = note.startSample;
        const std::int64_t noteEnd = note.startSample + note.lengthSamples;
        const std::int64_t ovStart = std::max(blockStart, noteStart);
        const std::int64_t ovEnd = std::min(blockEnd, noteEnd);
        if (ovStart >= ovEnd) continue;

        const double frequency = 440.0 * std::pow(2.0, (static_cast<double>(note.note) - 69.0) / 12.0);
        const double twoPiF = juce::MathConstants<double>::twoPi * frequency;
        const float pan = juce::jlimit(-1.0f, 1.0f, track.pan);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float panL = std::cos(angle);
        const float panR = std::sin(angle);
        const std::int64_t attack = std::max<std::int64_t>(1, static_cast<std::int64_t>(state.sampleRate * 0.006));
        const std::int64_t release = std::max<std::int64_t>(1, static_cast<std::int64_t>(state.sampleRate * 0.018));
        const float gain = 0.16f * note.velocity * track.gain;

        for (std::int64_t global = ovStart; global < ovEnd; ++global)
        {
            const auto local = global - noteStart;
            const auto remain = note.lengthSamples - local;
            float env = 1.0f;
            if (local < attack) env = static_cast<float>(local) / static_cast<float>(attack);
            if (remain < release) env = std::min(env, static_cast<float>(remain) / static_cast<float>(release));
            env = juce::jlimit(0.0f, 1.0f, env);

            const double t = static_cast<double>(local) / state.sampleRate;
            const float fundamental = static_cast<float>(std::sin(twoPiF * t));
            const float harmonic = static_cast<float>(std::sin(twoPiF * 2.0 * t)) * 0.22f;
            const float v = (fundamental + harmonic) * gain * env;
            const int dst = static_cast<int>(global - blockStart);
            if (left == right) left[dst] += v * 0.70710678f;
            else { left[dst] += v * panL; right[dst] += v * panR; }
        }
    }

    std::int64_t next = blockEnd;
    if (loopEnabled_.load(std::memory_order_relaxed))
    {
        const auto ls = loopStartSamples_.load(std::memory_order_relaxed);
        const auto le = loopEndSamples_.load(std::memory_order_relaxed);
        if (le > ls && next >= le)
            next = ls + (next - le);
    }
    else if (state.endSample > 0 && next >= state.endSample)
    {
        next = state.endSample;
        playing_.store(false, std::memory_order_release);
    }
    transportSamples_.store(next, std::memory_order_relaxed);
    renderReaders_[index].fetch_sub(1, std::memory_order_acq_rel);
}

void DawWorkspace::timerCallback()
{
    if (renderDirty_.load(std::memory_order_acquire)) rebuildRenderState();
    const bool nowPlaying = playing_.load(std::memory_order_acquire);
    playButton_.setButtonText(nowPlaying ? "PAUSE" : "PLAY");
    if (nowPlaying != lastReportedPlaying_)
    {
        lastReportedPlaying_ = nowPlaying;
        if (onPlayStateChanged) onPlayStateChanged(nowPlaying);
    }
    recordButton_.setButtonText(trackRecording_.load(std::memory_order_acquire) ? "STOP REC" : "REC");
    repaint();

    if (++autosaveTicks_ >= 300)
    {
        autosaveTicks_ = 0;
        if (projectDirty_) autosaveRecovery();
    }
}

void DawWorkspace::setTempoFromHost(double value)
{
    value = juce::jlimit(40.0, 240.0, value);
    if (std::abs(value - bpm()) < 0.0001) return;
    bpm_.store(value, std::memory_order_relaxed);
    bpmSlider_.setValue(value, juce::dontSendNotification);
    markRenderDirty();
    repaint();
}

void DawWorkspace::checkpointUndo()
{
    pushUndoSnapshot(serializeProject());
}

void DawWorkspace::pushUndoSnapshot(const juce::String& snapshot)
{
    if (snapshot.isEmpty()) return;
    undoStack_.push_back(snapshot);
    if (static_cast<int>(undoStack_.size()) > kUndoLimit)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void DawWorkspace::undo()
{
    if (undoStack_.empty()) return;
    redoStack_.push_back(serializeProject());
    const auto snapshot = undoStack_.back();
    undoStack_.pop_back();
    restoreProject(snapshot, false);
    projectDirty_ = true;
}

void DawWorkspace::redo()
{
    if (redoStack_.empty()) return;
    undoStack_.push_back(serializeProject());
    const auto snapshot = redoStack_.back();
    redoStack_.pop_back();
    restoreProject(snapshot, false);
    projectDirty_ = true;
}

juce::String DawWorkspace::serializeProject() const
{
    juce::XmlElement root("J3DAW");
    root.setAttribute("version", 1);
    root.setAttribute("bpm", bpm());
    root.setAttribute("trackCount", trackCount_);
    root.setAttribute("viewStartBeat", viewStartBeat_);
    root.setAttribute("zoom", zoom_);
    root.setAttribute("firstVisibleTrack", firstVisibleTrack_);

    auto* tracksXml = root.createNewChildElement("Tracks");
    for (int i = 0; i < trackCount_; ++i)
    {
        auto* t = tracksXml->createNewChildElement("Track");
        t->setAttribute("index", i);
        t->setAttribute("name", tracks_[i].name);
        t->setAttribute("colour", static_cast<int>(tracks_[i].colour.getARGB()));
        t->setAttribute("gain", tracks_[i].gain);
        t->setAttribute("pan", tracks_[i].pan);
        t->setAttribute("mute", tracks_[i].mute);
        t->setAttribute("solo", tracks_[i].solo);
        t->setAttribute("armed", tracks_[i].armed);
        t->setAttribute("midi", tracks_[i].midi);
    }

    auto* midiXml = root.createNewChildElement("MidiNotes");
    for (const auto& n : midiNotes_)
    {
        auto* x = midiXml->createNewChildElement("Note");
        x->setAttribute("id", n.id);
        x->setAttribute("track", n.track);
        x->setAttribute("startBeat", n.startBeat);
        x->setAttribute("lengthBeats", n.lengthBeats);
        x->setAttribute("note", n.note);
        x->setAttribute("velocity", n.velocity);
    }

    auto* clipsXml = root.createNewChildElement("Clips");
    for (const auto& c : clips_)
    {
        if (c.audio == nullptr) continue;
        auto* x = clipsXml->createNewChildElement("Clip");
        x->setAttribute("id", c.id);
        x->setAttribute("track", c.track);
        x->setAttribute("path", c.audio->path);
        x->setAttribute("startBeat", c.startBeat);
        x->setAttribute("lengthBeats", c.lengthBeats);
        x->setAttribute("sourceOffsetSeconds", c.sourceOffsetSeconds);
        x->setAttribute("gain", c.gain);
        x->setAttribute("muted", c.muted);
        x->setAttribute("loop", c.loop);
        x->setAttribute("fadeInBeats", c.fadeInBeats);
        x->setAttribute("fadeOutBeats", c.fadeOutBeats);
        x->setAttribute("colour", static_cast<int>(c.colour.getARGB()));
    }
    return root.toString();
}

bool DawWorkspace::restoreProject(const juce::String& xmlText, bool updateProjectFile, const juce::File& sourceFile)
{
    auto xml = juce::parseXML(xmlText);
    if (xml == nullptr || !xml->hasTagName("J3DAW")) return false;

    stopTransport(true);
    if (trackRecording_.load(std::memory_order_acquire))
        stopTrackRecording(false);
    clips_.clear();
    midiNotes_.clear();
    nextClipId_ = 1;
    nextMidiNoteId_ = 1;
    trackCount_ = juce::jlimit(1, kMaxTracks, xml->getIntAttribute("trackCount", 8));
    bpm_.store(juce::jlimit(40.0, 240.0, xml->getDoubleAttribute("bpm", 120.0)));
    bpmSlider_.setValue(bpm(), juce::dontSendNotification);
    viewStartBeat_ = std::max(0.0, xml->getDoubleAttribute("viewStartBeat", 0.0));
    zoom_ = juce::jlimit(0.5, 4.0, xml->getDoubleAttribute("zoom", 1.0));
    zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    firstVisibleTrack_ = juce::jlimit(0, std::max(0, trackCount_ - 1),
        xml->getIntAttribute("firstVisibleTrack", 0));

    if (auto* tracksXml = xml->getChildByName("Tracks"))
    {
        forEachXmlChildElementWithTagName(*tracksXml, t, "Track")
        {
            const int i = t->getIntAttribute("index", -1);
            if (i < 0 || i >= trackCount_) continue;
            tracks_[i].name = t->getStringAttribute("name", "Pista " + juce::String(i + 1));
            tracks_[i].colour = juce::Colour(static_cast<juce::uint32>(t->getIntAttribute("colour", static_cast<int>(trackColour(i).getARGB()))));
            tracks_[i].gain = static_cast<float>(t->getDoubleAttribute("gain", 1.0));
            tracks_[i].pan = static_cast<float>(t->getDoubleAttribute("pan", 0.0));
            tracks_[i].mute = t->getBoolAttribute("mute", false);
            tracks_[i].solo = t->getBoolAttribute("solo", false);
            tracks_[i].armed = t->getBoolAttribute("armed", false);
            tracks_[i].midi = t->getBoolAttribute("midi", false);
        }
    }

    if (auto* midiXml = xml->getChildByName("MidiNotes"))
    {
        forEachXmlChildElementWithTagName(*midiXml, x, "Note")
        {
            if (static_cast<int>(midiNotes_.size()) >= kMaxMidiNotes) break;
            MidiNote n;
            n.id = x->getIntAttribute("id", nextMidiNoteId_++);
            nextMidiNoteId_ = std::max(nextMidiNoteId_, n.id + 1);
            n.track = juce::jlimit(0, trackCount_ - 1, x->getIntAttribute("track", 0));
            n.startBeat = std::max(0.0, x->getDoubleAttribute("startBeat", 0.0));
            n.lengthBeats = std::max(0.125, x->getDoubleAttribute("lengthBeats", 1.0));
            n.note = juce::jlimit(0, 127, x->getIntAttribute("note", 60));
            n.velocity = juce::jlimit(0.01f, 1.0f, static_cast<float>(x->getDoubleAttribute("velocity", 0.8)));
            tracks_[n.track].midi = true;
            midiNotes_.push_back(n);
        }
    }

    int missing = 0;
    if (auto* clipsXml = xml->getChildByName("Clips"))
    {
        forEachXmlChildElementWithTagName(*clipsXml, x, "Clip")
        {
            if (static_cast<int>(clips_.size()) >= kMaxClips) break;
            juce::String error;
            const auto path = x->getStringAttribute("path");
            auto* data = loadAudioFile(juce::File(path), error);
            if (data == nullptr) { ++missing; continue; }

            Clip c;
            c.id = x->getIntAttribute("id", nextClipId_++);
            nextClipId_ = std::max(nextClipId_, c.id + 1);
            c.track = juce::jlimit(0, trackCount_ - 1, x->getIntAttribute("track", 0));
            c.startBeat = std::max(0.0, x->getDoubleAttribute("startBeat", 0.0));
            c.lengthBeats = std::max(0.05, x->getDoubleAttribute("lengthBeats", 4.0));
            c.sourceOffsetSeconds = std::max(0.0, x->getDoubleAttribute("sourceOffsetSeconds", 0.0));
            c.gain = static_cast<float>(x->getDoubleAttribute("gain", 1.0));
            c.muted = x->getBoolAttribute("muted", false);
            c.loop = x->getBoolAttribute("loop", false);
            c.fadeInBeats = std::max(0.0, x->getDoubleAttribute("fadeInBeats", 0.0));
            c.fadeOutBeats = std::max(0.0, x->getDoubleAttribute("fadeOutBeats", 0.0));
            c.colour = juce::Colour(static_cast<juce::uint32>(x->getIntAttribute("colour", static_cast<int>(tracks_[c.track].colour.getARGB()))));
            c.audio = data;
            clips_.push_back(c);
        }
    }

    if (updateProjectFile && sourceFile != juce::File()) projectFile_ = sourceFile;
    selectedTrack_ = 0;
    selectedClipId_ = -1;
    selectedMidiNoteId_ = -1;
    projectDirty_ = false;
    markRenderDirty();
    rebuildRenderState();
    syncInspector();
    repaint();
    if (onBpmChanged) onBpmChanged(bpm());
    refreshStatus(missing > 0 ? juce::String(missing) + " archivos faltantes" : "Proyecto cargado");
    return true;
}

bool DawWorkspace::saveProject(const juce::File& file)
{
    if (file == juce::File()) return false;
    auto target = file;
    if (target.getFileExtension().isEmpty()) target = target.withFileExtension(".j3w");
    target.getParentDirectory().createDirectory();
    if (!target.replaceWithText(serializeProject())) return false;
    projectFile_ = target;
    projectDirty_ = false;
    recoveryFile().deleteFile();
    refreshStatus("Guardado · " + target.getFileName());
    return true;
}

void DawWorkspace::saveProjectInteractive(bool saveAs)
{
    if (!saveAs && projectFile_ != juce::File())
    {
        saveProject(projectFile_);
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>("Guardar proyecto J3 Worship",
        projectFile_ != juce::File() ? projectFile_ : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("J3 Worship Project.j3w"),
        "*.j3w");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& c)
        {
            const auto f = c.getResult();
            if (f != juce::File()) saveProject(f);
        });
}

void DawWorkspace::openProjectInteractive()
{
    chooser_ = std::make_unique<juce::FileChooser>("Abrir proyecto J3 Worship", juce::File{}, "*.j3w");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& c)
        {
            const auto f = c.getResult();
            if (f == juce::File()) return;
            checkpointUndo();
            if (!restoreProject(f.loadFileAsString(), true, f))
                refreshStatus("No se pudo abrir el proyecto");
        });
}

void DawWorkspace::newProject()
{
    checkpointUndo();
    stopTransport(true);
    if (trackRecording_.load(std::memory_order_acquire))
        stopTrackRecording(false);
    clips_.clear();
    midiNotes_.clear();
    nextClipId_ = 1;
    nextMidiNoteId_ = 1;
    trackCount_ = 8;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        tracks_[i] = {};
        tracks_[i].name = "Pista " + juce::String(i + 1);
        tracks_[i].colour = trackColour(i);
    }
    tracks_[0].name = "Voz";
    tracks_[1].name = "Batería";
    tracks_[2].name = "Bajo";
    tracks_[3].name = "Guitarra";
    tracks_[4].name = "Teclado";
    tracks_[5].name = "Secuencias";
    tracks_[6].name = "Pads";
    tracks_[7].name = "FX";
    selectedTrack_ = 0;
    selectedClipId_ = -1;
    selectedMidiNoteId_ = -1;
    projectFile_ = {};
    bpm_.store(120.0);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    viewStartBeat_ = 0.0;
    firstVisibleTrack_ = 0;
    projectDirty_ = false;
    recoveryFile().deleteFile();
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus("Proyecto nuevo");
    if (onBpmChanged) onBpmChanged(120.0);
}

juce::File DawWorkspace::recoveryFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3Worship")
        .getChildFile("daw-recovery.j3w");
}

void DawWorkspace::autosaveRecovery()
{
    if (clips_.empty() && midiNotes_.empty() && !projectDirty_) return;
    auto f = recoveryFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(serializeProject());
}

