#include "FrontPanel.h"
#include "PluginEditor.h"

#include <cstdio>
#include <cstring>

using namespace c12;
using Lock = std::lock_guard<std::recursive_mutex>;

namespace
{
std::string two (int v) { char b[8]; std::snprintf (b, sizeof (b), "%02d", v); return b; }
const char* kNameChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#&+.";
}

Engine& FrontPanel::eng() { return ed.engine(); }

void FrontPanel::show (const std::string& a, const std::string& b)
{
    Lock l (eng().mutex());
    eng().setLcd (a, b);
}

std::string FrontPanel::soundName (int s)
{
    Lock l (eng().mutex());
    std::string n (1, bankLetter (s / 8));
    n += std::to_string (s % 8 + 1);
    const auto& nm = eng().project().sounds[(size_t) s].name;
    if (! nm.empty()) n += " " + nm;
    return n;
}

int FrontPanel::digitsNeeded() const
{
    return (mod == Module::Setup || mod == Module::Special) ? 2 : 1;
}

void FrontPanel::cancel (const char* why)
{
    mod = Module::None; fn = -1; stage = 0; digits.clear(); argA = argB = -1;
    show (why ? why : "Cancelled", "");
    ed.syncControlsFromEngine();
}

void FrontPanel::done (const std::string& a, const std::string& b)
{
    mod = Module::None; fn = -1; stage = 0; digits.clear(); argA = argB = -1;
    show (a, b);
    ed.syncControlsFromEngine();
}

void FrontPanel::start (Module m)
{
    if (m == Module::Special) { mod = Module::Special; }   // also reachable as SET-UP 23
    mod = m; fn = -1; stage = 0; digits.clear(); argA = argB = -1;
    switch (m)
    {
        case Module::Step:
            stepTick = 0;
            showStep();
            return;
        case Module::SegLength:
            fn = 0;
            show ("Segment " + two (eng().getSegment()) + " Len", "Bars? __");
            return;
        case Module::Copy:
            fn = 0;
            show ("Copy Segment " + two (eng().getSegment()), "To segment? __");
            return;
        default: prompt(); return;
    }
}

void FrontPanel::prompt()
{
    const std::string d = digits + std::string ((size_t) (digitsNeeded() - (int) digits.size()), '_');
    switch (mod)
    {
        case Module::Setup:   show ("Set-up Function", "Enter 11-23  " + d); break;
        case Module::Special: show ("Special Function", "Enter 12-99  " + d); break;
        case Module::Sync:    show ("Sync 1Int 2MIDI", "3SMPTE 4Click " + d); break;
        case Module::Disk:    show ("Disk Function", "Enter 0-9    " + d); break;
        case Module::Sample:  show ("Sample Function", "Enter 1-9    " + d); break;
        default: break;
    }
}

