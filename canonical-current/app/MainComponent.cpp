#include "MainComponent.h"
#include "BinaryData.h"
#include "j3/Routing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace
{
std::uint32_t background = 0xff080d12;
std::uint32_t topBar = 0xff0b131a;
std::uint32_t panel = 0xff0e1820;
std::uint32_t panel2 = 0xff12222c;
std::uint32_t panel3 = 0xff19303d;
std::uint32_t accent = 0xff158cff;
std::uint32_t accentDeep = 0xff0d67bf;
std::uint32_t good = 0xff2ed47a;
std::uint32_t warning = 0xffffc247;
std::uint32_t danger = 0xffff4d64;
std::uint32_t text = 0xffeff6fc;
std::uint32_t mutedText = 0xff91a3b3;
std::uint32_t border = 0xff263a47;

void syncPaletteGlobals(const j3ui::Palette& p)
{
    background = p.background.getARGB();
    topBar = p.topBar.getARGB();
    panel = p.panel.getARGB();
    panel2 = p.panel2.getARGB();
    panel3 = p.panel3.getARGB();
    accent = p.accent.getARGB();
    accentDeep = p.accent.darker(0.25f).getARGB();
    good = p.good.getARGB();
    warning = p.warning.getARGB();
    danger = p.danger.getARGB();
    text = p.text.getARGB();
    mutedText = p.mutedText.getARGB();
    border = p.border.getARGB();
}

float dbToGain(double db)
{
    return static_cast<float>(juce::Decibels::decibelsToGain(db, -90.0));
}

juce::String outputName(juce::AudioIODevice& device, int index)
{
    const auto names = device.getOutputChannelNames();
    if (index >= 0 && index < names.size() && names[index].isNotEmpty())
        return names[index];
    return "OUT " + juce::String(index + 1);
}

juce::String pluginDescriptionKey(const juce::PluginDescription& description)
{
    return description.fileOrIdentifier + "||" + juce::String(description.uniqueId)
        + "||" + description.name;
}

bool savedPluginKeyMatches(const juce::String& saved, const juce::PluginDescription& description)
{
    return saved == pluginDescriptionKey(description)
        || saved == description.fileOrIdentifier;
}

class GenericPluginEditorHolder final : public juce::Component
{
public:
    explicit GenericPluginEditorHolder(std::shared_ptr<juce::AudioPluginInstance> plugin)
        : plugin_(std::move(plugin))
    {
        if (plugin_ != nullptr && plugin_->hasEditor())
            editor_.reset(plugin_->createEditorIfNeeded());
        if (editor_ == nullptr && plugin_ != nullptr)
            editor_ = std::make_unique<juce::GenericAudioProcessorEditor>(*plugin_);

        int maxWidth = 1180;
        int maxHeight = 820;
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto work = display->userBounds;
            const int workWidth = static_cast<int>(std::lround(work.getWidth()));
            const int workHeight = static_cast<int>(std::lround(work.getHeight()));
            maxWidth = std::max(420, std::min(1180, workWidth - 80));
            maxHeight = std::max(320, std::min(820, workHeight - 100));
        }

        if (editor_ != nullptr)
        {
            addAndMakeVisible(*editor_);
            const int minWidth = std::min(520, maxWidth);
            const int minHeight = std::min(420, maxHeight);
            setSize(juce::jlimit(minWidth, maxWidth, editor_->getWidth()),
                    juce::jlimit(minHeight, maxHeight, editor_->getHeight()));
        }
        else
        {
            setSize(std::min(640, maxWidth), std::min(480, maxHeight));
        }
    }

    void resized() override
    {
        if (editor_ != nullptr)
            editor_->setBounds(getLocalBounds());
    }

private:
    std::shared_ptr<juce::AudioPluginInstance> plugin_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
};

class NativeEqEditor final : public juce::Component
{
public:
    NativeEqEditor(juce::String insertName,
                   std::atomic<float>& hpf,
                   std::atomic<float>& lpf,
                   std::array<std::atomic<float>, 4>& frequencies,
                   std::array<std::atomic<float>, 4>& gains,
                   std::array<std::atomic<float>, 4>& qs,
                   std::function<void()> onChanged)
        : insertName_(std::move(insertName)),
          hpf_(hpf), lpf_(lpf),
          frequencies_(frequencies), gains_(gains), qs_(qs),
          onChanged_(std::move(onChanged))
    {
        title_.setText("J3 PARAMETRIC EQ", juce::dontSendNotification);
        title_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title_.setColour(juce::Label::textColourId, juce::Colour(text));
        addAndMakeVisible(title_);

        subtitle_.setText(insertName_ + "  |  NATIVE PRE-FX", juce::dontSendNotification);
        subtitle_.setFont(juce::FontOptions(13.0f));
        subtitle_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
        addAndMakeVisible(subtitle_);

        bandLabel_.setText("BANDA", juce::dontSendNotification);
        bandLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
        bandLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        addAndMakeVisible(bandLabel_);

        for (int i = 0; i < 4; ++i)
            bandBox_.addItem("BANDA " + juce::String(i + 1), i + 1);
        bandBox_.setSelectedId(1, juce::dontSendNotification);
        bandBox_.onChange = [this]
        {
            selectedBand_ = juce::jlimit(0, 3, bandBox_.getSelectedId() - 1);
            syncBandControls();
            repaint();
        };
        addAndMakeVisible(bandBox_);

        setupLabel(hpfLabel_, "HPF");
        setupLabel(lpfLabel_, "LPF");
        setupLabel(freqLabel_, "FRECUENCIA");
        setupLabel(gainLabel_, "GANANCIA");
        setupLabel(qLabel_, "Q / ANCHO");

        setupSlider(hpfSlider_, 20.0, 1000.0, 1.0, " Hz");
        hpfSlider_.setSkewFactorFromMidPoint(120.0);
        setupSlider(lpfSlider_, 1000.0, 20000.0, 10.0, " Hz");
        lpfSlider_.setSkewFactorFromMidPoint(7000.0);
        setupSlider(freqSlider_, 20.0, 20000.0, 1.0, " Hz");
        freqSlider_.setSkewFactorFromMidPoint(1000.0);
        setupSlider(gainSlider_, -18.0, 18.0, 0.1, " dB");
        setupSlider(qSlider_, 0.20, 12.0, 0.05, " Q");
        qSlider_.setSkewFactorFromMidPoint(1.0);

        hpfSlider_.onValueChange = [this]
        {
            hpf_.store(static_cast<float>(hpfSlider_.getValue()), std::memory_order_relaxed);
            changed();
        };
        lpfSlider_.onValueChange = [this]
        {
            lpf_.store(static_cast<float>(lpfSlider_.getValue()), std::memory_order_relaxed);
            changed();
        };
        freqSlider_.onValueChange = [this]
        {
            frequencies_[selectedBand_].store(static_cast<float>(freqSlider_.getValue()), std::memory_order_relaxed);
            changed();
        };
        gainSlider_.onValueChange = [this]
        {
            gains_[selectedBand_].store(static_cast<float>(gainSlider_.getValue()), std::memory_order_relaxed);
            changed();
        };
        qSlider_.onValueChange = [this]
        {
            qs_[selectedBand_].store(static_cast<float>(qSlider_.getValue()), std::memory_order_relaxed);
            changed();
        };

        hint_.setText("Arrastrá los puntos del gráfico para mover frecuencia y ganancia. Doble click en un punto = 0 dB.",
                      juce::dontSendNotification);
        hint_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
        hint_.setFont(juce::FontOptions(12.5f));
        addAndMakeVisible(hint_);

        syncFromModel();
        setSize(920, 580);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(background));

        auto graph = graphArea_.toFloat();
        g.setColour(juce::Colour(0xff0b1117));
        g.fillRoundedRectangle(graph, 7.0f);
        g.setColour(juce::Colour(border));
        g.drawRoundedRectangle(graph.reduced(0.5f), 7.0f, 1.0f);

        if (graphArea_.isEmpty())
            return;

        static const std::array<double, 8> frequencyLines { 20.0, 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0, 20000.0 };
        g.setFont(juce::FontOptions(10.5f));
        for (const auto hz : frequencyLines)
        {
            const float x = hzToX(hz);
            g.setColour(juce::Colour(0xff26323d).withAlpha(hz == 1000.0 ? 0.9f : 0.55f));
            g.drawVerticalLine(static_cast<int>(std::round(x)),
                               static_cast<float>(graphArea_.getY()),
                               static_cast<float>(graphArea_.getBottom()));
            juce::String label = hz >= 1000.0 ? juce::String(hz / 1000.0, hz < 10000.0 ? 1 : 0) + "k"
                                             : juce::String(static_cast<int>(hz));
            g.setColour(juce::Colour(mutedText).withAlpha(0.75f));
            g.drawText(label, static_cast<int>(x) - 24, graphArea_.getBottom() - 18, 48, 16,
                       juce::Justification::centred, false);
        }

        for (int db : { -12, -6, 0, 6, 12 })
        {
            const float y = gainToY(static_cast<double>(db));
            g.setColour(db == 0 ? juce::Colour(0xff526270) : juce::Colour(0xff26323d).withAlpha(0.55f));
            g.drawHorizontalLine(static_cast<int>(std::round(y)),
                                 static_cast<float>(graphArea_.getX()),
                                 static_cast<float>(graphArea_.getRight()));
            if (db != 0)
            {
                g.setColour(juce::Colour(mutedText).withAlpha(0.7f));
                g.drawText((db > 0 ? "+" : "") + juce::String(db),
                           graphArea_.getX() + 5, static_cast<int>(y) - 8, 34, 16,
                           juce::Justification::centredLeft, false);
            }
        }

        juce::Path response;
        bool first = true;
        for (int px = graphArea_.getX(); px <= graphArea_.getRight(); px += 2)
        {
            const double hz = xToHz(static_cast<float>(px));
            double db = 0.0;
            const double hpf = std::max(20.0, static_cast<double>(hpf_.load(std::memory_order_relaxed)));
            const double lpf = std::max(1000.0, static_cast<double>(lpf_.load(std::memory_order_relaxed)));
            if (hz < hpf)
                db += std::max(-48.0, 24.0 * std::log2(std::max(0.0001, hz / hpf)));
            if (hz > lpf)
                db += std::max(-48.0, -24.0 * std::log2(std::max(1.0, hz / lpf)));

            for (int band = 0; band < 4; ++band)
            {
                const double centre = std::max(20.0, static_cast<double>(frequencies_[band].load(std::memory_order_relaxed)));
                const double q = std::max(0.2, static_cast<double>(qs_[band].load(std::memory_order_relaxed)));
                const double gain = static_cast<double>(gains_[band].load(std::memory_order_relaxed));
                const double octaves = std::log2(std::max(0.0001, hz / centre));
                const double sigma = std::max(0.16, 1.25 / q);
                db += gain * std::exp(-0.5 * (octaves / sigma) * (octaves / sigma));
            }

            const float y = gainToY(db);
            if (first)
            {
                response.startNewSubPath(static_cast<float>(px), y);
                first = false;
            }
            else
            {
                response.lineTo(static_cast<float>(px), y);
            }
        }

        g.setColour(juce::Colour(accent));
        g.strokePath(response, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved));

        static const std::array<std::uint32_t, 4> bandColours {
            0xff43c7ff, 0xff58df9a, 0xffffc85a, 0xffff7185
        };
        for (int band = 0; band < 4; ++band)
        {
            const float x = hzToX(frequencies_[band].load(std::memory_order_relaxed));
            const float y = gainToY(gains_[band].load(std::memory_order_relaxed));
            const float radius = band == selectedBand_ ? 9.0f : 7.0f;
            g.setColour(juce::Colour(bandColours[static_cast<std::size_t>(band)]));
            g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xff071018));
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.drawText(juce::String(band + 1),
                       static_cast<int>(x - radius), static_cast<int>(y - radius),
                       static_cast<int>(radius * 2.0f), static_cast<int>(radius * 2.0f),
                       juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        auto header = area.removeFromTop(48);
        title_.setBounds(header.removeFromLeft(330));
        subtitle_.setBounds(header);
        area.removeFromTop(8);

        graphArea_ = area.removeFromTop(std::max(250, area.getHeight() - 190));
        area.removeFromTop(12);

        auto controls = area;
        auto left = controls.removeFromLeft(controls.getWidth() / 2).reduced(4, 0);
        auto right = controls.reduced(4, 0);

        auto bandRow = left.removeFromTop(34);
        bandLabel_.setBounds(bandRow.removeFromLeft(70));
        bandBox_.setBounds(bandRow.removeFromLeft(160));
        left.removeFromTop(4);
        layoutControlRow(left, hpfLabel_, hpfSlider_);
        layoutControlRow(left, lpfLabel_, lpfSlider_);

        layoutControlRow(right, freqLabel_, freqSlider_);
        layoutControlRow(right, gainLabel_, gainSlider_);
        layoutControlRow(right, qLabel_, qSlider_);

        hint_.setBounds(area.removeFromBottom(24));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!graphArea_.contains(e.position.toInt()))
            return;

        float bestDistance = 100000.0f;
        int bestBand = 0;
        for (int band = 0; band < 4; ++band)
        {
            const float x = hzToX(frequencies_[band].load(std::memory_order_relaxed));
            const float y = gainToY(gains_[band].load(std::memory_order_relaxed));
            const float dx = e.position.x - x;
            const float dy = e.position.y - y;
            const float d = dx * dx + dy * dy;
            if (d < bestDistance)
            {
                bestDistance = d;
                bestBand = band;
            }
        }
        selectedBand_ = bestBand;
        dragBand_ = bestBand;
        bandBox_.setSelectedId(bestBand + 1, juce::dontSendNotification);
        syncBandControls();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand_ < 0 || !graphArea_.contains(e.position.toInt()))
            return;

        const double hz = xToHz(static_cast<float>(e.position.x));
        const double gain = yToGain(static_cast<float>(e.position.y));
        frequencies_[dragBand_].store(static_cast<float>(hz), std::memory_order_relaxed);
        gains_[dragBand_].store(static_cast<float>(gain), std::memory_order_relaxed);
        freqSlider_.setValue(hz, juce::dontSendNotification);
        gainSlider_.setValue(gain, juce::dontSendNotification);
        changed();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragBand_ = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!graphArea_.contains(e.position.toInt()))
            return;
        gains_[selectedBand_].store(0.0f, std::memory_order_relaxed);
        gainSlider_.setValue(0.0, juce::dontSendNotification);
        changed();
    }

private:
    void setupLabel(juce::Label& label, const juce::String& textValue)
    {
        label.setText(textValue, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(mutedText));
        label.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        addAndMakeVisible(label);
    }

    void setupSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 24);
        slider.setRange(min, max, step);
        slider.setTextValueSuffix(suffix);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(accent));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffeaf5ff));
        addAndMakeVisible(slider);
    }

    void layoutControlRow(juce::Rectangle<int>& area, juce::Label& label, juce::Slider& slider)
    {
        auto row = area.removeFromTop(38);
        label.setBounds(row.removeFromLeft(96));
        slider.setBounds(row);
        area.removeFromTop(3);
    }

    void syncFromModel()
    {
        hpfSlider_.setValue(hpf_.load(std::memory_order_relaxed), juce::dontSendNotification);
        lpfSlider_.setValue(lpf_.load(std::memory_order_relaxed), juce::dontSendNotification);
        syncBandControls();
    }

    void syncBandControls()
    {
        freqSlider_.setValue(frequencies_[selectedBand_].load(std::memory_order_relaxed), juce::dontSendNotification);
        gainSlider_.setValue(gains_[selectedBand_].load(std::memory_order_relaxed), juce::dontSendNotification);
        qSlider_.setValue(qs_[selectedBand_].load(std::memory_order_relaxed), juce::dontSendNotification);
    }

    void changed()
    {
        if (onChanged_)
            onChanged_();
        repaint();
    }

    float hzToX(double hz) const
    {
        const double normalized = std::log(std::clamp(hz, 20.0, 20000.0) / 20.0) / std::log(1000.0);
        return static_cast<float>(graphArea_.getX() + normalized * graphArea_.getWidth());
    }

    double xToHz(float x) const
    {
        const double normalized = std::clamp(
            (static_cast<double>(x) - graphArea_.getX()) / std::max(1, graphArea_.getWidth()), 0.0, 1.0);
        return 20.0 * std::pow(1000.0, normalized);
    }

    float gainToY(double gain) const
    {
        const double clamped = std::clamp(gain, -18.0, 18.0);
        const double normalized = (18.0 - clamped) / 36.0;
        return static_cast<float>(graphArea_.getY() + normalized * graphArea_.getHeight());
    }

    double yToGain(float y) const
    {
        const double normalized = std::clamp(
            (static_cast<double>(y) - graphArea_.getY()) / std::max(1, graphArea_.getHeight()), 0.0, 1.0);
        return 18.0 - normalized * 36.0;
    }

    juce::String insertName_;
    std::atomic<float>& hpf_;
    std::atomic<float>& lpf_;
    std::array<std::atomic<float>, 4>& frequencies_;
    std::array<std::atomic<float>, 4>& gains_;
    std::array<std::atomic<float>, 4>& qs_;
    std::function<void()> onChanged_;

    juce::Label title_;
    juce::Label subtitle_;
    juce::Label bandLabel_;
    juce::ComboBox bandBox_;
    juce::Label hpfLabel_, lpfLabel_, freqLabel_, gainLabel_, qLabel_;
    juce::Slider hpfSlider_, lpfSlider_, freqSlider_, gainSlider_, qSlider_;
    juce::Label hint_;
    juce::Rectangle<int> graphArea_;
    int selectedBand_ { 0 };
    int dragBand_ { -1 };
};


class DashboardCard final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const auto base = getLookAndFeel().findColour(juce::ComboBox::backgroundColourId);
        const auto outline = getLookAndFeel().findColour(juce::ComboBox::outlineColourId);
        g.setColour(base.darker(0.12f));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(outline.withAlpha(0.85f));
        g.drawRoundedRectangle(r, 7.0f, 1.0f);
    }
};

class SessionGrid final : public juce::Component
{
public:
    SessionGrid(std::function<void(int)> sceneCallback,
                std::function<void(int)> trackCallback)
        : onScene_(std::move(sceneCallback)), onTrack_(std::move(trackCallback))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        static const juce::String trackNames[8] {
            juce::String::fromUTF8("Voz Líder"), "Coros", "Guitarra", "Bajo",
            "Teclado", juce::String::fromUTF8("Batería"), "Secuencias", "Click"
        };
        static const juce::String cells[8][8] {
            { "Intro", "Verse 1", "Pre-Chorus", "Chorus", "Verse 2", "Bridge", "Instrumental", "Ending" },
            { "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Ambiente", "Drone", "Shimmer", juce::String::fromUTF8("—") },
            { "Clean", "Drive", "Ambient", "Solo", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") },
            { "Intro", "Verse", "Chorus", "Bridge", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") },
            { "Piano", "Pad", "Strings", "Synth", "Ambient", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") },
            { "Kit 1", "Kit 2", "Loop", juce::String::fromUTF8("Percusión"), "Shaker", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") },
            { "FX 1", "FX 2", "Drone", "Risers", "Impactos", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") },
            { "Click", juce::String::fromUTF8("Guía"), "Metron", juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—"), juce::String::fromUTF8("—") }
        };
        static constexpr std::uint32_t trackColours[8] {
            0xff168cff, 0xff9a4cf3, 0xff1bcf7a, 0xffffc52f,
            0xffff4dad, 0xffff5353, 0xff13bfe7, 0xff87929c
        };

        auto area = getLocalBounds();
        g.setColour(juce::Colour(0xff101820));
        g.fillRoundedRectangle(area.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff35434f));
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 5.0f, 1.0f);

        constexpr int columns = 8;
        constexpr int rows = 8;
        const int headerH = 30;
        const int colW = std::max(1, area.getWidth() / columns);
        const int rowH = std::max(18, (area.getHeight() - headerH) / rows);

        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        for (int col = 0; col < columns; ++col)
        {
            const int x = col * colW;
            auto colour = juce::Colour(trackColours[col]);
            auto header = juce::Rectangle<int>(x, 0,
                col == columns - 1 ? area.getWidth() - x : colW, headerH).reduced(1);
            g.setColour(colour.withMultipliedBrightness(col == selectedTrack_ ? 1.15f : 0.9f));
            g.fillRoundedRectangle(header.toFloat(), 3.5f);
            g.setColour(col <= 4 ? juce::Colour(0xff071018) : juce::Colours::white);
            g.drawText(juce::String(col + 1) + "  " + trackNames[col], header.reduced(5, 0),
                       juce::Justification::centredLeft, true);

            for (int row = 0; row < rows; ++row)
            {
                const int y = headerH + row * rowH;
                auto cell = juce::Rectangle<int>(x, y,
                    col == columns - 1 ? area.getWidth() - x : colW,
                    row == rows - 1 ? area.getHeight() - y : rowH).reduced(1);

                const bool populated = juce::String(cells[col][row]) != juce::String::fromUTF8("—");
                auto base = populated ? colour.withAlpha(0.28f) : juce::Colour(0xff182129);
                if (row == selectedScene_)
                    base = populated ? colour.withAlpha(0.58f) : juce::Colour(0xff22303b);

                g.setColour(base);
                g.fillRoundedRectangle(cell.toFloat(), 2.0f);
                g.setColour(juce::Colour(0xff41515e).withAlpha(0.75f));
                g.drawRoundedRectangle(cell.toFloat(), 2.0f, 0.7f);

                if (populated)
                {
                    auto textArea = cell.reduced(5, 0);
                    g.setColour(row == selectedScene_ ? juce::Colours::white : juce::Colour(0xffd9e4ec));
                    g.setFont(juce::FontOptions(10.5f));
                    g.drawText(juce::String::fromUTF8("▶"), textArea.removeFromLeft(14), juce::Justification::centred);
                    g.drawText(cells[col][row], textArea, juce::Justification::centredLeft, true);
                }
            }
        }

        const int y = headerH + selectedScene_ * rowH;
        g.setColour(juce::Colour(0xffeaf5ff).withAlpha(0.78f));
        g.drawHorizontalLine(y, 1.0f, static_cast<float>(getWidth() - 1));
        g.drawHorizontalLine(std::min(getHeight() - 1, y + rowH), 1.0f, static_cast<float>(getWidth() - 1));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        constexpr int columns = 8;
        constexpr int rows = 8;
        const int headerH = 30;
        const int colW = std::max(1, getWidth() / columns);
        const int col = juce::jlimit(0, columns - 1, e.x / colW);
        selectedTrack_ = col;
        if (onTrack_) onTrack_(col);

        if (e.y >= headerH && col == 0)
        {
            const int rowH = std::max(18, (getHeight() - headerH) / rows);
            const int row = juce::jlimit(0, rows - 1, (e.y - headerH) / rowH);
            selectedScene_ = row;
            if (onScene_) onScene_(row);
        }
        repaint();
    }

private:
    std::function<void(int)> onScene_;
    std::function<void(int)> onTrack_;
    int selectedScene_ { 1 };
    int selectedTrack_ { 0 };
};
}

MainComponent::MixerStrip::MixerStrip(int index, const juce::String& title,
                                      std::atomic<float>& gain, std::atomic<float>& pan,
                                      std::atomic<bool>& muted, std::atomic<float>& meter,
                                      std::atomic<int>& bus, std::atomic<int>& dca,
                                      std::function<void(int)> onOpenFx)
    : index_(index), gain_(gain), pan_(pan), muted_(muted), meter_(meter),
      bus_(bus), dca_(dca), meterBar_(meterValue_), onOpenFx_(std::move(onOpenFx))
{
    setOpaque(false);

    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    addAndMakeVisible(title_);

    fader_.setSliderStyle(juce::Slider::LinearVertical);
    fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 22);
    fader_.setRange(-60.0, 6.0, 0.1);
    fader_.setDoubleClickReturnValue(true, 0.0);
    fader_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    fader_.setColour(juce::Slider::trackColourId, juce::Colour(accentDeep));
    fader_.onValueChange = [this] { gain_.store(dbToGain(fader_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(fader_);

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 20);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDoubleClickReturnValue(true, 0.0);
    panSlider_.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accent));
    panSlider_.onValueChange = [this] { pan_.store(static_cast<float>(panSlider_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(panSlider_);

    muteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    muteButton_.onClick = [this] { muted_.store(muteButton_.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(muteButton_);

    fxButton_.setButtonText("FX / PLUGINS");
    fxButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff173246));
    fxButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(text));
    fxButton_.setTooltip(juce::String::fromUTF8("Abrir los 8 slots de efectos de este Mixer Insert"));
    fxButton_.onClick = [this] { if (onOpenFx_) onOpenFx_(index_); };
    addAndMakeVisible(fxButton_);

    busBox_.addItem("MASTER", 1);
    for (int i = 0; i < kBuses; ++i) busBox_.addItem("BUS " + juce::String(i + 1), i + 2);
    busBox_.onChange = [this] { bus_.store(busBox_.getSelectedId() <= 1 ? -1 : busBox_.getSelectedId() - 2, std::memory_order_relaxed); };
    addAndMakeVisible(busBox_);

    dcaBox_.addItem("NO DCA", 1);
    for (int i = 0; i < kDcas; ++i) dcaBox_.addItem("DCA " + juce::String(i + 1), i + 2);
    dcaBox_.onChange = [this] { dca_.store(dcaBox_.getSelectedId() <= 1 ? -1 : dcaBox_.getSelectedId() - 2, std::memory_order_relaxed); };
    addAndMakeVisible(dcaBox_);

    meterBar_.setPercentageDisplay(false);
    meterBar_.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(good));
    meterBar_.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff090c10));
    addAndMakeVisible(meterBar_);
    syncFromModel();
}

void MainComponent::MixerStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(juce::Colour(border));
    g.drawRoundedRectangle(r, 8.0f, 1.0f);

    auto graph = r.reduced(8.0f);
    graph.setY(graph.getY() + 28.0f);
    graph.setHeight(45.0f);
    g.setColour(juce::Colour(background).withAlpha(0.72f));
    g.fillRoundedRectangle(graph, 4.0f);

    juce::Path waveform;
    const float mid = graph.getCentreY();
    const float step = graph.getWidth() / static_cast<float>(meterHistory_.size() - 1);
    for (std::size_t i = 0; i < meterHistory_.size(); ++i)
    {
        const float x = graph.getX() + step * static_cast<float>(i);
        const float amplitude = juce::jlimit(0.0f, 1.0f, meterHistory_[i]);
        const float y = graph.getBottom() - amplitude * graph.getHeight();
        if (i == 0) waveform.startNewSubPath(x, y);
        else waveform.lineTo(x, y);
    }
    g.setColour(juce::Colour(accent).withAlpha(0.9f));
    g.strokePath(waveform, juce::PathStrokeType(1.45f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
}

void MainComponent::MixerStrip::resized()
{
    auto r = getLocalBounds().reduced(8);
    title_.setBounds(r.removeFromTop(26));
    r.removeFromTop(49); // real-time signal history graph painted behind this band
    meterBar_.setBounds(r.removeFromRight(10).reduced(0, 14));
    r.removeFromRight(4);
    dcaBox_.setBounds(r.removeFromBottom(28));
    r.removeFromBottom(3);
    busBox_.setBounds(r.removeFromBottom(28));
    r.removeFromBottom(3);
    fxButton_.setBounds(r.removeFromBottom(26));
    r.removeFromBottom(3);
    muteButton_.setBounds(r.removeFromBottom(28));
    panSlider_.setBounds(r.removeFromBottom(76));
    fader_.setBounds(r.reduced(2, 4));
}

void MainComponent::MixerStrip::timerTick()
{
    const auto peak = meter_.exchange(0.0f, std::memory_order_relaxed);
    const auto target = juce::jlimit(0.0, 1.0, static_cast<double>(peak));
    meterValue_ = std::max(target, meterValue_ * 0.86);
    std::move(meterHistory_.begin() + 1, meterHistory_.end(), meterHistory_.begin());
    meterHistory_.back() = static_cast<float>(meterValue_);
    repaint();
}

void MainComponent::MixerStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-6f, gain_.load(std::memory_order_relaxed));
    fader_.setValue(juce::Decibels::gainToDecibels(gain, -60.0f), juce::dontSendNotification);
    panSlider_.setValue(pan_.load(std::memory_order_relaxed), juce::dontSendNotification);
    muteButton_.setToggleState(muted_.load(std::memory_order_relaxed), juce::dontSendNotification);
    const int bus = bus_.load(std::memory_order_relaxed);
    const int dca = dca_.load(std::memory_order_relaxed);
    busBox_.setSelectedId(bus >= 0 && bus < kBuses ? bus + 2 : 1, juce::dontSendNotification);
    dcaBox_.setSelectedId(dca >= 0 && dca < kDcas ? dca + 2 : 1, juce::dontSendNotification);
}

MainComponent::GroupStrip::GroupStrip(const juce::String& title, std::atomic<float>& gain, std::atomic<bool>& muted)
    : gain_(gain), muted_(muted)
{
    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    title_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(title_);
    fader_.setSliderStyle(juce::Slider::LinearVertical);
    fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 22);
    fader_.setRange(-60.0, 12.0, 0.1);
    fader_.setDoubleClickReturnValue(true, 0.0);
    fader_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    fader_.onValueChange = [this] { gain_.store(dbToGain(fader_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(fader_);
    muteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    muteButton_.onClick = [this] { muted_.store(muteButton_.getToggleState(), std::memory_order_relaxed); };
    addAndMakeVisible(muteButton_);
    syncFromModel();
}

void MainComponent::GroupStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(juce::Colour(0xff303946));
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
}

void MainComponent::GroupStrip::resized()
{
    auto r = getLocalBounds().reduced(7);
    title_.setBounds(r.removeFromTop(26));
    muteButton_.setBounds(r.removeFromBottom(28));
    fader_.setBounds(r.reduced(2, 4));
}

void MainComponent::GroupStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-6f, gain_.load(std::memory_order_relaxed));
    fader_.setValue(juce::Decibels::gainToDecibels(gain, -60.0f), juce::dontSendNotification);
    muteButton_.setToggleState(muted_.load(std::memory_order_relaxed), juce::dontSendNotification);
}

