#include "DawWorkspace.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
constexpr int kTrackHeaderWidth = 190;
constexpr int kRulerHeight = 38;
constexpr double kTimelineSeconds = 600.0;

float constantPowerLeft(float pan) noexcept
{
    const auto angle = (juce::jlimit(-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    return std::cos(angle);
}

float constantPowerRight(float pan) noexcept
{
    const auto angle = (juce::jlimit(-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    return std::sin(angle);
}

bool isAudioExtension(const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

juce::Colour trackColourForIndex(int i)
{
    static const std::array<juce::Colour, 10> colours {
        juce::Colour(0xff1593ff), juce::Colour(0xffef4b9a), juce::Colour(0xff37ce76),
        juce::Colour(0xffffbf28), juce::Colour(0xffae55e9), juce::Colour(0xffff4f61),
        juce::Colour(0xff19bfd3), juce::Colour(0xffc0c8d0), juce::Colour(0xffff8a30),
        juce::Colour(0xff6e7dff)
    };
    return colours[static_cast<std::size_t>(i) % colours.size()];
}
}

DawWorkspace::TimelineView::TimelineView(DawWorkspace& owner) : owner_(owner)
{
    setOpaque(false);
    setMouseClickGrabsKeyboardFocus(true);
}

int DawWorkspace::TimelineView::trackFromY(int y) const
{
    const int rowH = std::max(54, static_cast<int>(84.0f * owner_.verticalZoom_));
    return juce::jlimit(0, owner_.trackCount_.load() - 1, (y - kRulerHeight) / rowH);
}

double DawWorkspace::TimelineView::secondsFromX(int x) const
{
    return std::max(0.0, (static_cast<double>(x - kTrackHeaderWidth) / owner_.horizontalZoom_));
}

int DawWorkspace::TimelineView::xFromSeconds(double seconds) const
{
    return kTrackHeaderWidth + static_cast<int>(seconds * owner_.horizontalZoom_);
}

juce::Rectangle<int> DawWorkspace::TimelineView::clipBounds(int clipIndex) const
{
    if (clipIndex < 0 || clipIndex >= kMaxClips || !owner_.clips_[clipIndex].active.load())
        return {};
    const auto& clip = owner_.clips_[clipIndex];
    const int rowH = std::max(54, static_cast<int>(84.0f * owner_.verticalZoom_));
    const int track = juce::jlimit(0, owner_.trackCount_.load() - 1, clip.track.load());
    const int x = xFromSeconds(clip.startSeconds.load());
    const int w = std::max(4, static_cast<int>(clip.lengthSeconds.load() * owner_.horizontalZoom_));
    return { x, kRulerHeight + track * rowH + 4, w, rowH - 8 };
}

void DawWorkspace::TimelineView::paint(juce::Graphics& g)
{
    g.fillAll(owner_.bg_);
    const int tracks = owner_.trackCount_.load();
    const int rowH = std::max(54, static_cast<int>(84.0f * owner_.verticalZoom_));
    const double bpm = owner_.bpm_.load();
    const double beatSeconds = 60.0 / std::max(20.0, bpm);
    const double totalSeconds = std::max(kTimelineSeconds, static_cast<double>(getWidth() - kTrackHeaderWidth) / owner_.horizontalZoom_);

    g.setColour(owner_.panel_.brighter(0.06f));
    g.fillRect(0, 0, getWidth(), kRulerHeight);
    g.setColour(owner_.border_);
    g.drawLine(static_cast<float>(kTrackHeaderWidth), 0.0f,
               static_cast<float>(kTrackHeaderWidth), static_cast<float>(getHeight()), 1.0f);

    for (int t = 0; t < tracks; ++t)
    {
        const int y = kRulerHeight + t * rowH;
        const auto row = juce::Rectangle<int>(0, y, getWidth(), rowH);
        g.setColour((t % 2 == 0 ? owner_.panel_ : owner_.panel_.darker(0.08f)));
        g.fillRect(row);
        if (t == owner_.selectedTrack_.load())
        {
            g.setColour(owner_.accent_.withAlpha(0.08f));
            g.fillRect(row);
        }

        g.setColour(owner_.tracks_[t].colour.withAlpha(0.8f));
        g.fillRect(0, y, 5, rowH);
        g.setColour(owner_.text_);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(owner_.tracks_[t].name, 12, y + 7, 88, 22, juce::Justification::centredLeft);

        const bool m = owner_.tracks_[t].mute.load();
        const bool s = owner_.tracks_[t].solo.load();
        const bool r = owner_.tracks_[t].arm.load();
        const bool i = owner_.tracks_[t].monitor.load();
        const std::array<std::pair<const char*, bool>, 4> flags {{{"M",m},{"S",s},{"R",r},{"I",i}}};
        int bx = 104;
        for (std::size_t n = 0; n < flags.size(); ++n)
        {
            const auto b = juce::Rectangle<int>(bx, y + 7, 18, 18);
            bool on = flags[n].second;
            g.setColour(on ? (n == 0 ? juce::Colour(0xffff4d64)
                          : n == 1 ? juce::Colour(0xffffc247)
                          : n == 2 ? juce::Colour(0xffff5366)
                                   : owner_.accent_) : owner_.border_);
            g.fillRoundedRectangle(b.toFloat(), 3.0f);
            g.setColour(on ? juce::Colours::white : owner_.text_);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(flags[n].first, b, juce::Justification::centred);
            bx += 21;
        }

        const float gain = juce::Decibels::gainToDecibels(std::max(1.0e-6f, owner_.tracks_[t].gain.load()), -60.0f);
        const float pan = owner_.tracks_[t].pan.load();
        g.setColour(owner_.text_.withAlpha(0.72f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String(gain, 1) + " dB", 12, y + 35, 70, 18, juce::Justification::centredLeft);
        g.drawText("PAN " + juce::String(pan, 2), 90, y + 35, 86, 18, juce::Justification::centredLeft);

        g.setColour(owner_.border_.withAlpha(0.7f));
        g.drawHorizontalLine(y + rowH - 1, 0.0f, static_cast<float>(getWidth()));
    }

    int beatIndex = 0;
    for (double s = 0.0; s <= totalSeconds; s += beatSeconds, ++beatIndex)
    {
        const int x = xFromSeconds(s);
        if (x < kTrackHeaderWidth || x > getWidth()) continue;
        const bool bar = beatIndex % 4 == 0;
        g.setColour((bar ? owner_.border_.brighter(0.32f) : owner_.border_).withAlpha(bar ? 0.85f : 0.35f));
        g.drawVerticalLine(x, static_cast<float>(kRulerHeight), static_cast<float>(getHeight()));
        if (bar)
        {
            g.setColour(owner_.text_.withAlpha(0.8f));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(beatIndex / 4 + 1), x + 3, 5, 42, 18, juce::Justification::centredLeft);
        }
    }

    for (int c = 0; c < kMaxClips; ++c)
    {
        const auto& clip = owner_.clips_[c];
        if (!clip.active.load()) continue;
        auto bounds = clipBounds(c);
        if (bounds.isEmpty() || !bounds.intersects(getLocalBounds())) continue;

        auto colour = clip.colour;
        if (clip.mute.load()) colour = colour.withSaturation(0.15f).darker(0.35f);
        g.setColour(colour.withAlpha(c == owner_.selectedClip_ ? 0.88f : 0.68f));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(c == owner_.selectedClip_ ? juce::Colours::white : colour.brighter(0.35f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, c == owner_.selectedClip_ ? 1.8f : 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(clip.name, bounds.reduced(7, 3).removeFromTop(18), juce::Justification::centredLeft, true);

        auto* buffer = clip.buffer.load(std::memory_order_acquire);
        if (buffer != nullptr && buffer->getNumSamples() > 0)
        {
            auto wave = bounds.reduced(5, 22);
            if (wave.getHeight() > 8)
            {
                const int channels = std::max(1, buffer->getNumChannels());
                const double sourceStart = clip.sourceStartSeconds.load();
                const double length = clip.lengthSeconds.load();
                const double sourceRate = std::max(1.0, clip.sourceSampleRate);
                const int pixels = std::max(1, wave.getWidth());
                g.setColour(juce::Colours::white.withAlpha(0.72f));
                for (int px = 0; px < pixels; ++px)
                {
                    const double t0 = sourceStart + length * static_cast<double>(px) / pixels;
                    const double t1 = sourceStart + length * static_cast<double>(px + 1) / pixels;
                    int a = juce::jlimit(0, buffer->getNumSamples() - 1, static_cast<int>(t0 * sourceRate));
                    int b = juce::jlimit(a + 1, buffer->getNumSamples(), static_cast<int>(t1 * sourceRate) + 1);
                    float lo = 1.0f, hi = -1.0f;
                    const int stride = std::max(1, (b - a) / 24);
                    for (int sample = a; sample < b; sample += stride)
                    {
                        float v = 0.0f;
                        for (int ch = 0; ch < channels; ++ch) v += buffer->getSample(ch, sample);
                        v /= static_cast<float>(channels);
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                    const float cy = static_cast<float>(wave.getCentreY());
                    const float half = wave.getHeight() * 0.45f;
                    g.drawVerticalLine(wave.getX() + px, cy - hi * half, cy - lo * half);
                }
            }
        }

        const double fi = clip.fadeInSeconds.load();
        const double fo = clip.fadeOutSeconds.load();
        if (fi > 0.001)
        {
            const int fx = bounds.getX() + static_cast<int>(fi * owner_.horizontalZoom_);
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawLine(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()),
                       static_cast<float>(std::min(fx, bounds.getRight())), static_cast<float>(bounds.getY()), 1.0f);
        }
        if (fo > 0.001)
        {
            const int fx = bounds.getRight() - static_cast<int>(fo * owner_.horizontalZoom_);
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawLine(static_cast<float>(std::max(fx, bounds.getX())), static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getBottom()), 1.0f);
        }
        if (clip.loop.load())
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawText("LOOP", bounds.getRight() - 42, bounds.getY() + 3, 36, 16, juce::Justification::centredRight);
        }
    }

    const int autoMode = owner_.automationModeBox_.getSelectedId();
    if (autoMode == 2 || autoMode == 3)
    {
        const int track = owner_.selectedTrack_.load();
        if (track >= 0 && track < tracks)
        {
            const int y = kRulerHeight + track * rowH;
            const auto& points = autoMode == 2 ? owner_.tracks_[track].volumeAutomation
                                                : owner_.tracks_[track].panAutomation;
            juce::Path path;
            bool started = false;
            g.setColour(owner_.accent_.brighter(0.35f));
            for (const auto& p : points)
            {
                if (!p.active.load()) continue;
                const double sec = owner_.beatToSeconds(p.beat.load());
                const float value = p.value.load();
                const int x = xFromSeconds(sec);
                const float norm = autoMode == 2 ? juce::jlimit(0.0f, 1.0f, value)
                                                  : juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);
                const float py = static_cast<float>(y + rowH - 8) - norm * (rowH - 16);
                if (!started) { path.startNewSubPath(static_cast<float>(x), py); started = true; }
                else path.lineTo(static_cast<float>(x), py);
                g.fillEllipse(static_cast<float>(x) - 3.5f, py - 3.5f, 7.0f, 7.0f);
            }
            if (started) g.strokePath(path, juce::PathStrokeType(1.7f));
        }
    }

    const int playX = xFromSeconds(owner_.playheadSeconds_.load(std::memory_order_acquire));
    g.setColour(juce::Colour(0xffff5366));
    g.drawVerticalLine(playX, 0.0f, static_cast<float>(getHeight()));
    g.fillTriangle(static_cast<float>(playX - 5), 0.0f,
                   static_cast<float>(playX + 5), 0.0f,
                   static_cast<float>(playX), 8.0f);
}

void DawWorkspace::TimelineView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mouseDownX_ = e.x;
    mouseDownY_ = e.y;

    if (e.y >= kRulerHeight && e.x < kTrackHeaderWidth)
    {
        const int track = trackFromY(e.y);
        owner_.selectedTrack_.store(track);
        const int localX = e.x;
        if (localX >= 102 && localX < 126) owner_.toggleTrackFlag(track, 0);
        else if (localX >= 126 && localX < 147) owner_.toggleTrackFlag(track, 1);
        else if (localX >= 147 && localX < 168) owner_.toggleTrackFlag(track, 2);
        else if (localX >= 168) owner_.toggleTrackFlag(track, 3);
        repaint();
        return;
    }

    dragClip_ = owner_.findClipAt(e.x, e.y);
    if (dragClip_ >= 0)
    {
        owner_.selectClip(dragClip_);
        owner_.pushUndoPoint();
        const auto bounds = clipBounds(dragClip_);
        dragOriginalStart_ = owner_.clips_[dragClip_].startSeconds.load();
        dragOriginalSourceStart_ = owner_.clips_[dragClip_].sourceStartSeconds.load();
        dragOriginalLength_ = owner_.clips_[dragClip_].lengthSeconds.load();
        dragOriginalTrack_ = owner_.clips_[dragClip_].track.load();
        if (std::abs(e.x - bounds.getX()) <= 7) dragMode_ = DragMode::TrimLeft;
        else if (std::abs(e.x - bounds.getRight()) <= 7) dragMode_ = DragMode::TrimRight;
        else dragMode_ = DragMode::Move;
        repaint();
        return;
    }

    const int autoMode = owner_.automationModeBox_.getSelectedId();
    if (autoMode == 2 || autoMode == 3)
    {
        owner_.pushUndoPoint();
        const int track = trackFromY(e.y);
        owner_.selectedTrack_.store(track);
        const int rowH = std::max(54, static_cast<int>(84.0f * owner_.verticalZoom_));
        const int rowY = kRulerHeight + track * rowH;
        float norm = 1.0f - juce::jlimit(0.0f, 1.0f, static_cast<float>(e.y - rowY) / rowH);
        const float value = autoMode == 2 ? norm : norm * 2.0f - 1.0f;
        owner_.addAutomationPoint(track, autoMode == 2,
                                  owner_.secondsToBeat(owner_.snapSeconds(secondsFromX(e.x))), value);
        dragMode_ = DragMode::Automation;
        repaint();
    }
    else if (e.y >= kRulerHeight)
    {
        owner_.selectedTrack_.store(trackFromY(e.y));
        owner_.selectClip(-1);
        repaint();
    }
}

void DawWorkspace::TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragClip_ < 0 || dragMode_ == DragMode::None || dragMode_ == DragMode::Automation)
        return;

    auto& clip = owner_.clips_[dragClip_];
    const double delta = static_cast<double>(e.x - mouseDownX_) / owner_.horizontalZoom_;
    if (dragMode_ == DragMode::Move)
    {
        clip.startSeconds.store(owner_.snapSeconds(std::max(0.0, dragOriginalStart_ + delta)));
        clip.track.store(trackFromY(e.y));
    }
    else if (dragMode_ == DragMode::TrimLeft)
    {
        double newStart = owner_.snapSeconds(std::max(0.0, dragOriginalStart_ + delta));
        double shift = newStart - dragOriginalStart_;
        const double maxShift = std::max(0.0, dragOriginalLength_ - 0.02);
        shift = juce::jlimit(-dragOriginalSourceStart_, maxShift, shift);
        clip.startSeconds.store(dragOriginalStart_ + shift);
        clip.sourceStartSeconds.store(dragOriginalSourceStart_ + shift);
        clip.lengthSeconds.store(std::max(0.02, dragOriginalLength_ - shift));
    }
    else if (dragMode_ == DragMode::TrimRight)
    {
        const double maxLength = std::max(0.02, clip.sourceDurationSeconds - dragOriginalSourceStart_);
        clip.lengthSeconds.store(juce::jlimit(0.02, maxLength,
            owner_.snapSeconds(std::max(0.02, dragOriginalLength_ + delta))));
    }
    owner_.dirty_ = true;
    repaint();
}

