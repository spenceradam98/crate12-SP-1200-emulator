// Colour themes: built-in presets plus user themes stored as small JSON files.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct Theme
{
    juce::String name;
    bool builtIn = false;

    // Each entry is one editable colour slot.
    juce::Colour panel, panelText, accent, buttonFace, buttonText, led, padFace, padText,
        lcdBg, lcdFg, faderCap, faderSlot, knobBody, knobCap, knobPointer, screen;

    struct Slot { const char* key; const char* label; juce::Colour Theme::* member; };
    static const std::vector<Slot>& slots();

    juce::var toVar() const;
    static Theme fromVar (const juce::var& v, const Theme& fallback);
};

class ThemeManager
{
public:
    ThemeManager();

    const std::vector<Theme>& builtIns() const { return presets; }
    std::vector<Theme> userThemes() const;
    juce::File userThemeDir() const;

    const Theme& current() const { return active; }
    void setCurrent (const Theme& t, bool remember = true);
    bool selectByName (const juce::String& name);

    bool saveUserTheme (const Theme& t);           // name taken from t.name
    bool deleteUserTheme (const juce::String& name);
    bool importTheme (const juce::File& f);
    bool exportTheme (const Theme& t, const juce::File& f) const;

private:
    std::vector<Theme> presets;
    Theme active;
    std::unique_ptr<juce::PropertiesFile> props;
};
