#include "PluginEditor.h"

using namespace c12;

namespace
{

template <typename T> T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }
using Lock = std::lock_guard<std::recursive_mutex>;
} // namespace

// ===================================================================== pads / lcd
void PadButton::paint (juce::Graphics& g)
{
    editor.hardware().drawPad (g, getLocalBounds().toFloat(), down, flash, dragOver, label, hasSound);
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { editor.padMenu (pad); return; }
    down = true;
    repaint();
    const float vel = 1.0f - 0.75f * clampv ((float) e.y / (float) std::max (1, getHeight()), 0.0f, 1.0f);
    editor.padDown (pad, vel);
}

void PadButton::mouseUp (const juce::MouseEvent&)
{
    if (! down) return;
    down = false;
    repaint();
    editor.padUp (pad);
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    editor.padFilesDropped (pad, files);
}

void LcdDisplay::paint (juce::Graphics& g)
{
    if (auto* hw = dynamic_cast<HardwareLookAndFeel*> (&getLookAndFeel()))
        hw->drawLcd (g, getLocalBounds().toFloat(), line1, line2);
}

// ===================================================================== editor
Crate12Editor::Crate12Editor (Crate12Processor& p) : AudioProcessorEditor (&p), proc (p)
{
    lnf.setTheme (themes.current());
    setLookAndFeel (&lnf);
    addAndMakeVisible (lcd);

    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto* pad = pads.add (new PadButton (*this, i));
        addAndMakeVisible (pad);

        auto* s = sliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
        s->setRange (0.0, 1.0);
        s->onValueChange = [this, i, s] {
            if (updating) return;
            if (panel.slider (i, (float) s->getValue())) return;      // a keypad function owns the sliders
            engine().setSlider (i, (float) s->getValue());
            proc.pushParam (Crate12Processor::kParamFader1 + i, (float) s->getValue());
        };
        addAndMakeVisible (s);
        auto* l = sliderLabels.add (new juce::Label ({}, juce::String (i + 1)));
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* bb = bankButtons.add (new juce::TextButton (juce::String ("BANK ") + bankLetter (b)));
        bb->setClickingTogglesState (false);
        bb->getProperties().set ("led", true);
        bb->onClick = [this, b] { engine().setBank (b); syncControlsFromEngine(); };
        addAndMakeVisible (bb);
    }

    const char* modeNames[] = { "MIX", "TUNE", "DECAY", "MULTI PITCH", "MULTI LEVEL", "MULTI DECAY", "EXIT MULTI", "TUNE/DECAY" };
    for (int m = 0; m < 8; ++m)
    {
        auto* mb = modeButtons.add (new juce::TextButton (modeNames[m]));
        if (m != 6) mb->getProperties().set ("led", true);
        mb->onClick = [this, m] {
            auto& e = engine();
            switch (m)
            {
                case 0: e.exitMultiMode(); e.setSliderMode (SliderMode::Mix); break;
                case 1: e.exitMultiMode(); e.setSliderMode (SliderMode::Tune); break;
                case 2: e.exitMultiMode(); e.setSliderMode (SliderMode::Decay); break;
                case 3: e.enterMultiMode (SliderMode::MultiPitch); break;
                case 4: e.enterMultiMode (SliderMode::MultiLevel); break;
                case 5: e.enterMultiMode (SliderMode::MultiDecay); break;
                case 7: e.exitMultiMode(); e.setSliderMode (SliderMode::TuneDecay); break;
                default: e.exitMultiMode(); break;
            }
            syncControlsFromEngine();
        };
        addAndMakeVisible (mb);
    }

    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> { &runStop, &record, &songSeg, &tapTempo, &tapRepeat, &eraseBtn, &setupBtn, &specialBtn,
                     &diskBtn, &segmentBtn, &songBtn, &sampleBtn, &defineMix, &themeBtn, &syncBtn, &segLenBtn, &stepBtn, &copyBtn })
        addAndMakeVisible (b);
    const char* keyNames[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "NO", "0", "YES", "<", "ENTER", ">" };
    const int keyCodes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, FrontPanel::No, 0, FrontPanel::Yes, FrontPanel::Left, FrontPanel::Enter, FrontPanel::Right };
    for (int k = 0; k < 15; ++k)
    {
        auto* kb = keypad.add (new juce::TextButton (keyNames[k]));
        const int code = keyCodes[k];
        kb->onClick = [this, code] { panel.key (code); syncControlsFromEngine(); };
        addAndMakeVisible (kb);
    }
    setWantsKeyboardFocus (true);
    for (auto* b : { &runStop, &record, &songSeg, &eraseBtn }) b->getProperties().set ("led", true);
    themeBtn.onClick = [this] { showThemeMenu(); };

    runStop.onClick = [this] { engine().runStop(); };
    record.onClick = [this] { engine().setRecord (! engine().isRecording()); };
    songSeg.onClick = [this] { engine().setSongMode (! engine().isSongMode()); syncControlsFromEngine(); };
    tapTempo.onClick = [this] { engine().tapTempo(); syncControlsFromEngine(); };
    tapRepeat.onStateChange = [this] { engine().setTapRepeat (tapRepeat.isDown()); };
    eraseBtn.onStateChange = [this] { engine().setEraseHeld (eraseBtn.isDown()); };
    // Left-click: hardware keypad flow. Right-click: quick menu.
    setupBtn.onClick = [this] { if (setupBtn.popupClick) showSetupMenu(); else panel.start (FrontPanel::Module::Setup); };
    specialBtn.onClick = [this] { if (specialBtn.popupClick) showSpecialMenu(); else panel.start (FrontPanel::Module::Special); };
    diskBtn.onClick = [this] { if (diskBtn.popupClick) showDiskMenu(); else panel.start (FrontPanel::Module::Disk); };
    sampleBtn.onClick = [this] { if (sampleBtn.popupClick) showSampleMenu(); else panel.start (FrontPanel::Module::Sample); };
    syncBtn.onClick = [this] { panel.start (FrontPanel::Module::Sync); };
    segLenBtn.onClick = [this] { if (segLenBtn.popupClick) showSegmentMenu(); else panel.start (FrontPanel::Module::SegLength); };
    stepBtn.onClick = [this] { panel.start (FrontPanel::Module::Step); };
    copyBtn.onClick = [this] { panel.start (FrontPanel::Module::Copy); };
    segmentBtn.onClick = [this] { if (segmentBtn.popupClick) showSegmentMenu(); else openWindow (gridWindow, new StepGridEditor (engine()), "Step edit"); };
    songBtn.onClick = [this] { if (songBtn.popupClick) showSongMenu(); else openWindow (songWindow, new SongEditor (engine()), "Song editor"); };
    defineMix.onClick = [this] { engine().defineMix (mixSel.getSelectedItemIndex()); };

    tempo.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tempo.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
    tempo.setRange (kMinTempo, kMaxTempo, 0.1);
    tempo.setMouseDragSensitivity (600);
    volume.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.onValueChange = [this] {
        if (updating) return;
        { Lock l (engine().mutex()); engine().project().globals.masterLevel = (float) volume.getValue(); }
        proc.pushParam (Crate12Processor::kParamMaster, (float) volume.getValue());
    };
    addAndMakeVisible (volume);
    tempo.onValueChange = [this] { if (! updating) { engine().setTempo (tempo.getValue()); proc.pushParam (Crate12Processor::kParamTempo, (float) tempo.getValue()); } };
    number.setSliderStyle (juce::Slider::IncDecButtons);
    number.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    number.setRange (0, 99, 1);
    number.onValueChange = [this] {
        if (updating) return;
        if (engine().isSongMode()) engine().setSong ((int) number.getValue());
        else engine().setSegment ((int) number.getValue());
    };
    addAndMakeVisible (tempo); addAndMakeVisible (number);

    for (int i = 0; i < 6; ++i) swing.addItem (juce::String (kSwingValues[(size_t) i]) + "%", i + 1);
    swing.onChange = [this] { if (! updating) { engine().setSwing (swing.getSelectedItemIndex()); proc.pushParam (Crate12Processor::kParamSwing, (float) swing.getSelectedItemIndex()); } };
    for (int i = 0; i <= 8; ++i) autoCorrect.addItem (autoCorrectName ((AutoCorrect) i), i + 1);
    autoCorrect.onChange = [this] { if (! updating) engine().setAutoCorrect ((AutoCorrect) autoCorrect.getSelectedItemIndex()); };
    const char* metNames[] = { "Off", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "Click 24ppq" };
    for (int i = 0; i < 10; ++i) metronome.addItem (metNames[i], i + 1);
    metronome.onChange = [this] { if (! updating) engine().setMetronome ((Metronome) metronome.getSelectedItemIndex()); };
    syncMode.addItem ("Internal", 1); syncMode.addItem ("MIDI", 2); syncMode.addItem ("SMPTE (audio in)", 3);
    syncMode.addItem ("Click (audio in)", 4); syncMode.addItem ("Host", 5);
    syncMode.onChange = [this] { if (! updating) engine().setSyncMode ((SyncMode) syncMode.getSelectedId()); };
    for (int i = 0; i < kNumMixes; ++i) mixSel.addItem ("Mix " + juce::String (i + 1), i + 1);
    mixSel.onChange = [this] { if (! updating) engine().selectMix (mixSel.getSelectedItemIndex()); };
    for (auto* c : { &swing, &autoCorrect, &metronome, &syncMode, &mixSel }) addAndMakeVisible (c);
    for (auto* l : { &tempoLabel, &volumeLabel, &numberLabel, &swingL, &acL, &metL, &syncL, &mixL })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }
    tempoLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setJustificationType (juce::Justification::centred);
    for (auto* l : sliderLabels) l->setFont (juce::FontOptions (12.0f, juce::Font::bold));

    setResizable (true, true);
    setResizeLimits (1240, 660, 2400, 1400);
    setSize (1320, 700);
    syncControlsFromEngine();
    startTimerHz (30);

    // Developer aid for screenshots: CRATE12_OPEN=grid|song, CRATE12_KEYS=<module letter><digits> (S=set-up, P=special)
    if (auto* open = std::getenv ("CRATE12_OPEN"))
    {
        const juce::String w (open);
        juce::Timer::callAfterDelay (600, [sp = juce::Component::SafePointer<Crate12Editor> (this), w] {
            if (sp == nullptr) return;
            if (w == "grid") sp->openWindow (sp->gridWindow, new StepGridEditor (sp->engine()), "Step edit");
            if (w == "song") sp->openWindow (sp->songWindow, new SongEditor (sp->engine()), "Song editor");
        });
    }
    if (auto* keys = std::getenv ("CRATE12_KEYS"))
    {
        const juce::String k (keys);
        juce::Timer::callAfterDelay (600, [sp = juce::Component::SafePointer<Crate12Editor> (this), k] {
            if (sp == nullptr || k.isEmpty()) return;
            sp->panel.start (k[0] == 'P' ? FrontPanel::Module::Special : FrontPanel::Module::Setup);
            for (int i = 1; i < k.length(); ++i) sp->panel.key (k[i] - '0');
        });
    }
}