void DawWorkspace::TimelineView::mouseUp(const juce::MouseEvent&)
{
    if (dragMode_ != DragMode::None && dragMode_ != DragMode::Automation)
        owner_.markDirty();
    dragMode_ = DragMode::None;
    dragClip_ = -1;
}

void DawWorkspace::TimelineView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.x < kTrackHeaderWidth || e.y < kRulerHeight) return;
    const int c = owner_.findClipAt(e.x, e.y);
    if (c >= 0)
    {
        owner_.selectClip(c);
        owner_.splitSelectedClip();
    }
}

void DawWorkspace::TimelineView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        owner_.zoomSlider_.setValue(juce::jlimit(35.0, 420.0,
            owner_.zoomSlider_.getValue() + wheel.deltaY * 60.0));
    }
    else
    {
        juce::Component::mouseWheelMove(e, wheel);
    }
}

DawWorkspace::PatternView::PatternView(DawWorkspace& owner) : owner_(owner) {}

void DawWorkspace::PatternView::paint(juce::Graphics& g)
{
    g.fillAll(owner_.bg_);
    static const std::array<const char*, kPatternLanes> names {
        "KICK", "SNARE", "HI-HAT", "PERC", "TOM", "CLAP", "SHAKER", "FX"
    };
    const int length = owner_.patternLength_.load();
    const int headerW = 110;
    const int top = 40;
    const int rowH = std::max(48, (getHeight() - top - 10) / kPatternLanes);
    const int stepW = std::max(22, (getWidth() - headerW - 18) / std::max(16, length));

    for (int lane = 0; lane < kPatternLanes; ++lane)
    {
        const int y = top + lane * rowH;
        g.setColour(lane % 2 == 0 ? owner_.panel_ : owner_.panel_.darker(0.08f));
        g.fillRect(0, y, getWidth(), rowH);
        g.setColour(owner_.text_);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(names[static_cast<std::size_t>(lane)], 10, y, headerW - 15, rowH, juce::Justification::centredLeft);

        for (int step = 0; step < length; ++step)
        {
            auto r = juce::Rectangle<int>(headerW + step * stepW + 2, y + 8, stepW - 5, rowH - 16);
            const bool on = owner_.patternStepEnabled(lane, step);
            juce::Colour base = (step % 4 == 0 ? owner_.border_.brighter(0.18f) : owner_.border_);
            if (on) base = trackColourForIndex(lane);
            g.setColour(base);
            g.fillRoundedRectangle(r.toFloat(), 3.5f);
            if (on)
            {
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.drawRoundedRectangle(r.toFloat().reduced(1.0f), 3.0f, 1.0f);
            }
        }
    }

    g.setColour(owner_.text_);
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("CHANNEL RACK / STEP SEQUENCER · 1/16", 10, 4, getWidth() - 20, 28, juce::Justification::centredLeft);
}

void DawWorkspace::PatternView::mouseDown(const juce::MouseEvent& e)
{
    const int length = owner_.patternLength_.load();
    const int headerW = 110;
    const int top = 40;
    const int rowH = std::max(48, (getHeight() - top - 10) / kPatternLanes);
    const int stepW = std::max(22, (getWidth() - headerW - 18) / std::max(16, length));
    if (e.x < headerW || e.y < top) return;
    const int lane = (e.y - top) / rowH;
    const int step = (e.x - headerW) / stepW;
    if (lane >= 0 && lane < kPatternLanes && step >= 0 && step < length)
    {
        owner_.pushUndoPoint();
        owner_.togglePatternStep(lane, step);
        owner_.markDirty();
        repaint();
    }
}

DawWorkspace::PianoRollView::PianoRollView(DawWorkspace& owner) : owner_(owner) {}

void DawWorkspace::PianoRollView::paint(juce::Graphics& g)
{
    g.fillAll(owner_.bg_);
    const int keyW = 70;
    const int top = 30;
    const int minNote = 36;
    const int maxNote = 96;
    const int noteCount = maxNote - minNote + 1;
    const float rowH = static_cast<float>(getHeight() - top) / noteCount;
    const float pixelsPerBeat = std::max(34.0f, owner_.horizontalZoom_ * static_cast<float>(owner_.beatToSeconds(1.0)));

    for (int note = minNote; note <= maxNote; ++note)
    {
        const int row = maxNote - note;
        const float y = top + row * rowH;
        const int pc = note % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour(black ? owner_.panel_.darker(0.18f) : owner_.panel_);
        g.fillRect(0.0f, y, static_cast<float>(getWidth()), rowH + 1.0f);
        g.setColour(owner_.border_.withAlpha(0.45f));
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
        if (pc == 0)
        {
            g.setColour(owner_.text_.withAlpha(0.65f));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("C" + juce::String(note / 12 - 1), 7, static_cast<int>(y), keyW - 12,
                       std::max(10, static_cast<int>(rowH)), juce::Justification::centredLeft);
        }
    }
    g.setColour(owner_.border_);
    g.drawVerticalLine(keyW, static_cast<float>(top), static_cast<float>(getHeight()));

    for (int beat = 0; beat < 256; ++beat)
    {
        const int x = keyW + static_cast<int>(beat * pixelsPerBeat);
        if (x > getWidth()) break;
        g.setColour((beat % 4 == 0 ? owner_.border_.brighter(0.25f) : owner_.border_).withAlpha(beat % 4 == 0 ? 0.8f : 0.35f));
        g.drawVerticalLine(x, static_cast<float>(top), static_cast<float>(getHeight()));
        if (beat % 4 == 0)
        {
            g.setColour(owner_.text_.withAlpha(0.75f));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(juce::String(beat / 4 + 1), x + 3, 3, 35, 20, juce::Justification::centredLeft);
        }
    }

    for (int i = 0; i < kMaxMidiNotes; ++i)
    {
        const auto& n = owner_.midiNotes_[i];
        if (!n.active.load()) continue;
        const int note = n.note.load();
        if (note < minNote || note > maxNote) continue;
        const float y = top + (maxNote - note) * rowH + 1.0f;
        const int x = keyW + static_cast<int>(n.startBeat.load() * pixelsPerBeat);
        const int w = std::max(5, static_cast<int>(n.lengthBeats.load() * pixelsPerBeat));
        auto rect = juce::Rectangle<float>(static_cast<float>(x), y, static_cast<float>(w), std::max(4.0f, rowH - 2.0f));
        g.setColour(owner_.accent_.withAlpha(0.82f));
        g.fillRoundedRectangle(rect, 2.5f);
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.drawRoundedRectangle(rect, 2.5f, 1.0f);
    }

    const double beat = owner_.secondsToBeat(owner_.playheadSeconds_.load());
    const int playX = keyW + static_cast<int>(beat * pixelsPerBeat);
    g.setColour(juce::Colour(0xffff5366));
    g.drawVerticalLine(playX, 0.0f, static_cast<float>(getHeight()));
}

void DawWorkspace::PianoRollView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        const int n = owner_.noteAt(e.x, e.y);
        if (n >= 0)
        {
            owner_.pushUndoPoint();
            owner_.deleteMidiNote(n);
            owner_.markDirty();
            repaint();
        }
        return;
    }

    draggingNote_ = owner_.noteAt(e.x, e.y);
    if (draggingNote_ >= 0)
    {
        owner_.pushUndoPoint();
        return;
    }

    const int keyW = 70;
    const int top = 30;
    const int minNote = 36;
    const int maxNote = 96;
    const int noteCount = maxNote - minNote + 1;
    if (e.x < keyW || e.y < top) return;
    const float rowH = static_cast<float>(getHeight() - top) / noteCount;
    const float pixelsPerBeat = std::max(34.0f, owner_.horizontalZoom_ * static_cast<float>(owner_.beatToSeconds(1.0)));
    const int row = juce::jlimit(0, noteCount - 1, static_cast<int>((e.y - top) / rowH));
    const int note = maxNote - row;
    double beat = (e.x - keyW) / pixelsPerBeat;
    beat = std::round(beat * 4.0) / 4.0;
    owner_.pushUndoPoint();
    owner_.addMidiNote(note, std::max(0.0, beat), 1.0, 0.82f);
    owner_.markDirty();
    repaint();
}

