#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace c12
{
// ===================================================================== helpers
int autoCorrectTicks (AutoCorrect q)
{
    switch (q)
    {
        case AutoCorrect::Q4:   return kPPQ;
        case AutoCorrect::Q4T:  return kPPQ * 2 / 3;
        case AutoCorrect::Q8:   return kPPQ / 2;
        case AutoCorrect::Q8T:  return kPPQ / 3;
        case AutoCorrect::Q16:  return kPPQ / 4;
        case AutoCorrect::Q16T: return kPPQ / 6;
        case AutoCorrect::Q32:  return kPPQ / 8;
        case AutoCorrect::Q32T: return kPPQ / 12;
        case AutoCorrect::HiRes: default: return 1;
    }
}

const char* autoCorrectName (AutoCorrect q)
{
    static const char* names[] = { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "HiRes" };
    return names[(int) q];
}

static int metronomeTicks (Metronome m)
{
    switch (m)
    {
        case Metronome::Q4:   return autoCorrectTicks (AutoCorrect::Q4);
        case Metronome::Q4T:  return autoCorrectTicks (AutoCorrect::Q4T);
        case Metronome::Q8:   return autoCorrectTicks (AutoCorrect::Q8);
        case Metronome::Q8T:  return autoCorrectTicks (AutoCorrect::Q8T);
        case Metronome::Q16:  return autoCorrectTicks (AutoCorrect::Q16);
        case Metronome::Q16T: return autoCorrectTicks (AutoCorrect::Q16T);
        case Metronome::Q32:  return autoCorrectTicks (AutoCorrect::Q32);
        case Metronome::Q32T: return autoCorrectTicks (AutoCorrect::Q32T);
        case Metronome::Click24: return kPPQ / 24;
        default: return 0;
    }
}

template <typename T> static T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }

static std::string fmt (const char* f, ...)
{
    char buf[64];
    va_list args;
    va_start (args, f);
    std::vsnprintf (buf, sizeof (buf), f, args);
    va_end (args);
    return buf;
}

using Lock = std::lock_guard<std::recursive_mutex>;

// ===================================================================== lifecycle
Engine::Engine()
{
    for (auto& m : meters) m.store (0.0f);
    proj.mixes.fill (Mix {});
    updateDefaultLcd();
}

void Engine::prepare (double sampleRate, int)
{
    Lock l (mtx);
    sr = sampleRate;
    for (auto& f : ladder) { f.fs = sr; f.reset(); }
    for (int i = 0; i < 4; ++i)
    {
        fixedLp[i].set (sr, i < 2 ? proj.globals.filter34Hz : proj.globals.filter56Hz);
        fixedLp[i].reset();
    }
    inputAA.set (sr, std::min (12500.0, sr * 0.45));
    inputAA.reset();
    dcL.reset(); dcR.reset();
    nativePhase = 0;
}

// ===================================================================== audio
double Engine::effectiveTempo() const
{
    double t = proj.globals.tempo + tempoOffset;
    if (proj.globals.syncMode == SyncMode::Host && hostWasPlaying) t = hostTempo;
    return clampv (t, kMinTempo, kMaxTempo * 2.0);
}

void Engine::process (const float* inL, const float* inR, float* mainL, float* mainR, float* const* chOut,
                      int n, const std::vector<MidiMsg>& midiIn, std::vector<MidiMsg>& midiOut,
                      const HostTransport& host)
{
    Lock l (mtx);
    midiOutPtr = &midiOut;
    auto& g = proj.globals;
    for (auto& m : queuedUiMidi) { m.offset = 0; midiOut.push_back (m); }
    queuedUiMidi.clear();

    // keep fixed filters in sync with settings
    if (last34 != g.filter34Hz || last56 != g.filter56Hz)
    {
        last34 = g.filter34Hz; last56 = g.filter56Hz;
        for (int i = 0; i < 4; ++i) fixedLp[i].set (sr, i < 2 ? g.filter34Hz : g.filter56Hz);
    }

    // ---- host transport sync (plugin)
    if (g.syncMode == SyncMode::Host && host.valid)
    {
        hostTempo = host.bpm > 0 ? host.bpm : hostTempo;
        const auto hostTicks = (int64_t) std::floor (host.ppqPosition * kPPQ);
        if (host.playing && ! hostWasPlaying)
        {
            hostWasPlaying = true;
            startPlayback (std::max<int64_t> (0, hostTicks), 0, false);
            tickFrac = host.ppqPosition * kPPQ - (double) hostTicks;
        }
        else if (! host.playing && hostWasPlaying)
        {
            hostWasPlaying = false;
            stopPlayback (0);
        }
        else if (host.playing && playing && std::llabs (hostTicks - absTick) > 2)
        {
            locate (std::max<int64_t> (0, hostTicks));     // loop / jump in host timeline
        }
    }

    const double nativeInc = kNativeRate / sr;
    const float inGain = std::pow (10.0f, (float) g.inputGainDb / 20.0f);
    const float drive = std::max (0.1f, g.outputDrive);
    const double base12 = 200.0 * std::pow (13000.0 / 200.0, (double) g.filter12Cutoff);
    float peaks[kNumChannels] {};
    size_t mi = 0;

    for (int i = 0; i < n; ++i)
    {
        curOffset = i;
        while (mi < midiIn.size() && midiIn[mi].offset <= i)
            handleMidi (midiIn[mi++]);

        // ---- external sync from the audio input (click pulses or SMPTE/LTC)
        const float rawIn = 0.5f * ((inL ? inL[i] : 0.0f) + (inR ? inR[i] : 0.0f));
        if (g.syncMode == SyncMode::Click)
        {
            if (! clickHigh && rawIn > g.syncInputThreshold) { clickHigh = true; externalClockPulse (false); }
            else if (clickHigh && rawIn < g.syncInputThreshold * 0.5f) clickHigh = false;
            if (playing && lastClockSample >= 0 && sampleCounter + i - lastClockSample > (int64_t) sr)
            { stopPlayback (i); midiWaiting = true; }
        }
        else if (g.syncMode == SyncMode::Smpte)
        {
            ltcDecodeSample (rawIn);
            if (playing && ltcLastFrameSample >= 0 && sampleCounter + i - ltcLastFrameSample > (int64_t) (0.3 * sr))
            { stopPlayback (i); midiWaiting = true; }
        }

        // ---- sequencer clock
        if (playing && ! midiWaiting)
        {
            if (g.syncMode == SyncMode::Midi || g.syncMode == SyncMode::Click)
            {
                if (absTick < midiClockCount * (kPPQ / 24) + (kPPQ / 24) - 1)
                    tickFrac += midiTicksPerSample;
            }
            else
            {
                tickFrac += effectiveTempo() * kPPQ / (60.0 * sr);
            }
            while (tickFrac >= 1.0 && playing)
            {
                tickFrac -= 1.0;
                fireTick (i);
            }
        }

        // ---- input path (anti-alias filter, then sampled at 26.04 kHz)
        const float in = 0.5f * ((inL ? inL[i] : 0.0f) + (inR ? inR[i] : 0.0f)) * inGain;
        inputHeld = inputAA.process (in);

        nativePhase += nativeInc;
        while (nativePhase >= 1.0)
        {
            nativePhase -= 1.0;
            nativeStep();
        }

        // ---- analog stage (runs at host rate on the zero-order-held DAC outputs)
        float L = 0, R = 0;
        for (int c = 0; c < kNumChannels; ++c)
        {
            auto& v = ch[c];
            float x = v.dac * v.env * v.gain;
            if (c < 2)
            {
                double fc = base12;
                if (g.filterEnvelopes) fc = base12 * (0.12 + 0.88 * (double) v.env);
                x = ladder[c].process (x, fc, g.filter12Reso);
            }
            else if (c < 6)
            {
                x = fixedLp[c - 2].process (x);
            }
            x = std::tanh (x * drive) / drive;
            if (chOut && chOut[c]) chOut[c][i] = x;
            peaks[c] = std::max (peaks[c], std::fabs (x));
            const float p = clampv (g.pan[(size_t) c], -1.0f, 1.0f);
            const float a = (float) ((p + 1.0) * kPi * 0.25);
            L += x * std::cos (a) * 1.41421356f * 0.5f;
            R += x * std::sin (a) * 1.41421356f * 0.5f;
        }

        // ---- metronome click
        if (clickEnv > 1e-4f)
        {
            const float c = std::sin (clickPhase) * clickEnv * g.metronomeLevel * 0.5f;
            clickPhase += (float) (2.0 * kPi * clickFreq / sr);
            if (clickPhase > 2.0f * (float) kPi) clickPhase -= 2.0f * (float) kPi;
            clickEnv *= (float) std::exp (-1.0 / (0.012 * sr));
            L += c; R += c;
        }

        if (ltcWriteOn)
        {
            const float v = ltcGenerateSample() * 0.5f;   // SPECIAL 14: LTC printed to the main outs
            L += v; R += v;
        }

        if (mainL) mainL[i] = dcL.process (L) * g.masterLevel;
        if (mainR) mainR[i] = dcR.process (R) * g.masterLevel;
    }

    for (int c = 0; c < kNumChannels; ++c)
        meters[c].store (std::max (peaks[c], meters[c].load() * 0.85f));

    // ---- MIDI note-offs due in this block
    const int64_t blockEnd = sampleCounter + n;
    for (auto it = pendingOffs.begin(); it != pendingOffs.end();)
    {
        if (it->at < blockEnd)
        {
            midiOut.push_back (MidiMsg::make ((int) std::max<int64_t> (0, it->at - sampleCounter),
                                              (uint8_t) (0x80 | it->chan), it->note, 0));
            it = pendingOffs.erase (it);
        }
        else ++it;
    }
    std::stable_sort (midiOut.begin(), midiOut.end(), [] (const MidiMsg& a, const MidiMsg& b) { return a.offset < b.offset; });

    sampleCounter = blockEnd;
    midiOutPtr = nullptr;
}