Crate12Editor::~Crate12Editor()
{
    stopTimer();
    if (themeWindow != nullptr) delete themeWindow.getComponent();
    if (gridWindow != nullptr) delete gridWindow.getComponent();
    if (songWindow != nullptr) delete songWindow.getComponent();
    setLookAndFeel (nullptr);
}

void Crate12Editor::applyTheme (const Theme& t, bool remember)
{
    themes.setCurrent (t, remember);
    lnf.setTheme (t);
    sendLookAndFeelChange();
    repaint();
}

void Crate12Editor::paint (juce::Graphics& g)
{
    const auto& th = lnf.theme();
    lnf.drawPanel (g, getLocalBounds());

    // silkscreened section frames
    auto frame = [&] (juce::Rectangle<int> r, const juce::String& title) {
        if (r.isEmpty()) return;
        auto f = r.toFloat().expanded (8, 6);
        g.setColour (th.screen.withAlpha (0.75f));
        g.drawRoundedRectangle (f, 6.0f, 1.2f);
        if (title.isNotEmpty())
        {
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 12;
            auto tr = juce::Rectangle<float> (f.getX() + 14, f.getY() - 7, (float) tw, 14);
            g.setColour (th.panel);
            g.fillRect (tr);
            g.setColour (th.panelText.withAlpha (0.85f));
            g.drawText (title, tr, juce::Justification::centred);
        }
    };
    frame (transportFrame, "PROGRAMMING");
    frame (performFrame, "PERFORMANCE");
    frame (sliderFrame, "LEVEL / TUNE / DECAY");
    frame (padFrame, "");
    frame (keypadFrame, "KEYPAD");

    // screws
    const float m = 10.0f;
    lnf.drawScrew (g, { m, m }, 5.0f, 0.6f);
    lnf.drawScrew (g, { (float) getWidth() - m, m }, 5.0f, 2.1f);
    lnf.drawScrew (g, { m, (float) getHeight() - m }, 5.0f, 1.2f);
    lnf.drawScrew (g, { (float) getWidth() - m, (float) getHeight() - m }, 5.0f, 0.2f);

    // badge
    auto badge = juce::Rectangle<float> (24.0f, (float) getHeight() - 40.0f, 118.0f, 26.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (badge.translated (0, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (th.accent.brighter (0.3f), 0, badge.getY(), th.accent.darker (0.3f), 0, badge.getBottom(), false));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (th.accent.contrasting (1.0f));
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("CRATE12", badge, juce::Justification::centred);
    g.setColour (th.panelText.withAlpha (0.65f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("12-BIT SAMPLING PERCUSSION   26.04 kHz", 156, getHeight() - 40, 400, 26, juce::Justification::centredLeft);
}

void Crate12Editor::resized()
{
    auto r = getLocalBounds().reduced (26, 24);
    r.removeFromBottom (34);

    // top: LCD, programming buttons, knobs
    auto top = r.removeFromTop (118);
    auto knobs = top.removeFromRight (190);
    {
        auto k1 = knobs.removeFromLeft (95), k2 = knobs;
        tempoLabel.setBounds (k1.removeFromTop (16));
        tempo.setBounds (k1);
        volumeLabel.setBounds (k2.removeFromTop (16));
        volume.setBounds (k2.withTrimmedBottom (20));
    }
    lcd.setBounds (top.removeFromLeft (340).withTrimmedBottom (4));
    top.removeFromLeft (24);
    transportFrame = top;
    auto row1 = top.removeFromTop (36);
    const int bw = juce::jmax (90, top.getWidth() / 6);
    for (auto* b : { &runStop, &record, &songSeg, &tapTempo, &tapRepeat, &eraseBtn }) b->setBounds (row1.removeFromLeft (bw).reduced (2));
    top.removeFromTop (4);
    auto row2 = top.removeFromTop (36);
    for (juce::TextButton* b : std::initializer_list<juce::TextButton*> { &setupBtn, &specialBtn, &diskBtn, &segmentBtn, &songBtn, &sampleBtn }) b->setBounds (row2.removeFromLeft (bw).reduced (2));
    top.removeFromTop (6);
    auto row3 = top.removeFromTop (30);
    numberLabel.setBounds (row3.removeFromLeft (56));
    number.setBounds (row3.removeFromLeft (130).reduced (0, 2));
    row3.removeFromLeft (12);
    themeBtn.setBounds (row3.removeFromRight (bw).reduced (2, 0));

    r.removeFromTop (22);
    {
        auto col = r.removeFromRight (196);
        r.removeFromRight (26);
        keypadFrame = col;
        auto mods = col.removeFromTop (76);
        auto m1 = mods.removeFromTop (38), m2 = mods;
        syncBtn.setBounds (m1.removeFromLeft (m1.getWidth() / 2).reduced (2));
        copyBtn.setBounds (m1.reduced (2));
        segLenBtn.setBounds (m2.removeFromLeft (m2.getWidth() / 2).reduced (2));
        stepBtn.setBounds (m2.reduced (2));
        col.removeFromTop (10);
        const int kh = juce::jmin (52, col.getHeight() / 5);
        for (int row = 0; row < 5; ++row)
        {
            auto line = col.removeFromTop (kh);
            const int kw = line.getWidth() / 3;
            for (int c = 0; c < 3; ++c) keypad[row * 3 + c]->setBounds (line.removeFromLeft (kw).reduced (3));
        }
    }
    auto perf = r.removeFromTop (88);
    performFrame = perf;
    auto combos = perf.removeFromTop (46);
    auto place = [&combos] (juce::Label& l, juce::Component& c, int w) {
        auto a = combos.removeFromLeft (w).reduced (3, 0);
        l.setBounds (a.removeFromTop (16));
        c.setBounds (a.removeFromTop (26));
    };
    place (swingL, swing, 100); place (acL, autoCorrect, 120); place (metL, metronome, 140);
    place (syncL, syncMode, 140); place (mixL, mixSel, 110);
    defineMix.setBounds (combos.removeFromLeft (130).reduced (3, 0).withTrimmedTop (14).withHeight (32));
    perf.removeFromTop (6);
    auto modes = perf.removeFromTop (34);
    const int mw = modes.getWidth() / 8;
    for (auto* m : modeButtons) m->setBounds (modes.removeFromLeft (mw).reduced (2, 0));

    r.removeFromTop (22);
    auto banks = r.removeFromBottom (32);
    for (auto* b : bankButtons) b->setBounds (banks.removeFromLeft (120).reduced (2, 0));
    r.removeFromBottom (14);

    auto padRow = r.removeFromBottom (96);
    padFrame = padRow;
    r.removeFromBottom (16);
    sliderFrame = r;
    auto sliderArea = r;
    const int w = padRow.getWidth() / kPadsPerBank;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        pads[i]->setBounds (padRow.removeFromLeft (w).reduced (6, 2));
        auto s = sliderArea.removeFromLeft (w);
        sliderLabels[i]->setBounds (s.removeFromBottom (18));
        sliders[i]->setBounds (s.withSizeKeepingCentre (juce::jmin (w, 90), s.getHeight()).reduced (0, 6));
    }
}

void Crate12Editor::syncControlsFromEngine()
{
    updating = true;
    auto& e = engine();
    {
        Lock l (e.mutex());
        const auto& g = e.project().globals;
        tempo.setValue (g.tempo, juce::dontSendNotification);
        swing.setSelectedItemIndex (g.swingIndex, juce::dontSendNotification);
        autoCorrect.setSelectedItemIndex ((int) g.autoCorrect, juce::dontSendNotification);
        metronome.setSelectedItemIndex ((int) g.metronome, juce::dontSendNotification);
        syncMode.setSelectedId ((int) g.syncMode, juce::dontSendNotification);
        mixSel.setSelectedItemIndex (g.currentMix, juce::dontSendNotification);
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            const int s = soundIndex (e.getBank(), i);
            const auto& snd = e.project().sounds[(size_t) s];
            pads[i]->hasSound = ! snd.empty();
            pads[i]->label = juce::String::charToString (bankLetter (e.getBank())) + juce::String (i + 1)
                             + "\n" + (snd.empty() ? juce::String ("--") : juce::String (snd.name));
            pads[i]->repaint();
        }
    }
    for (int i = 0; i < kPadsPerBank; ++i) sliders[i]->setValue (e.getSlider (i), juce::dontSendNotification);
    numberLabel.setText (e.isSongMode() ? "SONG #" : "SEG #", juce::dontSendNotification);
    number.setValue (e.isSongMode() ? e.getSong() : e.getSegment(), juce::dontSendNotification);
    songSeg.setButtonText (e.isSongMode() ? "SONG" : "SEGMENT");
    for (int b = 0; b < kNumBanks; ++b)
        bankButtons[b]->setToggleState (b == e.getBank(), juce::dontSendNotification);
    const int modeIdx = e.inMultiMode() ? -1 : (int) e.getSliderMode();
    for (int m = 0; m < 3; ++m) modeButtons[m]->setToggleState (m == modeIdx, juce::dontSendNotification);
    modeButtons[7]->setToggleState (modeIdx == (int) SliderMode::TuneDecay, juce::dontSendNotification);
    {
        float v8[8];
        if (panel.sliderPositions (v8)) for (int i = 0; i < kPadsPerBank; ++i) sliders[i]->setValue (v8[i], juce::dontSendNotification);
    }
    songSeg.setToggleState (e.isSongMode(), juce::dontSendNotification);
    { Lock l (e.mutex()); volume.setValue (e.project().globals.masterLevel, juce::dontSendNotification); }
    updating = false;
}

void Crate12Editor::timerCallback()
{
    auto& e = engine();
    juce::String a, b;
    { std::string l1, l2; e.getLcd (l1, l2); a = l1; b = l2; }
    if (e.getSampleState() != Engine::SampleState::Idle)
    {
        const int bars = (int) (e.getInputLevel() * 16.0f);
        b = juce::String::repeatedString ("#", bars).paddedRight ('.', 16);
    }
    else if (e.isPlaying())
    {
        int bar = 0, beat = 0; e.currentBeatInfo (bar, beat);
        b = (e.isRecording() ? "REC " : "PLAY") + juce::String::formatted (" %02d:%02d %5.1f", bar, beat, e.getTempo());
    }
    if (a != lcd.line1 || b != lcd.line2) { lcd.line1 = a; lcd.line2 = b; lcd.repaint(); }

    const auto mask = e.padFlashMask();
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        const int s = soundIndex (e.getBank(), i);
        auto* p = pads[i];
        if ((mask >> s) & 1u) p->flash = 0.8f;
        else p->flash *= 0.8f;
        if (p->flash > 0.005f) p->repaint();
    }
    if (lampTestUntil != 0)
    {
        const bool on = juce::Time::getMillisecondCounter() < lampTestUntil;
        for (auto* b : { &runStop, &record, &songSeg, &eraseBtn }) b->setToggleState (on, juce::dontSendNotification);
        for (auto* b : bankButtons) b->setToggleState (on, juce::dontSendNotification);
        for (auto* b : modeButtons) b->setToggleState (on, juce::dontSendNotification);
        for (auto* p : pads) { p->flash = on ? 0.8f : 0.0f; p->repaint(); }
        if (! on) { lampTestUntil = 0; syncControlsFromEngine(); }
        return;
    }
    runStop.setToggleState (e.isPlaying(), juce::dontSendNotification);
    record.setToggleState (e.isRecording(), juce::dontSendNotification);
    tapRepeat.setToggleState (tapRepeat.isDown(), juce::dontSendNotification);
    eraseBtn.setToggleState (eraseBtn.isDown(), juce::dontSendNotification);

    static int slow = 0;
    if (++slow % 15 == 0 && ! isMouseButtonDownAnywhere()) syncControlsFromEngine();
}

// ===================================================================== pads
void Crate12Editor::padDown (int pad, float vel)
{
    if (panel.pad (pad, eraseBtn.isDown())) { syncControlsFromEngine(); return; }   // keypad function wanted a pad
    engine().padPress (pad, juce::jlimit (1, 127, (int) std::lround (vel * 127.0f)));
}

bool Crate12Editor::keyPressed (const juce::KeyPress& k)
{
    const auto c = k.getTextCharacter();
    if (c >= '0' && c <= '9') { panel.key (c - '0'); return true; }
    if (k == juce::KeyPress::returnKey) { panel.key (FrontPanel::Enter); return true; }
    if (k == juce::KeyPress::leftKey) { panel.key (FrontPanel::Left); return true; }
    if (k == juce::KeyPress::rightKey) { panel.key (FrontPanel::Right); return true; }
    if (c == 'y' || c == 'Y') { panel.key (FrontPanel::Yes); return true; }
    if (c == 'n' || c == 'N' || k == juce::KeyPress::escapeKey) { panel.key (FrontPanel::No); return true; }
    if (k == juce::KeyPress::spaceKey) { engine().runStop(); return true; }
    return false;
}

void Crate12Editor::openWindow (juce::Component::SafePointer<juce::DialogWindow>& slot, juce::Component* content, const juce::String& title)
{
    if (slot != nullptr) { delete content; slot->toFront (true); return; }
    content->setLookAndFeel (&lnf);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = title;
    o.componentToCentreAround = this;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.escapeKeyTriggersCloseButton = true;
    slot = o.launchAsync();
    if (slot != nullptr) slot->setLookAndFeel (&lnf);
}

void Crate12Editor::padUp (int pad) { engine().padRelease (pad); }

void Crate12Editor::padFilesDropped (int pad, const juce::StringArray& files)
{
    // multiple files fill consecutive pads
    int slot = soundIndex (engine().getBank(), pad);
    for (auto& f : files)
    {
        if (slot >= kNumSounds) break;
        if (proc.loadSampleFile (juce::File (f), slot)) ++slot;
    }
    syncControlsFromEngine();
}

void Crate12Editor::padMenu (int pad)
{
    const int s = soundIndex (engine().getBank(), pad);
    engine().selectSound (s);
    juce::PopupMenu m, chMenu;
    int curCh = 0; bool loopOn = false, rev = false;
    {
        Lock l (engine().mutex());
        curCh = engine().project().working.channel[(size_t) s];
        loopOn = engine().project().sounds[(size_t) s].loop;
        rev = engine().project().sounds[(size_t) s].reversed;
    }
    for (int c = 0; c < kNumChannels; ++c)
        chMenu.addItem (100 + c, "Channel " + juce::String (c + 1) + (c < 2 ? " (dynamic filter)" : c < 6 ? " (fixed filter)" : " (unfiltered)"), true, c == curCh);
    m.addSectionHeader (juce::String ("Sound ") + bankLetter (s / 8) + juce::String (s % 8 + 1));
    m.addItem (1, "Load audio file...");
    m.addItem (2, "Sample from input (armed)");
    m.addItem (3, "Sample from input (force)");
    m.addSubMenu ("Channel assign", chMenu);
    m.addItem (4, "Loop", true, loopOn);
    m.addItem (5, "Set loop length (samples)...");
    m.addItem (6, "Truncate (start/end samples)...");
    m.addItem (7, "Make truncation permanent");
    m.addItem (8, "Reverse", true, rev);
    m.addItem (9, "Rename...");
    m.addItem (10, "Copy to sound #...");
    m.addItem (11, "Swap with sound #...");
    m.addItem (12, "MIDI note...");
    m.addItem (13, "Export as WAV...");
    m.addSeparator();
    m.addItem (14, "Delete sound");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, s] (int r) {
        auto& e = engine();
        if (r >= 100 && r < 108) { e.channelAssign (s, r - 100); return; }
        switch (r)
        {
            case 1: chooseFile (false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", [this, s] (juce::File f) { proc.loadSampleFile (f, s); syncControlsFromEngine(); }); break;
            case 2: e.armSampling (s); break;
            case 3: e.forceSampling (s); break;
            case 4: { int len = 0; bool on = false; { Lock l (e.mutex()); auto& snd = e.project().sounds[(size_t) s]; on = snd.loop; len = snd.loopLength > 0 ? snd.loopLength : 2604; } e.setLoop (s, ! on, len); break; }
            case 5: askNumber ("Loop length (samples, 2604 = 0.1 s)", 1, kAuthenticMemory, 2604, [this, s] (int v) { engine().setLoop (s, true, v); }); break;
            case 6: askNumber ("Truncate start (samples)", 0, kAuthenticMemory * 10, 0, [this, s] (int a) {
                        askNumber ("Truncate end (samples, 0 = full)", 0, kAuthenticMemory * 10, 0, [this, s, a] (int b) { engine().setTruncation (s, a, b); }); });
                    break;
            case 7: e.makeTruncationPermanent (s); break;
            case 8: e.reverseSound (s); break;
            case 9:
            {
                auto* w = new juce::AlertWindow ("Name sound", "Up to 8 characters", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", "", "Name");
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, s, w] (int res) {
                    if (res == 1) engine().nameSound (s, w->getTextEditorContents ("name").toUpperCase().toStdString());
                    syncControlsFromEngine();
                }), true);
                break;
            }
            case 10: askNumber ("Copy to sound # (1-32)", 1, 32, s + 1, [this, s] (int v) { engine().copySound (s, v - 1); syncControlsFromEngine(); }); break;
            case 11: askNumber ("Swap with sound # (1-32)", 1, 32, s + 1, [this, s] (int v) { engine().swapSounds (s, v - 1); syncControlsFromEngine(); }); break;
            case 12:
            {
                int cur = 36; { Lock l (e.mutex()); cur = e.project().globals.noteMap[(size_t) s]; }
                askNumber ("MIDI note (0-127)", 0, 127, cur, [this, s] (int v) { Lock l (engine().mutex()); engine().project().globals.noteMap[(size_t) s] = (uint8_t) v; });
                break;
            }
            case 13: chooseFile (true, "*.wav", [this, s] (juce::File f) { proc.exportSoundToWav (s, f.withFileExtension ("wav")); }); break;
            case 14: e.deleteSound (s); syncControlsFromEngine(); break;
            default: break;
        }
    });
}