void DawWorkspace::PianoRollView::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingNote_ < 0 || draggingNote_ >= kMaxMidiNotes || !owner_.midiNotes_[draggingNote_].active.load())
        return;
    const int keyW = 70;
    const int top = 30;
    const int minNote = 36;
    const int maxNote = 96;
    const int noteCount = maxNote - minNote + 1;
    const float rowH = static_cast<float>(getHeight() - top) / noteCount;
    const float pixelsPerBeat = std::max(34.0f, owner_.horizontalZoom_ * static_cast<float>(owner_.beatToSeconds(1.0)));
    const int row = juce::jlimit(0, noteCount - 1, static_cast<int>((e.y - top) / rowH));
    owner_.midiNotes_[draggingNote_].note.store(maxNote - row);
    double beat = std::max(0.0, (e.x - keyW) / pixelsPerBeat);
    owner_.midiNotes_[draggingNote_].startBeat.store(std::round(beat * 4.0) / 4.0);
    owner_.dirty_ = true;
    repaint();
}

DawWorkspace::PluginEditorHolder::PluginEditorHolder(std::shared_ptr<juce::AudioPluginInstance> plugin)
    : plugin_(std::move(plugin))
{
    if (plugin_ != nullptr)
    {
        if (plugin_->hasEditor())
            editor_.reset(plugin_->createEditorIfNeeded());
        if (editor_ == nullptr)
            editor_ = std::make_unique<juce::GenericAudioProcessorEditor>(*plugin_);
        addAndMakeVisible(*editor_);
        setSize(juce::jlimit(520, 1280, editor_->getWidth()),
                juce::jlimit(420, 900, editor_->getHeight()));
    }
}

DawWorkspace::PluginEditorHolder::~PluginEditorHolder() = default;

void DawWorkspace::PluginEditorHolder::resized()
{
    if (editor_ != nullptr) editor_->setBounds(getLocalBounds());
}

DawWorkspace::DawWorkspace()
    : timeline_(*this), patternView_(*this), pianoRollView_(*this)
{
    formatManager_.registerBasicFormats();
    instrumentFormatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    initialiseTracks();
    initialiseUi();
    browserThread_.startThread(3);
    startTimerHz(30);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

DawWorkspace::~DawWorkspace()
{
    stopTimer();
    removeKeyListener(this);
    playing_.store(false);
    instrumentPlugin_.store(nullptr, std::memory_order_release);
    browserThread_.stopThread(1500);
}

void DawWorkspace::initialiseTracks()
{
    for (int i = 0; i < kMaxTracks; ++i)
    {
        tracks_[i].name = i < 8 ? juce::StringArray{"Voz Lider","Coros","Guitarra","Bajo","Teclado","Bateria","Secuencias","Click"}[i]
                                : "Track " + juce::String(i + 1);
        tracks_[i].colour = trackColourForIndex(i);
        tracks_[i].gain.store(i == 0 ? 0.9f : 0.75f);
        tracks_[i].pan.store(0.0f);
        for (auto& p : tracks_[i].volumeAutomation) p.active.store(false);
        for (auto& p : tracks_[i].panAutomation) p.active.store(false);
    }
    for (auto& lane : patternSteps_) for (auto& step : lane) step.store(false);
    patternSteps_[0][0].store(true); patternSteps_[0][8].store(true);
    patternSteps_[1][4].store(true); patternSteps_[1][12].store(true);
    for (int s = 0; s < 16; s += 2) patternSteps_[2][s].store(true);
    for (auto& n : midiNotes_) n.active.store(false);
}

void DawWorkspace::initialiseUi()
{
    auto configureButton = [this](juce::Button& b)
    {
        addAndMakeVisible(b);
        b.setColour(juce::TextButton::buttonColourId, panel_.brighter(0.08f));
        b.setColour(juce::TextButton::textColourOffId, text_);
    };
    for (auto* b : { &newButton_, &openButton_, &saveButton_, &saveAsButton_, &addTrackButton_, &removeTrackButton_,
                     &splitButton_, &duplicateButton_, &deleteButton_, &loopClipButton_, &muteClipButton_,
                     &fadeInButton_, &fadeOutButton_, &rewindButton_, &stopButton_, &playButton_, &recordButton_ })
        configureButton(*b);
    addAndMakeVisible(loopButton_);
    addAndMakeVisible(snapButton_);
    snapButton_.setToggleState(true, juce::dontSendNotification);

    newButton_.onClick = [this] { newProject(); };
    openButton_.onClick = [this] { openProject(); };
    saveButton_.onClick = [this] { saveProject(); };
    saveAsButton_.onClick = [this] { saveProjectAs(); };
    addTrackButton_.onClick = [this] { addTrack(); };
    removeTrackButton_.onClick = [this] { removeSelectedTrack(); };
    splitButton_.onClick = [this] { splitSelectedClip(); };
    duplicateButton_.onClick = [this] { duplicateSelectedClip(); };
    deleteButton_.onClick = [this] { deleteSelectedClip(); };
    loopClipButton_.onClick = [this] { toggleSelectedClipLoop(); };
    muteClipButton_.onClick = [this] { toggleSelectedClipMute(); };
    fadeInButton_.onClick = [this] { nudgeFade(true, 0.1); };
    fadeOutButton_.onClick = [this] { nudgeFade(false, 0.1); };
    rewindButton_.onClick = [this] { rewind(); };
    stopButton_.onClick = [this] { stop(); };
    playButton_.onClick = [this] { togglePlay(); };
    recordButton_.onClick = [this] { requestRecord(); };
    loopButton_.onClick = [this] { toggleLoop(); };

    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    bpmSlider_.setRange(40.0, 240.0, 0.1);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.onValueChange = [this] { bpm_.store(bpmSlider_.getValue()); markDirty(); repaint(); };
    addAndMakeVisible(bpmSlider_);

    zoomSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider_.setRange(35.0, 420.0, 1.0);
    zoomSlider_.setValue(horizontalZoom_, juce::dontSendNotification);
    zoomSlider_.onValueChange = [this]
    {
        horizontalZoom_ = static_cast<float>(zoomSlider_.getValue());
        const int w = kTrackHeaderWidth + static_cast<int>(kTimelineSeconds * horizontalZoom_);
        timeline_.setSize(w, timeline_.getHeight());
        const int prW = 70 + static_cast<int>(256.0 * std::max(34.0f, horizontalZoom_ * static_cast<float>(beatToSeconds(1.0))));
        pianoRollView_.setSize(prW, pianoRollView_.getHeight());
        timeline_.repaint(); pianoRollView_.repaint();
    };
    addAndMakeVisible(zoomSlider_);

    verticalZoomSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    verticalZoomSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    verticalZoomSlider_.setRange(0.65, 1.8, 0.01);
    verticalZoomSlider_.setValue(verticalZoom_, juce::dontSendNotification);
    verticalZoomSlider_.onValueChange = [this]
    {
        verticalZoom_ = static_cast<float>(verticalZoomSlider_.getValue());
        const int rowH = std::max(54, static_cast<int>(84.0f * verticalZoom_));
        timeline_.setSize(timeline_.getWidth(), kRulerHeight + trackCount_.load() * rowH + 20);
        timeline_.repaint();
    };
    addAndMakeVisible(verticalZoomSlider_);

    positionLabel_.setColour(juce::Label::textColourId, text_);
    positionLabel_.setJustificationType(juce::Justification::centred);
    positionLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(positionLabel_);
    projectLabel_.setColour(juce::Label::textColourId, text_);
    projectLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(projectLabel_);
    hintLabel_.setColour(juce::Label::textColourId, text_.withAlpha(0.62f));
    hintLabel_.setText("Arrastrá WAV / MP3 / FLAC / AIFF · bordes=trim · doble click=split · Ctrl+rueda=zoom", juce::dontSendNotification);
    addAndMakeVisible(hintLabel_);

    editorTabs_.setTabBarDepth(35);
    editorTabs_.addTab("ARRANGER / PLAYLIST", panel_, &arrangerPage_, false);
    editorTabs_.addTab("PATTERNS / CHANNEL RACK", panel_, &patternPage_, false);
    editorTabs_.addTab("PIANO ROLL / MIDI", panel_, &pianoPage_, false);
    addAndMakeVisible(editorTabs_);

    timelineViewport_.setViewedComponent(&timeline_, false);
    timelineViewport_.setScrollBarsShown(true, true);
    arrangerPage_.addAndMakeVisible(timelineViewport_);

    patternViewport_.setViewedComponent(&patternView_, false);
    patternViewport_.setScrollBarsShown(true, false);
    patternPage_.addAndMakeVisible(patternViewport_);

    pianoViewport_.setViewedComponent(&pianoRollView_, false);
    pianoViewport_.setScrollBarsShown(true, true);
    pianoPage_.addAndMakeVisible(pianoViewport_);

    patternLengthBox_.addItem("16 STEPS", 16);
    patternLengthBox_.addItem("32 STEPS", 32);
    patternLengthBox_.addItem("64 STEPS", 64);
    patternLengthBox_.setSelectedId(16, juce::dontSendNotification);
    patternLengthBox_.onChange = [this]
    {
        const int id = patternLengthBox_.getSelectedId();
        patternLength_.store(id == 32 ? 32 : (id == 64 ? 64 : 16));
        lastPatternStep_ = -1;
        const int w = std::max(patternViewport_.getWidth(), 130 + patternLength_.load() * 24);
        patternView_.setSize(w, std::max(500, patternViewport_.getHeight()));
        patternView_.repaint();
        markDirty();
    };
    patternPage_.addAndMakeVisible(patternLengthBox_);

    automationModeBox_.addItem("AUTOMATION: OFF", 1);
    automationModeBox_.addItem("AUTOMATION: VOLUME", 2);
    automationModeBox_.addItem("AUTOMATION: PAN", 3);
    automationModeBox_.setSelectedId(1, juce::dontSendNotification);
    automationModeBox_.onChange = [this] { timeline_.repaint(); };
    arrangerPage_.addAndMakeVisible(automationModeBox_);
    automationLabel_.setText("Doble click/click en lane para crear puntos · curvas lineales", juce::dontSendNotification);
    automationLabel_.setColour(juce::Label::textColourId, text_.withAlpha(0.65f));
    arrangerPage_.addAndMakeVisible(automationLabel_);

    instrumentBox_.setTextWhenNothingSelected("VST3 INSTRUMENT");
    pianoPage_.addAndMakeVisible(instrumentBox_);
    for (auto* b : { &scanInstrumentButton_, &loadInstrumentButton_, &openInstrumentButton_, &removeInstrumentButton_ })
        pianoPage_.addAndMakeVisible(*b);
    pianoPage_.addAndMakeVisible(bypassInstrumentButton_);
    pianoPage_.addAndMakeVisible(instrumentStatus_);
    instrumentStatus_.setColour(juce::Label::textColourId, text_.withAlpha(0.75f));
    instrumentStatus_.setJustificationType(juce::Justification::centredLeft);
    scanInstrumentButton_.onClick = [this] { scanInstrumentPlugins(); };
    loadInstrumentButton_.onClick = [this] { loadSelectedInstrument(); };
    openInstrumentButton_.onClick = [this] { openInstrumentEditor(); };
    removeInstrumentButton_.onClick = [this] { removeInstrument(); };
    bypassInstrumentButton_.onClick = [this] { instrumentBypass_.store(bypassInstrumentButton_.getToggleState()); markDirty(); };
    refreshInstrumentUi();

    browserRootBox_.addItem("SAMPLES / DOCUMENTOS", 1);
    browserRootBox_.addItem("GRABACIONES J3", 2);
    browserRootBox_.addItem("PROYECTOS J3", 3);
    browserRootBox_.setSelectedId(1, juce::dontSendNotification);
    browserRootBox_.onChange = [this] { refreshBrowserRoot(); };
    arrangerPage_.addAndMakeVisible(browserRootBox_);
    arrangerPage_.addAndMakeVisible(browserAddButton_);
    browserAddButton_.onClick = [this] { addBrowserSelection(); };

    directoryList_ = std::make_unique<juce::DirectoryContentsList>(nullptr, browserThread_);
    fileTree_ = std::make_unique<juce::FileTreeComponent>(*directoryList_);
    fileTree_->setColour(juce::TreeView::backgroundColourId, panel_.darker(0.05f));
    arrangerPage_.addAndMakeVisible(*fileTree_);
    refreshBrowserRoot();

    updateProjectLabels();
}

void DawWorkspace::paint(juce::Graphics& g)
{
    g.fillAll(bg_);
    g.setColour(panel_);
    g.fillRect(0, 0, getWidth(), 82);
    g.setColour(border_);
    g.drawHorizontalLine(81, 0.0f, static_cast<float>(getWidth()));
}

void DawWorkspace::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(82).reduced(6, 5);
    projectLabel_.setBounds(top.removeFromLeft(230));
    top.removeFromLeft(6);
    for (auto* b : { &newButton_, &openButton_, &saveButton_, &saveAsButton_ })
    {
        b->setBounds(top.removeFromLeft(b == &saveAsButton_ ? 100 : 72).reduced(2));
    }
    top.removeFromLeft(8);
    addTrackButton_.setBounds(top.removeFromLeft(76).reduced(2));
    removeTrackButton_.setBounds(top.removeFromLeft(76).reduced(2));
    top.removeFromLeft(8);
    splitButton_.setBounds(top.removeFromLeft(72).reduced(2));
    duplicateButton_.setBounds(top.removeFromLeft(82).reduced(2));
    deleteButton_.setBounds(top.removeFromLeft(70).reduced(2));
    loopClipButton_.setBounds(top.removeFromLeft(78).reduced(2));
    muteClipButton_.setBounds(top.removeFromLeft(80).reduced(2));
    fadeInButton_.setBounds(top.removeFromLeft(82).reduced(2));
    fadeOutButton_.setBounds(top.removeFromLeft(88).reduced(2));

    auto transport = getLocalBounds().removeFromTop(82).removeFromBottom(35).reduced(6, 2);
    rewindButton_.setBounds(transport.removeFromLeft(48).reduced(2));
    stopButton_.setBounds(transport.removeFromLeft(56).reduced(2));
    playButton_.setBounds(transport.removeFromLeft(58).reduced(2));
    recordButton_.setBounds(transport.removeFromLeft(52).reduced(2));
    loopButton_.setBounds(transport.removeFromLeft(58));
    snapButton_.setBounds(transport.removeFromLeft(58));
    bpmSlider_.setBounds(transport.removeFromLeft(170).reduced(4, 0));
    zoomSlider_.setBounds(transport.removeFromLeft(120).reduced(4, 0));
    verticalZoomSlider_.setBounds(transport.removeFromLeft(90).reduced(4, 0));
    positionLabel_.setBounds(transport.removeFromLeft(150));
    hintLabel_.setBounds(transport);

    editorTabs_.setBounds(r);

    auto a = arrangerPage_.getLocalBounds().reduced(5);
    auto browser = a.removeFromLeft(205);
    browserRootBox_.setBounds(browser.removeFromTop(30));
    browser.removeFromTop(4);
    browserAddButton_.setBounds(browser.removeFromBottom(32));
    browser.removeFromBottom(4);
    if (fileTree_) fileTree_->setBounds(browser);
    a.removeFromLeft(5);
    auto autoRow = a.removeFromTop(32);
    automationModeBox_.setBounds(autoRow.removeFromLeft(200));
    automationLabel_.setBounds(autoRow);
    a.removeFromTop(4);
    timelineViewport_.setBounds(a);

    const int rowH = std::max(54, static_cast<int>(84.0f * verticalZoom_));
    timeline_.setSize(kTrackHeaderWidth + static_cast<int>(kTimelineSeconds * horizontalZoom_),
                      kRulerHeight + trackCount_.load() * rowH + 20);

    auto p = patternPage_.getLocalBounds().reduced(8);
    patternLengthBox_.setBounds(p.removeFromTop(32).removeFromLeft(140));
    p.removeFromTop(4);
    patternViewport_.setBounds(p);
    patternView_.setSize(std::max(p.getWidth(), 130 + patternLength_.load() * 24), std::max(500, p.getHeight()));

    auto pr = pianoPage_.getLocalBounds().reduced(6);
    auto instr = pr.removeFromTop(38);
    instrumentBox_.setBounds(instr.removeFromLeft(290).reduced(2));
    scanInstrumentButton_.setBounds(instr.removeFromLeft(130).reduced(2));
    loadInstrumentButton_.setBounds(instr.removeFromLeft(75).reduced(2));
    openInstrumentButton_.setBounds(instr.removeFromLeft(80).reduced(2));
    removeInstrumentButton_.setBounds(instr.removeFromLeft(70).reduced(2));
    bypassInstrumentButton_.setBounds(instr.removeFromLeft(80));
    instrumentStatus_.setBounds(instr);
    pr.removeFromTop(4);
    pianoViewport_.setBounds(pr);
    const float ppb = std::max(34.0f, horizontalZoom_ * static_cast<float>(beatToSeconds(1.0)));
    pianoRollView_.setSize(70 + static_cast<int>(256.0f * ppb), std::max(900, pr.getHeight()));
}