// ===================================================================== function entry
void FrontPanel::enterFunction()
{
    auto& e = eng();
    stage = 0; digits.clear();
    if (mod == Module::Setup)
    {
        switch (fn)
        {
            case 11: show ("Multi-Pitch", "Press a pad"); return;
            case 12: show ("Multi-Level", "Press a pad"); return;
            case 13: e.exitMultiMode(); done ("Multi Mode Off"); return;
            case 14: show ("Dynamic Buttons", "Yes / No?"); return;
            case 15: show ("Define Mix", "Mix # 1-8?"); return;
            case 16: show ("Select Mix", "Mix # 1-8?"); return;
            case 17: show ("Channel Assign", "Press a pad"); return;
            case 18: show ("Tune/Decay Select", "Press a pad"); return;
            case 19: show ("Truncate / Loop", "Press a pad"); return;
            case 20: show ("Delete Sound", "Press a pad"); return;
            case 21: show ("First Song Step", "Step? __ Enter"); return;
            case 22: show ("MIDI Channel", "01-16? __ Enter"); return;
            case 23: mod = Module::Special; fn = -1; prompt(); return;
            default: cancel ("No such function"); return;
        }
    }
    if (mod == Module::Special)
    {
        switch (fn)
        {
            case 12: show ("Clear All Memory", "Sure? Yes / No"); return;
            case 13:
            {
                const auto mi = e.memoryInfo();
                char a[20], b[20];
                std::snprintf (a, sizeof (a), "Smpl %.1fs free", (double) (mi.samplesTotal - mi.samplesUsed) / kNativeRate);
                std::snprintf (b, sizeof (b), "Seq %d%% free", 100 - mi.notesUsed * 100 / std::max (1, mi.notesTotal));
                done (a, b);
                return;
            }
            case 14: show ("Write SMPTE", e.isSmpteWriting() ? "Stop? Yes / No" : "Start? Yes / No"); return;
            case 15: show ("Clear Sounds", "Sure? Yes / No"); return;
            case 16: show ("Clear Sequences", "Sure? Yes / No"); return;
            case 17: show ("Copy Sound", "Press source pad"); return;
            case 18: show ("Swap Sounds", "Press first pad"); return;
            case 19: show ("Default Decay", "Slider 1, Enter"); return;
            case 20: show ("Reverse Sound", "Press a pad"); return;
            case 21: show ("Name Sound", "Press a pad"); return;
            case 22: show ("Dynamic Alloc", "Pair 1-4?"); return;
            case 23: show ("Tx Sound Dump", "Press a pad"); return;
            case 24: show ("Rx Sound Dump", "Press a pad"); return;
            case 99: ed.startLampTest(); done ("Lamp Test"); return;
            default: cancel ("No such function"); return;
        }
    }
    if (mod == Module::Sync)
    {
        switch (fn)
        {
            case 1: e.setSyncMode (SyncMode::Internal); done ("Sync: Internal"); return;
            case 2: e.setSyncMode (SyncMode::Midi); done ("Sync: MIDI"); return;
            case 3: show ("SMPTE Rate 1:24", "2:25 3:30 4:30df"); return;
            case 4: e.setSyncMode (SyncMode::Click); done ("Sync: Click", "24 ppq on input"); return;
            case 5: e.setSyncMode (SyncMode::Host); done ("Sync: Host (DAW)"); return;
            default: cancel ("No such function"); return;
        }
    }
    if (mod == Module::Disk)
    {
        mod = Module::None;
        ed.diskFunction (fn);
        fn = -1;
        return;
    }
    if (mod == Module::Sample)
    {
        Globals g; { Lock l (e.mutex()); g = e.project().globals; }
        switch (fn)
        {
            case 1: done ("VU Mode", "Arm with Sample 7"); return;
            case 2: show ("Assign Sound", "Press a pad"); return;
            case 3: show ("Input Level", "1:0 2:+20 3:+40"); return;
            case 4: show ("Threshold", "Slider 1, Enter"); return;
            case 5: show ("Sample Length", "Tenths? __ Enter"); return;
            case 6: case 7: e.armSampling (e.getSelectedSound()); mod = Module::None; fn = -1; return;
            case 8: e.cancelSampling(); done ("Sampling", "Cancelled"); return;
            case 9: e.forceSampling (e.getSelectedSound()); mod = Module::None; fn = -1; return;
            default: cancel ("No such function"); return;
        }
    }
}

