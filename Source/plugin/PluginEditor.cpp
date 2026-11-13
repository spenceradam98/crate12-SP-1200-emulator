#include "PluginEditor.h"

using namespace c12;

namespace
{
const juce::Colour kPanel (0xff2b2d31), kPanelHi (0xff3a3d43), kAccent (0xffe8792b), kText (0xffe6e2d6),
    kPadCol (0xff55585f), kLcdBg (0xff1d2b12), kLcdFg (0xffb6ff5a);

template <typename T> T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }
using Lock = std::lock_guard<std::recursive_mutex>;
} // namespace

// ===================================================================== pads / lcd
void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3);
    g.setColour (down ? kAccent.darker (0.2f) : kPadCol);
    g.fillRoundedRectangle (r, 6);
    if (flash > 0.01f) { g.setColour (kAccent.withAlpha (flash)); g.fillRoundedRectangle (r, 6); }
    g.setColour (dragOver ? kAccent : kPanelHi.brighter (0.3f));
    g.drawRoundedRectangle (r, 6, dragOver ? 3.0f : 1.5f);
    g.setColour (kText.withAlpha (hasSound ? 1.0f : 0.45f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (label, getLocalBounds().reduced (6), juce::Justification::centred, 2);
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
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, 4);
    g.setColour (kLcdBg);
    g.fillRoundedRectangle (r.reduced (4), 3);
    g.setColour (kLcdFg);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::plain));
    auto inner = getLocalBounds().reduced (12, 8);
    g.drawText (line1.paddedRight (' ', 16), inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft);
    g.drawText (line2.paddedRight (' ', 16), inner, juce::Justification::centredLeft);
}

// ===================================================================== editor
Crate12Editor::Crate12Editor (Crate12Processor& p) : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (lcd);

    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto* pad = pads.add (new PadButton (*this, i));
        addAndMakeVisible (pad);

        auto* s = sliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
        s->setRange (0.0, 1.0);
        s->setColour (juce::Slider::thumbColourId, kAccent);
        s->onValueChange = [this, i, s] { if (! updating) engine().setSlider (i, (float) s->getValue()); };
        addAndMakeVisible (s);
        auto* l = sliderLabels.add (new juce::Label ({}, juce::String (i + 1)));
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* bb = bankButtons.add (new juce::TextButton (juce::String ("BANK ") + bankLetter (b)));
        bb->setClickingTogglesState (false);
        bb->onClick = [this, b] { engine().setBank (b); syncControlsFromEngine(); };
        addAndMakeVisible (bb);
    }

    const char* modeNames[] = { "MIX", "TUNE", "DECAY", "MULTI PITCH", "MULTI LEVEL", "MULTI DECAY", "EXIT MULTI" };
    for (int m = 0; m < 7; ++m)
    {
        auto* mb = modeButtons.add (new juce::TextButton (modeNames[m]));
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
                default: e.exitMultiMode(); break;
            }
            syncControlsFromEngine();
        };
        addAndMakeVisible (mb);
    }

    for (auto* b : { &runStop, &record, &songSeg, &tapTempo, &tapRepeat, &eraseBtn, &setupBtn, &specialBtn,
                     &diskBtn, &segmentBtn, &songBtn, &sampleBtn, &defineMix })
        addAndMakeVisible (b);

    runStop.onClick = [this] { engine().runStop(); };
    record.onClick = [this] { engine().setRecord (! engine().isRecording()); };
    songSeg.onClick = [this] { engine().setSongMode (! engine().isSongMode()); syncControlsFromEngine(); };
    tapTempo.onClick = [this] { engine().tapTempo(); syncControlsFromEngine(); };
    tapRepeat.onStateChange = [this] { engine().setTapRepeat (tapRepeat.isDown()); };
    eraseBtn.onStateChange = [this] { engine().setEraseHeld (eraseBtn.isDown()); };
    setupBtn.onClick = [this] { showSetupMenu(); };
    specialBtn.onClick = [this] { showSpecialMenu(); };
    diskBtn.onClick = [this] { showDiskMenu(); };
    segmentBtn.onClick = [this] { showSegmentMenu(); };
    songBtn.onClick = [this] { showSongMenu(); };
    sampleBtn.onClick = [this] { showSampleMenu(); };
    defineMix.onClick = [this] { engine().defineMix (mixSel.getSelectedItemIndex()); };

    tempo.setSliderStyle (juce::Slider::LinearHorizontal);
    tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
    tempo.setRange (kMinTempo, kMaxTempo, 0.1);
    tempo.onValueChange = [this] { if (! updating) engine().setTempo (tempo.getValue()); };
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
    swing.onChange = [this] { if (! updating) engine().setSwing (swing.getSelectedItemIndex()); };
    for (int i = 0; i <= 8; ++i) autoCorrect.addItem (autoCorrectName ((AutoCorrect) i), i + 1);
    autoCorrect.onChange = [this] { if (! updating) engine().setAutoCorrect ((AutoCorrect) autoCorrect.getSelectedItemIndex()); };
    const char* metNames[] = { "Off", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "Click 24ppq" };
    for (int i = 0; i < 10; ++i) metronome.addItem (metNames[i], i + 1);
    metronome.onChange = [this] { if (! updating) engine().setMetronome ((Metronome) metronome.getSelectedItemIndex()); };
    syncMode.addItem ("Internal", 1); syncMode.addItem ("MIDI", 2); syncMode.addItem ("SMPTE (n/a)", 3);
    syncMode.addItem ("Click (n/a)", 4); syncMode.addItem ("Host", 5);
    syncMode.setItemEnabled (3, false); syncMode.setItemEnabled (4, false);
    syncMode.onChange = [this] { if (! updating) engine().setSyncMode ((SyncMode) syncMode.getSelectedId()); };
    for (int i = 0; i < kNumMixes; ++i) mixSel.addItem ("Mix " + juce::String (i + 1), i + 1);
    mixSel.onChange = [this] { if (! updating) engine().selectMix (mixSel.getSelectedItemIndex()); };
    for (auto* c : { &swing, &autoCorrect, &metronome, &syncMode, &mixSel }) addAndMakeVisible (c);
    for (auto* l : { &tempoLabel, &numberLabel, &swingL, &acL, &metL, &syncL, &mixL })
    {
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, kText.withAlpha (0.7f));
        addAndMakeVisible (l);
    }

    setResizable (true, true);
    setResizeLimits (900, 560, 2000, 1200);
    setSize (1040, 620);
    syncControlsFromEngine();
    startTimerHz (30);
}