void DawWorkspace::setThemeColours(juce::Colour background, juce::Colour panel, juce::Colour accent,
                                   juce::Colour text, juce::Colour border)
{
    bg_ = background; panel_ = panel; accent_ = accent; text_ = text; border_ = border;
    for (auto* b : { &newButton_, &openButton_, &saveButton_, &saveAsButton_, &addTrackButton_, &removeTrackButton_,
                     &splitButton_, &duplicateButton_, &deleteButton_, &loopClipButton_, &muteClipButton_,
                     &fadeInButton_, &fadeOutButton_, &rewindButton_, &stopButton_, &playButton_, &recordButton_ })
    {
        b->setColour(juce::TextButton::buttonColourId, panel_.brighter(0.08f));
        b->setColour(juce::TextButton::textColourOffId, text_);
    }
    if (fileTree_) fileTree_->setColour(juce::TreeView::backgroundColourId, panel_.darker(0.05f));
    repaint();
    timeline_.repaint(); patternView_.repaint(); pianoRollView_.repaint();
}

void DawWorkspace::prepare(double sampleRate, int blockSize)
{
    sampleRate_.store(std::max(8000.0, sampleRate));
    blockSize_.store(std::max(32, blockSize));
    instrumentScratch_.setSize(2, std::max(2048, blockSize), false, true, false);
    instrumentScratch_.clear();
    instrumentMidi_.ensureSize(8192);
    if (auto* plugin = instrumentPlugin_.load(std::memory_order_acquire))
    {
        plugin->setRateAndBufferSizeDetails(sampleRate_.load(), blockSize_.load());
        plugin->prepareToPlay(sampleRate_.load(), blockSize_.load());
    }
    resetRealtimeState();
}

void DawWorkspace::releaseResources()
{
    playing_.store(false);
    if (auto* plugin = instrumentPlugin_.load(std::memory_order_acquire))
        plugin->releaseResources();
}

void DawWorkspace::resetRealtimeState() noexcept
{
    lastPatternStep_ = -1;
    for (auto& v : drumVoices_) v.active = false;
    midiNoteWasOn_.fill(false);
}

void DawWorkspace::play()
{
    resetRealtimeState();
    playing_.store(true, std::memory_order_release);
    playButton_.setButtonText("PAUSE");
}

void DawWorkspace::stop()
{
    playing_.store(false, std::memory_order_release);
    playButton_.setButtonText("PLAY");
    resetRealtimeState();
}

void DawWorkspace::togglePlay()
{
    if (playing_.load()) stop(); else play();
}

void DawWorkspace::rewind()
{
    playheadSeconds_.store(0.0);
    resetRealtimeState();
    timeline_.repaint(); pianoRollView_.repaint();
}

void DawWorkspace::toggleLoop()
{
    loopEnabled_.store(loopButton_.getToggleState());
}

void DawWorkspace::requestRecord()
{
    recordRequested_.store(!recordRequested_.load());
    if (onRecordRequested) onRecordRequested();
}

void DawWorkspace::setExternalRecordState(bool recording) noexcept
{
    recordRequested_.store(recording);
}

bool DawWorkspace::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& p : files)
        if (isAudioExtension(juce::File(p))) return true;
    return false;
}

void DawWorkspace::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x);
    int track = selectedTrack_.load();
    double start = playheadSeconds_.load();
    if (editorTabs_.getCurrentTabIndex() == 0)
    {
        const auto screen = arrangerPage_.getLocalPoint(this, { x, y });
        const auto viewPoint = timeline_.getLocalPoint(&arrangerPage_, screen);
        if (viewPoint.x >= kTrackHeaderWidth)
            start = snapSeconds(timeline_.secondsFromX(viewPoint.x));
        if (viewPoint.y >= kRulerHeight)
            track = timeline_.trackFromY(viewPoint.y);
    }
    for (const auto& path : files)
    {
        juce::File f(path);
        if (isAudioExtension(f))
        {
            importAudioFile(f, track, start);
            start += 0.25;
        }
    }
}

int DawWorkspace::findFreeClip() const noexcept
{
    for (int i = 0; i < kMaxClips; ++i)
        if (!clips_[i].active.load(std::memory_order_acquire)) return i;
    return -1;
}

void DawWorkspace::importAudioFile(const juce::File& file, int targetTrack, double startSeconds)
{
    if (!file.existsAsFile() || !isAudioExtension(file)) return;
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return;

    const int slot = findFreeClip();
    if (slot < 0)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "J3 Worship",
                                               "El proyecto alcanzó el límite de clips activos.");
        return;
    }

    pushUndoPoint();
    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int samples = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max() - 1));
    auto buffer = std::make_unique<juce::AudioBuffer<float>>(channels, samples);
    buffer->clear();
    if (!reader->read(buffer.get(), 0, samples, 0, true, channels > 1))
        return;

    auto* raw = buffer.get();
    ownedAudioBuffers_.push_back(std::move(buffer));

    auto& clip = clips_[slot];
    clip.active.store(false, std::memory_order_release);
    clip.track.store(juce::jlimit(0, trackCount_.load() - 1, targetTrack));
    clip.startSeconds.store(snapSeconds(std::max(0.0, startSeconds)));
    clip.sourceStartSeconds.store(0.0);
    clip.lengthSeconds.store(static_cast<double>(samples) / reader->sampleRate);
    clip.gain.store(1.0f);
    clip.fadeInSeconds.store(0.0);
    clip.fadeOutSeconds.store(0.0);
    clip.loop.store(false);
    clip.mute.store(false);
    clip.sourceSampleRate = reader->sampleRate;
    clip.sourceDurationSeconds = static_cast<double>(samples) / reader->sampleRate;
    clip.sourceFile = file;
    clip.name = file.getFileNameWithoutExtension();
    clip.colour = tracks_[clip.track.load()].colour;
    clip.buffer.store(raw, std::memory_order_release);
    clip.active.store(true, std::memory_order_release);
    selectClip(slot);
    markDirty();
    timeline_.repaint();
}

int DawWorkspace::findClipAt(int x, int y) const
{
    for (int i = kMaxClips - 1; i >= 0; --i)
        if (clips_[i].active.load() && timeline_.clipBounds(i).contains(x, y)) return i;
    return -1;
}

void DawWorkspace::selectClip(int index)
{
    selectedClip_ = (index >= 0 && index < kMaxClips && clips_[index].active.load()) ? index : -1;
    if (selectedClip_ >= 0) selectedTrack_.store(clips_[selectedClip_].track.load());
    timeline_.repaint();
}

void DawWorkspace::deleteSelectedClip()
{
    if (selectedClip_ < 0) return;
    pushUndoPoint();
    clips_[selectedClip_].active.store(false, std::memory_order_release);
    selectedClip_ = -1;
    markDirty(); timeline_.repaint();
}