void Engine::nativeStep()
{
    ++nativeClock;
    for (auto& v : ch)
    {
        if (! v.active) continue;
        const Sound& s = proj.sounds[(size_t) v.sound];
        const int start = s.startPos(), end = std::min (s.endPos(), (int) s.data.size());
        int idx = (int) v.pos;
        if (idx >= end)
        {
            if (s.loop && s.loopLength > 0 && s.loopLength <= end - start)
            {
                v.pos -= s.loopLength * std::ceil ((v.pos - end + 1) / s.loopLength);
                idx = (int) v.pos;
            }
            else { v.active = false; v.dac = 0; continue; }
        }
        if (idx < start || idx >= end) { v.active = false; v.dac = 0; continue; }
        const int readIdx = s.reversed ? (start + end - 1 - idx) : idx;
        // Drop-sample (zero-order) pitch shifting, as on the original: no interpolation.
        v.dac = (float) s.data[(size_t) readIdx] / 2048.0f;
        v.pos += v.inc;
        v.env *= v.envCoef;
        if (v.env < 1.0e-4f) { v.active = false; v.dac = 0; }
    }

    // ---- sampler ADC
    const float vIn = clampv (inputHeld, -1.0f, 1.0f);
    const auto q = (int16_t) clampv ((int) std::lround (vIn * 2047.0f), kSampleMin, kSampleMax);
    const float a = std::fabs (vIn);
    if (a > inputPeak.load()) inputPeak.store (a);
    else inputPeak.store (inputPeak.load() * 0.9995f);

    auto st = sampleState.load();
    if (st == SampleState::Armed && a >= proj.globals.sampleThreshold)
    {
        sampleState.store (SampleState::Recording);
        st = SampleState::Recording;
        setLcd ("Sampling...", fmt ("Sound %c%d", bankLetter (sampleTarget / 8), sampleTarget % 8 + 1));
    }
    if (st == SampleState::Recording)
    {
        if (sampleBuf.size() < sampleBuf.capacity()) sampleBuf.push_back (q);
        if (sampleBuf.size() >= sampleBuf.capacity()) finishSampling();
    }
}

// ===================================================================== voices
PadParams Engine::padParamsFor (int sound) const { return proj.working.pads[(size_t) sound]; }

void Engine::emitNote (int sound, int velocity, int offset)
{
    auto& g = proj.globals;
    if (! g.midiOutNotes || midiOutPtr == nullptr) return;
    const uint8_t note = g.noteMap[(size_t) sound];
    const auto chan = (uint8_t) g.midiChannel;
    midiOutPtr->push_back (MidiMsg::make (offset, (uint8_t) (0x90 | chan), note, (uint8_t) clampv (velocity, 1, 127)));
    pendingOffs.push_back ({ sampleCounter + offset + (int64_t) (sr * 0.05), note, chan });
}

void Engine::startVoice (int sound, int velocity, const PadParams* ov, int offset)
{
    if (sound < 0 || sound >= kNumSounds) return;
    emitNote (sound, velocity, offset);
    padFlash.fetch_or (1u << sound);

    const Sound& s = proj.sounds[(size_t) sound];
    if (s.empty()) return;

    const PadParams p = ov ? *ov : padParamsFor (sound);
    int c = proj.working.channel[(size_t) sound] % kNumChannels;

    // SPECIAL 22 dynamic allocation: a sound may use either channel of its pair
    if (proj.globals.dynamicAllocation[(size_t) (c / 2)])
    {
        const int a = (c / 2) * 2, b = a + 1;
        if (! ch[a].active) c = a;
        else if (! ch[b].active) c = b;
        else c = ch[a].startedAt <= ch[b].startedAt ? a : b;
    }

    auto& v = ch[c];           // retrigger on the same channel chokes the previous sound
    v.active = true;
    v.sound = sound;
    v.pos = s.startPos();
    v.inc = std::pow (2.0, clampv (p.tune, kTuneMin, kTuneMax) / 12.0);
    v.gain = p.level * ((float) clampv (velocity, 1, 127) / 127.0f);
    v.env = 1.0f;
    if (p.decay >= 0.999f) v.envCoef = 1.0f;
    else
    {
        const double t60 = 0.03 * std::pow (2.5 / 0.03, (double) clampv (p.decay, 0.0f, 1.0f));
        v.envCoef = (float) std::exp (std::log (0.001) / (t60 * kNativeRate));
    }
    v.startedAt = nativeClock;
}

void Engine::triggerSound (int sound, int velocity)
{
    Lock l (mtx);
    startVoice (sound, velocity, nullptr, 0);
}

