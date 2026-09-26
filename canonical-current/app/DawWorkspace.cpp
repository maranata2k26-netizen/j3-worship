#include "DawWorkspace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

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

float equalPowerFade(double normalized) noexcept
{
    const auto t = juce::jlimit(0.0, 1.0, normalized);
    return static_cast<float>(std::sin(t * juce::MathConstants<double>::halfPi));
}

std::vector<int> detectTransientSamples(const juce::AudioBuffer<float>& audio, double sampleRate)
{
    std::vector<int> result;
    const int samples = audio.getNumSamples();
    const int channels = audio.getNumChannels();
    if (samples < 128 || channels <= 0 || sampleRate <= 0.0)
        return result;

    const int block = juce::jlimit(64, 1024, static_cast<int>(std::llround(sampleRate * 0.006)));
    const int blockCount = (samples + block - 1) / block;
    if (blockCount < 3)
        return result;

    std::vector<float> energy(static_cast<std::size_t>(blockCount), 0.0f);
    std::vector<float> flux(static_cast<std::size_t>(blockCount), 0.0f);

    for (int b = 0; b < blockCount; ++b)
    {
        const int start = b * block;
        const int end = std::min(samples, start + block);
        double sum = 0.0;
        std::int64_t count = 0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* src = audio.getReadPointer(ch);
            for (int i = start; i < end; ++i)
            {
                const double v = src[i];
                sum += v * v;
                ++count;
            }
        }
        energy[static_cast<std::size_t>(b)] = count > 0
            ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
    }

    double fluxSum = 0.0;
    for (int b = 1; b < blockCount; ++b)
    {
        const float current = energy[static_cast<std::size_t>(b)];
        const float previous = energy[static_cast<std::size_t>(b - 1)];
        const float value = std::max(0.0f, current - previous * 0.82f);
        flux[static_cast<std::size_t>(b)] = value;
        fluxSum += value;
    }

    const float meanFlux = static_cast<float>(fluxSum / std::max(1, blockCount - 1));
    const float threshold = std::max(1.0e-5f, meanFlux * 2.35f);
    const int minGap = std::max(block, static_cast<int>(std::llround(sampleRate * 0.055)));
    int last = -minGap;

    for (int b = 1; b + 1 < blockCount; ++b)
    {
        const float value = flux[static_cast<std::size_t>(b)];
        if (value < threshold
            || value < flux[static_cast<std::size_t>(b - 1)]
            || value < flux[static_cast<std::size_t>(b + 1)])
            continue;

        const int sample = b * block;
        if (sample - last < minGap)
        {
            if (!result.empty())
            {
                const int oldBlock = result.back() / block;
                if (value > flux[static_cast<std::size_t>(juce::jlimit(0, blockCount - 1, oldBlock))])
                {
                    result.back() = sample;
                    last = sample;
                }
            }
            continue;
        }

        result.push_back(sample);
        last = sample;
    }

    return result;
}

std::optional<double> estimateTempoFromTransients(const std::vector<int>& transientSamples, double sampleRate)
{
    if (transientSamples.size() < 3 || sampleRate <= 0.0)
        return std::nullopt;

    std::vector<double> bpms;
    bpms.reserve(transientSamples.size() - 1);
    for (std::size_t i = 1; i < transientSamples.size(); ++i)
    {
        const double seconds = static_cast<double>(transientSamples[i] - transientSamples[i - 1]) / sampleRate;
        if (seconds < 0.12 || seconds > 2.5)
            continue;

        double candidate = 60.0 / seconds;
        while (candidate < 70.0) candidate *= 2.0;
        while (candidate > 180.0) candidate *= 0.5;
        if (candidate >= 70.0 && candidate <= 180.0)
            bpms.push_back(candidate);
    }

    if (bpms.size() < 2)
        return std::nullopt;

    std::sort(bpms.begin(), bpms.end());
    const auto mid = bpms.size() / 2;
    return bpms.size() % 2 == 0 ? (bpms[mid - 1] + bpms[mid]) * 0.5 : bpms[mid];
}

std::optional<juce::AudioBuffer<float>> stretchWsola(const juce::AudioBuffer<float>& input,
                                                      double factor,
                                                      double sampleRate)
{
    const int channels = input.getNumChannels();
    const int inputSamples = input.getNumSamples();
    if (channels <= 0 || inputSamples < 128 || sampleRate <= 0.0
        || factor < 0.5 || factor > 2.0)
        return std::nullopt;

    const auto outputSamples64 = static_cast<std::int64_t>(std::llround(inputSamples * factor));
    if (outputSamples64 <= 0 || outputSamples64 > std::numeric_limits<int>::max())
        return std::nullopt;
    const int outputSamples = static_cast<int>(outputSamples64);

    if (std::abs(factor - 1.0) < 1.0e-4)
    {
        juce::AudioBuffer<float> copy(channels, inputSamples);
        for (int ch = 0; ch < channels; ++ch)
            copy.copyFrom(ch, 0, input, ch, 0, inputSamples);
        return copy;
    }

    int window = juce::jlimit(256, 4096, static_cast<int>(std::llround(sampleRate * 0.040)));
    window = std::min(window, inputSamples);
    if ((window & 1) != 0) --window;
    if (window < 128)
        return std::nullopt;

    const int synthesisHop = std::max(32, window / 2);
    const int overlap = window - synthesisHop;
    const int searchRadius = std::max(16, window / 4);
    const double analysisHop = static_cast<double>(synthesisHop) / factor;
    const int maxInputStart = std::max(0, inputSamples - window);

    juce::AudioBuffer<float> output(channels, outputSamples);
    output.clear();
    const int initialCopy = std::min(window, outputSamples);
    for (int ch = 0; ch < channels; ++ch)
        output.copyFrom(ch, 0, input, ch, 0, initialCopy);

    auto monoAt = [&](int sample)
    {
        sample = juce::jlimit(0, inputSamples - 1, sample);
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            sum += input.getSample(ch, sample);
        return static_cast<float>(sum / channels);
    };

    int previousAnalysis = 0;
    for (int outputStart = synthesisHop; outputStart < outputSamples; outputStart += synthesisHop)
    {
        const int expected = juce::jlimit(0, maxInputStart,
            static_cast<int>(std::llround(previousAnalysis + analysisHop)));
        const int minCandidate = juce::jlimit(0, maxInputStart, expected - searchRadius);
        const int maxCandidate = juce::jlimit(0, maxInputStart, expected + searchRadius);

        auto correlation = [&](int candidate)
        {
            double dot = 0.0, aa = 0.0, bb = 0.0;
            const int previousTail = std::min(inputSamples - overlap, previousAnalysis + synthesisHop);
            const int stride = overlap > 1024 ? 4 : (overlap > 512 ? 2 : 1);
            for (int n = 0; n < overlap; n += stride)
            {
                const double a = monoAt(previousTail + n);
                const double b = monoAt(candidate + n);
                dot += a * b;
                aa += a * a;
                bb += b * b;
            }
            const double denom = std::sqrt(aa * bb);
            return denom > 1.0e-10 ? dot / denom : -1.0;
        };

        int best = expected;
        double bestScore = -2.0;
        const int searchStep = std::max(1, searchRadius / 64);
        for (int candidate = minCandidate; candidate <= maxCandidate; candidate += searchStep)
        {
            const double score = correlation(candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }

        const int refineStart = std::max(minCandidate, best - searchStep);
        const int refineEnd = std::min(maxCandidate, best + searchStep);
        for (int candidate = refineStart; candidate <= refineEnd; ++candidate)
        {
            const double score = correlation(candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }

        const int availableInput = std::min(window, inputSamples - best);
        const int availableOutput = std::min(window, outputSamples - outputStart);
        const int frameSamples = std::min(availableInput, availableOutput);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* dst = output.getWritePointer(ch);
            const auto* src = input.getReadPointer(ch);
            const int blendSamples = std::min(overlap, frameSamples);
            for (int n = 0; n < blendSamples; ++n)
            {
                const float w = blendSamples > 1
                    ? static_cast<float>(n) / static_cast<float>(blendSamples - 1) : 1.0f;
                dst[outputStart + n] = dst[outputStart + n] * (1.0f - w) + src[best + n] * w;
            }
            if (frameSamples > blendSamples)
                std::copy(src + best + blendSamples, src + best + frameSamples,
                          dst + outputStart + blendSamples);
        }

        previousAnalysis = best;
    }

    return output;
}

bool writeFloatWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const int channels = audio.getNumChannels();
    const int samples = audio.getNumSamples();
    if (channels <= 0 || samples <= 0 || sampleRate <= 0.0)
        return false;

    const std::int64_t dataBytes64 = static_cast<std::int64_t>(channels)
                                   * static_cast<std::int64_t>(samples)
                                   * static_cast<std::int64_t>(sizeof(float));
    if (dataBytes64 <= 0 || dataBytes64 > std::numeric_limits<int>::max() - 44)
        return false;

    file.getParentDirectory().createDirectory();
    juce::FileOutputStream output(file);
    if (!output.openedOk())
        return false;
    output.setPosition(0);
    output.truncate();

    const int sampleRateHz = std::max(1, static_cast<int>(std::llround(sampleRate)));
    const int dataBytes = static_cast<int>(dataBytes64);
    const short channelCount = static_cast<short>(channels);
    const short bitsPerSample = 32;
    const short blockAlign = static_cast<short>(channels * static_cast<int>(sizeof(float)));
    const int byteRate = sampleRateHz * static_cast<int>(blockAlign);

    output.write("RIFF", 4);
    output.writeInt(36 + dataBytes);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    output.writeInt(16);
    output.writeShort(3); // IEEE 32-bit float
    output.writeShort(channelCount);
    output.writeInt(sampleRateHz);
    output.writeInt(byteRate);
    output.writeShort(blockAlign);
    output.writeShort(bitsPerSample);
    output.write("data", 4);
    output.writeInt(dataBytes);

    for (int sample = 0; sample < samples; ++sample)
        for (int ch = 0; ch < channels; ++ch)
        {
            const float value = audio.getSample(ch, sample);
            if (!output.write(&value, sizeof(value)))
                return false;
        }

    output.flush();
    return output.getStatus().wasOk();
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

class AudioClipRoutingEditor final : public juce::Component
{
public:
    AudioClipRoutingEditor(const juce::String& clipName,
                           const juce::String& filePath,
                           int currentInsert,
                           float gainDb,
                           float pan,
                           double startBeat,
                           double lengthBeats,
                           double durationSeconds,
                           double fadeInBeats,
                           double fadeOutBeats,
                           bool reversed,
                           std::function<void(int)> onRoute,
                           std::function<void(float)> onGain,
                           std::function<void(float)> onPan,
                           std::function<void(double)> onFadeIn,
                           std::function<void(double)> onFadeOut,
                           std::function<void(bool)> onReverse,
                           std::function<void()> onNormalize,
                           std::function<void()> onReplace,
                           std::function<void()> onReveal,
                           std::function<void(int)> onOpenMixer,
                           std::function<void(int)> onOpenFx)
        : onRoute_(std::move(onRoute)),
          onGain_(std::move(onGain)),
          onPan_(std::move(onPan)),
          onFadeIn_(std::move(onFadeIn)),
          onFadeOut_(std::move(onFadeOut)),
          onReverse_(std::move(onReverse)),
          onNormalize_(std::move(onNormalize)),
          onReplace_(std::move(onReplace)),
          onReveal_(std::move(onReveal)),
          onOpenMixer_(std::move(onOpenMixer)),
          onOpenFx_(std::move(onOpenFx))
    {
        title_.setText("AUDIO CLIP", juce::dontSendNotification);
        title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        title_.setColour(juce::Label::textColourId, juce::Colour(kText));
        addAndMakeVisible(title_);

        name_.setText(clipName.isNotEmpty() ? clipName : "Audio", juce::dontSendNotification);
        name_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        name_.setColour(juce::Label::textColourId, juce::Colour(kText));
        addAndMakeVisible(name_);

        file_.setText(filePath, juce::dontSendNotification);
        file_.setFont(juce::FontOptions(11.0f));
        file_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
        file_.setTooltip(filePath);
        addAndMakeVisible(file_);

        info_.setText("Start " + juce::String(startBeat, 2) + " beats"
                      + juce::String::fromUTF8(" · Length ") + juce::String(lengthBeats, 2) + " beats"
                      + juce::String::fromUTF8(" · Source ") + juce::String(durationSeconds, 2) + " s",
                      juce::dontSendNotification);
        info_.setFont(juce::FontOptions(12.0f));
        info_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
        addAndMakeVisible(info_);

        routeLabel_.setText("TRACK ROUTING", juce::dontSendNotification);
        routeLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        routeLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
        addAndMakeVisible(routeLabel_);

        for (int i = 0; i < 48; ++i)
            insertBox_.addItem("MIXER INSERT " + juce::String(i + 1), i + 1);
        insertBox_.setSelectedId(juce::jlimit(0, 47, currentInsert) + 1, juce::dontSendNotification);
        insertBox_.onChange = [this]
        {
            const int insert = juce::jlimit(0, 47, insertBox_.getSelectedId() - 1);
            if (onRoute_) onRoute_(insert);
        };
        addAndMakeVisible(insertBox_);

        configureSlider(gain_, -60.0, 12.0, 0.1, gainDb, " dB");
        gain_.setTooltip("Clip gain antes de entrar al mixer.");
        gain_.onValueChange = [this] { if (onGain_) onGain_(static_cast<float>(gain_.getValue())); };
        addAndMakeVisible(gain_);

        configureSlider(pan_, -1.0, 1.0, 0.01, pan, "");
        pan_.setTooltip("Pan del clip antes del mixer. -1 izquierda, 0 centro, +1 derecha.");
        pan_.onValueChange = [this] { if (onPan_) onPan_(static_cast<float>(pan_.getValue())); };
        addAndMakeVisible(pan_);

        configureSlider(fadeIn_, 0.0, std::max(0.25, lengthBeats), 0.01,
                        juce::jlimit(0.0, std::max(0.25, lengthBeats), fadeInBeats), " beats");
        fadeIn_.setTooltip("Fade In del clip.");
        fadeIn_.onValueChange = [this] { if (onFadeIn_) onFadeIn_(fadeIn_.getValue()); };
        addAndMakeVisible(fadeIn_);

        configureSlider(fadeOut_, 0.0, std::max(0.25, lengthBeats), 0.01,
                        juce::jlimit(0.0, std::max(0.25, lengthBeats), fadeOutBeats), " beats");
        fadeOut_.setTooltip("Fade Out del clip.");
        fadeOut_.onValueChange = [this] { if (onFadeOut_) onFadeOut_(fadeOut_.getValue()); };
        addAndMakeVisible(fadeOut_);

        gainLabel_.setText("VOLUME", juce::dontSendNotification);
        panLabel_.setText("PAN", juce::dontSendNotification);
        fadeInLabel_.setText("FADE IN", juce::dontSendNotification);
        fadeOutLabel_.setText("FADE OUT", juce::dontSendNotification);
        for (auto* label : { &gainLabel_, &panLabel_, &fadeInLabel_, &fadeOutLabel_ })
        {
            label->setFont(juce::FontOptions(10.5f, juce::Font::bold));
            label->setColour(juce::Label::textColourId, juce::Colour(kMuted));
            addAndMakeVisible(*label);
        }

        reverse_.setButtonText("REVERSE");
        reverse_.setToggleState(reversed, juce::dontSendNotification);
        reverse_.setColour(juce::ToggleButton::textColourId, juce::Colour(kText));
        reverse_.onClick = [this] { if (onReverse_) onReverse_(reverse_.getToggleState()); };
        addAndMakeVisible(reverse_);

        for (auto* button : { &normalize_, &replace_, &reveal_, &mixerButton_, &fxButton_ })
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff173246));
            addAndMakeVisible(*button);
        }
        normalize_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22527a));
        fxButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22527a));

        normalize_.onClick = [this] { if (onNormalize_) onNormalize_(); };
        replace_.onClick = [this] { if (onReplace_) onReplace_(); };
        reveal_.onClick = [this] { if (onReveal_) onReveal_(); };
        mixerButton_.onClick = [this]
        {
            const int insert = juce::jlimit(0, 47, insertBox_.getSelectedId() - 1);
            if (onOpenMixer_) onOpenMixer_(insert);
        };
        fxButton_.onClick = [this]
        {
            const int insert = juce::jlimit(0, 47, insertBox_.getSelectedId() - 1);
            if (onOpenFx_) onOpenFx_(insert);
        };

        hint_.setText(juce::String::fromUTF8(
            "Flujo real: AUDIO CLIP → TRACK → MIXER INSERT → FX CHAIN → BUS / MASTER → OUTPUT."),
            juce::dontSendNotification);
        hint_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        hint_.setColour(juce::Label::textColourId, juce::Colour(kAccent));
        addAndMakeVisible(hint_);

        setSize(640, 430);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b151e));
        g.setColour(juce::Colour(kBorder));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.0f);
        g.setColour(juce::Colour(kAccent));
        g.fillRoundedRectangle(18.0f, 94.0f, static_cast<float>(getWidth() - 36), 2.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(18);
        title_.setBounds(r.removeFromTop(28));
        name_.setBounds(r.removeFromTop(24));
        file_.setBounds(r.removeFromTop(20));
        info_.setBounds(r.removeFromTop(22));
        r.removeFromTop(10);

        routeLabel_.setBounds(r.removeFromTop(18));
        insertBox_.setBounds(r.removeFromTop(34));
        r.removeFromTop(10);

        auto labels = r.removeFromTop(18);
        const int columnWidth = std::max(120, (labels.getWidth() - 24) / 4);
        gainLabel_.setBounds(labels.removeFromLeft(columnWidth));
        labels.removeFromLeft(8);
        panLabel_.setBounds(labels.removeFromLeft(columnWidth));
        labels.removeFromLeft(8);
        fadeInLabel_.setBounds(labels.removeFromLeft(columnWidth));
        labels.removeFromLeft(8);
        fadeOutLabel_.setBounds(labels.removeFromLeft(columnWidth));

        auto sliders = r.removeFromTop(42);
        gain_.setBounds(sliders.removeFromLeft(columnWidth));
        sliders.removeFromLeft(8);
        pan_.setBounds(sliders.removeFromLeft(columnWidth));
        sliders.removeFromLeft(8);
        fadeIn_.setBounds(sliders.removeFromLeft(columnWidth));
        sliders.removeFromLeft(8);
        fadeOut_.setBounds(sliders.removeFromLeft(columnWidth));
        r.removeFromTop(10);

        auto tools = r.removeFromTop(34);
        reverse_.setBounds(tools.removeFromLeft(110));
        tools.removeFromLeft(8);
        normalize_.setBounds(tools.removeFromLeft(120));
        tools.removeFromLeft(8);
        replace_.setBounds(tools.removeFromLeft(140));
        tools.removeFromLeft(8);
        reveal_.setBounds(tools.removeFromLeft(160));
        r.removeFromTop(12);

        auto buttons = r.removeFromTop(36);
        mixerButton_.setBounds(buttons.removeFromLeft(190));
        buttons.removeFromLeft(10);
        fxButton_.setBounds(buttons.removeFromLeft(190));
        r.removeFromTop(14);
        hint_.setBounds(r.removeFromTop(28));
    }