void DawWorkspace::duplicateSelectedClip()
{
    if (selectedClip_ < 0 || !clips_[selectedClip_].active.load()) return;
    const int dst = findFreeClip();
    if (dst < 0) return;
    pushUndoPoint();
    const auto& src = clips_[selectedClip_];
    auto& c = clips_[dst];
    c.active.store(false);
    c.track.store(src.track.load());
    c.startSeconds.store(snapSeconds(src.startSeconds.load() + src.lengthSeconds.load()));
    c.sourceStartSeconds.store(src.sourceStartSeconds.load());
    c.lengthSeconds.store(src.lengthSeconds.load());
    c.gain.store(src.gain.load());
    c.fadeInSeconds.store(src.fadeInSeconds.load());
    c.fadeOutSeconds.store(src.fadeOutSeconds.load());
    c.loop.store(src.loop.load());
    c.mute.store(src.mute.load());
    c.sourceSampleRate = src.sourceSampleRate;
    c.sourceDurationSeconds = src.sourceDurationSeconds;
    c.sourceFile = src.sourceFile;
    c.name = src.name + " copy";
    c.colour = src.colour;
    c.buffer.store(src.buffer.load(std::memory_order_acquire));
    c.active.store(true, std::memory_order_release);
    selectClip(dst); markDirty(); timeline_.repaint();
}

void DawWorkspace::splitSelectedClip()
{
    if (selectedClip_ < 0 || !clips_[selectedClip_].active.load()) return;
    const int dst = findFreeClip();
    if (dst < 0) return;
    auto& src = clips_[selectedClip_];
    const double start = src.startSeconds.load();
    const double len = src.lengthSeconds.load();
    double split = playheadSeconds_.load();
    if (split <= start + 0.02 || split >= start + len - 0.02)
        split = start + len * 0.5;
    split = snapSeconds(split);
    if (split <= start + 0.02 || split >= start + len - 0.02) return;

    pushUndoPoint();
    const double leftLen = split - start;
    const double rightLen = len - leftLen;
    auto& c = clips_[dst];
    c.active.store(false);
    c.track.store(src.track.load());
    c.startSeconds.store(split);
    c.sourceStartSeconds.store(src.sourceStartSeconds.load() + leftLen);
    c.lengthSeconds.store(rightLen);
    c.gain.store(src.gain.load());
    c.fadeInSeconds.store(0.0);
    c.fadeOutSeconds.store(src.fadeOutSeconds.load());
    c.loop.store(src.loop.load());
    c.mute.store(src.mute.load());
    c.sourceSampleRate = src.sourceSampleRate;
    c.sourceDurationSeconds = src.sourceDurationSeconds;
    c.sourceFile = src.sourceFile;
    c.name = src.name + " B";
    c.colour = src.colour;
    c.buffer.store(src.buffer.load());
    src.lengthSeconds.store(leftLen);
    src.fadeOutSeconds.store(0.0);
    c.active.store(true, std::memory_order_release);
    selectClip(dst); markDirty(); timeline_.repaint();
}

void DawWorkspace::copySelectedClip() { copiedClip_ = selectedClip_; }

void DawWorkspace::pasteClip()
{
    if (copiedClip_ < 0 || !clips_[copiedClip_].active.load()) return;
    const int previous = selectedClip_;
    selectedClip_ = copiedClip_;
    duplicateSelectedClip();
    if (selectedClip_ >= 0) clips_[selectedClip_].startSeconds.store(snapSeconds(playheadSeconds_.load()));
    if (selectedClip_ < 0) selectedClip_ = previous;
    markDirty(); timeline_.repaint();
}

void DawWorkspace::nudgeFade(bool fadeIn, double amount)
{
    if (selectedClip_ < 0) return;
    pushUndoPoint();
    auto& c = clips_[selectedClip_];
    const double maxFade = std::max(0.0, c.lengthSeconds.load() * 0.5);
    auto& a = fadeIn ? c.fadeInSeconds : c.fadeOutSeconds;
    a.store(juce::jlimit(0.0, maxFade, a.load() + amount));
    markDirty(); timeline_.repaint();
}

void DawWorkspace::toggleSelectedClipLoop()
{
    if (selectedClip_ < 0) return;
    pushUndoPoint();
    clips_[selectedClip_].loop.store(!clips_[selectedClip_].loop.load());
    markDirty(); timeline_.repaint();
}

void DawWorkspace::toggleSelectedClipMute()
{
    if (selectedClip_ < 0) return;
    pushUndoPoint();
    clips_[selectedClip_].mute.store(!clips_[selectedClip_].mute.load());
    markDirty(); timeline_.repaint();
}

double DawWorkspace::snapSeconds(double seconds) const noexcept
{
    if (!snapButton_.getToggleState()) return std::max(0.0, seconds);
    const double grid = beatToSeconds(0.25);
    return std::max(0.0, std::round(seconds / grid) * grid);
}

double DawWorkspace::beatToSeconds(double beat) const noexcept
{
    return beat * 60.0 / std::max(20.0, bpm_.load(std::memory_order_relaxed));
}

double DawWorkspace::secondsToBeat(double seconds) const noexcept
{
    return seconds * std::max(20.0, bpm_.load(std::memory_order_relaxed)) / 60.0;
}

void DawWorkspace::addTrack()
{
    const int count = trackCount_.load();
    if (count >= kMaxTracks) return;
    pushUndoPoint();
    tracks_[count].name = "Track " + juce::String(count + 1);
    tracks_[count].colour = trackColourForIndex(count);
    trackCount_.store(count + 1);
    selectedTrack_.store(count);
    markDirty(); resized(); timeline_.repaint();
}

void DawWorkspace::removeSelectedTrack()
{
    const int count = trackCount_.load();
    if (count <= 1) return;
    const int track = juce::jlimit(0, count - 1, selectedTrack_.load());
    pushUndoPoint();
    for (auto& c : clips_)
    {
        if (!c.active.load()) continue;
        const int t = c.track.load();
        if (t == track) c.active.store(false, std::memory_order_release);
        else if (t > track) c.track.store(t - 1);
    }
    for (int t = track; t < count - 1; ++t)
    {
        tracks_[t].name = tracks_[t + 1].name;
        tracks_[t].colour = tracks_[t + 1].colour;
        tracks_[t].gain.store(tracks_[t + 1].gain.load());
        tracks_[t].pan.store(tracks_[t + 1].pan.load());
        tracks_[t].mute.store(tracks_[t + 1].mute.load());
        tracks_[t].solo.store(tracks_[t + 1].solo.load());
        tracks_[t].arm.store(tracks_[t + 1].arm.load());
        tracks_[t].monitor.store(tracks_[t + 1].monitor.load());
    }
    trackCount_.store(count - 1);
    selectedTrack_.store(std::min(track, count - 2));
    selectedClip_ = -1;
    markDirty(); resized(); timeline_.repaint();
}

void DawWorkspace::toggleTrackFlag(int track, int flag)
{
    if (track < 0 || track >= trackCount_.load()) return;
    pushUndoPoint();
    if (flag == 0) tracks_[track].mute.store(!tracks_[track].mute.load());
    else if (flag == 1) tracks_[track].solo.store(!tracks_[track].solo.load());
    else if (flag == 2) tracks_[track].arm.store(!tracks_[track].arm.load());
    else tracks_[track].monitor.store(!tracks_[track].monitor.load());
    markDirty();
}

void DawWorkspace::setTrackGainPan(int track, float gain, float pan)
{
    if (track < 0 || track >= trackCount_.load()) return;
    tracks_[track].gain.store(juce::jlimit(0.0f, 2.0f, gain));
    tracks_[track].pan.store(juce::jlimit(-1.0f, 1.0f, pan));
    markDirty();
}

void DawWorkspace::addAutomationPoint(int track, bool volume, double beat, float value)
{
    if (track < 0 || track >= trackCount_.load()) return;
    auto& points = volume ? tracks_[track].volumeAutomation : tracks_[track].panAutomation;
    for (auto& p : points)
    {
        if (!p.active.load())
        {
            p.beat.store(std::max(0.0, beat));
            p.value.store(value);
            p.active.store(true, std::memory_order_release);
            markDirty();
            return;
        }
    }
}

float DawWorkspace::evaluateAutomation(const std::array<AutomationPoint, kMaxAutomationPoints>& points,
                                       double beat, float fallback) const noexcept
{
    const AutomationPoint* before = nullptr;
    const AutomationPoint* after = nullptr;
    double beforeBeat = -1.0e30;
    double afterBeat = 1.0e30;
    for (const auto& p : points)
    {
        if (!p.active.load(std::memory_order_acquire)) continue;
        const double b = p.beat.load(std::memory_order_relaxed);
        if (b <= beat && b > beforeBeat) { before = &p; beforeBeat = b; }
        if (b >= beat && b < afterBeat) { after = &p; afterBeat = b; }
    }
    if (before == nullptr && after == nullptr) return fallback;
    if (before == nullptr) return after->value.load();
    if (after == nullptr) return before->value.load();
    if (std::abs(afterBeat - beforeBeat) < 1.0e-9) return before->value.load();
    const float a = before->value.load();
    const float b = after->value.load();
    const float x = static_cast<float>((beat - beforeBeat) / (afterBeat - beforeBeat));
    return a + (b - a) * x;
}

void DawWorkspace::togglePatternStep(int lane, int step)
{
    if (lane < 0 || lane >= kPatternLanes || step < 0 || step >= kPatternSteps) return;
    patternSteps_[lane][step].store(!patternSteps_[lane][step].load());
}

bool DawWorkspace::patternStepEnabled(int lane, int step) const noexcept
{
    return lane >= 0 && lane < kPatternLanes && step >= 0 && step < kPatternSteps
        && patternSteps_[lane][step].load(std::memory_order_relaxed);
}

void DawWorkspace::triggerDrumVoice(int lane) noexcept
{
    for (auto& v : drumVoices_)
    {
        if (!v.active)
        {
            v.active = true;
            v.lane = lane;
            v.phase = 0.0;
            v.env = 1.0;
            v.noise ^= static_cast<std::uint32_t>((lane + 1) * 0x9e3779b9u);
            return;
        }
    }
}

float DawWorkspace::renderDrums(double sampleRate) noexcept
{
    float out = 0.0f;
    const double sr = std::max(8000.0, sampleRate);
    for (auto& v : drumVoices_)
    {
        if (!v.active) continue;
        float sample = 0.0f;
        const double p = v.phase;
        if (v.lane == 0)
        {
            const double freq = 48.0 + 70.0 * v.env;
            sample = static_cast<float>(std::sin(p) * v.env * 0.85);
            v.phase += juce::MathConstants<double>::twoPi * freq / sr;
            v.env *= 0.9975;
        }
        else if (v.lane == 1 || v.lane == 5)
        {
            v.noise = v.noise * 1664525u + 1013904223u;
            const float noise = static_cast<float>((v.noise >> 8) & 0xffff) / 32768.0f - 1.0f;
            sample = noise * static_cast<float>(v.env) * (v.lane == 1 ? 0.55f : 0.35f);
            v.env *= v.lane == 1 ? 0.992 : 0.988;
        }
        else if (v.lane == 2 || v.lane == 6)
        {
            v.noise = v.noise * 1664525u + 1013904223u;
            const float noise = static_cast<float>((v.noise >> 8) & 0xffff) / 32768.0f - 1.0f;
            const float mod = static_cast<float>(std::sin(p));
            sample = (noise - mod * 0.25f) * static_cast<float>(v.env) * 0.25f;
            v.phase += juce::MathConstants<double>::twoPi * 7000.0 / sr;
            v.env *= 0.972;
        }
        else
        {
            const double freq = v.lane == 4 ? 150.0 : (v.lane == 7 ? 440.0 : 260.0);
            sample = static_cast<float>(std::sin(p) * v.env * 0.32);
            v.phase += juce::MathConstants<double>::twoPi * freq / sr;
            v.env *= 0.994;
        }
        out += sample;
        if (v.env < 0.0008) v.active = false;
    }
    return std::tanh(out);
}