// ===================================================================== menus
void Crate12Editor::askNumber (const juce::String& title, int minV, int maxV, int initial, std::function<void (int)> done)
{
    auto* w = new juce::AlertWindow (title, juce::String (minV) + " - " + juce::String (maxV), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("v", juce::String (initial));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, minV, maxV, done] (int res) {
        if (res == 1) done (juce::jlimit (minV, maxV, w->getTextEditorContents ("v").getIntValue()));
    }), true);
}

void Crate12Editor::chooseFile (bool save, const juce::String& patterns, std::function<void (juce::File)> done)
{
    chooser = std::make_unique<juce::FileChooser> (save ? "Save" : "Open", juce::File(), patterns);
    const int flags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [done] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f != juce::File()) done (f);
    });
}

void Crate12Editor::showSetupMenu()
{
    auto& e = engine();
    Globals g; int firstStep = 0;
    { Lock l (e.mutex()); g = e.project().globals; firstStep = e.project().firstSongStep; }
    juce::PopupMenu m, midiCh, midiMode, smpte, dynAlloc, analog;
    for (int c = 0; c < 16; ++c) midiCh.addItem (200 + c, "Channel " + juce::String (c + 1), true, g.midiChannel == c);
    midiMode.addItem (220, "1 Omni", true, g.midiMode == MidiMode::Omni);
    midiMode.addItem (221, "2 Poly", true, g.midiMode == MidiMode::Poly);
    const char* rates[] = { "24", "25", "30", "30 drop" };
    for (int i = 0; i < 4; ++i) smpte.addItem (230 + i, rates[i], true, (int) g.smpteRate == i);
    for (int i = 0; i < 4; ++i) dynAlloc.addItem (240 + i, "Channels " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2), true, g.dynamicAllocation[(size_t) i]);
    analog.addItem (250, "Ch 1-2 filter cutoff...");
    analog.addItem (251, "Ch 1-2 filter resonance...");
    analog.addItem (252, "Ch 3-4 filter Hz...");
    analog.addItem (253, "Ch 5-6 filter Hz...");
    analog.addItem (254, "Output drive...");
    analog.addItem (255, "Filter envelopes (ch 1-2)", true, g.filterEnvelopes);
    for (int c = 0; c < kNumChannels; ++c) analog.addItem (260 + c, "Pan ch " + juce::String (c + 1) + "... (extension)");

    m.addSectionHeader ("SET-UP");
    m.addItem (11, "11 Multi-Pitch");
    m.addItem (12, "12 Multi-Level");
    m.addItem (13, "13 Exit Multi Mode");
    m.addItem (14, "14 Dynamic Buttons", true, g.dynamicButtons);
    m.addItem (15, "15 Define Mix...");
    m.addItem (16, "16 Select Mix...");
    m.addItem (17, "17 Channel Assign (right-click a pad)", false);
    m.addItem (18, "18 Tune/Decay select for the selected pad...");
    m.addItem (19, "19 Truncate/Loop (right-click a pad)", false);
    m.addItem (20, "20 Delete Sound (selected)");
    m.addItem (21, "21 First Song Step... (" + juce::String (firstStep + 1) + ")");
    m.addSubMenu ("22 MIDI channel", midiCh);
    m.addSubMenu ("22 MIDI mode", midiMode);
    m.addItem (222, "22 Pitch keys (G#3-B4)", true, g.pitchKeys);
    m.addItem (223, "MIDI out: notes", true, g.midiOutNotes);
    m.addItem (224, "MIDI out: clock/start/stop/SPP", true, g.midiOutClock);
    m.addSubMenu ("SMPTE frame rate", smpte);
    m.addSubMenu ("Dynamic allocation", dynAlloc);
    m.addSubMenu ("Analog stage", analog);
    m.addItem (225, "Authentic limits (10 s / 2.5 s / 5000 notes)", true, g.authenticLimits);
    m.addItem (226, "Metronome only while recording", true, g.metronomeInRecordOnly);
    m.addItem (227, "Trigger out MIDI note... (" + juce::String (g.triggerNote) + ")");
    m.addItem (228, "SMPTE start time... (" + juce::String (Engine::framesToTimecode (g.smpteStartFrames, g.smpteRate)) + ")");
    m.addItem (229, "Click/SMPTE input threshold...");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, g] (int r) {
        auto& e = engine();
        if (r >= 200 && r < 216) { e.setMidiChannel (r - 200); return; }
        if (r == 220 || r == 221) { e.setMidiMode (r == 220 ? MidiMode::Omni : MidiMode::Poly); return; }
        if (r >= 230 && r < 234) { e.setSmpteRate ((SmpteRate) (r - 230)); return; }
        if (r >= 240 && r < 244) { e.setDynamicAllocation (r - 240, ! g.dynamicAllocation[(size_t) (r - 240)]); return; }
        if (r >= 260 && r < 268)
        {
            const int c = r - 260;
            askNumber ("Pan (-100 left .. 100 right)", -100, 100, (int) (g.pan[(size_t) c] * 100), [this, c] (int v) { Lock l (engine().mutex()); engine().project().globals.pan[(size_t) c] = (float) v / 100.0f; });
            return;
        }
        auto setG = [this] (std::function<void (Globals&)> f) { Lock l (engine().mutex()); f (engine().project().globals); };
        switch (r)
        {
            case 11: e.enterMultiMode (SliderMode::MultiPitch); break;
            case 12: e.enterMultiMode (SliderMode::MultiLevel); break;
            case 13: e.exitMultiMode(); break;
            case 14: e.setDynamicButtons (! g.dynamicButtons); break;
            case 15: askNumber ("Define Mix #", 1, 8, g.currentMix + 1, [this] (int v) { engine().defineMix (v - 1); }); break;
            case 16: askNumber ("Select Mix #", 1, 8, g.currentMix + 1, [this] (int v) { engine().selectMix (v - 1); }); break;
            case 18: askNumber ("Selected pad slider: 1 = Tune, 2 = Decay", 1, 2, 1, [this] (int v) { engine().setDecaySelect (engine().getSelectedSound(), v == 2); syncControlsFromEngine(); }); break;
            case 227: askNumber ("Trigger out MIDI note", 0, 127, g.triggerNote, [setG] (int v) { setG ([v] (Globals& x) { x.triggerNote = (uint8_t) v; }); }); break;
            case 228: askNumber ("SMPTE start (HHMMSSFF)", 0, 23595929, 1000000, [this, g] (int v) {
                          engine().setSmpteStart (Engine::timecodeToFrames (v / 1000000, (v / 10000) % 100, (v / 100) % 100, v % 100, g.smpteRate)); });
                      break;
            case 229: askNumber ("Sync input threshold (1-100 %)", 1, 100, (int) (g.syncInputThreshold * 100), [setG] (int v) { setG ([v] (Globals& x) { x.syncInputThreshold = (float) v / 100.0f; }); }); break;
            case 20: e.deleteSound (e.getSelectedSound()); break;
            case 21: askNumber ("First song step", 1, 999, 1, [this] (int v) { engine().setFirstSongStep (v - 1); }); break;
            case 222: e.setPitchKeys (! g.pitchKeys); break;
            case 223: setG ([] (Globals& x) { x.midiOutNotes = ! x.midiOutNotes; }); break;
            case 224: setG ([] (Globals& x) { x.midiOutClock = ! x.midiOutClock; }); break;
            case 225: setG ([] (Globals& x) { x.authenticLimits = ! x.authenticLimits; }); break;
            case 226: setG ([] (Globals& x) { x.metronomeInRecordOnly = ! x.metronomeInRecordOnly; }); break;
            case 250: askNumber ("Ch 1-2 cutoff (0-100)", 0, 100, (int) (g.filter12Cutoff * 100), [setG] (int v) { setG ([v] (Globals& x) { x.filter12Cutoff = (float) v / 100.0f; }); }); break;
            case 251: askNumber ("Ch 1-2 resonance (0-100)", 0, 100, (int) (g.filter12Reso * 100), [setG] (int v) { setG ([v] (Globals& x) { x.filter12Reso = (float) v / 100.0f; }); }); break;
            case 252: askNumber ("Ch 3-4 cutoff Hz", 1000, 20000, (int) g.filter34Hz, [setG] (int v) { setG ([v] (Globals& x) { x.filter34Hz = (float) v; }); }); break;
            case 253: askNumber ("Ch 5-6 cutoff Hz", 1000, 20000, (int) g.filter56Hz, [setG] (int v) { setG ([v] (Globals& x) { x.filter56Hz = (float) v; }); }); break;
            case 254: askNumber ("Output drive (10-500 %)", 10, 500, (int) (g.outputDrive * 100), [setG] (int v) { setG ([v] (Globals& x) { x.outputDrive = (float) v / 100.0f; }); }); break;
            case 255: e.setFilterEnvelopes (! g.filterEnvelopes); break;
            default: break;
        }
        syncControlsFromEngine();
    });
}