void Engine::playPadLive (int pad, int velocity, int offset, bool fromMidi)
{
    auto& g = proj.globals;
    if (! fromMidi && ! g.dynamicButtons) velocity = 127;
    heldPads |= 1u << pad;
    heldVel[pad] = velocity;
    if (eraseHeld && playing) return;           // real-time erase, no sound
    if (tapRepeatHeld && playing) return;       // note repeat fires on the grid

    if (multiSource >= 0)
    {
        PadParams p = padParamsFor (multiSource);
        // multi values live in a per-pad table (see enterMultiMode)
        const float mv = multiTable[(size_t) pad];
        if (multiKind == SliderMode::MultiPitch) p.tune = mv;
        else if (multiKind == SliderMode::MultiLevel) p.level = mv;
        else p.decay = mv;
        startVoice (multiSource, velocity, &p, offset);
        recordNote (multiSource, velocity, &p);
        selectedSound = multiSource;
    }
    else
    {
        const int s = soundIndex (bank, pad);
        startVoice (s, velocity, nullptr, offset);
        recordNote (s, velocity, nullptr);
        selectedSound = s;
    }
}

void Engine::padPress (int pad, int velocity)
{
    Lock l (mtx);
    if (pad < 0 || pad >= kPadsPerBank) return;
    playPadLive (pad, velocity, 0, false);
    if (! playing) updateDefaultLcd();
}

void Engine::padRelease (int pad)
{
    Lock l (mtx);
    if (pad >= 0 && pad < kPadsPerBank) heldPads &= ~(1u << pad);
}

void Engine::setBank (int b)
{
    Lock l (mtx);
    bank = clampv (b, 0, kNumBanks - 1);
    updateDefaultLcd();
}

void Engine::selectSound (int s)
{
    Lock l (mtx);
    selectedSound = clampv (s, 0, kNumSounds - 1);
}

// ===================================================================== sliders / multi modes
void Engine::setSliderMode (SliderMode m)
{
    Lock l (mtx);
    if (m == SliderMode::MultiPitch || m == SliderMode::MultiLevel || m == SliderMode::MultiDecay) { enterMultiMode (m); return; }
    sliderMode = m;
    if (m == SliderMode::Tune) proj.globals.tuneMode = true;
    if (m == SliderMode::Decay) proj.globals.tuneMode = false;
}

void Engine::setSlider (int i, float v)
{
    Lock l (mtx);
    if (i < 0 || i >= kPadsPerBank) return;
    v = clampv (v, 0.0f, 1.0f);
    if (multiSource >= 0)
    {
        if (multiKind == SliderMode::MultiPitch) multiTable[(size_t) i] = kTuneMin + v * (kTuneMax - kTuneMin);
        else multiTable[(size_t) i] = v;
        return;
    }
    auto& p = proj.working.pads[(size_t) soundIndex (bank, i)];
    switch (sliderMode)
    {
        case SliderMode::Mix:   p.level = v; break;
        case SliderMode::Tune:  p.tune = kTuneMin + v * (kTuneMax - kTuneMin); break;
        case SliderMode::Decay: p.decay = v; break;
        case SliderMode::TuneDecay:
            if (proj.working.decaySelect[(size_t) soundIndex (bank, i)]) p.decay = v;
            else p.tune = kTuneMin + v * (kTuneMax - kTuneMin);
            break;
        default: break;
    }
}

float Engine::getSlider (int i)
{
    Lock l (mtx);
    if (i < 0 || i >= kPadsPerBank) return 0;
    if (multiSource >= 0)
    {
        const float mv = multiTable[(size_t) i];
        return multiKind == SliderMode::MultiPitch ? (mv - kTuneMin) / (kTuneMax - kTuneMin) : mv;
    }
    const auto& p = proj.working.pads[(size_t) soundIndex (bank, i)];
    switch (sliderMode)
    {
        case SliderMode::Tune:  return (p.tune - kTuneMin) / (kTuneMax - kTuneMin);
        case SliderMode::Decay: return p.decay;
        case SliderMode::TuneDecay:
            return proj.working.decaySelect[(size_t) soundIndex (bank, i)] ? p.decay : (p.tune - kTuneMin) / (kTuneMax - kTuneMin);
        default:                return p.level;
    }
}

void Engine::enterMultiMode (SliderMode m)
{
    Lock l (mtx);
    multiSource = selectedSound;
    multiKind = m;
    const PadParams base = padParamsFor (multiSource);
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        // Pads spread left to right (manual: multi-pitch leftmost highest, multi-level leftmost softest,
        // multi-decay leftmost longest).
        if (m == SliderMode::MultiPitch) multiTable[(size_t) i] = clampv (base.tune + 7.0f - 2.0f * (float) i, kTuneMin, kTuneMax);
        else if (m == SliderMode::MultiLevel) multiTable[(size_t) i] = (float) (i + 1) / 8.0f;
        else multiTable[(size_t) i] = 1.0f - (float) i / 8.0f;
    }
    const char* names[] = { "Multi-Pitch", "Multi-Level", "Multi-Decay" };
    const int k = m == SliderMode::MultiPitch ? 0 : m == SliderMode::MultiLevel ? 1 : 2;
    setLcd (names[k], fmt ("Sound %c%d", bankLetter (multiSource / 8), multiSource % 8 + 1));
}

void Engine::exitMultiMode()
{
    Lock l (mtx);
    multiSource = -1;
    updateDefaultLcd();
}

bool Engine::inMultiMode() const { return multiSource >= 0; }

