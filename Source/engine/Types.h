// Crate12 - 12-bit sampling drum machine engine (SP-1200-style workflow).
// Core data model. Pure C++17, no framework dependencies.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace c12
{
// ---------------------------------------------------------------- hardware constants
constexpr double kNativeRate        = 26040.0;   // internal DAC/ADC clock (Hz)
constexpr int    kNumBanks          = 4;         // A-D
constexpr int    kPadsPerBank       = 8;
constexpr int    kNumSounds         = kNumBanks * kPadsPerBank; // 32 sound locations
constexpr int    kNumChannels       = 8;         // 8 output channels, one voice each
constexpr int    kNumMixes          = 8;
constexpr int    kNumSegments       = 100;       // 00-99
constexpr int    kNumSongs          = 100;       // 00-99
constexpr int    kPPQ               = 96;        // sequencer ticks per quarter note
constexpr int    kAuthenticMaxNotes = 5000;
constexpr int    kAuthenticMemory   = 260400;    // 10.0 s @ 26.04 kHz (4 zones x 2.5 s)
constexpr int    kAuthenticMaxSound = kAuthenticMemory / 4; // 2.5 s per sound
constexpr int    kSampleMin = -2048, kSampleMax = 2047;     // 12-bit signed
constexpr double kMinTempo = 40.0, kMaxTempo = 240.0;
constexpr float  kTuneMin = -8.0f, kTuneMax = 7.0f;          // semitones (slider range)

inline int soundIndex (int bank, int pad) { return bank * kPadsPerBank + pad; }
inline char bankLetter (int bank)         { return (char) ('A' + bank); }

// ---------------------------------------------------------------- enums
enum class SyncMode : uint8_t { Internal = 1, Midi = 2, Smpte = 3, Click = 4, Host = 5 };
enum class MidiMode : uint8_t { Omni = 1, Poly = 2 };
enum class SmpteRate : uint8_t { Fps24, Fps25, Fps30, Fps30Drop };

// Auto-correct (quantize). Values from the original manual.
enum class AutoCorrect : uint8_t { Q4, Q4T, Q8, Q8T, Q16, Q16T, Q32, Q32T, HiRes };
int autoCorrectTicks (AutoCorrect q);          // grid size in ticks (1 for HiRes)
const char* autoCorrectName (AutoCorrect q);

// Metronome: same divisions plus Off and Click (24ppq pulse).
enum class Metronome : uint8_t { Off, Q4, Q4T, Q8, Q8T, Q16, Q16T, Q32, Q32T, Click24 };

// Swing amounts from the original: 50, 54, 58, 63, 67, 71 %
constexpr std::array<int, 6> kSwingValues { 50, 54, 58, 63, 67, 71 };

enum class SliderMode : uint8_t { Mix, Tune, Decay, MultiPitch, MultiLevel, MultiDecay };

// ---------------------------------------------------------------- sounds
struct Sound
{
    std::vector<int16_t> data;     // 12-bit values stored in int16 (range -2048..2047)
    std::string name;              // up to 8 chars on the LCD
    int  truncStart = 0;           // playback start (samples)
    int  truncEnd   = 0;           // playback end (exclusive); 0 = data.size()
    bool loop       = false;       // loop the last loopLength samples before truncEnd
    int  loopLength = 0;
    bool reversed   = false;

    bool empty() const { return data.empty(); }
    int  startPos() const { return truncStart; }
    int  endPos() const { return truncEnd > 0 ? truncEnd : (int) data.size(); }
    int  memoryUsed() const { return (int) data.size(); }
};

// Per-sound playback parameters held in a mix.
struct PadParams
{
    float level = 0.8f;   // 0..1 (slider)
    float tune  = 0.0f;   // semitones, kTuneMin..kTuneMax
    float decay = 1.0f;   // 0..1, 1 = full length (no decay)
};

struct Mix
{
    std::array<PadParams, kNumSounds> pads {};
    std::array<uint8_t, kNumSounds> channel {};   // output channel 0..7 per sound
    Mix() { for (int i = 0; i < kNumSounds; ++i) channel[(size_t) i] = (uint8_t) (i % kNumChannels); }
};

// ---------------------------------------------------------------- sequences
struct NoteEvent
{
    int32_t tick     = 0;
    uint8_t sound    = 0;     // 0..31
    uint8_t velocity = 127;   // 1..127
    // Parameter snapshot when recorded in a multi mode (SP-1200 "Multi-Pitch" etc.)
    bool    hasOverride = false;
    PadParams override_ {};
    bool    skipOnce = false;   // runtime only: just recorded ahead of the playhead
};

struct Segment
{
    int beatsPerBar = 4;        // 1..99
    int beatUnit    = 4;        // 4, 8, 16, 32
    int bars        = 2;        // 0..99
    int extraBeats  = 0;        // 0..99
    std::vector<NoteEvent> events;   // kept sorted by tick

    int ticksPerBeat() const { return kPPQ * 4 / beatUnit; }
    int lengthTicks() const  { return (bars * beatsPerBar + extraBeats) * ticksPerBeat(); }
    bool isEmpty() const     { return events.empty(); }
};

struct SongStep
{
    enum class Type : uint8_t
    {
        Segment,      // a = segment number
        RepeatBegin,  // a = repeat count (2..99)
        RepeatEnd,
        MixChange,    // a = mix 0..7
        TempoChange,  // a = 1 accelerando / 2 ritardando, b = BPM change (0..99), c = over N beats (1..32)
        SubSong,      // a = song number; plays it then returns
        TriggerOut,   // fires the trigger output (a = unused)
        End           // a = 0 stop, 1 repeat song, 2 link to song b
    };
    Type type = Type::Segment;
    int a = 0, b = 0, c = 0;
};

struct Song
{
    std::vector<SongStep> steps;
    bool isEmpty() const { return steps.empty(); }
};

// ---------------------------------------------------------------- global settings
struct Globals
{
    // Performance
    double      tempo       = 90.0;      // 40.0..240.0, 0.1 resolution
    int         swingIndex  = 0;         // index into kSwingValues
    AutoCorrect autoCorrect = AutoCorrect::Q16;
    Metronome   metronome   = Metronome::Q4;
    bool        metronomeInRecordOnly = true;
    float       metronomeLevel = 0.5f;

    // SET-UP
    bool  dynamicButtons = true;         // SET-UP 14: pads velocity sensitive
    int   currentMix     = 0;            // SET-UP 16
    bool  tuneMode       = true;         // SET-UP 18: 1 = tuning, 2 = decay (slider function)

    // MIDI (SET-UP 22)
    int      midiChannel = 0;            // 0..15
    MidiMode midiMode    = MidiMode::Omni;
    bool     pitchKeys   = false;        // keys G#3..B4 play the selected sound at 16 pitches
    bool     midiOutNotes = true;
    bool     midiOutClock = true;
    std::array<uint8_t, kNumSounds> noteMap {}; // MIDI note per sound

    // SYNC
    SyncMode  syncMode  = SyncMode::Host;
    SmpteRate smpteRate = SmpteRate::Fps30;
    int64_t   smpteStartFrames = 0;      // HH:MM:SS:FF start offset, in frames

    // SPECIAL 22: dynamic allocation per channel pair (1/2, 3/4, 5/6, 7/8)
    std::array<bool, 4> dynamicAllocation { false, false, false, false };

    // Analog stage
    bool  filterEnvelopes = true;        // power-on "Filter Envs Off" option
    float filter12Cutoff  = 0.7f;        // ch 1-2 SSM2044-style dynamic filter (0..1)
    float filter12Reso    = 0.15f;       // (0..1)
    float filter34Hz      = 7500.0f;     // fixed low-pass ch 3-4 (approximate)
    float filter56Hz      = 11000.0f;    // fixed low-pass ch 5-6 (approximate)
    float outputDrive     = 1.0f;        // output stage saturation amount

    // Emulator extensions (not on the hardware, defaults keep it authentic)
    bool  authenticLimits = true;        // 10 s memory, 2.5 s per sound, 5000 notes
    std::array<float, kNumChannels> pan {};  // -1..1 per channel for the stereo mix bus
    float masterLevel = 0.8f;

    // Sampling (SAMPLE module)
    int   inputGainDb = 0;               // 0, 20, 40
    float sampleThreshold = 0.05f;       // 0..1
    float sampleLengthSec = 2.5f;        // 0.1 .. 2.5 (authentic)

    Globals()
    {
        for (int i = 0; i < kNumSounds; ++i) noteMap[(size_t) i] = (uint8_t) (36 + i);
    }
};

// Everything that persists (state / "disk").
struct Project
{
    Globals globals;
    std::array<Sound, kNumSounds> sounds;
    std::array<Mix, kNumMixes> mixes;
    Mix working;                                   // live slider state (becomes a mix via DEFINE MIX)
    std::array<Segment, kNumSegments> segments;
    std::array<Song, kNumSongs> songs;
    int firstSongStep = 0;                         // SET-UP 21: start song at step
};

// Simple MIDI message type so the engine stays framework independent.
struct MidiMsg
{
    int     offset = 0;         // sample offset in block
    uint8_t bytes[3] {};
    uint8_t size = 0;
    static MidiMsg make (int off, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t n = 3)
    { MidiMsg m; m.offset = off; m.bytes[0] = a; m.bytes[1] = b; m.bytes[2] = c; m.size = n; return m; }
};

struct HostTransport
{
    bool   valid = false;
    bool   playing = false;
    double bpm = 120.0;
    double ppqPosition = 0.0;
};
} // namespace c12
