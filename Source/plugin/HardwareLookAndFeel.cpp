#include "HardwareLookAndFeel.h"

using juce::Colour;
using juce::ColourGradient;
using juce::Graphics;
using juce::Rectangle;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

void softShadow (Graphics& g, const juce::Path& p, float dy, float radius, float alpha)
{
    juce::DropShadow (juce::Colours::black.withAlpha (alpha), (int) radius, { 0, (int) dy }).drawForPath (g, p);
}

Colour lum (Colour c, float amount) { return amount >= 0 ? c.brighter (amount) : c.darker (-amount); }
} // namespace

HardwareLookAndFeel::HardwareLookAndFeel() { setTheme (Theme {}); }

void HardwareLookAndFeel::setTheme (const Theme& t)
{
    th = t;
    texture = {};
    setColour (juce::Label::textColourId, th.panelText);
    setColour (juce::TextButton::buttonColourId, th.buttonFace);
    setColour (juce::TextButton::textColourOffId, th.buttonText);
    setColour (juce::TextButton::textColourOnId, th.buttonText);
    const auto well = th.lcdBg.interpolatedWith (juce::Colours::black, 0.35f);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::textColourId, th.lcdFg);
    setColour (juce::ComboBox::arrowColourId, th.lcdFg);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, th.lcdFg);
    setColour (juce::Slider::textBoxBackgroundColourId, well);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::textColourId, th.lcdFg);
    setColour (juce::PopupMenu::backgroundColourId, th.panel.darker (0.5f));
    setColour (juce::PopupMenu::textColourId, th.panelText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, th.accent.contrasting (1.0f));
    setColour (juce::PopupMenu::headerTextColourId, th.accent);
    setColour (juce::AlertWindow::backgroundColourId, th.panel.darker (0.3f));
    setColour (juce::AlertWindow::textColourId, th.panelText);
    setColour (juce::ResizableWindow::backgroundColourId, th.panel);
    setColour (juce::DocumentWindow::textColourId, th.panelText);
    setColour (juce::ScrollBar::thumbColourId, th.accent);
}

juce::Font HardwareLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions ((float) juce::jmin (13, h / 2 + 1), juce::Font::bold));
}

juce::Font HardwareLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

juce::Slider::SliderLayout HardwareLookAndFeel::getSliderLayout (juce::Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);
    if (s.isRotary() && s.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        layout.textBoxBounds = layout.textBoxBounds.withSizeKeepingCentre (juce::jmin (64, s.getWidth()), layout.textBoxBounds.getHeight());
    return layout;
}