// ===================================================================== recording
void Engine::recordNote (int sound, int velocity, const PadParams* ov)
{
    if (! recording || ! playing || songMode) return;
    auto& seg = proj.segments[(size_t) curSegment.load()];
    const int len = std::max (seg.lengthTicks(), 1);
    auto& g = proj.globals;
    if (g.authenticLimits && totalNotes() >= kAuthenticMaxNotes)
    {
        setLcd ("Sequence Memory", "Full");
        return;
    }

    double pos = (double) segTick - 1.0 + tickFrac;
    if (pos < 0) pos += len;
    const int grid = autoCorrectTicks (g.autoCorrect);
    int q = (int) std::lround (pos / grid) * grid;
    const int swing = kSwingValues[(size_t) clampv (g.swingIndex, 0, 5)];
    if (swing > 50 && (grid == kPPQ / 4 || grid == kPPQ / 2) && ((q / grid) & 1))
        q += (int) std::lround ((swing - 50) / 50.0 * grid);
    q = ((q % len) + len) % len;

    NoteEvent e;
    e.tick = q;
    e.sound = (uint8_t) sound;
    e.velocity = (uint8_t) clampv (velocity, 1, 127);
    if (ov) { e.hasOverride = true; e.override_ = *ov; }
    e.skipOnce = q >= segTick;        // already heard live; don't double-trigger this pass
    auto it = std::upper_bound (seg.events.begin(), seg.events.end(), e,
                                [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
    seg.events.insert (it, e);
}

int Engine::totalNotes()
{
    Lock l (mtx);
    int n = 0;
    for (auto& s : proj.segments) n += (int) s.events.size();
    return n;
}

// ===================================================================== sequencer core
void Engine::fireTick (int offset)
{
    auto& g = proj.globals;
    auto& seg = proj.segments[(size_t) curSegment.load()];
    const int len = std::max (seg.lengthTicks(), 1);
    const int t = segTick;

    // real-time erase: ERASE + pad held while playing
    if (eraseHeld && heldPads)
    {
        auto& ev = seg.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const NoteEvent& e) {
            if (e.tick != t) return false;
            for (int p = 0; p < kPadsPerBank; ++p)
                if ((heldPads >> p) & 1u)
                {
                    const int s = multiSource >= 0 ? multiSource : soundIndex (bank, p);
                    if (e.sound == s) return true;
                }
            return false;
        }), ev.end());
    }

    // play events on this tick
    auto lo = std::lower_bound (seg.events.begin(), seg.events.end(), t,
                                [] (const NoteEvent& e, int tick) { return e.tick < tick; });
    for (auto it = lo; it != seg.events.end() && it->tick == t; ++it)
    {
        if (it->skipOnce) { it->skipOnce = false; continue; }
        startVoice (it->sound, it->velocity, it->hasOverride ? &it->override_ : nullptr, offset);
    }

    // TAP/REPEAT note repeat at the auto-correct rate
    if (tapRepeatHeld && heldPads && ! eraseHeld)
    {
        const int grid = std::max (autoCorrectTicks (g.autoCorrect), kPPQ / 8);
        if (t % grid == 0)
        {
            for (int p = 0; p < kPadsPerBank; ++p)
            {
                if (! ((heldPads >> p) & 1u)) continue;
                const int s = multiSource >= 0 ? multiSource : soundIndex (bank, p);
                PadParams mp = padParamsFor (s);
                const PadParams* ov = nullptr;
                if (multiSource >= 0)
                {
                    const float mv = multiTable[(size_t) p];
                    if (multiKind == SliderMode::MultiPitch) mp.tune = mv;
                    else if (multiKind == SliderMode::MultiLevel) mp.level = mv;
                    else mp.decay = mv;
                    ov = &mp;
                }
                startVoice (s, heldVel[p], ov, offset);
                if (recording && ! songMode && ! (g.authenticLimits && totalNotes() >= kAuthenticMaxNotes))
                {
                    NoteEvent e; e.tick = t; e.sound = (uint8_t) s; e.velocity = (uint8_t) heldVel[p];
                    if (ov) { e.hasOverride = true; e.override_ = *ov; }
                    auto it = std::upper_bound (seg.events.begin(), seg.events.end(), e,
                                                [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
                    seg.events.insert (it, e);
                }
            }
        }
    }

    // metronome
    const int mt = metronomeTicks (g.metronome);
    if (mt > 0 && (recording || ! g.metronomeInRecordOnly) && t % mt == 0)
    {
        const bool accent = t % (seg.beatsPerBar * seg.ticksPerBeat()) == 0;
        clickEnv = g.metronome == Metronome::Click24 ? 0.25f : 1.0f;
        clickFreq = accent ? 2000.0f : 1000.0f;
        clickPhase = 0;
    }

    // MIDI clock out (24 ppq)
    if (g.midiOutClock && g.syncMode != SyncMode::Midi && midiOutPtr && absTick % (kPPQ / 24) == 0)
        midiOutPtr->push_back (MidiMsg::make (offset, 0xF8, 0, 0, 1));

    // song tempo-change ramps
    if (rampTicksLeft > 0)
    {
        tempoOffset += rampDeltaPerTick;
        if (--rampTicksLeft == 0)
        {
            g.tempo = clampv (g.tempo + tempoOffset, kMinTempo, kMaxTempo);
            tempoOffset = 0;
        }
    }

    ++segTick;
    ++absTick;
    if (segTick >= len)
    {
        segTick = 0;
        onSegmentEnd();
    }
}

void Engine::onSegmentEnd()
{
    if (! songMode) return;   // segment mode loops
    if (! advanceSong (false)) stopPlayback (curOffset);
}

// Executes song steps until the next segment step. Returns false when the song ends (stop).
bool Engine::advanceSong (bool dryRun)
{
    for (int guard = 0; guard < 4096; ++guard)
    {
        ++songStep;
        auto& song = proj.songs[(size_t) curSong.load()];
        if (songStep >= (int) song.steps.size())
        {
            if (! callStack.empty())
            {
                auto r = callStack.back(); callStack.pop_back();
                curSong = r.song; songStep = r.step; repeatStack = r.repeats;
                continue;
            }
            return false;
        }
        const auto st = song.steps[(size_t) songStep];
        switch (st.type)
        {
            case SongStep::Type::Segment:
                curSegment = clampv (st.a, 0, kNumSegments - 1);
                segTick = 0;
                if (proj.segments[(size_t) curSegment.load()].lengthTicks() <= 0) continue;
                return true;
            case SongStep::Type::RepeatBegin:
                repeatStack.push_back ({ songStep, std::max (1, st.a) - 1 });
                break;
            case SongStep::Type::RepeatEnd:
                if (! repeatStack.empty())
                {
                    auto& top = repeatStack.back();
                    if (top.remaining > 0) { --top.remaining; songStep = top.beginStep; }
                    else repeatStack.pop_back();
                }
                break;
            case SongStep::Type::MixChange:
                if (! dryRun) proj.working = proj.mixes[(size_t) clampv (st.a, 0, kNumMixes - 1)], proj.globals.currentMix = st.a;
                break;
            case SongStep::Type::TempoChange:
                if (! dryRun)
                {
                    rampTicksLeft = std::max (1, st.c) * kPPQ;
                    rampDeltaPerTick = (st.a == 2 ? -1.0 : 1.0) * st.b / rampTicksLeft;
                }
                break;
            case SongStep::Type::SubSong:
                if (callStack.size() < 8)
                {
                    callStack.push_back ({ curSong.load(), songStep, repeatStack });
                    curSong = clampv (st.a, 0, kNumSongs - 1);
                    songStep = -1;
                    repeatStack.clear();
                }
                break;
            case SongStep::Type::TriggerOut:
                if (! dryRun)
                {
                    triggerOuts.fetch_add (1);
                    clickEnv = 1.0f; clickFreq = 3000.0f; clickPhase = 0;
                    if (proj.globals.triggerOutMidi && midiOutPtr)
                    {
                        const auto chn = (uint8_t) proj.globals.midiChannel;
                        midiOutPtr->push_back (MidiMsg::make (curOffset, (uint8_t) (0x90 | chn), proj.globals.triggerNote, 127));
                        pendingOffs.push_back ({ sampleCounter + curOffset + (int64_t) (sr * 0.02), proj.globals.triggerNote, chn });
                    }
                }
                break;
            case SongStep::Type::End:
                if (! callStack.empty())
                {
                    auto r = callStack.back(); callStack.pop_back();
                    curSong = r.song; songStep = r.step; repeatStack = r.repeats;
                    break;
                }
                if (st.a == 1) { songStep = -1; repeatStack.clear(); break; }
                if (st.a == 2)
                {
                    curSong = clampv (st.b, 0, kNumSongs - 1); songStep = -1; repeatStack.clear();
                    if (! dryRun && proj.songs[(size_t) curSong.load()].tempo > 0) proj.globals.tempo = proj.songs[(size_t) curSong.load()].tempo;
                    break;
                }
                return false;
        }
    }
    return false;
}

void Engine::locate (int64_t target)
{
    target = std::max<int64_t> (0, target);
    absTick = target;
    tickFrac = 0;
    if (! songMode)
    {
        const int len = std::max (proj.segments[(size_t) curSegment.load()].lengthTicks(), 1);
        segTick = (int) (target % len);
        return;
    }
    songStep = proj.firstSongStep - 1;
    repeatStack.clear(); callStack.clear();
    if (! advanceSong (true)) { segTick = 0; return; }
    int64_t remaining = target;
    for (int guard = 0; guard < 100000; ++guard)
    {
        const int len = std::max (proj.segments[(size_t) curSegment.load()].lengthTicks(), 1);
        if (remaining < len) { segTick = (int) remaining; return; }
        remaining -= len;
        if (! advanceSong (true)) { segTick = 0; return; }
    }
}

void Engine::startPlayback (int64_t abs, int offset, bool sendStart)
{
    if (songMode && proj.songs[(size_t) curSong.load()].isEmpty())
    {
        setLcd ("Song is empty", "");
        return;
    }
    rampTicksLeft = 0; tempoOffset = 0;
    if (songMode && proj.songs[(size_t) curSong.load()].tempo > 0) proj.globals.tempo = proj.songs[(size_t) curSong.load()].tempo;
    if (songMode && abs == 0)
    {
        // starting from the top: run the opening steps for real (mix/tempo changes, triggers)
        absTick = 0; tickFrac = 0; segTick = 0;
        songStep = proj.firstSongStep - 1;
        repeatStack.clear(); callStack.clear();
        if (! advanceSong (false)) { setLcd ("Song is empty", ""); return; }
    }
    else locate (abs);
    for (auto& s : proj.segments) for (auto& e : s.events) e.skipOnce = false;
    playing = true;
    midiWaiting = false;
    if (sendStart && midiOutPtr && proj.globals.midiOutClock)
    {
        if (abs == 0) midiOutPtr->push_back (MidiMsg::make (offset, 0xFA, 0, 0, 1));
        else
        {
            const int spp = (int) (abs / (kPPQ / 4));
            midiOutPtr->push_back (MidiMsg::make (offset, 0xF2, (uint8_t) (spp & 0x7F), (uint8_t) ((spp >> 7) & 0x7F), 3));
            midiOutPtr->push_back (MidiMsg::make (offset, 0xFB, 0, 0, 1));
        }
    }
    fireTick (offset);
}

void Engine::stopPlayback (int offset)
{
    if (playing && midiOutPtr && proj.globals.midiOutClock && proj.globals.syncMode != SyncMode::Midi)
        midiOutPtr->push_back (MidiMsg::make (offset, 0xFC, 0, 0, 1));
    playing = false;
    midiWaiting = false;
    rampTicksLeft = 0;
    if (tempoOffset != 0) { proj.globals.tempo = clampv (proj.globals.tempo + tempoOffset, kMinTempo, kMaxTempo); tempoOffset = 0; }
    updateDefaultLcd();
}

// Pending transport requests from the UI thread are applied immediately under the lock.
void Engine::runStop()
{
    Lock l (mtx);
    if (playing || midiWaiting) { stop(); return; }
    play (true);
}

void Engine::play (bool fromStart)
{
    Lock l (mtx);
    const auto sm = proj.globals.syncMode;
    if (sm == SyncMode::Midi || sm == SyncMode::Click || sm == SyncMode::Smpte)
    {
        midiWaiting = true;      // "ready" mode, sync light flashes until the master starts
        setLcd ("Waiting for", sm == SyncMode::Midi ? "MIDI Start" : sm == SyncMode::Click ? "Click" : "SMPTE");
        return;
    }
    std::vector<MidiMsg> tmp;
    auto* prev = midiOutPtr;
    if (! midiOutPtr) midiOutPtr = &tmp;    // UI-thread start: clock start goes out with next block
    startPlayback (fromStart ? 0 : absTick, 0, true);
    if (prev == nullptr) { queuedUiMidi.insert (queuedUiMidi.end(), tmp.begin(), tmp.end()); midiOutPtr = nullptr; }
}

void Engine::stop()
{
    Lock l (mtx);
    std::vector<MidiMsg> tmp;
    auto* prev = midiOutPtr;
    if (! midiOutPtr) midiOutPtr = &tmp;
    stopPlayback (0);
    if (prev == nullptr) { queuedUiMidi.insert (queuedUiMidi.end(), tmp.begin(), tmp.end()); midiOutPtr = nullptr; }
}

void Engine::setRecord (bool on)
{
    Lock l (mtx);
    if (on && songMode) { setLcd ("Record only in", "Segment mode"); return; }
    recording = on;
    setLcd (on ? "Record" : "Record off", fmt ("Segment %02d", curSegment.load()));
}

void Engine::setSongMode (bool on)
{
    Lock l (mtx);
    if (playing) stop();
    songMode = on;
    if (on) recording = false;
    updateDefaultLcd();
}

void Engine::setSegment (int s)
{
    Lock l (mtx);
    curSegment = clampv (s, 0, kNumSegments - 1);
    if (playing && ! songMode)
    {
        const int len = std::max (proj.segments[(size_t) curSegment.load()].lengthTicks(), 1);
        segTick %= len;
    }
    updateDefaultLcd();
}

void Engine::setSong (int s)
{
    Lock l (mtx);
    curSong = clampv (s, 0, kNumSongs - 1);
    updateDefaultLcd();
}

void Engine::setTempo (double bpm)
{
    Lock l (mtx);
    proj.globals.tempo = std::round (clampv (bpm, kMinTempo, kMaxTempo) * 10.0) / 10.0;
    updateDefaultLcd();
}

double Engine::getTempo() { Lock l (mtx); return effectiveTempo(); }

void Engine::tapTempo()
{
    Lock l (mtx);
    const auto now = std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (! tapTimes.empty() && now - tapTimes.back() > 2000000) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() >= 2)
    {
        const double avg = (double) (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1) / 1.0e6;
        setTempo (60.0 / avg);
    }
}