void Crate12Editor::showSpecialMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SPECIAL");
    m.addItem (12, "12 Clear All Memory");
    m.addItem (13, "13 Check Remaining Memory");
    m.addItem (14, juce::String ("14 Write SMPTE to main out") + (engine().isSmpteWriting() ? " (on)" : ""), true, engine().isSmpteWriting());
    m.addItem (15, "15 Clear All Sounds");
    m.addItem (16, "16 Clear All Sequences");
    m.addItem (17, "17 Copy Sound (selected)...");
    m.addItem (18, "18 Swap Sound (selected)...");
    m.addItem (19, "19 Default Decay (all sounds)...");
    m.addItem (20, "Reverse Sound (selected)");
    m.addItem (21, "21 Name Sound (selected)...");
    m.addItem (22, "22 Dynamic Allocation: see SET-UP menu", false);
    m.addItem (23, "23 Tx Sound Dump (selected sound, MIDI SDS)");
    m.addItem (24, "24 Rx Sound Dump into selected sound");
    m.addItem (99, "99 Lamp Test");
    m.addSeparator();
    m.addItem (30, "Load Factory Kit (banks A-C)");
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) {
        auto& e = engine();
        const int s = e.getSelectedSound();
        switch (r)
        {
            case 12:
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear All Memory", "Erase all sounds and sequences?", "Clear", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) { engine().clearAllMemory(); syncControlsFromEngine(); } }));
                break;
            case 13:
            {
                const auto mi = e.memoryInfo();
                const double secs = (double) (mi.samplesTotal - mi.samplesUsed) / kNativeRate;
                e.setLcd (juce::String::formatted ("Smpl %.1fs free", secs).toStdString(),
                          juce::String::formatted ("Seq %d%% free", 100 - mi.notesUsed * 100 / std::max (1, mi.notesTotal)).toStdString());
                break;
            }
            case 15: e.clearSounds(); break;
            case 16: e.clearSequences(); break;
            case 17: askNumber ("Copy selected sound to # (1-32)", 1, 32, s + 1, [this, s] (int v) { engine().copySound (s, v - 1); }); break;
            case 18: askNumber ("Swap selected sound with # (1-32)", 1, 32, s + 1, [this, s] (int v) { engine().swapSounds (s, v - 1); }); break;
            case 19: askNumber ("Default decay for all sounds (0-100)", 0, 100, 100, [this] (int v) { engine().setDefaultDecayAll ((float) v / 100.0f); }); break;
            case 20: e.reverseSound (s); break;
            case 14: e.setSmpteWrite (! e.isSmpteWriting()); break;
            case 21:
            {
                auto* w = new juce::AlertWindow ("Name sound", "Up to 8 characters", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", "", "Name");
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, s, w] (int res) {
                    if (res == 1) engine().nameSound (s, w->getTextEditorContents ("name").toUpperCase().toStdString());
                    syncControlsFromEngine();
                }), true);
                break;
            }
            case 23: e.sdsSend (s); break;
            case 24: e.sdsArmReceive (s); break;
            case 99: startLampTest(); break;
            case 30: e.loadFactoryKit(); break;
            default: break;
        }
        syncControlsFromEngine();
    });
}