Crate12Editor::~Crate12Editor() { stopTimer(); }

void Crate12Editor::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("CRATE12", 20, getHeight() - 36, 200, 28, juce::Justification::centredLeft);
    g.setColour (kText.withAlpha (0.5f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("12-BIT SAMPLING PERCUSSION  26.04 kHz", 130, getHeight() - 36, 400, 28, juce::Justification::centredLeft);
}

void Crate12Editor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromBottom (32);

    // top: LCD + transport
    auto top = r.removeFromTop (110);
    lcd.setBounds (top.removeFromLeft (330));
    top.removeFromLeft (16);
    auto row1 = top.removeFromTop (34);
    for (auto* b : { &runStop, &record, &songSeg, &tapTempo, &tapRepeat, &eraseBtn })
    { b->setBounds (row1.removeFromLeft (100).reduced (2)); }
    top.removeFromTop (8);
    auto row2 = top.removeFromTop (34);
    for (auto* b : { &setupBtn, &specialBtn, &diskBtn, &segmentBtn, &songBtn, &sampleBtn })
    { b->setBounds (row2.removeFromLeft (100).reduced (2)); }
    top.removeFromTop (6);
    auto row3 = top;
    tempoLabel.setBounds (row3.removeFromLeft (50));
    tempo.setBounds (row3.removeFromLeft (230));
    numberLabel.setBounds (row3.removeFromLeft (50));
    number.setBounds (row3.removeFromLeft (120));

    r.removeFromTop (10);
    auto combos = r.removeFromTop (46);
    auto place = [&combos] (juce::Label& l, juce::Component& c, int w) {
        auto a = combos.removeFromLeft (w).reduced (3, 0);
        l.setBounds (a.removeFromTop (16));
        c.setBounds (a.removeFromTop (26));
    };
    place (swingL, swing, 100); place (acL, autoCorrect, 120); place (metL, metronome, 130);
    place (syncL, syncMode, 130); place (mixL, mixSel, 100);
    defineMix.setBounds (combos.removeFromLeft (110).reduced (3, 0).withTrimmedTop (16).withHeight (26));

    r.removeFromTop (8);
    auto modes = r.removeFromTop (30);
    for (auto* m : modeButtons) m->setBounds (modes.removeFromLeft (118).reduced (2, 0));

    r.removeFromTop (8);
    auto banks = r.removeFromBottom (30);
    for (auto* b : bankButtons) b->setBounds (banks.removeFromLeft (100).reduced (2, 0));
    r.removeFromBottom (8);

    auto padRow = r.removeFromBottom (90);
    auto sliderArea = r;
    const int w = padRow.getWidth() / kPadsPerBank;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        pads[i]->setBounds (padRow.removeFromLeft (w).reduced (4));
        auto s = sliderArea.removeFromLeft (w);
        sliderLabels[i]->setBounds (s.removeFromBottom (18));
        sliders[i]->setBounds (s.reduced (w / 3, 4));
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
        bankButtons[b]->setColour (juce::TextButton::buttonColourId, b == e.getBank() ? kAccent.darker (0.3f) : kPanelHi);
    const int modeIdx = e.inMultiMode() ? -1 : (int) e.getSliderMode();
    for (int m = 0; m < 3; ++m)
        modeButtons[m]->setColour (juce::TextButton::buttonColourId, m == modeIdx ? kAccent.darker (0.3f) : kPanelHi);
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
    runStop.setColour (juce::TextButton::buttonColourId, e.isPlaying() ? juce::Colour (0xff2f8a3a) : kPanelHi);
    record.setColour (juce::TextButton::buttonColourId, e.isRecording() ? juce::Colour (0xffb02a2a) : kPanelHi);

    static int slow = 0;
    if (++slow % 15 == 0 && ! isMouseButtonDownAnywhere()) syncControlsFromEngine();
}

// ===================================================================== pads
void Crate12Editor::padDown (int pad, float vel)
{
    engine().padPress (pad, juce::jlimit (1, 127, (int) std::lround (vel * 127.0f)));
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
    m.addItem (18, juce::String ("18 Tune/Decay select: ") + (g.tuneMode ? "Tune" : "Decay"));
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
            case 18: e.setTuneDecayMode (! g.tuneMode); break;
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
    m.addItem (14, "14 Write SMPTE (not applicable in software)", false);
    m.addItem (15, "15 Clear All Sounds");
    m.addItem (16, "16 Clear All Sequences");
    m.addItem (17, "17 Copy Sound (selected)...");
    m.addItem (18, "18 Swap Sound (selected)...");
    m.addItem (19, "19 Default Decay (all sounds)...");
    m.addItem (20, "Reverse Sound (selected)");
    m.addItem (21, "Name Sound: right-click a pad", false);
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
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) {
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
            default: break;
        }
    });
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