// ===================================================================== keys
void FrontPanel::key (int k)
{
    if (! active()) return;
    auto& e = eng();
    const bool isDigit = k >= 0 && k <= 9;

    // choosing the function number
    if (fn < 0)
    {
        if (k == No) { cancel(); return; }
        if (k == Left && ! digits.empty()) { digits.pop_back(); prompt(); return; }
        if (! isDigit) return;
        digits += (char) ('0' + k);
        if ((int) digits.size() < digitsNeeded()) { prompt(); return; }
        fn = std::stoi (digits);
        enterFunction();
        return;
    }

    auto numberEntry = [&] (int maxDigits, const std::string& title, const std::string& label) -> int {
        // returns the value on Enter, -1 while typing
        if (isDigit && (int) digits.size() < maxDigits) digits += (char) ('0' + k);
        if (k == Left && ! digits.empty()) digits.pop_back();
        if (k == Enter && ! digits.empty()) { const int v = std::stoi (digits); digits.clear(); return v; }
        show (title, label + digits + std::string ((size_t) std::max (0, maxDigits - (int) digits.size()), '_'));
        return -1;
    };

    if (k == No && ! (mod == Module::Special && fn == 21 && stage == 1))
    {
        // NO answers yes/no questions; elsewhere it backs out
        const bool yesNo = (mod == Module::Setup && fn == 14) || (mod == Module::Special && (fn == 12 || fn == 14 || fn == 15 || fn == 16))
                           || (mod == Module::Setup && fn == 20 && stage == 1) || (mod == Module::Setup && fn == 22 && stage == 2)
                           || (mod == Module::Setup && fn == 19 && stage == 2) || (mod == Module::Special && fn == 22 && stage == 1)
                           || (mod == Module::SegLength && stage == 2);
        if (! yesNo) { cancel(); return; }
    }

    switch (mod)
    {
        case Module::Setup:
            switch (fn)
            {
                case 14: if (k == Yes || k == No) { e.setDynamicButtons (k == Yes); done ("Dynamic Buttons", k == Yes ? "Yes" : "No"); } return;
                case 15: if (k >= 1 && k <= 8) { e.defineMix (k - 1); done ("Mix Defined", "Mix " + std::to_string (k)); } return;
                case 16: if (k >= 1 && k <= 8) { e.selectMix (k - 1); done ("Mix Selected", "Mix " + std::to_string (k)); } return;
                case 17: if (stage == 1 && k >= 1 && k <= 8) { e.channelAssign (argA, k - 1); done (soundName (argA), "-> Channel " + std::to_string (k)); } return;
                case 18: if (stage == 1 && (k == 1 || k == 2)) { e.setDecaySelect (argA, k == 2); done (soundName (argA), k == 2 ? "Slider = Decay" : "Slider = Tune"); } return;
                case 19:
                    if (stage == 1 && (k == Enter || k == Yes)) { stage = 2; show ("Make Truncation", "Permanent? Y/N"); }
                    else if (stage == 2 && (k == Yes || k == No)) { if (k == Yes) e.makeTruncationPermanent (argA); done ("Truncate", k == Yes ? "Made permanent" : "Kept full sound"); }
                    return;
                case 20: if (stage == 1 && (k == Yes || k == No)) { if (k == Yes) e.deleteSound (argA); done (k == Yes ? "Sound Deleted" : "Not deleted", soundName (argA)); } return;
                case 21: { const int v = numberEntry (2, "First Song Step", "Step? "); if (v >= 1) { e.setFirstSongStep (v - 1); done ("First Song Step", two (v)); } return; }
                case 22:
                    if (stage == 0) { const int v = numberEntry (2, "MIDI Channel", "01-16? "); if (v >= 1 && v <= 16) { e.setMidiChannel (v - 1); stage = 1; show ("MIDI Mode", "1 Omni  2 Poly"); } }
                    else if (stage == 1 && (k == 1 || k == 2)) { e.setMidiMode (k == 1 ? MidiMode::Omni : MidiMode::Poly); stage = 2; show ("Pitch Keys", "Yes / No?"); }
                    else if (stage == 2 && (k == Yes || k == No)) { e.setPitchKeys (k == Yes); done ("MIDI Set", k == Yes ? "Pitch keys on" : "Pitch keys off"); }
                    return;
                default: return;
            }
        case Module::Special:
            switch (fn)
            {
                case 12: if (k == Yes) { e.clearAllMemory(); done ("Memory Cleared"); } else if (k == No) cancel(); return;
                case 14: if (k == Yes) { e.setSmpteWrite (! e.isSmpteWriting()); done (e.isSmpteWriting() ? "Writing SMPTE" : "SMPTE Stopped", e.smpteNow()); } else if (k == No) cancel(); return;
                case 15: if (k == Yes) { e.clearSounds(); done ("Sounds Cleared"); } else if (k == No) cancel(); return;
                case 16: if (k == Yes) { e.clearSequences(); done ("Sequences Cleared"); } else if (k == No) cancel(); return;
                case 19: if (k == Enter || k == Yes) { e.setDefaultDecayAll (sl[0]); done ("Default Decay Set", std::to_string ((int) (sl[0] * 100)) + "%"); } return;
                case 21:
                    if (stage != 1) return;
                    if (k == Left) cursor = std::max (0, cursor - 1);
                    if (k == Right) cursor = std::min (7, cursor + 1);
                    if (k == No) { nameBuf[(size_t) cursor] = ' '; }
                    if (k == Enter || k == Yes)
                    {
                        auto n = nameBuf;
                        while (! n.empty() && n.back() == ' ') n.pop_back();
                        e.nameSound (argA, n);
                        done ("Sound Named", soundName (argA));
                        return;
                    }
                    show ("Name: " + nameBuf, std::string ((size_t) (6 + cursor), ' ') + "^ sl1");
                    return;
                case 22:
                    if (stage == 0 && k >= 1 && k <= 4) { argA = k - 1; stage = 1; show ("Dynamic Alloc " + std::to_string (k * 2 - 1) + "/" + std::to_string (k * 2), "On? Yes / No"); }
                    else if (stage == 1 && (k == Yes || k == No)) { e.setDynamicAllocation (argA, k == Yes); done ("Dynamic Alloc", k == Yes ? "On" : "Off"); }
                    return;
                default: return;
            }
        case Module::Sync:
            if (fn == 3)
            {
                if (stage == 0 && k >= 1 && k <= 4)
                {
                    e.setSmpteRate ((SmpteRate) (k - 1));
                    stage = 1;
                    show ("SMPTE Start", "HHMMSSFF Enter");
                }
                else if (stage == 1)
                {
                    const int v = numberEntry (8, "SMPTE Start", "");
                    if (v >= 0)
                    {
                        Globals g; { Lock l (e.mutex()); g = e.project().globals; }
                        const int h = v / 1000000, m = (v / 10000) % 100, s = (v / 100) % 100, f = v % 100;
                        e.setSmpteStart (Engine::timecodeToFrames (h, m, s, f, g.smpteRate));
                        e.setSyncMode (SyncMode::Smpte);
                        done ("Sync: SMPTE", Engine::framesToTimecode (Engine::timecodeToFrames (h, m, s, f, g.smpteRate), g.smpteRate));
                    }
                }
            }
            return;
        case Module::Sample:
            switch (fn)
            {
                case 3: if (k >= 1 && k <= 3) { e.setInputGainDb ((k - 1) * 20); done ("Input Level", "+" + std::to_string ((k - 1) * 20) + " dB"); } return;
                case 4: if (k == Enter || k == Yes) { e.setSampleThreshold (sl[0]); done ("Threshold", std::to_string ((int) (sl[0] * 100)) + "%"); } return;
                case 5: { const int v = numberEntry (2, "Sample Length", "Tenths? "); if (v >= 1) { e.setSampleLength ((float) v / 10.0f); done ("Sample Length", std::to_string (v / 10) + "." + std::to_string (v % 10) + " s"); } return; }
                default: return;
            }
        case Module::SegLength:
            if (stage == 0) { const int v = numberEntry (2, "Seg Length: Bars", "Bars? "); if (v >= 0) { argA = v; stage = 1; show ("Seg Length: Beats", "Beats? __"); } }
            else if (stage == 1) { const int v = numberEntry (2, "Seg Length: Beats", "Beats? "); if (v >= 0) { argB = v; stage = 2; show ("Truncate notes", "past end? Y/N"); } }
            else if (stage == 2 && (k == Yes || k == No))
            {
                e.setSegmentLength (e.getSegment(), argA, argB, k == Yes);
                done ("Segment " + two (e.getSegment()), std::to_string (argA) + " bars " + std::to_string (argB) + " beats");
            }
            return;
        case Module::Copy:
            if (stage == 0)
            {
                const int v = numberEntry (2, "Copy Segment " + two (e.getSegment()), "To segment? ");
                if (v < 0) return;
                if (v == e.getSegment()) { stage = 1; argA = v; show ("Append Segment", "Times? __ Enter"); return; }
                const bool ok = e.copySegment (e.getSegment(), v);
                done (ok ? "Segment Copied" : "Sequence Memory", ok ? two (e.getSegment()) + " -> " + two (v) : "Full");
            }
            else
            {
                const int v = numberEntry (2, "Append Segment", "Times? ");
                if (v >= 1) { const bool ok = e.copySegment (e.getSegment(), e.getSegment(), v); done (ok ? "Appended" : "Sequence Memory", ok ? std::to_string (v) + " times" : "Full"); }
            }
            return;
        case Module::Step:
        {
            Segment seg; { Lock l (e.mutex()); seg = e.project().segments[(size_t) e.getSegment()]; }
            int grid = 24;
            { Lock l (e.mutex()); grid = autoCorrectTicks (e.project().globals.autoCorrect); }
            if (grid <= 1) grid = 6;
            const int len = std::max (1, seg.lengthTicks());
            if (k == Right) stepTick = (stepTick + grid) % len;
            if (k == Left) stepTick = (stepTick - grid + len) % len;
            if (k == Enter || k == No) { done ("Step Program", "Done"); return; }
            if (k == Left || k == Right)
                for (auto& ev : seg.events) if (ev.tick == stepTick) e.triggerSound (ev.sound, ev.velocity);
            showStep();
            return;
        }
        default: return;
    }
}

