#pragma once

#include <JuceHeader.h>

namespace j3ui
{
struct Palette
{
    juce::Colour background;
    juce::Colour topBar;
    juce::Colour panel;
    juce::Colour panel2;
    juce::Colour panel3;
    juce::Colour accent;
    juce::Colour accent2;
    juce::Colour good;
    juce::Colour warning;
    juce::Colour danger;
    juce::Colour text;
    juce::Colour mutedText;
    juce::Colour border;
};

Palette paletteForTheme(int id);
juce::String themeName(int id);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void setTheme(int id);
    int themeId() const noexcept { return themeId_; }
    const Palette& palette() const noexcept { return palette_; }

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

private:
    void applyColours();
    int themeId_ { 1 };
    Palette palette_;
};
}