private:
    static void configureSlider(juce::Slider& slider, double min, double max, double step,
                                double value, const juce::String& suffix)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
    }

    juce::Label title_;
    juce::Label name_;
    juce::Label file_;
    juce::Label info_;
    juce::Label routeLabel_;
    juce::Label gainLabel_;
    juce::Label panLabel_;
    juce::Label fadeInLabel_;
    juce::Label fadeOutLabel_;
    juce::ComboBox insertBox_;
    juce::Slider gain_;
    juce::Slider pan_;
    juce::Slider fadeIn_;
    juce::Slider fadeOut_;
    juce::ToggleButton reverse_;
    juce::TextButton normalize_ { "NORMALIZE" };
    juce::TextButton replace_ { "REPLACE AUDIO" };
    juce::TextButton reveal_ { "OPEN LOCATION" };
    juce::TextButton mixerButton_ { "OPEN MIXER" };
    juce::TextButton fxButton_ { "FX / PLUGINS" };
    juce::Label hint_;
    std::function<void(int)> onRoute_;
    std::function<void(float)> onGain_;
    std::function<void(float)> onPan_;
    std::function<void(double)> onFadeIn_;
    std::function<void(double)> onFadeOut_;
    std::function<void(bool)> onReverse_;
    std::function<void()> onNormalize_;
    std::function<void()> onReplace_;
    std::function<void()> onReveal_;
    std::function<void(int)> onOpenMixer_;
    std::function<void(int)> onOpenFx_;
};}

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
        tracks_[i].mixerInsert = i;
    }
    tracks_[0].name = "Voz";
    tracks_[1].name = juce::String::fromUTF8("Batería");
    tracks_[2].name = "Bajo";
    tracks_[3].name = "Guitarra";
    tracks_[4].name = "Teclado";
    tracks_[5].name = "Secuencias";
    tracks_[6].name = "Pads";
    tracks_[7].name = "FX";

    for (int i = 0; i < kMaxTracks; ++i)
    {
        liveActiveClipIds_[i].store(kLiveNoClip, std::memory_order_relaxed);
        livePendingClipIds_[i].store(kLiveNoClip, std::memory_order_relaxed);
        liveClipLaunchSamples_[i].store(0, std::memory_order_relaxed);
        livePendingLaunchSamples_[i].store(0, std::memory_order_relaxed);
    }

    configureControls();
    loadWorkspaceState();
    syncInspector();
    rebuildRenderState();

    const auto recover = recoveryFile();
    if (recover.existsAsFile())
    {
        const auto xml = recover.loadFileAsString();
        if (xml.isNotEmpty() && restoreProject(xml, false))
            refreshStatus(juce::String::fromUTF8("Sesión recuperada automáticamente"));
    }

    startTimerHz(30);
}

DawWorkspace::~DawWorkspace()
{
    stopTimer();
    if (trackRecording_.load(std::memory_order_acquire))
        stopTrackRecording(false);
    saveWorkspaceState();
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
                     &splitButton_, &duplicateButton_, &deleteButton_, &mixerViewButton_, &dspViewButton_,
                     &pluginsViewButton_, &padsViewButton_, &iemViewButton_ })
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
                    importFiles(paths, selectedTrack_, beatAtX(static_cast<float>(timelineBounds().getX() + headerWidth_ + 12)));
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

    mixerViewButton_.onClick = [this] { if (onOpenMixer) onOpenMixer(); };
    dspViewButton_.onClick = [this] { if (onOpenDsp) onOpenDsp(); };
    pluginsViewButton_.onClick = [this] { if (onOpenPlugins) onOpenPlugins(); };
    padsViewButton_.onClick = [this] { if (onOpenPads) onOpenPads(); };
    iemViewButton_.onClick = [this] { if (onOpenIem) onOpenIem(); };
    for (auto* b : { &mixerViewButton_, &dspViewButton_, &pluginsViewButton_, &padsViewButton_, &iemViewButton_ })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff162d3d));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd9e9f5));
    }

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

    workspaceBox_.addItem("LIVE", 1);
    workspaceBox_.addItem("MIX", 2);
    workspaceBox_.addItem("RECORD", 3);
    workspaceBox_.addItem("EDIT", 4);
    workspaceBox_.addItem("IEM", 5);
    workspaceBox_.setSelectedId(workspacePreset_, juce::dontSendNotification);
    workspaceBox_.setTooltip("Workspace: reorganiza Browser, Inspector, Mixer y Arranger");
    workspaceBox_.onChange = [this] { applyWorkspacePreset(workspaceBox_.getSelectedId()); };
    addAndMakeVisible(workspaceBox_);

    for (auto* b : { &fitProjectButton_, &fitSelectionButton_, &resetWorkspaceButton_ })
    {
        addButton(*b);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff142735));
    }
    fitProjectButton_.setTooltip("Fit Project");
    fitSelectionButton_.setTooltip("Fit Selection");
    resetWorkspaceButton_.setTooltip(juce::String::fromUTF8("Restaurar distribución del workspace"));
    fitProjectButton_.onClick = [this] { fitProject(); };
    fitSelectionButton_.onClick = [this] { fitSelection(); };
    resetWorkspaceButton_.onClick = [this] { resetWorkspace(); };

    browserSearch_.setTextToShowWhenEmpty("Buscar canciones, samples, plugins...", juce::Colour(kMuted));
    browserSearch_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a151e));
    browserSearch_.setColour(juce::TextEditor::textColourId, juce::Colour(kText));
    browserSearch_.setColour(juce::TextEditor::outlineColourId, juce::Colour(kBorder));
    browserSearch_.setTooltip("Buscar en el Browser");
    addAndMakeVisible(browserSearch_);

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

    trackVolumeSlider_.setDoubleClickReturnValue(true, 0.0);
    trackPanSlider_.setDoubleClickReturnValue(true, 0.0);
    clipGainSlider_.setDoubleClickReturnValue(true, 0.0);
    fadeInSlider_.setDoubleClickReturnValue(true, 0.0);
    fadeOutSlider_.setDoubleClickReturnValue(true, 0.0);
    bpmSlider_.setDoubleClickReturnValue(true, 120.0);
    zoomSlider_.setDoubleClickReturnValue(true, 1.0);

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

    for (int i = 0; i < kMaxTracks; ++i)
        clipMixerBox_.addItem("MIXER INSERT " + juce::String(i + 1), i + 1);
    clipMixerBox_.setSelectedId(1, juce::dontSendNotification);
    clipMixerBox_.setTooltip(juce::String::fromUTF8("Asigna este audio o pista MIDI a un Insert del mixer, como el flujo Channel → Mixer de FL Studio."));
    clipMixerBox_.onChange = [this]
    {
        const int insert = juce::jlimit(0, kMaxTracks - 1, clipMixerBox_.getSelectedId() - 1);
        bool changed = false;
        for (auto& c : clips_)
        {
            if (c.id != selectedClipId_) continue;
            if (c.mixerInsert != insert)
            {
                checkpointUndo();
                c.mixerInsert = insert;
                changed = true;
            }
            break;
        }
        if (!changed && selectedClipId_ < 0 && selectedTrack_ >= 0 && selectedTrack_ < trackCount_
            && tracks_[selectedTrack_].mixerInsert != insert)
        {
            checkpointUndo();
            tracks_[selectedTrack_].mixerInsert = insert;
            changed = true;
        }
        if (changed)
        {
            projectDirty_ = true;
            markRenderDirty();
            if (onMixerRoutingChanged) onMixerRoutingChanged();
            repaint();
        }
    };
    addAndMakeVisible(clipMixerBox_);

    for (auto* b : { &openMixerInsertButton_, &openPluginsInsertButton_ })
    {
        addButton(*b);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff173246));
    }
    openMixerInsertButton_.onClick = [this]
    {
        const int insert = juce::jlimit(0, kMaxTracks - 1, clipMixerBox_.getSelectedId() - 1);
        if (onOpenMixerInsert) onOpenMixerInsert(insert);
    };
    openPluginsInsertButton_.onClick = [this]
    {
        const int insert = juce::jlimit(0, kMaxTracks - 1, clipMixerBox_.getSelectedId() - 1);
        if (onOpenPluginsForInsert) onOpenPluginsForInsert(insert);
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
    juce::ColourGradient toolbarGradient(juce::Colour(0xff111e29), 0.0f, static_cast<float>(toolbar.getY()),
                                         juce::Colour(0xff09131b), 0.0f, static_cast<float>(toolbar.getBottom()), false);
    g.setGradientFill(toolbarGradient);
    g.fillRect(toolbar);
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(toolbar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    const auto browser = browserBounds();
    const auto inspector = inspectorBounds();
    const auto mixer = mixerBounds();
    const auto tl = timelineBounds();
    const auto ruler = rulerBounds();

    g.setColour(juce::Colour(0xff0a141c));
    g.fillRect(browser);
    g.setColour(juce::Colour(0xff0d1922));
    g.fillRect(inspector);
    g.setColour(juce::Colour(0xff09131b));
    g.fillRect(mixer);

    g.setColour(juce::Colour(kBorder));
    g.drawVerticalLine(browser.getRight() - 1, static_cast<float>(browser.getY()), static_cast<float>(browser.getBottom()));
    g.drawVerticalLine(inspector.getX(), static_cast<float>(inspector.getY()), static_cast<float>(inspector.getBottom()));
    g.drawHorizontalLine(mixer.getY(), 0.0f, static_cast<float>(getWidth()));

    g.setColour(juce::Colour(kText));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("BROWSER", browser.reduced(10, 7).removeFromTop(18), juce::Justification::centredLeft);
    static const std::array<juce::String, 7> browserItems {
        "Canciones", "Setlists", "Pads", "Samples", "Plugins", "Favoritos", "Proyectos"
    };
    int browserY = browser.getY() + 70;
    for (std::size_t i = 0; i < browserItems.size(); ++i)
    {
        juce::Rectangle<int> item(browser.getX() + 8, browserY, std::max(0, browser.getWidth() - 16), 27);
        const bool active = static_cast<int>(i) == selectedBrowserItem_;
        g.setColour(juce::Colour(active ? 0xff132a38 : 0xff0e1b24));
        g.fillRoundedRectangle(item.toFloat(), 4.0f);
        g.setColour(juce::Colour(active ? kText : kMuted));
        g.setFont(juce::FontOptions(11.0f, active ? juce::Font::bold : juce::Font::plain));
        g.drawText(browserItems[i], item.reduced(8, 0), juce::Justification::centredLeft);
        browserY += 31;
    }

    g.setColour(juce::Colour(kText));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("INSPECTOR", inspector.reduced(10, 7).removeFromTop(18), juce::Justification::centredLeft);
    g.setColour(juce::Colour(kMuted));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(selectedClipId_ >= 0 ? juce::String::fromUTF8("CLIP · AUDIO") : (tracks_[selectedTrack_].midi ? juce::String::fromUTF8("TRACK · MIDI") : juce::String::fromUTF8("TRACK · AUDIO")),
               inspector.getX() + 10, inspector.getY() + 23, std::max(0, inspector.getWidth() - 20), 14,
               juce::Justification::centredLeft);

    g.setColour(juce::Colour(kText));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("MIXER · QUICK VIEW"), mixer.getX() + 10, mixer.getY() + 5, 180, 18, juce::Justification::centredLeft);
    auto mixerContent = mixer.reduced(8, 25);
    const int visibleMixerTracks = std::min(8, trackCount_);
    const int mixerStripWidth = visibleMixerTracks > 0 ? std::max(1, mixerContent.getWidth() / visibleMixerTracks) : mixerContent.getWidth();
    for (int i = 0; i < visibleMixerTracks; ++i)
    {
        auto strip = mixerContent.removeFromLeft(mixerStripWidth).reduced(2);
        const bool selected = i == selectedTrack_;
        const auto stripBounds = strip;
        g.setColour(juce::Colour(selected ? 0xff152d3c : 0xff0d1a23));
        g.fillRoundedRectangle(strip.toFloat(), 4.0f);
        g.setColour(selected ? tracks_[i].colour.brighter(0.18f) : tracks_[i].colour);
        g.fillRect(strip.removeFromTop(3));

        g.setColour(juce::Colour(kText));
        g.setFont(juce::FontOptions(10.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(tracks_[i].name, strip.removeFromTop(19).reduced(4, 0), juce::Justification::centred, 1);

        auto footer = strip.removeFromBottom(22);
        auto body = strip.reduced(7, 3);
        const float gainDb = static_cast<float>(gainToDb(tracks_[i].gain));
        const float normGain = juce::jlimit(0.0f, 1.0f, (gainDb + 60.0f) / 72.0f);

        // A real fader-style quick view reads as a mixer even when no audio is playing.
        auto faderLane = body.withWidth(std::max(22, body.getWidth() / 3)).withCentre(body.getCentre());
        const int laneX = faderLane.getCentreX();
        const int laneTop = faderLane.getY() + 3;
        const int laneBottom = faderLane.getBottom() - 3;
        g.setColour(juce::Colour(0xff263b48));
        g.fillRoundedRectangle(static_cast<float>(laneX - 2), static_cast<float>(laneTop),
                               4.0f, static_cast<float>(std::max(1, laneBottom - laneTop)), 2.0f);

        const int handleY = laneBottom - static_cast<int>(normGain * std::max(1, laneBottom - laneTop));
        auto handle = juce::Rectangle<int>(laneX - std::max(11, faderLane.getWidth() / 3),
                                           handleY - 3,
                                           std::max(22, (faderLane.getWidth() * 2) / 3), 7);
        g.setColour(tracks_[i].mute ? juce::Colour(kDanger).withAlpha(0.72f)
                                    : tracks_[i].colour.brighter(selected ? 0.28f : 0.08f));
        g.fillRoundedRectangle(handle.toFloat(), 2.5f);
        g.setColour(juce::Colour(0xffeaf5ff).withAlpha(selected ? 0.9f : 0.42f));
        g.drawRoundedRectangle(handle.toFloat(), 2.5f, 0.8f);

        // Pan marker gives useful at-a-glance information without adding another control row.
        const float pan = juce::jlimit(-1.0f, 1.0f, tracks_[i].pan);
        auto panRail = juce::Rectangle<int>(body.getX() + 5, body.getBottom() - 8,
                                            std::max(12, body.getWidth() - 10), 2);
        g.setColour(juce::Colour(0xff273a46));
        g.fillRoundedRectangle(panRail.toFloat(), 1.0f);
        const int panX = panRail.getCentreX() + static_cast<int>(pan * panRail.getWidth() * 0.45f);
        g.setColour(juce::Colour(kMuted));
        g.fillEllipse(static_cast<float>(panX - 2), static_cast<float>(panRail.getCentreY() - 2), 5.0f, 5.0f);

        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.setColour(juce::Colour(kMuted));
        auto gainLabel = footer.removeFromLeft(std::max(42, footer.getWidth() - 64)).reduced(3, 1);
        g.drawFittedText(juce::String(gainDb, 1) + " dB", gainLabel, juce::Justification::centredLeft, 1);

        auto chipArea = footer.reduced(1, 2);
        auto drawMiniChip = [&](const char* label, bool active, juce::Colour colour)
        {
            if (chipArea.getWidth() < 14) return;
            auto chip = chipArea.removeFromLeft(std::min(18, chipArea.getWidth())).reduced(1);
            g.setColour(active ? colour : juce::Colour(0xff182832));
            g.fillRoundedRectangle(chip.toFloat(), 2.0f);
            g.setColour(active ? juce::Colours::white : juce::Colour(0xff708491));
            g.drawText(label, chip, juce::Justification::centred);
        };
        drawMiniChip("M", tracks_[i].mute, juce::Colour(kDanger));
        drawMiniChip("S", tracks_[i].solo, juce::Colour(kGood));
        if (!tracks_[i].midi)
            drawMiniChip("R", tracks_[i].armed, juce::Colour(kDanger));

        if (selected)
        {
            g.setColour(tracks_[i].colour.withAlpha(0.85f));
            g.drawRoundedRectangle(stripBounds.toFloat().reduced(0.5f), 4.0f, 1.2f);
        }
    }

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
        if (x < tl.getX() + headerWidth_ || x > tl.getRight()) continue;
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

    g.saveState();
    g.reduceClipRegion(tl);
    g.setColour(juce::Colour(0xff0b1720));
    g.fillRect(tl.withWidth(headerWidth_));
    g.setColour(juce::Colour(kBorder));
    g.drawVerticalLine(tl.getX() + headerWidth_ - 1, static_cast<float>(tl.getY()), static_cast<float>(tl.getBottom()));

    for (int t = 0; t < trackCount_; ++t)
    {
        auto header = trackHeaderBounds(t);
        auto row = header.withX(tl.getX() + headerWidth_).withWidth(std::max(0, tl.getWidth() - headerWidth_));
        const bool selected = t == selectedTrack_;
        g.setColour(juce::Colour(selected ? 0xff142a38 : (t % 2 == 0 ? 0xff0b151e : 0xff0d1821)));
        g.fillRect(row);
        g.setColour(juce::Colour(kBorder).withAlpha(0.55f));
        g.drawHorizontalLine(row.getBottom() - 1, static_cast<float>(tl.getX() + headerWidth_), static_cast<float>(tl.getRight()));

        g.setColour(juce::Colour(selected ? 0xff172a37 : 0xff101d27));
        g.fillRect(header);
        g.setColour(tracks_[t].colour);
        g.fillRect(header.removeFromLeft(5));
        g.setColour(juce::Colour(kText));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(juce::String(t + 1) + "  " + tracks_[t].name, header.reduced(9, 7).removeFromTop(23),
                   juce::Justification::centredLeft);
        auto meta = header.reduced(9, 7).withTrimmedTop(25);
        g.setFont(juce::FontOptions(10.5f));
        g.setColour(juce::Colour(kMuted));
        const juce::String typeText = tracks_[t].midi ? "MIDI / PIANO ROLL" : "AUDIO";
        g.drawText(typeText + juce::String(juce::String::fromUTF8("  ·  ")) + juce::String(gainToDb(tracks_[t].gain), 1) + " dB",
                   meta.removeFromLeft(std::max(48, meta.getWidth() - 74)), juce::Justification::centredLeft);

        auto chipArea = meta.reduced(0, 3);
        auto drawChip = [&](const juce::String& label, bool active, juce::Colour activeColour)
        {
            auto chip = chipArea.removeFromLeft(21).toFloat();
            chipArea.removeFromLeft(2);
            g.setColour(active ? activeColour : juce::Colour(0xff1b2a34));
            g.fillRoundedRectangle(chip, 3.0f);
            g.setColour(active ? juce::Colours::white : juce::Colour(0xff748794));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(label, chip.toNearestInt(), juce::Justification::centred);
        };
        drawChip("M", tracks_[t].mute, juce::Colour(kDanger));
        drawChip("S", tracks_[t].solo, juce::Colour(kGood));
        drawChip("R", tracks_[t].armed, juce::Colour(kDanger));
    }

    // Keep arranger media strictly inside the timeline body. The fixed track-header
    // column must never be painted over, even when horizontal zoom/scroll moves a
    // clip's logical start to the left of the visible viewport.
    const auto arrangerContent = tl.withTrimmedLeft(headerWidth_);
    g.saveState();
    g.reduceClipRegion(arrangerContent);

    for (const auto& clip : clips_)
    {
        auto cb = clipBounds(clip);
        if (!cb.intersects(arrangerContent.toFloat())) continue;
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
                        if (clip.reversed) u = 1.0 - u;
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

                if (selected && clip.audio->transientsAnalyzed && wantedSource > 1.0)
                {
                    g.setColour(juce::Colour(0xff7ce7ff).withAlpha(0.82f));
                    int drawn = 0;
                    for (const int transient : clip.audio->transientSamples)
                    {
                        double u = (static_cast<double>(transient) - sourceStart) / wantedSource;
                        if (clip.reversed) u = 1.0 - u;
                        if (u < 0.0 || u > 1.0)
                            continue;
                        const float x = wave.getX() + static_cast<float>(u) * wave.getWidth();
                        g.drawVerticalLine(static_cast<int>(x), wave.getY(), wave.getBottom());
                        juce::Path marker;
                        marker.addTriangle(x - 3.5f, wave.getY(), x + 3.5f, wave.getY(), x, wave.getY() + 5.0f);
                        g.fillPath(marker);
                        if (++drawn >= 96) break;
                    }
                }
            }
        }

        if (clip.reversed)
        {
            g.setColour(juce::Colour(0xff071018).withAlpha(0.78f));
            auto rev = juce::Rectangle<float>(cb.getRight() - 36.0f, cb.getY() + 4.0f, 30.0f, 15.0f);
            g.fillRoundedRectangle(rev, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            g.drawText("REV", rev.toNearestInt(), juce::Justification::centred);
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
        if (!nb.intersects(arrangerContent.toFloat()))
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

    g.restoreState(); // arrangerContent
    g.restoreState(); // timeline

    const double posBeat = (static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed))) * bpm() / 60.0;
    const float playX = xForBeat(posBeat);
    if (playX >= tl.getX() + headerWidth_ && playX <= tl.getRight())
    {
        g.setColour(juce::Colour(kAccent));
        g.drawVerticalLine(static_cast<int>(playX), static_cast<float>(ruler.getY()), static_cast<float>(tl.getBottom()));
        juce::Path marker;
        marker.addTriangle(playX - 6.0f, static_cast<float>(ruler.getY()),
                           playX + 6.0f, static_cast<float>(ruler.getY()),
                           playX, static_cast<float>(ruler.getY() + 8));
        g.fillPath(marker);
    }

    g.setColour(juce::Colour(0xff6f8492));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("J3 ARRANGER  ·  AUDIO + MIDI  ·  SPACE PLAY/STOP  ·  CTRL+S  ·  CTRL+Z/Y"),
               std::max(8, getWidth() - 540), mixer.getBottom() - 18, std::min(532, getWidth() - 16), 15,
               juce::Justification::centredRight);
}