// ===================================================================== pads and sliders
bool FrontPanel::pad (int p, bool eraseHeld)
{
    if (! active() || (fn < 0 && mod != Module::Step)) return false;
    auto& e = eng();
    const int s = soundIndex (e.getBank(), p);
    if (mod == Module::Step)
    {
        if (eraseHeld) e.stepErase (e.getSegment(), stepTick, s);
        else { e.stepInsert (e.getSegment(), stepTick, s, 127); e.triggerSound (s, 127); }
        showStep();
        return true;
    }
    if (mod == Module::Setup)
    {
        switch (fn)
        {
            case 11: e.selectSound (s); e.enterMultiMode (SliderMode::MultiPitch); mod = Module::None; fn = -1; ed.syncControlsFromEngine(); return true;
            case 12: e.selectSound (s); e.enterMultiMode (SliderMode::MultiLevel); mod = Module::None; fn = -1; ed.syncControlsFromEngine(); return true;
            case 17: argA = s; stage = 1; show (soundName (s), "Channel 1-8?"); return true;
            case 18: argA = s; stage = 1; show (soundName (s), "1 Tune  2 Decay"); return true;
            case 19:
            {
                argA = s; stage = 1;
                Lock l (e.mutex());
                const auto& snd = e.project().sounds[(size_t) s];
                const float size = (float) std::max<size_t> (1, snd.data.size());
                const float st = (float) snd.truncStart / size, en = (float) snd.endPos() / size, lp = snd.loop ? (float) snd.loopLength / size : 0.0f;
                sl[0] = st; sl[1] = 0.5f; sl[2] = en; sl[3] = 0.5f; sl[4] = lp; sl[5] = 0.5f;
                showTruncate();
                return true;
            }
            case 20: argA = s; stage = 1; show ("Delete " + soundName (s), "Sure? Yes / No"); return true;
            default: return false;
        }
    }
    if (mod == Module::Special)
    {
        switch (fn)
        {
            case 17:
                if (stage == 0) { argA = s; stage = 1; show ("Copy " + soundName (s), "Press dest pad"); }
                else { e.copySound (argA, s); done ("Sound Copied", soundName (argA).substr (0, 2) + " -> " + soundName (s).substr (0, 2)); }
                return true;
            case 18:
                if (stage == 0) { argA = s; stage = 1; show ("Swap " + soundName (s), "Press second pad"); }
                else { e.swapSounds (argA, s); done ("Sounds Swapped", soundName (argA).substr (0, 2) + " <> " + soundName (s).substr (0, 2)); }
                return true;
            case 20: e.reverseSound (s); done ("Reversed", soundName (s)); return true;
            case 21:
            {
                argA = s; stage = 1; cursor = 0;
                Lock l (e.mutex());
                nameBuf = e.project().sounds[(size_t) s].name;
                nameBuf.resize (8, ' ');
                show ("Name: " + nameBuf, "      ^ sl1, < >");
                return true;
            }
            case 23: e.sdsSend (s); mod = Module::None; fn = -1; return true;
            case 24: e.sdsArmReceive (s); mod = Module::None; fn = -1; return true;
            default: return false;
        }
    }
    if (mod == Module::Sample && fn == 2)
    {
        e.selectSound (s);
        done ("Sample Into", soundName (s));
        return true;
    }
    return false;
}