MainComponent::IemSendStrip::IemSendStrip(int sourceIndex, const juce::String& title,
                                          std::atomic<float>& gain, std::atomic<float>& pan)
    : sourceIndex_(sourceIndex), gain_(gain), panValue_(pan)
{
    title_.setText(title, juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    title_.setColour(juce::Label::textColourId, juce::Colour(text));
    title_.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    addAndMakeVisible(title_);

    level_.setSliderStyle(juce::Slider::LinearVertical);
    level_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 22);
    level_.setRange(-80.0, 6.0, 0.1);
    level_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    level_.onValueChange = [this] { gain_.store(dbToGain(level_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(level_);

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 20);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDoubleClickReturnValue(true, 0.0);
    panSlider_.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(accent));
    panSlider_.onValueChange = [this] { panValue_.store(static_cast<float>(panSlider_.getValue()), std::memory_order_relaxed); };
    addAndMakeVisible(panSlider_);
    syncFromModel();
}

void MainComponent::IemSendStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(panel2));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(juce::Colour(0xff303946));
    g.drawRoundedRectangle(r, 8.0f, 1.0f);
}

void MainComponent::IemSendStrip::resized()
{
    auto r = getLocalBounds().reduced(8);
    title_.setBounds(r.removeFromTop(26));
    panSlider_.setBounds(r.removeFromBottom(86));
    level_.setBounds(r.reduced(2, 4));
}

void MainComponent::IemSendStrip::syncFromModel()
{
    const auto gain = std::max(1.0e-8f, gain_.load(std::memory_order_relaxed));
    level_.setValue(juce::Decibels::gainToDecibels(gain, -80.0f), juce::dontSendNotification);
    panSlider_.setValue(panValue_.load(std::memory_order_relaxed), juce::dontSendNotification);
}

MainComponent::MainComponent()
{
    lookAndFeel_ = std::make_unique<j3ui::LookAndFeel>();
    setLookAndFeel(lookAndFeel_.get());
    syncPaletteGlobals(lookAndFeel_->palette());
    setOpaque(true);
    brandLogo_ = juce::ImageFileFormat::loadFrom(BinaryData::J3WorshipLogo_png,
                                                  BinaryData::J3WorshipLogo_pngSize);
    pluginFormatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int slot = 0; slot < kPluginSlots; ++slot)
        {
            pluginBypass_[ch][slot].store(false);
            pluginFaults_[ch][slot].store(0);
        }
    for (int i = 0; i < kMaxChannels; ++i)
    {
        channelGain_[i].store(dbToGain(-6.0));
        channelPan_[i].store(0.0f);
        channelMute_[i].store(false);
        channelMeter_[i].store(0.0f);
        channelBus_[i].store(-1);
        channelDca_[i].store(-1);
        channelHpf_[i].store(20.0f);
        channelLpf_[i].store(20000.0f);
        channelGate_[i].store(-60.0f);
        channelCompThreshold_[i].store(-18.0f);
        channelCompRatio_[i].store(3.0f);
        channelDenoise_[i].store(0.0f);
        channelDenoiseThreshold_[i].store(-60.0f);
        const float defaultEqFreq[4] { 200.0f, 600.0f, 1800.0f, 5400.0f };
        for (int band = 0; band < 4; ++band)
        {
            channelEqFreq_[i][band].store(defaultEqFreq[band]);
            channelEqGain_[i][band].store(0.0f);
            channelEqQ_[i][band].store(1.0f);
        }
        dspRevision_[i].store(1);
        dspAppliedRevision_[i] = 0;
    }
    for (int i = 0; i < kBuses; ++i)
    {
        busGain_[i].store(1.0f);
        busMute_[i].store(false);
    }
    for (int i = 0; i < kDcas; ++i)
    {
        dcaGain_[i].store(1.0f);
        dcaMute_[i].store(false);
    }
    for (int m = 0; m < kIemMixes; ++m)
    {
        iemMaster_[m].store(1.0f);
        iemMute_[m].store(false);
        iemOutLeft_[m].store(-1);
        iemOutRight_[m].store(-1);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            iemSendGain_[m][ch].store(0.0f);
            iemSendPan_[m][ch].store(0.0f);
        }
    }

    brandLabel_.setText("J3 WORSHIP", juce::dontSendNotification);
    brandLabel_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    brandLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    addAndMakeVisible(brandLabel_);

    versionLabel_.setText(juce::JUCEApplication::getInstance()->getApplicationVersion(), juce::dontSendNotification);
    versionLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    versionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff6f7b8a));
    addAndMakeVisible(versionLabel_);

    statusLabel_.setText("Audio: inicializando...", juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    addAndMakeVisible(statusLabel_);

    safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE · CHECK"), juce::dontSendNotification);
    safetyLabel_.setJustificationType(juce::Justification::centred);
    safetyLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff402a14));
    safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffc247));
    safetyLabel_.setTooltip(juce::String::fromUTF8("LIVE SAFE: estado global de audio, routing, XRUNs, grabación y protección de plugins."));
    safetyLabel_.setInterceptsMouseClicks(true, false);
    safetyLabel_.addMouseListener(this, false);
    addAndMakeVisible(safetyLabel_);

    for (int id = 1; id <= 6; ++id)
        themeBox_.addItem(j3ui::themeName(id), id);
    themeBox_.setSelectedId(1, juce::dontSendNotification);
    themeBox_.setTooltip("Tema visual de J3 Worship");
    themeBox_.onChange = [this] { applyTheme(themeBox_.getSelectedId()); };
    addAndMakeVisible(themeBox_);

    updateButton_.setVisible(true);
    updateButton_.setButtonText(juce::String::fromUTF8("COMPROBANDO…"));
    updateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    updateButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    updateButton_.onClick = [this] { beginUpdateInstall(); };
    addAndMakeVisible(updateButton_);

    audioSettingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    audioSettingsButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(text));
    audioSettingsButton_.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible(audioSettingsButton_);

    liveMonitorButton_.setTooltip("Habilita el monitoreo de entradas hacia el PA. Arranca apagado por seguridad.");
    liveMonitorButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    liveMonitorButton_.onClick = [this]
    {
        if (liveMonitorButton_.getToggleState())
        {
            auto* device = deviceManager_.getCurrentAudioDevice();
            if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
            {
                liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
                showAudioError(juce::String::fromUTF8("No hay entradas de audio activas. Configurá primero AUDIO / MIDI."));
                return;
            }
            if (!routeIsSafe(paLeft_.load(), paRight_.load(), clickOutput_.load()))
            {
                liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
                showAudioError("Routing bloqueado: CLICK no puede compartir una salida asignada al PA.");
                return;
            }
        }
        liveMonitorEnabled_.store(liveMonitorButton_.getToggleState(), std::memory_order_release);
        updateDiagnostics();
    };
    addAndMakeVisible(liveMonitorButton_);

    nowLabel_.setText("NOW: STOPPED", juce::dontSendNotification);
    nowLabel_.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    nowLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    nowLabel_.setJustificationType(juce::Justification::centred);
    nextLabel_.setText(juce::String::fromUTF8("NEXT: —"), juce::dontSendNotification);
    nextLabel_.setFont(juce::FontOptions(20.0f));
    nextLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffbdc6d3));
    nextLabel_.setJustificationType(juce::Justification::centred);
    liveHint_.setText(juce::String::fromUTF8("LIVE WORSHIP · cambios cuantizados · FREE / PAD mantiene ambiente · monitoreo PA arranca apagado"), juce::dontSendNotification);
    liveHint_.setJustificationType(juce::Justification::centred);
    liveHint_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    livePage_.addAndMakeVisible(nowLabel_);
    livePage_.addAndMakeVisible(nextLabel_);
    livePage_.addAndMakeVisible(liveHint_);

    const std::array<juce::String, 8> names { "INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "BRIDGE", "INSTRUMENTAL", "FREE / PAD", "ENDING" };
    const std::array<j3::SectionKind, 8> kinds { j3::SectionKind::Intro, j3::SectionKind::Verse, j3::SectionKind::PreChorus,
        j3::SectionKind::Chorus, j3::SectionKind::Bridge, j3::SectionKind::Instrumental, j3::SectionKind::FreePad, j3::SectionKind::Ending };
    for (size_t i = 0; i < names.size(); ++i)
    {
        auto b = std::make_unique<juce::TextButton>(names[i]);
        b->setColour(juce::TextButton::buttonColourId, i == 6 ? juce::Colour(0xff593d86) : juce::Colour(panel2));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(accent));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(text));
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        b->onClick = [this, name = names[i], kind = kinds[i]] { setLiveSection(name, kind, kind == j3::SectionKind::FreePad ? 1 : 4); };
        livePage_.addAndMakeVisible(*b);
        liveButtons_[i] = std::move(b);
    }

    setlistTitle_.setText("WORSHIP SETLIST", juce::dontSendNotification);
    setlistTitle_.setFont(juce::FontOptions(27.0f, juce::Font::bold));
    setlistTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    setlistPage_.addAndMakeVisible(setlistTitle_);

    setlistSongBox_.onChange = [this]
    {
        const int index = setlistSongBox_.getSelectedId() - 1;
        if (index >= 0 && setlist_.select(static_cast<std::size_t>(index)))
            refreshSetlistUi();
    };
    setlistPage_.addAndMakeVisible(setlistSongBox_);

    songNameEditor_.setTextToShowWhenEmpty("Song name", juce::Colour(mutedText));
    songArtistEditor_.setTextToShowWhenEmpty("Artist (optional)", juce::Colour(mutedText));
    songKeyEditor_.setTextToShowWhenEmpty("Key, e.g. G", juce::Colour(mutedText));
    for (auto* e : { &songNameEditor_, &songArtistEditor_, &songKeyEditor_ })
    {
        e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(panel2));
        e->setColour(juce::TextEditor::textColourId, juce::Colour(text));
        e->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff303946));
        setlistPage_.addAndMakeVisible(*e);
    }

    songBpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    songBpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 26);
    songBpmSlider_.setRange(40.0, 240.0, 0.1);
    songBpmSlider_.setValue(120.0, juce::dontSendNotification);
    songBpmSlider_.setTextValueSuffix(" BPM");
    songBpmSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    setlistPage_.addAndMakeVisible(songBpmSlider_);

    addSongButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    removeSongButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    loadSongButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315f46));
    addSongButton_.onClick = [this] { addSetlistSong(); };
    removeSongButton_.onClick = [this] { removeSetlistSong(); };
    loadSongButton_.onClick = [this] { loadSelectedSong(); };
    setlistPage_.addAndMakeVisible(addSongButton_);
    setlistPage_.addAndMakeVisible(removeSongButton_);
    setlistPage_.addAndMakeVisible(loadSongButton_);

    setlistInfoLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    setlistInfoLabel_.setFont(juce::FontOptions(18.0f));
    setlistInfoLabel_.setJustificationType(juce::Justification::topLeft);
    setlistPage_.addAndMakeVisible(setlistInfoLabel_);
    refreshSetlistUi();

    mixerPrevButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    mixerNextButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    mixerPrevButton_.onClick = [this] { setMixerBank(mixerBankStart_ - kVisibleChannels); };
    mixerNextButton_.onClick = [this] { setMixerBank(mixerBankStart_ + kVisibleChannels); };
    mixerBankLabel_.setJustificationType(juce::Justification::centred);
    mixerBankLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    mixerBankLabel_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    mixerPage_.addAndMakeVisible(mixerPrevButton_);
    mixerPage_.addAndMakeVisible(mixerNextButton_);
    mixerPage_.addAndMakeVisible(mixerBankLabel_);
    rebuildMixerBank();

    dashboardLeftCard_ = std::make_unique<DashboardCard>();
    dashboardRightCard_ = std::make_unique<DashboardCard>();
    dashboardBottomCard_ = std::make_unique<DashboardCard>();
    mixerPage_.addAndMakeVisible(*dashboardLeftCard_);
    mixerPage_.addAndMakeVisible(*dashboardRightCard_);
    mixerPage_.addAndMakeVisible(*dashboardBottomCard_);

    sessionGrid_ = std::make_unique<SessionGrid>(
        [this, names, kinds](int scene)
        {
            const int index = juce::jlimit(0, static_cast<int>(names.size()) - 1, scene);
            setLiveSection(names[static_cast<std::size_t>(index)],
                           kinds[static_cast<std::size_t>(index)],
                           kinds[static_cast<std::size_t>(index)] == j3::SectionKind::FreePad ? 1 : 4);
            refreshDashboard();
        },
        [this](int channel)
        {
            const int ch = juce::jlimit(0, kMaxChannels - 1, channel);
            setMixerBank((ch / kVisibleChannels) * kVisibleChannels);
            pluginChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
            dspChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
            selectedDspChannel_ = ch;
            refreshPluginUi();
            refreshDspUi();
        });
    mixerPage_.addAndMakeVisible(*sessionGrid_);

    dashboardSetlistTitle_.setText(juce::String::fromUTF8("BIBLIOTECA · SETLIST"), juce::dontSendNotification);
    dashboardSetlistTitle_.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    dashboardSongInfo_.setFont(juce::FontOptions(14.0f));
    dashboardSongInfo_.setJustificationType(juce::Justification::topLeft);
    dashboardSongBox_.onChange = [this]
    {
        const int id = dashboardSongBox_.getSelectedId();
        if (id > 0 && setlist_.select(static_cast<std::size_t>(id - 1)))
        {
            setlistSongBox_.setSelectedId(id, juce::dontSendNotification);
            refreshSetlistUi();
        }
    };
    dashboardLoadSongButton_.onClick = [this] { loadSelectedSong(); };
    mixerPage_.addAndMakeVisible(dashboardSetlistTitle_);
    mixerPage_.addAndMakeVisible(dashboardSongBox_);
    mixerPage_.addAndMakeVisible(dashboardLoadSongButton_);
    mixerPage_.addAndMakeVisible(dashboardSongInfo_);

    dashboardIemTitle_.setText(juce::String::fromUTF8("IEM · MONITORES"), juce::dontSendNotification);
    dashboardIemTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    for (int i = 0; i < kIemMixes; ++i)
        dashboardIemMixBox_.addItem("MIX " + juce::String(i + 1), i + 1);
    dashboardIemMixBox_.setSelectedId(1, juce::dontSendNotification);
    dashboardIemMixBox_.onChange = [this]
    {
        selectedIemMix_ = juce::jlimit(0, kIemMixes - 1, dashboardIemMixBox_.getSelectedId() - 1);
        iemMixBox_.setSelectedId(selectedIemMix_ + 1, juce::dontSendNotification);
        rebuildIemBank();
        refreshIemUi();
    };
    dashboardIemMasterSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    dashboardIemMasterSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
    dashboardIemMasterSlider_.setRange(-60.0, 12.0, 0.1);
    dashboardIemMasterSlider_.setTextValueSuffix(" dB");
    dashboardIemMasterSlider_.onValueChange = [this]
    {
        const int mix = juce::jlimit(0, kIemMixes - 1, dashboardIemMixBox_.getSelectedId() - 1);
        iemMaster_[mix].store(dbToGain(dashboardIemMasterSlider_.getValue()), std::memory_order_relaxed);
        if (mix == selectedIemMix_)
            iemMasterSlider_.setValue(dashboardIemMasterSlider_.getValue(), juce::dontSendNotification);
    };
    mixerPage_.addAndMakeVisible(dashboardIemTitle_);
    mixerPage_.addAndMakeVisible(dashboardIemMixBox_);
    mixerPage_.addAndMakeVisible(dashboardIemMasterSlider_);

    dashboardRecordTitle_.setText(juce::String::fromUTF8("GRABACIÓN"), juce::dontSendNotification);
    dashboardRecordTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    dashboardRecordButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(danger).darker(0.35f));
    dashboardRecordButton_.onClick = [this] { startStopRecording(); };
    dashboardRecordInfo_.setFont(juce::FontOptions(13.5f));
    dashboardRecordInfo_.setJustificationType(juce::Justification::topLeft);
    mixerPage_.addAndMakeVisible(dashboardRecordTitle_);
    mixerPage_.addAndMakeVisible(dashboardRecordButton_);
    mixerPage_.addAndMakeVisible(dashboardRecordInfo_);

    dashboardPluginsTitle_.setText("PLUGINS VST3", juce::dontSendNotification);
    dashboardPluginsTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    dashboardPluginsInfo_.setFont(juce::FontOptions(13.5f));
    dashboardPluginsInfo_.setJustificationType(juce::Justification::topLeft);
    mixerPage_.addAndMakeVisible(dashboardPluginsTitle_);
    mixerPage_.addAndMakeVisible(dashboardPluginsInfo_);

    dashboardLiveTitle_.setText(juce::String::fromUTF8("LIVE · SECCIONES"), juce::dontSendNotification);
    dashboardLiveTitle_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    dashboardStopButton_.setButtonText("STOP ALL");
    dashboardStopButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7d1f2b));
    dashboardStopButton_.setTooltip(L"Silencia PA, pads, click y reproducción inmediatamente. La grabación continúa.");
    dashboardStopButton_.onClick = [this] { panicStopAll(); };
    mixerPage_.addAndMakeVisible(dashboardStopButton_);
    dashboardPadButton_.onClick = [this]
    {
        padEnabledButton_.triggerClick();
        refreshDashboard();
    };
    dashboardClickButton_.onClick = [this]
    {
        clickEnabledButton_.triggerClick();
        refreshDashboard();
    };
    dashboardTempoLabel_.setJustificationType(juce::Justification::centred);
    dashboardTempoLabel_.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    mixerPage_.addAndMakeVisible(dashboardLiveTitle_);
    mixerPage_.addAndMakeVisible(dashboardPadButton_);
    mixerPage_.addAndMakeVisible(dashboardClickButton_);
    mixerPage_.addAndMakeVisible(dashboardTempoLabel_);

    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto b = std::make_unique<juce::TextButton>(names[i]);
        b->setColour(juce::TextButton::buttonColourId,
                     i == 3 ? juce::Colour(danger).darker(0.28f)
                            : (i == 6 ? juce::Colour(0xff593d86) : juce::Colour(panel3)));
        b->onClick = [this, name = names[i], kind = kinds[i]]
        {
            setLiveSection(name, kind, kind == j3::SectionKind::FreePad ? 1 : 4);
            refreshDashboard();
        };
        mixerPage_.addAndMakeVisible(*b);
        dashboardSectionButtons_[i] = std::move(b);
    }
    refreshDashboard();

    dspTitle_.setText(juce::String::fromUTF8("J3 CHANNEL DSP · EQ · GATE · COMP · DENOISE"), juce::dontSendNotification);
    dspTitle_.setFont(juce::FontOptions(23.0f, juce::Font::bold));
    dspTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    dspPage_.addAndMakeVisible(dspTitle_);

    for (int i = 0; i < kMaxChannels; ++i)
        dspChannelBox_.addItem("MIXER INSERT " + juce::String(i + 1), i + 1);
    dspChannelBox_.setSelectedId(1, juce::dontSendNotification);
    dspChannelBox_.onChange = [this]
    {
        selectedDspChannel_ = juce::jlimit(0, kMaxChannels - 1, dspChannelBox_.getSelectedId() - 1);
        refreshDspUi();
    };
    dspPage_.addAndMakeVisible(dspChannelBox_);

    auto setupHorizontal = [this](juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 24);
        s.setRange(lo, hi, step);
        s.setTextValueSuffix(suffix);
        s.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
        dspPage_.addAndMakeVisible(s);
    };
    setupHorizontal(hpfSlider_, 20.0, 500.0, 1.0, " Hz HPF");
    setupHorizontal(lpfSlider_, 2000.0, 20000.0, 10.0, " Hz LPF");
    setupHorizontal(gateSlider_, -80.0, -20.0, 0.5, " dB GATE");
    setupHorizontal(compThresholdSlider_, -40.0, 0.0, 0.5, " dB COMP");
    setupHorizontal(compRatioSlider_, 1.0, 10.0, 0.1, ":1");
    setupHorizontal(denoiseSlider_, 0.0, 1.0, 0.01, " DENOISE");
    setupHorizontal(denoiseThresholdSlider_, -80.0, -30.0, 0.5, " dB NOISE");

    hpfSlider_.onValueChange = [this] { channelHpf_[selectedDspChannel_].store(static_cast<float>(hpfSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    lpfSlider_.onValueChange = [this] { channelLpf_[selectedDspChannel_].store(static_cast<float>(lpfSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    gateSlider_.onValueChange = [this] { channelGate_[selectedDspChannel_].store(static_cast<float>(gateSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    compThresholdSlider_.onValueChange = [this] { channelCompThreshold_[selectedDspChannel_].store(static_cast<float>(compThresholdSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    compRatioSlider_.onValueChange = [this] { channelCompRatio_[selectedDspChannel_].store(static_cast<float>(compRatioSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    denoiseSlider_.onValueChange = [this] { channelDenoise_[selectedDspChannel_].store(static_cast<float>(denoiseSlider_.getValue())); markDspDirty(selectedDspChannel_); };
    denoiseThresholdSlider_.onValueChange = [this] { channelDenoiseThreshold_[selectedDspChannel_].store(static_cast<float>(denoiseThresholdSlider_.getValue())); markDspDirty(selectedDspChannel_); };

    for (int band = 0; band < 4; ++band)
    {
        eqBandLabels_[band].setText("EQ " + juce::String(band + 1), juce::dontSendNotification);
        eqBandLabels_[band].setJustificationType(juce::Justification::centred);
        eqBandLabels_[band].setColour(juce::Label::textColourId, juce::Colour(text));
        dspPage_.addAndMakeVisible(eqBandLabels_[band]);
        setupHorizontal(eqFreqSliders_[band], 40.0, 18000.0, 1.0, " Hz");
        setupHorizontal(eqGainSliders_[band], -18.0, 18.0, 0.1, " dB");
        setupHorizontal(eqQSliders_[band], 0.20, 12.0, 0.05, " Q");
        eqFreqSliders_[band].onValueChange = [this, band]
        {
            channelEqFreq_[selectedDspChannel_][band].store(static_cast<float>(eqFreqSliders_[band].getValue()));
            markDspDirty(selectedDspChannel_);
        };
        eqGainSliders_[band].onValueChange = [this, band]
        {
            channelEqGain_[selectedDspChannel_][band].store(static_cast<float>(eqGainSliders_[band].getValue()));
            markDspDirty(selectedDspChannel_);
        };
        eqQSliders_[band].onValueChange = [this, band]
        {
            channelEqQ_[selectedDspChannel_][band].store(static_cast<float>(eqQSliders_[band].getValue()));
            markDspDirty(selectedDspChannel_);
        };
    }

    for (auto* b : { &vocalPresetButton_, &kickPresetButton_, &snarePresetButton_, &guitarPresetButton_, &bassPresetButton_, &resetDspButton_ })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
        dspPage_.addAndMakeVisible(*b);
    }
    vocalPresetButton_.onClick = [this] { applyDspPreset(1); };
    kickPresetButton_.onClick = [this] { applyDspPreset(2); };
    snarePresetButton_.onClick = [this] { applyDspPreset(3); };
    guitarPresetButton_.onClick = [this] { applyDspPreset(4); };
    bassPresetButton_.onClick = [this] { applyDspPreset(5); };
    resetDspButton_.onClick = [this] { applyDspPreset(0); };
    refreshDspUi();

    groupsTitle_.setText("8 SUBGROUPS + 8 DCA", juce::dontSendNotification);
    groupsTitle_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    groupsTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    groupsPage_.addAndMakeVisible(groupsTitle_);
    for (int i = 0; i < kBuses; ++i)
    {
        busStrips_[i] = std::make_unique<GroupStrip>("BUS " + juce::String(i + 1), busGain_[i], busMute_[i]);
        groupsPage_.addAndMakeVisible(*busStrips_[i]);
    }
    for (int i = 0; i < kDcas; ++i)
    {
        dcaStrips_[i] = std::make_unique<GroupStrip>("DCA " + juce::String(i + 1), dcaGain_[i], dcaMute_[i]);
        groupsPage_.addAndMakeVisible(*dcaStrips_[i]);
    }

    iemTitle_.setText("16 STEREO IEM MIXES", juce::dontSendNotification);
    iemTitle_.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    iemTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    iemPage_.addAndMakeVisible(iemTitle_);
    for (int i = 0; i < kIemMixes; ++i)
        iemMixBox_.addItem("IEM " + juce::String(i + 1), i + 1);
    iemMixBox_.setSelectedId(1, juce::dontSendNotification);
    iemMixBox_.onChange = [this]
    {
        selectedIemMix_ = juce::jlimit(0, kIemMixes - 1, iemMixBox_.getSelectedId() - 1);
        rebuildIemBank();
        refreshIemUi();
    };
    iemPage_.addAndMakeVisible(iemMixBox_);

    iemRouteLabel_.setText("PHYSICAL OUTPUT PAIR", juce::dontSendNotification);
    iemRouteLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    iemPage_.addAndMakeVisible(iemRouteLabel_);
    iemOutLeftBox_.onChange = [this] { applyIemRoutingFromControls(); };
    iemOutRightBox_.onChange = [this] { applyIemRoutingFromControls(); };
    iemPage_.addAndMakeVisible(iemOutLeftBox_);
    iemPage_.addAndMakeVisible(iemOutRightBox_);

    iemMasterSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    iemMasterSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 24);
    iemMasterSlider_.setRange(-60.0, 12.0, 0.1);
    iemMasterSlider_.setTextValueSuffix(" dB");
    iemMasterSlider_.onValueChange = [this]
    {
        iemMaster_[selectedIemMix_].store(dbToGain(iemMasterSlider_.getValue()), std::memory_order_relaxed);
    };
    iemPage_.addAndMakeVisible(iemMasterSlider_);
    iemMuteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(mutedText));
    iemMuteButton_.onClick = [this] { iemMute_[selectedIemMix_].store(iemMuteButton_.getToggleState(), std::memory_order_relaxed); };
    iemPage_.addAndMakeVisible(iemMuteButton_);

    iemPrevButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    iemNextButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    iemPrevButton_.onClick = [this]
    {
        iemBankStart_ = juce::jlimit(0, kMaxChannels - kVisibleChannels, iemBankStart_ - kVisibleChannels);
        rebuildIemBank();
    };
    iemNextButton_.onClick = [this]
    {
        iemBankStart_ = juce::jlimit(0, kMaxChannels - kVisibleChannels, iemBankStart_ + kVisibleChannels);
        rebuildIemBank();
    };
    iemBankLabel_.setJustificationType(juce::Justification::centred);
    iemBankLabel_.setColour(juce::Label::textColourId, juce::Colour(text));
    iemPage_.addAndMakeVisible(iemPrevButton_);
    iemPage_.addAndMakeVisible(iemNextButton_);
    iemPage_.addAndMakeVisible(iemBankLabel_);
    rebuildIemBank();

    pluginsTitle_.setText("MIXER FX RACK | DIRECT SLOT WORKFLOW", juce::dontSendNotification);
    pluginsTitle_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    pluginsTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    pluginsPage_.addAndMakeVisible(pluginsTitle_);

    pluginChainTitle_.setText("FX CHAIN", juce::dontSendNotification);
    pluginChainTitle_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    pluginChainTitle_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    pluginsPage_.addAndMakeVisible(pluginChainTitle_);

    pluginBrowserTitle_.setText("ADD EFFECT", juce::dontSendNotification);
    pluginBrowserTitle_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    pluginBrowserTitle_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    pluginsPage_.addAndMakeVisible(pluginBrowserTitle_);

    nativeEqButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff173f54));
    nativeEqButton_.setTooltip("EQ nativo J3: 4 bandas + HPF/LPF. Click para abrir el editor visual.");
    nativeEqButton_.onClick = [this] { openNativeEqEditor(); };
    pluginsPage_.addAndMakeVisible(nativeEqButton_);

    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        auto button = std::make_unique<juce::TextButton>();
        button->setTooltip("Seleccioná este slot. Elegí un plugin de la lista de la derecha y hacé doble click para cargarlo.");
        button->setClickingTogglesState(false);
        button->onClick = [this, slot]
        {
            pluginSlotBox_.setSelectedId(slot + 1, juce::dontSendNotification);
            refreshPluginUi();
            pluginCatalogList_.grabKeyboardFocus();
        };
        pluginsPage_.addAndMakeVisible(*button);
        pluginSlotButtons_[slot] = std::move(button);
    }

    pluginSearch_.setTextToShowWhenEmpty("BUSCAR PLUGIN POR NOMBRE...", juce::Colour(mutedText));
    pluginSearch_.setTooltip(juce::String::fromUTF8("Buscá por nombre o fabricante. El resultado se filtra mientras escribís."));
    pluginSearch_.onTextChange = [this] { refreshPluginBrowser(); };
    pluginsPage_.addAndMakeVisible(pluginSearch_);

    for (const auto& category : { "ALL", "EQ", "COMPRESSOR", "REVERB", "DELAY", "SATURATION",
                                  "GUITAR", "INSTRUMENTS", "UTILITY", "FAVORITES",
                                  "RECENTLY USED", "J3 PLUGINS" })
        pluginCategoryBox_.addItem(category, pluginCategoryBox_.getNumItems() + 1);
    pluginCategoryBox_.setSelectedId(1, juce::dontSendNotification);
    pluginCategoryBox_.onChange = [this] { refreshPluginBrowser(); };
    pluginsPage_.addAndMakeVisible(pluginCategoryBox_);

    for (int i = 0; i < kMaxChannels; ++i)
        pluginChannelBox_.addItem("MIXER INSERT " + juce::String(i + 1), i + 1);
    pluginChannelBox_.setSelectedId(1, juce::dontSendNotification);
    pluginChannelBox_.onChange = [this] { refreshPluginUi(); };
    pluginsPage_.addAndMakeVisible(pluginChannelBox_);

    for (int i = 0; i < kPluginSlots; ++i)
        pluginSlotBox_.addItem("FX SLOT " + juce::String(i + 1), i + 1);
    pluginSlotBox_.setSelectedId(1, juce::dontSendNotification);
    pluginSlotBox_.onChange = [this] { refreshPluginUi(); };
    pluginsPage_.addAndMakeVisible(pluginSlotBox_);

    pluginCatalogBox_.setTooltip(juce::String::fromUTF8("Modelo interno de selección del navegador VST3."));
    pluginCatalogBox_.onChange = [this] { refreshPluginUi(); };

    pluginCatalogList_.setRowHeight(42);
    pluginCatalogList_.setMultipleSelectionEnabled(false);
    pluginCatalogList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0b1117));
    pluginCatalogList_.setColour(juce::ListBox::outlineColourId, juce::Colour(border));
    pluginCatalogList_.setOutlineThickness(1);
    pluginCatalogList_.setTooltip("Plugins encontrados. Doble click para cargar el seleccionado en el slot activo.");
    pluginsPage_.addAndMakeVisible(pluginCatalogList_);

    favoritePluginButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    favoritePluginButton_.setTooltip(juce::String::fromUTF8("Marcá este plugin para encontrarlo rápido en FAVORITES."));
    favoritePluginButton_.onClick = [this]
    {
        const int row = pluginCatalogBox_.getSelectedId() - 1;
        if (row < 0 || row >= static_cast<int>(pluginBrowserIndices_.size()))
            return;
        const int actual = pluginBrowserIndices_[static_cast<std::size_t>(row)];
        if (actual < 0 || actual >= static_cast<int>(pluginDescriptions_.size()))
            return;

        const auto& description = pluginDescriptions_[static_cast<std::size_t>(actual)];
        const auto key = pluginDescriptionKey(description);
        if (favoritePluginButton_.getToggleState())
        {
            favoritePluginPaths_.removeString(description.fileOrIdentifier);
            favoritePluginPaths_.addIfNotAlreadyThere(key);
        }
        else
        {
            favoritePluginPaths_.removeString(key);
            favoritePluginPaths_.removeString(description.fileOrIdentifier);
        }
        saveAppState();
        refreshPluginBrowser();
    };
    pluginsPage_.addAndMakeVisible(favoritePluginButton_);

    scanPluginsButton_.setButtonText("ESCANEAR CARPETA...");
    pluginLocationsButton_.setButtonText("ESCANEAR TODO");
    loadPluginButton_.setButtonText("CARGAR EN SLOT");
    scanPluginsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    pluginLocationsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    scanPluginsButton_.setTooltip("Elegí la carpeta donde están tus VST3. J3 la agrega y la escanea.");
    pluginLocationsButton_.setTooltip("Escanea las ubicaciones estándar de Windows y todas las carpetas que agregaste.");
    loadPluginButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315f46));
    removePluginButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    movePluginUpButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    movePluginDownButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    openPluginEditorButton_.setButtonText("ABRIR PLUGIN");
    openPluginEditorButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315f46));
    bypassPluginButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    scanPluginsButton_.onClick = [this] { chooseAdditionalVst3Folder(); };
    pluginLocationsButton_.onClick = [this] { scanVst3Plugins(); };
    loadPluginButton_.onClick = [this] { loadSelectedPlugin(); };
    removePluginButton_.onClick = [this] { removeSelectedPlugin(); };
    movePluginUpButton_.onClick = [this] { moveSelectedPlugin(-1); };
    movePluginDownButton_.onClick = [this] { moveSelectedPlugin(1); };
    openPluginEditorButton_.onClick = [this] { openSelectedPluginEditor(); };
    bypassPluginButton_.onClick = [this]
    {
        const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
        const int slot = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);
        pluginBypass_[ch][slot].store(bypassPluginButton_.getToggleState(), std::memory_order_release);
        saveAppState();
        refreshPluginUi();
    };
    pluginsPage_.addAndMakeVisible(scanPluginsButton_);
    pluginsPage_.addAndMakeVisible(pluginLocationsButton_);
    pluginsPage_.addAndMakeVisible(loadPluginButton_);
    pluginsPage_.addAndMakeVisible(removePluginButton_);
    pluginsPage_.addAndMakeVisible(movePluginUpButton_);
    pluginsPage_.addAndMakeVisible(movePluginDownButton_);
    pluginsPage_.addAndMakeVisible(bypassPluginButton_);
    pluginsPage_.addAndMakeVisible(openPluginEditorButton_);

    pluginStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    pluginStatusLabel_.setFont(juce::FontOptions(16.5f));
    pluginStatusLabel_.setJustificationType(juce::Justification::topLeft);
    pluginsPage_.addAndMakeVisible(pluginStatusLabel_);
    refreshPluginUi();

    padTitle_.setText("J3 PADS", juce::dontSendNotification);
    padTitle_.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    padTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    padPage_.addAndMakeVisible(padTitle_);

    const std::array<juce::String, 12> padKeys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < static_cast<int>(padKeys.size()); ++i)
        padKeyBox_.addItem(padKeys[static_cast<std::size_t>(i)], i + 1);
    padKeyBox_.setSelectedId(1, juce::dontSendNotification);
    padKeyBox_.onChange = [this]
    {
        ambientPad_.setRootMidi(60 + juce::jlimit(0, 11, padKeyBox_.getSelectedId() - 1));
        refreshPadUi();
        saveAppState();
    };
    padPage_.addAndMakeVisible(padKeyBox_);

    padMinorButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    padMinorButton_.onClick = [this]
    {
        ambientPad_.setMinor(padMinorButton_.getToggleState());
        refreshPadUi();
        saveAppState();
    };
    padPage_.addAndMakeVisible(padMinorButton_);

    padEnabledButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    padEnabledButton_.onClick = [this]
    {
        if (padEnabledButton_.getToggleState() && !padToPa_.load(std::memory_order_relaxed))
        {
            padEnabledButton_.setToggleState(false, juce::dontSendNotification);
            showAudioError(juce::String::fromUTF8("J3 PADS no tiene una ruta activa. Habilitá ROUTE TO PA."));
            return;
        }
        ambientPad_.setEnabled(padEnabledButton_.getToggleState());
        refreshPadUi();
    };
    padPage_.addAndMakeVisible(padEnabledButton_);

    padToPaButton_.setToggleState(true, juce::dontSendNotification);
    padToPaButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    padToPaButton_.onClick = [this]
    {
        padToPa_.store(padToPaButton_.getToggleState(), std::memory_order_release);
        if (!padToPaButton_.getToggleState())
        {
            ambientPad_.setEnabled(false);
            padEnabledButton_.setToggleState(false, juce::dontSendNotification);
        }
        refreshPadUi();
        saveAppState();
    };
    padPage_.addAndMakeVisible(padToPaButton_);

    padVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    padVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 26);
    padVolumeSlider_.setRange(0.0, 1.0, 0.01);
    padVolumeSlider_.setValue(0.18, juce::dontSendNotification);
    padVolumeSlider_.setTextValueSuffix(" VOL");
    padVolumeSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    padVolumeSlider_.onValueChange = [this]
    {
        ambientPad_.setVolume(static_cast<float>(padVolumeSlider_.getValue()));
        refreshPadUi();
    };
    padPage_.addAndMakeVisible(padVolumeSlider_);

    padInfoLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    padInfoLabel_.setFont(juce::FontOptions(17.0f));
    padInfoLabel_.setJustificationType(juce::Justification::topLeft);
    padPage_.addAndMakeVisible(padInfoLabel_);
    refreshPadUi();

    clickTitle_.setText("J3 CLICK", juce::dontSendNotification);
    clickTitle_.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    clickTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    clickPage_.addAndMakeVisible(clickTitle_);

    clickEnabledButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    clickEnabledButton_.onClick = [this]
    {
        clickAudible_.store(clickEnabledButton_.getToggleState(), std::memory_order_release);
        if (clickAudible_.load())
        {
            transportRunning_.store(true, std::memory_order_release);
            clickGenerator_.setEnabled(true);
        }
        if (clickAudible_.load() && clickOutput_.load() < 0)
        {
            clickEnabledButton_.setToggleState(false, juce::dontSendNotification);
            clickAudible_.store(false, std::memory_order_release);
            if (!liveStarted_)
            {
                transportRunning_.store(false, std::memory_order_release);
                clickGenerator_.setEnabled(false);
            }
            showAudioError(juce::String::fromUTF8("Elegí una salida CLICK / GUIDE distinta del PA antes de activar el click."));
        }
        updateClickUi();
        saveAppState();
    };
    clickPage_.addAndMakeVisible(clickEnabledButton_);

    bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 28);
    bpmSlider_.setRange(40.0, 240.0, 0.1);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(accent));
    bpmSlider_.onValueChange = [this]
    {
        clickGenerator_.setTempo(bpmSlider_.getValue());
        dawWorkspace_.setTempoFromHost(bpmSlider_.getValue());
        if (liveStarted_) liveEngine_.setTempo(bpmSlider_.getValue(), clickGenerator_.numerator());
        updateClickUi();
    };
    clickPage_.addAndMakeVisible(bpmSlider_);

    tapTempoButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    tapTempoButton_.onClick = [this] { handleTapTempo(); };
    clickPage_.addAndMakeVisible(tapTempoButton_);

    timeSignatureBox_.addItem("4/4", 1);
    timeSignatureBox_.addItem("3/4", 2);
    timeSignatureBox_.addItem("6/8", 3);
    timeSignatureBox_.addItem("2/4", 4);
    timeSignatureBox_.setSelectedId(1, juce::dontSendNotification);
    timeSignatureBox_.onChange = [this]
    {
        const int id = timeSignatureBox_.getSelectedId();
        const int n = id == 2 ? 3 : (id == 3 ? 6 : (id == 4 ? 2 : 4));
        const int d = id == 3 ? 8 : 4;
        clickGenerator_.setTimeSignature(n, d);
        if (liveStarted_) liveEngine_.setTempo(bpmSlider_.getValue(), n);
        saveAppState();
    };
    clickPage_.addAndMakeVisible(timeSignatureBox_);

    subdivisionBox_.addItem("Quarter", 1);
    subdivisionBox_.addItem("Eighth", 2);
    subdivisionBox_.addItem("Sixteenth", 3);
    subdivisionBox_.setSelectedId(1, juce::dontSendNotification);
    subdivisionBox_.onChange = [this]
    {
        const int id = subdivisionBox_.getSelectedId();
        clickGenerator_.setSubdivision(id == 3 ? 4 : (id == 2 ? 2 : 1));
        saveAppState();
    };
    clickPage_.addAndMakeVisible(subdivisionBox_);

    accentButton_.setToggleState(true, juce::dontSendNotification);
    accentButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    accentButton_.onClick = [this]
    {
        clickGenerator_.setAccentEnabled(accentButton_.getToggleState());
        saveAppState();
    };
    clickPage_.addAndMakeVisible(accentButton_);

    clickVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    clickVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 26);
    clickVolumeSlider_.setRange(0.0, 1.0, 0.01);
    clickVolumeSlider_.setValue(0.35, juce::dontSendNotification);
    clickVolumeSlider_.onValueChange = [this]
    {
        clickGenerator_.setLevel(static_cast<float>(clickVolumeSlider_.getValue()));
    };
    clickPage_.addAndMakeVisible(clickVolumeSlider_);

    clickRouteLabel_.setColour(juce::Label::textColourId, juce::Colour(mutedText));
    clickRouteLabel_.setFont(juce::FontOptions(16.0f));
    clickRouteLabel_.setJustificationType(juce::Justification::topLeft);
    clickPage_.addAndMakeVisible(clickRouteLabel_);

    recordingTitle_.setText("RECORD SERVICE", juce::dontSendNotification);
    recordingTitle_.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    recordingTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    recordingPage_.addAndMakeVisible(recordingTitle_);
    recordButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b2430));
    recordButton_.onClick = [this] { startStopRecording(); };
    recordingPage_.addAndMakeVisible(recordButton_);
    recordingStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    recordingStatusLabel_.setFont(juce::FontOptions(17.0f));
    recordingStatusLabel_.setJustificationType(juce::Justification::topLeft);
    recordingPage_.addAndMakeVisible(recordingStatusLabel_);
    openRecordingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    openRecordingsButton_.onClick = [this] { recordingsRoot().revealToUser(); };
    recordingPage_.addAndMakeVisible(openRecordingsButton_);
    updateRecordingUi();

    setupTitle_.setText("AUDIO & ROUTING", juce::dontSendNotification);
    setupTitle_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    setupTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    setupPage_.addAndMakeVisible(setupTitle_);

    setupDeviceLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffbdc6d3));
    setupDeviceLabel_.setFont(juce::FontOptions(16.0f));
    setupDeviceLabel_.setJustificationType(juce::Justification::topLeft);
    setupPage_.addAndMakeVisible(setupDeviceLabel_);

    routingTitle_.setText("SAFE OUTPUT ROUTING", juce::dontSendNotification);
    routingTitle_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    routingTitle_.setColour(juce::Label::textColourId, juce::Colour(text));
    setupPage_.addAndMakeVisible(routingTitle_);

    paLeftLabel_.setText("PA LEFT / MONO", juce::dontSendNotification);
    paRightLabel_.setText("PA RIGHT", juce::dontSendNotification);
    clickLabel_.setText("CLICK / GUIDE", juce::dontSendNotification);
    for (auto* label : { &paLeftLabel_, &paRightLabel_, &clickLabel_ })
    {
        label->setColour(juce::Label::textColourId, juce::Colour(mutedText));
        setupPage_.addAndMakeVisible(*label);
    }
    setupPage_.addAndMakeVisible(paLeftBox_);
    setupPage_.addAndMakeVisible(paRightBox_);
    setupPage_.addAndMakeVisible(clickBox_);
    paLeftBox_.onChange = [this] { applyRoutingFromControls(); };
    paRightBox_.onChange = [this] { applyRoutingFromControls(); };
    clickBox_.onChange = [this] { applyRoutingFromControls(); };

    rescanButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    testOutputButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    driverPanelButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(panel3));
    rescanButton_.onClick = [this]
    {
        scanAvailableDevices();
        updateDiagnostics();
        refreshRoutingControls();
    };
    testOutputButton_.onClick = [this]
    {
        if (deviceManager_.getCurrentAudioDevice() == nullptr)
            showAudioError("No hay un dispositivo de audio activo.");
        else
            deviceManager_.playTestSound();
    };
    driverPanelButton_.onClick = [this] { openDriverControlPanel(); };
    setupPage_.addAndMakeVisible(rescanButton_);
    setupPage_.addAndMakeVisible(testOutputButton_);
    setupPage_.addAndMakeVisible(driverPanelButton_);

    diagnosticsLabel_.setFont(juce::FontOptions(16.5f));
    diagnosticsLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd4dbe5));
    diagnosticsLabel_.setJustificationType(juce::Justification::topLeft);
    diagnosticsPage_.addAndMakeVisible(diagnosticsLabel_);
    runCheckButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
    runCheckButton_.onClick = [this]
    {
        scanAvailableDevices();
        updateDiagnostics();
    };
    diagnosticsPage_.addAndMakeVisible(runCheckButton_);

    dawWorkspace_.onBpmChanged = [this](double value)
    {
        if (std::abs(bpmSlider_.getValue() - value) > 0.001)
            bpmSlider_.setValue(value, juce::dontSendNotification);
        clickGenerator_.setTempo(value);
        if (liveStarted_)
            liveEngine_.setTempo(value, clickGenerator_.numerator());
        updateClickUi();
        refreshDashboard();
    };
    dawWorkspace_.onPlayStateChanged = [this](bool playing)
    {
        if (playing)
        {
            auto* device = deviceManager_.getCurrentAudioDevice();
            const bool hasOutput = device != nullptr
                && device->getActiveOutputChannels().countNumberOfSetBits() > 0;

            if (!hasOutput)
            {
                deviceManager_.closeAudioDevice();
                const auto error = deviceManager_.initialise(0, 2, nullptr, true, {}, nullptr);
                device = deviceManager_.getCurrentAudioDevice();

                if (error.isNotEmpty() || device == nullptr
                    || device->getActiveOutputChannels().countNumberOfSetBits() == 0)
                {
                    dawWorkspace_.emergencyStop();
                    lastAudioError_ = error.isNotEmpty()
                        ? error
                        : juce::String::fromUTF8("No hay una salida de audio disponible.");
                    showAudioError(juce::String::fromUTF8(
                        "PLAY necesita una salida de audio. J3 intentó abrir automáticamente la salida predeterminada de Windows, pero no pudo. Elegí una salida en AUDIO / MIDI."));
                    updateDiagnostics();
                    openAudioSettings();
                    return;
                }

                lastAudioError_.clear();
                refreshRoutingControls();
                updateDiagnostics();
                saveAudioState();
            }

            transportRunning_.store(true, std::memory_order_release);
            clickGenerator_.setTempo(dawWorkspace_.bpm());
            clickGenerator_.setEnabled(clickEnabledButton_.getToggleState());
        }
        else if (!liveStarted_)
        {
            transportRunning_.store(false, std::memory_order_release);
            clickGenerator_.setEnabled(false);
            pendingBeatEvents_.store(0, std::memory_order_release);
        }
        updateClickUi();
    };
    dawWorkspace_.onSelectedTrackChanged = [this](int track)
    {
        const int ch = juce::jlimit(0, kMaxChannels - 1, track);
        pluginChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
        dspChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
        selectedDspChannel_ = ch;
        setMixerBank((ch / kVisibleChannels) * kVisibleChannels);
        refreshPluginUi();
        refreshDspUi();
    };
    dawWorkspace_.onOpenMixer = [this] { tabs_.setCurrentTabIndex(0); };
    dawWorkspace_.onOpenMixerInsert = [this](int insert)
    {
        const int ch = juce::jlimit(0, kMaxChannels - 1, insert);
        setMixerBank((ch / kVisibleChannels) * kVisibleChannels);
        pluginChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
        dspChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
        selectedDspChannel_ = ch;
        tabs_.setCurrentTabIndex(0);
        refreshPluginUi();
        refreshDspUi();
    };
    dawWorkspace_.onOpenPluginsForInsert = [this](int insert)
    {
        const int ch = juce::jlimit(0, kMaxChannels - 1, insert);
        pluginChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
        int targetSlot = 0;
        for (int slot = 0; slot < kPluginSlots; ++slot)
        {
            if (pluginPaths_[ch][slot].isEmpty()
                && channelPlugins_[ch][slot].load(std::memory_order_acquire) == nullptr)
            {
                targetSlot = slot;
                break;
            }
        }
        pluginSlotBox_.setSelectedId(targetSlot + 1, juce::dontSendNotification);
        setMixerBank((ch / kVisibleChannels) * kVisibleChannels);
        tabs_.setCurrentTabIndex(6);
        refreshPluginUi();
    };
    dawWorkspace_.onMixerRoutingChanged = [this]
    {
        rebuildMixerBank();
        refreshPluginUi();
        resized();
    };
    dawWorkspace_.onOpenDsp = [this]
    {
        tabs_.setCurrentTabIndex(3);
        refreshDspUi();
    };
    dawWorkspace_.onOpenPlugins = [this]
    {
        tabs_.setCurrentTabIndex(6);
        refreshPluginUi();
    };
    dawWorkspace_.onOpenPads = [this] { tabs_.setCurrentTabIndex(5); };
    dawWorkspace_.onOpenIem = [this]
    {
        tabs_.setCurrentTabIndex(7);
        refreshIemUi();
    };
    dawWorkspace_.onOpenSetlist = [this]
    {
        tabs_.setCurrentTabIndex(2);
        refreshDashboard();
    };
    tabs_.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(background));
    tabs_.setTabBarDepth(42);
    tabs_.addTab("MIXER", juce::Colour(panel), &mixerPage_, false);
    tabs_.addTab("ARRANGER", juce::Colour(panel), &dawWorkspace_, false);
    tabs_.addTab("SETLIST", juce::Colour(panel), &setlistPage_, false);
    tabs_.addTab("DSP", juce::Colour(panel), &dspPage_, false);
    tabs_.addTab("GRUPOS", juce::Colour(panel), &groupsPage_, false);
    tabs_.addTab("PADS", juce::Colour(panel), &padPage_, false);
    tabs_.addTab("FX RACK", juce::Colour(panel), &pluginsPage_, false);
    tabs_.addTab("IEM", juce::Colour(panel), &iemPage_, false);
    tabs_.addTab("CLICK", juce::Colour(panel), &clickPage_, false);
    tabs_.addTab(juce::String::fromUTF8("GRABACIÓN"), juce::Colour(panel), &recordingPage_, false);
    tabs_.addTab("RUTEO", juce::Colour(panel), &setupPage_, false);
    tabs_.addTab("AJUSTES", juce::Colour(panel), &diagnosticsPage_, false);
    addAndMakeVisible(tabs_);
    tabs_.setCurrentTabIndex(0);

    const bool firstRunSetup = !getAudioStateFile().existsAsFile() && !getAppStateFile().existsAsFile();
    recoveredAfterUncleanExit_ = getRuntimeLockFile().existsAsFile();
    getRuntimeLockFile().getParentDirectory().createDirectory();
    getRuntimeLockFile().replaceWithText("running");
    configureAudio();
    loadAppState();
    rebuildMixerBank();
    rebuildIemBank();
    refreshIemUi();
    refreshPluginUi();
    updateClickUi();
    updateRecordingUi();
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->restoreSavedPluginsAfterScan();
    });
    applyTheme(themeId_, false);
    refreshDashboard();
    startTimerHz(30);
    setSize(1600, 960);
    if (firstRunSetup)
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->showFirstRunSetup();
        });
    checkForUpdatesAsync();
}