void DawWorkspace::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop(toolbarHeight_).reduced(8, 6);
    auto topRow = toolbar.removeFromTop(28);
    toolbar.removeFromTop(4);
    auto bottomRow = toolbar.removeFromTop(28);

    auto takeLeft = [](juce::Rectangle<int>& row, juce::Component& component, int width)
    {
        const int w = std::min(width, std::max(0, row.getWidth()));
        component.setBounds(row.removeFromLeft(w));
        if (row.getWidth() > 0) row.removeFromLeft(std::min(4, row.getWidth()));
    };
    auto takeRight = [](juce::Rectangle<int>& row, juce::Component& component, int width)
    {
        const int w = std::min(width, std::max(0, row.getWidth()));
        component.setBounds(row.removeFromRight(w));
        if (row.getWidth() > 0) row.removeFromRight(std::min(4, row.getWidth()));
    };

    takeLeft(topRow, newButton_, 56);
    takeLeft(topRow, openButton_, 58);
    takeLeft(topRow, saveButton_, 66);
    takeLeft(topRow, importButton_, 104);
    topRow.removeFromLeft(std::min(6, topRow.getWidth()));
    takeLeft(topRow, addTrackButton_, 68);
    takeLeft(topRow, addMidiTrackButton_, 64);
    takeLeft(topRow, patternButton_, 84);

    takeRight(topRow, iemViewButton_, 42);
    takeRight(topRow, padsViewButton_, 50);
    takeRight(topRow, pluginsViewButton_, 72);
    takeRight(topRow, dspViewButton_, 48);
    takeRight(topRow, mixerViewButton_, 62);

    takeLeft(bottomRow, stopButton_, 54);
    takeLeft(bottomRow, playButton_, 60);
    takeLeft(bottomRow, recordButton_, 48);
    takeLeft(bottomRow, loopButton_, 58);
    bottomRow.removeFromLeft(std::min(8, bottomRow.getWidth()));
    takeLeft(bottomRow, splitButton_, 66);
    takeLeft(bottomRow, duplicateButton_, 74);
    takeLeft(bottomRow, deleteButton_, 60);
    bottomRow.removeFromLeft(std::min(8, bottomRow.getWidth()));
    takeLeft(bottomRow, bpmSlider_, 136);
    takeLeft(bottomRow, snapBox_, 102);

    takeRight(bottomRow, resetWorkspaceButton_, 68);
    takeRight(bottomRow, workspaceBox_, 94);
    takeRight(bottomRow, fitSelectionButton_, 44);
    takeRight(bottomRow, fitProjectButton_, 44);
    zoomSlider_.setBounds(bottomRow);

    auto browser = browserBounds().reduced(9, 8);
    browser.removeFromTop(24);
    browserSearch_.setBounds(browser.removeFromTop(30));

    auto inspector = inspectorBounds().reduced(10, 8);
    const bool compactInspector = inspector.getHeight() < 390;
    const int rowHeight = compactInspector ? 24 : 30;
    const int buttonHeight = compactInspector ? 24 : 28;
    const int gap = compactInspector ? 4 : 7;
    const int statusHeight = compactInspector ? 40 : 54;

    statusLabel_.setBounds(inspector.removeFromBottom(std::min(statusHeight, inspector.getHeight())));
    inspector.removeFromTop(compactInspector ? 22 : 27);
    trackNameEditor_.setBounds(inspector.removeFromTop(rowHeight));
    inspector.removeFromTop(gap);

    auto trackButtons = inspector.removeFromTop(buttonHeight);
    trackMuteButton_.setBounds(trackButtons.removeFromLeft(42));
    trackButtons.removeFromLeft(5);
    trackSoloButton_.setBounds(trackButtons.removeFromLeft(42));
    trackButtons.removeFromLeft(5);
    trackArmButton_.setBounds(trackButtons.removeFromLeft(54));

    inspector.removeFromTop(gap);
    trackVolumeSlider_.setBounds(inspector.removeFromTop(rowHeight));
    trackPanSlider_.setBounds(inspector.removeFromTop(rowHeight));
    inspector.removeFromTop(gap);

    auto clipButtons = inspector.removeFromTop(buttonHeight);
    clipMuteButton_.setBounds(clipButtons.removeFromLeft(std::min(92, clipButtons.getWidth() / 2)));
    clipButtons.removeFromLeft(std::min(5, clipButtons.getWidth()));
    clipLoopButton_.setBounds(clipButtons);

    inspector.removeFromTop(gap);
    clipGainSlider_.setBounds(inspector.removeFromTop(rowHeight));
    fadeInSlider_.setBounds(inspector.removeFromTop(rowHeight));
    fadeOutSlider_.setBounds(inspector.removeFromTop(rowHeight));
    inspector.removeFromTop(gap);
    clipMixerBox_.setBounds(inspector.removeFromTop(compactInspector ? 26 : 30));
    inspector.removeFromTop(compactInspector ? 3 : 5);
    auto insertButtons = inspector.removeFromTop(buttonHeight);
    openMixerInsertButton_.setBounds(insertButtons.removeFromLeft(std::min(104, insertButtons.getWidth() / 2)));
    insertButtons.removeFromLeft(std::min(5, insertButtons.getWidth()));
    openPluginsInsertButton_.setBounds(insertButtons);

    repaint();
}

juce::Rectangle<int> DawWorkspace::timelineBounds() const
{
    const int left = juce::jlimit(140, std::max(140, getWidth() / 3), browserWidth_);
    const int right = juce::jlimit(220, std::max(220, getWidth() / 3), inspectorWidth_);
    const int bottom = juce::jlimit(96, std::max(96, getHeight() / 2), mixerHeight_);
    return { left, toolbarHeight_ + rulerHeight_,
             std::max(0, getWidth() - left - right),
             std::max(0, getHeight() - toolbarHeight_ - rulerHeight_ - bottom) };
}

juce::Rectangle<int> DawWorkspace::rulerBounds() const
{
    const auto tl = timelineBounds();
    return { tl.getX(), toolbarHeight_, tl.getWidth(), rulerHeight_ };
}

juce::Rectangle<int> DawWorkspace::browserBounds() const
{
    const auto tl = timelineBounds();
    return { 0, toolbarHeight_, tl.getX(), std::max(0, getHeight() - toolbarHeight_ - mixerBounds().getHeight()) };
}

juce::Rectangle<int> DawWorkspace::inspectorBounds() const
{
    const auto tl = timelineBounds();
    return { tl.getRight(), toolbarHeight_, std::max(0, getWidth() - tl.getRight()),
             std::max(0, getHeight() - toolbarHeight_ - mixerBounds().getHeight()) };
}

juce::Rectangle<int> DawWorkspace::mixerBounds() const
{
    const int h = juce::jlimit(96, std::max(96, getHeight() / 2), mixerHeight_);
    return { 0, std::max(toolbarHeight_, getHeight() - h), getWidth(), h };
}

bool DawWorkspace::validateLayoutForTesting(juce::String& report) const
{
    const auto local = getLocalBounds();
    const auto browser = browserBounds();
    const auto timeline = timelineBounds();
    const auto inspector = inspectorBounds();
    const auto mixer = mixerBounds();

    auto fail = [&report](const juce::String& message)
    {
        report = message;
        return false;
    };

    if (!local.contains(browser) || !local.contains(timeline) || !local.contains(inspector) || !local.contains(mixer))
        return fail("major workspace region escaped the component bounds");
    if (timeline.getWidth() < 360 || timeline.getHeight() < 180)
        return fail("arranger viewport is too small");
    if (browser.getWidth() < 140 || inspector.getWidth() < 220 || mixer.getHeight() < 96)
        return fail("browser, inspector or mixer fell below its professional minimum");
    if (browser.getRight() != timeline.getX() || inspector.getX() != timeline.getRight()
        || mixer.getY() != timeline.getBottom())
        return fail("workspace regions overlap or leave an unintended gap");

    for (int i = 0; i < getNumChildComponents(); ++i)
    {
        const auto* child = getChildComponent(i);
        if (child == nullptr || !child->isVisible()) continue;
        const auto bounds = child->getBounds();
        if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
            return fail("visible control has an empty layout: " + child->getName());
        if (!local.contains(bounds))
            return fail("visible control escaped the viewport: " + child->getName());
    }

    if (!browser.contains(browserSearch_.getBounds()))
        return fail("browser search escaped the browser panel");
    if (!inspector.contains(trackNameEditor_.getBounds())
        || !inspector.contains(trackVolumeSlider_.getBounds())
        || !inspector.contains(trackPanSlider_.getBounds())
        || !inspector.contains(clipGainSlider_.getBounds())
        || !inspector.contains(clipMixerBox_.getBounds()))
        return fail("inspector controls escaped the inspector panel");

    report = "OK";
    return true;
}

