#include "EditorDialogs.h"

using namespace c12;
using Lock = std::lock_guard<std::recursive_mutex>;

void askNumberAsync (const juce::String& title, int minV, int maxV, int initial, std::function<void (int)> done)
{
    auto* w = new juce::AlertWindow (title, juce::String (minV) + " - " + juce::String (maxV), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("v", juce::String (initial));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, minV, maxV, done] (int res) {
        if (res == 1) done (juce::jlimit (minV, maxV, w->getTextEditorContents ("v").getIntValue()));
    }), true);
}

// ===================================================================== step grid
StepGridEditor::StepGridEditor (Engine& e) : engine (e)
{
    addAndMakeVisible (title);
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* bb = banks.add (new juce::TextButton (juce::String ("BANK ") + bankLetter (b)));
        bb->getProperties().set ("led", true);
        bb->onClick = [this, b] { bank = b; refresh(); };
        addAndMakeVisible (bb);
    }
    const char* names[] = { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
    for (int i = 0; i < 8; ++i) grid.addItem (names[i], i + 1);
    { Lock l (engine.mutex()); const int q = (int) engine.project().globals.autoCorrect; grid.setSelectedItemIndex (q < 8 ? q : 4, juce::dontSendNotification); }
    grid.onChange = [this] { refresh(); };
    addAndMakeVisible (grid);
    segNum.setSliderStyle (juce::Slider::IncDecButtons);
    segNum.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    segNum.setRange (0, 99, 1);
    segNum.setValue (engine.getSegment(), juce::dontSendNotification);
    segNum.onValueChange = [this] { engine.setSegment ((int) segNum.getValue()); refresh(); };
    addAndMakeVisible (segNum);
    eraseSeg.onClick = [this] {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Erase segment", "Erase every note in this segment?", "Erase", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) { engine.eraseSegment (engine.getSegment()); refresh(); } }));
    };
    lengthBtn.onClick = [this] {
        Segment s; { Lock l (engine.mutex()); s = engine.project().segments[(size_t) engine.getSegment()]; }
        askNumberAsync ("Bars", 0, 99, s.bars, [this, s] (int bars) {
            askNumberAsync ("Extra beats", 0, 99, s.extraBeats, [this, bars] (int beats) { engine.setSegmentLength (engine.getSegment(), bars, beats, false); refresh(); }); });
    };
    addAndMakeVisible (eraseSeg);
    addAndMakeVisible (lengthBtn);
    view.setViewedComponent (&canvas, false);
    view.setScrollBarsShown (false, true);
    addAndMakeVisible (view);
    setSize (980, 420);
    refresh();
    startTimerHz (20);
}

StepGridEditor::~StepGridEditor() { stopTimer(); }

int StepGridEditor::gridTicks() const { return autoCorrectTicks ((AutoCorrect) juce::jmax (0, grid.getSelectedItemIndex())); }

void StepGridEditor::refresh()
{
    Segment s; { Lock l (engine.mutex()); s = engine.project().segments[(size_t) engine.getSegment()]; }
    const int cols = juce::jmax (1, s.lengthTicks() / gridTicks());
    canvas.setSize (kNameW + cols * kCellW + 4, kRowH * kPadsPerBank + 22);
    title.setText ("Segment " + juce::String (engine.getSegment()).paddedLeft ('0', 2) + "   " + juce::String (s.bars) + " bars + " + juce::String (s.extraBeats)
                       + " beats   " + juce::String (s.beatsPerBar) + "/" + juce::String (s.beatUnit) + "   " + juce::String ((int) s.events.size()) + " notes",
                   juce::dontSendNotification);
    for (int b = 0; b < kNumBanks; ++b) banks[b]->setToggleState (b == bank, juce::dontSendNotification);
    canvas.repaint();
}

void StepGridEditor::timerCallback()
{
    int bar, beat;
    const int t = engine.isPlaying() && ! engine.isSongMode() ? engine.currentBeatInfo (bar, beat) : -1;
    if (t != playTick) { playTick = t; canvas.repaint(); }
}

void StepGridEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.2f));
}

void StepGridEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    segNum.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (10);
    for (auto* b : banks) b->setBounds (top.removeFromLeft (90).reduced (2, 0));
    top.removeFromLeft (10);
    grid.setBounds (top.removeFromLeft (90));
    eraseSeg.setBounds (top.removeFromRight (140).reduced (2, 0));
    lengthBtn.setBounds (top.removeFromRight (100).reduced (2, 0));
    r.removeFromTop (6);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    view.setBounds (r);
}

void StepGridEditor::Canvas::paint (juce::Graphics& g)
{
    auto& o = owner;
    Segment s; std::array<std::string, kNumSounds> names;
    {
        Lock l (o.engine.mutex());
        s = o.engine.project().segments[(size_t) o.engine.getSegment()];
        for (int i = 0; i < kNumSounds; ++i) names[(size_t) i] = o.engine.project().sounds[(size_t) i].name;
    }
    const auto& lf = getLookAndFeel();
    const auto text = lf.findColour (juce::Label::textColourId);
    const auto accent = lf.findColour (juce::PopupMenu::highlightedBackgroundColourId);
    const int gt = o.gridTicks();
    const int cols = juce::jmax (1, s.lengthTicks() / gt);
    const int tpb = s.ticksPerBeat();

    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (int c = 0; c < cols; ++c)
    {
        const int tick = c * gt;
        if (tick % tpb == 0)
        {
            g.setColour (text.withAlpha (0.7f));
            g.drawText (juce::String (tick / (tpb * s.beatsPerBar) + 1) + "." + juce::String ((tick / tpb) % s.beatsPerBar + 1),
                        kNameW + c * kCellW, 0, kCellW * 2, 18, juce::Justification::centredLeft);
        }
    }
    for (int row = 0; row < kPadsPerBank; ++row)
    {
        const int snd = soundIndex (o.bank, row);
        const int y = 20 + row * kRowH;
        g.setColour (text);
        g.drawText (juce::String::charToString (bankLetter (o.bank)) + juce::String (row + 1) + " " + juce::String (names[(size_t) snd]),
                    4, y, kNameW - 8, kRowH, juce::Justification::centredLeft);
        for (int c = 0; c < cols; ++c)
        {
            const int t0 = c * gt, t1 = t0 + gt;
            int vel = 0;
            for (auto& e : s.events) if (e.sound == snd && e.tick >= t0 && e.tick < t1) vel = std::max (vel, (int) e.velocity);
            juce::Rectangle<float> cell ((float) (kNameW + c * kCellW + 1), (float) (y + 3), (float) kCellW - 3, (float) kRowH - 6);
            const bool beatStart = t0 % tpb == 0, barStart = t0 % (tpb * s.beatsPerBar) == 0;
            g.setColour (juce::Colours::black.withAlpha (barStart ? 0.55f : beatStart ? 0.42f : 0.3f));
            g.fillRoundedRectangle (cell, 3.0f);
            if (vel > 0)
            {
                g.setColour (accent.withAlpha (0.35f + 0.65f * (float) vel / 127.0f));
                g.fillRoundedRectangle (cell.reduced (2), 2.5f);
            }
            if (o.playTick >= t0 && o.playTick < t1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.fillRoundedRectangle (cell, 3.0f);
            }
        }
    }
}

void StepGridEditor::Canvas::mouseDown (const juce::MouseEvent& e)
{
    auto& o = owner;
    const int c = (e.x - kNameW) / kCellW, row = (e.y - 20) / kRowH;
    if (c < 0 || row < 0 || row >= kPadsPerBank || e.x < kNameW) return;
    const int gt = o.gridTicks(), t0 = c * gt, seg = o.engine.getSegment();
    const int snd = soundIndex (o.bank, row);
    bool any = false;
    std::vector<int> ticks;
    {
        Lock l (o.engine.mutex());
        for (auto& ev : o.engine.project().segments[(size_t) seg].events)
            if (ev.sound == snd && ev.tick >= t0 && ev.tick < t0 + gt) { any = true; ticks.push_back (ev.tick); }
    }
    if (any) for (int t : ticks) o.engine.stepErase (seg, t, snd);
    else
    {
        const int vel = e.mods.isShiftDown() ? 64 : 127;
        o.engine.stepInsert (seg, t0, snd, vel);
        o.engine.triggerSound (snd, vel);
    }
    o.refresh();
}