MainComponent::~MainComponent()
{
    shuttingDown_.store(true, std::memory_order_release);
    if (pluginScanThread_.joinable())
    {
        pluginScanThread_.request_stop();
        pluginScanThread_.join();
    }
    stopTimer();
    liveMonitorEnabled_.store(false, std::memory_order_release);
    recordingEnabled_.store(false, std::memory_order_release);
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(this);
    std::string recordError;
    recorder_.stop(recordError);
    saveAppState(true);
    saveAudioState();
    deviceManager_.closeAudioDevice();
    getRuntimeLockFile().deleteFile();
    setLookAndFeel(nullptr);
}

void MainComponent::mouseUp(const juce::MouseEvent& e)
{
    if (e.eventComponent != &safetyLabel_)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(safetyLabel_.getText());
    const auto label = safetyLabel_.getText();
    const juce::String summary = label.containsIgnoreCase("NO AUDIO")
        ? "No hay interfaz de audio activa."
        : (label.containsIgnoreCase("WARN")
            ? juce::String::fromUTF8("Hay una advertencia de rendimiento, grabación, plugin o salida.")
            : (label.containsIgnoreCase("CHECK")
                ? juce::String::fromUTF8("Revisá interfaz y routing antes del show.")
                : "Audio y routing sin alertas detectadas."));
    menu.addItem(100, summary, false);
    menu.addSeparator();
    menu.addItem(1, juce::String::fromUTF8("Abrir diagnóstico completo"));

    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&safetyLabel_),
        [safe](int result)
        {
            if (safe == nullptr || result != 1) return;
            safe->updateDiagnostics();
            safe->tabs_.setCurrentTabIndex(11);
        });
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(background));
    auto top = getLocalBounds().removeFromTop(74);
    g.setColour(juce::Colour(topBar));
    g.fillRect(top);
    g.setColour(juce::Colour(border));
    g.drawHorizontalLine(73, 0.0f, static_cast<float>(getWidth()));
    if (brandLogo_.isValid())
        g.drawImageWithin(brandLogo_, 14, 7, 58, 58, juce::RectanglePlacement::centred);
    g.setColour(juce::Colour(0xffaebdc8));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("LIVE · MIX · IEM · RECORD · WORSHIP"), 80, 48, 220, 14,
               juce::Justification::centredLeft, false);
}

void MainComponent::applyTheme(int themeId, bool persist)
{
    themeId_ = juce::jlimit(1, 6, themeId);
    if (lookAndFeel_ == nullptr)
        lookAndFeel_ = std::make_unique<j3ui::LookAndFeel>();

    lookAndFeel_->setTheme(themeId_);
    syncPaletteGlobals(lookAndFeel_->palette());
    setLookAndFeel(lookAndFeel_.get());
    themeBox_.setSelectedId(themeId_, juce::dontSendNotification);
    const auto& p = lookAndFeel_->palette();

    std::function<void(juce::Component&)> styleComponent;
    styleComponent = [&](juce::Component& component)
    {
        if (auto* label = dynamic_cast<juce::Label*>(&component))
        {
            if (label != &statusLabel_)
                label->setColour(juce::Label::textColourId, p.text);
        }
        if (auto* button = dynamic_cast<juce::TextButton*>(&component))
        {
            button->setColour(juce::TextButton::buttonColourId, p.panel3);
            button->setColour(juce::TextButton::buttonOnColourId, p.accent);
            button->setColour(juce::TextButton::textColourOffId, p.text);
            button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        }
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(&component))
            toggle->setColour(juce::ToggleButton::textColourId, p.text);
        if (auto* slider = dynamic_cast<juce::Slider*>(&component))
        {
            slider->setColour(juce::Slider::thumbColourId, p.accent);
            slider->setColour(juce::Slider::trackColourId, p.accent.withAlpha(0.72f));
            slider->setColour(juce::Slider::rotarySliderFillColourId, p.accent);
            slider->setColour(juce::Slider::rotarySliderOutlineColourId, p.border);
            slider->setColour(juce::Slider::textBoxTextColourId, p.text);
            slider->setColour(juce::Slider::textBoxBackgroundColourId, p.panel);
            slider->setColour(juce::Slider::textBoxOutlineColourId, p.border);
        }
        if (auto* combo = dynamic_cast<juce::ComboBox*>(&component))
        {
            combo->setColour(juce::ComboBox::backgroundColourId, p.panel2);
            combo->setColour(juce::ComboBox::textColourId, p.text);
            combo->setColour(juce::ComboBox::outlineColourId, p.border);
            combo->setColour(juce::ComboBox::arrowColourId, p.mutedText);
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(&component))
        {
            editor->setColour(juce::TextEditor::backgroundColourId, p.panel2);
            editor->setColour(juce::TextEditor::textColourId, p.text);
            editor->setColour(juce::TextEditor::outlineColourId, p.border);
        }
        if (auto* progress = dynamic_cast<juce::ProgressBar*>(&component))
        {
            progress->setColour(juce::ProgressBar::backgroundColourId, p.background);
            progress->setColour(juce::ProgressBar::foregroundColourId, p.good);
        }

        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* child = component.getChildComponent(i))
                styleComponent(*child);
    };
    styleComponent(*this);

    versionLabel_.setColour(juce::Label::textColourId, p.mutedText);
    liveHint_.setColour(juce::Label::textColourId, p.mutedText);
    dashboardSongInfo_.setColour(juce::Label::textColourId, p.mutedText);
    dashboardRecordInfo_.setColour(juce::Label::textColourId, p.mutedText);
    dashboardPluginsInfo_.setColour(juce::Label::textColourId, p.mutedText);
    updateButton_.setColour(juce::TextButton::buttonColourId, p.accent.darker(0.2f));
    recordButton_.setColour(juce::TextButton::buttonColourId, p.danger.darker(0.35f));
    dashboardRecordButton_.setColour(juce::TextButton::buttonColourId, p.danger.darker(0.35f));
    dashboardStopButton_.setColour(juce::TextButton::buttonColourId, p.danger.darker(0.45f));
    loadPluginButton_.setColour(juce::TextButton::buttonColourId, p.accent.darker(0.25f));
    nativeEqButton_.setColour(juce::TextButton::buttonColourId, p.accent.darker(0.5f));
    openPluginEditorButton_.setColour(juce::TextButton::buttonColourId, p.good.darker(0.45f));

    const std::array<juce::Colour, 8> sectionColours {
        p.accent.darker(0.28f), p.good.darker(0.42f), p.warning.darker(0.42f), p.danger.darker(0.32f),
        p.accent2.darker(0.42f), p.accent.darker(0.5f), juce::Colour(0xff684494), p.mutedText.darker(0.42f)
    };
    for (std::size_t i = 0; i < sectionColours.size(); ++i)
    {
        if (liveButtons_[i])
            liveButtons_[i]->setColour(juce::TextButton::buttonColourId, sectionColours[i]);
        if (dashboardSectionButtons_[i])
            dashboardSectionButtons_[i]->setColour(juce::TextButton::buttonColourId, sectionColours[i]);
    }

    tabs_.setColour(juce::TabbedComponent::backgroundColourId, p.background);
    for (int i = 0; i < tabs_.getNumTabs(); ++i)
        tabs_.setTabBackgroundColour(i, p.panel);

    if (dashboardLeftCard_) dashboardLeftCard_->repaint();
    if (dashboardRightCard_) dashboardRightCard_->repaint();
    if (dashboardBottomCard_) dashboardBottomCard_->repaint();
    sendLookAndFeelChange();
    repaint();

    if (persist)
        saveAppState();
}

