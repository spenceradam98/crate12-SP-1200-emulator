#include "Theme.h"

#include <cstdlib>

namespace
{
juce::Colour c (juce::uint32 argb) { return juce::Colour (0xff000000 | argb); }

Theme make (const char* name, juce::uint32 panel, juce::uint32 panelText, juce::uint32 accent,
            juce::uint32 buttonFace, juce::uint32 buttonText, juce::uint32 led,
            juce::uint32 padFace, juce::uint32 padText, juce::uint32 lcdBg, juce::uint32 lcdFg,
            juce::uint32 faderCap, juce::uint32 faderSlot, juce::uint32 knobBody, juce::uint32 knobCap,
            juce::uint32 knobPointer, juce::uint32 screen)
{
    Theme t;
    t.name = name; t.builtIn = true;
    t.panel = c (panel); t.panelText = c (panelText); t.accent = c (accent);
    t.buttonFace = c (buttonFace); t.buttonText = c (buttonText); t.led = c (led);
    t.padFace = c (padFace); t.padText = c (padText); t.lcdBg = c (lcdBg); t.lcdFg = c (lcdFg);
    t.faderCap = c (faderCap); t.faderSlot = c (faderSlot); t.knobBody = c (knobBody); t.knobCap = c (knobCap);
    t.knobPointer = c (knobPointer); t.screen = c (screen);
    return t;
}

std::vector<Theme> makePresets()
{
    //            name                panel     text      accent    btnFace   btnText   led       pad       padText   lcdBg     lcdFg     fadCap    slot      knobBody  knobCap   pointer   screen
    return {
        make ("Graphite",           0x2b2d31, 0xe6e2d6, 0xe8792b, 0x45484e, 0xeeeeee, 0xff5a2a, 0x55585f, 0xf0ece0, 0x1d2b12, 0xb6ff5a, 0xb9bcc2, 0x111214, 0x24262a, 0x9a9da3, 0xffffff, 0x6c7078),
        make ("Studio Grey 87",     0x8e8f8a, 0x1c1c1c, 0xc8312b, 0xd9d6cc, 0x1a1a1a, 0xff2b1e, 0x3b3d40, 0xe9e6dc, 0x26341c, 0xc4ff6e, 0xe7e4da, 0x2a2a2a, 0x2d2e30, 0xbfbfbf, 0xf2f2f2, 0x2a2a2a),
        make ("Cream Machine",      0xe6dcc4, 0x3a2f24, 0xd0542a, 0xf3ecd9, 0x3a2f24, 0xff4a1a, 0x8a7d68, 0xfbf6e8, 0x303d22, 0xd7ff8a, 0xf6f0e0, 0x4a4036, 0x3a332c, 0xd8cfbc, 0xffffff, 0x9a8a70),
        make ("Midnight",           0x14161c, 0xc9d1e0, 0x5aa9ff, 0x252a35, 0xdfe6f2, 0x3ec7ff, 0x2c3240, 0xe2e8f5, 0x08141f, 0x5ad2ff, 0x9aa3b5, 0x050608, 0x1b1e26, 0x8c94a6, 0x5aa9ff, 0x3a4256),
        make ("Walnut Tape",        0x4a2f1e, 0xf1e2c6, 0xe0a33a, 0x2f2b27, 0xf1e2c6, 0xffb43a, 0x2a2622, 0xf1e2c6, 0x231a0c, 0xffc35a, 0xd8d2c4, 0x1a120a, 0x1e1b18, 0xc0b8a8, 0xfff0d0, 0xa98055),
        make ("Mint Console",       0xa9d6c5, 0x1f3b33, 0xe2574c, 0xe9f4ef, 0x1f3b33, 0xff3b30, 0x3f6b5f, 0xf2fbf7, 0x173027, 0x8fffd0, 0xf2f5f3, 0x22352f, 0x2a3e38, 0xd5e2dc, 0xffffff, 0x2f5d50),
        make ("Navy Rack",          0x1f2d4a, 0xe8edf7, 0xf2c14e, 0x2f4166, 0xf2f5fb, 0xffd35a, 0x34466b, 0xf2f5fb, 0x0b1424, 0xffd35a, 0xc9d2e3, 0x0b1220, 0x172238, 0xaeb8cc, 0xf2c14e, 0x5b6f99),
        make ("Arcade Purple",      0x2a1740, 0xf1e6ff, 0xff4fd8, 0x3d2160, 0xffffff, 0xff4fd8, 0x4a2a75, 0xffffff, 0x12081f, 0x58ffe6, 0xd9c8f0, 0x0d0616, 0x231338, 0xb79ce0, 0x58ffe6, 0x7c4fb3),
        make ("Vaporwave",          0x2d2a4a, 0xfde7ff, 0xff71ce, 0x403b6b, 0xfde7ff, 0x01cdfe, 0x4f4a85, 0xfde7ff, 0x16142b, 0x05ffa1, 0xe6d9ff, 0x14122a, 0x2a2648, 0xb967ff, 0x01cdfe, 0x8c7fd9),
        make ("Desert Sand",        0xcdb48e, 0x3b2a18, 0x9b3d1f, 0xe8d8bc, 0x3b2a18, 0xff5a1f, 0x7a6449, 0xfaf0dc, 0x3a2d14, 0xffd27a, 0xefe3cc, 0x3b2a18, 0x4a3a28, 0xd9c6a4, 0xfff5e0, 0x8a6d47),
        make ("Racing Red",         0xa3201e, 0xfff3ee, 0xffd23f, 0x2b2b2b, 0xffffff, 0xffd23f, 0x2f2f2f, 0xffffff, 0x1a0a0a, 0xff6a4a, 0xdedede, 0x1a1a1a, 0x1f1f1f, 0xc9c9c9, 0xffd23f, 0x5e1110),
        make ("Olive Field",        0x4b5232, 0xe8e6d0, 0xd99a2b, 0x5c6440, 0xf0eed8, 0xffb02b, 0x363b24, 0xf0eed8, 0x1b1f10, 0xc8ff5a, 0xc8c6b0, 0x1a1c10, 0x2a2e1c, 0xa9a68c, 0xf0eed8, 0x8a9160),
        make ("Ice White",          0xeef1f4, 0x2a3038, 0x2b8cff, 0xffffff, 0x2a3038, 0x2b8cff, 0xd5dbe2, 0x2a3038, 0x1c2733, 0x9fd4ff, 0xffffff, 0x9aa4b0, 0xc5ccd5, 0xf5f7fa, 0x2b8cff, 0xaab3bf),
        make ("Amber CRT",          0x1b1712, 0xffb347, 0xffb347, 0x2a241c, 0xffcf8a, 0xffa020, 0x2e271e, 0xffcf8a, 0x120c04, 0xffb347, 0x9a8a72, 0x0a0805, 0x231d16, 0x8a7a62, 0xffb347, 0x5a4a32),
        make ("Phosphor Green",     0x0f1a12, 0x7dff9a, 0x39ff6a, 0x18291c, 0xa8ffbb, 0x39ff6a, 0x1c3322, 0xa8ffbb, 0x041006, 0x39ff6a, 0x8aa592, 0x040805, 0x13241a, 0x6f8c78, 0x39ff6a, 0x2f5a3a),
        make ("Ocean",              0x0f4c5c, 0xe6fbff, 0xff9f43, 0x15697f, 0xe6fbff, 0xffae5a, 0x1b5f70, 0xe6fbff, 0x062730, 0x6ff3ff, 0xd6e8ec, 0x062730, 0x0c3a46, 0xa6c8d0, 0xff9f43, 0x3d8fa3),
        make ("Sunset",             0x3a1c2e, 0xffe9d6, 0xff7a3d, 0x57263f, 0xffe9d6, 0xffb03d, 0x6b2f4c, 0xffe9d6, 0x1e0c15, 0xffb86b, 0xf0d6c8, 0x1e0c15, 0x2e1524, 0xd9a89a, 0xff7a3d, 0xa34a5f),
        make ("Monochrome",         0x202020, 0xf0f0f0, 0xffffff, 0x3a3a3a, 0xffffff, 0xffffff, 0x444444, 0xffffff, 0x0c0c0c, 0xe8e8e8, 0xd0d0d0, 0x080808, 0x1c1c1c, 0xa8a8a8, 0xffffff, 0x666666),
        make ("Bubblegum",          0xf7c6d9, 0x4a1f33, 0x2fb7a8, 0xfde6ef, 0x4a1f33, 0xff3d8b, 0xe28fb0, 0x4a1f33, 0x3a1427, 0xffc6e0, 0xffffff, 0x8a4a66, 0xd77aa0, 0xfde6ef, 0x2fb7a8, 0xc9709a),
        make ("Brushed Steel",      0x9ea3a8, 0x161819, 0xff6b1a, 0xc7cbcf, 0x161819, 0xff4a1a, 0x5d6166, 0xf4f6f7, 0x1b2416, 0xa9ff6a, 0xe3e6e8, 0x232527, 0x3a3d40, 0xd3d6d9, 0xff6b1a, 0x5d6166),
    };
}
} // namespace