void Engine::setSwing (int i)       { Lock l (mtx); proj.globals.swingIndex = clampv (i, 0, 5); setLcd ("Swing", fmt ("%d%%", kSwingValues[(size_t) proj.globals.swingIndex])); }
void Engine::setAutoCorrect (AutoCorrect q) { Lock l (mtx); proj.globals.autoCorrect = q; setLcd ("Auto Correct", autoCorrectName (q)); }
void Engine::setMetronome (Metronome m) { Lock l (mtx); proj.globals.metronome = m; }
void Engine::setTapRepeat (bool h)  { Lock l (mtx); tapRepeatHeld = h; }
void Engine::setEraseHeld (bool h)  { Lock l (mtx); eraseHeld = h; }

int Engine::currentBeatInfo (int& bar, int& beat)
{
    Lock l (mtx);
    const auto& seg = proj.segments[(size_t) curSegment.load()];
    const int tpb = seg.ticksPerBeat();
    const int t = std::max (0, segTick - 1);
    bar = t / (tpb * std::max (1, seg.beatsPerBar)) + 1;
    beat = (t / tpb) % std::max (1, seg.beatsPerBar) + 1;
    return t;
}

// ===================================================================== MIDI in
void Engine::handleMidi (const MidiMsg& m)
{
    auto& g = proj.globals;
    const uint8_t st = m.bytes[0];
    const int64_t now = sampleCounter + curOffset;

    if (st >= 0xF0)
    {
        if (g.syncMode != SyncMode::Midi) return;
        switch (st)
        {
            case 0xF8:
                ++midiClockCount;
                if (lastClockSample >= 0 && now > lastClockSample)
                    midiTicksPerSample = (double) (kPPQ / 24) / (double) (now - lastClockSample);
                lastClockSample = now;
                break;
            case 0xFA: midiClockCount = 0; startPlayback (0, curOffset, false); break;
            case 0xFB: startPlayback (midiClockCount * (kPPQ / 24), curOffset, false); break;
            case 0xFC: stopPlayback (curOffset); break;
            case 0xF2:
            {
                const int spp = m.bytes[1] | (m.bytes[2] << 7);   // 16th notes
                midiClockCount = (int64_t) spp * 6;
                locate ((int64_t) spp * (kPPQ / 4));
                break;
            }
            default: break;
        }
        return;
    }

    const int type = st & 0xF0, chan = st & 0x0F;
    if (g.midiMode == MidiMode::Poly && chan != g.midiChannel) return;

    if (type == 0x90 && m.bytes[2] > 0)
    {
        const int note = m.bytes[1], vel = m.bytes[2];
        // Pitch keys: G#3..B4 play the selected sound at 16 pitches.
        if (g.pitchKeys && note >= 56 && note <= 71)
        {
            const int s = selectedSound;
            PadParams p = padParamsFor (s);
            p.tune = kTuneMin + (float) (note - 56);
            startVoice (s, vel, &p, curOffset);
            recordNote (s, vel, &p);
            return;
        }
        for (int s = 0; s < kNumSounds; ++s)
        {
            if (g.noteMap[(size_t) s] == note)
            {
                startVoice (s, vel, nullptr, curOffset);
                recordNote (s, vel, nullptr);
                break;
            }
        }
    }
}

