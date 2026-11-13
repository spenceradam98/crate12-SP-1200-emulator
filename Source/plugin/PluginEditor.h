#pragma once

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

class Crate12Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Crate12Editor (Crate12Processor&);
    ~Crate12Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void padDown (int pad, float velocity01);
    void padUp (int pad);
    void padMenu (int pad);
    void padFilesDropped (int pad, const juce::StringArray& files);
    c12::Engine& engine() { return proc.getEngine(); }

private:
    void timerCallback() override;
    void syncControlsFromEngine();
    void showSetupMenu();
    void showSpecialMenu();
    void showDiskMenu();
    void showSegmentMenu();
    void showSongMenu();
    void showSampleMenu();
    void askNumber (const juce::String& title, int minV, int maxV, int initial, std::function<void (int)> done);
    void chooseFile (bool save, const juce::String& patterns, std::function<void (juce::File)> done);

    Crate12Processor& proc;
    LcdDisplay lcd;
    juce::OwnedArray<PadButton> pads;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    juce::OwnedArray<juce::TextButton> bankButtons;
    juce::OwnedArray<juce::TextButton> modeButtons;

    juce::TextButton runStop { "RUN/STOP" }, record { "RECORD" }, songSeg { "SEGMENT" },
        tapTempo { "TAP TEMPO" }, tapRepeat { "TAP/REPEAT" }, eraseBtn { "ERASE" },
        setupBtn { "SET-UP" }, specialBtn { "SPECIAL" }, diskBtn { "DISK" },
        segmentBtn { "SEGMENT EDIT" }, songBtn { "SONG EDIT" }, sampleBtn { "SAMPLE" };
    juce::Slider tempo, number;
    juce::Label tempoLabel { {}, "TEMPO" }, numberLabel { {}, "SEG #" };
    juce::ComboBox swing, autoCorrect, metronome, syncMode, mixSel;
    juce::Label swingL { {}, "SWING" }, acL { {}, "AUTO CORRECT" }, metL { {}, "METRONOME" }, syncL { {}, "SYNC" }, mixL { {}, "MIX" };
    juce::TextButton defineMix { "DEFINE MIX" };

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<uint8_t> lastDisk;     // for "load segment #/sound #" from a chosen disk file
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Crate12Editor)
};