void DawWorkspace::addMidiNote(int note, double startBeat, double lengthBeat, float velocity)
{
    for (auto& n : midiNotes_)
    {
        if (!n.active.load())
        {
            n.note.store(juce::jlimit(0, 127, note));
            n.startBeat.store(std::max(0.0, startBeat));
            n.lengthBeats.store(std::max(0.0625, lengthBeat));
            n.velocity.store(juce::jlimit(0.01f, 1.0f, velocity));
            n.active.store(true, std::memory_order_release);
            return;
        }
    }
}

void DawWorkspace::deleteMidiNote(int index)
{
    if (index >= 0 && index < kMaxMidiNotes) midiNotes_[index].active.store(false, std::memory_order_release);
}

int DawWorkspace::noteAt(int x, int y) const
{
    const int keyW = 70, top = 30, minNote = 36, maxNote = 96;
    const int noteCount = maxNote - minNote + 1;
    if (x < keyW || y < top) return -1;
    const float rowH = static_cast<float>(pianoRollView_.getHeight() - top) / noteCount;
    const float ppb = std::max(34.0f, horizontalZoom_ * static_cast<float>(beatToSeconds(1.0)));
    for (int i = kMaxMidiNotes - 1; i >= 0; --i)
    {
        const auto& n = midiNotes_[i];
        if (!n.active.load()) continue;
        const int note = n.note.load();
        if (note < minNote || note > maxNote) continue;
        const float ny = top + (maxNote - note) * rowH;
        const int nx = keyW + static_cast<int>(n.startBeat.load() * ppb);
        const int nw = std::max(5, static_cast<int>(n.lengthBeats.load() * ppb));
        if (juce::Rectangle<float>(static_cast<float>(nx), ny, static_cast<float>(nw), std::max(4.0f, rowH)).contains(static_cast<float>(x), static_cast<float>(y)))
            return i;
    }
    return -1;
}

void DawWorkspace::scanInstrumentPlugins()
{
    auto* format = dynamic_cast<juce::VST3PluginFormat*>(instrumentFormatManager_.getFormat(0));
    if (format == nullptr) return;
    instrumentEntries_.clear();
    instrumentBox_.clear(juce::dontSendNotification);
    const auto roots = format->getDefaultLocationsToSearch();
    juce::StringArray seen;
    for (int r = 0; r < roots.getNumPaths(); ++r)
    {
        juce::Array<juce::File> files;
        roots[r].findChildFiles(files, juce::File::findFilesAndDirectories, true, "*.vst3");
        for (const auto& f : files)
        {
            const auto path = f.getFullPathName();
            if (seen.contains(path, true)) continue;
            seen.add(path);
            InstrumentEntry entry;
            entry.path = path;
            instrumentEntries_.push_back(entry);
            instrumentBox_.addItem(f.getFileNameWithoutExtension(), static_cast<int>(instrumentEntries_.size()));
        }
    }
    if (!instrumentEntries_.empty()) instrumentBox_.setSelectedId(1, juce::dontSendNotification);
    instrumentStatus_.setText(juce::String(instrumentEntries_.size()) + " módulos VST3 encontrados · se valida instrumento al cargar",
                              juce::dontSendNotification);
}

void DawWorkspace::loadSelectedInstrument()
{
    const int index = instrumentBox_.getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int>(instrumentEntries_.size())) return;
    loadInstrumentPath(instrumentEntries_[static_cast<std::size_t>(index)].path);
}

void DawWorkspace::loadInstrumentPath(const juce::String& path, const juce::String& savedState)
{
    auto* format = dynamic_cast<juce::VST3PluginFormat*>(instrumentFormatManager_.getFormat(0));
    if (format == nullptr || path.isEmpty()) return;

    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile(types, path);
    juce::PluginDescription* chosen = nullptr;
    for (auto* type : types)
        if (type != nullptr && type->isInstrument) { chosen = type; break; }
    if (chosen == nullptr)
    {
        instrumentStatus_.setText("El módulo seleccionado no se identifica como instrumento VST3.", juce::dontSendNotification);
        return;
    }

    const auto desc = *chosen;
    instrumentStatus_.setText("Cargando " + desc.name + "…", juce::dontSendNotification);
    instrumentFormatManager_.createPluginInstanceAsync(
        desc, sampleRate_.load(), blockSize_.load(),
        [safe = juce::Component::SafePointer<DawWorkspace>(this), path, savedState]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (safe == nullptr) return;
            if (instance == nullptr)
            {
                safe->instrumentStatus_.setText("No se pudo cargar VST3: " + error, juce::dontSendNotification);
                return;
            }
            if (!instance->acceptsMidi() && instance->getTotalNumInputChannels() > 0)
            {
                safe->instrumentStatus_.setText("El plugin cargado no acepta MIDI como instrumento.", juce::dontSendNotification);
                return;
            }
            instance->setRateAndBufferSizeDetails(safe->sampleRate_.load(), safe->blockSize_.load());
            instance->prepareToPlay(safe->sampleRate_.load(), safe->blockSize_.load());
            if (savedState.isNotEmpty())
            {
                juce::MemoryBlock state;
                if (state.fromBase64Encoding(savedState))
                    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            }
            auto shared = std::shared_ptr<juce::AudioPluginInstance>(std::move(instance));
            safe->instrumentOwners_.push_back(shared);
            safe->instrumentPath_ = path;
            safe->instrumentName_ = shared->getName();
            safe->instrumentPlugin_.store(shared.get(), std::memory_order_release);
            safe->instrumentBypass_.store(false);
            safe->refreshInstrumentUi();
            safe->markDirty();
        });
}

void DawWorkspace::openInstrumentEditor()
{
    auto* raw = instrumentPlugin_.load(std::memory_order_acquire);
    if (raw == nullptr) return;
    std::shared_ptr<juce::AudioPluginInstance> plugin;
    for (auto& p : instrumentOwners_) if (p.get() == raw) { plugin = p; break; }
    if (plugin == nullptr) return;
    auto holder = std::make_unique<PluginEditorHolder>(plugin);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(holder.release());
    options.dialogTitle = "J3 Worship · " + plugin->getName();
    options.dialogBackgroundColour = panel_;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void DawWorkspace::removeInstrument()
{
    instrumentPlugin_.store(nullptr, std::memory_order_release);
    instrumentPath_.clear(); instrumentName_.clear(); instrumentStateBase64_.clear();
    instrumentBypass_.store(false);
    refreshInstrumentUi(); markDirty();
}

void DawWorkspace::refreshInstrumentUi()
{
    const bool loaded = instrumentPlugin_.load() != nullptr;
    openInstrumentButton_.setEnabled(loaded);
    removeInstrumentButton_.setEnabled(loaded);
    bypassInstrumentButton_.setEnabled(loaded);
    bypassInstrumentButton_.setToggleState(instrumentBypass_.load(), juce::dontSendNotification);
    if (loaded) instrumentStatus_.setText("Instrumento: " + instrumentName_ + " · MIDI desde Piano Roll", juce::dontSendNotification);
    else if (instrumentStatus_.getText().isEmpty()) instrumentStatus_.setText("Sin instrumento VST3 · usa sintetizador interno", juce::dontSendNotification);
}

void DawWorkspace::processAudio(float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0 || !playing_.load(std::memory_order_acquire))
        return;
    const double sr = sampleRate_.load(std::memory_order_relaxed);
    const double start = playheadSeconds_.load(std::memory_order_relaxed);

    mixAudioClips(left, right, numSamples, start, sr);
    processPatterns(left, right, numSamples, start, sr);
    if (instrumentPlugin_.load(std::memory_order_acquire) != nullptr && !instrumentBypass_.load(std::memory_order_relaxed))
        processInstrument(left, right, numSamples, start, sr);
    else
        mixInternalMidi(left, right, numSamples, start, sr);

    double next = start + static_cast<double>(numSamples) / sr;
    if (loopEnabled_.load(std::memory_order_relaxed))
    {
        const double ls = loopStartSeconds_.load();
        const double le = std::max(ls + 0.1, loopEndSeconds_.load());
        if (next >= le)
        {
            next = ls + std::fmod(next - le, le - ls);
            resetRealtimeState();
        }
    }
    playheadSeconds_.store(next, std::memory_order_release);
}

void DawWorkspace::mixAudioClips(float* left, float* right, int numSamples,
                                 double blockStartSeconds, double sr) noexcept
{
    const int tracks = trackCount_.load(std::memory_order_relaxed);
    bool anySolo = false;
    for (int t = 0; t < tracks; ++t) if (tracks_[t].solo.load()) { anySolo = true; break; }

    for (int c = 0; c < kMaxClips; ++c)
    {
        auto& clip = clips_[c];
        if (!clip.active.load(std::memory_order_acquire) || clip.mute.load(std::memory_order_relaxed)) continue;
        const int track = clip.track.load(std::memory_order_relaxed);
        if (track < 0 || track >= tracks) continue;
        if (tracks_[track].mute.load() || (anySolo && !tracks_[track].solo.load())) continue;
        auto* buffer = clip.buffer.load(std::memory_order_acquire);
        if (buffer == nullptr || buffer->getNumSamples() <= 0) continue;

        const double clipStart = clip.startSeconds.load();
        const double clipLen = clip.lengthSeconds.load();
        if (clipLen <= 0.0) continue;
        const double blockEnd = blockStartSeconds + static_cast<double>(numSamples) / sr;
        if (blockEnd <= clipStart || blockStartSeconds >= clipStart + clipLen) continue;

        const double sourceStart = clip.sourceStartSeconds.load();
        const double sourceRate = clip.sourceSampleRate;
        const int sourceChannels = buffer->getNumChannels();
        const float clipGain = clip.gain.load();
        const double fadeIn = clip.fadeInSeconds.load();
        const double fadeOut = clip.fadeOutSeconds.load();
        const bool loopClip = clip.loop.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const double timelineTime = blockStartSeconds + static_cast<double>(i) / sr;
            if (timelineTime < clipStart || timelineTime >= clipStart + clipLen) continue;
            double local = timelineTime - clipStart;
            if (loopClip && clip.sourceDurationSeconds > sourceStart + 0.001)
                local = std::fmod(local, clip.sourceDurationSeconds - sourceStart);
            double sourcePos = (sourceStart + local) * sourceRate;
            int i0 = static_cast<int>(sourcePos);
            if (i0 < 0 || i0 >= buffer->getNumSamples()) continue;
            const int i1 = std::min(i0 + 1, buffer->getNumSamples() - 1);
            const float frac = static_cast<float>(sourcePos - i0);
            float l = buffer->getSample(0, i0) + (buffer->getSample(0, i1) - buffer->getSample(0, i0)) * frac;
            float r = sourceChannels > 1
                ? buffer->getSample(1, i0) + (buffer->getSample(1, i1) - buffer->getSample(1, i0)) * frac
                : l;

            float env = 1.0f;
            if (fadeIn > 0.0 && local < fadeIn) env *= static_cast<float>(local / fadeIn);
            const double tail = clipLen - local;
            if (fadeOut > 0.0 && tail < fadeOut) env *= static_cast<float>(std::max(0.0, tail / fadeOut));

            const double beat = secondsToBeat(timelineTime);
            float tg = tracks_[track].gain.load();
            float tp = tracks_[track].pan.load();
            const float av = evaluateAutomation(tracks_[track].volumeAutomation, beat, 1.0f);
            const float ap = evaluateAutomation(tracks_[track].panAutomation, beat, tp);
            tg *= juce::jlimit(0.0f, 1.5f, av);
            tp = juce::jlimit(-1.0f, 1.0f, ap);
            const float gl = constantPowerLeft(tp) * tg * clipGain * env;
            const float gr = constantPowerRight(tp) * tg * clipGain * env;
            left[i] += l * gl;
            right[i] += r * gr;
        }
    }
}