juce::Rectangle<int> DawWorkspace::trackHeaderBounds(int track) const
{
    const auto tl = timelineBounds();
    return { tl.getX(), tl.getY() + (track - firstVisibleTrack_) * trackHeight_, headerWidth_, trackHeight_ };
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
    const auto arrangerContent = timelineBounds().withTrimmedLeft(headerWidth_);
    if (!arrangerContent.contains(point))
        return nullptr;

    for (auto it = midiNotes_.rbegin(); it != midiNotes_.rend(); ++it)
        if (midiNoteBounds(*it).expanded(1.0f, 2.0f).contains(point.toFloat()))
            return &*it;
    return nullptr;
}

const DawWorkspace::MidiNote* DawWorkspace::midiNoteAt(juce::Point<int> point) const
{
    const auto arrangerContent = timelineBounds().withTrimmedLeft(headerWidth_);
    if (!arrangerContent.contains(point))
        return nullptr;

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

    const auto arrangerContent = timelineBounds().withTrimmedLeft(headerWidth_);
    if (!arrangerContent.contains(point))
        return nullptr;

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

    const auto arrangerContent = timelineBounds().withTrimmedLeft(headerWidth_);
    if (!arrangerContent.contains(point))
        return nullptr;

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
    const auto tl = timelineBounds();
    return std::max(0.0, viewStartBeat_ + (static_cast<double>(x) - (tl.getX() + headerWidth_)) / pixelsPerBeat());
}

float DawWorkspace::xForBeat(double beat) const noexcept
{
    const auto tl = timelineBounds();
    return static_cast<float>(tl.getX() + headerWidth_ + (beat - viewStartBeat_) * pixelsPerBeat());
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

void DawWorkspace::fitProject()
{
    const auto tl = timelineBounds();
    const double endBeat = std::max(4.0, projectEndBeat());
    const double usable = std::max(80, tl.getWidth() - headerWidth_ - 24);
    zoom_ = juce::jlimit(0.5, 4.0, usable / (44.0 * endBeat));
    viewStartBeat_ = 0.0;
    zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    repaint();
}

void DawWorkspace::fitSelection()
{
    double startBeat = 0.0;
    double endBeat = 0.0;
    bool found = false;
    if (const auto* c = clipAt({ -1, -1 }))
    {
        startBeat = c->startBeat;
        endBeat = c->startBeat + c->lengthBeats;
        found = true;
    }
    if (!found && selectedMidiNoteId_ >= 0)
    {
        for (const auto& n : midiNotes_)
            if (n.id == selectedMidiNoteId_)
            {
                startBeat = n.startBeat;
                endBeat = n.startBeat + n.lengthBeats;
                found = true;
                break;
            }
    }
    if (!found) { fitProject(); return; }

    const auto tl = timelineBounds();
    const double pad = std::max(0.5, (endBeat - startBeat) * 0.12);
    const double span = std::max(0.5, (endBeat - startBeat) + pad * 2.0);
    const double usable = std::max(80, tl.getWidth() - headerWidth_ - 24);
    zoom_ = juce::jlimit(0.5, 4.0, usable / (44.0 * span));
    viewStartBeat_ = std::max(0.0, startBeat - pad);
    zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    repaint();
}

juce::File DawWorkspace::workspaceStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("J3 Worship")
        .getChildFile("workspace.xml");
}

void DawWorkspace::loadWorkspaceState()
{
    const auto file = workspaceStateFile();
    if (!file.existsAsFile()) return;
    auto xml = juce::parseXML(file.loadFileAsString());
    if (xml == nullptr || !xml->hasTagName("J3Workspace")) return;

    workspacePreset_ = juce::jlimit(1, 5, xml->getIntAttribute("preset", 4));
    browserWidth_ = juce::jlimit(140, 360, xml->getIntAttribute("browserWidth", 210));
    inspectorWidth_ = juce::jlimit(220, 420, xml->getIntAttribute("inspectorWidth", 285));
    mixerHeight_ = juce::jlimit(96, 360, xml->getIntAttribute("mixerHeight", 125));
    trackHeight_ = juce::jlimit(44, 150, xml->getIntAttribute("trackHeight", 76));
    workspaceBox_.setSelectedId(workspacePreset_, juce::dontSendNotification);
}

void DawWorkspace::saveWorkspaceState() const
{
    juce::XmlElement xml("J3Workspace");
    xml.setAttribute("preset", workspacePreset_);
    xml.setAttribute("browserWidth", browserWidth_);
    xml.setAttribute("inspectorWidth", inspectorWidth_);
    xml.setAttribute("mixerHeight", mixerHeight_);
    xml.setAttribute("trackHeight", trackHeight_);
    auto file = workspaceStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(xml.toString());
}

void DawWorkspace::applyWorkspacePreset(int preset)
{
    workspacePreset_ = juce::jlimit(1, 5, preset);
    switch (workspacePreset_)
    {
        case 1: browserWidth_ = 180; inspectorWidth_ = 230; mixerHeight_ = 168; trackHeight_ = 72; break;
        case 2: browserWidth_ = 150; inspectorWidth_ = 245; mixerHeight_ = 245; trackHeight_ = 64; break;
        case 3: browserWidth_ = 165; inspectorWidth_ = 255; mixerHeight_ = 175; trackHeight_ = 76; break;
        case 5: browserWidth_ = 150; inspectorWidth_ = 225; mixerHeight_ = 220; trackHeight_ = 66; break;
        default: browserWidth_ = 210; inspectorWidth_ = 285; mixerHeight_ = 125; trackHeight_ = 82; break;
    }
    workspaceBox_.setSelectedId(workspacePreset_, juce::dontSendNotification);
    resized();
    saveWorkspaceState();
    refreshStatus("Workspace " + workspaceBox_.getText());
}

void DawWorkspace::resetWorkspace()
{
    zoom_ = 1.0;
    viewStartBeat_ = 0.0;
    firstVisibleTrack_ = 0;
    applyWorkspacePreset(4);
    zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    refreshStatus("Workspace restablecido");
}

void DawWorkspace::showContextMenu(juce::Point<int> point)
{
    juce::PopupMenu menu;
    const bool hasClip = selectedClipId_ >= 0;
    const bool hasMidi = selectedMidiNoteId_ >= 0;

    if (hasClip)
    {
        menu.addItem(1, "Dividir en playhead");
        menu.addItem(2, "Duplicar");
        menu.addItem(3, "Mute / Unmute");
        menu.addItem(4, "Loop / No Loop");
        menu.addSeparator();
        menu.addItem(7, "Normalize");
        menu.addItem(8, "Reverse");
        menu.addItem(9, "Crossfade con clip solapado");
        menu.addItem(15, "Bounce in place");

        juce::PopupMenu stretchMenu;
        stretchMenu.addItem(20, "50%  ·  2x faster");
        stretchMenu.addItem(21, "75%");
        stretchMenu.addItem(22, "125%");
        stretchMenu.addItem(23, "150%");
        stretchMenu.addItem(24, "200%  ·  2x longer");
        menu.addSubMenu(juce::String::fromUTF8("Time Stretch · PITCH LOCK"), stretchMenu);
        menu.addItem(25, L"Detectar transientes / Warp Markers");
        menu.addItem(26, L"AUTO WARP al BPM del proyecto");
        menu.addItem(5, "Fit Selection");
        menu.addSeparator();
        menu.addItem(6, "Eliminar");
    }
    else if (hasMidi)
    {
        menu.addItem(2, "Duplicar nota");
        menu.addItem(5, "Fit Selection");
        menu.addSeparator();
        menu.addItem(6, "Eliminar nota");
    }
    else
    {
        menu.addItem(10, "Renombrar pista");
        menu.addItem(11, "Agregar pista de audio");
        menu.addItem(12, "Agregar pista MIDI");
        menu.addSeparator();
        menu.addItem(13, "Fit Project");
        menu.addItem(14, "Reset Workspace");
    }

    juce::Component::SafePointer<DawWorkspace> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ point.x, point.y, 1, 1 }),
        [safe](int result)
        {
            if (safe == nullptr || result == 0) return;
            switch (result)
            {
                case 1: safe->splitSelectedClipAtPlayhead(); break;
                case 2: safe->duplicateSelectedClip(); break;
                case 3:
                    for (auto& c : safe->clips_) if (c.id == safe->selectedClipId_)
                    {
                        safe->checkpointUndo(); c.muted = !c.muted; safe->projectDirty_ = true;
                        safe->markRenderDirty(); safe->syncInspector(); safe->repaint(); break;
                    }
                    break;
                case 4:
                    for (auto& c : safe->clips_) if (c.id == safe->selectedClipId_)
                    {
                        safe->checkpointUndo(); c.loop = !c.loop; safe->projectDirty_ = true;
                        safe->markRenderDirty(); safe->syncInspector(); safe->repaint(); break;
                    }
                    break;
                case 5: safe->fitSelection(); break;
                case 6: safe->deleteSelectedClip(); break;
                case 7: safe->normalizeSelectedClip(); break;
                case 8: safe->reverseSelectedClip(); break;
                case 9: safe->crossfadeSelectedClip(); break;
                case 15: safe->bounceSelectedClip(); break;
                case 20: safe->timeStretchSelectedClip(0.50); break;
                case 21: safe->timeStretchSelectedClip(0.75); break;
                case 22: safe->timeStretchSelectedClip(1.25); break;
                case 23: safe->timeStretchSelectedClip(1.50); break;
                case 24: safe->timeStretchSelectedClip(2.00); break;
                case 25: safe->detectTransientsSelectedClip(); break;
                case 26: safe->autoWarpSelectedClip(); break;
                case 10: safe->trackNameEditor_.grabKeyboardFocus(); safe->trackNameEditor_.selectAll(); break;
                case 11: safe->checkpointUndo(); safe->addTrack(); break;
                case 12: safe->checkpointUndo(); safe->addMidiTrack(); break;
                case 13: safe->fitProject(); break;
                case 14: safe->resetWorkspace(); break;
                default: break;
            }
        });
}

