// The iPhone screens' look: colours, type sizes and a LookAndFeel with touch-sized controls (rounded buttons,
// a thick slider track with a large thumb, switches for on/off settings).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace phone
{
namespace colours
{
inline const juce::Colour background { 0xff121316 };
inline const juce::Colour card { 0xff1e2024 };
inline const juce::Colour cardPressed { 0xff2a2d33 };
inline const juce::Colour separator { 0xff2e3137 };
inline const juce::Colour text { 0xffeaeaec };
inline const juce::Colour secondary { 0xff9a9da5 };
inline const juce::Colour accent { 0xff4fa3e0 };
inline const juce::Colour finger { 0xffe0a24f };  // A covered hole (as on the desktop)
inline const juce::Colour good { 0xff4cc38a };
inline const juce::Colour warning { 0xffe0a24f };
inline const juce::Colour record { 0xffe0483e };
} // namespace colours

// Points (iOS logical pixels).
namespace sizes
{
constexpr float title = 24.0f, body = 17.0f, caption = 13.0f;
constexpr int gutter = 16, rowHeight = 48, cardRadius = 14;
} // namespace sizes

inline juce::Font font (float size, bool bold = false)
{
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, colours::card);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, colours::text);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Slider::trackColourId, colours::accent);
        setColour (juce::Slider::backgroundColourId, colours::separator);
        setColour (juce::Slider::thumbColourId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, colours::secondary);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background, bool highlighted, bool down) override
    {
        auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)
            colour = colour.brighter (0.15f);
        else if (highlighted)
            colour = colour.brighter (0.05f);
        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 12.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (sizes::body, true); }

    // A switch on the right, its label on the left (iOS style).
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool) override
    {
        auto area = button.getLocalBounds().toFloat();
        const auto track = area.removeFromRight (51.0f).withSizeKeepingCentre (51.0f, 31.0f);
        const bool on = button.getToggleState();
        g.setColour (on ? colours::good : colours::separator);
        g.fillRoundedRectangle (track, 15.5f);
        const auto knob = juce::Rectangle<float> (27.0f, 27.0f).withCentre ({ on ? track.getRight() - 15.5f : track.getX() + 15.5f, track.getCentreY() });
        g.setColour (juce::Colours::white);
        g.fillEllipse (knob);
        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (font (sizes::body));
        g.drawText (button.getButtonText(), area.withTrimmedRight (8.0f), juce::Justification::centredLeft);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, position, 0.0f, 0.0f, style, slider);
            return;
        }
        const float thumb = 26.0f;
        const float left = (float) x + thumb / 2.0f, right = (float) (x + width) - thumb / 2.0f;
        const float centreY = (float) y + (float) height / 2.0f;
        const float px = juce::jlimit (left, right, position);
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (left, centreY - 3.0f, right - left, 6.0f, 3.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (left, centreY - 3.0f, px - left, 6.0f, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({ px, centreY + 1.0f }));
        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({ px, centreY }));
    }

    int getSliderThumbRadius (juce::Slider&) override { return 13; }
};
} // namespace phone
