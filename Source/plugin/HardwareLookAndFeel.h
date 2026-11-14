// Hardware-style rendering: recessed fader slots with ridged caps, knurled knobs with skirts,
// bevelled push buttons with LEDs, rubber pads and a glass LCD. Everything is drawn from the
// active Theme so every colour scheme gets the same physical look.
#pragma once

#include "Theme.h"

class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel();
    void setTheme (const Theme& t);
    const Theme& theme() const { return th; }

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    // Shared painters used by custom components
    void drawPad (juce::Graphics&, juce::Rectangle<float> r, bool down, float flash, bool dragOver, const juce::String& text, bool hasSound);
    void drawLcd (juce::Graphics&, juce::Rectangle<float> r, const juce::String& l1, const juce::String& l2);
    void drawScrew (juce::Graphics&, juce::Point<float> c, float radius, float angle);
    void drawPanel (juce::Graphics&, juce::Rectangle<int> area);

private:
    Theme th;
    juce::Image texture;   // brushed / grain texture, rebuilt per theme and size
};