void DawWorkspace::mouseMove(const juce::MouseEvent& e)
{
    const auto browser = browserBounds();
    const auto inspector = inspectorBounds();
    const auto mixer = mixerBounds();
    if (browser.contains(e.getPosition()) && e.y >= browser.getY() + 70 && e.y < browser.getY() + 70 + 7 * 31)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (std::abs(e.x - browser.getRight()) <= splitterSize_
        || std::abs(e.x - inspector.getX()) <= splitterSize_)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    if (std::abs(e.y - mixer.getY()) <= splitterSize_)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    if (const auto* c = clipAt(e.getPosition()))
    {
        const auto cb = clipBounds(*c);
        if (std::abs(static_cast<float>(e.x) - cb.getX()) <= 7.0f
            || std::abs(static_cast<float>(e.x) - cb.getRight()) <= 7.0f)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void DawWorkspace::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void DawWorkspace::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.y < toolbarHeight_) return;

    const auto tl = timelineBounds();
    const auto browser = browserBounds();
    const auto inspector = inspectorBounds();
    const auto mixer = mixerBounds();

    if (std::abs(e.x - browser.getRight()) <= splitterSize_ && e.y >= browser.getY() && e.y < browser.getBottom())
    {
        dragMode_ = DragMode::resizeBrowser;
        dragStartPoint_ = e.getPosition();
        dragStartBrowserWidth_ = browserWidth_;
        return;
    }
    if (std::abs(e.x - inspector.getX()) <= splitterSize_ && e.y >= inspector.getY() && e.y < inspector.getBottom())
    {
        dragMode_ = DragMode::resizeInspector;
        dragStartPoint_ = e.getPosition();
        dragStartInspectorWidth_ = inspectorWidth_;
        return;
    }
    if (std::abs(e.y - mixer.getY()) <= splitterSize_)
    {
        dragMode_ = DragMode::resizeMixer;
        dragStartPoint_ = e.getPosition();
        dragStartMixerHeight_ = mixerHeight_;
        return;
    }

    if (browser.contains(e.getPosition()))
    {
        const int item = (e.y - (browser.getY() + 70)) / 31;
        if (item >= 0 && item < 7)
        {
            selectedBrowserItem_ = item;
            repaint();
            switch (item)
            {
                case 0:
                case 1:
                    if (onOpenSetlist) onOpenSetlist();
                    break;
                case 2:
                    if (onOpenPads) onOpenPads();
                    break;
                case 3:
                    importButton_.triggerClick();
                    break;
                case 4:
                case 5:
                    if (onOpenPlugins) onOpenPlugins();
                    break;
                case 6:
                    openProjectInteractive();
                    break;
                default:
                    break;
            }
        }
        return;
    }

    if (mixer.contains(e.getPosition()))
    {
        auto content = mixer.reduced(8, 25);
        const int count = std::min(8, trackCount_);
        if (count > 0)
        {
            const int stripW = std::max(1, content.getWidth() / count);
            const int idx = juce::jlimit(0, count - 1, (e.x - content.getX()) / stripW);
            selectedTrack_ = idx;
            selectedClipId_ = -1;
            selectedMidiNoteId_ = -1;
            syncInspector();
            repaint();
        }
        return;
    }

    if (e.y >= tl.getY() && e.x >= tl.getX() && e.x < tl.getX() + headerWidth_)
    {
        const int t = trackAtY(e.y);
        if (t >= 0)
        {
            selectedTrack_ = t;
            selectedClipId_ = -1;
            selectedMidiNoteId_ = -1;

            const auto header = trackHeaderBounds(t).reduced(9, 7);
            const int chipStart = header.getRight() - 69;
            if (!e.mods.isPopupMenu() && e.x >= chipStart && e.y >= header.getY() + 25)
            {
                const int chip = juce::jlimit(0, 2, (e.x - chipStart) / 23);
                checkpointUndo();
                if (chip == 0) tracks_[t].mute = !tracks_[t].mute;
                else if (chip == 1) tracks_[t].solo = !tracks_[t].solo;
                else if (!tracks_[t].midi) tracks_[t].armed = !tracks_[t].armed;
                projectDirty_ = true;
                markRenderDirty();
            }

            syncInspector();
            repaint();
            if (e.mods.isPopupMenu())
                showContextMenu(e.getScreenPosition());
        }
        return;
    }

    if (rulerBounds().contains(e.getPosition()) && e.x >= tl.getX() + headerWidth_)
    {
        setTransportBeat(e.mods.isCtrlDown() ? beatAtX(static_cast<float>(e.x))
                                             : snapBeat(beatAtX(static_cast<float>(e.x))));
        repaint();
        return;
    }

    if (auto* n = midiNoteAt(e.getPosition()))
    {
        selectedMidiNoteId_ = n->id;
        selectedClipId_ = -1;
        selectedTrack_ = n->track;
        if (e.mods.isPopupMenu())
        {
            syncInspector(); repaint(); showContextMenu(e.getScreenPosition()); return;
        }
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

    if (auto* selected = clipAt(e.getPosition()))
    {
        selectedMidiNoteId_ = -1;
        selectedClipId_ = selected->id;
        selectedTrack_ = selected->track;
        if (e.mods.isPopupMenu())
        {
            syncInspector(); repaint(); showContextMenu(e.getScreenPosition()); return;
        }

        if (e.mods.isAltDown() && static_cast<int>(clips_.size()) < kMaxClips)
        {
            checkpointUndo();
            Clip copy = *selected;
            copy.id = nextClipId_++;
            clips_.push_back(copy);
            selectedClipId_ = copy.id;
            selected = clipAt({ -1, -1 });
        }

        dragStartPoint_ = e.getPosition();
        dragStartBeat_ = selected->startBeat;
        dragStartLength_ = selected->lengthBeats;
        dragStartOffsetSeconds_ = selected->sourceOffsetSeconds;
        dragStartTrack_ = selected->track;
        dragUndoSnapshot_ = e.mods.isAltDown() ? juce::String() : serializeProject();
        dragChanged_ = e.mods.isAltDown();

        const auto cb = clipBounds(*selected);
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
    if (e.mods.isPopupMenu())
        showContextMenu(e.getScreenPosition());
}

void DawWorkspace::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode_ == DragMode::none) return;

    if (dragMode_ == DragMode::resizeBrowser)
    {
        browserWidth_ = juce::jlimit(140, std::max(140, getWidth() / 3),
                                    dragStartBrowserWidth_ + e.x - dragStartPoint_.x);
        resized();
        return;
    }
    if (dragMode_ == DragMode::resizeInspector)
    {
        inspectorWidth_ = juce::jlimit(220, std::max(220, getWidth() / 3),
                                      dragStartInspectorWidth_ - (e.x - dragStartPoint_.x));
        resized();
        return;
    }
    if (dragMode_ == DragMode::resizeMixer)
    {
        mixerHeight_ = juce::jlimit(96, std::max(96, getHeight() / 2),
                                   dragStartMixerHeight_ - (e.y - dragStartPoint_.y));
        resized();
        return;
    }

    if (rejectStructuralEditWhileLive("mover o recortar clips"))
    {
        dragMode_ = DragMode::none;
        dragUndoSnapshot_.clear();
        dragChanged_ = false;
        return;
    }

    const double deltaBeat = static_cast<double>(e.x - dragStartPoint_.x) / pixelsPerBeat();
    const auto quantize = [this, &e](double beat)
    {
        return e.mods.isCtrlDown() ? std::max(0.0, beat) : snapBeat(beat);
    };

    if (dragMode_ == DragMode::midiMove || dragMode_ == DragMode::midiResize)
    {
        for (auto& n : midiNotes_)
        {
            if (n.id != selectedMidiNoteId_) continue;
            if (dragMode_ == DragMode::midiMove)
            {
                n.startBeat = quantize(dragStartBeat_ + deltaBeat);
                const int newTrack = trackAtY(e.y);
                if (newTrack >= 0 && tracks_[newTrack].midi)
                    n.track = newTrack;
                n.note = midiPitchAtY(n.track, e.y);
            }
            else
            {
                const double raw = dragStartLength_ + deltaBeat;
                n.lengthBeats = std::max(0.125, e.mods.isCtrlDown() ? raw : snapBeat(raw));
            }
            break;
        }
    }
    else
    {
        auto* clip = clipAt({ -1, -1 });
        if (clip == nullptr) return;
        const int newTrack = trackAtY(e.y);
        if (dragMode_ == DragMode::move)
        {
            clip->startBeat = quantize(dragStartBeat_ + deltaBeat);
            if (newTrack >= 0 && !tracks_[newTrack].midi) clip->track = newTrack;
        }
        else if (dragMode_ == DragMode::trimRight)
        {
            const double raw = dragStartLength_ + deltaBeat;
            const double nextLength = std::max(e.mods.isCtrlDown() ? 0.05 : (snapBeats_ > 0.0 ? snapBeats_ : 0.05),
                                               e.mods.isCtrlDown() ? raw : snapBeat(raw));
            clip->lengthBeats = nextLength;
            if (clip->reversed)
            {
                const double removedBeats = dragStartLength_ - nextLength;
                clip->sourceOffsetSeconds = std::max(0.0,
                    dragStartOffsetSeconds_ + removedBeats * 60.0 / std::max(1.0, bpm()));
            }
        }
        else if (dragMode_ == DragMode::trimLeft)
        {
            const double oldEnd = dragStartBeat_ + dragStartLength_;
            double newStart = quantize(dragStartBeat_ + deltaBeat);
            newStart = juce::jlimit(0.0, oldEnd - 0.05, newStart);
            const double shiftedBeats = newStart - dragStartBeat_;
            clip->startBeat = newStart;
            clip->lengthBeats = oldEnd - newStart;
            clip->sourceOffsetSeconds = clip->reversed
                ? dragStartOffsetSeconds_
                : std::max(0.0, dragStartOffsetSeconds_ + shiftedBeats * 60.0 / std::max(1.0, bpm()));
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

    const bool resizedWorkspace = dragMode_ == DragMode::resizeBrowser
                               || dragMode_ == DragMode::resizeInspector
                               || dragMode_ == DragMode::resizeMixer;
    dragMode_ = DragMode::none;
    dragUndoSnapshot_.clear();
    dragChanged_ = false;
    if (resizedWorkspace) saveWorkspaceState();
}

void DawWorkspace::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto tl = timelineBounds();
    if (liveSessionActive() && tl.contains(e.getPosition()))
    {
        rejectStructuralEditWhileLive("editar el arreglo");
        return;
    }
    if (e.y >= tl.getY() && e.x >= tl.getX() && e.x < tl.getX() + headerWidth_)
    {
        const int track = trackAtY(e.y);
        if (track >= 0)
        {
            selectedTrack_ = track;
            syncInspector();
            trackNameEditor_.grabKeyboardFocus();
            trackNameEditor_.selectAll();
        }
        return;
    }

    if (e.x >= tl.getX() + headerWidth_ && e.x < tl.getRight() && e.y >= tl.getY() && e.y < tl.getBottom())
    {
        const int track = trackAtY(e.y);
        if (track >= 0)
        {
            const double beat = e.mods.isCtrlDown() ? beatAtX(static_cast<float>(e.x))
                                                    : snapBeat(beatAtX(static_cast<float>(e.x)));
            if (tracks_[track].midi)
            {
                checkpointUndo();
                addMidiNote(track, beat, midiPitchAtY(track, e.y),
                            snapBeats_ > 0.0 ? std::max(0.25, snapBeats_) : 1.0);
                return;
            }

            if (const auto* c = clipAt(e.getPosition()); c != nullptr)
            {
                selectedClipId_ = c->id;
                selectedTrack_ = c->track;
                syncInspector();
                fitSelection();
                openSelectedClipEditor();
                refreshStatus(juce::String::fromUTF8("AUDIO EDITOR · Mixer Insert + FX"));
                return;
            }

            chooser_ = std::make_unique<juce::FileChooser>(
                "Importar audio", juce::File{}, "*.wav;*.mp3;*.flac;*.aif;*.aiff");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                [this, track, beat](const juce::FileChooser& chooser)
                {
                    juce::StringArray paths;
                    for (const auto& file : chooser.getResults()) paths.add(file.getFullPathName());
                    if (!paths.isEmpty()) importFiles(paths, track, beat);
                });
        }
    }
}

void DawWorkspace::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const auto tl = timelineBounds();
        const double before = beatAtX(static_cast<float>(e.x));
        const double nextZoom = juce::jlimit(0.5, 4.0, zoom_ * (1.0 + static_cast<double>(wheel.deltaY) * 0.55));
        if (std::abs(nextZoom - zoom_) > 0.0001)
        {
            zoom_ = nextZoom;
            const double cursorPx = static_cast<double>(e.x - (tl.getX() + headerWidth_));
            viewStartBeat_ = std::max(0.0, before - cursorPx / pixelsPerBeat());
            zoomSlider_.setValue(zoom_, juce::dontSendNotification);
        }
    }
    else if (e.mods.isAltDown())
    {
        const int oldHeight = trackHeight_;
        trackHeight_ = juce::jlimit(44, 150, trackHeight_ + (wheel.deltaY > 0.0f ? 6 : -6));
        if (trackHeight_ != oldHeight) resized();
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
    if (mods.isCommandDown() && code == 'B') { bounceSelectedClip(); return true; }
    if (mods.isCommandDown() && mods.isShiftDown() && code == 'F') { crossfadeSelectedClip(); return true; }
    if (mods.isCommandDown() && mods.isAltDown() && code == 'W') { autoWarpSelectedClip(); return true; }
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

void DawWorkspace::openSelectedClipEditor()
{
    Clip* selected = nullptr;
    for (auto& clip : clips_)
    {
        if (clip.id == selectedClipId_)
        {
            selected = &clip;
            break;
        }
    }
    if (selected == nullptr || selected->audio == nullptr)
        return;

    const int clipId = selected->id;
    const int insert = juce::jlimit(0, kMaxTracks - 1, selected->mixerInsert);
    const auto clipName = juce::File(selected->audio->path).getFileName();
    auto safe = juce::Component::SafePointer<DawWorkspace>(this);

    auto editor = std::make_unique<AudioClipRoutingEditor>(
        clipName,
        selected->audio->path,
        insert,
        gainToDb(selected->gain),
        selected->pan,
        selected->startBeat,
        selected->lengthBeats,
        selected->audio->durationSeconds,
        selected->fadeInBeats,
        selected->fadeOutBeats,
        selected->reversed,
        [safe, clipId](int newInsert)
        {
            if (safe == nullptr)
                return;
            if (safe->rejectStructuralEditWhileLive("cambiar el ruteo del clip"))
                return;
            for (auto& clip : safe->clips_)
            {
                if (clip.id != clipId)
                    continue;
                newInsert = juce::jlimit(0, kMaxTracks - 1, newInsert);
                if (clip.mixerInsert == newInsert)
                    return;
                safe->checkpointUndo();
                clip.mixerInsert = newInsert;
                safe->projectDirty_ = true;
                safe->markRenderDirty();
                safe->rebuildRenderState();
                safe->syncInspector();
                if (safe->onMixerRoutingChanged) safe->onMixerRoutingChanged();
                safe->repaint();
                safe->refreshStatus("Mixer Insert " + juce::String(newInsert + 1));
                return;
            }
        },
        [safe, clipId](float db)
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("editar el gain del clip")) return;
            for (auto& clip : safe->clips_)
                if (clip.id == clipId)
                {
                    safe->checkpointUndo();
                    clip.gain = dbToGain(db);
                    safe->projectDirty_ = true;
                    safe->markRenderDirty();
                    safe->rebuildRenderState();
                    safe->syncInspector();
                    safe->repaint();
                    return;
                }
        },
        [safe, clipId](float pan)
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("editar el pan del clip")) return;
            for (auto& clip : safe->clips_)
                if (clip.id == clipId)
                {
                    clip.pan = juce::jlimit(-1.0f, 1.0f, pan);
                    safe->projectDirty_ = true;
                    safe->markRenderDirty();
                    safe->rebuildRenderState();
                    safe->syncInspector();
                    safe->repaint();
                    return;
                }
        },
        [safe, clipId](double beats)
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("editar fades del clip")) return;
            for (auto& clip : safe->clips_)
                if (clip.id == clipId)
                {
                    clip.fadeInBeats = juce::jlimit(0.0, clip.lengthBeats, beats);
                    safe->projectDirty_ = true;
                    safe->markRenderDirty();
                    safe->rebuildRenderState();
                    safe->syncInspector();
                    safe->repaint();
                    return;
                }
        },
        [safe, clipId](double beats)
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("editar fades del clip")) return;
            for (auto& clip : safe->clips_)
                if (clip.id == clipId)
                {
                    clip.fadeOutBeats = juce::jlimit(0.0, clip.lengthBeats, beats);
                    safe->projectDirty_ = true;
                    safe->markRenderDirty();
                    safe->rebuildRenderState();
                    safe->syncInspector();
                    safe->repaint();
                    return;
                }
        },
        [safe, clipId](bool shouldReverse)
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("hacer reverse")) return;
            for (auto& clip : safe->clips_)
                if (clip.id == clipId)
                {
                    safe->checkpointUndo();
                    clip.reversed = shouldReverse;
                    safe->projectDirty_ = true;
                    safe->markRenderDirty();
                    safe->rebuildRenderState();
                    safe->syncInspector();
                    safe->repaint();
                    return;
                }
        },
        [safe, clipId]
        {
            if (safe == nullptr) return;
            safe->selectedClipId_ = clipId;
            safe->normalizeSelectedClip();
        },
        [safe, clipId]
        {
            if (safe == nullptr) return;
            if (safe->rejectStructuralEditWhileLive("reemplazar audio")) return;
            safe->chooser_ = std::make_unique<juce::FileChooser>(
                "Replace audio", juce::File{}, "*.wav;*.mp3;*.flac;*.aif;*.aiff");
            safe->chooser_->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, clipId](const juce::FileChooser& chooser)
                {
                    if (safe == nullptr) return;
                    if (safe->rejectStructuralEditWhileLive("reemplazar audio")) return;
                    const auto file = chooser.getResult();
                    if (!file.existsAsFile()) return;
                    juce::String error;
                    auto* audio = safe->loadAudioFile(file, error);
                    if (audio == nullptr)
                    {
                        safe->refreshStatus(error);
                        return;
                    }
                    for (auto& clip : safe->clips_)
                        if (clip.id == clipId)
                        {
                            safe->checkpointUndo();
                            clip.audio = audio;
                            clip.sourceOffsetSeconds = 0.0;
                            clip.lengthBeats = std::max(0.05, audio->durationSeconds * safe->bpm() / 60.0);
                            clip.fadeInBeats = std::min(clip.fadeInBeats, clip.lengthBeats);
                            clip.fadeOutBeats = std::min(clip.fadeOutBeats, clip.lengthBeats);
                            safe->projectDirty_ = true;
                            safe->markRenderDirty();
                            safe->rebuildRenderState();
                            safe->syncInspector();
                            safe->autosaveRecovery();
                            safe->repaint();
                            safe->refreshStatus("Audio replaced: " + file.getFileName());
                            return;
                        }
                });
        },
        [safe, clipId]
        {
            if (safe == nullptr) return;
            for (const auto& clip : safe->clips_)
                if (clip.id == clipId && clip.audio != nullptr)
                {
                    juce::File(clip.audio->path).revealToUser();
                    return;
                }
        },
        [safe](int targetInsert)
        {
            if (safe != nullptr && safe->onOpenMixerInsert)
                safe->onOpenMixerInsert(juce::jlimit(0, kMaxTracks - 1, targetInsert));
        },
        [safe](int targetInsert)
        {
            if (safe != nullptr && safe->onOpenPluginsForInsert)
                safe->onOpenPluginsForInsert(juce::jlimit(0, kMaxTracks - 1, targetInsert));
        });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor.release());
    options.dialogTitle = juce::String::fromUTF8("J3 Worship · Audio Clip");
    options.dialogBackgroundColour = juce::Colour(0xff0b151e);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void DawWorkspace::addTrack()
{
    if (rejectStructuralEditWhileLive("agregar pistas")) return;
    if (trackCount_ >= kMaxTracks)
    {
        refreshStatus(juce::String::fromUTF8("Máximo de 48 pistas alcanzado"));
        return;
    }
    const int i = trackCount_++;
    tracks_[i] = {};
    tracks_[i].name = "Pista " + juce::String(i + 1);
    tracks_[i].colour = trackColour(i);
    tracks_[i].mixerInsert = i;
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
    if (rejectStructuralEditWhileLive("agregar pistas MIDI")) return;
    if (trackCount_ >= kMaxTracks)
    {
        refreshStatus(juce::String::fromUTF8("Máximo de 48 pistas alcanzado"));
        return;
    }
    const int i = trackCount_++;
    tracks_[i] = {};
    tracks_[i].midi = true;
    tracks_[i].name = "MIDI " + juce::String(i + 1);
    tracks_[i].colour = trackColour(i);
    tracks_[i].mixerInsert = i;
    selectedTrack_ = i;
    selectedClipId_ = -1;
    selectedMidiNoteId_ = -1;
    const int visible = std::max(1, timelineBounds().getHeight() / std::max(1, trackHeight_));
    firstVisibleTrack_ = std::max(0, i - visible + 1);
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus(juce::String::fromUTF8("Pista MIDI creada · doble clic para dibujar notas"));
}

void DawWorkspace::addMidiNote(int track, double startBeat, int note, double lengthBeats, float velocity)
{
    if (rejectStructuralEditWhileLive("editar MIDI")) return;
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
    if (rejectStructuralEditWhileLive("crear patrones")) return;
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
    refreshStatus(juce::String::fromUTF8("Pattern MIDI de 16 pasos creado · editable en el piano roll"));
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
    if (rejectStructuralEditWhileLive("importar audio")) return;
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
            lastError = juce::String::fromUTF8("Máximo de ") + juce::String(kMaxClips) + " clips alcanzado";
            break;
        }

        Clip clip;
        clip.id = nextClipId_++;
        clip.track = track;
        clip.startBeat = cursor;
        clip.lengthBeats = std::max(0.25, data->durationSeconds * bpm() / 60.0);
        clip.mixerInsert = tracks_[track].mixerInsert;
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
        rebuildRenderState();
        syncInspector();
        if (onMixerRoutingChanged) onMixerRoutingChanged();
        repaint();
        refreshStatus(juce::String(imported) + (imported == 1 ? " clip importado" : " clips importados"));
    }
    else if (lastError.isNotEmpty())
        refreshStatus(lastError);
}