// ===================================================================== song editor
static juce::String stepText (const SongStep& s)
{
    switch (s.type)
    {
        case SongStep::Type::Segment:     return "Segment " + juce::String (s.a).paddedLeft ('0', 2);
        case SongStep::Type::RepeatBegin: return "|-  Repeat x" + juce::String (s.a);
        case SongStep::Type::RepeatEnd:   return "-|  End repeat";
        case SongStep::Type::MixChange:   return "Mix change -> Mix " + juce::String (s.a + 1);
        case SongStep::Type::TempoChange: return juce::String (s.a == 2 ? "Ritardando  -" : "Accelerando +") + juce::String (s.b) + " BPM over " + juce::String (s.c) + " beats";
        case SongStep::Type::SubSong:     return "Sub-song " + juce::String (s.a).paddedLeft ('0', 2);
        case SongStep::Type::TriggerOut:  return "Trigger out";
        case SongStep::Type::End:         return s.a == 0 ? "END (stop)" : s.a == 1 ? "END (repeat song)" : "END (link to song " + juce::String (s.b).paddedLeft ('0', 2) + ")";
    }
    return {};
}

SongEditor::SongEditor (Engine& e) : engine (e)
{
    for (auto* s : { &songNum, &tempo, &firstStep })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 22);
        addAndMakeVisible (s);
    }
    songNum.setRange (0, 99, 1);
    songNum.setValue (engine.getSong(), juce::dontSendNotification);
    songNum.onValueChange = [this] { engine.setSong (song()); refresh(); };
    tempo.setRange (0, kMaxTempo, 0.1);
    tempo.onValueChange = [this] { engine.setSongTempo (song(), tempo.getValue() < kMinTempo ? 0.0 : tempo.getValue()); };
    firstStep.setRange (1, 999, 1);
    firstStep.onValueChange = [this] { engine.setFirstSongStep ((int) firstStep.getValue() - 1); };
    for (auto* l : { &songL, &tempoL, &firstL }) { l->setFont (juce::FontOptions (11.0f, juce::Font::bold)); addAndMakeVisible (l); }

    list.setModel (this);
    list.setRowHeight (24);
    addAndMakeVisible (list);

    using T = SongStep::Type;
    struct B { const char* name; std::function<void()> fn; };
    std::vector<B> defs {
        { "SEGMENT", [this] { askNumberAsync ("Segment #", 0, 99, engine.getSegment(), [this] (int v) { insert ({ T::Segment, v, 0, 0 }); }); } },
        { "REPEAT", [this] { askNumberAsync ("Repeat count", 2, 99, 2, [this] (int v) { insert ({ T::RepeatBegin, v, 0, 0 }); }); } },
        { "END REPEAT", [this] { insert ({ T::RepeatEnd, 0, 0, 0 }); } },
        { "MIX CHANGE", [this] { askNumberAsync ("Mix #", 1, 8, 1, [this] (int v) { insert ({ T::MixChange, v - 1, 0, 0 }); }); } },
        { "ACCELERANDO", [this] { askNumberAsync ("BPM increase", 1, 99, 10, [this] (int b) { askNumberAsync ("Over beats", 1, 32, 8, [this, b] (int c) { insert ({ T::TempoChange, 1, b, c }); }); }); } },
        { "RITARDANDO", [this] { askNumberAsync ("BPM decrease", 1, 99, 10, [this] (int b) { askNumberAsync ("Over beats", 1, 32, 8, [this, b] (int c) { insert ({ T::TempoChange, 2, b, c }); }); }); } },
        { "SUB-SONG", [this] { askNumberAsync ("Sub-song #", 0, 99, 0, [this] (int v) { insert ({ T::SubSong, v, 0, 0 }); }); } },
        { "TRIGGER", [this] { insert ({ T::TriggerOut, 0, 0, 0 }); } },
        { "END: STOP", [this] { insert ({ T::End, 0, 0, 0 }); } },
        { "END: REPEAT", [this] { insert ({ T::End, 1, 0, 0 }); } },
        { "END: LINK", [this] { askNumberAsync ("Link to song #", 0, 99, 0, [this] (int v) { insert ({ T::End, 2, v, 0 }); }); } },
        { "DELETE", [this] { const int r = list.getSelectedRow(); if (r >= 0) { engine.songDeleteStep (song(), r); refresh(); list.selectRow (juce::jmin (r, getNumRows() - 1)); } } },
        { "MOVE UP", [this] {
              const int r = list.getSelectedRow();
              if (r <= 0) return;
              const auto st = steps[(size_t) r];
              engine.songDeleteStep (song(), r); engine.songInsertStep (song(), r - 1, st); refresh(); list.selectRow (r - 1); } },
        { "MOVE DOWN", [this] {
              const int r = list.getSelectedRow();
              if (r < 0 || r >= getNumRows() - 1) return;
              const auto st = steps[(size_t) r];
              engine.songDeleteStep (song(), r); engine.songInsertStep (song(), r + 1, st); refresh(); list.selectRow (r + 1); } },
        { "CLEAR SONG", [this] { engine.songClear (song()); refresh(); } },
    };
    for (auto& d : defs)
    {
        auto* b = buttons.add (new juce::TextButton (d.name));
        b->onClick = d.fn;
        addAndMakeVisible (b);
    }
    setSize (760, 520);
    refresh();
    startTimerHz (10);
}