// ===================================================================== segment editing
void Engine::eraseSegment (int seg)
{
    Lock l (mtx);
    auto& s = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)];
    s = Segment {};
}

void Engine::eraseSoundFromSegment (int seg, int sound)
{
    Lock l (mtx);
    auto& ev = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)].events;
    ev.erase (std::remove_if (ev.begin(), ev.end(), [sound] (const NoteEvent& e) { return e.sound == sound; }), ev.end());
}

bool Engine::copySegment (int src, int dst, int appendTimes)
{
    Lock l (mtx);
    src = clampv (src, 0, kNumSegments - 1);
    dst = clampv (dst, 0, kNumSegments - 1);
    const Segment s = proj.segments[(size_t) src];
    if (src != dst)
    {
        if (proj.globals.authenticLimits && totalNotes() - (int) proj.segments[(size_t) dst].events.size() + (int) s.events.size() > kAuthenticMaxNotes)
            return false;
        proj.segments[(size_t) dst] = s;
        return true;
    }
    // append mode: repeat the segment onto itself appendTimes more times
    const int times = clampv (appendTimes, 1, 99);
    if (proj.globals.authenticLimits && totalNotes() + (int) s.events.size() * times > kAuthenticMaxNotes) return false;
    auto& d = proj.segments[(size_t) dst];
    const int len = s.lengthTicks();
    const int totalBeats = s.bars * s.beatsPerBar + s.extraBeats;
    for (int r = 1; r <= times; ++r)
        for (auto e : s.events) { e.tick += len * r; d.events.push_back (e); }
    const int newBeats = totalBeats * (times + 1);
    d.bars = std::min (99, newBeats / std::max (1, d.beatsPerBar));
    d.extraBeats = newBeats - d.bars * d.beatsPerBar;
    std::stable_sort (d.events.begin(), d.events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
    return true;
}

void Engine::setSegmentLength (int seg, int bars, int beats, bool truncate)
{
    Lock l (mtx);
    auto& s = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)];
    s.bars = clampv (bars, 0, 99);
    s.extraBeats = clampv (beats, 0, 99);
    if (s.lengthTicks() <= 0) s.bars = 1;
    if (truncate)
    {
        const int len = s.lengthTicks();
        s.events.erase (std::remove_if (s.events.begin(), s.events.end(), [len] (const NoteEvent& e) { return e.tick >= len; }), s.events.end());
    }
}

void Engine::setTimeSignature (int seg, int beatsPerBar, int beatUnit)
{
    Lock l (mtx);
    auto& s = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)];
    s.beatsPerBar = clampv (beatsPerBar, 1, 99);
    if (beatUnit == 4 || beatUnit == 8 || beatUnit == 16 || beatUnit == 32) s.beatUnit = beatUnit;
}