bool FrontPanel::sliderPositions (float* v8) const
{
    if (mod == Module::Setup && fn == 19 && stage == 1)
    {
        for (int i = 0; i < 8; ++i) v8[i] = i < 6 ? sl[i] : 0.0f;
        return true;
    }
    return false;
}

bool FrontPanel::slider (int i, float v)
{
    if (! active() || fn < 0) return false;
    if (mod == Module::Setup && fn == 19 && stage == 1 && i < 6)
    {
        sl[i] = v;
        auto& e = eng();
        int size = 0, loopLen = 0;
        { Lock l (e.mutex()); size = (int) e.project().sounds[(size_t) argA].data.size(); }
        if (size <= 0) return true;
        // coarse + fine pairs (fine = +/- 1/32 of the coarse range)
        auto pos = [&] (int c) { return std::max (0.0f, std::min (1.0f, sl[c] + (sl[c + 1] - 0.5f) / 16.0f)); };
        const int st = (int) (pos (0) * (float) size);
        const int en = std::max (st + 1, (int) (pos (2) * (float) size));
        loopLen = (int) (pos (4) * (float) size);
        e.setTruncation (argA, st, en >= size ? 0 : en);
        e.setLoop (argA, loopLen > 0, loopLen);
        showTruncate();
        return true;
    }
    if (i == 0 && ((mod == Module::Special && fn == 19) || (mod == Module::Sample && fn == 4)))
    {
        sl[0] = v;
        show (mod == Module::Special ? "Default Decay" : "Threshold", std::to_string ((int) (v * 100)) + "%  Enter");
        return true;
    }
    if (i == 0 && mod == Module::Special && fn == 21 && stage == 1)
    {
        const int n = (int) std::strlen (kNameChars);
        nameBuf[(size_t) cursor] = kNameChars[std::min (n - 1, (int) (v * (float) n))];
        show ("Name: " + nameBuf, std::string ((size_t) (6 + cursor), ' ') + "^");
        return true;
    }
    return false;
}