const std::vector<Theme::Slot>& Theme::slots()
{
    static const std::vector<Slot> s {
        { "panel", "Panel", &Theme::panel },
        { "panelText", "Panel text", &Theme::panelText },
        { "screen", "Panel lines and screws", &Theme::screen },
        { "accent", "Accent", &Theme::accent },
        { "buttonFace", "Button face", &Theme::buttonFace },
        { "buttonText", "Button text", &Theme::buttonText },
        { "led", "Button LED", &Theme::led },
        { "padFace", "Pad", &Theme::padFace },
        { "padText", "Pad text", &Theme::padText },
        { "lcdBg", "Display background", &Theme::lcdBg },
        { "lcdFg", "Display text", &Theme::lcdFg },
        { "faderCap", "Fader cap", &Theme::faderCap },
        { "faderSlot", "Fader slot", &Theme::faderSlot },
        { "knobBody", "Knob body", &Theme::knobBody },
        { "knobCap", "Knob cap", &Theme::knobCap },
        { "knobPointer", "Knob pointer", &Theme::knobPointer },
    };
    return s;
}

juce::var Theme::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("format", "crate12-theme-1");
    for (auto& s : slots()) o->setProperty (s.key, "#" + (this->*(s.member)).toDisplayString (false));
    return juce::var (o);
}