void DawWorkspace::processPatterns(float* left, float* right, int numSamples,
                                   double blockStartSeconds, double sr) noexcept
{
    const int length = patternLength_.load(std::memory_order_relaxed);
    const double stepSeconds = beatToSeconds(0.25);
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = blockStartSeconds + static_cast<double>(i) / sr;
        const int absoluteStep = static_cast<int>(std::floor(t / stepSeconds));
        const int step = ((absoluteStep % length) + length) % length;
        if (absoluteStep != lastPatternStep_)
        {
            lastPatternStep_ = absoluteStep;
            for (int lane = 0; lane < kPatternLanes; ++lane)
                if (patternSteps_[lane][step].load(std::memory_order_relaxed))
                    triggerDrumVoice(lane);
        }
        const float v = renderDrums(sr);
        left[i] += v * 0.52f;
        right[i] += v * 0.52f;
    }
}

void DawWorkspace::mixInternalMidi(float* left, float* right, int numSamples,
                                   double blockStartSeconds, double sr) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double time = blockStartSeconds + static_cast<double>(i) / sr;
        const double beat = secondsToBeat(time);
        float synth = 0.0f;
        for (const auto& n : midiNotes_)
        {
            if (!n.active.load(std::memory_order_acquire)) continue;
            const double startBeat = n.startBeat.load();
            const double endBeat = startBeat + n.lengthBeats.load();
            if (beat < startBeat || beat >= endBeat) continue;
            const int note = n.note.load();
            const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double elapsed = beatToSeconds(beat - startBeat);
            const double duration = beatToSeconds(n.lengthBeats.load());
            const double attack = std::min(0.01, duration * 0.1);
            const double release = std::min(0.08, duration * 0.2);
            float env = 1.0f;
            if (elapsed < attack) env = static_cast<float>(elapsed / std::max(0.0001, attack));
            if (duration - elapsed < release) env *= static_cast<float>((duration - elapsed) / std::max(0.0001, release));
            const double phase = juce::MathConstants<double>::twoPi * freq * elapsed;
            synth += static_cast<float>((std::sin(phase) * 0.75 + std::sin(phase * 2.0) * 0.18)
                                        * n.velocity.load() * env * 0.17);
        }
        left[i] += synth;
        right[i] += synth;
    }
}

void DawWorkspace::processInstrument(float* left, float* right, int numSamples,
                                     double blockStartSeconds, double sr) noexcept
{
    auto* plugin = instrumentPlugin_.load(std::memory_order_acquire);
    if (plugin == nullptr || instrumentScratch_.getNumSamples() < numSamples) return;
    instrumentScratch_.clear(0, numSamples);
    instrumentMidi_.clear();

    const double blockEndSeconds = blockStartSeconds + static_cast<double>(numSamples) / sr;
    const double blockStartBeat = secondsToBeat(blockStartSeconds);
    const double blockEndBeat = secondsToBeat(blockEndSeconds);

    std::array<bool, 128> shouldBeOn {};
    for (const auto& n : midiNotes_)
    {
        if (!n.active.load(std::memory_order_acquire)) continue;
        const int note = juce::jlimit(0, 127, n.note.load());
        const double sb = n.startBeat.load();
        const double eb = sb + n.lengthBeats.load();
        if (sb < blockEndBeat && eb > blockStartBeat) shouldBeOn[static_cast<std::size_t>(note)] = true;
        if (sb >= blockStartBeat && sb < blockEndBeat)
        {
            const double eventSeconds = beatToSeconds(sb);
            const int offset = juce::jlimit(0, numSamples - 1,
                static_cast<int>((eventSeconds - blockStartSeconds) * sr));
            instrumentMidi_.addEvent(juce::MidiMessage::noteOn(1, note, n.velocity.load()), offset);
        }
        if (eb >= blockStartBeat && eb < blockEndBeat)
        {
            const double eventSeconds = beatToSeconds(eb);
            const int offset = juce::jlimit(0, numSamples - 1,
                static_cast<int>((eventSeconds - blockStartSeconds) * sr));
            instrumentMidi_.addEvent(juce::MidiMessage::noteOff(1, note), offset);
        }
    }
    for (int note = 0; note < 128; ++note)
    {
        if (midiNoteWasOn_[static_cast<std::size_t>(note)] && !shouldBeOn[static_cast<std::size_t>(note)])
            instrumentMidi_.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        else if (!midiNoteWasOn_[static_cast<std::size_t>(note)] && shouldBeOn[static_cast<std::size_t>(note)])
        {
            bool hasStart = false;
            for (const auto meta : instrumentMidi_)
                if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() == note) { hasStart = true; break; }
            if (!hasStart) instrumentMidi_.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
        }
        midiNoteWasOn_[static_cast<std::size_t>(note)] = shouldBeOn[static_cast<std::size_t>(note)];
    }

    try
    {
        plugin->processBlock(instrumentScratch_, instrumentMidi_);
        const int channels = std::max(1, instrumentScratch_.getNumChannels());
        const auto* l = instrumentScratch_.getReadPointer(0);
        const auto* r = instrumentScratch_.getReadPointer(channels > 1 ? 1 : 0);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += l[i] * 0.65f;
            right[i] += r[i] * 0.65f;
        }
    }
    catch (...)
    {
        instrumentBypass_.store(true, std::memory_order_release);
    }
}

juce::File DawWorkspace::projectsRoot() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship").getChildFile("Projects");
}

juce::File DawWorkspace::autosaveFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship").getChildFile("Autosave").getChildFile("DAW-Autosave.j3w");
}

void DawWorkspace::newProject()
{
    stop();
    pushUndoPoint();
    for (auto& c : clips_) c.active.store(false, std::memory_order_release);
    for (auto& lane : patternSteps_) for (auto& step : lane) step.store(false);
    for (auto& n : midiNotes_) n.active.store(false, std::memory_order_release);
    trackCount_.store(8);
    selectedTrack_.store(0);
    selectedClip_ = -1;
    playheadSeconds_.store(0.0);
    currentProject_ = {};
    instrumentPlugin_.store(nullptr);
    instrumentPath_.clear(); instrumentName_.clear(); instrumentStateBase64_.clear();
    dirty_ = false;
    undoXml_.clear(); redoXml_.clear();
    updateProjectLabels();
    resized(); repaint(); timeline_.repaint(); patternView_.repaint(); pianoRollView_.repaint();
}

void DawWorkspace::openProject()
{
    projectsRoot().createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>("Abrir proyecto J3 Worship", projectsRoot(), "*.j3w");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<DawWorkspace>(this)](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (file.existsAsFile()) safe->loadProjectFrom(file);
        });
}

void DawWorkspace::saveProject()
{
    if (currentProject_ == juce::File()) saveProjectAs();
    else saveProjectTo(currentProject_);
}

void DawWorkspace::saveProjectAs()
{
    projectsRoot().createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>("Guardar proyecto J3 Worship",
                                                   projectsRoot().getChildFile("Proyecto.j3w"), "*.j3w");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                        | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<DawWorkspace>(this)](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (!file.hasFileExtension("j3w")) file = file.withFileExtension(".j3w");
            safe->saveProjectTo(file);
        });
}

std::unique_ptr<juce::XmlElement> DawWorkspace::createProjectXml(bool includePluginState)
{
    auto xml = std::make_unique<juce::XmlElement>("J3WorshipProject");
    xml->setAttribute("version", "1.2.0");
    xml->setAttribute("bpm", bpm_.load());
    xml->setAttribute("trackCount", trackCount_.load());
    xml->setAttribute("playhead", playheadSeconds_.load());
    xml->setAttribute("loopEnabled", loopEnabled_.load());
    xml->setAttribute("loopStart", loopStartSeconds_.load());
    xml->setAttribute("loopEnd", loopEndSeconds_.load());
    xml->setAttribute("patternLength", patternLength_.load());

    for (int t = 0; t < trackCount_.load(); ++t)
    {
        auto* tr = xml->createNewChildElement("Track");
        tr->setAttribute("index", t);
        tr->setAttribute("name", tracks_[t].name);
        tr->setAttribute("colour", tracks_[t].colour.toString());
        tr->setAttribute("gain", static_cast<double>(tracks_[t].gain.load()));
        tr->setAttribute("pan", static_cast<double>(tracks_[t].pan.load()));
        tr->setAttribute("mute", tracks_[t].mute.load());
        tr->setAttribute("solo", tracks_[t].solo.load());
        tr->setAttribute("arm", tracks_[t].arm.load());
        tr->setAttribute("monitor", tracks_[t].monitor.load());
        for (int i = 0; i < kMaxAutomationPoints; ++i)
        {
            if (tracks_[t].volumeAutomation[i].active.load())
            {
                auto* a = tr->createNewChildElement("Automation");
                a->setAttribute("type", "volume");
                a->setAttribute("beat", tracks_[t].volumeAutomation[i].beat.load());
                a->setAttribute("value", static_cast<double>(tracks_[t].volumeAutomation[i].value.load()));
            }
            if (tracks_[t].panAutomation[i].active.load())
            {
                auto* a = tr->createNewChildElement("Automation");
                a->setAttribute("type", "pan");
                a->setAttribute("beat", tracks_[t].panAutomation[i].beat.load());
                a->setAttribute("value", static_cast<double>(tracks_[t].panAutomation[i].value.load()));
            }
        }
    }

    for (int c = 0; c < kMaxClips; ++c)
    {
        if (!clips_[c].active.load()) continue;
        const auto& clip = clips_[c];
        auto* e = xml->createNewChildElement("Clip");
        e->setAttribute("slot", c);
        e->setAttribute("track", clip.track.load());
        e->setAttribute("file", clip.sourceFile.getFullPathName());
        e->setAttribute("name", clip.name);
        e->setAttribute("colour", clip.colour.toString());
        e->setAttribute("start", clip.startSeconds.load());
        e->setAttribute("sourceStart", clip.sourceStartSeconds.load());
        e->setAttribute("length", clip.lengthSeconds.load());
        e->setAttribute("gain", static_cast<double>(clip.gain.load()));
        e->setAttribute("fadeIn", clip.fadeInSeconds.load());
        e->setAttribute("fadeOut", clip.fadeOutSeconds.load());
        e->setAttribute("loop", clip.loop.load());
        e->setAttribute("mute", clip.mute.load());
    }

    auto* pattern = xml->createNewChildElement("Pattern");
    for (int lane = 0; lane < kPatternLanes; ++lane)
        for (int step = 0; step < patternLength_.load(); ++step)
            if (patternSteps_[lane][step].load())
            {
                auto* s = pattern->createNewChildElement("Step");
                s->setAttribute("lane", lane); s->setAttribute("index", step);
            }

    auto* midi = xml->createNewChildElement("PianoRoll");
    for (int i = 0; i < kMaxMidiNotes; ++i)
        if (midiNotes_[i].active.load())
        {
            auto* n = midi->createNewChildElement("Note");
            n->setAttribute("note", midiNotes_[i].note.load());
            n->setAttribute("startBeat", midiNotes_[i].startBeat.load());
            n->setAttribute("lengthBeats", midiNotes_[i].lengthBeats.load());
            n->setAttribute("velocity", static_cast<double>(midiNotes_[i].velocity.load()));
        }

    if (auto* plugin = instrumentPlugin_.load(); plugin != nullptr && instrumentPath_.isNotEmpty())
    {
        if (includePluginState)
        {
            juce::MemoryBlock state;
            try { plugin->getStateInformation(state); instrumentStateBase64_ = state.toBase64Encoding(); }
            catch (...) {}
        }
        auto* p = xml->createNewChildElement("Instrument");
        p->setAttribute("path", instrumentPath_);
        p->setAttribute("name", instrumentName_);
        p->setAttribute("bypass", instrumentBypass_.load());
        p->setAttribute("state", instrumentStateBase64_);
    }
    return xml;
}

bool DawWorkspace::saveProjectTo(const juce::File& file, bool isAutosave)
{
    auto xml = createProjectXml(!isAutosave);
    if (xml == nullptr) return false;
    file.getParentDirectory().createDirectory();
    if (!isAutosave && file.existsAsFile())
    {
        const auto backup = file.getSiblingFile(file.getFileNameWithoutExtension() + ".backup.j3w");
        file.copyFileTo(backup);
    }
    const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
    temp.deleteFile();
    if (!temp.replaceWithText(xml->toString(), false, false, "\n")) return false;
    if (file.existsAsFile()) file.deleteFile();
    if (!temp.moveFileTo(file)) return false;
    if (!isAutosave)
    {
        currentProject_ = file;
        dirty_ = false;
        updateProjectLabels();
    }
    return true;
}