void MainComponent::refreshDashboard()
{
    const int previousId = dashboardSongBox_.getSelectedId();
    dashboardSongBox_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < setlist_.size(); ++i)
    {
        if (const auto* song = setlist_.song(i))
            dashboardSongBox_.addItem(juce::String(song->name), static_cast<int>(i + 1));
    }

    if (setlist_.size() > 0)
    {
        const int currentId = static_cast<int>(setlist_.currentIndex()) + 1;
        dashboardSongBox_.setSelectedId(currentId, juce::dontSendNotification);
        if (const auto* song = setlist_.current())
        {
            juce::String info;
            info << (song->artist.empty() ? juce::String("Worship set") : juce::String(song->artist)) << "\n";
            info << (song->key.empty() ? juce::String(juce::String::fromUTF8("Tono —")) : "Tono " + juce::String(song->key))
                 << juce::String::fromUTF8("  ·  ") << juce::String(song->bpm, 1) << " BPM";
            dashboardSongInfo_.setText(info, juce::dontSendNotification);
        }
    }
    else
    {
        juce::ignoreUnused(previousId);
        dashboardSongInfo_.setText(juce::String::fromUTF8("Setlist vacío\nAgregá canciones en SETLIST."), juce::dontSendNotification);
    }

    const int mix = juce::jlimit(0, kIemMixes - 1, selectedIemMix_);
    dashboardIemMixBox_.setSelectedId(mix + 1, juce::dontSendNotification);
    const float master = std::max(1.0e-6f, iemMaster_[mix].load(std::memory_order_relaxed));
    dashboardIemMasterSlider_.setValue(juce::Decibels::gainToDecibels(master, -60.0f),
                                       juce::dontSendNotification);

    const bool recording = recordingEnabled_.load(std::memory_order_acquire);
    dashboardRecordButton_.setButtonText(recording ? "DETENER" : "GRABAR");
    dashboardRecordInfo_.setText(recording ? juce::String::fromUTF8("Grabando multicanal…") : juce::String::fromUTF8("Listo para grabación multicanal"),
                                 juce::dontSendNotification);

    const int selectedPluginChannel = juce::jlimit(0, kMaxChannels - 1,
        std::max(0, pluginChannelBox_.getSelectedId() - 1));
    juce::String chain;
    chain << inputChannelName(selectedPluginChannel) << "\n";
    int loadedPlugins = 0;
    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        auto plugin = channelPlugins_[selectedPluginChannel][slot].load(std::memory_order_acquire);
        if (plugin == nullptr)
            continue;
        if (loadedPlugins < 3)
        {
            if (loadedPlugins > 0) chain << juce::String::fromUTF8(" · ");
            chain << (pluginNames_[selectedPluginChannel][slot].isNotEmpty()
                ? pluginNames_[selectedPluginChannel][slot] : plugin->getName());
            if (pluginBypass_[selectedPluginChannel][slot].load(std::memory_order_relaxed))
                chain << (pluginFaults_[selectedPluginChannel][slot].load(std::memory_order_relaxed) > 0
                    ? " [SAFE BYP]" : " [BYP]");
        }
        ++loadedPlugins;
    }
    if (loadedPlugins == 0)
        chain << juce::String::fromUTF8("Sin inserts · 8 slots disponibles");
    else if (loadedPlugins > 3)
        chain << juce::String::fromUTF8(" · +") << (loadedPlugins - 3);
    dashboardPluginsInfo_.setText(chain, juce::dontSendNotification);

    dashboardPadButton_.setToggleState(padEnabledButton_.getToggleState(), juce::dontSendNotification);
    dashboardClickButton_.setToggleState(clickEnabledButton_.getToggleState(), juce::dontSendNotification);
    dashboardTempoLabel_.setText(juce::String(bpmSlider_.getValue(), 1) + juce::String::fromUTF8(" BPM  ·  ")
        + (padKeyBox_.getText().isNotEmpty() ? padKeyBox_.getText() : juce::String("C"))
        + (padMinorButton_.getToggleState() ? "m" : ""), juce::dontSendNotification);
}

void MainComponent::checkForUpdatesAsync()
{
    if (updateBusy_.exchange(true, std::memory_order_acq_rel))
        return;

    updateButton_.setVisible(true);
    updateButton_.setEnabled(false);
    updateButton_.setButtonText(juce::String::fromUTF8("COMPROBANDO…"));
    updateButton_.setTooltip(juce::String::fromUTF8("Buscando la última versión publicada de J3 Worship."));
    resized();

    auto current = j3::Updater::parseVersion(
        juce::JUCEApplication::getInstance()->getApplicationVersion().toStdString()).value_or(j3::SemVer { 0, 0, 0 });
    const auto currentText = juce::JUCEApplication::getInstance()->getApplicationVersion();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    std::thread([safe, current, currentText]
    {
        juce::String error;
        auto update = j3ui::UpdateService::checkLatest(current, error);
        juce::MessageManager::callAsync([safe, update, error, currentText]
        {
            if (safe == nullptr)
                return;
            safe->updateBusy_.store(false, std::memory_order_release);
            safe->updateButton_.setEnabled(true);
            safe->updateButton_.setVisible(true);

            if (update.has_value())
            {
                safe->availableUpdate_ = *update;
                safe->updateButton_.setButtonText("ACTUALIZAR " + update->versionText);
                safe->updateButton_.setTooltip(juce::String::fromUTF8("Nueva versión disponible. Descarga verificada por SHA-256."));
                safe->updateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(accentDeep));
                safe->resized();
                safe->showAvailableUpdatePrompt();
            }
            else if (error.isNotEmpty())
            {
                safe->availableUpdate_.reset();
                safe->updateButton_.setButtonText(juce::String::fromUTF8("REINTENTAR"));
                safe->updateButton_.setTooltip(juce::String::fromUTF8("No se pudo comprobar la versión: ") + error);
                safe->updateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(warning).darker(0.45f));
                safe->resized();
            }
            else
            {
                safe->availableUpdate_.reset();
                safe->downloadedUpdateInstaller_ = {};
                safe->updateButton_.setButtonText(juce::String::fromUTF8("✓ ACTUALIZADO"));
                safe->updateButton_.setTooltip(juce::String::fromUTF8("J3 Worship ") + currentText
                    + juce::String::fromUTF8(" está actualizado. Tocá para comprobar de nuevo."));
                safe->updateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(good).darker(0.45f));
                safe->resized();
            }
        });
    }).detach();
}

void MainComponent::showAvailableUpdatePrompt()
{
    if (updatePromptShown_ || !availableUpdate_.has_value())
        return;

    updatePromptShown_ = true;
    const auto version = availableUpdate_->versionText;
    auto safe = juce::Component::SafePointer<MainComponent>(this);

    auto* alert = new juce::AlertWindow(
        juce::String::fromUTF8("Nueva versión de J3 Worship"),
        juce::String::fromUTF8("Hay una actualización disponible: ") + version
            + juce::String::fromUTF8("\n\nPodés actualizar desde acá. J3 la descarga, verifica y vuelve a abrir automáticamente."),
        juce::MessageBoxIconType::InfoIcon);

    alert->addButton(juce::String::fromUTF8("ACTUALIZAR AHORA"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(juce::String::fromUTF8("MÁS TARDE"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(true,
        juce::ModalCallbackFunction::create([safe](int result)
        {
            if (safe != nullptr && result == 1)
                safe->beginUpdateInstall();
        }),
        true);
}

void MainComponent::beginUpdateInstall()
{
    if (!availableUpdate_.has_value())
    {
        checkForUpdatesAsync();
        return;
    }

    const auto safeToInstallNow = [this]
    {
        const bool liveMode = liveMonitorEnabled_.load(std::memory_order_acquire);
        const bool recording = recordingEnabled_.load(std::memory_order_acquire)
            || dawWorkspace_.isRecordingTracks();
        const bool sessionActive = transportRunning_.load(std::memory_order_acquire);
        return j3::Updater::safeToInstall(liveMode, recording, sessionActive);
    };

    if (downloadedUpdateInstaller_.existsAsFile())
    {
        if (!safeToInstallNow())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("Actualización lista"),
                juce::String::fromUTF8("La nueva versión ya está descargada y verificada. Detené LIVE/CLICK y la grabación; después tocá INSTALAR."));
            updateButton_.setButtonText("INSTALAR " + availableUpdate_->versionText);
            return;
        }

        juce::String error;
        if (!j3ui::UpdateService::launchInstallerAndRestart(downloadedUpdateInstaller_, error))
        {
            showAudioError(error);
            return;
        }
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
        return;
    }

    if (updateBusy_.exchange(true, std::memory_order_acq_rel))
        return;

    updateButton_.setEnabled(false);
    updateButton_.setButtonText(juce::String::fromUTF8("DESCARGANDO…"));
    const auto update = *availableUpdate_;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    std::thread([safe, update]
    {
        juce::File installer;
        juce::String error;
        const bool ok = j3ui::UpdateService::downloadAndVerify(update, installer, error);
        juce::MessageManager::callAsync([safe, update, installer, error, ok]
        {
            if (safe == nullptr)
                return;
            safe->updateBusy_.store(false, std::memory_order_release);
            safe->updateButton_.setEnabled(true);
            if (!ok)
            {
                safe->updateButton_.setButtonText("REINTENTAR " + update.versionText);
                safe->showAudioError("No se pudo actualizar: " + error);
                return;
            }

            safe->downloadedUpdateInstaller_ = installer;
            safe->updateButton_.setButtonText("INSTALAR " + update.versionText);
            const bool canInstall = j3::Updater::safeToInstall(
                safe->liveMonitorEnabled_.load(std::memory_order_acquire),
                safe->recordingEnabled_.load(std::memory_order_acquire)
                    || safe->dawWorkspace_.isRecordingTracks(),
                safe->transportRunning_.load(std::memory_order_acquire));
            if (!canInstall)
                return;

            juce::String launchError;
            if (!j3ui::UpdateService::launchInstallerAndRestart(installer, launchError))
            {
                safe->showAudioError(launchError);
                return;
            }
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    }).detach();
}

juce::String MainComponent::inputChannelName(int channel) const
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        if (channel >= 0 && channel < names.size() && names[channel].isNotEmpty())
            return names[channel];
    }
    return "IN " + juce::String(channel + 1);
}

void MainComponent::markDspDirty(int channel) noexcept
{
    if (channel >= 0 && channel < kMaxChannels)
        dspRevision_[channel].fetch_add(1, std::memory_order_release);
}

void MainComponent::applyDspParameters(int channel) noexcept
{
    if (channel < 0 || channel >= kMaxChannels)
        return;
    const auto revision = dspRevision_[channel].load(std::memory_order_acquire);
    if (dspAppliedRevision_[channel] == revision)
        return;

    auto& dsp = channelDsp_[channel];
    auto& insertLeft = insertEqLeft_[channel];
    auto& insertRight = insertEqRight_[channel];
    const auto hpf = channelHpf_[channel].load(std::memory_order_relaxed);
    const auto lpf = channelLpf_[channel].load(std::memory_order_relaxed);
    dsp.setHpf(hpf);
    dsp.setLpf(lpf);
    insertLeft.setHpf(hpf);
    insertRight.setHpf(hpf);
    insertLeft.setLpf(lpf);
    insertRight.setLpf(lpf);
    for (int band = 0; band < 4; ++band)
    {
        const auto frequency = channelEqFreq_[channel][band].load(std::memory_order_relaxed);
        const auto q = channelEqQ_[channel][band].load(std::memory_order_relaxed);
        const auto gain = channelEqGain_[channel][band].load(std::memory_order_relaxed);
        dsp.setEqBand(static_cast<std::size_t>(band), frequency, q, gain);
        insertLeft.setBand(static_cast<std::size_t>(band), frequency, q, gain);
        insertRight.setBand(static_cast<std::size_t>(band), frequency, q, gain);
    }
    dsp.setGate(channelGate_[channel].load(std::memory_order_relaxed));
    dsp.setCompressor(channelCompThreshold_[channel].load(std::memory_order_relaxed),
                      channelCompRatio_[channel].load(std::memory_order_relaxed));
    dsp.setDenoise(channelDenoise_[channel].load(std::memory_order_relaxed),
                   channelDenoiseThreshold_[channel].load(std::memory_order_relaxed));
    dspAppliedRevision_[channel] = revision;
}

void MainComponent::refreshDspUi()
{
    const int ch = juce::jlimit(0, kMaxChannels - 1, selectedDspChannel_);
    dspChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
    hpfSlider_.setValue(channelHpf_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    lpfSlider_.setValue(channelLpf_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    gateSlider_.setValue(channelGate_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    compThresholdSlider_.setValue(channelCompThreshold_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    compRatioSlider_.setValue(channelCompRatio_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    denoiseSlider_.setValue(channelDenoise_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    denoiseThresholdSlider_.setValue(channelDenoiseThreshold_[ch].load(std::memory_order_relaxed), juce::dontSendNotification);
    for (int band = 0; band < 4; ++band)
    {
        eqFreqSliders_[band].setValue(channelEqFreq_[ch][band].load(std::memory_order_relaxed), juce::dontSendNotification);
        eqGainSliders_[band].setValue(channelEqGain_[ch][band].load(std::memory_order_relaxed), juce::dontSendNotification);
        eqQSliders_[band].setValue(channelEqQ_[ch][band].load(std::memory_order_relaxed), juce::dontSendNotification);
    }
    juce::String dspName = "J3 PARAMETRIC EQ + CHANNEL DSP  ·  MIXER INSERT " + juce::String(ch + 1);
    const auto routedName = dawWorkspace_.mixerInsertName(ch);
    if (routedName.isNotEmpty()) dspName << juce::String::fromUTF8("  ·  ") << routedName;
    dspTitle_.setText(dspName, juce::dontSendNotification);
}

void MainComponent::applyDspPreset(int preset)
{
    const int ch = juce::jlimit(0, kMaxChannels - 1, selectedDspChannel_);
    float hpf = 20.0f, lpf = 20000.0f, gate = -60.0f, comp = -18.0f, ratio = 3.0f, denoise = 0.0f, noise = -60.0f;
    float freq[4] { 200.0f, 600.0f, 1800.0f, 5400.0f };
    float gain[4] { 0.0f, 0.0f, 0.0f, 0.0f };

    switch (preset)
    {
        case 1: // Vocal
            hpf = 80.0f; lpf = 18000.0f; gate = -55.0f; comp = -16.0f; ratio = 3.0f; denoise = 0.22f; noise = -58.0f;
            freq[0] = 180.0f; gain[0] = -1.0f; freq[1] = 450.0f; gain[1] = -2.0f;
            freq[2] = 3000.0f; gain[2] = 2.0f; freq[3] = 10000.0f; gain[3] = 1.0f; break;
        case 2: // Kick
            hpf = 25.0f; lpf = 12000.0f; gate = -45.0f; comp = -12.0f; ratio = 4.0f; denoise = 0.04f;
            freq[0] = 60.0f; gain[0] = 3.0f; freq[1] = 300.0f; gain[1] = -3.0f;
            freq[2] = 3200.0f; gain[2] = 2.0f; freq[3] = 8000.0f; gain[3] = 0.5f; break;
        case 3: // Snare
            hpf = 70.0f; lpf = 16000.0f; gate = -48.0f; comp = -14.0f; ratio = 4.0f; denoise = 0.04f;
            freq[0] = 180.0f; gain[0] = 1.5f; freq[1] = 650.0f; gain[1] = -2.0f;
            freq[2] = 4200.0f; gain[2] = 2.5f; freq[3] = 9000.0f; gain[3] = 1.0f; break;
        case 4: // Guitar
            hpf = 75.0f; lpf = 15000.0f; gate = -65.0f; comp = -20.0f; ratio = 2.5f; denoise = 0.08f;
            freq[0] = 140.0f; gain[0] = -1.0f; freq[1] = 450.0f; gain[1] = -1.5f;
            freq[2] = 2500.0f; gain[2] = 1.0f; freq[3] = 7500.0f; gain[3] = 0.5f; break;
        case 5: // Bass
            hpf = 30.0f; lpf = 9000.0f; gate = -65.0f; comp = -14.0f; ratio = 4.0f; denoise = 0.03f;
            freq[0] = 80.0f; gain[0] = 2.0f; freq[1] = 250.0f; gain[1] = -1.0f;
            freq[2] = 900.0f; gain[2] = 1.0f; freq[3] = 4500.0f; gain[3] = 0.5f; break;
        default: break;
    }

    channelHpf_[ch].store(hpf);
    channelLpf_[ch].store(lpf);
    channelGate_[ch].store(gate);
    channelCompThreshold_[ch].store(comp);
    channelCompRatio_[ch].store(ratio);
    channelDenoise_[ch].store(denoise);
    channelDenoiseThreshold_[ch].store(noise);
    for (int band = 0; band < 4; ++band)
    {
        channelEqFreq_[ch][band].store(freq[band]);
        channelEqGain_[ch][band].store(gain[band]);
        channelEqQ_[ch][band].store(1.0f);
    }
    markDspDirty(ch);
    refreshDspUi();
    saveAppState();
}

void MainComponent::refreshSetlistUi()
{
    const int previous = setlist_.size() > 0 ? static_cast<int>(setlist_.currentIndex()) : -1;
    setlistSongBox_.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < setlist_.size(); ++i)
    {
        const auto* song = setlist_.song(i);
        if (song == nullptr) continue;
        juce::String label = juce::String(static_cast<int>(i + 1)) + ". " + juce::String(song->name);
        if (!song->key.empty()) label << juce::String::fromUTF8(" · ") << juce::String(song->key);
        label << juce::String::fromUTF8(" · ") << juce::String(song->bpm, 1) << " BPM";
        setlistSongBox_.addItem(label, static_cast<int>(i + 1));
    }
    if (previous >= 0 && previous < static_cast<int>(setlist_.size()))
        setlistSongBox_.setSelectedId(previous + 1, juce::dontSendNotification);
    else if (setlist_.size() > 0)
    {
        setlist_.select(0);
        setlistSongBox_.setSelectedId(1, juce::dontSendNotification);
    }

    juce::String info;
    if (const auto* song = setlist_.current())
    {
        info << "Selected: " << juce::String(song->name) << "\n";
        if (!song->artist.empty()) info << "Artist: " << juce::String(song->artist) << "\n";
        info << "Key: " << (song->key.empty() ? juce::String(juce::String::fromUTF8("—")) : juce::String(song->key))
             << juce::String::fromUTF8(" · ") << juce::String(song->bpm, 1) << juce::String::fromUTF8(" BPM · ")
             << song->numerator << "/" << song->denominator << "\n";
        info << "LOAD INTO LIVE transfers tempo/time signature to J3 CLICK and LIVE.";
    }
    else
    {
        info = "Setlist empty. Add songs with name, key and BPM.";
    }
    setlistInfoLabel_.setText(info, juce::dontSendNotification);
    removeSongButton_.setEnabled(setlist_.size() > 0);
    loadSongButton_.setEnabled(setlist_.size() > 0);
}

void MainComponent::addSetlistSong()
{
    const auto name = songNameEditor_.getText().trim();
    if (name.isEmpty())
    {
        showAudioError(juce::String::fromUTF8("Escribí el nombre de la canción antes de agregarla al setlist."));
        return;
    }
    j3::SongRef song;
    song.name = name.toStdString();
    song.artist = songArtistEditor_.getText().trim().toStdString();
    song.key = songKeyEditor_.getText().trim().toStdString();
    song.bpm = songBpmSlider_.getValue();
    song.numerator = 4;
    song.denominator = 4;
    setlist_.add(std::move(song));
    setlist_.select(setlist_.size() - 1);
    songNameEditor_.clear();
    songArtistEditor_.clear();
    songKeyEditor_.clear();
    refreshSetlistUi();
    saveAppState();
}

void MainComponent::removeSetlistSong()
{
    const int selected = setlistSongBox_.getSelectedId() - 1;
    if (selected < 0)
        return;
    setlist_.remove(static_cast<std::size_t>(selected));
    refreshSetlistUi();
    saveAppState();
}

void MainComponent::loadSelectedSong()
{
    const int selected = setlistSongBox_.getSelectedId() - 1;
    if (selected >= 0)
        setlist_.select(static_cast<std::size_t>(selected));
    const auto* song = setlist_.current();
    if (song == nullptr)
        return;

    bpmSlider_.setValue(song->bpm, juce::sendNotificationSync);
    int signatureId = 1;
    if (song->numerator == 3 && song->denominator == 4) signatureId = 2;
    else if (song->numerator == 6 && song->denominator == 8) signatureId = 3;
    else if (song->numerator == 2 && song->denominator == 4) signatureId = 4;
    timeSignatureBox_.setSelectedId(signatureId, juce::sendNotificationSync);

    liveHint_.setText("SONG: " + juce::String(song->name) + juce::String::fromUTF8(" · ")
        + (song->key.empty() ? juce::String(juce::String::fromUTF8("KEY —")) : "KEY " + juce::String(song->key))
        + juce::String::fromUTF8(" · ") + juce::String(song->bpm, 1) + juce::String::fromUTF8(" BPM · section changes remain quantized"),
        juce::dontSendNotification);

    // If the key is recognisable, make J3 PADS follow it without auto-enabling audio.
    auto key = juce::String(song->key).trim().toUpperCase();
    const bool minor = key.endsWithChar('M') && !key.endsWith("MAJ");
    if (minor) key = key.dropLastCharacters(1);
    static const std::array<juce::String, 12> keys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < static_cast<int>(keys.size()); ++i)
    {
        if (key == keys[static_cast<std::size_t>(i)])
        {
            ambientPad_.setRootMidi(60 + i);
            ambientPad_.setMinor(minor);
            refreshPadUi();
            break;
        }
    }
    refreshSetlistUi();
    saveAppState();
}

void MainComponent::setMixerBank(int firstChannel)
{
    const int maxStart = std::max(0, kMaxChannels - kVisibleChannels);
    const int snapped = (juce::jlimit(0, maxStart, firstChannel) / kVisibleChannels) * kVisibleChannels;
    if (snapped == mixerBankStart_ && strips_[0] != nullptr)
        return;
    mixerBankStart_ = snapped;
    rebuildMixerBank();
    resized();
}

void MainComponent::rebuildMixerBank()
{
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        strips_[i].reset();
        const int channel = mixerBankStart_ + i;
        auto title = "INS " + juce::String(channel + 1);
        const auto routedName = dawWorkspace_.mixerInsertName(channel);
        if (routedName.isNotEmpty()) title << juce::String::fromUTF8(" · ") << routedName;
        strips_[i] = std::make_unique<MixerStrip>(
            channel, title,
            channelGain_[channel], channelPan_[channel], channelMute_[channel], channelMeter_[channel],
            channelBus_[channel], channelDca_[channel],
            [this](int ch)
            {
                pluginChannelBox_.setSelectedId(ch + 1, juce::dontSendNotification);
                int targetSlot = 0;
                for (int slot = 0; slot < kPluginSlots; ++slot)
                {
                    if (pluginPaths_[ch][slot].isEmpty()
                        && channelPlugins_[ch][slot].load(std::memory_order_acquire) == nullptr)
                    {
                        targetSlot = slot;
                        break;
                    }
                }
                pluginSlotBox_.setSelectedId(targetSlot + 1, juce::dontSendNotification);
                refreshPluginUi();
                tabs_.setCurrentTabIndex(6);
            });
        mixerPage_.addAndMakeVisible(*strips_[i]);
    }
    mixerBankLabel_.setText("MIXER INSERTS " + juce::String(mixerBankStart_ + 1) + juce::String::fromUTF8("–")
        + juce::String(std::min(kMaxChannels, mixerBankStart_ + kVisibleChannels))
        + " / " + juce::String(kMaxChannels), juce::dontSendNotification);
    mixerPrevButton_.setEnabled(mixerBankStart_ > 0);
    mixerNextButton_.setEnabled(mixerBankStart_ + kVisibleChannels < kMaxChannels);
}

void MainComponent::rebuildIemBank()
{
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        iemStrips_[i].reset();
        const int channel = iemBankStart_ + i;
        iemStrips_[i] = std::make_unique<IemSendStrip>(
            channel, inputChannelName(channel),
            iemSendGain_[selectedIemMix_][channel], iemSendPan_[selectedIemMix_][channel]);
        iemPage_.addAndMakeVisible(*iemStrips_[i]);
    }
    iemBankLabel_.setText("SOURCES " + juce::String(iemBankStart_ + 1) + juce::String::fromUTF8("–")
        + juce::String(std::min(kMaxChannels, iemBankStart_ + kVisibleChannels))
        + " / " + juce::String(kMaxChannels), juce::dontSendNotification);
    iemPrevButton_.setEnabled(iemBankStart_ > 0);
    iemNextButton_.setEnabled(iemBankStart_ + kVisibleChannels < kMaxChannels);
}

void MainComponent::refreshIemUi()
{
    const int mix = juce::jlimit(0, kIemMixes - 1, selectedIemMix_);
    const auto master = std::max(1.0e-6f, iemMaster_[mix].load(std::memory_order_relaxed));
    iemMasterSlider_.setValue(juce::Decibels::gainToDecibels(master, -60.0f), juce::dontSendNotification);
    iemMuteButton_.setToggleState(iemMute_[mix].load(std::memory_order_relaxed), juce::dontSendNotification);

    const int left = iemOutLeft_[mix].load(std::memory_order_relaxed);
    const int right = iemOutRight_[mix].load(std::memory_order_relaxed);
    iemOutLeftBox_.setSelectedId(left >= 0 ? left + 2 : 1, juce::dontSendNotification);
    iemOutRightBox_.setSelectedId(right >= 0 ? right + 2 : 1, juce::dontSendNotification);

    for (auto& strip : iemStrips_)
        if (strip) strip->syncFromModel();
}

bool MainComponent::iemRouteIsSafe(int mix, int left, int right) const noexcept
{
    if (left < 0 || right < 0)
        return left < 0 && right < 0;
    if (left == right)
        return false;
    const int paL = paLeft_.load(std::memory_order_relaxed);
    const int paR = paRight_.load(std::memory_order_relaxed);
    const int click = clickOutput_.load(std::memory_order_relaxed);
    if (left == paL || left == paR || right == paL || right == paR || left == click || right == click)
        return false;
    for (int i = 0; i < kIemMixes; ++i)
    {
        if (i == mix) continue;
        const int l = iemOutLeft_[i].load(std::memory_order_relaxed);
        const int r = iemOutRight_[i].load(std::memory_order_relaxed);
        if (l < 0 || r < 0) continue;
        if (left == l || left == r || right == l || right == r)
            return false;
    }
    return true;
}

bool MainComponent::anyIemRouted() const noexcept
{
    for (int i = 0; i < kIemMixes; ++i)
        if (iemOutLeft_[i].load(std::memory_order_relaxed) >= 0
            && iemOutRight_[i].load(std::memory_order_relaxed) >= 0
            && !iemMute_[i].load(std::memory_order_relaxed))
            return true;
    return false;
}

void MainComponent::applyIemRoutingFromControls()
{
    const int mix = selectedIemMix_;
    const int left = iemOutLeftBox_.getSelectedId() <= 1 ? -1 : iemOutLeftBox_.getSelectedId() - 2;
    const int right = iemOutRightBox_.getSelectedId() <= 1 ? -1 : iemOutRightBox_.getSelectedId() - 2;
    const bool bothOff = left < 0 && right < 0;
    if (!bothOff && !iemRouteIsSafe(mix, left, right))
    {
        iemOutLeft_[mix].store(-1, std::memory_order_relaxed);
        iemOutRight_[mix].store(-1, std::memory_order_relaxed);
        iemOutLeftBox_.setSelectedId(1, juce::dontSendNotification);
        iemOutRightBox_.setSelectedId(1, juce::dontSendNotification);
        showAudioError(juce::String::fromUTF8("J3 SAFE ROUTING bloqueó ese IEM: usá dos salidas libres, distintas del PA, CLICK y otros IEM."));
        return;
    }
    iemOutLeft_[mix].store(left, std::memory_order_relaxed);
    iemOutRight_[mix].store(right, std::memory_order_relaxed);
    saveAppState();
    updateDiagnostics();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(74).reduced(14, 8);
    top.removeFromLeft(64);
    brandLabel_.setBounds(top.removeFromLeft(190));
    versionLabel_.setBounds(top.removeFromLeft(62).reduced(0, 11));

    audioSettingsButton_.setBounds(top.removeFromRight(132));
    top.removeFromRight(8);
    liveMonitorButton_.setBounds(top.removeFromRight(112));
    top.removeFromRight(8);
    themeBox_.setBounds(top.removeFromRight(154).reduced(0, 3));
    top.removeFromRight(8);
    if (updateButton_.isVisible())
    {
        updateButton_.setBounds(top.removeFromRight(148).reduced(0, 2));
        top.removeFromRight(8);
    }
    else
    {
        updateButton_.setBounds({});
    }
    safetyLabel_.setBounds(top.removeFromRight(126).reduced(0, 5));
    top.removeFromRight(8);
    statusLabel_.setBounds(top.reduced(4, 0));
    tabs_.setBounds(area.reduced(8));

    auto liveArea = livePage_.getLocalBounds().reduced(24);
    nowLabel_.setBounds(liveArea.removeFromTop(58));
    nextLabel_.setBounds(liveArea.removeFromTop(38));
    liveHint_.setBounds(liveArea.removeFromTop(36));
    liveArea.removeFromTop(18);
    const int gap = 12;
    const int columns = 4;
    const int rows = 2;
    const int buttonW = (liveArea.getWidth() - gap * (columns - 1)) / columns;
    const int buttonH = (liveArea.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < 8; ++i)
    {
        const int c = i % columns;
        const int r = i / columns;
        liveButtons_[i]->setBounds(liveArea.getX() + c * (buttonW + gap),
                                  liveArea.getY() + r * (buttonH + gap), buttonW, buttonH);
    }

    auto setArea = setlistPage_.getLocalBounds().reduced(30);
    setlistTitle_.setBounds(setArea.removeFromTop(48));
    setArea.removeFromTop(10);
    setlistSongBox_.setBounds(setArea.removeFromTop(42).removeFromLeft(std::min(720, setArea.getWidth())));
    setArea.removeFromTop(18);
    auto songRow = setArea.removeFromTop(44);
    const int songGap = 10;
    const int songNameW = std::max(220, songRow.getWidth() * 35 / 100);
    songNameEditor_.setBounds(songRow.removeFromLeft(songNameW));
    songRow.removeFromLeft(songGap);
    songArtistEditor_.setBounds(songRow.removeFromLeft(std::max(180, songRow.getWidth() * 40 / 100)));
    songRow.removeFromLeft(songGap);
    songKeyEditor_.setBounds(songRow.removeFromLeft(std::min(150, songRow.getWidth())));
    setArea.removeFromTop(14);
    songBpmSlider_.setBounds(setArea.removeFromTop(48).removeFromLeft(std::min(560, setArea.getWidth())));
    setArea.removeFromTop(16);
    auto songButtons = setArea.removeFromTop(44);
    addSongButton_.setBounds(songButtons.removeFromLeft(160));
    songButtons.removeFromLeft(10);
    removeSongButton_.setBounds(songButtons.removeFromLeft(140));
    songButtons.removeFromLeft(10);
    loadSongButton_.setBounds(songButtons.removeFromLeft(190));
    setArea.removeFromTop(20);
    setlistInfoLabel_.setBounds(setArea.removeFromTop(170));

    auto mixerArea = mixerPage_.getLocalBounds().reduced(9);
    const int leftWidth = juce::jlimit(205, 252, mixerArea.getWidth() * 16 / 100);
    const int rightWidth = juce::jlimit(238, 302, mixerArea.getWidth() * 19 / 100);
    auto dashboardLeft = mixerArea.removeFromLeft(leftWidth);
    mixerArea.removeFromLeft(8);
    auto dashboardRight = mixerArea.removeFromRight(rightWidth);
    mixerArea.removeFromRight(8);
    auto dashboardBottom = mixerArea.removeFromBottom(126);
    mixerArea.removeFromBottom(8);

    if (dashboardLeftCard_) dashboardLeftCard_->setBounds(dashboardLeft);
    if (dashboardRightCard_) dashboardRightCard_->setBounds(dashboardRight);
    if (dashboardBottomCard_) dashboardBottomCard_->setBounds(dashboardBottom);

    auto leftContent = dashboardLeft.reduced(12);
    dashboardSetlistTitle_.setBounds(leftContent.removeFromTop(28));
    leftContent.removeFromTop(4);
    dashboardSongBox_.setBounds(leftContent.removeFromTop(34));
    leftContent.removeFromTop(8);
    dashboardSongInfo_.setBounds(leftContent.removeFromTop(70));
    leftContent.removeFromTop(8);
    dashboardLoadSongButton_.setBounds(leftContent.removeFromTop(36));

    auto rightContent = dashboardRight.reduced(12);
    dashboardIemTitle_.setBounds(rightContent.removeFromTop(26));
    auto iemRow = rightContent.removeFromTop(34);
    dashboardIemMixBox_.setBounds(iemRow.removeFromLeft(92));
    iemRow.removeFromLeft(6);
    dashboardIemMasterSlider_.setBounds(iemRow);
    rightContent.removeFromTop(12);
    dashboardRecordTitle_.setBounds(rightContent.removeFromTop(24));
    auto recordRow = rightContent.removeFromTop(38);
    dashboardRecordButton_.setBounds(recordRow.removeFromLeft(92));
    recordRow.removeFromLeft(8);
    dashboardRecordInfo_.setBounds(recordRow);
    rightContent.removeFromTop(12);
    dashboardPluginsTitle_.setBounds(rightContent.removeFromTop(24));
    dashboardPluginsInfo_.setBounds(rightContent.removeFromTop(62));

    const int sessionHeight = juce::jlimit(180, 250, mixerArea.getHeight() * 34 / 100);
    if (sessionGrid_)
        sessionGrid_->setBounds(mixerArea.removeFromTop(sessionHeight));
    mixerArea.removeFromTop(6);

    auto mixerNav = mixerArea.removeFromTop(32);
    mixerPrevButton_.setBounds(mixerNav.removeFromLeft(82).reduced(1));
    mixerNextButton_.setBounds(mixerNav.removeFromRight(82).reduced(1));
    mixerBankLabel_.setBounds(mixerNav);
    mixerArea.removeFromTop(4);
    const int stripW = std::max(1, mixerArea.getWidth() / kVisibleChannels);
    for (int i = 0; i < kVisibleChannels; ++i)
        if (strips_[i]) strips_[i]->setBounds(mixerArea.removeFromLeft(stripW).reduced(2));

    auto transport = dashboardBottom.reduced(10);
    auto transportTop = transport.removeFromTop(28);
    dashboardLiveTitle_.setBounds(transportTop.removeFromLeft(150));
    dashboardStopButton_.setBounds(transportTop.removeFromLeft(70).reduced(2));
    dashboardPadButton_.setBounds(transportTop.removeFromLeft(70).reduced(2));
    dashboardClickButton_.setBounds(transportTop.removeFromLeft(78).reduced(2));
    dashboardTempoLabel_.setBounds(transportTop.removeFromRight(150));
    transport.removeFromTop(7);
    const int sectionGap = 5;
    const int sectionWidth = std::max(54, (transport.getWidth() - sectionGap * 7) / 8);
    for (int i = 0; i < 8; ++i)
    {
        if (dashboardSectionButtons_[i])
        {
            dashboardSectionButtons_[i]->setBounds(transport.removeFromLeft(sectionWidth));
            if (i < 7) transport.removeFromLeft(sectionGap);
        }
    }

    auto dspArea = dspPage_.getLocalBounds().reduced(20);
    auto dspHeader = dspArea.removeFromTop(42);
    dspTitle_.setBounds(dspHeader.removeFromLeft(std::min(480, dspHeader.getWidth() / 2)));
    dspChannelBox_.setBounds(dspHeader.removeFromLeft(160).reduced(4, 3));
    auto presetRow = dspHeader;
    const int presetW = std::max(64, presetRow.getWidth() / 6);
    vocalPresetButton_.setBounds(presetRow.removeFromLeft(presetW).reduced(2));
    kickPresetButton_.setBounds(presetRow.removeFromLeft(presetW).reduced(2));
    snarePresetButton_.setBounds(presetRow.removeFromLeft(presetW).reduced(2));
    guitarPresetButton_.setBounds(presetRow.removeFromLeft(presetW).reduced(2));
    bassPresetButton_.setBounds(presetRow.removeFromLeft(presetW).reduced(2));
    resetDspButton_.setBounds(presetRow.reduced(2));
    dspArea.removeFromTop(10);

    auto dynamics = dspArea.removeFromLeft(dspArea.getWidth() / 2).reduced(8);
    auto eqArea = dspArea.reduced(8);
    const int dynH = std::max(36, dynamics.getHeight() / 7);
    hpfSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    lpfSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    gateSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    compThresholdSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    compRatioSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    denoiseSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));
    denoiseThresholdSlider_.setBounds(dynamics.removeFromTop(dynH).reduced(4));

    const int eqH = std::max(70, eqArea.getHeight() / 4);
    for (int band = 0; band < 4; ++band)
    {
        auto row = eqArea.removeFromTop(eqH).reduced(4);
        eqBandLabels_[band].setBounds(row.removeFromLeft(54));
        const int controlW = std::max(1, row.getWidth() / 3);
        eqFreqSliders_[band].setBounds(row.removeFromLeft(controlW).reduced(3));
        eqGainSliders_[band].setBounds(row.removeFromLeft(controlW).reduced(3));
        eqQSliders_[band].setBounds(row.reduced(3));
    }

    auto groupsArea = groupsPage_.getLocalBounds().reduced(18);
    groupsTitle_.setBounds(groupsArea.removeFromTop(38));
    groupsArea.removeFromTop(8);
    auto busRow = groupsArea.removeFromTop(std::max(220, groupsArea.getHeight() / 2 - 5));
    const int groupW = std::max(1, busRow.getWidth() / kBuses);
    for (int i = 0; i < kBuses; ++i)
        if (busStrips_[i]) busStrips_[i]->setBounds(busRow.removeFromLeft(groupW).reduced(3));
    groupsArea.removeFromTop(10);
    const int dcaW = std::max(1, groupsArea.getWidth() / kDcas);
    for (int i = 0; i < kDcas; ++i)
        if (dcaStrips_[i]) dcaStrips_[i]->setBounds(groupsArea.removeFromLeft(dcaW).reduced(3));

    auto iemArea = iemPage_.getLocalBounds().reduced(18);
    auto iemTop = iemArea.removeFromTop(40);
    iemTitle_.setBounds(iemTop.removeFromLeft(300));
    iemMixBox_.setBounds(iemTop.removeFromLeft(150).reduced(4, 3));
    iemRouteLabel_.setBounds(iemTop.removeFromLeft(160));
    iemOutLeftBox_.setBounds(iemTop.removeFromLeft(170).reduced(4, 3));
    iemOutRightBox_.setBounds(iemTop.removeFromLeft(170).reduced(4, 3));
    iemMuteButton_.setBounds(iemTop.removeFromRight(120));
    iemArea.removeFromTop(8);
    auto iemMasterRow = iemArea.removeFromTop(40);
    iemMasterSlider_.setBounds(iemMasterRow.removeFromLeft(520));
    auto iemNav = iemMasterRow;
    iemPrevButton_.setBounds(iemNav.removeFromLeft(110));
    iemNextButton_.setBounds(iemNav.removeFromRight(110));
    iemBankLabel_.setBounds(iemNav);
    iemArea.removeFromTop(8);
    const int iemW = std::max(1, iemArea.getWidth() / kVisibleChannels);
    for (int i = 0; i < kVisibleChannels; ++i)
        if (iemStrips_[i]) iemStrips_[i]->setBounds(iemArea.removeFromLeft(iemW).reduced(3));

    auto pluginsArea = pluginsPage_.getLocalBounds().reduced(24);
    pluginsTitle_.setBounds(pluginsArea.removeFromTop(46));
    auto insertRow = pluginsArea.removeFromTop(40);
    pluginChannelBox_.setBounds(insertRow.removeFromLeft(std::min(260, insertRow.getWidth())).reduced(2));
    pluginSlotBox_.setBounds({}); // selection model only; the visible rack buttons replace this combo
    pluginsArea.removeFromTop(8);

    const int chainWidth = std::max(420, std::min(620, pluginsArea.getWidth() * 46 / 100));
    auto chain = pluginsArea.removeFromLeft(chainWidth).reduced(6);
    pluginsArea.removeFromLeft(14);
    auto browser = pluginsArea.reduced(6);

    pluginChainTitle_.setBounds(chain.removeFromTop(24));
    chain.removeFromTop(4);
    nativeEqButton_.setBounds(chain.removeFromTop(40));
    chain.removeFromTop(8);
    const int slotGap = 5;
    const int slotH = std::max(34, std::min(46, (chain.getHeight() - 42 - slotGap * (kPluginSlots - 1)) / kPluginSlots));
    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        if (pluginSlotButtons_[slot])
            pluginSlotButtons_[slot]->setBounds(chain.removeFromTop(slotH));
        if (slot + 1 < kPluginSlots)
            chain.removeFromTop(slotGap);
    }

    pluginBrowserTitle_.setBounds(browser.removeFromTop(24));
    browser.removeFromTop(4);

    auto searchRow = browser.removeFromTop(40);
    pluginSearch_.setBounds(searchRow.removeFromLeft(std::max(210, searchRow.getWidth() * 58 / 100)).reduced(2));
    searchRow.removeFromLeft(6);
    pluginCategoryBox_.setBounds(searchRow.reduced(2));
    browser.removeFromTop(7);

    auto scanRow = browser.removeFromTop(38);
    const int scanHalf = std::max(120, scanRow.getWidth() / 2);
    scanPluginsButton_.setBounds(scanRow.removeFromLeft(scanHalf).reduced(2));
    scanRow.removeFromLeft(6);
    pluginLocationsButton_.setBounds(scanRow.reduced(2));
    browser.removeFromTop(8);

    pluginCatalogBox_.setBounds({});
    const int footerReserve = 142;
    const int listHeight = std::max(150, browser.getHeight() - footerReserve);
    pluginCatalogList_.setBounds(browser.removeFromTop(listHeight));
    browser.removeFromTop(8);

    auto primaryActions = browser.removeFromTop(38);
    loadPluginButton_.setBounds(primaryActions.removeFromLeft(std::min(150, primaryActions.getWidth())).reduced(1));
    primaryActions.removeFromLeft(5);
    openPluginEditorButton_.setBounds(primaryActions.removeFromLeft(std::min(135, primaryActions.getWidth())).reduced(1));
    primaryActions.removeFromLeft(5);
    favoritePluginButton_.setBounds(primaryActions.removeFromLeft(std::min(105, primaryActions.getWidth())).reduced(1));
    primaryActions.removeFromLeft(5);
    bypassPluginButton_.setBounds(primaryActions.reduced(1));
    browser.removeFromTop(5);

    auto secondaryActions = browser.removeFromTop(34);
    removePluginButton_.setBounds(secondaryActions.removeFromLeft(100).reduced(1));
    secondaryActions.removeFromLeft(5);
    movePluginUpButton_.setBounds(secondaryActions.removeFromLeft(95).reduced(1));
    secondaryActions.removeFromLeft(5);
    movePluginDownButton_.setBounds(secondaryActions.removeFromLeft(105).reduced(1));
    browser.removeFromTop(6);
    pluginStatusLabel_.setBounds(browser);

    auto padArea = padPage_.getLocalBounds().reduced(42);
    padTitle_.setBounds(padArea.removeFromTop(54));
    padArea.removeFromTop(18);
    auto padControls = padArea.removeFromTop(52);
    padKeyBox_.setBounds(padControls.removeFromLeft(150).reduced(3));
    padControls.removeFromLeft(14);
    padMinorButton_.setBounds(padControls.removeFromLeft(130));
    padControls.removeFromLeft(14);
    padEnabledButton_.setBounds(padControls.removeFromLeft(150));
    padControls.removeFromLeft(14);
    padToPaButton_.setBounds(padControls.removeFromLeft(170));
    padArea.removeFromTop(24);
    padVolumeSlider_.setBounds(padArea.removeFromTop(52).removeFromLeft(std::min(620, padArea.getWidth())));
    padArea.removeFromTop(28);
    padInfoLabel_.setBounds(padArea.removeFromTop(160));

    auto clickArea = clickPage_.getLocalBounds().reduced(36);
    clickTitle_.setBounds(clickArea.removeFromTop(48));
    clickArea.removeFromTop(18);
    clickEnabledButton_.setBounds(clickArea.removeFromTop(40).removeFromLeft(180));
    clickArea.removeFromTop(18);
    auto tempoRow = clickArea.removeFromTop(58);
    bpmSlider_.setBounds(tempoRow.removeFromLeft(std::min(560, tempoRow.getWidth() - 180)));
    tempoRow.removeFromLeft(16);
    tapTempoButton_.setBounds(tempoRow.removeFromLeft(160));
    clickArea.removeFromTop(18);
    auto optionRow = clickArea.removeFromTop(48);
    timeSignatureBox_.setBounds(optionRow.removeFromLeft(180));
    optionRow.removeFromLeft(16);
    subdivisionBox_.setBounds(optionRow.removeFromLeft(180));
    optionRow.removeFromLeft(16);
    accentButton_.setBounds(optionRow.removeFromLeft(140));
    clickArea.removeFromTop(18);
    clickVolumeSlider_.setBounds(clickArea.removeFromTop(48).removeFromLeft(520));
    clickArea.removeFromTop(20);
    clickRouteLabel_.setBounds(clickArea.removeFromTop(120));

    auto recordArea = recordingPage_.getLocalBounds().reduced(36);
    recordingTitle_.setBounds(recordArea.removeFromTop(48));
    recordArea.removeFromTop(20);
    recordButton_.setBounds(recordArea.removeFromTop(58).removeFromLeft(220));
    recordArea.removeFromTop(18);
    openRecordingsButton_.setBounds(recordArea.removeFromTop(44).removeFromLeft(260));
    recordArea.removeFromTop(18);
    recordingStatusLabel_.setBounds(recordArea);

    auto setup = setupPage_.getLocalBounds().reduced(32);
    setupTitle_.setBounds(setup.removeFromTop(42));
    setupDeviceLabel_.setBounds(setup.removeFromTop(160));
    setup.removeFromTop(14);
    routingTitle_.setBounds(setup.removeFromTop(32));
    auto routeRow = setup.removeFromTop(78);
    const int routeGap = 16;
    const int routeWidth = (routeRow.getWidth() - routeGap * 2) / 3;
    auto left = routeRow.removeFromLeft(routeWidth);
    routeRow.removeFromLeft(routeGap);
    auto right = routeRow.removeFromLeft(routeWidth);
    routeRow.removeFromLeft(routeGap);
    auto click = routeRow;
    paLeftLabel_.setBounds(left.removeFromTop(24));
    paLeftBox_.setBounds(left.removeFromTop(36));
    paRightLabel_.setBounds(right.removeFromTop(24));
    paRightBox_.setBounds(right.removeFromTop(36));
    clickLabel_.setBounds(click.removeFromTop(24));
    clickBox_.setBounds(click.removeFromTop(36));
    setup.removeFromTop(20);
    auto buttons = setup.removeFromTop(44);
    rescanButton_.setBounds(buttons.removeFromLeft(180));
    buttons.removeFromLeft(12);
    testOutputButton_.setBounds(buttons.removeFromLeft(170));
    buttons.removeFromLeft(12);
    driverPanelButton_.setBounds(buttons.removeFromLeft(220));

    auto d = diagnosticsPage_.getLocalBounds().reduced(28);
    runCheckButton_.setBounds(d.removeFromTop(44).removeFromLeft(210));
    d.removeFromTop(18);
    diagnosticsLabel_.setBounds(d);
}

juce::File MainComponent::getAudioStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("audio-device.xml");
}

