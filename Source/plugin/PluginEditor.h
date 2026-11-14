#pragma once

#include "EditorDialogs.h"
#include "FrontPanel.h"
#include "HardwareLookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class Crate12Editor;

// Velocity-sensitive pad: velocity comes from where you click (top = loudest) when Dynamic Buttons is on.
class PadButton : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    PadButton (Crate12Editor& ed, int index) : editor (ed), pad (index) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    float flash = 0;
    juce::String label;
    bool hasSound = false;
private:
    Crate12Editor& editor;
    int pad;
    bool down = false, dragOver = false;
};

class LcdDisplay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    juce::String line1, line2;
};

// Colour-scheme editor: one swatch per theme slot, live preview, save / import / export.
class ThemeEditor : public juce::Component
{
public:
    ThemeEditor (ThemeManager& tm, Theme start, std::function<void (const Theme&, bool remember)> apply);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void refresh();
    ThemeManager& themes;
    Theme working;
    std::function<void (const Theme&, bool)> apply;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::TextButton> swatches;
    juce::ComboBox base;
    juce::Label baseLabel { {}, "Start from" }, nameLabel { {}, "Theme name" };
    juce::TextEditor name;
    juce::TextButton save { "SAVE THEME" }, exportBtn { "EXPORT FILE..." }, importBtn { "IMPORT FILE..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

// Button that tells a right-click apart from a normal click (right-click opens the quick menu).
struct DualButton : public juce::TextButton
{
    using juce::TextButton::TextButton;
    bool popupClick = false;
    void clicked (const juce::ModifierKeys& m) override { popupClick = m.isPopupMenu(); }
};

class Crate12Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    friend class FrontPanel;
    void startLampTest() { lampTestUntil = juce::Time::getMillisecondCounter() + 2500; }
    bool keyPressed (const juce::KeyPress&) override;
    explicit Crate12Editor (Crate12Processor&);
    ~Crate12Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void padDown (int pad, float velocity01);
    void padUp (int pad);
    void padMenu (int pad);
    void padFilesDropped (int pad, const juce::StringArray& files);
    c12::Engine& engine() { return proc.getEngine(); }
    HardwareLookAndFeel& hardware() { return lnf; }
    void applyTheme (const Theme& t, bool remember);
    void diskFunction (int hardwareNumber);   // DISK 0-9

private:
    void showThemeMenu();
    void openThemeEditor();
    void timerCallback() override;
    void syncControlsFromEngine();
    void showSetupMenu();
    void showSpecialMenu();
    void showDiskMenu();
    void diskAction (int menuId);
    void showSegmentMenu();
    void showSongMenu();
    void showSampleMenu();
    void askNumber (const juce::String& title, int minV, int maxV, int initial, std::function<void (int)> done);
    void chooseFile (bool save, const juce::String& patterns, std::function<void (juce::File)> done);

    Crate12Processor& proc;
    ThemeManager themes;          // declared before any component so it outlives them
    HardwareLookAndFeel lnf;
    LcdDisplay lcd;
    juce::OwnedArray<PadButton> pads;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::TextButton> bankButtons;
    juce::OwnedArray<juce::TextButton> modeButtons;

    juce::TextButton runStop { "RUN/STOP" }, record { "RECORD" }, songSeg { "SEGMENT" },
        tapTempo { "TAP TEMPO" }, tapRepeat { "TAP/REPEAT" }, eraseBtn { "ERASE" }, themeBtn { "THEME" };
    DualButton setupBtn { "SET-UP" }, specialBtn { "SPECIAL" }, diskBtn { "DISK" },
        segmentBtn { "STEP EDIT" }, songBtn { "SONG EDIT" }, sampleBtn { "SAMPLE" },
        syncBtn { "SYNC" }, segLenBtn { "SEG LENGTH" }, stepBtn { "STEP PROG" }, copyBtn { "COPY" };
    juce::OwnedArray<juce::TextButton> keypad;   // 1-9, NO, 0, YES, <, ENTER, >
    juce::Rectangle<int> keypadFrame;
    FrontPanel panel { *this };
    juce::uint32 lampTestUntil = 0;
    juce::Component::SafePointer<juce::DialogWindow> gridWindow, songWindow;
    void openWindow (juce::Component::SafePointer<juce::DialogWindow>& slot, juce::Component* content, const juce::String& title);
    juce::Slider tempo, volume, number;
    juce::Label tempoLabel { {}, "TEMPO" }, volumeLabel { {}, "VOLUME" }, numberLabel { {}, "SEG #" };
    juce::Component::SafePointer<juce::DialogWindow> themeWindow;
    juce::ComboBox swing, autoCorrect, metronome, syncMode, mixSel;
    juce::Label swingL { {}, "SWING" }, acL { {}, "AUTO CORRECT" }, metL { {}, "METRONOME" }, syncL { {}, "SYNC" }, mixL { {}, "MIX" };
    juce::TextButton defineMix { "DEFINE MIX" };

    juce::Rectangle<int> transportFrame, performFrame, sliderFrame, padFrame;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<uint8_t> lastDisk;     // for "load segment #/sound #" from a chosen disk file
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Crate12Editor)
};