bool DawWorkspace::loadProjectFrom(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("J3WorshipProject")) return false;
    stop();
    const bool ok = loadProjectXml(*xml, file);
    if (ok)
    {
        currentProject_ = file;
        dirty_ = false;
        undoXml_.clear(); redoXml_.clear();
        updateProjectLabels();
        resized(); repaint(); timeline_.repaint(); patternView_.repaint(); pianoRollView_.repaint();
    }
    return ok;
}

bool DawWorkspace::loadProjectXml(const juce::XmlElement& xml, const juce::File& projectFile)
{
    juce::ignoreUnused(projectFile);
    playing_.store(false);
    for (auto& c : clips_) c.active.store(false, std::memory_order_release);
    for (auto& lane : patternSteps_) for (auto& s : lane) s.store(false);
    for (auto& n : midiNotes_) n.active.store(false, std::memory_order_release);
    for (auto& t : tracks_)
    {
        for (auto& p : t.volumeAutomation) p.active.store(false);
        for (auto& p : t.panAutomation) p.active.store(false);
    }

    bpm_.store(juce::jlimit(40.0, 240.0, xml.getDoubleAttribute("bpm", 120.0)));
    bpmSlider_.setValue(bpm_.load(), juce::dontSendNotification);
    trackCount_.store(juce::jlimit(1, kMaxTracks, xml.getIntAttribute("trackCount", 8)));
    playheadSeconds_.store(std::max(0.0, xml.getDoubleAttribute("playhead", 0.0)));
    loopEnabled_.store(xml.getBoolAttribute("loopEnabled", false));
    loopButton_.setToggleState(loopEnabled_.load(), juce::dontSendNotification);
    loopStartSeconds_.store(std::max(0.0, xml.getDoubleAttribute("loopStart", 0.0)));
    loopEndSeconds_.store(std::max(loopStartSeconds_.load() + 0.1, xml.getDoubleAttribute("loopEnd", 16.0)));
    patternLength_.store(juce::jlimit(16, 64, xml.getIntAttribute("patternLength", 16)));
    patternLengthBox_.setSelectedId(patternLength_.load(), juce::dontSendNotification);

    forEachXmlChildElementWithTagName(xml, tr, "Track")
    {
        const int t = tr->getIntAttribute("index", -1);
        if (t < 0 || t >= trackCount_.load()) continue;
        tracks_[t].name = tr->getStringAttribute("name", "Track " + juce::String(t + 1));
        tracks_[t].colour = juce::Colour::fromString(tr->getStringAttribute("colour", trackColourForIndex(t).toString()));
        tracks_[t].gain.store(static_cast<float>(tr->getDoubleAttribute("gain", 1.0)));
        tracks_[t].pan.store(static_cast<float>(tr->getDoubleAttribute("pan", 0.0)));
        tracks_[t].mute.store(tr->getBoolAttribute("mute", false));
        tracks_[t].solo.store(tr->getBoolAttribute("solo", false));
        tracks_[t].arm.store(tr->getBoolAttribute("arm", false));
        tracks_[t].monitor.store(tr->getBoolAttribute("monitor", false));
        int vi = 0, pi = 0;
        forEachXmlChildElementWithTagName(*tr, a, "Automation")
        {
            const bool volume = a->getStringAttribute("type") == "volume";
            auto& points = volume ? tracks_[t].volumeAutomation : tracks_[t].panAutomation;
            int& idx = volume ? vi : pi;
            if (idx >= kMaxAutomationPoints) continue;
            points[idx].beat.store(a->getDoubleAttribute("beat", 0.0));
            points[idx].value.store(static_cast<float>(a->getDoubleAttribute("value", volume ? 1.0 : 0.0)));
            points[idx].active.store(true);
            ++idx;
        }
    }

    forEachXmlChildElementWithTagName(xml, ce, "Clip")
    {
        int slot = ce->getIntAttribute("slot", -1);
        if (slot < 0 || slot >= kMaxClips || clips_[slot].active.load()) slot = findFreeClip();
        if (slot < 0) continue;
        const juce::File source(ce->getStringAttribute("file"));
        if (!source.existsAsFile()) continue;
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(source));
        if (reader == nullptr || reader->lengthInSamples <= 0) continue;
        const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
        const int samples = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max() - 1));
        auto buffer = std::make_unique<juce::AudioBuffer<float>>(channels, samples);
        if (!reader->read(buffer.get(), 0, samples, 0, true, channels > 1)) continue;
        auto* raw = buffer.get();
        ownedAudioBuffers_.push_back(std::move(buffer));
        auto& c = clips_[slot];
        c.active.store(false);
        c.track.store(juce::jlimit(0, trackCount_.load() - 1, ce->getIntAttribute("track", 0)));
        c.startSeconds.store(std::max(0.0, ce->getDoubleAttribute("start", 0.0)));
        c.sourceStartSeconds.store(std::max(0.0, ce->getDoubleAttribute("sourceStart", 0.0)));
        c.lengthSeconds.store(std::max(0.02, ce->getDoubleAttribute("length", static_cast<double>(samples) / reader->sampleRate)));
        c.gain.store(static_cast<float>(ce->getDoubleAttribute("gain", 1.0)));
        c.fadeInSeconds.store(std::max(0.0, ce->getDoubleAttribute("fadeIn", 0.0)));
        c.fadeOutSeconds.store(std::max(0.0, ce->getDoubleAttribute("fadeOut", 0.0)));
        c.loop.store(ce->getBoolAttribute("loop", false));
        c.mute.store(ce->getBoolAttribute("mute", false));
        c.sourceSampleRate = reader->sampleRate;
        c.sourceDurationSeconds = static_cast<double>(samples) / reader->sampleRate;
        c.sourceFile = source;
        c.name = ce->getStringAttribute("name", source.getFileNameWithoutExtension());
        c.colour = juce::Colour::fromString(ce->getStringAttribute("colour", tracks_[c.track.load()].colour.toString()));
        c.buffer.store(raw, std::memory_order_release);
        c.active.store(true, std::memory_order_release);
    }

    if (auto* pattern = xml.getChildByName("Pattern"))
        forEachXmlChildElementWithTagName(*pattern, se, "Step")
        {
            const int lane = se->getIntAttribute("lane", -1), step = se->getIntAttribute("index", -1);
            if (lane >= 0 && lane < kPatternLanes && step >= 0 && step < kPatternSteps)
                patternSteps_[lane][step].store(true);
        }

    if (auto* midi = xml.getChildByName("PianoRoll"))
        forEachXmlChildElementWithTagName(*midi, ne, "Note")
            addMidiNote(ne->getIntAttribute("note", 60), ne->getDoubleAttribute("startBeat", 0.0),
                        ne->getDoubleAttribute("lengthBeats", 1.0),
                        static_cast<float>(ne->getDoubleAttribute("velocity", 0.8)));

    if (auto* instr = xml.getChildByName("Instrument"))
    {
        instrumentBypass_.store(instr->getBoolAttribute("bypass", false));
        loadInstrumentPath(instr->getStringAttribute("path"), instr->getStringAttribute("state"));
    }
    else removeInstrument();

    selectedTrack_.store(0);
    selectedClip_ = -1;
    resetRealtimeState();
    return true;
}

void DawWorkspace::pushUndoPoint()
{
    if (restoringHistory_) return;
    auto xml = createProjectXml(false);
    if (xml == nullptr) return;
    undoXml_.push_back(xml->toString());
    if (undoXml_.size() > 50) undoXml_.erase(undoXml_.begin());
    redoXml_.clear();
}

void DawWorkspace::undo()
{
    if (undoXml_.empty()) return;
    auto current = createProjectXml(false);
    if (current) redoXml_.push_back(current->toString());
    const auto text = undoXml_.back(); undoXml_.pop_back();
    auto xml = juce::XmlDocument::parse(text);
    if (xml)
    {
        restoringHistory_ = true;
        loadProjectXml(*xml, currentProject_);
        restoringHistory_ = false;
        dirty_ = true;
        resized(); timeline_.repaint(); patternView_.repaint(); pianoRollView_.repaint();
    }
}

void DawWorkspace::redo()
{
    if (redoXml_.empty()) return;
    auto current = createProjectXml(false);
    if (current) undoXml_.push_back(current->toString());
    const auto text = redoXml_.back(); redoXml_.pop_back();
    auto xml = juce::XmlDocument::parse(text);
    if (xml)
    {
        restoringHistory_ = true;
        loadProjectXml(*xml, currentProject_);
        restoringHistory_ = false;
        dirty_ = true;
        resized(); timeline_.repaint(); patternView_.repaint(); pianoRollView_.repaint();
    }
}

void DawWorkspace::markDirty()
{
    dirty_ = true;
    updateProjectLabels();
}

void DawWorkspace::updateProjectLabels()
{
    const juce::String name = currentProject_ == juce::File() ? "Proyecto sin guardar" : currentProject_.getFileNameWithoutExtension();
    projectLabel_.setText(name + (dirty_ ? " *" : ""), juce::dontSendNotification);
}

void DawWorkspace::refreshBrowserRoot()
{
    if (!directoryList_) return;
    juce::File root;
    const int id = browserRootBox_.getSelectedId();
    if (id == 2)
        root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("J3 Worship").getChildFile("Recordings");
    else if (id == 3)
        root = projectsRoot();
    else
        root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    root.createDirectory();
    directoryList_->setDirectory(root, true, true);
}

void DawWorkspace::addBrowserSelection()
{
    if (!fileTree_) return;
    const auto file = fileTree_->getSelectedFile();
    if (isAudioExtension(file))
        importAudioFile(file, selectedTrack_.load(), playheadSeconds_.load());
    else if (file.hasFileExtension("j3w"))
        loadProjectFrom(file);
}

bool DawWorkspace::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if ((code == 'R' || code == 'r') && !mods.isCommandDown()) { requestRecord(); return true; }
    if (mods.isCommandDown() && (code == 'S' || code == 's'))
    {
        if (mods.isShiftDown()) saveProjectAs(); else saveProject();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) redo(); else undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { redo(); return true; }
    if (mods.isCommandDown() && (code == 'C' || code == 'c')) { copySelectedClip(); return true; }
    if (mods.isCommandDown() && (code == 'V' || code == 'v')) { pasteClip(); return true; }
    if (mods.isCommandDown() && (code == 'X' || code == 'x')) { copySelectedClip(); deleteSelectedClip(); return true; }
    if (mods.isCommandDown() && (code == 'D' || code == 'd')) { duplicateSelectedClip(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }
    return false;
}

void DawWorkspace::timerCallback()
{
    const double sec = playheadSeconds_.load(std::memory_order_acquire);
    const double beat = secondsToBeat(sec);
    const int bar = static_cast<int>(beat / 4.0) + 1;
    const int beatInBar = static_cast<int>(beat) % 4 + 1;
    const int hundredths = static_cast<int>(std::fmod(sec, 1.0) * 100.0);
    const int totalSeconds = static_cast<int>(sec);
    positionLabel_.setText(juce::String::formatted("%03d:%02d  ·  %d.%d",
                           totalSeconds / 60, totalSeconds % 60, bar, beatInBar),
                           juce::dontSendNotification);
    recordButton_.setButtonText(recordRequested_.load() ? "REC ●" : "REC");
    playButton_.setButtonText(playing_.load() ? "PAUSE" : "PLAY");
    if (playing_.load())
    {
        timeline_.repaint();
        pianoRollView_.repaint();
    }

    const auto now = static_cast<std::uint64_t>(juce::Time::getMillisecondCounterHiRes());
    if (dirty_ && now - lastAutosaveMs_ > 15000)
    {
        saveProjectTo(autosaveFile(), true);
        lastAutosaveMs_ = now;
    }
}