Theme Theme::fromVar (const juce::var& v, const Theme& fallback)
{
    Theme t = fallback;
    t.builtIn = false;
    if (auto* o = v.getDynamicObject())
    {
        t.name = o->getProperty ("name").toString().trim();
        for (auto& s : slots())
        {
            const auto str = o->getProperty (s.key).toString().trimCharactersAtStart ("#");
            if (str.length() == 6 && str.containsOnly ("0123456789abcdefABCDEF"))
                t.*(s.member) = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) str.getHexValue32()));
        }
    }
    if (t.name.isEmpty()) t.name = "Custom";
    return t;
}

// ===================================================================== manager
ThemeManager::ThemeManager() : presets (makePresets()), active (presets.front())
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Crate12";
    o.filenameSuffix = ".settings";
    o.folderName = "Crate12";
    o.osxLibrarySubFolder = "Application Support";
    props = std::make_unique<juce::PropertiesFile> (o);
    selectByName (props->getValue ("theme", presets.front().name));
    // Developer aid for screenshots: CRATE12_THEME=<index or name> picks a theme without saving it.
    if (auto* env = std::getenv ("CRATE12_THEME"))
    {
        const juce::String v (env);
        if (v.containsOnly ("0123456789") && v.getIntValue() < (int) presets.size()) active = presets[(size_t) v.getIntValue()];
        else selectByName (v);
    }
}

juce::File ThemeManager::userThemeDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Crate12").getChildFile ("Themes");
    dir.createDirectory();
    return dir;
}

std::vector<Theme> ThemeManager::userThemes() const
{
    std::vector<Theme> out;
    auto files = userThemeDir().findChildFiles (juce::File::findFiles, false, "*.c12theme");
    files.sort();
    for (auto& f : files)
    {
        const auto v = juce::JSON::parse (f.loadFileAsString());
        if (v.isObject()) out.push_back (Theme::fromVar (v, presets.front()));
    }
    return out;
}

void ThemeManager::setCurrent (const Theme& t, bool remember)
{
    active = t;
    if (remember && props) { props->setValue ("theme", t.name); props->saveIfNeeded(); }
}

bool ThemeManager::selectByName (const juce::String& name)
{
    for (auto& t : presets) if (t.name == name) { setCurrent (t, false); return true; }
    for (auto& t : userThemes()) if (t.name == name) { setCurrent (t, false); return true; }
    return false;
}

static juce::File fileFor (const juce::File& dir, const juce::String& name)
{
    return dir.getChildFile (juce::File::createLegalFileName (name) + ".c12theme");
}

bool ThemeManager::saveUserTheme (const Theme& t)
{
    return fileFor (userThemeDir(), t.name).replaceWithText (juce::JSON::toString (t.toVar()));
}

bool ThemeManager::deleteUserTheme (const juce::String& name) { return fileFor (userThemeDir(), name).deleteFile(); }

bool ThemeManager::importTheme (const juce::File& f)
{
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    auto t = Theme::fromVar (v, presets.front());
    for (auto& p : presets) if (p.name == t.name) t.name << " (custom)";   // never shadow a preset
    return saveUserTheme (t);
}

bool ThemeManager::exportTheme (const Theme& t, const juce::File& f) const
{
    return f.replaceWithText (juce::JSON::toString (t.toVar()));
}