void Crate12Editor::showDiskMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("DISK");
    m.addItem (1, "0 Load Sequences and Sounds...");
    m.addItem (2, "1 Save Sequences...");
    m.addItem (3, "2 Save Sounds...");
    m.addItem (4, "3 Load Sequences...");
    m.addItem (5, "4 Load Segment #...");
    m.addItem (6, "5 Load Sounds...");
    m.addItem (7, "6 Load Sound #...");
    m.addItem (8, "Save All (sequences + sounds + settings)...");
    m.addSeparator();
    m.addItem (9, "Import folder of audio files (fills pads from the current bank)...");
    m.addItem (10, "7 Catalog Sequences (browse a disk file)...");
    m.addItem (11, "8 Catalog Sounds (browse a disk file)...");
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) { diskAction (r); });
}

void Crate12Editor::diskFunction (int n)
{
    static const int map[] = { 1, 2, 3, 4, 5, 6, 7, 10, 11 };
    if (n >= 0 && n <= 8) diskAction (map[n]);
    else engine().setLcd ("Disk 9 Format", "Not needed here");
}

void Crate12Editor::diskAction (int r)
{
    {
        auto saveTo = [this] (int sections) {
            chooseFile (true, "*.c12", [this, sections] (juce::File f) {
                const auto blob = engine().saveDisk (sections);
                f.withFileExtension ("c12").replaceWithData (blob.data(), blob.size());
            });
        };
        auto loadFrom = [this] (std::function<void (const juce::MemoryBlock&)> fn) {
            chooseFile (false, "*.c12", [fn] (juce::File f) { juce::MemoryBlock mb; if (f.loadFileAsData (mb)) fn (mb); });
        };
        switch (r)
        {
            case 1: loadFrom ([this] (const juce::MemoryBlock& mb) { engine().loadDisk ((const uint8_t*) mb.getData(), mb.getSize(), kDiskSounds | kDiskSequences); syncControlsFromEngine(); }); break;
            case 2: saveTo (kDiskSequences); break;
            case 3: saveTo (kDiskSounds); break;
            case 4: loadFrom ([this] (const juce::MemoryBlock& mb) { engine().loadDisk ((const uint8_t*) mb.getData(), mb.getSize(), kDiskSequences); syncControlsFromEngine(); }); break;
            case 5: loadFrom ([this] (const juce::MemoryBlock& mb) {
                        lastDisk.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
                        askNumber ("Segment # on disk", 0, 99, 0, [this] (int src) {
                            askNumber ("Load into segment #", 0, 99, engine().getSegment(), [this, src] (int dst) {
                                engine().loadSegmentFromDisk (lastDisk.data(), lastDisk.size(), src, dst); }); }); });
                    break;
            case 6: loadFrom ([this] (const juce::MemoryBlock& mb) { engine().loadDisk ((const uint8_t*) mb.getData(), mb.getSize(), kDiskSounds); syncControlsFromEngine(); }); break;
            case 7: loadFrom ([this] (const juce::MemoryBlock& mb) {
                        lastDisk.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
                        askNumber ("Sound # on disk (1-32)", 1, 32, 1, [this] (int src) {
                            askNumber ("Load into sound # (1-32)", 1, 32, engine().getSelectedSound() + 1, [this, src] (int dst) {
                                engine().loadSoundFromDisk (lastDisk.data(), lastDisk.size(), src - 1, dst - 1); syncControlsFromEngine(); }); }); });
                    break;
            case 8: saveTo (kDiskAll); break;
            case 9:
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a folder of samples", juce::File());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
                    const auto dir = fc.getResult();
                    if (! dir.isDirectory()) return;
                    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
                    files.sort();
                    int slot = soundIndex (engine().getBank(), 0), loaded = 0;
                    for (auto& f : files)
                    {
                        if (slot >= kNumSounds) break;
                        if (proc.loadSampleFile (f, slot)) { ++slot; ++loaded; }
                    }
                    engine().setLcd ("Folder Import", (juce::String (loaded) + " sounds").toStdString());
                    syncControlsFromEngine();
                });
                break;
            }
            case 10: case 11:
            {
                const bool sounds = r == 11;
                loadFrom ([this, sounds] (const juce::MemoryBlock& mb) {
                    auto tmp = std::make_shared<Project>();
                    int found = 0;
                    if (! deserializeProject (*tmp, (const uint8_t*) mb.getData(), mb.getSize(), &found)) { engine().setLcd ("Disk Error", "Unreadable file"); return; }
                    juce::PopupMenu cat;
                    if (! sounds)
                    {
                        cat.addSectionHeader ("Load into segment " + juce::String (engine().getSegment()).paddedLeft ('0', 2));
                        for (int i = 0; i < kNumSegments; ++i)
                        {
                            const auto& sg = tmp->segments[(size_t) i];
                            if (sg.events.empty()) continue;
                            cat.addItem (100 + i, "Seg " + juce::String (i).paddedLeft ('0', 2) + "   " + juce::String (sg.bars) + " bars + " + juce::String (sg.extraBeats)
                                                  + " beats, " + juce::String (sg.beatsPerBar) + "/" + juce::String (sg.beatUnit) + ", " + juce::String ((int) sg.events.size()) + " notes");
                        }
                    }
                    else
                    {
                        const int dst = engine().getSelectedSound();
                        cat.addSectionHeader (juce::String ("Load into sound ") + bankLetter (dst / 8) + juce::String (dst % 8 + 1));
                        for (int i = 0; i < kNumSounds; ++i)
                        {
                            const auto& sd = tmp->sounds[(size_t) i];
                            if (sd.empty()) continue;
                            cat.addItem (100 + i, juce::String::charToString (bankLetter (i / 8)) + juce::String (i % 8 + 1) + "   " + juce::String (sd.name).paddedRight (' ', 9)
                                                  + juce::String ((double) sd.data.size() / kNativeRate, 2) + " s");
                        }
                    }
                    if (cat.getNumItems() <= 1) cat.addItem (99, "(nothing on this disk)", false);
                    lastDisk.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
                    cat.showMenuAsync (juce::PopupMenu::Options(), [this, sounds] (int pick) {
                        if (pick < 100) return;
                        if (sounds) engine().loadSoundFromDisk (lastDisk.data(), lastDisk.size(), pick - 100, engine().getSelectedSound());
                        else engine().loadSegmentFromDisk (lastDisk.data(), lastDisk.size(), pick - 100, engine().getSegment());
                        syncControlsFromEngine();
                    });
                });
                break;
            }
            default: break;
        }
    }
}

