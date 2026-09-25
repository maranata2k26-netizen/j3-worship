#include "J3Theme.h"

#include <cmath>

namespace j3ui
{
Palette paletteForTheme(int id)
{
    switch (id)
    {
        case 2: // Ableton-inspired neutral studio
            return {
                juce::Colour(0xff202020), juce::Colour(0xff282828), juce::Colour(0xff303030),
                juce::Colour(0xff393939), juce::Colour(0xff464646), juce::Colour(0xffffb84a),
                juce::Colour(0xff8fd14f), juce::Colour(0xff64d88b), juce::Colour(0xffffc65c),
                juce::Colour(0xffff5f64), juce::Colour(0xfff1f1f1), juce::Colour(0xffb7b7b7),
                juce::Colour(0xff565656)
            };
        case 3: // Studio Blue
            return {
                juce::Colour(0xff071018), juce::Colour(0xff0b1722), juce::Colour(0xff0e1d29),
                juce::Colour(0xff132633), juce::Colour(0xff193343), juce::Colour(0xff208bff),
                juce::Colour(0xff00c2ff), juce::Colour(0xff2ed47a), juce::Colour(0xffffc247),
                juce::Colour(0xffff4d64), juce::Colour(0xfff0f7ff), juce::Colour(0xff94a9ba),
                juce::Colour(0xff274354)
            };
        case 4: // Midnight violet
            return {
                juce::Colour(0xff100d18), juce::Colour(0xff171120), juce::Colour(0xff1c1528),
                juce::Colour(0xff261d34), juce::Colour(0xff332542), juce::Colour(0xffa76cff),
                juce::Colour(0xff5ba9ff), juce::Colour(0xff49d89b), juce::Colour(0xffffc45d),
                juce::Colour(0xffff6178), juce::Colour(0xfff7f0ff), juce::Colour(0xffb4a6c6),
                juce::Colour(0xff463557)
            };
        case 5: // High contrast stage
            return {
                juce::Colour(0xff050505), juce::Colour(0xff0c0c0c), juce::Colour(0xff121212),
                juce::Colour(0xff1c1c1c), juce::Colour(0xff262626), juce::Colour(0xff00a8ff),
                juce::Colour(0xfff4d35e), juce::Colour(0xff38e07d), juce::Colour(0xffffd166),
                juce::Colour(0xffff4560), juce::Colour(0xffffffff), juce::Colour(0xffc9c9c9),
                juce::Colour(0xff444444)
            };
        case 6: // FL-inspired colourful production workspace
            return {
                juce::Colour(0xff14181d), juce::Colour(0xff1b2026), juce::Colour(0xff232a31),
                juce::Colour(0xff2b343d), juce::Colour(0xff35414b), juce::Colour(0xffff8a35),
                juce::Colour(0xff57c7ff), juce::Colour(0xff62d68a), juce::Colour(0xffffd15a),
                juce::Colour(0xffff5c73), juce::Colour(0xfff4f6f8), juce::Colour(0xffaeb8c2),
                juce::Colour(0xff46535f)
            };
        default: // J3 Dark
            return {
                juce::Colour(0xff080d12), juce::Colour(0xff0b131a), juce::Colour(0xff0e1820),
                juce::Colour(0xff12222c), juce::Colour(0xff19303d), juce::Colour(0xff158cff),
                juce::Colour(0xff00c7ff), juce::Colour(0xff2ed47a), juce::Colour(0xffffc247),
                juce::Colour(0xffff4d64), juce::Colour(0xffeff6fc), juce::Colour(0xff91a3b3),
                juce::Colour(0xff263a47)
            };
    }
}

juce::String themeName(int id)
{
    switch (id)
    {
        case 2: return "ABLETON STYLE";
        case 3: return "STUDIO BLUE";
        case 4: return "MIDNIGHT";
        case 5: return "HIGH CONTRAST";
        case 6: return "FL STUDIO STYLE";
        default: return "J3 DARK";
    }
}

LookAndFeel::LookAndFeel()
{
    setTheme(1);
}

void LookAndFeel::setTheme(int id)
{
    themeId_ = juce::jlimit(1, 6, id);
    palette_ = paletteForTheme(themeId_);
    applyColours();
}

void LookAndFeel::applyColours()
{
    setColour(juce::ResizableWindow::backgroundColourId, palette_.background);
    setColour(juce::Label::textColourId, palette_.text);
    setColour(juce::TextButton::buttonColourId, palette_.panel3);
    setColour(juce::TextButton::buttonOnColourId, palette_.accent);
    setColour(juce::TextButton::textColourOffId, palette_.text);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::ToggleButton::textColourId, palette_.text);
    setColour(juce::Slider::backgroundColourId, palette_.panel);
    setColour(juce::Slider::trackColourId, palette_.accent.withAlpha(0.72f));
    setColour(juce::Slider::thumbColourId, palette_.accent);
    setColour(juce::Slider::rotarySliderFillColourId, palette_.accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, palette_.border);
    setColour(juce::Slider::textBoxTextColourId, palette_.text);
    setColour(juce::Slider::textBoxBackgroundColourId, palette_.panel);
    setColour(juce::Slider::textBoxOutlineColourId, palette_.border);
    setColour(juce::ComboBox::backgroundColourId, palette_.panel2);
    setColour(juce::ComboBox::textColourId, palette_.text);
    setColour(juce::ComboBox::outlineColourId, palette_.border);
    setColour(juce::ComboBox::arrowColourId, palette_.mutedText);
    setColour(juce::TextEditor::backgroundColourId, palette_.panel2);
    setColour(juce::TextEditor::textColourId, palette_.text);
    setColour(juce::TextEditor::outlineColourId, palette_.border);
    setColour(juce::PopupMenu::backgroundColourId, palette_.panel2);
    setColour(juce::PopupMenu::textColourId, palette_.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette_.accent.withAlpha(0.35f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::ProgressBar::backgroundColourId, palette_.background);
    setColour(juce::ProgressBar::foregroundColourId, palette_.good);
    setColour(juce::TabbedComponent::backgroundColourId, palette_.background);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, palette_.border);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, palette_.accent);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                       const juce::Colour& backgroundColour,
                                       bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = button.getToggleState() ? palette_.accent : backgroundColour;
    if (highlighted) colour = colour.brighter(0.08f);
    if (down) colour = colour.darker(0.12f);

    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour((button.getToggleState() ? palette_.accent2 : palette_.border).withAlpha(0.85f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                   float sliderPos, float, float,
                                   const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool vertical = style == juce::Slider::LinearVertical
                       || style == juce::Slider::LinearBarVertical;
    const auto track = slider.findColour(juce::Slider::trackColourId);
    const auto thumb = slider.findColour(juce::Slider::thumbColourId);
    const auto base = palette_.border.withAlpha(0.72f);

    if (vertical)
    {
        const float cx = x + width * 0.5f;
        const float top = static_cast<float>(y + 5);
        const float bottom = static_cast<float>(y + height - 5);
        g.setColour(base);
        g.fillRoundedRectangle(cx - 2.0f, top, 4.0f, bottom - top, 2.0f);
        g.setColour(track);
        g.fillRoundedRectangle(cx - 2.0f, sliderPos, 4.0f, bottom - sliderPos, 2.0f);
        g.setColour(thumb);
        g.fillRoundedRectangle(cx - 9.0f, sliderPos - 5.0f, 18.0f, 10.0f, 3.0f);
    }
    else
    {
        const float cy = y + height * 0.5f;
        const float left = static_cast<float>(x + 5);
        const float right = static_cast<float>(x + width - 5);
        g.setColour(base);
        g.fillRoundedRectangle(left, cy - 2.0f, right - left, 4.0f, 2.0f);
        g.setColour(track);
        g.fillRoundedRectangle(left, cy - 2.0f, juce::jmax(0.0f, sliderPos - left), 4.0f, 2.0f);
        g.setColour(thumb);
        g.fillEllipse(sliderPos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                   float sliderPosProportional, float rotaryStartAngle,
                                   float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(width), static_cast<float>(height)).reduced(6.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(palette_.border);
    g.strokePath(backgroundArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f,
                           rotaryStartAngle, angle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(valueArc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    const float pointerLength = radius * 0.62f;
    const auto end = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * pointerLength;
    g.setColour(palette_.text);
    g.drawLine(centre.x, centre.y, end.x, end.y, 2.0f);
    g.fillEllipse(centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
}
}