SongEditor::~SongEditor() { stopTimer(); list.setModel (nullptr); }

void SongEditor::insert (SongStep st)
{
    const int r = list.getSelectedRow();
    const int at = r >= 0 ? r + 1 : getNumRows();
    engine.songInsertStep (song(), at, st);
    refresh();
    list.selectRow (at);
}

void SongEditor::refresh()
{
    Song s; int first = 0;
    { Lock l (engine.mutex()); s = engine.project().songs[(size_t) song()]; first = engine.project().firstSongStep; }
    steps = s.steps;
    tempo.setValue (s.tempo, juce::dontSendNotification);
    firstStep.setValue (first + 1, juce::dontSendNotification);
    list.updateContent();
    list.repaint();
}

void SongEditor::timerCallback() { list.repaint(); }

int SongEditor::getNumRows() { return (int) steps.size(); }

void SongEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) steps.size()) return;
    const auto& lf = getLookAndFeel();
    if (selected) { g.setColour (lf.findColour (juce::PopupMenu::highlightedBackgroundColourId)); g.fillRect (0, 0, w, h); }
    g.setColour (selected ? lf.findColour (juce::PopupMenu::highlightedTextColourId) : lf.findColour (juce::Label::textColourId));
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    g.drawText (juce::String (row + 1).paddedLeft ('0', 2) + "   " + stepText (steps[(size_t) row]), 8, 0, w - 16, h, juce::Justification::centredLeft);
}

void SongEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.2f));
}

void SongEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (44);
    auto col = [&top] (juce::Label& l, juce::Component& c, int w) { auto a = top.removeFromLeft (w); l.setBounds (a.removeFromTop (16)); c.setBounds (a.removeFromTop (26).reduced (0, 1)); top.removeFromLeft (16); };
    col (songL, songNum, 130); col (tempoL, tempo, 170); col (firstL, firstStep, 140);
    r.removeFromTop (8);
    auto side = r.removeFromRight (170);
    for (auto* b : buttons) { b->setBounds (side.removeFromTop (30).reduced (2)); }
    r.removeFromRight (8);
    list.setBounds (r);
}