// ===================================================================== panel
void HardwareLookAndFeel::drawPanel (Graphics& g, Rectangle<int> area)
{
    if (texture.isNull() || texture.getWidth() != area.getWidth() || texture.getHeight() != area.getHeight())
    {
        texture = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        Graphics tg (texture);
        tg.setGradientFill (ColourGradient (lum (th.panel, 0.08f), 0, 0, lum (th.panel, -0.12f), 0, (float) area.getHeight(), false));
        tg.fillAll();
        juce::Random rng (1200);
        // fine horizontal brushing, strongest on light panels
        const float strength = 0.025f + 0.03f * th.panel.getPerceivedBrightness();
        for (int y = 0; y < area.getHeight(); ++y)
        {
            const float a = rng.nextFloat() * strength;
            tg.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            const int x0 = rng.nextInt (juce::jmax (1, area.getWidth() / 3));
            tg.drawHorizontalLine (y, (float) x0, (float) area.getWidth() - (float) rng.nextInt (juce::jmax (1, area.getWidth() / 4)));
        }
        // speckle grain
        for (int i = 0; i < area.getWidth() * area.getHeight() / 60; ++i)
        {
            tg.setColour (juce::Colours::black.withAlpha (rng.nextFloat() * 0.05f));
            tg.fillRect (rng.nextInt (area.getWidth()), rng.nextInt (area.getHeight()), 1, 1);
        }
    }
    g.drawImageAt (texture, area.getX(), area.getY());
    // outer bevel
    auto r = area.toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (r.getX(), r.getY() + 1, r.getRight(), r.getY() + 1, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (r.getX(), r.getBottom() - 1, r.getRight(), r.getBottom() - 1, 2.0f);
}

void HardwareLookAndFeel::drawScrew (Graphics& g, juce::Point<float> c, float rad, float angle)
{
    Rectangle<float> r (c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r.translated (0, 1.2f).expanded (0.8f));
    g.setGradientFill (ColourGradient (Colour (0xffe8e8e8), r.getX(), r.getY(), Colour (0xff6a6a6a), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (Colour (0xff3a3a3a));
    g.drawEllipse (r, 0.8f);
    const auto d = juce::Point<float> (std::cos (angle), std::sin (angle)) * rad * 0.75f;
    g.setColour (Colour (0xff2a2a2a));
    g.drawLine ({ c - d, c + d }, rad * 0.35f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawLine ({ c - d + juce::Point<float> (0, 0.8f), c + d + juce::Point<float> (0, 0.8f) }, 0.6f);
}

// ===================================================================== faders
void HardwareLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cx = (float) x + (float) w * 0.5f;
    const float capW = juce::jmin ((float) w * 0.8f, 46.0f);
    const float capH = juce::jlimit (22.0f, 34.0f, capW * 0.7f);
    const float top = (float) y, bottom = (float) (y + h);

    // scale ticks either side of the slot
    g.setColour (th.panelText.withAlpha (0.55f));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = top + (bottom - top) * (float) i / 10.0f;
        const float len = (i % 5 == 0) ? capW * 0.42f : capW * 0.26f;
        g.drawLine (cx - capW * 0.5f - len, ty, cx - capW * 0.5f - 3, ty, i % 5 == 0 ? 1.4f : 0.9f);
        g.drawLine (cx + capW * 0.5f + 3, ty, cx + capW * 0.5f + len, ty, i % 5 == 0 ? 1.4f : 0.9f);
    }

    // recessed slot
    Rectangle<float> slot (cx - 3.5f, top - 4, 7.0f, bottom - top + 8);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillRoundedRectangle (slot.translated (0.8f, 1.0f), 3.5f);
    g.setColour (th.faderSlot);
    g.fillRoundedRectangle (slot, 3.5f);
    g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.7f), slot.getX(), 0, juce::Colours::transparentBlack, slot.getX() + 4, 0, false));
    g.fillRoundedRectangle (slot, 3.5f);

    // cap with drop shadow
    Rectangle<float> cap (cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
    juce::Path capPath;
    capPath.addRoundedRectangle (cap, 3.0f);
    softShadow (g, capPath, capH * 0.18f, capH * 0.45f, 0.65f);

    const auto base = th.faderCap;
    // body: two angled faces meeting at a centre ridge, like a moulded cap
    auto upper = cap.withHeight (capH * 0.5f);
    auto lower = cap.withTrimmedTop (capH * 0.5f);
    g.setGradientFill (ColourGradient (lum (base, 0.45f), 0, upper.getY(), lum (base, -0.15f), 0, upper.getBottom(), false));
    g.fillRoundedRectangle (upper.withBottom (upper.getBottom() + 2), 3.0f);
    g.setGradientFill (ColourGradient (lum (base, -0.35f), 0, lower.getY(), lum (base, 0.12f), 0, lower.getBottom(), false));
    g.fillRoundedRectangle (lower.withTop (lower.getY() - 0.5f), 3.0f);

    // grip ridges
    for (int i = 1; i <= 3; ++i)
    {
        const float dy = capH * 0.11f * (float) i;
        for (float sign : { -1.0f, 1.0f })
        {
            const float ly = cap.getCentreY() + sign * dy;
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawLine (cap.getX() + 3, ly, cap.getRight() - 3, ly, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawLine (cap.getX() + 3, ly + 1, cap.getRight() - 3, ly + 1, 0.8f);
        }
    }
    // centre indicator line
    g.setColour (th.knobPointer);
    g.fillRect (cap.getX() + 2, cap.getCentreY() - 1.0f, capW - 4, 2.0f);
    // edges
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawLine (cap.getX() + 3, cap.getY() + 1, cap.getRight() - 3, cap.getY() + 1, 1.0f);
}

// ===================================================================== knobs
void HardwareLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat();
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float rb = outer * 0.74f;                 // skirt radius
    const float angle = start + pos * (end - start);

    // scale dots around the knob
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const auto p = c + juce::Point<float> (std::sin (a), -std::cos (a)) * (outer * 0.93f);
        g.setColour (th.panelText.withAlpha (i == 0 || i == 10 || i == 5 ? 0.85f : 0.5f));
        const float d = (i == 0 || i == 10 || i == 5) ? 3.2f : 2.2f;
        g.fillEllipse (p.x - d * 0.5f, p.y - d * 0.5f, d, d);
    }

    // shadow
    juce::Path body;
    body.addEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2);
    softShadow (g, body, rb * 0.16f, rb * 0.5f, 0.7f);

    // skirt
    g.setGradientFill (ColourGradient (lum (th.knobBody, 0.35f), c.x - rb, c.y - rb, lum (th.knobBody, -0.4f), c.x + rb, c.y + rb, false));
    g.fillPath (body);

    // knurling on the skirt edge
    const int teeth = 60;
    for (int i = 0; i < teeth; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) teeth;
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        const float light = 0.5f + 0.5f * std::cos (a + kPi * 0.75f);   // lit from top-left
        g.setColour ((i % 2 ? juce::Colours::black : juce::Colours::white).withAlpha (0.08f + 0.18f * (i % 2 ? 1.0f - light : light)));
        g.drawLine ({ c + dir * (rb * 0.80f), c + dir * (rb * 0.99f) }, 1.3f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - rb, c.y - rb, rb * 2, rb * 2, 1.0f);

    // skirt pointer notch
    {
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        g.setColour (th.knobPointer.withAlpha (0.9f));
        g.drawLine ({ c + dir * (rb * 0.80f), c + dir * (rb * 0.99f) }, 2.2f);
    }

    // cap (raised, slightly smaller) with metallic sheen
    const float rc = rb * 0.70f;
    Rectangle<float> capR (c.x - rc, c.y - rc, rc * 2, rc * 2);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (capR.translated (0, rc * 0.06f).expanded (rc * 0.04f));
    ColourGradient cg (lum (th.knobCap, 0.4f), capR.getX(), capR.getY(), lum (th.knobCap, -0.45f), capR.getRight(), capR.getBottom(), false);
    cg.addColour (0.5, th.knobCap);
    g.setGradientFill (cg);
    g.fillEllipse (capR);
    // concentric machining rings
    for (float k = 0.25f; k < 1.0f; k += 0.12f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawEllipse (capR.reduced (rc * (1.0f - k)), 0.7f);
    }
    // specular highlight
    {
        Rectangle<float> hl (c.x - rc * 0.75f, c.y - rc * 0.85f, rc * 1.1f, rc * 0.8f);
        ColourGradient hg (juce::Colours::white.withAlpha (0.38f), hl.getCentreX(), hl.getY(), juce::Colours::white.withAlpha (0.0f), hl.getCentreX(), hl.getBottom(), false);
        g.setGradientFill (hg);
        g.fillEllipse (hl);
    }
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (capR, 0.9f);

    // pointer line on the cap
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine ({ c + dir * (rc * 0.25f) + juce::Point<float> (0.7f, 0.7f), c + dir * (rc * 0.92f) + juce::Point<float> (0.7f, 0.7f) }, 3.0f);
    g.setColour (th.knobPointer);
    g.drawLine ({ c + dir * (rc * 0.25f), c + dir * (rc * 0.92f) }, 2.6f);
}