bool Engine::stepInsert (int seg, int tick, int sound, int velocity)
{
    Lock l (mtx);
    if (proj.globals.authenticLimits && totalNotes() >= kAuthenticMaxNotes) return false;
    auto& s = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)];
    NoteEvent e; e.tick = clampv (tick, 0, std::max (0, s.lengthTicks() - 1));
    e.sound = (uint8_t) clampv (sound, 0, kNumSounds - 1);
    e.velocity = (uint8_t) clampv (velocity, 1, 127);
    auto it = std::upper_bound (s.events.begin(), s.events.end(), e, [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
    s.events.insert (it, e);
    return true;
}

void Engine::stepErase (int seg, int tick, int sound)
{
    Lock l (mtx);
    auto& ev = proj.segments[(size_t) clampv (seg, 0, kNumSegments - 1)].events;
    ev.erase (std::remove_if (ev.begin(), ev.end(), [=] (const NoteEvent& e) { return e.tick == tick && (sound < 0 || e.sound == sound); }), ev.end());
}

// ===================================================================== song editing
void Engine::songInsertStep (int song, int index, SongStep step)
{
    Lock l (mtx);
    auto& s = proj.songs[(size_t) clampv (song, 0, kNumSongs - 1)].steps;
    index = clampv (index, 0, (int) s.size());
    s.insert (s.begin() + index, step);
}

void Engine::songDeleteStep (int song, int index)
{
    Lock l (mtx);
    auto& s = proj.songs[(size_t) clampv (song, 0, kNumSongs - 1)].steps;
    if (index >= 0 && index < (int) s.size()) s.erase (s.begin() + index);
}

void Engine::songClear (int song) { Lock l (mtx); proj.songs[(size_t) clampv (song, 0, kNumSongs - 1)].steps.clear(); }
void Engine::setFirstSongStep (int step) { Lock l (mtx); proj.firstSongStep = std::max (0, step); }

// ===================================================================== SET-UP / SPECIAL
void Engine::setDynamicButtons (bool on) { Lock l (mtx); proj.globals.dynamicButtons = on; setLcd ("Dynamic Buttons", on ? "Yes" : "No"); }

void Engine::defineMix (int m)
{
    Lock l (mtx);
    m = clampv (m, 0, kNumMixes - 1);
    proj.mixes[(size_t) m] = proj.working;
    proj.globals.currentMix = m;
    setLcd ("Mix Defined", fmt ("Mix %d", m + 1));
}

void Engine::selectMix (int m)
{
    Lock l (mtx);
    m = clampv (m, 0, kNumMixes - 1);
    proj.working = proj.mixes[(size_t) m];
    proj.globals.currentMix = m;
    setLcd ("Mix Selected", fmt ("Mix %d", m + 1));
}

void Engine::channelAssign (int s, int c)
{
    Lock l (mtx);
    proj.working.channel[(size_t) clampv (s, 0, kNumSounds - 1)] = (uint8_t) clampv (c, 0, kNumChannels - 1);
}

void Engine::setTuneDecayMode (bool tune) { setSliderMode (tune ? SliderMode::Tune : SliderMode::Decay); }

void Engine::setDecaySelect (int sound, bool decay)
{
    Lock l (mtx);
    sound = clampv (sound, 0, kNumSounds - 1);
    proj.working.decaySelect[(size_t) sound] = decay ? 1 : 0;
    sliderMode = SliderMode::TuneDecay;
    setLcd (fmt ("Sound %c%d", bankLetter (sound / 8), sound % 8 + 1), decay ? "Slider = Decay" : "Slider = Tune");
}

void Engine::setSongTempo (int song, double bpm)
{
    Lock l (mtx);
    proj.songs[(size_t) clampv (song, 0, kNumSongs - 1)].tempo = bpm <= 0 ? 0.0 : clampv (bpm, kMinTempo, kMaxTempo);
}

void Engine::setSmpteStart (int64_t frames) { Lock l (mtx); proj.globals.smpteStartFrames = std::max<int64_t> (0, frames); }

void Engine::setTruncation (int s, int start, int end)
{
    Lock l (mtx);
    auto& snd = proj.sounds[(size_t) clampv (s, 0, kNumSounds - 1)];
    const int size = (int) snd.data.size();
    snd.truncStart = clampv (start, 0, std::max (0, size - 1));
    snd.truncEnd = end <= 0 ? 0 : clampv (end, snd.truncStart + 1, size);
    // truncation cannot be shorter than the loop
    if (snd.loop) snd.loopLength = std::min (snd.loopLength, snd.endPos() - snd.startPos());
}

void Engine::setLoop (int s, bool on, int loopLength)
{
    Lock l (mtx);
    auto& snd = proj.sounds[(size_t) clampv (s, 0, kNumSounds - 1)];
    snd.loop = on;
    snd.loopLength = clampv (loopLength, 0, snd.endPos() - snd.startPos());
}

void Engine::makeTruncationPermanent (int s)
{
    Lock l (mtx);
    auto& snd = proj.sounds[(size_t) clampv (s, 0, kNumSounds - 1)];
    if (snd.empty()) return;
    std::vector<int16_t> d (snd.data.begin() + snd.startPos(), snd.data.begin() + snd.endPos());
    snd.data = std::move (d);
    snd.truncStart = 0; snd.truncEnd = 0;
}

void Engine::deleteSound (int s)
{
    Lock l (mtx);
    s = clampv (s, 0, kNumSounds - 1);
    for (auto& c : ch) if (c.sound == s) { c.active = false; c.dac = 0; }
    proj.sounds[(size_t) s] = Sound {};
}

void Engine::setMidiChannel (int c) { Lock l (mtx); proj.globals.midiChannel = clampv (c, 0, 15); }
void Engine::setMidiMode (MidiMode m) { Lock l (mtx); proj.globals.midiMode = m; }
void Engine::setPitchKeys (bool on) { Lock l (mtx); proj.globals.pitchKeys = on; }

void Engine::clearAllMemory()
{
    Lock l (mtx);
    stopPlayback (0);
    for (auto& c : ch) { c.active = false; c.dac = 0; }
    const Globals keep = proj.globals;
    proj = Project {};
    proj.globals = keep;
    setLcd ("Memory Cleared", "");
}

Engine::MemoryInfo Engine::memoryInfo()
{
    Lock l (mtx);
    return { memoryUsed(), kAuthenticMemory, totalNotes(), kAuthenticMaxNotes };
}

int Engine::memoryUsed() const
{
    int n = 0;
    for (auto& s : proj.sounds) n += s.memoryUsed();
    return n;
}

void Engine::clearSounds()
{
    Lock l (mtx);
    for (auto& c : ch) { c.active = false; c.dac = 0; }
    for (auto& s : proj.sounds) s = Sound {};
}

void Engine::clearSequences()
{
    Lock l (mtx);
    stopPlayback (0);
    for (auto& s : proj.segments) s = Segment {};
    for (auto& s : proj.songs) s = Song {};
}

void Engine::copySound (int src, int dst)
{
    Lock l (mtx);
    src = clampv (src, 0, kNumSounds - 1); dst = clampv (dst, 0, kNumSounds - 1);
    if (src == dst) return;
    if (proj.globals.authenticLimits && memoryUsed() - proj.sounds[(size_t) dst].memoryUsed() + proj.sounds[(size_t) src].memoryUsed() > kAuthenticMemory)
    {
        setLcd ("Sample Memory", "Full");
        return;
    }
    for (auto& c : ch) if (c.sound == dst) { c.active = false; c.dac = 0; }
    proj.sounds[(size_t) dst] = proj.sounds[(size_t) src];
    proj.working.pads[(size_t) dst] = proj.working.pads[(size_t) src];
}

void Engine::swapSounds (int a, int b)
{
    Lock l (mtx);
    a = clampv (a, 0, kNumSounds - 1); b = clampv (b, 0, kNumSounds - 1);
    for (auto& c : ch) if (c.sound == a || c.sound == b) { c.active = false; c.dac = 0; }
    std::swap (proj.sounds[(size_t) a], proj.sounds[(size_t) b]);
    std::swap (proj.working.pads[(size_t) a], proj.working.pads[(size_t) b]);
}

void Engine::setDefaultDecayAll (float d)
{
    Lock l (mtx);
    for (auto& p : proj.working.pads) p.decay = clampv (d, 0.0f, 1.0f);
}

void Engine::reverseSound (int s)
{
    Lock l (mtx);
    auto& snd = proj.sounds[(size_t) clampv (s, 0, kNumSounds - 1)];
    snd.reversed = ! snd.reversed;
}

void Engine::nameSound (int s, const std::string& name)
{
    Lock l (mtx);
    proj.sounds[(size_t) clampv (s, 0, kNumSounds - 1)].name = name.substr (0, 8);
}

void Engine::setDynamicAllocation (int pair, bool on) { Lock l (mtx); proj.globals.dynamicAllocation[(size_t) clampv (pair, 0, 3)] = on; }
void Engine::setSyncMode (SyncMode m) { Lock l (mtx); if (playing) stop(); proj.globals.syncMode = m; hostWasPlaying = false; midiWaiting = false; }
void Engine::setSmpteRate (SmpteRate r) { Lock l (mtx); proj.globals.smpteRate = r; }
void Engine::setFilterEnvelopes (bool on) { Lock l (mtx); proj.globals.filterEnvelopes = on; setLcd (on ? "Filter Envs On" : "Filter Envs Off", ""); }

// ===================================================================== sampling
void Engine::setInputGainDb (int db) { Lock l (mtx); proj.globals.inputGainDb = db >= 40 ? 40 : db >= 20 ? 20 : 0; }
void Engine::setSampleThreshold (float t) { Lock l (mtx); proj.globals.sampleThreshold = clampv (t, 0.0f, 1.0f); }
void Engine::setSampleLength (float s)
{
    Lock l (mtx);
    const float maxLen = proj.globals.authenticLimits ? 2.5f : 60.0f;
    proj.globals.sampleLengthSec = std::round (clampv (s, 0.1f, maxLen) * 10.0f) / 10.0f;
}

void Engine::armSampling (int sound)
{
    Lock l (mtx);
    sampleTarget = clampv (sound, 0, kNumSounds - 1);
    int len = (int) (proj.globals.sampleLengthSec * kNativeRate);
    if (proj.globals.authenticLimits)
    {
        len = std::min (len, kAuthenticMaxSound);
        const int avail = kAuthenticMemory - memoryUsed() + proj.sounds[(size_t) sampleTarget].memoryUsed();
        len = std::min (len, avail);
        if (len < 2600) { setLcd ("Sample Memory", "Full"); return; }
    }
    sampleBuf.clear();
    sampleBuf.reserve ((size_t) len);
    sampleState = SampleState::Armed;
    setLcd ("Sample Armed", fmt ("Sound %c%d", bankLetter (sampleTarget / 8), sampleTarget % 8 + 1));
}

void Engine::forceSampling (int sound)
{
    Lock l (mtx);
    armSampling (sound);
    if (sampleState == SampleState::Armed) sampleState = SampleState::Recording;
}

void Engine::cancelSampling() { Lock l (mtx); sampleState = SampleState::Idle; sampleBuf.clear(); }

void Engine::finishSampling()
{
    for (auto& c : ch) if (c.sound == sampleTarget) { c.active = false; c.dac = 0; }
    auto& s = proj.sounds[(size_t) sampleTarget];
    std::swap (s.data, sampleBuf);
    sampleBuf.clear();
    s.truncStart = 0; s.truncEnd = 0; s.loop = false; s.loopLength = 0; s.reversed = false;
    s.name = fmt ("SMP %c%d", bankLetter (sampleTarget / 8), sampleTarget % 8 + 1);
    sampleState = SampleState::Idle;
    setLcd ("Sample Done", fmt ("%.2f sec", (double) s.data.size() / kNativeRate));
}

bool Engine::loadSound (int sound, const float* mono, int n, double srcRate, const std::string& name)
{
    if (mono == nullptr || n <= 0 || srcRate <= 0) return false;
    // Prepare outside the lock: anti-alias filter + linear resample to 26.04 kHz + 12-bit quantise.
    std::vector<float> filtered ((size_t) n);
    Butter4 aa;
    aa.set (srcRate, std::min (12500.0, srcRate * 0.45));
    for (int i = 0; i < n; ++i) filtered[(size_t) i] = aa.process (mono[i]);
    const double step = srcRate / kNativeRate;
    const int outLen = (int) ((double) (n - 1) / step) + 1;
    std::vector<int16_t> data;
    data.reserve ((size_t) outLen);
    for (int i = 0; i < outLen; ++i)
    {
        const double pos = i * step;
        const int i0 = (int) pos;
        const int i1 = std::min (i0 + 1, n - 1);
        const float fr = (float) (pos - i0);
        const float v = filtered[(size_t) i0] + (filtered[(size_t) i1] - filtered[(size_t) i0]) * fr;
        data.push_back ((int16_t) clampv ((int) std::lround (clampv (v, -1.0f, 1.0f) * 2047.0f), kSampleMin, kSampleMax));
    }

    Lock l (mtx);
    sound = clampv (sound, 0, kNumSounds - 1);
    if (proj.globals.authenticLimits)
    {
        const int avail = kAuthenticMemory - memoryUsed() + proj.sounds[(size_t) sound].memoryUsed();
        const int maxLen = std::min (kAuthenticMaxSound, avail);
        if (maxLen <= 0) { setLcd ("Sample Memory", "Full"); return false; }
        if ((int) data.size() > maxLen) data.resize ((size_t) maxLen);
    }
    for (auto& c : ch) if (c.sound == sound) { c.active = false; c.dac = 0; }
    auto& s = proj.sounds[(size_t) sound];
    s = Sound {};
    s.data = std::move (data);
    s.name = name.substr (0, 8);
    setLcd ("Sound Loaded", fmt ("%c%d %s", bankLetter (sound / 8), sound % 8 + 1, s.name.c_str()));
    return true;
}

// ===================================================================== disk
std::vector<uint8_t> Engine::saveDisk (int sections)
{
    Lock l (mtx);
    return serializeProject (proj, sections);
}

bool Engine::loadDisk (const uint8_t* data, size_t size, int sections)
{
    Project tmp;
    int found = 0;
    if (! deserializeProject (tmp, data, size, &found)) return false;
    Lock l (mtx);
    stopPlayback (0);
    for (auto& c : ch) { c.active = false; c.dac = 0; }
    if ((sections & kDiskSounds) && (found & kDiskSounds)) proj.sounds = tmp.sounds;
    if ((sections & kDiskSequences) && (found & kDiskSequences))
    {
        proj.segments = tmp.segments; proj.songs = tmp.songs; proj.mixes = tmp.mixes;
        proj.working = tmp.working; proj.firstSongStep = tmp.firstSongStep;
    }
    if ((sections & kDiskGlobals) && (found & kDiskGlobals)) proj.globals = tmp.globals;
    updateDefaultLcd();
    return true;
}

bool Engine::loadSegmentFromDisk (const uint8_t* data, size_t size, int src, int dst)
{
    Project tmp;
    int found = 0;
    if (! deserializeProject (tmp, data, size, &found) || ! (found & kDiskSequences)) return false;
    Lock l (mtx);
    proj.segments[(size_t) clampv (dst, 0, kNumSegments - 1)] = tmp.segments[(size_t) clampv (src, 0, kNumSegments - 1)];
    return true;
}

bool Engine::loadSoundFromDisk (const uint8_t* data, size_t size, int src, int dst)
{
    Project tmp;
    int found = 0;
    if (! deserializeProject (tmp, data, size, &found) || ! (found & kDiskSounds)) return false;
    Lock l (mtx);
    dst = clampv (dst, 0, kNumSounds - 1);
    for (auto& c : ch) if (c.sound == dst) { c.active = false; c.dac = 0; }
    proj.sounds[(size_t) dst] = tmp.sounds[(size_t) clampv (src, 0, kNumSounds - 1)];
    return true;
}

// ===================================================================== LCD
void Engine::getLcd (std::string& a, std::string& b) { Lock l (mtx); a = lcd1; b = lcd2; }
void Engine::setLcd (const std::string& a, const std::string& b) { lcd1 = a.substr (0, 16); lcd2 = b.substr (0, 16); }

void Engine::updateDefaultLcd()
{
    const double t = proj.globals.tempo;
    if (songMode) setLcd (fmt ("Song %02d", curSong.load()), fmt ("Tempo %5.1f", t));
    else setLcd (fmt ("Segment %02d", curSegment.load()), fmt ("Bank %c  %5.1f", bankLetter (bank), t));
}

std::vector<MidiMsg> Engine::takeQueuedUiMidi()
{
    std::vector<MidiMsg> out;
    std::swap (out, queuedUiMidi);
    return out;
}
} // namespace c12