void Crate12Editor::showSegmentMenu()
{
    auto& e = engine();
    const int seg = e.getSegment();
    Segment s;
    { Lock l (e.mutex()); s = e.project().segments[(size_t) seg]; }
    juce::PopupMenu m;
    m.addSectionHeader ("Segment " + juce::String (seg).paddedLeft ('0', 2) + "  " + juce::String (s.bars) + " bars + "
                        + juce::String (s.extraBeats) + " beats, " + juce::String (s.beatsPerBar) + "/" + juce::String (s.beatUnit)
                        + ", " + juce::String ((int) s.events.size()) + " notes");
    m.addItem (1, "Set length (standard)...");
    m.addItem (2, "Set length (truncate)...");
    m.addItem (3, "Time signature...");
    m.addItem (4, "Erase segment");
    m.addItem (5, "Erase selected sound from segment");
    m.addItem (6, "Copy segment to #...");
    m.addItem (7, "Append (copy onto itself) x times...");
    m.addItem (8, "Step program: insert selected sound at step...");
    m.addItem (9, "Step program: erase step...");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, seg, s] (int r) {
        auto& en = engine();
        switch (r)
        {
            case 1: case 2:
            {
                const bool trunc = r == 2;
                askNumber ("Bars", 0, 99, s.bars, [this, seg, trunc, s] (int bars) {
                    askNumber ("Extra beats", 0, 99, s.extraBeats, [this, seg, trunc, bars] (int beats) { engine().setSegmentLength (seg, bars, beats, trunc); }); });
                break;
            }
            case 3: askNumber ("Beats per bar", 1, 99, s.beatsPerBar, [this, seg, s] (int num) {
                        askNumber ("Beat unit (4, 8, 16, 32)", 4, 32, s.beatUnit, [this, seg, num] (int den) { engine().setTimeSignature (seg, num, den); }); });
                    break;
            case 4: en.eraseSegment (seg); break;
            case 5: en.eraseSoundFromSegment (seg, en.getSelectedSound()); break;
            case 6: askNumber ("Copy to segment #", 0, 99, seg, [this, seg] (int dst) { engine().copySegment (seg, dst); }); break;
            case 7: askNumber ("Append how many times", 1, 99, 1, [this, seg] (int t) { engine().copySegment (seg, seg, t); }); break;
            case 8:
            {
                const int grid = autoCorrectTicks ((AutoCorrect) autoCorrect.getSelectedItemIndex());
                askNumber ("Step number (auto-correct grid, from 1)", 1, 9999, 1, [this, seg, grid] (int step) {
                    engine().stepInsert (seg, (step - 1) * grid, engine().getSelectedSound(), 127); });
                break;
            }
            case 9:
            {
                const int grid = autoCorrectTicks ((AutoCorrect) autoCorrect.getSelectedItemIndex());
                askNumber ("Step number to erase", 1, 9999, 1, [this, seg, grid] (int step) { engine().stepErase (seg, (step - 1) * grid, -1); });
                break;
            }
            default: break;
        }
    });
}