// ===================================================================== buttons
void HardwareLookAndFeel::drawButtonBackground (Graphics& g, juce::Button& b, const Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f, 2.5f);
    const float rad = 3.5f;
    const bool hasLed = (bool) b.getProperties().getWithDefault ("led", false);
    const bool lit = b.getToggleState();

    juce::Path p;
    p.addRoundedRectangle (r, rad);
    if (! down) softShadow (g, p, 2.0f, 4.0f, 0.55f);

    // bezel the button sits in
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.expanded (1.2f), rad + 1);

    auto face = r.translated (0, down ? 1.0f : 0.0f);
    auto col = (bool) b.getProperties().getWithDefault ("swatch", false) ? bg : th.buttonFace;
    if (over && ! down) col = col.brighter (0.06f);
    ColourGradient grad (lum (col, down ? -0.15f : 0.22f), 0, face.getY(), lum (col, down ? 0.05f : -0.22f), 0, face.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (face, rad);
    // top highlight edge
    g.setColour (juce::Colours::white.withAlpha (down ? 0.05f : 0.28f));
    g.drawLine (face.getX() + rad, face.getY() + 1.0f, face.getRight() - rad, face.getY() + 1.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (face, rad, 0.9f);

    if (hasLed)
    {
        const float d = juce::jmin (7.0f, face.getHeight() * 0.28f);
        Rectangle<float> led (face.getX() + 7, face.getCentreY() - d * 0.5f, d, d);
        if (lit)
        {
            g.setColour (th.led.withAlpha (0.35f));
            g.fillEllipse (led.expanded (d * 0.7f));
            g.setGradientFill (ColourGradient (th.led.brighter (0.8f), led.getCentreX(), led.getCentreY(), th.led, led.getRight(), led.getBottom(), true));
        }
        else
        {
            g.setColour (th.led.darker (0.85f).withAlpha (0.9f));
        }
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (led, 0.7f);
    }
}

void HardwareLookAndFeel::drawButtonText (Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool hasLed = (bool) b.getProperties().getWithDefault ("led", false);
    auto r = b.getLocalBounds().reduced (4, 2).translated (0, down ? 1 : 0);
    if (hasLed) r.removeFromLeft (juce::jmin (12, r.getWidth() / 6));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const auto txt = (bool) b.getProperties().getWithDefault ("swatch", false) ? b.findColour (juce::TextButton::textColourOffId) : th.buttonText;
    g.setColour (txt.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 1, 0.8f);
}

void HardwareLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    Rectangle<float> r (0, 0, (float) w, (float) h);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillRoundedRectangle (r.translated (0, 1), 3);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r.withTrimmedBottom (1), 3);
    g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.5f), 0, 0, juce::Colours::transparentBlack, 0, 6, false));
    g.fillRoundedRectangle (r.withTrimmedBottom (1), 3);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.withTrimmedBottom (1).reduced (0.5f), 3, 1);
    juce::Path arrow;
    const float ax = (float) w - 14, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

