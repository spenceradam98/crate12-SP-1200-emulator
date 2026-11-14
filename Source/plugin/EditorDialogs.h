// Larger editing windows: step-edit grid for segments, and the song step list editor.
#pragma once

#include "Engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

void askNumberAsync (const juce::String& title, int minV, int maxV, int initial, std::function<void (int)> done);

class StepGridEditor : public juce::Component, private juce::Timer
{
public:
    explicit StepGridEditor (c12::Engine& e);
    ~StepGridEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Canvas : public juce::Component
    {
        StepGridEditor& owner;
        explicit Canvas (StepGridEditor& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    };
    void timerCallback() override;
    void refresh();
    int  gridTicks() const;

    c12::Engine& engine;
    juce::Label title;
    juce::OwnedArray<juce::TextButton> banks;
    juce::ComboBox grid;
    juce::Slider segNum;
    juce::TextButton eraseSeg { "ERASE SEGMENT" }, lengthBtn { "LENGTH..." };
    juce::Viewport view;
    Canvas canvas { *this };
    int bank = 0, playTick = -1;
    static constexpr int kCellW = 26, kRowH = 34, kNameW = 110;
    friend struct Canvas;
};

class SongEditor : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit SongEditor (c12::Engine& e);
    ~SongEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void timerCallback() override;
    void insert (c12::SongStep st);
    void refresh();
    int song() const { return (int) songNum.getValue(); }

    c12::Engine& engine;
    juce::Slider songNum, tempo, firstStep;
    juce::Label songL { {}, "SONG #" }, tempoL { {}, "SONG TEMPO (0 = none)" }, firstL { {}, "FIRST STEP" };
    juce::ListBox list;
    juce::OwnedArray<juce::TextButton> buttons;
    std::vector<c12::SongStep> steps;
};