juce::File MainComponent::getAppStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("app-state.xml");
}

juce::File MainComponent::getRuntimeLockFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("runtime.lock");
}

juce::File MainComponent::recordingsRoot() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("Recordings");
}

void MainComponent::loadAppState()
{
    const auto file = getAppStateFile();
    if (!file.existsAsFile())
        return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("J3WorshipState"))
        return;

    const auto stateVersion = j3::Updater::parseVersion(
        xml->getStringAttribute("version", "1.0.0").toStdString()).value_or(j3::SemVer { 1, 0, 0 });
    const bool migrateLegacyMixer = stateVersion < j3::SemVer { 1, 6, 0 };

    themeId_ = juce::jlimit(1, 6, xml->getIntAttribute("themeId", 1));
    themeBox_.setSelectedId(themeId_, juce::dontSendNotification);
    applyTheme(themeId_, false);

    paLeft_.store(xml->getIntAttribute("paLeft", 0));
    paRight_.store(xml->getIntAttribute("paRight", 1));
    clickOutput_.store(xml->getIntAttribute("clickOutput", -1));
    bpmSlider_.setValue(xml->getDoubleAttribute("bpm", 120.0), juce::dontSendNotification);
    timeSignatureBox_.setSelectedId(xml->getIntAttribute("timeSignatureId", 1), juce::dontSendNotification);
    subdivisionBox_.setSelectedId(xml->getIntAttribute("subdivisionId", 1), juce::dontSendNotification);
    accentButton_.setToggleState(xml->getBoolAttribute("accent", true), juce::dontSendNotification);
    clickVolumeSlider_.setValue(xml->getDoubleAttribute("clickVolume", 0.35), juce::dontSendNotification);
    ambientPad_.setRootMidi(xml->getIntAttribute("padRootMidi", 60));
    ambientPad_.setMinor(xml->getBoolAttribute("padMinor", false));
    ambientPad_.setVolume(static_cast<float>(xml->getDoubleAttribute("padVolume", 0.18)));
    padToPa_.store(xml->getBoolAttribute("padToPa", true), std::memory_order_release);
    ambientPad_.setEnabled(false);
    padEnabledButton_.setToggleState(false, juce::dontSendNotification);
    clickAudible_.store(false, std::memory_order_release);
    clickEnabledButton_.setToggleState(false, juce::dontSendNotification);

    clickGenerator_.setTempo(bpmSlider_.getValue());
    const int sig = timeSignatureBox_.getSelectedId();
    clickGenerator_.setTimeSignature(sig == 2 ? 3 : (sig == 3 ? 6 : (sig == 4 ? 2 : 4)), sig == 3 ? 8 : 4);
    const int sub = subdivisionBox_.getSelectedId();
    clickGenerator_.setSubdivision(sub == 3 ? 4 : (sub == 2 ? 2 : 1));
    clickGenerator_.setAccentEnabled(accentButton_.getToggleState());
    clickGenerator_.setLevel(static_cast<float>(clickVolumeSlider_.getValue()));

    setlist_.clear();
    forEachXmlChildElementWithTagName(*xml, songXml, "Song")
    {
        j3::SongRef song;
        song.name = songXml->getStringAttribute("name").toStdString();
        song.artist = songXml->getStringAttribute("artist").toStdString();
        song.key = songXml->getStringAttribute("key").toStdString();
        song.bpm = songXml->getDoubleAttribute("bpm", 120.0);
        song.numerator = songXml->getIntAttribute("numerator", 4);
        song.denominator = songXml->getIntAttribute("denominator", 4);
        if (!song.name.empty()) setlist_.add(std::move(song));
    }
    if (setlist_.size() > 0)
        setlist_.select(static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(setlist_.size()) - 1,
            xml->getIntAttribute("setlistIndex", 0))));

    favoritePluginPaths_.clear();
    recentPluginPaths_.clear();
    pluginCustomLocations_.clear();
    forEachXmlChildElementWithTagName(*xml, location, "PluginLocation")
    {
        const auto path = location->getStringAttribute("path").trim();
        if (path.isNotEmpty()) pluginCustomLocations_.addIfNotAlreadyThere(path);
    }
    forEachXmlChildElementWithTagName(*xml, fav, "PluginFavorite")
    {
        const auto path = fav->getStringAttribute("path").trim();
        if (path.isNotEmpty()) favoritePluginPaths_.addIfNotAlreadyThere(path);
    }
    forEachXmlChildElementWithTagName(*xml, recent, "PluginRecent")
    {
        const auto path = recent->getStringAttribute("path").trim();
        if (path.isNotEmpty()) recentPluginPaths_.addIfNotAlreadyThere(path);
    }

    forEachXmlChildElementWithTagName(*xml, vst, "Vst3")
    {
        const int channel = vst->getIntAttribute("channel", -1);
        const int slot = vst->getIntAttribute("slot", -1);
        if (channel < 0 || channel >= kMaxChannels || slot < 0 || slot >= kPluginSlots)
            continue;
        pluginPaths_[channel][slot] = vst->getStringAttribute("path");
        pluginNames_[channel][slot] = vst->getStringAttribute("name");
        pluginStateBase64_[channel][slot] = vst->getStringAttribute("state");
        pluginBypass_[channel][slot].store(
            migrateLegacyMixer || vst->getBoolAttribute("bypass", false),
            std::memory_order_relaxed);
    }

    forEachXmlChildElementWithTagName(*xml, ch, "Channel")
    {
        const int index = ch->getIntAttribute("index", -1);
        if (index < 0 || index >= kMaxChannels) continue;
        channelGain_[index].store(static_cast<float>(ch->getDoubleAttribute("gain", dbToGain(-6.0))), std::memory_order_relaxed);
        channelPan_[index].store(static_cast<float>(ch->getDoubleAttribute("pan", 0.0)), std::memory_order_relaxed);
        channelMute_[index].store(ch->getBoolAttribute("mute", false), std::memory_order_relaxed);
        channelBus_[index].store(juce::jlimit(-1, kBuses - 1, ch->getIntAttribute("bus", -1)), std::memory_order_relaxed);
        channelDca_[index].store(juce::jlimit(-1, kDcas - 1, ch->getIntAttribute("dca", -1)), std::memory_order_relaxed);
        if (migrateLegacyMixer)
        {
            // Before 1.6 these values described live input channels. They now also back DAW
            // mixer inserts, so stale mute/bus/DCA/fader state can make a newly imported
            // track completely silent. Start the new insert workflow from an audible state.
            channelGain_[index].store(1.0f, std::memory_order_relaxed);
            channelPan_[index].store(0.0f, std::memory_order_relaxed);
            channelMute_[index].store(false, std::memory_order_relaxed);
            channelBus_[index].store(-1, std::memory_order_relaxed);
            channelDca_[index].store(-1, std::memory_order_relaxed);
        }
        channelHpf_[index].store(static_cast<float>(ch->getDoubleAttribute("hpf", 20.0)));
        channelLpf_[index].store(static_cast<float>(ch->getDoubleAttribute("lpf", 20000.0)));
        channelGate_[index].store(static_cast<float>(ch->getDoubleAttribute("gate", -60.0)));
        channelCompThreshold_[index].store(static_cast<float>(ch->getDoubleAttribute("compThreshold", -18.0)));
        channelCompRatio_[index].store(static_cast<float>(ch->getDoubleAttribute("compRatio", 3.0)));
        channelDenoise_[index].store(static_cast<float>(ch->getDoubleAttribute("denoise", 0.0)));
        channelDenoiseThreshold_[index].store(static_cast<float>(ch->getDoubleAttribute("denoiseThreshold", -60.0)));
        for (int band = 0; band < 4; ++band)
        {
            const auto b = juce::String(band);
            channelEqFreq_[index][band].store(static_cast<float>(ch->getDoubleAttribute("eq" + b + "Freq", channelEqFreq_[index][band].load())));
            channelEqGain_[index][band].store(static_cast<float>(ch->getDoubleAttribute("eq" + b + "Gain", 0.0)));
            channelEqQ_[index][band].store(static_cast<float>(ch->getDoubleAttribute("eq" + b + "Q", 1.0)));
        }
        markDspDirty(index);
    }

    forEachXmlChildElementWithTagName(*xml, bus, "Bus")
    {
        const int index = bus->getIntAttribute("index", -1);
        if (index < 0 || index >= kBuses) continue;
        busGain_[index].store(static_cast<float>(bus->getDoubleAttribute("gain", 1.0)), std::memory_order_relaxed);
        busMute_[index].store(bus->getBoolAttribute("mute", false), std::memory_order_relaxed);
    }
    forEachXmlChildElementWithTagName(*xml, dca, "Dca")
    {
        const int index = dca->getIntAttribute("index", -1);
        if (index < 0 || index >= kDcas) continue;
        dcaGain_[index].store(static_cast<float>(dca->getDoubleAttribute("gain", 1.0)), std::memory_order_relaxed);
        dcaMute_[index].store(dca->getBoolAttribute("mute", false), std::memory_order_relaxed);
    }

    forEachXmlChildElementWithTagName(*xml, iem, "Iem")
    {
        const int mix = iem->getIntAttribute("index", -1);
        if (mix < 0 || mix >= kIemMixes) continue;
        iemMaster_[mix].store(static_cast<float>(iem->getDoubleAttribute("master", 1.0)), std::memory_order_relaxed);
        iemMute_[mix].store(iem->getBoolAttribute("mute", false), std::memory_order_relaxed);
        iemOutLeft_[mix].store(iem->getIntAttribute("outL", -1), std::memory_order_relaxed);
        iemOutRight_[mix].store(iem->getIntAttribute("outR", -1), std::memory_order_relaxed);
        forEachXmlChildElementWithTagName(*iem, send, "Send")
        {
            const int ch = send->getIntAttribute("channel", -1);
            if (ch < 0 || ch >= kMaxChannels) continue;
            iemSendGain_[mix][ch].store(static_cast<float>(send->getDoubleAttribute("gain", 0.0)), std::memory_order_relaxed);
            iemSendPan_[mix][ch].store(static_cast<float>(send->getDoubleAttribute("pan", 0.0)), std::memory_order_relaxed);
        }
    }

    for (auto& strip : busStrips_) if (strip) strip->syncFromModel();
    for (auto& strip : dcaStrips_) if (strip) strip->syncFromModel();
    rebuildMixerBank();
    rebuildIemBank();
    refreshRoutingControls();
    refreshIemUi();
    refreshDspUi();
    refreshPadUi();
    refreshSetlistUi();
    refreshDashboard();
    if (migrateLegacyMixer)
        saveAppState();
}

