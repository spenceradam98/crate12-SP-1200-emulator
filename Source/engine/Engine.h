// Crate12 engine: sampler voices, analog stage, sequencer, song mode, sync and MIDI.
#pragma once

#include "Filters.h"
#include "Types.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace c12
{
// Disk sections (DISK module equivalents)
enum DiskSection : int { kDiskSounds = 1, kDiskSequences = 2, kDiskGlobals = 4, kDiskAll = 7 };

std::vector<uint8_t> serializeProject (const Project& p, int sections = kDiskAll);
bool deserializeProject (Project& out, const uint8_t* data, size_t size, int* sectionsFound = nullptr);

class Engine
{
public:
    Engine();

    // ------------------------------------------------------------ audio
    void prepare (double sampleRate, int maxBlockSize);
    // chOut: 8 pointers (any may be null) for the individual channel outputs.
    void process (const float* inL, const float* inR, float* mainL, float* mainR, float* const* chOut,
                  int numSamples, const std::vector<MidiMsg>& midiIn, std::vector<MidiMsg>& midiOut,
                  const HostTransport& host);

    // All public calls lock internally; safe from the message thread.
    std::recursive_mutex& mutex() { return mtx; }
    Project& project() { return proj; }              // hold mutex() while touching directly

    // ------------------------------------------------------------ performance
    void padPress (int pad, int velocity);            // pad 0..7 of current bank
    void padRelease (int pad);
    void triggerSound (int sound, int velocity);       // direct trigger (no recording)
    void setBank (int bank);
    int  getBank() const { return bank.load(); }
    void selectSound (int sound);
    int  getSelectedSound() const { return selectedSound.load(); }

    void  setSliderMode (SliderMode m);
    SliderMode getSliderMode() const { return sliderMode; }
    void  setSlider (int index, float value01);       // 8 sliders, acting on current bank
    float getSlider (int index);

    // SET-UP 11/12/13 + multi-decay: spread the selected sound across the 8 pads
    void enterMultiMode (SliderMode multi);
    void exitMultiMode();
    bool inMultiMode() const;

    // ------------------------------------------------------------ transport / performance
    void runStop();
    void play (bool fromStart = true);
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setRecord (bool on);
    bool isRecording() const { return recording.load(); }
    void setSongMode (bool on);
    bool isSongMode() const { return songMode.load(); }
    void setSegment (int seg);
    int  getSegment() const { return curSegment.load(); }
    void setSong (int song);
    int  getSong() const { return curSong.load(); }
    void setTempo (double bpm);
    double getTempo();
    void tapTempo();
    void setSwing (int index);
    void setAutoCorrect (AutoCorrect q);
    void setMetronome (Metronome m);
    void setTapRepeat (bool held);                     // TAP/REPEAT held = note repeat at auto-correct rate
    void setEraseHeld (bool held);                     // ERASE held + pad while playing deletes that sound

    // ------------------------------------------------------------ segment editing
    void eraseSegment (int seg);
    void eraseSoundFromSegment (int seg, int sound);
    bool copySegment (int src, int dst, int appendTimes = 0); // src==dst with appendTimes = append mode
    void setSegmentLength (int seg, int bars, int beats, bool truncate);
    void setTimeSignature (int seg, int beatsPerBar, int beatUnit);
    bool stepInsert (int seg, int tick, int sound, int velocity);
    void stepErase (int seg, int tick, int sound);    // sound < 0 = all at tick
    int  totalNotes();

    // ------------------------------------------------------------ song editing
    void songInsertStep (int song, int index, SongStep step);
    void songDeleteStep (int song, int index);
    void songClear (int song);
    void setFirstSongStep (int step);

    // ------------------------------------------------------------ SET-UP / SPECIAL
    void setDynamicButtons (bool on);
    void defineMix (int mix);                          // SET-UP 15
    void selectMix (int mix);                          // SET-UP 16
    void channelAssign (int sound, int channel);       // SET-UP 17
    void setTuneDecayMode (bool tune);                 // SET-UP 18
    void setTruncation (int sound, int start, int end);// SET-UP 19
    void setLoop (int sound, bool on, int loopLength);
    void makeTruncationPermanent (int sound);
    void deleteSound (int sound);                      // SET-UP 20
    void setMidiChannel (int ch);                      // SET-UP 22
    void setMidiMode (MidiMode m);
    void setPitchKeys (bool on);
    void clearAllMemory();                             // SPECIAL 12
    struct MemoryInfo { int samplesUsed, samplesTotal, notesUsed, notesTotal; };
    MemoryInfo memoryInfo();                           // SPECIAL 13
    void clearSounds();                                // SPECIAL 15
    void clearSequences();                             // SPECIAL 16
    void copySound (int src, int dst);                 // SPECIAL 17
    void swapSounds (int a, int b);                    // SPECIAL 18
    void setDefaultDecayAll (float decay);             // SPECIAL 19
    void reverseSound (int sound);
    void nameSound (int sound, const std::string& name);
    void setDynamicAllocation (int pair, bool on);     // SPECIAL 22
    void setSyncMode (SyncMode m);
    void setSmpteRate (SmpteRate r);
    void setFilterEnvelopes (bool on);

    // ------------------------------------------------------------ sampling
    enum class SampleState { Idle, Armed, Recording };
    void setInputGainDb (int db);
    void setSampleThreshold (float t);
    void setSampleLength (float seconds);
    void armSampling (int sound);
    void forceSampling (int sound);
    void cancelSampling();
    SampleState getSampleState() const { return sampleState.load(); }
    float getInputLevel() const { return inputPeak.load(); }
    // Import external audio: resampled to 26.04 kHz through the input filter and quantised to 12 bits.
    bool loadSound (int sound, const float* mono, int numSamples, double sourceRate, const std::string& name);

    // ------------------------------------------------------------ disk (DISK 0-8 equivalents)
    std::vector<uint8_t> saveDisk (int sections);
    bool loadDisk (const uint8_t* data, size_t size, int sections);
    bool loadSegmentFromDisk (const uint8_t* data, size_t size, int srcSeg, int dstSeg);
    bool loadSoundFromDisk (const uint8_t* data, size_t size, int srcSound, int dstSound);

    // ------------------------------------------------------------ status for UI
    void getLcd (std::string& line1, std::string& line2);
    void setLcd (const std::string& l1, const std::string& l2);
    float channelMeter (int ch) const { return meters[(size_t) ch].load(); }
    int   currentBeatInfo (int& bar, int& beat);
    uint32_t padFlashMask() { return padFlash.exchange (0); }
    int   triggerOutCount() const { return triggerOuts.load(); }
    std::vector<MidiMsg> takeQueuedUiMidi();

private:
    struct Channel
    {
        bool   active = false;
        int    sound = -1;
        double pos = 0, inc = 1;
        float  gain = 0, env = 0, envCoef = 1;
        float  dac = 0;               // held 12-bit DAC value, normalised
        uint64_t startedAt = 0;
    };

    // internals (mutex held)
    void startVoice (int sound, int velocity, const PadParams* override_, int sampleOffset);
    void playPadLive (int pad, int velocity, int sampleOffset, bool fromMidi);
    PadParams padParamsFor (int sound) const;
    void recordNote (int sound, int velocity, const PadParams* override_);
    void nativeStep();
    void fireTick (int sampleOffset);
    void onSegmentEnd();
    bool advanceSong (bool dryRun);
    void locate (int64_t absTicks);
    void startPlayback (int64_t absTicks, int sampleOffset, bool sendStart);
    void stopPlayback (int sampleOffset);
    void handleMidi (const MidiMsg& m);
    double effectiveTempo() const;
    void finishSampling();
    int  memoryUsed() const;
    int  multiSound() const { return multiSource; }
    void updateDefaultLcd();
    void emitNote (int sound, int velocity, int offset);

    std::recursive_mutex mtx;
    Project proj;
    double sr = 48000.0;

    Channel ch[kNumChannels];
    Ladder4 ladder[2];
    Butter4 fixedLp[4];
    DcBlocker dcL, dcR;
    double nativePhase = 0;
    float last34 = -1, last56 = -1;
    uint64_t nativeClock = 0;

    // UI state
    std::atomic<int> bank { 0 }, selectedSound { 0 };
    SliderMode sliderMode = SliderMode::Mix;
    int multiSource = -1;              // sound spread over pads, -1 = off
    std::array<float, kPadsPerBank> multiTable {};
    SliderMode multiKind = SliderMode::MultiPitch;
    uint32_t heldPads = 0;
    int heldVel[kPadsPerBank] {};
    bool tapRepeatHeld = false, eraseHeld = false;

    // transport
    std::atomic<bool> playing { false }, recording { false }, songMode { false };
    std::atomic<int> curSegment { 0 }, curSong { 0 };
    int    segTick = 0;                // next tick to fire within current segment
    double tickFrac = 0;
    int64_t absTick = 0;               // ticks since song/segment start (for SPP + clock)
    int    songStep = 0;
    struct RepeatFrame { int beginStep, remaining; };
    std::vector<RepeatFrame> repeatStack;
    struct ReturnFrame { int song, step; std::vector<RepeatFrame> repeats; };
    std::vector<ReturnFrame> callStack;
    // tempo ramp (song TEMPO CHANGE)
    double rampDeltaPerTick = 0; int rampTicksLeft = 0;
    double tempoOffset = 0;

    // MIDI / sync
    struct PendingOff { int64_t at; uint8_t note, chan; };
    std::vector<PendingOff> pendingOffs;
    int64_t sampleCounter = 0;
    int64_t midiClockCount = 0, lastClockSample = -1;
    double  midiTicksPerSample = 0;
    bool    midiWaiting = false;       // ready mode: run pressed, waiting for master start
    bool    hostWasPlaying = false;
    double  hostTempo = 120.0;
    int     curOffset = 0;
    std::vector<MidiMsg>* midiOutPtr = nullptr;
    std::vector<int64_t> tapTimes;
    std::vector<MidiMsg> queuedUiMidi;   // start/stop sent from the UI thread

    // metronome
    float clickEnv = 0, clickPhase = 0, clickFreq = 1000;

    // sampling
    std::atomic<SampleState> sampleState { SampleState::Idle };
    int sampleTarget = 0;
    std::vector<int16_t> sampleBuf;
    Butter4 inputAA;
    float inputHeld = 0;
    std::atomic<float> inputPeak { 0 };

    // status
    std::string lcd1, lcd2;
    std::atomic<float> meters[kNumChannels];
    std::atomic<uint32_t> padFlash { 0 };
    std::atomic<int> triggerOuts { 0 };
};
} // namespace c12