static juce::String describeStep (const SongStep& s)
{
    switch (s.type)
    {
        case SongStep::Type::Segment:     return "Segment " + juce::String (s.a).paddedLeft ('0', 2);
        case SongStep::Type::RepeatBegin: return "|- Repeat x" + juce::String (s.a);
        case SongStep::Type::RepeatEnd:   return "-| End repeat";
        case SongStep::Type::MixChange:   return "Mix change -> " + juce::String (s.a + 1);
        case SongStep::Type::TempoChange: return juce::String (s.a == 2 ? "Ritardando -" : "Accelerando +") + juce::String (s.b) + " BPM / " + juce::String (s.c) + " beats";
        case SongStep::Type::SubSong:     return "Sub-song " + juce::String (s.a).paddedLeft ('0', 2);
        case SongStep::Type::TriggerOut:  return "Trigger out";
        case SongStep::Type::End:         return s.a == 0 ? "END (stop)" : s.a == 1 ? "END (repeat song)" : "END (link to song " + juce::String (s.b) + ")";
    }
    return {};
}

void Crate12Editor::showSongMenu()
{
    auto& e = engine();
    const int song = e.getSong();
    Song s;
    { Lock l (e.mutex()); s = e.project().songs[(size_t) song]; }
    juce::PopupMenu m, steps;
    for (int i = 0; i < (int) s.steps.size(); ++i)
        steps.addItem (1000 + i, juce::String (i + 1).paddedLeft ('0', 2) + "  " + describeStep (s.steps[(size_t) i]) + "   (click to delete)");
    if (s.steps.empty()) steps.addItem (999, "(empty)", false);
    m.addSectionHeader ("Song " + juce::String (song).paddedLeft ('0', 2) + "  (" + juce::String ((int) s.steps.size()) + " steps)");
    m.addSubMenu ("Steps", steps);
    m.addItem (1, "Append segment...");
    m.addItem (2, "Append repeat begin...");
    m.addItem (3, "Append repeat end");
    m.addItem (4, "Append mix change...");
    m.addItem (5, "Append accelerando...");
    m.addItem (6, "Append ritardando...");
    m.addItem (7, "Append sub-song...");
    m.addItem (8, "Append trigger out");
    m.addItem (9, "Append END: stop");
    m.addItem (10, "Append END: repeat song");
    m.addItem (11, "Append END: link to song...");
    m.addItem (12, "Delete last step");
    m.addItem (13, "Clear song");
    m.addItem (14, "Song tempo... (" + (s.tempo > 0 ? juce::String (s.tempo, 1) : juce::String ("none")) + ")");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, song, n = (int) s.steps.size()] (int r) {
        auto& en = engine();
        auto add = [this, song, n] (SongStep st) { engine().songInsertStep (song, n, st); };
        using T = SongStep::Type;
        if (r >= 1000) { en.songDeleteStep (song, r - 1000); return; }
        switch (r)
        {
            case 1: askNumber ("Segment #", 0, 99, en.getSegment(), [add] (int v) { add ({ T::Segment, v, 0, 0 }); }); break;
            case 2: askNumber ("Repeat count", 2, 99, 2, [add] (int v) { add ({ T::RepeatBegin, v, 0, 0 }); }); break;
            case 3: add ({ T::RepeatEnd, 0, 0, 0 }); break;
            case 4: askNumber ("Mix #", 1, 8, 1, [add] (int v) { add ({ T::MixChange, v - 1, 0, 0 }); }); break;
            case 5: case 6:
            {
                const int dir = r == 5 ? 1 : 2;
                askNumber ("BPM change", 1, 99, 10, [this, add, dir] (int bpm) {
                    askNumber ("Over how many beats", 1, 32, 8, [add, dir, bpm] (int beats) { add ({ T::TempoChange, dir, bpm, beats }); }); });
                break;
            }
            case 7: askNumber ("Sub-song #", 0, 99, 0, [add] (int v) { add ({ T::SubSong, v, 0, 0 }); }); break;
            case 8: add ({ T::TriggerOut, 0, 0, 0 }); break;
            case 9: add ({ T::End, 0, 0, 0 }); break;
            case 10: add ({ T::End, 1, 0, 0 }); break;
            case 11: askNumber ("Link to song #", 0, 99, 0, [add] (int v) { add ({ T::End, 2, v, 0 }); }); break;
            case 12: if (n > 0) en.songDeleteStep (song, n - 1); break;
            case 13: en.songClear (song); break;
            case 14: askNumber ("Song tempo (0 = none)", 0, (int) kMaxTempo, 0, [this, song] (int v) { engine().setSongTempo (song, (double) v); }); break;
            default: break;
        }
    });
}

void Crate12Editor::showSampleMenu()
{
    auto& e = engine();
    Globals g; { Lock l (e.mutex()); g = e.project().globals; }
    const int s = e.getSelectedSound();
    juce::PopupMenu m, gain;
    for (int db : { 0, 20, 40 }) gain.addItem (300 + db, "+" + juce::String (db) + " dB", true, g.inputGainDb == db);
    m.addSectionHeader (juce::String ("SAMPLE  -> sound ") + bankLetter (s / 8) + juce::String (s % 8 + 1) + " (tap a pad to select)");
    m.addSubMenu ("3 Input level", gain);
    m.addItem (4, "4 Threshold... (" + juce::String ((int) (g.sampleThreshold * 100)) + "%)");
    m.addItem (5, "5 Sample length... (" + juce::String (g.sampleLengthSec, 1) + " s)");
    m.addItem (7, "7 Arm sampling");
    m.addItem (9, "9 Force sampling");
    m.addItem (10, "Cancel sampling");
    m.addItem (11, "Load audio file into selected sound...");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, s] (int r) {
        auto& en = engine();
        if (r >= 300) { en.setInputGainDb (r - 300); return; }
        switch (r)
        {
            case 4: askNumber ("Threshold (0-100 %)", 0, 100, 5, [this] (int v) { engine().setSampleThreshold ((float) v / 100.0f); }); break;
            case 5: askNumber ("Length in tenths of a second (1-25)", 1, 600, 25, [this] (int v) { engine().setSampleLength ((float) v / 10.0f); }); break;
            case 7: en.armSampling (s); break;
            case 9: en.forceSampling (s); break;
            case 10: en.cancelSampling(); break;
            case 11: chooseFile (false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", [this, s] (juce::File f) { proc.loadSampleFile (f, s); syncControlsFromEngine(); }); break;
            default: break;
        }
    });
}