void MainComponent::saveAppState(bool capturePluginState)
{
    juce::XmlElement xml("J3WorshipState");
    xml.setAttribute("version", juce::JUCEApplication::getInstance()->getApplicationVersion());
    xml.setAttribute("themeId", themeId_);
    xml.setAttribute("paLeft", paLeft_.load());
    xml.setAttribute("paRight", paRight_.load());
    xml.setAttribute("clickOutput", clickOutput_.load());
    xml.setAttribute("bpm", bpmSlider_.getValue());
    xml.setAttribute("timeSignatureId", timeSignatureBox_.getSelectedId());
    xml.setAttribute("subdivisionId", subdivisionBox_.getSelectedId());
    xml.setAttribute("accent", accentButton_.getToggleState());
    xml.setAttribute("clickVolume", clickVolumeSlider_.getValue());
    xml.setAttribute("padRootMidi", ambientPad_.rootMidi());
    xml.setAttribute("padMinor", ambientPad_.minor());
    xml.setAttribute("padVolume", static_cast<double>(ambientPad_.volume()));
    xml.setAttribute("padToPa", padToPa_.load(std::memory_order_relaxed));
    xml.setAttribute("setlistIndex", setlist_.size() > 0 ? static_cast<int>(setlist_.currentIndex()) : 0);
    for (std::size_t i = 0; i < setlist_.size(); ++i)
    {
        const auto* song = setlist_.song(i);
        if (song == nullptr) continue;
        auto* songXml = xml.createNewChildElement("Song");
        songXml->setAttribute("name", juce::String(song->name));
        songXml->setAttribute("artist", juce::String(song->artist));
        songXml->setAttribute("key", juce::String(song->key));
        songXml->setAttribute("bpm", song->bpm);
        songXml->setAttribute("numerator", song->numerator);
        songXml->setAttribute("denominator", song->denominator);
    }

    for (const auto& path : pluginCustomLocations_)
    {
        auto* location = xml.createNewChildElement("PluginLocation");
        location->setAttribute("path", path);
    }
    for (const auto& path : favoritePluginPaths_)
    {
        auto* fav = xml.createNewChildElement("PluginFavorite");
        fav->setAttribute("path", path);
    }
    for (const auto& path : recentPluginPaths_)
    {
        auto* recent = xml.createNewChildElement("PluginRecent");
        recent->setAttribute("path", path);
    }

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        for (int slot = 0; slot < kPluginSlots; ++slot)
        {
            auto plugin = channelPlugins_[ch][slot].load(std::memory_order_acquire);
            if (capturePluginState && plugin != nullptr)
            {
                try
                {
                    juce::MemoryBlock state;
                    plugin->getStateInformation(state);
                    pluginStateBase64_[ch][slot] = state.toBase64Encoding();
                }
                catch (...) {}
            }

            if (pluginPaths_[ch][slot].isEmpty())
                continue;

            auto* vst = xml.createNewChildElement("Vst3");
            vst->setAttribute("channel", ch);
            vst->setAttribute("slot", slot);
            vst->setAttribute("path", pluginPaths_[ch][slot]);
            vst->setAttribute("name", pluginNames_[ch][slot]);
            vst->setAttribute("bypass", pluginBypass_[ch][slot].load(std::memory_order_relaxed));
            vst->setAttribute("state", pluginStateBase64_[ch][slot]);
        }
    }

    for (int i = 0; i < kMaxChannels; ++i)
    {
        auto* ch = xml.createNewChildElement("Channel");
        ch->setAttribute("index", i);
        ch->setAttribute("gain", static_cast<double>(channelGain_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("pan", static_cast<double>(channelPan_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("mute", channelMute_[i].load(std::memory_order_relaxed));
        ch->setAttribute("bus", channelBus_[i].load(std::memory_order_relaxed));
        ch->setAttribute("dca", channelDca_[i].load(std::memory_order_relaxed));
        ch->setAttribute("hpf", static_cast<double>(channelHpf_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("lpf", static_cast<double>(channelLpf_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("gate", static_cast<double>(channelGate_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("compThreshold", static_cast<double>(channelCompThreshold_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("compRatio", static_cast<double>(channelCompRatio_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("denoise", static_cast<double>(channelDenoise_[i].load(std::memory_order_relaxed)));
        ch->setAttribute("denoiseThreshold", static_cast<double>(channelDenoiseThreshold_[i].load(std::memory_order_relaxed)));
        for (int band = 0; band < 4; ++band)
        {
            const auto b = juce::String(band);
            ch->setAttribute("eq" + b + "Freq", static_cast<double>(channelEqFreq_[i][band].load(std::memory_order_relaxed)));
            ch->setAttribute("eq" + b + "Gain", static_cast<double>(channelEqGain_[i][band].load(std::memory_order_relaxed)));
            ch->setAttribute("eq" + b + "Q", static_cast<double>(channelEqQ_[i][band].load(std::memory_order_relaxed)));
        }
    }
    for (int i = 0; i < kBuses; ++i)
    {
        auto* bus = xml.createNewChildElement("Bus");
        bus->setAttribute("index", i);
        bus->setAttribute("gain", static_cast<double>(busGain_[i].load(std::memory_order_relaxed)));
        bus->setAttribute("mute", busMute_[i].load(std::memory_order_relaxed));
    }
    for (int i = 0; i < kDcas; ++i)
    {
        auto* dca = xml.createNewChildElement("Dca");
        dca->setAttribute("index", i);
        dca->setAttribute("gain", static_cast<double>(dcaGain_[i].load(std::memory_order_relaxed)));
        dca->setAttribute("mute", dcaMute_[i].load(std::memory_order_relaxed));
    }
    for (int m = 0; m < kIemMixes; ++m)
    {
        auto* iem = xml.createNewChildElement("Iem");
        iem->setAttribute("index", m);
        iem->setAttribute("master", static_cast<double>(iemMaster_[m].load(std::memory_order_relaxed)));
        iem->setAttribute("mute", iemMute_[m].load(std::memory_order_relaxed));
        iem->setAttribute("outL", iemOutLeft_[m].load(std::memory_order_relaxed));
        iem->setAttribute("outR", iemOutRight_[m].load(std::memory_order_relaxed));
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            const float gain = iemSendGain_[m][ch].load(std::memory_order_relaxed);
            const float pan = iemSendPan_[m][ch].load(std::memory_order_relaxed);
            if (gain <= 1.0e-8f && std::abs(pan) < 1.0e-5f) continue;
            auto* send = iem->createNewChildElement("Send");
            send->setAttribute("channel", ch);
            send->setAttribute("gain", static_cast<double>(gain));
            send->setAttribute("pan", static_cast<double>(pan));
        }
    }

    const auto file = getAppStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(xml.toString(), false, false, "\n");
}

void MainComponent::startStopRecording()
{
    if (recordingEnabled_.load(std::memory_order_acquire))
    {
        recordingEnabled_.store(false, std::memory_order_release);
        std::string error;
        if (!recorder_.stop(error) && !error.empty())
            showAudioError(juce::String::fromUTF8("No se pudo finalizar la grabación: ") + juce::String(error));
        recordChannelCount_.store(0, std::memory_order_release);
        updateRecordingUi();
        return;
    }

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        showAudioError("No hay una interfaz de audio activa.");
        return;
    }

    const auto active = device->getActiveInputChannels();
    const auto names = device->getInputChannelNames();
    std::vector<std::string> channelNames;
    channelNames.reserve(j3::kRecordMaxChannels);
    int mapped = 0;
    for (int input = 0; input <= active.getHighestBit() && mapped < static_cast<int>(j3::kRecordMaxChannels); ++input)
    {
        if (!active[input]) continue;
        recordInputIndices_[static_cast<std::size_t>(mapped)] = input;
        channelNames.push_back((input < names.size() && names[input].isNotEmpty()
            ? names[input] : "Input " + juce::String(input + 1)).toStdString());
        ++mapped;
    }
    if (channelNames.empty())
    {
        showAudioError(juce::String::fromUTF8("No hay entradas activas para grabar. Activá entradas desde AUDIO / MIDI."));
        return;
    }

    const auto root = recordingsRoot();
    root.createDirectory();
    const auto freeBytes = root.getBytesFreeOnVolume();
    constexpr std::int64_t kMinimumRecordingFreeBytes = 1024LL * 1024LL * 1024LL;
    if (freeBytes >= 0 && freeBytes < kMinimumRecordingFreeBytes)
    {
        showAudioError(juce::String::fromUTF8("Espacio insuficiente para una grabación segura. Liberá al menos 1 GB en el disco de grabaciones."));
        return;
    }

    const auto now = juce::Time::getCurrentTime();
    const auto dir = root.getChildFile(now.formatted("%Y-%m-%d")).getChildFile(now.formatted("%H%M%S-Service"));
    std::string error;
    if (!recorder_.start(dir.getFullPathName().toStdString(), static_cast<std::uint32_t>(sampleRate_.load()), channelNames, error))
    {
        showAudioError("No se pudo iniciar RECORD SERVICE: " + juce::String(error));
        return;
    }
    recordChannelCount_.store(static_cast<int>(channelNames.size()), std::memory_order_release);
    recordingEnabled_.store(true, std::memory_order_release);
    updateRecordingUi();
}

void MainComponent::stopRecordingAfterDeviceLoss(const juce::String& reason)
{
    const bool wasRecording = recordingEnabled_.exchange(false, std::memory_order_acq_rel);
    if (!wasRecording && !recorder_.recording())
        return;

    std::string error;
    recorder_.stop(error);
    recordChannelCount_.store(0, std::memory_order_release);
    updateRecordingUi();

    juce::String message = reason;
    if (!error.empty()) message << "\nRecorder: " << juce::String(error);
    if (message.isNotEmpty())
        lastAudioError_ = message;
}

void MainComponent::updateRecordingUi()
{
    const bool active = recordingEnabled_.load(std::memory_order_acquire);
    recordButton_.setButtonText(active ? "STOP RECORDING" : "RECORD SERVICE");
    recordButton_.setColour(juce::TextButton::buttonColourId, active ? juce::Colour(danger) : juce::Colour(0xff9b2430));
    juce::String status;
    status << (active ? juce::String::fromUTF8("● RECORDING\n") : "READY\n");
    status << "Folder: " << recordingsRoot().getFullPathName() << "\n";
    status << "Individual WAV files: " << recordChannelCount_.load() << "\n";
    const auto drops = recorder_.overflowCount();
    status << "Dropped recording blocks: " << drops << "\n";
    if (recorder_.hasWorkerError())
        status << juce::String::fromUTF8("⚠ DISK WRITE ERROR — recording protection stopped the writer.\n");
    else if (drops > 0)
        status << juce::String::fromUTF8("⚠ DISK TOO SLOW — el audio en vivo sigue protegido, revisá la grabación.\n");
    status << "Audio is copied into a lock-free queue; disk I/O stays off the real-time callback.";
    recordingStatusLabel_.setText(status, juce::dontSendNotification);
}

void MainComponent::handleTapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTapMs_ > 0.0)
    {
        const double interval = now - lastTapMs_;
        if (interval >= 250.0 && interval <= 2000.0)
            bpmSlider_.setValue(60000.0 / interval, juce::sendNotificationSync);
    }
    lastTapMs_ = now;
}


bool MainComponent::pluginMutationLocked() const noexcept
{
    return liveMonitorEnabled_.load(std::memory_order_acquire)
        || recordingEnabled_.load(std::memory_order_acquire)
        || dawWorkspace_.isRecordingTracks()
        || transportRunning_.load(std::memory_order_acquire)
        || dawWorkspace_.isPlaying();
}

void MainComponent::scanVst3Plugins()
{
    if (pluginScanBusy_.exchange(true, std::memory_order_acq_rel))
    {
        pluginStatusLabel_.setText("Escaneo VST3 en curso...", juce::dontSendNotification);
        return;
    }

    auto* format = pluginFormatManager_.getFormat(0);
    if (format == nullptr)
    {
        pluginScanBusy_.store(false, std::memory_order_release);
        pluginStatusLabel_.setText("VST3 host format is unavailable in this build.", juce::dontSendNotification);
        return;
    }

    const auto searchPath = format->getDefaultLocationsToSearch();
    std::vector<std::filesystem::path> roots;
    roots.reserve(static_cast<std::size_t>(searchPath.getNumPaths() + pluginCustomLocations_.size()));
    for (int i = 0; i < searchPath.getNumPaths(); ++i)
        roots.emplace_back(std::filesystem::u8path(searchPath[i].getFullPathName().toStdString()));
    for (const auto& custom : pluginCustomLocations_)
    {
        const juce::File folder(custom);
        if (folder.isDirectory())
            roots.emplace_back(std::filesystem::u8path(folder.getFullPathName().toStdString()));
    }

    pluginStatusLabel_.setText(
        "Escaneando VST3 reales en segundo plano... podés seguir usando el audio.",
        juce::dontSendNotification);

    if (pluginScanThread_.joinable())
        pluginScanThread_.join();

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    pluginScanThread_ = std::jthread([safe, roots = std::move(roots)](std::stop_token stop) mutable
    {
        j3::PluginCatalog scannedBundles;
        scannedBundles.scan(roots);
        if (stop.stop_requested())
            return;

        std::vector<juce::PluginDescription> descriptions;
        juce::VST3PluginFormat scannerFormat;

        for (const auto& record : scannedBundles.plugins())
        {
            if (stop.stop_requested())
                return;

            const juce::String bundlePath(record.path.wstring().c_str());
            juce::OwnedArray<juce::PluginDescription> types;
            scannerFormat.findAllTypesForFile(types, bundlePath);
            for (auto* type : types)
                if (type != nullptr)
                    descriptions.push_back(*type);
        }

        if (stop.stop_requested())
            return;

        std::sort(descriptions.begin(), descriptions.end(),
            [](const juce::PluginDescription& a, const juce::PluginDescription& b)
            {
                const int byName = a.name.compareIgnoreCase(b.name);
                if (byName != 0)
                    return byName < 0;
                const int byMaker = a.manufacturerName.compareIgnoreCase(b.manufacturerName);
                if (byMaker != 0)
                    return byMaker < 0;
                return pluginDescriptionKey(a) < pluginDescriptionKey(b);
            });

        descriptions.erase(std::unique(descriptions.begin(), descriptions.end(),
            [](const juce::PluginDescription& a, const juce::PluginDescription& b)
            {
                return pluginDescriptionKey(a) == pluginDescriptionKey(b);
            }), descriptions.end());

        juce::MessageManager::callAsync(
            [safe, scannedBundles = std::move(scannedBundles), descriptions = std::move(descriptions)]() mutable
            {
                if (safe == nullptr)
                    return;

                safe->pluginCatalog_ = std::move(scannedBundles);
                safe->pluginDescriptions_ = std::move(descriptions);
                safe->pluginScanBusy_.store(false, std::memory_order_release);
                safe->pluginsScanned_ = true;
                safe->refreshPluginBrowser();
                safe->restoreSavedPluginsAfterScan();
                safe->refreshPluginUi();

                safe->pluginStatusLabel_.setText(
                    juce::String(safe->pluginDescriptions_.size()) + " plugins VST3 encontrados en "
                        + juce::String(safe->pluginCatalog_.plugins().size())
                        + " bundles. Waves shells incluidos como plugins individuales.",
                    juce::dontSendNotification);
            });
    });
}

void MainComponent::chooseAdditionalVst3Folder()
{
    juce::File start = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
   #if JUCE_WINDOWS
    const juce::File standard("C:\\Program Files\\Common Files\\VST3");
    if (standard.isDirectory())
        start = standard;
   #endif

    pluginFolderChooser_ = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Elegí una carpeta VST3 adicional"), start);
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    pluginFolderChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;
            const auto folder = chooser.getResult();
            if (!folder.isDirectory())
                return;
            safe->pluginCustomLocations_.addIfNotAlreadyThere(folder.getFullPathName());
            safe->saveAppState();
            safe->scanVst3Plugins();
        });
}

void MainComponent::restoreSavedPluginsAfterScan()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int slot = 0; slot < kPluginSlots; ++slot)
            if (pluginPaths_[ch][slot].isNotEmpty()
                && channelPlugins_[ch][slot].load(std::memory_order_acquire) == nullptr)
                loadPluginPathIntoSlot(pluginPaths_[ch][slot], ch, slot);
}

void MainComponent::refreshPluginBrowser()
{
    const auto& plugins = pluginDescriptions_;
    juce::String previousKey;
    const int previousRow = pluginCatalogBox_.getSelectedId() - 1;
    if (previousRow >= 0 && previousRow < static_cast<int>(pluginBrowserIndices_.size()))
    {
        const int previousActual = pluginBrowserIndices_[static_cast<std::size_t>(previousRow)];
        if (previousActual >= 0 && previousActual < static_cast<int>(plugins.size()))
            previousKey = pluginDescriptionKey(plugins[static_cast<std::size_t>(previousActual)]);
    }

    pluginBrowserIndices_.clear();
    pluginCatalogBox_.clear(juce::dontSendNotification);

    const auto query = pluginSearch_.getText().trim().toLowerCase();
    const int category = std::max(1, pluginCategoryBox_.getSelectedId());

    auto matchesCategory = [this, category](const juce::PluginDescription& plugin)
    {
        const auto key = pluginDescriptionKey(plugin);
        const auto haystack = (plugin.name + " " + plugin.manufacturerName + " "
            + plugin.fileOrIdentifier).toLowerCase();

        if (category == 1) return true;
        if (category == 10)
            return favoritePluginPaths_.contains(key) || favoritePluginPaths_.contains(plugin.fileOrIdentifier);
        if (category == 11)
            return recentPluginPaths_.contains(key) || recentPluginPaths_.contains(plugin.fileOrIdentifier);
        if (category == 12)
            return haystack.contains("j3");

        switch (category)
        {
            case 2: return haystack.contains(" eq") || haystack.startsWith("eq") || haystack.contains("equalizer")
                        || haystack.contains("pro-q");
            case 3: return haystack.contains("compress") || haystack.contains("limiter") || haystack.contains("1176")
                        || haystack.contains("2a") || haystack.contains("cla-") || haystack.contains("dynamic")
                        || haystack.contains("gate");
            case 4: return haystack.contains("reverb") || haystack.contains("verb") || haystack.contains("room")
                        || haystack.contains("hall") || haystack.contains("valhalla");
            case 5: return haystack.contains("delay") || haystack.contains("echo");
            case 6: return haystack.contains("satur") || haystack.contains("tape") || haystack.contains("tube")
                        || haystack.contains("distort") || haystack.contains("drive");
            case 7: return haystack.contains("guitar") || haystack.contains(" amp") || haystack.contains("cab");
            case 8: return plugin.isInstrument || haystack.contains("instrument") || haystack.contains("synth")
                        || haystack.contains("piano") || haystack.contains("kontakt") || haystack.contains("keys")
                        || haystack.contains("organ") || haystack.contains("drum");
            case 9: return haystack.contains("utility") || haystack.contains("meter") || haystack.contains("analy")
                        || haystack.contains("gain") || haystack.contains("stereo");
            default: return true;
        }
    };

    int id = 1;
    int selectedId = 0;
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i)
    {
        const auto& plugin = plugins[static_cast<std::size_t>(i)];
        const auto searchText = (plugin.name + " " + plugin.manufacturerName).toLowerCase();
        if (query.isNotEmpty() && !searchText.contains(query))
            continue;
        if (!matchesCategory(plugin))
            continue;

        pluginBrowserIndices_.push_back(i);
        const auto key = pluginDescriptionKey(plugin);
        juce::String label;
        if (favoritePluginPaths_.contains(key) || favoritePluginPaths_.contains(plugin.fileOrIdentifier))
            label << "* ";
        label << plugin.name;
        if (plugin.manufacturerName.isNotEmpty())
            label << juce::String::fromUTF8(" · ") << plugin.manufacturerName;
        pluginCatalogBox_.addItem(label, id);
        if (key == previousKey)
            selectedId = id;
        ++id;
    }

    if (selectedId == 0 && !pluginBrowserIndices_.empty())
        selectedId = 1;

    pluginCatalogBox_.setSelectedId(selectedId, juce::dontSendNotification);
    pluginCatalogList_.updateContent();
    if (selectedId > 0)
        pluginCatalogList_.selectRow(selectedId - 1, false, true);
    else
        pluginCatalogList_.deselectAllRows();
    pluginCatalogList_.repaint();
    refreshPluginUi();
}


int MainComponent::getNumRows()
{
    return static_cast<int>(pluginBrowserIndices_.size());
}

void MainComponent::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                     int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(pluginBrowserIndices_.size()))
        return;

    const int actual = pluginBrowserIndices_[static_cast<std::size_t>(rowNumber)];
    if (actual < 0 || actual >= static_cast<int>(pluginDescriptions_.size()))
        return;

    const auto& plugin = pluginDescriptions_[static_cast<std::size_t>(actual)];
    const auto row = juce::Rectangle<int>(0, 0, width, height).reduced(2);

    if (rowIsSelected)
    {
        g.setColour(juce::Colour(accentDeep).withAlpha(0.82f));
        g.fillRoundedRectangle(row.toFloat(), 5.0f);
    }
    else if ((rowNumber & 1) != 0)
    {
        g.setColour(juce::Colour(0xff111a22));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);
    }

    const auto key = pluginDescriptionKey(plugin);
    const bool favorite = favoritePluginPaths_.contains(key)
        || favoritePluginPaths_.contains(plugin.fileOrIdentifier);
    juce::String name = favorite ? "*  " : "";
    name << plugin.name;

    auto textArea = row.reduced(10, 3);
    auto nameArea = textArea.removeFromTop(20);
    g.setColour(juce::Colour(text));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(name, nameArea, juce::Justification::centredLeft, true);

    juce::String detail = plugin.manufacturerName.isNotEmpty() ? plugin.manufacturerName : "VST3";
    const juce::File pluginFile(plugin.fileOrIdentifier);
    const auto bundleName = pluginFile.getFileName();
    if (bundleName.isNotEmpty())
        detail << "  |  " << bundleName;
    g.setColour(juce::Colour(mutedText));
    g.setFont(juce::FontOptions(10.8f));
    g.drawText(detail, textArea, juce::Justification::centredLeft, true);
}

void MainComponent::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(pluginBrowserIndices_.size()))
    {
        pluginCatalogBox_.setSelectedId(0, juce::dontSendNotification);
        refreshPluginUi();
        return;
    }

    pluginCatalogBox_.setSelectedId(lastRowSelected + 1, juce::dontSendNotification);
    refreshPluginUi();
}

void MainComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(pluginBrowserIndices_.size()))
        return;

    pluginCatalogList_.selectRow(row, false, true);
    pluginCatalogBox_.setSelectedId(row + 1, juce::dontSendNotification);
    refreshPluginUi();
    loadSelectedPlugin();
}

void MainComponent::refreshPluginUi()
{
    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const int slot = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);

    pluginSlotBox_.clear(juce::dontSendNotification);
    for (int i = 0; i < kPluginSlots; ++i)
    {
        juce::String slotName = juce::String(i + 1) + juce::String::fromUTF8(" · ");
        slotName << (pluginNames_[ch][i].isNotEmpty() ? pluginNames_[ch][i] : juce::String("Empty"));
        if (pluginBypass_[ch][i].load(std::memory_order_relaxed))
            slotName << "  [BYPASS]";
        pluginSlotBox_.addItem(slotName, i + 1);
    }
    pluginSlotBox_.setSelectedId(slot + 1, juce::dontSendNotification);

    const auto routedName = dawWorkspace_.mixerInsertName(ch);
    juce::String chainHeader = "MIXER INSERT " + juce::String(ch + 1);
    if (routedName.isNotEmpty())
        chainHeader << juce::String::fromUTF8("  ·  ") << routedName;
    chainHeader << "  ·  FX CHAIN";
    pluginsTitle_.setText(chainHeader, juce::dontSendNotification);

    for (int i = 0; i < kPluginSlots; ++i)
    {
        if (!pluginSlotButtons_[i]) continue;
        const bool occupied = pluginNames_[ch][i].isNotEmpty() || pluginPaths_[ch][i].isNotEmpty()
            || channelPlugins_[ch][i].load(std::memory_order_acquire) != nullptr;
        juce::String label = juce::String(i + 1) + "   ";
        if (occupied)
            label << (pluginNames_[ch][i].isNotEmpty() ? pluginNames_[ch][i] : juce::String("PLUGIN"));
        else
            label << "CLICK TO CHOOSE PLUGIN";
        if (pluginBypass_[ch][i].load(std::memory_order_relaxed))
            label << "   [BYPASS]";
        pluginSlotButtons_[i]->setButtonText(label);
        pluginSlotButtons_[i]->setColour(juce::TextButton::buttonColourId,
            i == slot ? juce::Colour(accentDeep) : juce::Colour(occupied ? 0xff173246 : panel3));
    }

    nativeEqButton_.setButtonText("J3 EQ  |  NATIVE  |  PRE-FX");
    pluginBrowserTitle_.setText("PLUGINS  |  SLOT " + juce::String(slot + 1),
        juce::dontSendNotification);

    auto plugin = channelPlugins_[ch][slot].load(std::memory_order_acquire);

    const int browserRow = pluginCatalogBox_.getSelectedId() - 1;
    const auto& catalog = pluginDescriptions_;
    const bool browserSelectionValid = browserRow >= 0
        && browserRow < static_cast<int>(pluginBrowserIndices_.size())
        && pluginBrowserIndices_[static_cast<std::size_t>(browserRow)] >= 0
        && pluginBrowserIndices_[static_cast<std::size_t>(browserRow)] < static_cast<int>(catalog.size());

    const bool loaded = plugin != nullptr;
    bypassPluginButton_.setToggleState(pluginBypass_[ch][slot].load(std::memory_order_relaxed), juce::dontSendNotification);
    bypassPluginButton_.setEnabled(loaded);
    const bool slotOccupied = loaded || pluginPaths_[ch][slot].isNotEmpty();
    removePluginButton_.setEnabled(slotOccupied);
    movePluginUpButton_.setEnabled(slotOccupied && slot > 0);
    movePluginDownButton_.setEnabled(slotOccupied && slot < kPluginSlots - 1);
    openPluginEditorButton_.setEnabled(loaded);
    loadPluginButton_.setEnabled(pluginsScanned_ && browserSelectionValid);
    favoritePluginButton_.setEnabled(browserSelectionValid);
    if (browserSelectionValid)
    {
        const auto actual = pluginBrowserIndices_[static_cast<std::size_t>(browserRow)];
        const auto& selectedPlugin = catalog[static_cast<std::size_t>(actual)];
        const auto selectedKey = pluginDescriptionKey(selectedPlugin);
        favoritePluginButton_.setToggleState(
            favoritePluginPaths_.contains(selectedKey)
                || favoritePluginPaths_.contains(selectedPlugin.fileOrIdentifier),
            juce::dontSendNotification);
    }
    else
    {
        favoritePluginButton_.setToggleState(false, juce::dontSendNotification);
    }

    juce::String status;
    status << "Mixer Insert " << (ch + 1);
    if (routedName.isNotEmpty()) status << juce::String::fromUTF8(" · ") << routedName;
    status << juce::String::fromUTF8(" · FX Slot ") << (slot + 1) << "\n";
    if (loaded)
    {
        status << "Loaded: " << (pluginNames_[ch][slot].isNotEmpty() ? pluginNames_[ch][slot] : plugin->getName()) << "\n";
        const auto faults = pluginFaults_[ch][slot].load(std::memory_order_relaxed);
        status << "Latency: " << plugin->getLatencySamples() << juce::String::fromUTF8(" samples · ")
               << (pluginBypass_[ch][slot].load(std::memory_order_relaxed) ? "BYPASSED" : "ACTIVE") << "\n";
        if (faults > 0)
            status << juce::String::fromUTF8("LIVE SAFE: auto-bypass por audio inválido/fallo (") << faults << ").\n";
        status << (plugin->hasEditor()
            ? "OPEN PLUGIN abre la interfaz nativa del VST3."
            : juce::String::fromUTF8("OPEN PLUGIN abre el editor genérico de parámetros."));
    }
    else if (pluginPaths_[ch][slot].isNotEmpty())
    {
        status << "Saved insert: " << pluginNames_[ch][slot] << "\n";
        status << "Not loaded yet or unavailable at its previous path.";
    }
    else
    {
        status << "Slot libre. ";
        if (pluginScanBusy_.load(std::memory_order_acquire))
            status << "Escaneando VST3...";
        else if (!pluginsScanned_)
            status << "Usá ESCANEAR CARPETA... y elegí dónde están tus plugins.";
        else
            status << pluginBrowserIndices_.size() << " visibles / " << pluginDescriptions_.size()
                   << " plugins VST3 encontrados.";
    }
    pluginStatusLabel_.setText(status, juce::dontSendNotification);
}


void MainComponent::openNativeEqEditor()
{
    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const auto routedName = dawWorkspace_.mixerInsertName(ch);
    const juce::String insertName = "MIXER INSERT " + juce::String(ch + 1)
        + (routedName.isNotEmpty() ? "  |  " + routedName : juce::String());

    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto editor = std::make_unique<NativeEqEditor>(
        insertName,
        channelHpf_[ch],
        channelLpf_[ch],
        channelEqFreq_[ch],
        channelEqGain_[ch],
        channelEqQ_[ch],
        [safe, ch]
        {
            if (safe == nullptr)
                return;
            safe->markDspDirty(ch);
        });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor.release());
    options.dialogTitle = "J3 Worship | J3 Parametric EQ";
    options.dialogBackgroundColour = juce::Colour(background);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::loadSelectedPlugin()
{
    if (pluginMutationLocked())
    {
        showAudioError(juce::String::fromUTF8("Por seguridad, no se cargan plugins nuevos durante LIVE, reproducción o grabación. Podés abrir o bypassar los que ya están cargados."));
        return;
    }

    const int row = pluginCatalogBox_.getSelectedId() - 1;
    if (row < 0 || row >= static_cast<int>(pluginBrowserIndices_.size()))
    {
        showAudioError(juce::String::fromUTF8("Seleccioná un VST3 del navegador."));
        return;
    }

    const int selected = pluginBrowserIndices_[static_cast<std::size_t>(row)];
    if (selected < 0 || selected >= static_cast<int>(pluginDescriptions_.size()))
        return;

    const auto description = pluginDescriptions_[static_cast<std::size_t>(selected)];
    const auto key = pluginDescriptionKey(description);
    recentPluginPaths_.removeString(key);
    recentPluginPaths_.removeString(description.fileOrIdentifier);
    recentPluginPaths_.insert(0, key);
    while (recentPluginPaths_.size() > 12)
        recentPluginPaths_.remove(recentPluginPaths_.size() - 1);

    saveAppState();
    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const int slot = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);
    loadPluginDescriptionIntoSlot(description, ch, slot);
}