void DawWorkspace::deleteSelectedClip()
{
    if (rejectStructuralEditWhileLive("borrar clips")) return;
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
    if (rejectStructuralEditWhileLive("duplicar clips")) return;
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
    if (rejectStructuralEditWhileLive("dividir clips")) return;
    const double playBeat = (static_cast<double>(transportSamples_.load(std::memory_order_relaxed))
        / std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed))) * bpm() / 60.0;
    for (auto& c : clips_)
    {
        if (c.id != selectedClipId_) continue;
        if (playBeat <= c.startBeat + 0.02 || playBeat >= c.startBeat + c.lengthBeats - 0.02) return;
        if (static_cast<int>(clips_.size()) >= kMaxClips) return;

        checkpointUndo();
        const double leftBeats = playBeat - c.startBeat;
        const double originalLengthBeats = c.lengthBeats;
        const double originalOffsetSeconds = c.sourceOffsetSeconds;
        const double secondsPerBeat = 60.0 / std::max(1.0, bpm());
        Clip right = c;
        right.id = nextClipId_++;
        right.startBeat = playBeat;
        right.lengthBeats = originalLengthBeats - leftBeats;
        if (c.reversed)
        {
            c.sourceOffsetSeconds = std::max(0.0,
                originalOffsetSeconds + right.lengthBeats * secondsPerBeat);
            right.sourceOffsetSeconds = originalOffsetSeconds;
        }
        else
        {
            right.sourceOffsetSeconds = originalOffsetSeconds + leftBeats * secondsPerBeat;
        }
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

void DawWorkspace::normalizeSelectedClip()
{
    if (rejectStructuralEditWhileLive("normalizar clips")) return;
    for (auto& clip : clips_)
    {
        if (clip.id != selectedClipId_ || clip.audio == nullptr)
            continue;

        const auto& audio = clip.audio->samples;
        const int totalSamples = audio.getNumSamples();
        if (totalSamples <= 0)
            return;

        const int start = juce::jlimit(0, totalSamples - 1,
            static_cast<int>(std::llround(clip.sourceOffsetSeconds * clip.audio->sampleRate)));
        const double seconds = clip.lengthBeats * 60.0 / std::max(1.0, bpm());
        const int wanted = std::max(1, static_cast<int>(std::llround(seconds * clip.audio->sampleRate)));
        const int count = std::max(1, std::min(wanted, totalSamples - start));

        float peak = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            peak = std::max(peak, audio.getMagnitude(ch, start, count));

        if (peak <= 1.0e-7f)
        {
            refreshStatus(juce::String::fromUTF8("Normalize: el clip no contiene señal útil."));
            return;
        }

        checkpointUndo();
        constexpr float targetPeak = 0.89125094f; // -1 dBFS
        const float normalizedGain = targetPeak / peak;
        const float minGain = dbToGain(-36.0);
        const float maxGain = dbToGain(18.0);
        clip.gain = juce::jlimit(minGain, maxGain, normalizedGain);
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
        refreshStatus(juce::String::fromUTF8("Clip normalizado a -1 dBFS de pico · no destructivo"));
        return;
    }
}

void DawWorkspace::reverseSelectedClip()
{
    if (rejectStructuralEditWhileLive("editar clips")) return;
    for (auto& clip : clips_)
    {
        if (clip.id != selectedClipId_)
            continue;

        checkpointUndo();
        clip.reversed = !clip.reversed;
        projectDirty_ = true;
        markRenderDirty();
        syncInspector();
        repaint();
        refreshStatus(clip.reversed ? juce::String::fromUTF8("Reverse activado · no destructivo")
                                    : "Reverse desactivado");
        return;
    }
}

void DawWorkspace::crossfadeSelectedClip()
{
    if (rejectStructuralEditWhileLive("hacer crossfade")) return;
    auto* selected = clipAt({ -1, -1 });
    if (selected == nullptr)
    {
        refreshStatus(juce::String::fromUTF8("Crossfade: seleccioná un clip de audio."));
        return;
    }

    Clip* partner = nullptr;
    double bestOverlap = 0.0;
    for (auto& candidate : clips_)
    {
        if (candidate.id == selected->id || candidate.track != selected->track)
            continue;

        Clip* earlier = selected->startBeat <= candidate.startBeat ? selected : &candidate;
        Clip* later = earlier == selected ? &candidate : selected;
        const double earlierEnd = earlier->startBeat + earlier->lengthBeats;
        const double laterEnd = later->startBeat + later->lengthBeats;

        // A conventional crossfade is the overlap at the hand-off from the earlier
        // clip to the later clip. Ignore a clip fully nested inside another because
        // its fade-out would occur before the earlier clip actually ends.
        if (later->startBeat >= earlierEnd || laterEnd + 1.0e-6 < earlierEnd)
            continue;

        const double overlap = earlierEnd - later->startBeat;
        if (overlap > bestOverlap + 1.0e-6)
        {
            bestOverlap = overlap;
            partner = &candidate;
        }
    }

    if (partner == nullptr || bestOverlap <= 0.0)
    {
        refreshStatus("Crossfade: el clip no se solapa con otro clip de la misma pista.");
        return;
    }

    checkpointUndo();
    Clip* earlier = selected->startBeat <= partner->startBeat ? selected : partner;
    Clip* later = earlier == selected ? partner : selected;
    const double overlap = juce::jlimit(0.0,
        std::min(earlier->lengthBeats, later->lengthBeats), bestOverlap);
    earlier->fadeOutBeats = std::max(earlier->fadeOutBeats, overlap);
    later->fadeInBeats = std::max(later->fadeInBeats, overlap);
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus(juce::String::fromUTF8("Crossfade equal-power aplicado · ") + juce::String(overlap, 2) + " beats");
}

void DawWorkspace::bounceSelectedClip()
{
    if (rejectStructuralEditWhileLive("hacer bounce")) return;
    auto* clip = clipAt({ -1, -1 });
    if (clip == nullptr || clip->audio == nullptr)
    {
        refreshStatus(juce::String::fromUTF8("Bounce: seleccioná un clip de audio."));
        return;
    }

    const auto& source = clip->audio->samples;
    const int sourceSamples = source.getNumSamples();
    const int channels = juce::jlimit(1, 2, source.getNumChannels());
    const double sourceRate = std::max(1.0, clip->audio->sampleRate);
    const double durationSeconds = clip->lengthBeats * 60.0 / std::max(1.0, bpm());
    const auto outputSamples64 = static_cast<std::int64_t>(std::llround(durationSeconds * sourceRate));
    if (sourceSamples <= 0 || outputSamples64 <= 0
        || outputSamples64 > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
    {
        refreshStatus(juce::String::fromUTF8("Bounce: duración de clip inválida o demasiado grande."));
        return;
    }
    const int outputSamples = static_cast<int>(outputSamples64);

    juce::AudioBuffer<float> rendered(channels, outputSamples);
    rendered.clear();
    const double sourceOffset = clip->sourceOffsetSeconds * sourceRate;
    for (int sample = 0; sample < outputSamples; ++sample)
    {
        const int mappedLocal = clip->reversed ? (outputSamples - 1 - sample) : sample;
        double sourcePosition = sourceOffset + static_cast<double>(mappedLocal);
        if (clip->loop)
        {
            sourcePosition = std::fmod(sourcePosition, static_cast<double>(sourceSamples));
            if (sourcePosition < 0.0) sourcePosition += sourceSamples;
        }
        else if (sourcePosition < 0.0 || sourcePosition >= sourceSamples - 1)
        {
            continue;
        }

        const int i0 = juce::jlimit(0, sourceSamples - 1, static_cast<int>(sourcePosition));
        const int i1 = std::min(sourceSamples - 1, i0 + 1);
        const float frac = static_cast<float>(sourcePosition - i0);
        float envelope = 1.0f;
        const double beatAtSample = static_cast<double>(sample) / sourceRate * bpm() / 60.0;
        if (clip->fadeInBeats > 0.0 && beatAtSample < clip->fadeInBeats)
            envelope *= equalPowerFade(beatAtSample / clip->fadeInBeats);
        const double remainingBeats = clip->lengthBeats - beatAtSample;
        if (clip->fadeOutBeats > 0.0 && remainingBeats < clip->fadeOutBeats)
            envelope *= equalPowerFade(remainingBeats / clip->fadeOutBeats);

        for (int ch = 0; ch < channels; ++ch)
        {
            const int sourceCh = std::min(ch, source.getNumChannels() - 1);
            const float a = source.getSample(sourceCh, i0);
            const float b = source.getSample(sourceCh, i1);
            rendered.setSample(ch, sample, (a + (b - a) * frac) * clip->gain * envelope);
        }
    }

    auto root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship").getChildFile("DAW Bounces");
    root.createDirectory();
    const auto sourceName = juce::File(clip->audio->path).getFileNameWithoutExtension();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto fileName = juce::File::createLegalFileName(
        (sourceName.isNotEmpty() ? sourceName : "Clip") + "-bounce-" + stamp
        + "-" + juce::String(clip->id) + ".wav");
    const auto file = root.getChildFile(fileName);

    if (!writeFloatWav(file, rendered, sourceRate))
    {
        refreshStatus("Bounce: no se pudo escribir el WAV.");
        return;
    }

    juce::String error;
    auto* bouncedAudio = loadAudioFile(file, error);
    if (bouncedAudio == nullptr)
    {
        refreshStatus("Bounce: " + error);
        return;
    }

    checkpointUndo();
    clip->audio = bouncedAudio;
    clip->sourceOffsetSeconds = 0.0;
    clip->gain = 1.0f;
    clip->loop = false;
    clip->reversed = false;
    clip->fadeInBeats = 0.0;
    clip->fadeOutBeats = 0.0;
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus(juce::String::fromUTF8("Bounce in place listo · ") + file.getFileName());
}

void DawWorkspace::detectTransientsSelectedClip()
{
    if (rejectStructuralEditWhileLive("analizar transientes")) return;
    auto* clip = clipAt({ -1, -1 });
    if (clip == nullptr || clip->audio == nullptr)
    {
        refreshStatus(juce::String::fromUTF8("Transientes: seleccioná un clip de audio."));
        return;
    }

    auto& audio = *clip->audio;
    audio.transientSamples = detectTransientSamples(audio.samples, audio.sampleRate);
    audio.transientsAnalyzed = true;
    repaint();

    refreshStatus(juce::String::fromUTF8("Transientes listos · ")
        + juce::String(static_cast<int>(audio.transientSamples.size()))
        + L" Warp Markers detectados");
}

void DawWorkspace::timeStretchSelectedClip(double factor)
{
    if (rejectStructuralEditWhileLive("estirar audio")) return;
    factor = juce::jlimit(0.5, 2.0, factor);
    auto* clip = clipAt({ -1, -1 });
    if (clip == nullptr || clip->audio == nullptr)
    {
        refreshStatus(juce::String::fromUTF8("Time Stretch: seleccioná un clip de audio."));
        return;
    }

    const auto& source = clip->audio->samples;
    const int sourceSamples = source.getNumSamples();
    const int channels = source.getNumChannels();
    const double sourceRate = std::max(1.0, clip->audio->sampleRate);
    const double durationSeconds = clip->lengthBeats * 60.0 / std::max(1.0, bpm());
    const auto inputSamples64 = static_cast<std::int64_t>(std::llround(durationSeconds * sourceRate));
    if (sourceSamples <= 0 || channels <= 0 || inputSamples64 < 128
        || inputSamples64 > std::numeric_limits<int>::max())
    {
        refreshStatus(juce::String::fromUTF8("Time Stretch: el clip es demasiado corto o inválido."));
        return;
    }

    const auto outputSamples64 = static_cast<std::int64_t>(std::llround(inputSamples64 * factor));
    const std::int64_t estimatedBytes = outputSamples64 * channels * static_cast<std::int64_t>(sizeof(float));
    if (outputSamples64 <= 0 || outputSamples64 > std::numeric_limits<int>::max()
        || estimatedBytes > 512LL * 1024LL * 1024LL)
    {
        refreshStatus(juce::String::fromUTF8("Time Stretch: el resultado sería demasiado grande. Dividí el clip y procesalo por partes."));
        return;
    }

    const int inputSamples = static_cast<int>(inputSamples64);
    juce::AudioBuffer<float> current(channels, inputSamples);
    current.clear();
    const double sourceOffset = clip->sourceOffsetSeconds * sourceRate;
    for (int sample = 0; sample < inputSamples; ++sample)
    {
        const int mappedLocal = clip->reversed ? (inputSamples - 1 - sample) : sample;
        double sourcePosition = sourceOffset + static_cast<double>(mappedLocal);
        if (clip->loop)
        {
            sourcePosition = std::fmod(sourcePosition, static_cast<double>(sourceSamples));
            if (sourcePosition < 0.0) sourcePosition += sourceSamples;
        }
        else if (sourcePosition < 0.0 || sourcePosition >= sourceSamples - 1)
        {
            continue;
        }

        const int i0 = juce::jlimit(0, sourceSamples - 1, static_cast<int>(sourcePosition));
        const int i1 = std::min(sourceSamples - 1, i0 + 1);
        const float frac = static_cast<float>(sourcePosition - i0);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float a = source.getSample(ch, i0);
            const float b = source.getSample(ch, i1);
            current.setSample(ch, sample, a + (b - a) * frac);
        }
    }

    refreshStatus(juce::String::fromUTF8("Time Stretch · procesando con PITCH LOCK…"));
    auto stretched = stretchWsola(current, factor, sourceRate);
    if (!stretched.has_value())
    {
        refreshStatus(L"Time Stretch: no se pudo procesar el clip.");
        return;
    }

    auto root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("J3 Worship").getChildFile("DAW Warps");
    root.createDirectory();
    const auto sourceName = juce::File(clip->audio->path).getFileNameWithoutExtension();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto percent = static_cast<int>(std::llround(factor * 100.0));
    const auto fileName = juce::File::createLegalFileName(
        (sourceName.isNotEmpty() ? sourceName : "Clip") + "-stretch-"
        + juce::String(percent) + "-" + stamp + "-" + juce::String(clip->id) + ".wav");
    const auto file = root.getChildFile(fileName);

    if (!writeFloatWav(file, *stretched, sourceRate))
    {
        refreshStatus(L"Time Stretch: no se pudo escribir el WAV procesado.");
        return;
    }

    juce::String error;
    auto* stretchedAudio = loadAudioFile(file, error);
    if (stretchedAudio == nullptr)
    {
        refreshStatus(L"Time Stretch: " + error);
        return;
    }

    checkpointUndo();
    clip->audio = stretchedAudio;
    clip->sourceOffsetSeconds = 0.0;
    clip->loop = false;
    clip->reversed = false;
    clip->lengthBeats = std::max(0.05, clip->lengthBeats * factor);
    projectDirty_ = true;
    markRenderDirty();
    syncInspector();
    repaint();
    refreshStatus(juce::String::fromUTF8("Time Stretch PITCH LOCK listo · ") + juce::String(percent) + "%");
}

void DawWorkspace::autoWarpSelectedClip()
{
    if (rejectStructuralEditWhileLive("usar Auto Warp")) return;
    auto* clip = clipAt({ -1, -1 });
    if (clip == nullptr || clip->audio == nullptr)
    {
        refreshStatus(juce::String::fromUTF8("Auto Warp: seleccioná un clip de audio."));
        return;
    }

    auto& audio = *clip->audio;
    if (!audio.transientsAnalyzed)
    {
        audio.transientSamples = detectTransientSamples(audio.samples, audio.sampleRate);
        audio.transientsAnalyzed = true;
    }

    const double sourceStart = clip->sourceOffsetSeconds * audio.sampleRate;
    const double sourceSpan = clip->lengthBeats * 60.0 / std::max(1.0, bpm()) * audio.sampleRate;
    std::vector<int> local;
    local.reserve(audio.transientSamples.size());
    for (const int sample : audio.transientSamples)
    {
        if (sample >= sourceStart && sample <= sourceStart + sourceSpan)
            local.push_back(static_cast<int>(sample - sourceStart));
    }
    if (local.size() < 3)
        local = audio.transientSamples;

    const auto sourceTempo = estimateTempoFromTransients(local, audio.sampleRate);
    if (!sourceTempo.has_value())
    {
        refreshStatus(L"Auto Warp: no hay suficientes transientes claros para estimar el BPM.");
        repaint();
        return;
    }

    const double projectTempo = std::max(1.0, bpm());
    const double factor = *sourceTempo / projectTempo;
    if (factor < 0.5 || factor > 2.0)
    {
        refreshStatus(juce::String::fromUTF8("Auto Warp: el cambio requerido supera el rango seguro 50–200%."));
        repaint();
        return;
    }

    auto* before = clip->audio;
    const int clipId = clip->id;
    timeStretchSelectedClip(factor);
    for (auto& candidate : clips_)
    {
        if (candidate.id != clipId) continue;
        if (candidate.audio != before)
        {
            refreshStatus(juce::String::fromUTF8("AUTO WARP listo · detectado ")
                + juce::String(*sourceTempo, 1) + " BPM → proyecto "
                + juce::String(projectTempo, 1) + " BPM · PITCH LOCK");
        }
        break;
    }
}

void DawWorkspace::togglePlay()
{
    if (liveSessionActive())
    {
        stopLiveClips();
        return;
    }

    const bool next = !playing_.load(std::memory_order_acquire);
    if (next)
    {
        if (renderDirty_.load(std::memory_order_acquire))
            rebuildRenderState();
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
    clearLiveState(false);
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

    if (recordArmedCount_ <= 0)
    {
        refreshStatus(juce::String::fromUTF8("Armá al menos una pista de audio para grabar."));
        return false;
    }

    const auto sr = std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed));
    const auto now = juce::Time::getCurrentTime();
    currentTakeDirectory_ = trackRecordingRoot()
        .getChildFile(now.formatted("%Y-%m-%d"))
        .getChildFile(now.formatted("%H%M%S-DAW"));
    if (!currentTakeDirectory_.createDirectory())
    {
        refreshStatus(juce::String::fromUTF8("No se pudo crear la carpeta de grabación DAW."));
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
    refreshStatus(juce::String::fromUTF8("REC DAW activo · ") + juce::String(recordArmedCount_)
        + juce::String::fromUTF8(" pista(s) armada(s) · entradas activas en orden"));
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
        refreshStatus(juce::String::fromUTF8("La grabación terminó con error: ") + juce::String(error));
        return;
    }

    if (importTake)
        importRecordedTake();
    else
        refreshStatus(juce::String::fromUTF8("Grabación DAW detenida."));
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
            lastError = juce::String::fromUTF8("Máximo de 512 clips alcanzado");
            break;
        }

        const int track = juce::jlimit(0, trackCount_ - 1, recordTrackMap_[i]);
        Clip clip;
        clip.id = nextClipId_++;
        clip.track = track;
        clip.startBeat = std::max(0.0, recordStartBeat_);
        clip.lengthBeats = std::max(0.25, data->durationSeconds * bpm() / 60.0);
        clip.mixerInsert = tracks_[track].mixerInsert;
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
        refreshStatus(juce::String::fromUTF8("Toma DAW importada · ") + juce::String(imported) + " clip(s)");
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
    if (selectedTrack_ != lastNotifiedTrack_)
    {
        lastNotifiedTrack_ = selectedTrack_;
        if (onSelectedTrackChanged) onSelectedTrackChanged(selectedTrack_);
    }
    auto& t = tracks_[selectedTrack_];
    trackNameEditor_.setText(t.name, juce::dontSendNotification);
    trackVolumeSlider_.setValue(gainToDb(t.gain), juce::dontSendNotification);
    trackPanSlider_.setValue(t.pan, juce::dontSendNotification);
    trackMuteButton_.setToggleState(t.mute, juce::dontSendNotification);
    trackSoloButton_.setToggleState(t.solo, juce::dontSendNotification);
    if (t.midi) t.armed = false;
    trackArmButton_.setToggleState(t.armed, juce::dontSendNotification);
    trackArmButton_.setEnabled(!t.midi);

    const auto* c = clipAt({ -1, -1 });
    const bool clipSelected = c != nullptr;
    clipGainSlider_.setEnabled(clipSelected);
    clipMuteButton_.setEnabled(clipSelected);
    clipLoopButton_.setEnabled(clipSelected);
    fadeInSlider_.setEnabled(clipSelected);
    fadeOutSlider_.setEnabled(clipSelected);
    clipMixerBox_.setEnabled(clipSelected || t.midi);
    openMixerInsertButton_.setEnabled(clipSelected || t.midi);
    openPluginsInsertButton_.setEnabled(clipSelected || t.midi);
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
        clipMixerBox_.setSelectedId(juce::jlimit(0, kMaxTracks - 1, c->mixerInsert) + 1, juce::dontSendNotification);
    }
    else
    {
        clipMixerBox_.setSelectedId(juce::jlimit(0, kMaxTracks - 1, t.mixerInsert) + 1, juce::dontSendNotification);
    }
}