// ===================================================================== themes
namespace
{
class SwatchPicker : public juce::ColourSelector, private juce::ChangeListener
{
public:
    SwatchPicker (juce::Colour start, std::function<void (juce::Colour)> cb)
        : juce::ColourSelector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace),
          onPick (std::move (cb))
    {
        setCurrentColour (start, juce::dontSendNotification);
        setSize (300, 380);
        addChangeListener (this);
    }
    ~SwatchPicker() override { removeChangeListener (this); }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onPick) onPick (getCurrentColour()); }
    std::function<void (juce::Colour)> onPick;
};
} // namespace

ThemeEditor::ThemeEditor (ThemeManager& tm, Theme start, std::function<void (const Theme&, bool)> cb)
    : themes (tm), working (std::move (start)), apply (std::move (cb))
{
    const auto& slots = Theme::slots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto* l = labels.add (new juce::Label ({}, slots[i].label));
        addAndMakeVisible (l);
        auto* b = swatches.add (new juce::TextButton());
        b->getProperties().set ("swatch", true);
        b->onClick = [this, i, b] {
            const auto member = Theme::slots()[i].member;
            auto picker = std::make_unique<SwatchPicker> (working.*member, [this, member] (juce::Colour c) {
                working.*member = c;
                refresh();
                apply (working, false);
            });
            juce::CallOutBox::launchAsynchronously (std::move (picker), b->getScreenBounds(), nullptr);
        };
        addAndMakeVisible (b);
    }

    int id = 1;
    for (auto& t : themes.builtIns()) base.addItem (t.name, id++);
    for (auto& t : themes.userThemes()) base.addItem (t.name + " (custom)", id++);
    base.setText ("Choose a theme to start from...", juce::dontSendNotification);
    base.onChange = [this] {
        const int idx = base.getSelectedItemIndex();
        const auto& b = themes.builtIns();
        const auto user = themes.userThemes();
        if (idx < 0) return;
        const Theme picked = idx < (int) b.size() ? b[(size_t) idx] : user[(size_t) idx - b.size()];
        const auto keepName = name.getText();
        working = picked;
        working.name = keepName;
        refresh();
        apply (working, false);
    };
    addAndMakeVisible (base);
    addAndMakeVisible (baseLabel);
    addAndMakeVisible (nameLabel);
    name.setText (working.builtIn ? working.name + " custom" : working.name);
    addAndMakeVisible (name);

    save.onClick = [this] {
        working.name = name.getText().trim().isEmpty() ? juce::String ("My Theme") : name.getText().trim();
        for (auto& b : themes.builtIns()) if (b.name == working.name) working.name << " custom";
        name.setText (working.name, false);
        working.builtIn = false;
        if (themes.saveUserTheme (working)) apply (working, true);
    };
    exportBtn.onClick = [this] {
        working.name = name.getText().trim().isEmpty() ? juce::String ("My Theme") : name.getText().trim();
        chooser = std::make_unique<juce::FileChooser> ("Export theme", juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (working.name) + ".c12theme"), "*.c12theme");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) themes.exportTheme (working, fc.getResult().withFileExtension ("c12theme")); });
    };
    importBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import theme", juce::File(), "*.c12theme");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File() || ! themes.importTheme (f)) return;
            const auto v = juce::JSON::parse (f.loadFileAsString());
            working = Theme::fromVar (v, working);
            name.setText (working.name, false);
            refresh();
            apply (working, false);
        });
    };
    for (auto* b : { &save, &exportBtn, &importBtn }) addAndMakeVisible (b);
    refresh();
    setSize (460, 40 + 30 * (int) slots.size() + 130);
}

void ThemeEditor::refresh()
{
    const auto& slots = Theme::slots();
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto col = working.*(slots[i].member);
        auto* b = swatches[(int) i];
        b->setButtonText ("#" + col.toDisplayString (false));
        b->setColour (juce::TextButton::buttonColourId, col);
        b->setColour (juce::TextButton::textColourOffId, col.contrasting (1.0f));
        b->repaint();
    }
}

void ThemeEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.25f));
}

void ThemeEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto top = r.removeFromTop (28);
    baseLabel.setBounds (top.removeFromLeft (110));
    base.setBounds (top);
    r.removeFromTop (8);
    for (int i = 0; i < swatches.size(); ++i)
    {
        auto row = r.removeFromTop (30);
        labels[i]->setBounds (row.removeFromLeft (200));
        swatches[i]->setBounds (row.reduced (0, 3));
    }
    r.removeFromTop (10);
    auto nm = r.removeFromTop (28);
    nameLabel.setBounds (nm.removeFromLeft (110));
    name.setBounds (nm);
    r.removeFromTop (12);
    auto btns = r.removeFromTop (34);
    const int w = btns.getWidth() / 3;
    save.setBounds (btns.removeFromLeft (w).reduced (3, 0));
    exportBtn.setBounds (btns.removeFromLeft (w).reduced (3, 0));
    importBtn.setBounds (btns.reduced (3, 0));
}

void Crate12Editor::showThemeMenu()
{
    juce::PopupMenu m, user;
    const auto current = themes.current().name;
    int id = 1;
    m.addSectionHeader ("Built-in themes");
    for (auto& t : themes.builtIns()) m.addItem (id++, t.name, true, t.name == current);
    const auto users = themes.userThemes();
    const int userBase = id;
    for (auto& t : users) user.addItem (id++, t.name, true, t.name == current);
    if (users.empty()) user.addItem (999, "(none yet)", false);
    m.addSubMenu ("My themes", user);
    m.addSeparator();
    m.addItem (1000, "Create / edit colour scheme...");
    m.addItem (1001, "Import theme file...");
    m.addItem (1002, "Delete current custom theme", ! users.empty() && ! themes.current().builtIn);
    m.addItem (1003, "Show themes folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeBtn), [this, users, userBase] (int r) {
        const auto& b = themes.builtIns();
        if (r >= 1 && r <= (int) b.size()) { applyTheme (b[(size_t) r - 1], true); return; }
        if (r >= userBase && r < userBase + (int) users.size()) { applyTheme (users[(size_t) (r - userBase)], true); return; }
        switch (r)
        {
            case 1000: openThemeEditor(); break;
            case 1001:
                chooseFile (false, "*.c12theme", [this] (juce::File f) {
                    if (themes.importTheme (f))
                    {
                        auto t = Theme::fromVar (juce::JSON::parse (f.loadFileAsString()), themes.current());
                        themes.selectByName (t.name);
                        applyTheme (themes.current(), true);
                    }
                });
                break;
            case 1002: themes.deleteUserTheme (themes.current().name); applyTheme (themes.builtIns().front(), true); break;
            case 1003: themes.userThemeDir().revealToUser(); break;
            default: break;
        }
    });
}

void Crate12Editor::openThemeEditor()
{
    if (themeWindow != nullptr) { themeWindow->toFront (true); return; }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ThemeEditor (themes, themes.current(), [this] (const Theme& t, bool remember) { applyTheme (t, remember); }));
    o.dialogTitle = "Crate12 colour scheme";
    o.componentToCentreAround = this;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    themeWindow = o.launchAsync();
    if (themeWindow != nullptr) themeWindow->setLookAndFeel (&lnf);
}