// ===================================================================== pads and LCD
void HardwareLookAndFeel::drawPad (Graphics& g, Rectangle<float> r, bool down, float flash, bool dragOver, const juce::String& text, bool hasSound)
{
    r = r.reduced (4);
    const float rad = 7.0f;
    // well the pad sits in
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.expanded (3), rad + 2);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (r.expanded (3.5f).translated (0, 0.8f), rad + 2, 1.0f);

    auto face = down ? r.reduced (1.5f).translated (0, 1.5f) : r;
    juce::Path p;
    p.addRoundedRectangle (face, rad);
    if (! down) softShadow (g, p, 2.5f, 5.0f, 0.6f);
    // soft rubber: low-contrast gradient with a matte highlight
    g.setGradientFill (ColourGradient (lum (th.padFace, down ? -0.15f : 0.18f), 0, face.getY(), lum (th.padFace, down ? -0.05f : -0.2f), 0, face.getBottom(), false));
    g.fillPath (p);
    g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (down ? 0.02f : 0.12f), 0, face.getY(), juce::Colours::transparentWhite, 0, face.getY() + face.getHeight() * 0.45f, false));
    g.fillRoundedRectangle (face.reduced (2), rad - 1);
    if (flash > 0.01f)
    {
        g.setColour (th.accent.withAlpha (flash * 0.55f));
        g.fillPath (p);
    }
    g.setColour (dragOver ? th.accent : juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (face, rad, dragOver ? 2.5f : 1.0f);

    g.setColour (th.padText.withAlpha (hasSound ? 0.95f : 0.4f));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (text, face.toNearestInt().reduced (6), juce::Justification::centred, 2);
}

void HardwareLookAndFeel::drawLcd (Graphics& g, Rectangle<float> r, const juce::String& l1, const juce::String& l2)
{
    // bezel
    g.setGradientFill (ColourGradient (Colour (0xff0b0b0b), 0, r.getY(), Colour (0xff2a2a2a), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.translated (0, 1), 5, 1);
    auto glass = r.reduced (7);
    g.setColour (th.lcdBg);
    g.fillRoundedRectangle (glass, 3);
    // character cells (unlit dot matrix background)
    auto text = glass.reduced (10, 8);
    const float cellW = text.getWidth() / 16.0f, cellH = text.getHeight() / 2.0f;
    g.setColour (th.lcdFg.withAlpha (0.06f));
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 16; ++col)
            g.fillRect (Rectangle<float> (text.getX() + (float) col * cellW + 1, text.getY() + (float) row * cellH + cellH * 0.12f, cellW - 2, cellH * 0.76f));
    // text with a slight glow
    const juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), cellH * 0.78f, juce::Font::plain));
    g.setFont (f);
    for (int row = 0; row < 2; ++row)
    {
        const auto& s = row == 0 ? l1 : l2;
        for (int col = 0; col < juce::jmin (16, s.length()); ++col)
        {
            Rectangle<float> cell (text.getX() + (float) col * cellW, text.getY() + (float) row * cellH, cellW, cellH);
            const auto ch = juce::String::charToString (s[col]);
            g.setColour (th.lcdFg.withAlpha (0.25f));
            g.drawText (ch, cell.translated (0.6f, 0.6f), juce::Justification::centred, false);
            g.setColour (th.lcdFg);
            g.drawText (ch, cell, juce::Justification::centred, false);
        }
    }
    // inner shadow and glass reflection
    g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, glass.getY(), juce::Colours::transparentBlack, 0, glass.getY() + 10, false));
    g.fillRoundedRectangle (glass, 3);
    g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (0.09f), glass.getX(), glass.getY(), juce::Colours::transparentWhite, glass.getCentreX(), glass.getBottom(), false));
    juce::Path refl;
    refl.addTriangle (glass.getX(), glass.getY(), glass.getX() + glass.getWidth() * 0.55f, glass.getY(), glass.getX(), glass.getBottom());
    g.fillPath (refl);
}