juce::String DawWorkspace::mixerInsertName(int insert) const
{
    insert = juce::jlimit(0, kMaxTracks - 1, insert);
    juce::StringArray names;

    for (const auto& clip : clips_)
    {
        if (clip.mixerInsert != insert || clip.track < 0 || clip.track >= trackCount_)
            continue;
        names.addIfNotAlreadyThere(tracks_[clip.track].name);
    }
    for (int track = 0; track < trackCount_; ++track)
    {
        if (tracks_[track].midi && tracks_[track].mixerInsert == insert)
            names.addIfNotAlreadyThere(tracks_[track].name);
    }

    if (names.isEmpty())
        return {};
    if (names.size() == 1)
        return names[0];
    return names[0] + " +" + juce::String(names.size() - 1);
}

void DawWorkspace::refreshStatus(const juce::String& text)
{
    statusLabel_.setText(text, juce::dontSendNotification);
}

bool DawWorkspace::rejectStructuralEditWhileLive(const juce::String& action)
{
    if (!liveSessionActive())
        return false;

    refreshStatus(juce::String::fromUTF8("LIVE LOCK · STOP CLIPS antes de ")
        + action + juce::String::fromUTF8(". Mixer, FX, mute/solo y controles LIVE siguen disponibles."));
    return true;
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
        if (state.clipCount >= kMaxClips) break;
        if (c.audio == nullptr) continue;
        auto& rc = state.clips[state.clipCount++];
        rc.audio = c.audio;
        rc.id = c.id;
        rc.track = c.track;
        rc.startSample = static_cast<std::int64_t>(std::llround(c.startBeat * secondsPerBeat * state.sampleRate));
        rc.lengthSamples = std::max<std::int64_t>(1, static_cast<std::int64_t>(
            std::llround(c.lengthBeats * secondsPerBeat * state.sampleRate)));
        rc.sourceOffsetSeconds = c.sourceOffsetSeconds;
        rc.gain = c.gain;
        rc.pan = c.pan;
        rc.muted = c.muted;
        rc.loop = c.loop;
        rc.reversed = c.reversed;
        rc.mixerInsert = juce::jlimit(0, kMaxTracks - 1, c.mixerInsert);
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
        rn.mixerInsert = juce::jlimit(0, kMaxTracks - 1, tracks_[n.track].mixerInsert);
        state.endSample = std::max(state.endSample, rn.startSample + rn.lengthSamples);
    }

    activeRenderState_.store(target, std::memory_order_release);
    renderDirty_.store(false, std::memory_order_release);
}

void DawWorkspace::renderToMaster(float* left, float* right, int numSamples) noexcept
{
    renderBlock(left, right, nullptr, 0, numSamples);
}

void DawWorkspace::renderToMixer(juce::AudioBuffer<float>& mixerBuffer,
                                 int numMixerChannels,
                                 int numSamples) noexcept
{
    renderBlock(nullptr, nullptr, &mixerBuffer, numMixerChannels, numSamples);
}