void MainComponent::loadPluginPathIntoSlot(const juce::String& savedKey, int channel, int slot)
{
    if (channel < 0 || channel >= kMaxChannels || slot < 0 || slot >= kPluginSlots || savedKey.isEmpty())
        return;

    for (const auto& description : pluginDescriptions_)
    {
        const bool legacyNameMatches = savedKey == description.fileOrIdentifier
            && (pluginNames_[channel][slot].isEmpty()
                || pluginNames_[channel][slot].equalsIgnoreCase(description.name));
        if (savedPluginKeyMatches(savedKey, description) && (savedKey != description.fileOrIdentifier || legacyNameMatches))
        {
            loadPluginDescriptionIntoSlot(description, channel, slot);
            return;
        }
    }

    auto* format = pluginFormatManager_.getFormat(0);
    if (format == nullptr)
        return;

    juce::String fileOrIdentifier = savedKey;
    const int separator = savedKey.indexOf("||");
    if (separator > 0)
        fileOrIdentifier = savedKey.substring(0, separator);

    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile(types, fileOrIdentifier);
    if (types.isEmpty())
    {
        pluginStatusLabel_.setText("Could not identify VST3: " + fileOrIdentifier, juce::dontSendNotification);
        return;
    }

    juce::PluginDescription description = *types[0];
    for (auto* candidate : types)
    {
        if (candidate == nullptr)
            continue;
        if (pluginDescriptionKey(*candidate) == savedKey
            || (pluginNames_[channel][slot].isNotEmpty()
                && pluginNames_[channel][slot].equalsIgnoreCase(candidate->name)))
        {
            description = *candidate;
            break;
        }
    }

    loadPluginDescriptionIntoSlot(description, channel, slot);
}

void MainComponent::loadPluginDescriptionIntoSlot(const juce::PluginDescription& description,
                                                   int channel, int slot)
{
    if (channel < 0 || channel >= kMaxChannels || slot < 0 || slot >= kPluginSlots
        || description.fileOrIdentifier.isEmpty())
        return;

    const double sr = std::max(8000.0, sampleRate_.load(std::memory_order_acquire));
    const int bs = std::max(64, bufferSize_.load(std::memory_order_acquire));
    const bool savedBypass = pluginBypass_[channel][slot].load(std::memory_order_relaxed);
    const auto savedState = pluginStateBase64_[channel][slot];
    const auto key = pluginDescriptionKey(description);

    pluginStatusLabel_.setText("Loading " + description.name + "...", juce::dontSendNotification);
    pluginFormatManager_.createPluginInstanceAsync(
        description, sr, bs,
        [safe = juce::Component::SafePointer<MainComponent>(this), channel, slot, key,
         pluginName = description.name, savedBypass, savedState]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (safe == nullptr)
                return;

            if (instance == nullptr)
            {
                safe->pluginStatusLabel_.setText("VST3 load failed: " + error, juce::dontSendNotification);
                return;
            }

            const int inChannels = instance->getTotalNumInputChannels();
            const int outChannels = instance->getTotalNumOutputChannels();
            if (inChannels > 2 || outChannels > 2 || inChannels < 1 || outChannels < 1)
            {
                safe->pluginStatusLabel_.setText(
                    "Insert rejected: mixer FX slots accept mono/stereo audio-effect VST3 plug-ins.",
                    juce::dontSendNotification);
                return;
            }

            instance->setRateAndBufferSizeDetails(
                std::max(8000.0, safe->sampleRate_.load(std::memory_order_acquire)),
                std::max(64, safe->bufferSize_.load(std::memory_order_acquire)));
            instance->prepareToPlay(
                std::max(8000.0, safe->sampleRate_.load(std::memory_order_acquire)),
                std::max(64, safe->bufferSize_.load(std::memory_order_acquire)));

            if (savedState.isNotEmpty())
            {
                juce::MemoryBlock state;
                if (state.fromBase64Encoding(savedState))
                    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            }

            auto shared = std::shared_ptr<juce::AudioPluginInstance>(std::move(instance));
            safe->pluginPaths_[channel][slot] = key;
            safe->pluginNames_[channel][slot] = pluginName.isNotEmpty() ? pluginName : shared->getName();
            safe->pluginBypass_[channel][slot].store(savedBypass, std::memory_order_release);
            safe->pluginFaults_[channel][slot].store(0, std::memory_order_release);
            safe->channelPlugins_[channel][slot].store(shared, std::memory_order_release);
            safe->refreshPluginUi();
            safe->saveAppState();
        });
}

void MainComponent::removeSelectedPlugin()
{
    if (pluginMutationLocked())
    {
        showAudioError(juce::String::fromUTF8("Por seguridad, no se quitan plugins durante LIVE, reproducción o grabación. Usá BYPASS si necesitás sacarlo de la cadena inmediatamente."));
        return;
    }

    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const int slot = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);
    channelPlugins_[ch][slot].store({}, std::memory_order_release);
    pluginPaths_[ch][slot].clear();
    pluginNames_[ch][slot].clear();
    pluginStateBase64_[ch][slot].clear();
    pluginBypass_[ch][slot].store(false, std::memory_order_release);
    pluginFaults_[ch][slot].store(0, std::memory_order_release);
    refreshPluginUi();
    saveAppState();
}

void MainComponent::moveSelectedPlugin(int delta)
{
    if (pluginMutationLocked())
    {
        showAudioError(juce::String::fromUTF8("Por seguridad, el orden de FX no se cambia durante LIVE, reproducción o grabación."));
        return;
    }

    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const int from = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);
    const int to = from + delta;
    if (to < 0 || to >= kPluginSlots || to == from)
        return;
    if (pluginPaths_[ch][from].isEmpty() && channelPlugins_[ch][from].load(std::memory_order_acquire) == nullptr)
        return;

    saveAppState(true);

    auto fromPlugin = channelPlugins_[ch][from].load(std::memory_order_acquire);
    auto toPlugin = channelPlugins_[ch][to].load(std::memory_order_acquire);
    channelPlugins_[ch][from].store(toPlugin, std::memory_order_release);
    channelPlugins_[ch][to].store(fromPlugin, std::memory_order_release);

    std::swap(pluginPaths_[ch][from], pluginPaths_[ch][to]);
    std::swap(pluginNames_[ch][from], pluginNames_[ch][to]);
    std::swap(pluginStateBase64_[ch][from], pluginStateBase64_[ch][to]);

    const bool fromBypass = pluginBypass_[ch][from].load(std::memory_order_relaxed);
    const bool toBypass = pluginBypass_[ch][to].load(std::memory_order_relaxed);
    pluginBypass_[ch][from].store(toBypass, std::memory_order_release);
    pluginBypass_[ch][to].store(fromBypass, std::memory_order_release);

    const int fromFaults = pluginFaults_[ch][from].load(std::memory_order_relaxed);
    const int toFaults = pluginFaults_[ch][to].load(std::memory_order_relaxed);
    pluginFaults_[ch][from].store(toFaults, std::memory_order_release);
    pluginFaults_[ch][to].store(fromFaults, std::memory_order_release);

    pluginSlotBox_.setSelectedId(to + 1, juce::dontSendNotification);
    saveAppState(false);
    refreshPluginUi();
}

void MainComponent::openSelectedPluginEditor()
{
    const int ch = juce::jlimit(0, kMaxChannels - 1, pluginChannelBox_.getSelectedId() - 1);
    const int slot = juce::jlimit(0, kPluginSlots - 1, pluginSlotBox_.getSelectedId() - 1);
    auto plugin = channelPlugins_[ch][slot].load(std::memory_order_acquire);
    if (plugin == nullptr)
        return;

    auto holder = std::make_unique<GenericPluginEditorHolder>(plugin);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(holder.release());
    options.dialogTitle = juce::String::fromUTF8("J3 Worship · ") + plugin->getName();
    options.dialogBackgroundColour = juce::Colour(panel);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

const float* MainComponent::processPluginChain(int channel, const float* input, int numSamples) noexcept
{
    if (channel < 0 || channel >= kMaxChannels || input == nullptr || numSamples <= 0
        || pluginScratch_.getNumChannels() < 2 || pluginScratch_.getNumSamples() < numSamples)
        return input;

    bool hasPlugin = false;
    for (int slot = 0; slot < kPluginSlots; ++slot)
        if (!pluginBypass_[channel][slot].load(std::memory_order_relaxed)
            && channelPlugins_[channel][slot].load(std::memory_order_acquire) != nullptr)
            hasPlugin = true;
    if (!hasPlugin)
        return input;

    pluginScratch_.copyFrom(0, 0, input, numSamples);
    pluginScratch_.copyFrom(1, 0, input, numSamples);

    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        if (pluginBypass_[channel][slot].load(std::memory_order_relaxed))
            continue;

        auto plugin = channelPlugins_[channel][slot].load(std::memory_order_acquire);
        if (plugin == nullptr)
            continue;

        const int channels = juce::jlimit(1, 2,
            std::max(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
        if (channels == 2)
            pluginScratch_.copyFrom(1, 0, pluginScratch_, 0, 0, numSamples);
        for (int ch = 0; ch < channels; ++ch)
            pluginGuardScratch_.copyFrom(ch, 0, pluginScratch_, ch, 0, numSamples);

        bool restoreAndBypass = false;
        try
        {
            juce::AudioBuffer<float> view(pluginScratch_.getArrayOfWritePointers(), channels, numSamples);
            pluginMidiScratch_.clear();
            plugin->processBlock(view, pluginMidiScratch_);

            for (int ch = 0; ch < channels && !restoreAndBypass; ++ch)
            {
                const auto* data = pluginScratch_.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    if (!std::isfinite(data[i]) || std::abs(data[i]) > 64.0f)
                    {
                        restoreAndBypass = true;
                        break;
                    }
                }
            }

            if (!restoreAndBypass && plugin->getTotalNumOutputChannels() >= 2)
            {
                auto* mono = pluginScratch_.getWritePointer(0);
                const auto* right = pluginScratch_.getReadPointer(1);
                for (int i = 0; i < numSamples; ++i)
                    mono[i] = (mono[i] + right[i]) * 0.70710678f;
            }
        }
        catch (...)
        {
            restoreAndBypass = true;
        }

        if (restoreAndBypass)
        {
            for (int ch = 0; ch < channels; ++ch)
                pluginScratch_.copyFrom(ch, 0, pluginGuardScratch_, ch, 0, numSamples);
            pluginBypass_[channel][slot].store(true, std::memory_order_release);
            pluginFaults_[channel][slot].fetch_add(1, std::memory_order_relaxed);
        }

        pluginScratch_.copyFrom(1, 0, pluginScratch_, 0, 0, numSamples);
    }

    return pluginScratch_.getReadPointer(0);
}

void MainComponent::processPluginChainStereo(int channel, float* left, float* right, int numSamples) noexcept
{
    if (channel < 0 || channel >= kMaxChannels || left == nullptr || right == nullptr || numSamples <= 0
        || pluginScratch_.getNumChannels() < 2 || pluginScratch_.getNumSamples() < numSamples
        || pluginGuardScratch_.getNumChannels() < 2 || pluginGuardScratch_.getNumSamples() < numSamples)
        return;

    bool hasPlugin = false;
    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        if (!pluginBypass_[channel][slot].load(std::memory_order_relaxed)
            && channelPlugins_[channel][slot].load(std::memory_order_acquire) != nullptr)
        {
            hasPlugin = true;
            break;
        }
    }
    if (!hasPlugin)
        return;

    pluginScratch_.copyFrom(0, 0, left, numSamples);
    pluginScratch_.copyFrom(1, 0, right, numSamples);

    for (int slot = 0; slot < kPluginSlots; ++slot)
    {
        if (pluginBypass_[channel][slot].load(std::memory_order_relaxed))
            continue;

        auto plugin = channelPlugins_[channel][slot].load(std::memory_order_acquire);
        if (plugin == nullptr)
            continue;

        const int channels = juce::jlimit(1, 2,
            std::max(plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));

        if (channels == 1)
        {
            auto* mono = pluginScratch_.getWritePointer(0);
            const auto* stereoRight = pluginScratch_.getReadPointer(1);
            for (int i = 0; i < numSamples; ++i)
                mono[i] = (mono[i] + stereoRight[i]) * 0.70710678f;
        }

        for (int ch = 0; ch < channels; ++ch)
            pluginGuardScratch_.copyFrom(ch, 0, pluginScratch_, ch, 0, numSamples);

        bool restoreAndBypass = false;
        try
        {
            juce::AudioBuffer<float> view(pluginScratch_.getArrayOfWritePointers(), channels, numSamples);
            pluginMidiScratch_.clear();
            plugin->processBlock(view, pluginMidiScratch_);

            for (int ch = 0; ch < channels && !restoreAndBypass; ++ch)
            {
                const auto* data = pluginScratch_.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    if (!std::isfinite(data[i]) || std::abs(data[i]) > 64.0f)
                    {
                        restoreAndBypass = true;
                        break;
                    }
                }
            }
        }
        catch (...)
        {
            restoreAndBypass = true;
        }

        if (restoreAndBypass)
        {
            for (int ch = 0; ch < channels; ++ch)
                pluginScratch_.copyFrom(ch, 0, pluginGuardScratch_, ch, 0, numSamples);
            pluginBypass_[channel][slot].store(true, std::memory_order_release);
            pluginFaults_[channel][slot].fetch_add(1, std::memory_order_relaxed);
        }

        if (channels == 1)
            pluginScratch_.copyFrom(1, 0, pluginScratch_, 0, 0, numSamples);
    }

    juce::FloatVectorOperations::copy(left, pluginScratch_.getReadPointer(0), numSamples);
    juce::FloatVectorOperations::copy(right, pluginScratch_.getReadPointer(1), numSamples);
}

void MainComponent::refreshPadUi()
{
    static const std::array<juce::String, 12> keys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int note = ambientPad_.rootMidi();
    const int keyIndex = juce::jlimit(0, 11, note - 60);
    padKeyBox_.setSelectedId(keyIndex + 1, juce::dontSendNotification);
    padMinorButton_.setToggleState(ambientPad_.minor(), juce::dontSendNotification);
    padVolumeSlider_.setValue(ambientPad_.volume(), juce::dontSendNotification);
    padToPaButton_.setToggleState(padToPa_.load(std::memory_order_relaxed), juce::dontSendNotification);
    padEnabledButton_.setToggleState(ambientPad_.enabled(), juce::dontSendNotification);

    juce::String info;
    info << "Chord: " << keys[static_cast<std::size_t>(keyIndex)] << (ambientPad_.minor() ? " minor" : " major") << "\n";
    info << juce::String::fromUTF8("Stereo ambient generator · continuous sustain · click-free fade in/out\n");
    info << "Route: " << (padToPa_.load(std::memory_order_relaxed) ? "PA master" : "OFF")
         << juce::String::fromUTF8(" · Output protection remains active.");
    padInfoLabel_.setText(info, juce::dontSendNotification);
}

void MainComponent::updateClickUi()
{
    const int out = clickOutput_.load(std::memory_order_relaxed);
    juce::String route = "CLICK routing: ";
    if (out < 0) route << juce::String::fromUTF8("OFF — choose an output in AUDIO / ROUTING.");
    else route << "Output " << (out + 1) << ". J3 Safe Routing blocks this output if it is also used by PA.";
    route << "\nTempo: " << juce::String(bpmSlider_.getValue(), 1) << juce::String::fromUTF8(" BPM · ")
          << clickGenerator_.numerator() << "/" << clickGenerator_.denominator();
    clickRouteLabel_.setText(route, juce::dontSendNotification);
}

void MainComponent::configureAudio()
{
    deviceManager_.addChangeListener(this);
    const auto stateFile = getAudioStateFile();
    std::unique_ptr<juce::XmlElement> savedState;
    if (stateFile.existsAsFile())
        savedState = juce::XmlDocument::parse(stateFile);

    // A DAW must always be able to play through an ordinary Windows stereo device.
    // Requesting kMaxChannels here made first-run startup fail on laptops/headphones
    // because JUCE tried to satisfy a 48-in/48-out configuration. Saved multichannel
    // setups (XR18, Focusrite, etc.) are still restored from XML when they exist.
    auto error = deviceManager_.initialise(0, 2, savedState.get(), true, {}, nullptr);

    if (error.isNotEmpty() || deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        deviceManager_.closeAudioDevice();
        const auto fallbackError = deviceManager_.initialise(0, 2, nullptr, true, {}, nullptr);
        if (fallbackError.isNotEmpty())
            error = fallbackError;
        else
            error.clear();
    }

    if (error.isNotEmpty() || deviceManager_.getCurrentAudioDevice() == nullptr)
    {
        lastAudioError_ = error.isNotEmpty()
            ? error
            : juce::String::fromUTF8("Windows no devolvió una salida de audio activa.");
        statusLabel_.setText(juce::String::fromUTF8("Audio: requiere configuración"), juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
        scheduleReconnect();
    }
    else
    {
        lastAudioError_.clear();
    }

    deviceManager_.addAudioCallback(this);

    scanAvailableDevices();
    refreshRoutingControls();
    updateDiagnostics();
    saveAudioState();
}

void MainComponent::preferAsioWhenAvailable(bool onlyIfNoSavedState)
{
    juce::ignoreUnused(onlyIfNoSavedState);
#if JUCE_WINDOWS && JUCE_ASIO
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        if (type == nullptr || !type->getTypeName().containsIgnoreCase("ASIO"))
            continue;
        type->scanForDevices();
        const auto outputs = type->getDeviceNames(false);
        const auto inputs = type->getDeviceNames(true);
        if (!outputs.isEmpty() || !inputs.isEmpty())
        {
            deviceManager_.setCurrentAudioDeviceType(type->getTypeName(), true);
            return;
        }
    }
#endif
}

void MainComponent::scanAvailableDevices()
{
    juce::String inventory;
    for (auto* type : deviceManager_.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;
        type->scanForDevices();
        const auto outputs = type->getDeviceNames(false);
        const auto inputs = type->getDeviceNames(true);
        inventory << type->getTypeName() << ": ";
        if (outputs.isEmpty() && inputs.isEmpty())
        {
            inventory << "no devices";
        }
        else
        {
            juce::StringArray unique;
            unique.addArray(outputs);
            unique.addArray(inputs);
            unique.removeDuplicates(false);
            inventory << unique.joinIntoString(", ");
        }
        inventory << "\n";
    }
    deviceInventory_ = inventory.trimEnd();
}

void MainComponent::saveAudioState()
{
    if (shuttingDown_.load(std::memory_order_acquire) && deviceManager_.getCurrentAudioDevice() == nullptr)
        return;

    if (auto state = deviceManager_.createStateXml())
    {
        const auto file = getAudioStateFile();
        const auto result = file.getParentDirectory().createDirectory();
        if (result.wasOk())
            file.replaceWithText(state->toString(), false, false, "\n");
    }
}

void MainComponent::showFirstRunSetup()
{
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto* alert = new juce::AlertWindow(
        "WELCOME TO J3 WORSHIP",
        juce::String::fromUTF8(
            "Configuración inicial rápida\n\n"
            "1. Elegí tu interfaz (ASIO recomendado cuando esté disponible).\n"
            "2. Volvé a RUTEO y elegí PA L/R y CLICK / GUIDE.\n"
            "3. En PLUGINS tocá SCAN PLUGINS.\n\n"
            "Podés omitirlo y configurarlo más tarde. J3 no bloquea el inicio."),
        juce::MessageBoxIconType::InfoIcon);
    alert->addButton("SET UP AUDIO", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("SKIP FOR NOW", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(true,
        juce::ModalCallbackFunction::create([safe](int result)
        {
            if (safe == nullptr || result != 1)
                return;
            safe->tabs_.setCurrentTabIndex(10);
            safe->openAudioSettings();
        }),
        true);
}

void MainComponent::openAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager_, 0, kMaxChannels, 0, kMaxChannels, true, true, true, false);
    selector->setSize(800, 680);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = juce::String::fromUTF8("J3 Worship — Audio / MIDI");
    options.dialogBackgroundColour = juce::Colour(panel);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::openDriverControlPanel()
{
    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        if (device->hasControlPanel())
        {
            device->showControlPanel();
            return;
        }
    }
    showAudioError(juce::String::fromUTF8("El driver actual no expone un panel de control propio. Usá AUDIO / MIDI para cambiar sample rate, buffer o dispositivo."));
}

void MainComponent::showAudioError(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "J3 Worship", message);
}

juce::String MainComponent::buildDeviceInventoryText() const
{
    if (deviceInventory_.isEmpty())
        return juce::String::fromUTF8("No se escanearon drivers todavía.");
    return deviceInventory_;
}

void MainComponent::refreshRoutingControls()
{
    paLeftBox_.clear(juce::dontSendNotification);
    paRightBox_.clear(juce::dontSendNotification);
    clickBox_.clear(juce::dontSendNotification);
    iemOutLeftBox_.clear(juce::dontSendNotification);
    iemOutRightBox_.clear(juce::dontSendNotification);
    clickBox_.addItem("OFF / NOT ROUTED", 1);
    iemOutLeftBox_.addItem("OFF", 1);
    iemOutRightBox_.addItem("OFF", 1);

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        setupDeviceLabel_.setText(juce::String::fromUTF8("No hay interfaz activa. Abrí AUDIO / MIDI para elegir un driver."), juce::dontSendNotification);
        clickBox_.setSelectedId(1, juce::dontSendNotification);
        iemOutLeftBox_.setSelectedId(1, juce::dontSendNotification);
        iemOutRightBox_.setSelectedId(1, juce::dontSendNotification);
        return;
    }

    const auto names = device->getOutputChannelNames();
    const auto activeMask = device->getActiveOutputChannels();
    const int outputCount = names.size();
    std::vector<bool> active(static_cast<std::size_t>(std::max(0, outputCount)), false);
    for (int i = 0; i < outputCount; ++i)
    {
        const bool enabled = activeMask[i];
        active[static_cast<std::size_t>(i)] = enabled;
        auto label = juce::String(i + 1) + juce::String::fromUTF8(" · ") + outputName(*device, i);
        if (!enabled)
            label << juce::String::fromUTF8(" · INACTIVA");
        paLeftBox_.addItem(label, i + 1);
        paRightBox_.addItem(label, i + 1);
        clickBox_.addItem(label, i + 2);
        iemOutLeftBox_.addItem(label, i + 2);
        iemOutRightBox_.addItem(label, i + 2);
    }

    const auto selected = j3::chooseActiveStereoOutputs(active, paLeft_.load(), paRight_.load());
    const int left = selected.left;
    const int right = selected.right;
    paLeft_.store(left);
    paRight_.store(right);
    paLeftBox_.setSelectedId(left >= 0 ? left + 1 : 0, juce::dontSendNotification);
    paRightBox_.setSelectedId(right >= 0 ? right + 1 : 0, juce::dontSendNotification);

    int click = clickOutput_.load();
    if (click >= 0 && click < outputCount
        && active[static_cast<std::size_t>(click)]
        && click != left && click != right)
        clickBox_.setSelectedId(click + 2, juce::dontSendNotification);
    else
    {
        click = -1;
        clickOutput_.store(-1);
        clickBox_.setSelectedId(1, juce::dontSendNotification);
    }

    std::vector<bool> occupied(static_cast<std::size_t>(std::max(0, outputCount)), false);
    if (left >= 0 && left < outputCount) occupied[static_cast<std::size_t>(left)] = true;
    if (right >= 0 && right < outputCount) occupied[static_cast<std::size_t>(right)] = true;
    if (click >= 0 && click < outputCount) occupied[static_cast<std::size_t>(click)] = true;
    for (int m = 0; m < kIemMixes; ++m)
    {
        const int l = iemOutLeft_[m].load(std::memory_order_relaxed);
        const int r = iemOutRight_[m].load(std::memory_order_relaxed);
        const bool valid = l >= 0 && r >= 0 && l < outputCount && r < outputCount && l != r
            && active[static_cast<std::size_t>(l)] && active[static_cast<std::size_t>(r)]
            && !occupied[static_cast<std::size_t>(l)] && !occupied[static_cast<std::size_t>(r)];
        if (valid)
        {
            occupied[static_cast<std::size_t>(l)] = true;
            occupied[static_cast<std::size_t>(r)] = true;
        }
        else
        {
            iemOutLeft_[m].store(-1, std::memory_order_relaxed);
            iemOutRight_[m].store(-1, std::memory_order_relaxed);
        }
    }

    juce::String routeNote;
    if (!selected.valid())
        routeNote = juce::String::fromUTF8("\n⚠ No hay salidas activas. Abrí AUDIO / MIDI.");
    else if (selected.usedFallback)
        routeNote = juce::String::fromUTF8("\n✓ J3 corrigió automáticamente el PA a salidas activas ") + juce::String(left + 1)
            + "/" + juce::String(right + 1);

    setupDeviceLabel_.setText("Device: " + device->getName() + "\nDriver: " + deviceManager_.getCurrentAudioDeviceType()
        + "\nOutputs: " + juce::String(outputCount) + juce::String::fromUTF8(" · Inputs: ") + juce::String(device->getInputChannelNames().size())
        + routeNote,
        juce::dontSendNotification);
    refreshIemUi();
    rebuildMixerBank();
    rebuildIemBank();
}

void MainComponent::applyRoutingFromControls()
{
    const int newLeft = paLeftBox_.getSelectedId() > 0 ? paLeftBox_.getSelectedId() - 1 : 0;
    const int newRight = paRightBox_.getSelectedId() > 0 ? paRightBox_.getSelectedId() - 1 : newLeft;
    const int newClick = clickBox_.getSelectedId() <= 1 ? -1 : clickBox_.getSelectedId() - 2;

    if (!routeIsSafe(newLeft, newRight, newClick))
    {
        clickBox_.setSelectedId(clickOutput_.load() >= 0 ? clickOutput_.load() + 2 : 1, juce::dontSendNotification);
        showAudioError(juce::String::fromUTF8("J3 SAFE ROUTING bloqueó esa selección: CLICK / GUIDE no puede compartir una salida usada por el PA."));
        return;
    }

    for (int m = 0; m < kIemMixes; ++m)
    {
        const int l = iemOutLeft_[m].load(std::memory_order_relaxed);
        const int r = iemOutRight_[m].load(std::memory_order_relaxed);
        if (l < 0 || r < 0) continue;
        if (l == newLeft || l == newRight || r == newLeft || r == newRight || l == newClick || r == newClick)
        {
            refreshRoutingControls();
            showAudioError(juce::String::fromUTF8("J3 SAFE ROUTING bloqueó esa salida porque ya está dedicada a un IEM. Liberá primero ese IEM."));
            return;
        }
    }

    paLeft_.store(newLeft);
    paRight_.store(newRight);
    clickOutput_.store(newClick);
    saveAppState();
    updateDiagnostics();
    updateClickUi();
}