void FrontPanel::showTruncate()
{
    auto& e = eng();
    Lock l (e.mutex());
    const auto& s = e.project().sounds[(size_t) argA];
    char a[24], b[24];
    std::snprintf (a, sizeof (a), "S%5d E%5d", s.truncStart, s.endPos());
    std::snprintf (b, sizeof (b), s.loop ? "Loop %5d Enter" : "Loop off   Enter", s.loopLength);
    e.setLcd (a, b);
}

void FrontPanel::showStep()
{
    auto& e = eng();
    Lock l (e.mutex());
    const auto& seg = e.project().segments[(size_t) e.getSegment()];
    const int tpb = seg.ticksPerBeat();
    const int bar = stepTick / (tpb * std::max (1, seg.beatsPerBar)) + 1;
    const int beat = (stepTick / tpb) % std::max (1, seg.beatsPerBar) + 1;
    const int sub = stepTick % tpb;
    std::string names;
    int count = 0;
    for (auto& ev : seg.events)
        if (ev.tick == stepTick)
        {
            if (count < 4) names += std::string (count ? " " : "") + bankLetter (ev.sound / 8) + std::to_string (ev.sound % 8 + 1);
            ++count;
        }
    if (count > 4) names += "+";
    char a[24];
    std::snprintf (a, sizeof (a), "Step %02d:%d:%02d", bar, beat, sub);
    e.setLcd (a, names.empty() ? "-- < > pads" : names);
}