void DawWorkspace::renderBlock(float* masterLeft,
                               float* masterRight,
                               juce::AudioBuffer<float>* mixerBuffer,
                               int numMixerChannels,
                               int numSamples) noexcept
{
    const bool mixerMode = mixerBuffer != nullptr;
    if (numSamples <= 0 || !playing_.load(std::memory_order_acquire))
        return;
    if (!mixerMode && (masterLeft == nullptr || masterRight == nullptr))
        return;
    if (mixerMode && (numMixerChannels <= 0
        || mixerBuffer->getNumChannels() < numMixerChannels * 2
        || mixerBuffer->getNumSamples() < numSamples))
        return;

    const int index = activeRenderState_.load(std::memory_order_acquire);
    renderReaders_[index].fetch_add(1, std::memory_order_acq_rel);
    const auto& state = renderStates_[index];

    // LIVE/CLIPS is a separate performance transport. It reuses the exact same
    // render snapshot and mixer insert routing as the arranger, but clip starts
    // are driven by quantized launch samples instead of timeline startBeat.
    if (liveSessionEnabled_.load(std::memory_order_acquire))
    {
        const std::int64_t liveBlockStart = liveClockSamples_.load(std::memory_order_relaxed);
        const std::int64_t liveBlockEnd = liveBlockStart + numSamples;

        auto findLiveClip = [&state](int clipId, int trackIndex) noexcept -> const RenderClip*
        {
            if (clipId < 0)
                return nullptr;
            for (int ci = 0; ci < state.clipCount; ++ci)
            {
                const auto& candidate = state.clips[ci];
                if (candidate.id == clipId && candidate.track == trackIndex)
                    return &candidate;
            }
            return nullptr;
        };

        auto renderLiveSegment = [&](const RenderClip* clip,
                                     const RenderTrack& track,
                                     std::int64_t launchSample,
                                     std::int64_t segmentStart,
                                     std::int64_t segmentEnd) noexcept
        {
            if (clip == nullptr || clip->audio == nullptr || clip->muted
                || clip->lengthSamples <= 0 || segmentStart >= segmentEnd)
                return;
            if (track.mute || (state.anySolo && !track.solo))
                return;

            float* left = masterLeft;
            float* right = masterRight;
            if (mixerMode)
            {
                const int insert = juce::jlimit(0, numMixerChannels - 1, clip->mixerInsert);
                left = mixerBuffer->getWritePointer(insert * 2);
                right = mixerBuffer->getWritePointer(insert * 2 + 1);
            }

            const auto& src = clip->audio->samples;
            const int srcSamples = src.getNumSamples();
            const int srcChannels = src.getNumChannels();
            if (srcSamples <= 0 || srcChannels <= 0)
                return;

            const double ratio = clip->audio->sampleRate / std::max(1.0, state.sampleRate);
            const double sourceOffset = clip->sourceOffsetSeconds * clip->audio->sampleRate;
            const float pan = juce::jlimit(-1.0f, 1.0f, track.pan + clip->pan);
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float panL = std::cos(angle);
            const float panR = std::sin(angle);
            const float baseGain = clip->gain * track.gain;

            for (std::int64_t global = segmentStart; global < segmentEnd; ++global)
            {
                const std::int64_t elapsed = global - launchSample;
                if (elapsed < 0)
                    continue;

                const std::int64_t cycleLocal = elapsed % clip->lengthSamples;
                const std::int64_t mappedLocal = clip->reversed
                    ? (clip->lengthSamples - 1 - cycleLocal)
                    : cycleLocal;

                double srcPos = sourceOffset + static_cast<double>(mappedLocal) * ratio;
                srcPos = std::fmod(srcPos, static_cast<double>(srcSamples));
                if (srcPos < 0.0)
                    srcPos += srcSamples;

                const int i0 = juce::jlimit(0, srcSamples - 1, static_cast<int>(srcPos));
                const int i1 = (i0 + 1 < srcSamples) ? i0 + 1 : 0;
                const float frac = static_cast<float>(srcPos - static_cast<double>(i0));
                auto read = [&](int ch) noexcept
                {
                    const int sourceCh = std::min(ch, srcChannels - 1);
                    const float a = src.getSample(sourceCh, i0);
                    const float b = src.getSample(sourceCh, i1);
                    return a + (b - a) * frac;
                };

                float env = 1.0f;
                if (clip->fadeInSamples > 0 && cycleLocal < clip->fadeInSamples)
                    env *= equalPowerFade(static_cast<double>(cycleLocal)
                                          / static_cast<double>(clip->fadeInSamples));
                const auto remain = clip->lengthSamples - cycleLocal;
                if (clip->fadeOutSamples > 0 && remain < clip->fadeOutSamples)
                    env *= equalPowerFade(static_cast<double>(remain)
                                          / static_cast<double>(clip->fadeOutSamples));
                env = juce::jlimit(0.0f, 1.0f, env);

                const int dst = static_cast<int>(global - liveBlockStart);
                if (srcChannels == 1)
                {
                    const float value = read(0) * baseGain * env;
                    if (left == right)
                        left[dst] += value;
                    else
                    {
                        left[dst] += value * panL;
                        right[dst] += value * panR;
                    }
                }
                else
                {
                    float l = read(0) * baseGain * env;
                    float r = read(1) * baseGain * env;
                    if (pan < 0.0f) r *= 1.0f + pan;
                    else if (pan > 0.0f) l *= 1.0f - pan;
                    if (left == right)
                        left[dst] += (l + r) * 0.70710678f;
                    else
                    {
                        left[dst] += l;
                        right[dst] += r;
                    }
                }
            }
        };

        bool anythingActiveOrPending = false;
        const int liveTracks = std::min(state.trackCount, kLiveMaxTracks);
        for (int trackIndex = 0; trackIndex < liveTracks; ++trackIndex)
        {
            int activeId = liveActiveClipIds_[trackIndex].load(std::memory_order_acquire);
            int pendingId = livePendingClipIds_[trackIndex].load(std::memory_order_acquire);
            std::int64_t activeLaunch = liveClipLaunchSamples_[trackIndex].load(std::memory_order_relaxed);
            const std::int64_t pendingLaunch = livePendingLaunchSamples_[trackIndex].load(std::memory_order_relaxed);

            if (pendingId >= 0 && pendingLaunch <= liveBlockStart)
            {
                activeId = pendingId;
                activeLaunch = pendingLaunch;
                liveActiveClipIds_[trackIndex].store(activeId, std::memory_order_release);
                liveClipLaunchSamples_[trackIndex].store(activeLaunch, std::memory_order_relaxed);
                livePendingClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
                livePendingLaunchSamples_[trackIndex].store(0, std::memory_order_relaxed);
                pendingId = kLiveNoClip;
            }

            const auto& track = state.tracks[trackIndex];
            const auto* activeClip = findLiveClip(activeId, trackIndex);
            const auto* pendingClip = findLiveClip(pendingId, trackIndex);

            if (activeId >= 0 && activeClip == nullptr)
            {
                liveActiveClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
                activeId = kLiveNoClip;
            }
            if (pendingId >= 0 && pendingClip == nullptr)
            {
                livePendingClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
                livePendingLaunchSamples_[trackIndex].store(0, std::memory_order_relaxed);
                pendingId = kLiveNoClip;
            }

            if (pendingId >= 0 && pendingLaunch > liveBlockStart && pendingLaunch < liveBlockEnd)
            {
                renderLiveSegment(activeClip, track, activeLaunch, liveBlockStart, pendingLaunch);
                renderLiveSegment(pendingClip, track, pendingLaunch, pendingLaunch, liveBlockEnd);

                liveActiveClipIds_[trackIndex].store(pendingId, std::memory_order_release);
                liveClipLaunchSamples_[trackIndex].store(pendingLaunch, std::memory_order_relaxed);
                livePendingClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
                livePendingLaunchSamples_[trackIndex].store(0, std::memory_order_relaxed);
                activeId = pendingId;
                pendingId = kLiveNoClip;
            }
            else
            {
                renderLiveSegment(activeClip, track, activeLaunch, liveBlockStart, liveBlockEnd);
            }

            anythingActiveOrPending = anythingActiveOrPending
                || activeId >= 0
                || pendingId >= 0;
        }

        const int pendingScene = livePendingScene_.load(std::memory_order_acquire);
        const auto pendingSceneSample = livePendingSceneSample_.load(std::memory_order_relaxed);
        if (pendingScene >= 0 && pendingSceneSample < liveBlockEnd)
        {
            liveActiveScene_.store(pendingScene, std::memory_order_release);
            livePendingScene_.store(-1, std::memory_order_release);
            livePendingSceneSample_.store(0, std::memory_order_relaxed);
        }

        liveClockSamples_.store(liveBlockEnd, std::memory_order_relaxed);
        if (!anythingActiveOrPending)
        {
            liveSessionEnabled_.store(false, std::memory_order_release);
            playing_.store(false, std::memory_order_release);
        }

        renderReaders_[index].fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    if (state.clipCount == 0 && state.midiNoteCount == 0)
    {
        playing_.store(false, std::memory_order_release);
        transportSamples_.store(0, std::memory_order_relaxed);
        renderReaders_[index].fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

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

        float* left = masterLeft;
        float* right = masterRight;
        if (mixerMode)
        {
            const int insert = juce::jlimit(0, numMixerChannels - 1, clip.mixerInsert);
            left = mixerBuffer->getWritePointer(insert * 2);
            right = mixerBuffer->getWritePointer(insert * 2 + 1);
        }

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
        const float pan = juce::jlimit(-1.0f, 1.0f, track.pan + clip.pan);
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float panL = std::cos(angle);
        const float panR = std::sin(angle);
        const float baseGain = clip.gain * track.gain;

        for (std::int64_t global = ovStart; global < ovEnd; ++global)
        {
            const std::int64_t local = global - clipStart;
            const std::int64_t mappedLocal = clip.reversed ? (clip.lengthSamples - 1 - local) : local;
            double srcPos = sourceOffset + static_cast<double>(mappedLocal) * ratio;
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
                env *= equalPowerFade(static_cast<double>(local) / static_cast<double>(clip.fadeInSamples));
            const auto remain = clip.lengthSamples - local;
            if (clip.fadeOutSamples > 0 && remain < clip.fadeOutSamples)
                env *= equalPowerFade(static_cast<double>(remain) / static_cast<double>(clip.fadeOutSamples));
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

        float* left = masterLeft;
        float* right = masterRight;
        if (mixerMode)
        {
            const int insert = juce::jlimit(0, numMixerChannels - 1, note.mixerInsert);
            left = mixerBuffer->getWritePointer(insert * 2);
            right = mixerBuffer->getWritePointer(insert * 2 + 1);
        }

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

    const bool liveLock = liveSessionActive();
    for (auto* component : { static_cast<juce::Component*>(&newButton_),
                             static_cast<juce::Component*>(&openButton_),
                             static_cast<juce::Component*>(&importButton_),
                             static_cast<juce::Component*>(&addTrackButton_),
                             static_cast<juce::Component*>(&addMidiTrackButton_),
                             static_cast<juce::Component*>(&patternButton_),
                             static_cast<juce::Component*>(&splitButton_),
                             static_cast<juce::Component*>(&duplicateButton_),
                             static_cast<juce::Component*>(&deleteButton_),
                             static_cast<juce::Component*>(&trackNameEditor_),
                             static_cast<juce::Component*>(&clipGainSlider_),
                             static_cast<juce::Component*>(&clipMuteButton_),
                             static_cast<juce::Component*>(&clipLoopButton_),
                             static_cast<juce::Component*>(&clipMixerBox_),
                             static_cast<juce::Component*>(&fadeInSlider_),
                             static_cast<juce::Component*>(&fadeOutSlider_) })
        component->setEnabled(!liveLock);

    const bool nowPlaying = playing_.load(std::memory_order_acquire);
    playButton_.setButtonText(liveLock ? "STOP CLIPS" : (nowPlaying ? "PAUSE" : "PLAY"));
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

void DawWorkspace::emergencyStop()
{
    stopTransport(false);
    refreshStatus(juce::String::fromUTF8("STOP ALL · reproducción detenida"));
}


int DawWorkspace::liveClipIdForSlot(int trackIndex, int sceneIndex) const
{
    if (trackIndex < 0 || trackIndex >= std::min(trackCount_, kLiveMaxTracks)
        || sceneIndex < 0 || sceneIndex >= kLiveMaxScenes)
        return kLiveNoClip;

    std::vector<const Clip*> trackClips;
    trackClips.reserve(clips_.size());
    for (const auto& clip : clips_)
        if (clip.track == trackIndex && clip.audio != nullptr)
            trackClips.push_back(&clip);

    std::sort(trackClips.begin(), trackClips.end(),
        [](const Clip* a, const Clip* b)
        {
            if (a->startBeat != b->startBeat)
                return a->startBeat < b->startBeat;
            return a->id < b->id;
        });

    if (sceneIndex >= static_cast<int>(trackClips.size()))
        return kLiveNoClip;
    return trackClips[static_cast<std::size_t>(sceneIndex)]->id;
}

DawWorkspace::LiveSessionSnapshot DawWorkspace::liveSessionSnapshot() const
{
    LiveSessionSnapshot snapshot;
    snapshot.sceneCount = kLiveMaxScenes;
    snapshot.activeScene = liveActiveScene_.load(std::memory_order_acquire);
    snapshot.pendingScene = livePendingScene_.load(std::memory_order_acquire);
    snapshot.quantizationBeats = liveQuantizationBeats_.load(std::memory_order_acquire);
    snapshot.active = liveSessionEnabled_.load(std::memory_order_acquire);
    snapshot.editLocked = snapshot.active;

    const int count = std::min(trackCount_, kLiveMaxTracks);
    snapshot.tracks.reserve(static_cast<std::size_t>(count));

    for (int trackIndex = 0; trackIndex < count; ++trackIndex)
    {
        LiveTrackView track;
        track.trackIndex = trackIndex;
        track.name = tracks_[trackIndex].name.isNotEmpty()
            ? tracks_[trackIndex].name
            : "Pista " + juce::String(trackIndex + 1);
        track.colour = tracks_[trackIndex].colour;
        track.mixerInsert = juce::jlimit(0, kMaxTracks - 1, tracks_[trackIndex].mixerInsert);

        const int activeId = liveActiveClipIds_[trackIndex].load(std::memory_order_acquire);
        const int pendingId = livePendingClipIds_[trackIndex].load(std::memory_order_acquire);
        track.active = activeId >= 0;
        track.pending = pendingId >= 0;

        std::vector<const Clip*> trackClips;
        trackClips.reserve(clips_.size());
        for (const auto& clip : clips_)
            if (clip.track == trackIndex && clip.audio != nullptr)
                trackClips.push_back(&clip);

        std::sort(trackClips.begin(), trackClips.end(),
            [](const Clip* a, const Clip* b)
            {
                if (a->startBeat != b->startBeat)
                    return a->startBeat < b->startBeat;
                return a->id < b->id;
            });

        const int slotCount = std::min(kLiveMaxScenes, static_cast<int>(trackClips.size()));
        track.clips.reserve(static_cast<std::size_t>(slotCount));
        for (int scene = 0; scene < slotCount; ++scene)
        {
            const auto* clip = trackClips[static_cast<std::size_t>(scene)];
            LiveClipCell cell;
            cell.clipId = clip->id;
            cell.trackIndex = trackIndex;
            cell.sceneIndex = scene;
            cell.mixerInsert = juce::jlimit(0, kMaxTracks - 1, clip->mixerInsert);
            cell.name = clip->audio != nullptr
                ? juce::File(clip->audio->path).getFileNameWithoutExtension()
                : "Clip " + juce::String(scene + 1);
            if (cell.name.isEmpty())
                cell.name = "Clip " + juce::String(scene + 1);
            cell.colour = clip->colour;
            cell.lengthBeats = clip->lengthBeats;
            cell.active = clip->id == activeId;
            cell.pending = clip->id == pendingId;
            track.clips.push_back(std::move(cell));
        }

        snapshot.tracks.push_back(std::move(track));
    }

    return snapshot;
}

std::int64_t DawWorkspace::nextLiveBoundarySample(std::int64_t now) const noexcept
{
    const int quantBeats = liveQuantizationBeats_.load(std::memory_order_relaxed);
    if (quantBeats <= 0)
        return std::max<std::int64_t>(0, now);

    const double sampleRate = std::max(1.0, renderSampleRate_.load(std::memory_order_relaxed));
    const double secondsPerBeat = 60.0 / std::max(1.0, bpm());
    const auto quantum = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::llround(static_cast<double>(quantBeats) * secondsPerBeat * sampleRate)));
    now = std::max<std::int64_t>(0, now);
    const auto remainder = now % quantum;
    return remainder == 0 ? now : now + (quantum - remainder);
}

void DawWorkspace::setLiveQuantizationBeats(int beats)
{
    if (beats != 0 && beats != 1 && beats != 2 && beats != 4 && beats != 8)
        beats = 4;
    liveQuantizationBeats_.store(beats, std::memory_order_release);
    refreshStatus(beats == 0
        ? juce::String::fromUTF8("LIVE/CLIPS · cuantización OFF")
        : juce::String::fromUTF8("LIVE/CLIPS · cuantización ") + juce::String(beats) + " beat(s)");
    repaint();
}

void DawWorkspace::launchLiveClip(int trackIndex, int sceneIndex)
{
    const int clipId = liveClipIdForSlot(trackIndex, sceneIndex);
    if (clipId < 0)
    {
        refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · esa celda todavía no tiene audio."));
        return;
    }

    const bool wasPlaying = playing_.exchange(true, std::memory_order_acq_rel);
    liveSessionEnabled_.store(true, std::memory_order_release);
    const auto target = nextLiveBoundarySample(liveClockSamples_.load(std::memory_order_acquire));
    livePendingClipIds_[trackIndex].store(clipId, std::memory_order_release);
    livePendingLaunchSamples_[trackIndex].store(target, std::memory_order_release);
    livePendingScene_.store(-1, std::memory_order_release);
    liveActiveScene_.store(-1, std::memory_order_release);
    playButton_.setButtonText("PAUSE");
    if (!wasPlaying && onPlayStateChanged)
        onPlayStateChanged(true);

    refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · clip en cola · ")
        + tracks_[trackIndex].name + " · escena " + juce::String(sceneIndex + 1));
    repaint();
}

void DawWorkspace::launchLiveScene(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= kLiveMaxScenes)
        return;

    const auto target = nextLiveBoundarySample(liveClockSamples_.load(std::memory_order_acquire));
    bool queuedAny = false;
    const int count = std::min(trackCount_, kLiveMaxTracks);
    for (int track = 0; track < count; ++track)
    {
        const int clipId = liveClipIdForSlot(track, sceneIndex);
        if (clipId < 0)
            continue;
        livePendingClipIds_[track].store(clipId, std::memory_order_release);
        livePendingLaunchSamples_[track].store(target, std::memory_order_release);
        queuedAny = true;
    }

    if (!queuedAny)
    {
        refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · la escena está vacía."));
        return;
    }

    const bool wasPlaying = playing_.exchange(true, std::memory_order_acq_rel);
    liveSessionEnabled_.store(true, std::memory_order_release);
    livePendingScene_.store(sceneIndex, std::memory_order_release);
    livePendingSceneSample_.store(target, std::memory_order_release);
    playButton_.setButtonText("PAUSE");
    if (!wasPlaying && onPlayStateChanged)
        onPlayStateChanged(true);

    refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · escena ")
        + juce::String(sceneIndex + 1) + juce::String::fromUTF8(" en cola"));
    repaint();
}

void DawWorkspace::stopLiveTrack(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= kMaxTracks)
        return;
    livePendingClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
    liveActiveClipIds_[trackIndex].store(kLiveNoClip, std::memory_order_release);
    livePendingLaunchSamples_[trackIndex].store(0, std::memory_order_release);
    liveClipLaunchSamples_[trackIndex].store(0, std::memory_order_release);
    liveActiveScene_.store(-1, std::memory_order_release);
    livePendingScene_.store(-1, std::memory_order_release);

    bool anything = false;
    for (int i = 0; i < std::min(trackCount_, kLiveMaxTracks); ++i)
    {
        if (liveActiveClipIds_[i].load(std::memory_order_acquire) >= 0
            || livePendingClipIds_[i].load(std::memory_order_acquire) >= 0)
        {
            anything = true;
            break;
        }
    }
    if (!anything)
    {
        liveSessionEnabled_.store(false, std::memory_order_release);
        playing_.store(false, std::memory_order_release);
        playButton_.setButtonText("PLAY");
        if (onPlayStateChanged) onPlayStateChanged(false);
    }
    refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · STOP TRACK · ")
        + tracks_[juce::jlimit(0, trackCount_ - 1, trackIndex)].name);
    repaint();
}

void DawWorkspace::clearLiveState(bool stopTransportToo) noexcept
{
    for (int i = 0; i < kMaxTracks; ++i)
    {
        liveActiveClipIds_[i].store(kLiveNoClip, std::memory_order_release);
        livePendingClipIds_[i].store(kLiveNoClip, std::memory_order_release);
        liveClipLaunchSamples_[i].store(0, std::memory_order_release);
        livePendingLaunchSamples_[i].store(0, std::memory_order_release);
    }
    liveActiveScene_.store(-1, std::memory_order_release);
    livePendingScene_.store(-1, std::memory_order_release);
    livePendingSceneSample_.store(0, std::memory_order_release);
    liveSessionEnabled_.store(false, std::memory_order_release);
    liveClockSamples_.store(0, std::memory_order_release);
    if (stopTransportToo)
        playing_.store(false, std::memory_order_release);
}

void DawWorkspace::stopLiveClips()
{
    const bool wasActive = liveSessionEnabled_.load(std::memory_order_acquire);
    clearLiveState(true);
    playButton_.setButtonText("PLAY");
    if (wasActive && onPlayStateChanged)
        onPlayStateChanged(false);
    refreshStatus(juce::String::fromUTF8("LIVE/CLIPS · STOP ALL CLIPS · PADS y CLICK no fueron modificados"));
    repaint();
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
    if (rejectStructuralEditWhileLive("deshacer cambios estructurales")) return;
    if (undoStack_.empty()) return;
    redoStack_.push_back(serializeProject());
    const auto snapshot = undoStack_.back();
    undoStack_.pop_back();
    restoreProject(snapshot, false);
    projectDirty_ = true;
}

void DawWorkspace::redo()
{
    if (rejectStructuralEditWhileLive("rehacer cambios estructurales")) return;
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
    root.setAttribute("version", 3);
    root.setAttribute("bpm", bpm());
    root.setAttribute("trackCount", trackCount_);
    root.setAttribute("viewStartBeat", viewStartBeat_);
    root.setAttribute("zoom", zoom_);
    root.setAttribute("firstVisibleTrack", firstVisibleTrack_);
    root.setAttribute("liveQuantizationBeats", liveQuantizationBeats_.load(std::memory_order_relaxed));

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
        t->setAttribute("mixerInsert", tracks_[i].mixerInsert);
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
        x->setAttribute("pan", c.pan);
        x->setAttribute("muted", c.muted);
        x->setAttribute("loop", c.loop);
        x->setAttribute("reversed", c.reversed);
        x->setAttribute("fadeInBeats", c.fadeInBeats);
        x->setAttribute("fadeOutBeats", c.fadeOutBeats);
        x->setAttribute("mixerInsert", c.mixerInsert);
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
    for (int i = 0; i < trackCount_; ++i)
        tracks_[i].mixerInsert = i;
    bpm_.store(juce::jlimit(40.0, 240.0, xml->getDoubleAttribute("bpm", 120.0)));
    bpmSlider_.setValue(bpm(), juce::dontSendNotification);
    viewStartBeat_ = std::max(0.0, xml->getDoubleAttribute("viewStartBeat", 0.0));
    zoom_ = juce::jlimit(0.5, 4.0, xml->getDoubleAttribute("zoom", 1.0));
    zoomSlider_.setValue(zoom_, juce::dontSendNotification);
    firstVisibleTrack_ = juce::jlimit(0, std::max(0, trackCount_ - 1),
        xml->getIntAttribute("firstVisibleTrack", 0));
    setLiveQuantizationBeats(xml->getIntAttribute("liveQuantizationBeats", 4));

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
            tracks_[i].mixerInsert = juce::jlimit(0, kMaxTracks - 1, t->getIntAttribute("mixerInsert", i));
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
            c.pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(x->getDoubleAttribute("pan", 0.0)));
            c.muted = x->getBoolAttribute("muted", false);
            c.loop = x->getBoolAttribute("loop", false);
            c.reversed = x->getBoolAttribute("reversed", false);
            c.fadeInBeats = std::max(0.0, x->getDoubleAttribute("fadeInBeats", 0.0));
            c.fadeOutBeats = std::max(0.0, x->getDoubleAttribute("fadeOutBeats", 0.0));
            c.mixerInsert = juce::jlimit(0, kMaxTracks - 1,
                x->getIntAttribute("mixerInsert", tracks_[c.track].mixerInsert));
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
    refreshStatus(juce::String::fromUTF8("Guardado · ") + target.getFileName());
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
    if (rejectStructuralEditWhileLive("abrir otro proyecto")) return;
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
    if (rejectStructuralEditWhileLive("crear un proyecto nuevo")) return;
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
        tracks_[i].mixerInsert = i;
    }
    tracks_[0].name = "Voz";
    tracks_[1].name = juce::String::fromUTF8("Batería");
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