void MainComponent::updateDiagnostics()
{
    juce::String report;
    if (auto* d = deviceManager_.getCurrentAudioDevice())
    {
        const auto sr = d->getCurrentSampleRate();
        const auto bs = d->getCurrentBufferSizeSamples();
        const auto inLatencyMs = sr > 0.0 ? (1000.0 * d->getInputLatencyInSamples() / sr) : 0.0;
        const auto outLatencyMs = sr > 0.0 ? (1000.0 * d->getOutputLatencyInSamples() / sr) : 0.0;
        const auto activeInputs = d->getActiveInputChannels().countNumberOfSetBits();
        const auto activeOutputs = d->getActiveOutputChannels().countNumberOfSetBits();
        const auto xruns = deviceManager_.getXRunCount();
        const auto isAsio = deviceManager_.getCurrentAudioDeviceType().containsIgnoreCase("ASIO");
        const bool safeRouting = routeIsSafe(paLeft_.load(), paRight_.load(), clickOutput_.load());
        const auto outputFaults = outputSafetyEvents_.load(std::memory_order_relaxed);
        const auto recordingDrops = recorder_.overflowCount();
        const bool bufferRisk = bs < 32 || bs > 1024;
        const auto freeDiskBytes = recordingsRoot().getParentDirectory().getBytesFreeOnVolume();
        const bool diskLow = freeDiskBytes >= 0 && freeDiskBytes < (2LL * 1024LL * 1024LL * 1024LL);
        const int midiInputs = juce::MidiInput::getAvailableDevices().size();
        const int midiOutputs = juce::MidiOutput::getAvailableDevices().size();
        bool pluginProtectionActive = false;
        for (int ch = 0; ch < kMaxChannels && !pluginProtectionActive; ++ch)
            for (int slot = 0; slot < kPluginSlots; ++slot)
                if (pluginFaults_[ch][slot].load(std::memory_order_relaxed) > 0)
                {
                    pluginProtectionActive = true;
                    break;
                }

        const bool hardUnsafe = !audioRunning_.load(std::memory_order_acquire) || !safeRouting;
        const bool degraded = xruns > 0 || outputFaults > 0 || recordingDrops > 0
            || recorder_.hasWorkerError() || pluginProtectionActive || bufferRisk || diskLow;

        if (hardUnsafe)
        {
            safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE · CHECK"), juce::dontSendNotification);
            safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff4a151c));
            safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffff7384));
        }
        else if (degraded)
        {
            safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE · WARN"), juce::dontSendNotification);
            safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff4a3612));
            safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffcf67));
        }
        else
        {
            safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE ✓"), juce::dontSendNotification);
            safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff123c2b));
            safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff73e7ad));
        }

        report << juce::String::fromUTF8("✓ AUDIO DEVICE\n    ") << d->getName() << "\n\n";
        report << (isAsio ? juce::String::fromUTF8("✓") : juce::String::fromUTF8("⚠")) << " DRIVER TYPE\n    " << deviceManager_.getCurrentAudioDeviceType();
        if (!isAsio)
            report << "  (ASIO recommended for lowest-latency live use when the interface provides it)";
        report << "\n\n";
        report << juce::String::fromUTF8("✓ I/O\n    ") << activeInputs << juce::String::fromUTF8(" active inputs  ·  ") << activeOutputs << " active outputs\n\n";
        report << juce::String::fromUTF8("✓ SAMPLE RATE\n    ") << juce::String(sr, 0) << " Hz\n\n";
        report << (bufferRisk ? juce::String::fromUTF8("⚠") : juce::String::fromUTF8("✓")) << " BUFFER\n    " << bs << " samples";
        if (bufferRisk)
            report << (bs < 32 ? juce::String::fromUTF8("  ·  demasiado bajo para un show estable") : juce::String::fromUTF8("  ·  demasiado alto para monitoreo en vivo"));
        report << "\n\n";
        report << juce::String::fromUTF8("✓ REPORTED I/O LATENCY\n    Input ") << juce::String(inLatencyMs, 2)
               << juce::String::fromUTF8(" ms  ·  Output ") << juce::String(outLatencyMs, 2) << " ms\n\n";
        report << (xruns == 0 ? juce::String::fromUTF8("✓") : juce::String::fromUTF8("⚠")) << " XRUNS / DROPOUTS\n    " << xruns << "\n\n";
        report << juce::String::fromUTF8("✓ MIDI\n    ") << midiInputs << juce::String::fromUTF8(" input(s) · ") << midiOutputs << " output(s)\n\n";
        report << (diskLow ? juce::String::fromUTF8("⚠") : juce::String::fromUTF8("✓")) << " RECORDING DISK\n    "
               << juce::String(static_cast<double>(std::max<std::int64_t>(0, freeDiskBytes)) / (1024.0 * 1024.0 * 1024.0), 1)
               << " GB libres\n\n";
        report << juce::String::fromUTF8("✓ VST3\n    ") << pluginDescriptions_.size()
               << juce::String::fromUTF8(" plugin(s) reales en catálogo · escaneo de carpetas en segundo plano\n\n");
        report << (pluginProtectionActive ? juce::String::fromUTF8("⚠") : juce::String::fromUTF8("✓")) << " PLUGIN PROTECTION\n    "
               << (pluginProtectionActive ? juce::String::fromUTF8("Uno o más VST3 fueron auto-bypasseados para proteger el audio.")
                                          : "Sin fallos de plugins detectados.")
               << "\n\n";
        report << (routeIsSafe(paLeft_.load(), paRight_.load(), clickOutput_.load()) ? juce::String::fromUTF8("✓") : juce::String::fromUTF8("✕"))
               << " SAFE ROUTING\n    PA L " << (paLeft_.load() + 1) << juce::String::fromUTF8("  ·  PA R ") << (paRight_.load() + 1)
               << juce::String::fromUTF8("  ·  CLICK ") << (clickOutput_.load() < 0 ? juce::String("OFF") : juce::String(clickOutput_.load() + 1)) << "\n\n";

        bool allRoutedInsertsBlocked = false;
        if (dawWorkspace_.isPlaying())
        {
            if (dawOutputUnavailable_.load(std::memory_order_relaxed))
                report << juce::String::fromUTF8("✕ DAW PLAYBACK\n    NO HAY SALIDA DE AUDIO ACTIVA. Abrí AUDIO / MIDI y activá al menos una salida.\n\n");
            else if (dawOutputFallbackActive_.load(std::memory_order_relaxed))
                report << juce::String::fromUTF8("⚠ DAW PLAYBACK\n    La salida PA guardada no está activa. J3 está usando automáticamente una salida activa de respaldo.\n\n");
            else
                report << juce::String::fromUTF8("✓ DAW PLAYBACK\n    Mixer → PA L/R activo.\n\n");

            int routedInsertCount = 0;
            int blockedInsertCount = 0;
            juce::StringArray blockedReasons;
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                const auto routedName = dawWorkspace_.mixerInsertName(ch);
                if (routedName.isEmpty())
                    continue;

                ++routedInsertCount;
                juce::String reason;
                if (channelMute_[ch].load(std::memory_order_relaxed))
                    reason = "MUTE";
                else if (channelGain_[ch].load(std::memory_order_relaxed) <= 1.0e-5f)
                    reason = "FADER -INF";
                else
                {
                    const int dca = channelDca_[ch].load(std::memory_order_relaxed);
                    const int bus = channelBus_[ch].load(std::memory_order_relaxed);
                    if (dca >= 0 && dca < kDcas && dcaMute_[dca].load(std::memory_order_relaxed))
                        reason = "DCA " + juce::String(dca + 1) + " MUTED";
                    else if (bus >= 0 && bus < kBuses && busMute_[bus].load(std::memory_order_relaxed))
                        reason = "BUS " + juce::String(bus + 1) + " MUTED";
                }

                if (reason.isNotEmpty())
                {
                    ++blockedInsertCount;
                    blockedReasons.add("Insert " + juce::String(ch + 1) + " · " + routedName + " · " + reason);
                }
            }

            if (masterGain_.load(std::memory_order_relaxed) <= 1.0e-5f)
            {
                report << juce::String::fromUTF8("✕ MASTER\n    El MASTER está en silencio. Subí el master para escuchar el proyecto.\n\n");
                allRoutedInsertsBlocked = routedInsertCount > 0;
            }
            else if (!blockedReasons.isEmpty())
            {
                allRoutedInsertsBlocked = routedInsertCount > 0 && blockedInsertCount == routedInsertCount;
                report << (allRoutedInsertsBlocked ? juce::String::fromUTF8("✕") : juce::String::fromUTF8("⚠"))
                       << " MUTED ROUTES\n    " << blockedReasons.joinIntoString("\n    ") << "\n\n";
            }
        }
        else
        {
            report << juce::String::fromUTF8("✓ DAW PLAYBACK\n    Listo para reproducir.\n\n");
        }
        report << (liveMonitorEnabled_.load() ? juce::String::fromUTF8("⚠ LIVE INPUT MONITORING: ON") : juce::String::fromUTF8("✓ LIVE INPUT MONITORING: OFF (safe default)")) << "\n\n";
        if (lastAudioError_.isNotEmpty())
            report << juce::String::fromUTF8("⚠ LAST AUDIO ERROR\n    ") << lastAudioError_ << "\n\n";
        report << "AVAILABLE AUDIO DRIVERS\n" << buildDeviceInventoryText();

        if (dawWorkspace_.isPlaying() && dawOutputUnavailable_.load(std::memory_order_relaxed))
        {
            statusLabel_.setText(juce::String::fromUTF8("NO HAY SALIDA DE AUDIO ACTIVA · PLAY no puede producir sonido"), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colour(danger));
        }
        else if (dawWorkspace_.isPlaying() && allRoutedInsertsBlocked)
        {
            statusLabel_.setText(juce::String::fromUTF8("PLAYBACK SILENCIADO · revisá MUTE / FADER / BUS / DCA en el mixer"), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colour(danger));
        }
        else if (dawWorkspace_.isPlaying() && dawOutputFallbackActive_.load(std::memory_order_relaxed))
        {
            statusLabel_.setText(juce::String::fromUTF8("PLAYBACK · salida guardada no disponible · usando salida activa de respaldo"), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
        }
        else if (dawWorkspace_.isPlaying())
        {
            statusLabel_.setText(juce::String::fromUTF8("PLAYBACK · Mixer → PA L/R · ") + d->getName(), juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colour(good));
        }
        else
        {
            statusLabel_.setText("Audio: " + d->getName() + juce::String::fromUTF8(" · ") + deviceManager_.getCurrentAudioDeviceType()
                + juce::String::fromUTF8(" · ") + juce::String(sr / 1000.0, 1) + juce::String::fromUTF8(" kHz · ") + juce::String(bs) + " smp", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, xruns == 0 ? juce::Colour(good) : juce::Colour(warning));
        }
    }
    else
    {
        report = juce::String::fromUTF8("✕ AUDIO DEVICE\n    No hay dispositivo seleccionado.\n\nAbrí AUDIO / MIDI y elegí un driver. J3 Worship soporta cualquier interfaz que Windows exponga a JUCE mediante ASIO o WASAPI.\n\nAVAILABLE AUDIO DRIVERS\n") + buildDeviceInventoryText();
        if (lastAudioError_.isNotEmpty())
            report << "\n\nLAST ERROR\n" << lastAudioError_;
        statusLabel_.setText("Audio: sin dispositivo", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
        safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE · NO AUDIO"), juce::dontSendNotification);
        safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff4a151c));
        safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffff7384));
    }
    diagnosticsLabel_.setText(report, juce::dontSendNotification);
}

void MainComponent::setLiveSection(const juce::String& name, j3::SectionKind kind, int bars)
{
    j3::Section section { name.toStdString(), kind, bars };
    if (!liveStarted_)
    {
        liveEngine_.setTempo(bpmSlider_.getValue(), clickGenerator_.numerator());
        liveEngine_.start(section);
        liveStarted_ = true;
        transportRunning_.store(true, std::memory_order_release);
        clickGenerator_.setEnabled(true);
        clickGenerator_.reset();
    }
    else if (kind == j3::SectionKind::FreePad)
    {
        liveEngine_.enterFreePad();
    }
    else
    {
        liveEngine_.request(section, j3::Quantize::Bar);
    }
    refreshLiveLabels();
    refreshDashboard();
}

void MainComponent::stopLiveTransport()
{
    liveEngine_.stop();
    liveStarted_ = false;
    transportRunning_.store(false, std::memory_order_release);
    clickAudible_.store(false, std::memory_order_release);
    clickEnabledButton_.setToggleState(false, juce::dontSendNotification);
    clickGenerator_.setEnabled(false);
    clickGenerator_.reset();
    refreshLiveLabels();
    updateClickUi();
    refreshDashboard();
}

void MainComponent::panicStopAll()
{
    liveMonitorEnabled_.store(false, std::memory_order_release);
    liveMonitorButton_.setToggleState(false, juce::dontSendNotification);

    ambientPad_.setEnabled(false);
    padEnabledButton_.setToggleState(false, juce::dontSendNotification);

    stopLiveTransport();
    dawWorkspace_.emergencyStop();

    statusLabel_.setText(juce::String::fromUTF8("STOP ALL · salidas de show silenciadas · grabación preservada"), juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(warning));
    safetyLabel_.setText(juce::String::fromUTF8("LIVE SAFE · MUTED"), juce::dontSendNotification);
    safetyLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff402a14));
    safetyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffc247));
    refreshPadUi();
    refreshDashboard();
}

void MainComponent::refreshLiveLabels()
{
    nowLabel_.setText("NOW: " + juce::String(liveEngine_.now()), juce::dontSendNotification);
    nextLabel_.setText("NEXT: " + juce::String(liveEngine_.next()), juce::dontSendNotification);
    refreshDashboard();
}

bool MainComponent::routeIsSafe(int paLeft, int paRight, int clickOutput) const noexcept
{
    return clickOutput < 0 || (clickOutput != paLeft && clickOutput != paRight);
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    for (int o = 0; o < numOutputChannels; ++o)
        if (outputChannelData[o] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[o], numSamples);

    dawWorkspace_.captureInputBlock(inputChannelData, numInputChannels, numSamples);

    const int left = paLeft_.load(std::memory_order_relaxed);
    const int right = paRight_.load(std::memory_order_relaxed);
    const int click = clickOutput_.load(std::memory_order_relaxed);
    const bool safePa = left >= 0 && right >= 0 && left < numOutputChannels && right < numOutputChannels
        && outputChannelData[left] != nullptr && outputChannelData[right] != nullptr;

    int playbackLeft = left;
    int playbackRight = right;
    bool playbackRouteReady = safePa;
    if (!playbackRouteReady)
    {
        int first = -1;
        int second = -1;
        for (int o = 0; o < numOutputChannels; ++o)
        {
            if (outputChannelData[o] == nullptr || o == click)
                continue;
            if (first < 0) first = o;
            else { second = o; break; }
        }
        if (first >= 0)
        {
            playbackLeft = first;
            playbackRight = second >= 0 ? second : first;
            playbackRouteReady = true;
        }
    }

    const bool monitoring = liveMonitorEnabled_.load(std::memory_order_acquire);
    const bool busAvailable = busScratch_.getNumChannels() >= kBuses * 2 && busScratch_.getNumSamples() >= numSamples;

    if (monitoring)
    {
        if (busAvailable)
            busScratch_.clear(0, numSamples);

        auto* outL = safePa ? outputChannelData[left] : nullptr;
        auto* outR = safePa ? outputChannelData[right] : nullptr;
        const bool monoPa = safePa && left == right;
        const auto master = masterGain_.load(std::memory_order_relaxed);
        const int channels = std::min(numInputChannels, kMaxChannels);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* in = inputChannelData[ch];
            if (in == nullptr)
                continue;

            const auto* processedInput = processPluginChain(ch, in, numSamples);
            applyDspParameters(ch);
            const bool muted = channelMute_[ch].load(std::memory_order_relaxed);
            const int dca = channelDca_[ch].load(std::memory_order_relaxed);
            const bool dcaMuted = dca >= 0 && dca < kDcas && dcaMute_[dca].load(std::memory_order_relaxed);
            const float dcaGain = dca >= 0 && dca < kDcas ? dcaGain_[dca].load(std::memory_order_relaxed) : 1.0f;
            const float channelGain = channelGain_[ch].load(std::memory_order_relaxed);
            const auto pan = juce::jlimit(-1.0f, 1.0f, channelPan_[ch].load(std::memory_order_relaxed));
            const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float panL = std::cos(angle);
            const float panR = std::sin(angle);
            const int bus = channelBus_[ch].load(std::memory_order_relaxed);
            float peak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float x = channelDsp_[ch].process(processedInput[i]);
                peak = std::max(peak, std::abs(x));
                if (muted)
                    continue;

                // IEM sends are post-channel DSP but pre-fader/DCA, so monitor balances stay independent.
                for (int m = 0; m < kIemMixes; ++m)
                {
                    if (iemMute_[m].load(std::memory_order_relaxed))
                        continue;
                    const int il = iemOutLeft_[m].load(std::memory_order_relaxed);
                    const int ir = iemOutRight_[m].load(std::memory_order_relaxed);
                    if (il < 0 || ir < 0 || il >= numOutputChannels || ir >= numOutputChannels || il == ir)
                        continue;
                    if (il == left || il == right || ir == left || ir == right || il == click || ir == click)
                        continue;
                    auto* iemL = outputChannelData[il];
                    auto* iemR = outputChannelData[ir];
                    if (iemL == nullptr || iemR == nullptr)
                        continue;
                    const float send = iemSendGain_[m][ch].load(std::memory_order_relaxed)
                        * iemMaster_[m].load(std::memory_order_relaxed);
                    if (send <= 1.0e-8f)
                        continue;
                    const float iemPan = juce::jlimit(-1.0f, 1.0f, iemSendPan_[m][ch].load(std::memory_order_relaxed));
                    const float iemAngle = (iemPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                    iemL[i] += x * send * std::cos(iemAngle);
                    iemR[i] += x * send * std::sin(iemAngle);
                }

                if (!safePa || dcaMuted)
                    continue;

                const float post = x * channelGain * dcaGain;
                if (bus >= 0 && bus < kBuses && busAvailable)
                {
                    busScratch_.getWritePointer(bus * 2)[i] += post * panL;
                    busScratch_.getWritePointer(bus * 2 + 1)[i] += post * panR;
                }
                else if (monoPa)
                {
                    outL[i] += post * master;
                }
                else
                {
                    outL[i] += post * panL * master;
                    outR[i] += post * panR * master;
                }
            }

            auto previous = channelMeter_[ch].load(std::memory_order_relaxed);
            while (peak > previous && !channelMeter_[ch].compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
        }

        if (safePa && busAvailable)
        {
            for (int bus = 0; bus < kBuses; ++bus)
            {
                if (busMute_[bus].load(std::memory_order_relaxed))
                    continue;
                const float gain = busGain_[bus].load(std::memory_order_relaxed) * master;
                const auto* busL = busScratch_.getReadPointer(bus * 2);
                const auto* busR = busScratch_.getReadPointer(bus * 2 + 1);
                for (int i = 0; i < numSamples; ++i)
                {
                    if (monoPa)
                        outL[i] += (busL[i] + busR[i]) * 0.70710678f * gain;
                    else
                    {
                        outL[i] += busL[i] * gain;
                        outR[i] += busR[i] * gain;
                    }
                }
            }
        }

    }

    const bool padAudible = ambientPad_.enabled() && padToPa_.load(std::memory_order_acquire) && safePa;
    if (padAudible)
        ambientPad_.process(outputChannelData[left], outputChannelData[right], numSamples);

    const bool dawRunning = dawWorkspace_.isPlaying();
    const bool dawScratchReady = dawMixerScratch_.getNumChannels() >= kMaxChannels * 2
        && dawMixerScratch_.getNumSamples() >= numSamples;
    const bool dawAudible = playbackRouteReady && dawRunning && dawScratchReady;
    dawOutputFallbackActive_.store(dawRunning && playbackRouteReady && !safePa, std::memory_order_relaxed);
    dawOutputUnavailable_.store(dawRunning && (!playbackRouteReady || !dawScratchReady), std::memory_order_relaxed);
    if (dawRunning && dawScratchReady)
    {
        dawMixerScratch_.clear(0, numSamples);
        dawWorkspace_.renderToMixer(dawMixerScratch_, kMaxChannels, numSamples);

        if (dawAudible)
        {
            if (busAvailable)
                busScratch_.clear(0, numSamples);

            auto* outL = outputChannelData[playbackLeft];
            auto* outR = outputChannelData[playbackRight];
            const bool monoPa = playbackLeft == playbackRight;
            const float master = masterGain_.load(std::memory_order_relaxed);

            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                auto* insertL = dawMixerScratch_.getWritePointer(ch * 2);
                auto* insertR = dawMixerScratch_.getWritePointer(ch * 2 + 1);

                // Native J3 EQ is the fixed PRE-FX stage, like a channel EQ in a
                // hardware/FL-style mixer. VST3 effects then run in slots 1..8.
                applyDspParameters(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    insertL[i] = insertEqLeft_[ch].process(insertL[i]);
                    insertR[i] = insertEqRight_[ch].process(insertR[i]);
                }
                processPluginChainStereo(ch, insertL, insertR, numSamples);

                const bool muted = channelMute_[ch].load(std::memory_order_relaxed);
                const int dca = channelDca_[ch].load(std::memory_order_relaxed);
                const bool dcaMuted = dca >= 0 && dca < kDcas && dcaMute_[dca].load(std::memory_order_relaxed);
                const float dcaGain = dca >= 0 && dca < kDcas ? dcaGain_[dca].load(std::memory_order_relaxed) : 1.0f;
                const float insertGain = channelGain_[ch].load(std::memory_order_relaxed) * dcaGain;
                const float pan = juce::jlimit(-1.0f, 1.0f, channelPan_[ch].load(std::memory_order_relaxed));
                const int bus = channelBus_[ch].load(std::memory_order_relaxed);
                float peak = 0.0f;

                for (int i = 0; i < numSamples; ++i)
                {
                    float l = insertL[i];
                    float r = insertR[i];
                    peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
                    if (muted || dcaMuted)
                        continue;

                    if (pan < 0.0f) r *= 1.0f + pan;
                    else if (pan > 0.0f) l *= 1.0f - pan;
                    l *= insertGain;
                    r *= insertGain;

                    if (bus >= 0 && bus < kBuses && busAvailable)
                    {
                        busScratch_.getWritePointer(bus * 2)[i] += l;
                        busScratch_.getWritePointer(bus * 2 + 1)[i] += r;
                    }
                    else if (monoPa)
                    {
                        outL[i] += (l + r) * 0.70710678f * master;
                    }
                    else
                    {
                        outL[i] += l * master;
                        outR[i] += r * master;
                    }
                }

                auto previous = channelMeter_[ch].load(std::memory_order_relaxed);
                while (peak > previous
                    && !channelMeter_[ch].compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
            }

            if (busAvailable)
            {
                for (int bus = 0; bus < kBuses; ++bus)
                {
                    if (busMute_[bus].load(std::memory_order_relaxed))
                        continue;
                    const float gain = busGain_[bus].load(std::memory_order_relaxed) * master;
                    const auto* busL = busScratch_.getReadPointer(bus * 2);
                    const auto* busR = busScratch_.getReadPointer(bus * 2 + 1);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        if (monoPa)
                            outL[i] += (busL[i] + busR[i]) * 0.70710678f * gain;
                        else
                        {
                            outL[i] += busL[i] * gain;
                            outR[i] += busR[i] * gain;
                        }
                    }
                }
            }
        }
    }

    // Soft output protection is applied after live inputs, pads and DAW playback have been summed.
    // Only exclude CLICK when it actually owns a dedicated safe output; a stale conflicting
    // click route must never remove protection from a PA output.
    const bool dedicatedClickOutput = click >= 0 && click < numOutputChannels
        && outputChannelData[click] != nullptr
        && routeIsSafe(dawRunning ? playbackLeft : left, dawRunning ? playbackRight : right, click);
    if (monitoring || padAudible || dawAudible)
    {
        for (int o = 0; o < numOutputChannels; ++o)
        {
            auto* out = outputChannelData[o];
            if (out == nullptr || (dedicatedClickOutput && o == click))
                continue;
            for (int i = 0; i < numSamples; ++i)
            {
                if (!std::isfinite(out[i]))
                {
                    out[i] = 0.0f;
                    outputSafetyEvents_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    out[i] = std::tanh(out[i]);
                }
            }
        }
    }

    if (transportRunning_.load(std::memory_order_acquire))
    {
        float* clickOut = nullptr;
        if (clickAudible_.load(std::memory_order_relaxed) && click >= 0 && click < numOutputChannels
            && routeIsSafe(left, right, click))
            clickOut = outputChannelData[click];
        const int beats = clickGenerator_.process(clickOut, numSamples);
        if (beats > 0)
            pendingBeatEvents_.fetch_add(beats, std::memory_order_relaxed);
    }

    if (recordingEnabled_.load(std::memory_order_acquire))
    {
        const int wanted = recordChannelCount_.load(std::memory_order_relaxed);
        if (wanted > 0 && wanted <= static_cast<int>(j3::kRecordMaxChannels))
        {
            int offset = 0;
            while (offset < numSamples)
            {
                const int frames = std::min<int>(static_cast<int>(j3::kRecordMaxFrames), numSamples - offset);
                std::array<const float*, j3::kRecordMaxChannels> ptrs {};
                bool valid = true;
                for (int rec = 0; rec < wanted; ++rec)
                {
                    const int input = recordInputIndices_[static_cast<std::size_t>(rec)];
                    if (input < 0 || input >= numInputChannels || inputChannelData[input] == nullptr)
                    {
                        valid = false;
                        break;
                    }
                    ptrs[static_cast<std::size_t>(rec)] = inputChannelData[input] + offset;
                }
                if (valid)
                    recorder_.submit(ptrs.data(), static_cast<std::size_t>(wanted), static_cast<std::size_t>(frames));
                offset += frames;
            }
        }
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const auto sr = device->getCurrentSampleRate();
    sampleRate_.store(sr, std::memory_order_release);
    bufferSize_.store(device->getCurrentBufferSizeSamples(), std::memory_order_release);
    const int preparedBlock = std::max(2048, device->getCurrentBufferSizeSamples());
    busScratch_.setSize(kBuses * 2, preparedBlock, false, true, false);
    busScratch_.clear();
    pluginScratch_.setSize(2, preparedBlock, false, true, false);
    pluginScratch_.clear();
    pluginGuardScratch_.setSize(2, preparedBlock, false, true, false);
    pluginGuardScratch_.clear();
    dawMixerScratch_.setSize(kMaxChannels * 2, preparedBlock, false, true, false);
    dawMixerScratch_.clear();
    outputSafetyEvents_.store(0, std::memory_order_release);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        for (int slot = 0; slot < kPluginSlots; ++slot)
        {
            if (auto plugin = channelPlugins_[ch][slot].load(std::memory_order_acquire))
            {
                try
                {
                    plugin->releaseResources();
                    plugin->setRateAndBufferSizeDetails(sr, device->getCurrentBufferSizeSamples());
                    plugin->prepareToPlay(sr, device->getCurrentBufferSizeSamples());
                }
                catch (...)
                {
                    pluginBypass_[ch][slot].store(true, std::memory_order_release);
                }
            }
        }
    }
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        channelDsp_[ch].prepare(sr);
        insertEqLeft_[ch].prepare(sr);
        insertEqRight_[ch].prepare(sr);
        dspAppliedRevision_[ch] = 0;
    }
    clickGenerator_.prepare(sr);
    ambientPad_.prepare(sr);
    dawWorkspace_.prepare(sr, preparedBlock);
    clickGenerator_.setTempo(bpmSlider_.getValue());
    clickGenerator_.setEnabled(transportRunning_.load(std::memory_order_acquire));
    audioRunning_.store(true, std::memory_order_release);
    reconnectAttempts_ = 0;
    lastAudioError_.clear();

    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->refreshRoutingControls();
            safe->updateDiagnostics();
            safe->saveAudioState();
        }
    });
}

void MainComponent::audioDeviceStopped()
{
    audioRunning_.store(false, std::memory_order_release);
    liveMonitorEnabled_.store(false, std::memory_order_release);
    recordingEnabled_.store(false, std::memory_order_release);
    scheduleReconnect();
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr)
        {
            safe->liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
            safe->stopRecordingAfterDeviceLoss(juce::String::fromUTF8("La interfaz de audio se detuvo. La grabación fue finalizada de forma segura."));
            safe->updateDiagnostics();
        }
    });
}

void MainComponent::audioDeviceError(const juce::String& errorMessage)
{
    recordingEnabled_.store(false, std::memory_order_release);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this), errorMessage]
    {
        if (safe == nullptr)
            return;
        safe->lastAudioError_ = errorMessage;
        safe->liveMonitorEnabled_.store(false, std::memory_order_release);
        safe->liveMonitorButton_.setToggleState(false, juce::dontSendNotification);
        safe->stopRecordingAfterDeviceLoss("Error de interfaz: " + errorMessage);
        safe->statusLabel_.setText(juce::String::fromUTF8("Audio error · intentando recuperar"), juce::dontSendNotification);
        safe->statusLabel_.setColour(juce::Label::textColourId, juce::Colour(danger));
        safe->scheduleReconnect();
        safe->updateDiagnostics();
    });
}

void MainComponent::scheduleReconnect()
{
    audioRunning_.store(false, std::memory_order_release);
    lastReconnectAttemptMs_ = 0;
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;
    saveAudioState();
    refreshRoutingControls();
    updateDiagnostics();
}

void MainComponent::timerCallback()
{
    for (auto& strip : strips_)
        if (strip) strip->timerTick();

    const int beats = pendingBeatEvents_.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < beats; ++i)
        if (liveStarted_) liveEngine_.tickBeat();
    if (beats > 0 && liveStarted_)
        refreshLiveLabels();

    static int ticks = 0;
    ++ticks;
    if (ticks % 30 == 0)
    {
        if (recordingEnabled_.load(std::memory_order_acquire) && recorder_.hasWorkerError())
            stopRecordingAfterDeviceLoss(juce::String::fromUTF8("Grabación detenida por error de escritura en disco. El audio LIVE continúa protegido."));

        updateDiagnostics();
        updateClickUi();
        updateRecordingUi();
        refreshDashboard();
    }
    if (ticks % 150 == 0)
        saveAppState();

    // Keep the update badge truthful even if a new release is published while
    // J3 Worship stays open for hours. Re-check every 2 minutes when no update
    // is already pending. The request itself bypasses caches in UpdateService.
    if (ticks % 3600 == 0
        && !updateBusy_.load(std::memory_order_acquire)
        && !availableUpdate_.has_value())
        checkForUpdatesAsync();

    if (!audioRunning_.load(std::memory_order_acquire) && !shuttingDown_.load(std::memory_order_acquire))
    {
        const auto now = static_cast<std::uint64_t>(juce::Time::getMillisecondCounterHiRes());
        const auto waitMs = static_cast<std::uint64_t>(std::min(10000, 2000 + reconnectAttempts_ * 1500));
        if (lastReconnectAttemptMs_ == 0 || now - lastReconnectAttemptMs_ >= waitMs)
        {
            lastReconnectAttemptMs_ = now;
            ++reconnectAttempts_;
            deviceManager_.restartLastAudioDevice();
        }
    }
}
