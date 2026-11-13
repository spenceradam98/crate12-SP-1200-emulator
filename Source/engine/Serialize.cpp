// Binary project format ("disk"). Chunked so later versions can add fields.
#include "Engine.h"

#include <cstring>

namespace c12
{
namespace
{
struct Writer
{
    std::vector<uint8_t> b;
    void u8 (uint8_t v) { b.push_back (v); }
    void i32 (int32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) ((uint32_t) v >> (8 * i))); }
    void i64 (int64_t v) { for (int i = 0; i < 8; ++i) b.push_back ((uint8_t) ((uint64_t) v >> (8 * i))); }
    void f32 (float v) { int32_t x; std::memcpy (&x, &v, 4); i32 (x); }
    void f64 (double v) { int64_t x; std::memcpy (&x, &v, 8); i64 (x); }
    void str (const std::string& s) { i32 ((int32_t) s.size()); b.insert (b.end(), s.begin(), s.end()); }
    size_t beginChunk (const char* tag) { b.insert (b.end(), tag, tag + 4); i32 (0); return b.size(); }
    void endChunk (size_t start)
    {
        const auto len = (uint32_t) (b.size() - start);
        for (int i = 0; i < 4; ++i) b[start - 4 + (size_t) i] = (uint8_t) (len >> (8 * i));
    }
};

struct Reader
{
    const uint8_t* p; size_t n, pos = 0; bool ok = true;
    Reader (const uint8_t* d, size_t s) : p (d), n (s) {}
    bool need (size_t k) { if (pos + k > n) ok = false; return ok; }
    uint8_t u8() { return need (1) ? p[pos++] : 0; }
    int32_t i32() { if (! need (4)) return 0; uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t) p[pos++] << (8 * i); return (int32_t) v; }
    int64_t i64() { if (! need (8)) return 0; uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= (uint64_t) p[pos++] << (8 * i); return (int64_t) v; }
    float f32() { int32_t x = i32(); float v; std::memcpy (&v, &x, 4); return v; }
    double f64() { int64_t x = i64(); double v; std::memcpy (&v, &x, 8); return v; }
    std::string str() { const int32_t l = i32(); if (l < 0 || ! need ((size_t) l)) return {}; std::string s ((const char*) p + pos, (size_t) l); pos += (size_t) l; return s; }
};

void writePad (Writer& w, const PadParams& p) { w.f32 (p.level); w.f32 (p.tune); w.f32 (p.decay); }
PadParams readPad (Reader& r) { PadParams p; p.level = r.f32(); p.tune = r.f32(); p.decay = r.f32(); return p; }
void writeMix (Writer& w, const Mix& m) { for (auto& p : m.pads) writePad (w, p); for (auto c : m.channel) w.u8 (c); }
void readMix (Reader& r, Mix& m) { for (auto& p : m.pads) p = readPad (r); for (auto& c : m.channel) c = (uint8_t) (r.u8() % kNumChannels); }
} // namespace

std::vector<uint8_t> serializeProject (const Project& p, int sections)
{
    Writer w;
    w.b.insert (w.b.end(), { 'C', '1', '2', 'P' });
    w.i32 (1); // version

    if (sections & kDiskGlobals)
    {
        const auto c = w.beginChunk ("GLOB");
        const auto& g = p.globals;
        w.f64 (g.tempo); w.i32 (g.swingIndex); w.u8 ((uint8_t) g.autoCorrect); w.u8 ((uint8_t) g.metronome);
        w.u8 (g.metronomeInRecordOnly); w.f32 (g.metronomeLevel);
        w.u8 (g.dynamicButtons); w.i32 (g.currentMix); w.u8 (g.tuneMode);
        w.i32 (g.midiChannel); w.u8 ((uint8_t) g.midiMode); w.u8 (g.pitchKeys); w.u8 (g.midiOutNotes); w.u8 (g.midiOutClock);
        for (auto n : g.noteMap) w.u8 (n);
        w.u8 ((uint8_t) g.syncMode); w.u8 ((uint8_t) g.smpteRate); w.i64 (g.smpteStartFrames);
        for (auto d : g.dynamicAllocation) w.u8 (d);
        w.u8 (g.filterEnvelopes); w.f32 (g.filter12Cutoff); w.f32 (g.filter12Reso); w.f32 (g.filter34Hz); w.f32 (g.filter56Hz); w.f32 (g.outputDrive);
        w.u8 (g.authenticLimits); for (auto pn : g.pan) w.f32 (pn); w.f32 (g.masterLevel);
        w.i32 (g.inputGainDb); w.f32 (g.sampleThreshold); w.f32 (g.sampleLengthSec);
        w.endChunk (c);
    }
    if (sections & kDiskSounds)
    {
        const auto c = w.beginChunk ("SNDS");
        for (const auto& s : p.sounds)
        {
            w.str (s.name); w.i32 (s.truncStart); w.i32 (s.truncEnd); w.u8 (s.loop); w.i32 (s.loopLength); w.u8 (s.reversed);
            w.i32 ((int32_t) s.data.size());
            for (auto v : s.data) { w.u8 ((uint8_t) (v & 0xFF)); w.u8 ((uint8_t) ((uint16_t) v >> 8)); }
        }
        w.endChunk (c);
    }
    if (sections & kDiskSequences)
    {
        const auto c = w.beginChunk ("SEQS");
        writeMix (w, p.working);
        for (const auto& m : p.mixes) writeMix (w, m);
        w.i32 (p.firstSongStep);
        for (const auto& s : p.segments)
        {
            w.i32 (s.beatsPerBar); w.i32 (s.beatUnit); w.i32 (s.bars); w.i32 (s.extraBeats);
            w.i32 ((int32_t) s.events.size());
            for (const auto& e : s.events)
            {
                w.i32 (e.tick); w.u8 (e.sound); w.u8 (e.velocity); w.u8 (e.hasOverride);
                if (e.hasOverride) writePad (w, e.override_);
            }
        }
        for (const auto& s : p.songs)
        {
            w.i32 ((int32_t) s.steps.size());
            for (const auto& st : s.steps) { w.u8 ((uint8_t) st.type); w.i32 (st.a); w.i32 (st.b); w.i32 (st.c); }
        }
        w.endChunk (c);
    }
    return w.b;
}

bool deserializeProject (Project& p, const uint8_t* data, size_t size, int* found)
{
    if (found) *found = 0;
    if (data == nullptr || size < 8 || std::memcmp (data, "C12P", 4) != 0) return false;
    Reader r (data, size);
    r.pos = 4;
    const int version = r.i32();
    if (version < 1) return false;

    while (r.ok && r.pos + 8 <= size)
    {
        char tag[5] {};
        std::memcpy (tag, data + r.pos, 4); r.pos += 4;
        const auto len = (size_t) (uint32_t) r.i32();
        if (! r.need (len)) return false;
        Reader c (data + r.pos, len);
        const std::string t (tag);
        if (t == "GLOB")
        {
            auto& g = p.globals;
            g.tempo = c.f64(); g.swingIndex = c.i32() % 6; g.autoCorrect = (AutoCorrect) std::min<uint8_t> (c.u8(), 8);
            g.metronome = (Metronome) std::min<uint8_t> (c.u8(), 9); g.metronomeInRecordOnly = c.u8(); g.metronomeLevel = c.f32();
            g.dynamicButtons = c.u8(); g.currentMix = c.i32() % kNumMixes; g.tuneMode = c.u8();
            g.midiChannel = c.i32() & 15; g.midiMode = (MidiMode) c.u8(); g.pitchKeys = c.u8(); g.midiOutNotes = c.u8(); g.midiOutClock = c.u8();
            for (auto& n : g.noteMap) n = c.u8() & 0x7F;
            g.syncMode = (SyncMode) c.u8(); g.smpteRate = (SmpteRate) (c.u8() & 3); g.smpteStartFrames = c.i64();
            for (auto& d : g.dynamicAllocation) d = c.u8();
            g.filterEnvelopes = c.u8(); g.filter12Cutoff = c.f32(); g.filter12Reso = c.f32(); g.filter34Hz = c.f32(); g.filter56Hz = c.f32(); g.outputDrive = c.f32();
            g.authenticLimits = c.u8(); for (auto& pn : g.pan) pn = c.f32(); g.masterLevel = c.f32();
            g.inputGainDb = c.i32(); g.sampleThreshold = c.f32(); g.sampleLengthSec = c.f32();
            if (g.swingIndex < 0) g.swingIndex = 0;
            if (g.currentMix < 0) g.currentMix = 0;
            if (c.ok && found) *found |= kDiskGlobals;
        }
        else if (t == "SNDS")
        {
            for (auto& s : p.sounds)
            {
                s.name = c.str(); s.truncStart = c.i32(); s.truncEnd = c.i32(); s.loop = c.u8(); s.loopLength = c.i32(); s.reversed = c.u8();
                const int32_t n = c.i32();
                if (n < 0 || ! c.need ((size_t) n * 2)) return false;
                s.data.resize ((size_t) n);
                for (auto& v : s.data) { const uint8_t lo = c.u8(), hi = c.u8(); v = (int16_t) (uint16_t) (lo | (hi << 8)); }
                const int sz = (int) s.data.size();
                if (s.truncStart < 0 || s.truncStart > sz) s.truncStart = 0;
                if (s.truncEnd < 0 || s.truncEnd > sz) s.truncEnd = 0;
                if (s.loopLength < 0 || s.loopLength > sz) s.loopLength = 0;
            }
            if (c.ok && found) *found |= kDiskSounds;
        }
        else if (t == "SEQS")
        {
            readMix (c, p.working);
            for (auto& m : p.mixes) readMix (c, m);
            p.firstSongStep = std::max (0, c.i32());
            for (auto& s : p.segments)
            {
                s.beatsPerBar = c.i32(); s.beatUnit = c.i32(); s.bars = c.i32(); s.extraBeats = c.i32();
                if (s.beatUnit != 4 && s.beatUnit != 8 && s.beatUnit != 16 && s.beatUnit != 32) s.beatUnit = 4;
                if (s.beatsPerBar < 1 || s.beatsPerBar > 99) s.beatsPerBar = 4;
                const int32_t n = c.i32();
                if (n < 0 || n > 1000000) return false;
                s.events.clear();
                for (int i = 0; i < n && c.ok; ++i)
                {
                    NoteEvent e; e.tick = c.i32(); e.sound = (uint8_t) (c.u8() % kNumSounds); e.velocity = c.u8(); e.hasOverride = c.u8();
                    if (e.hasOverride) e.override_ = readPad (c);
                    s.events.push_back (e);
                }
            }
            for (auto& s : p.songs)
            {
                const int32_t n = c.i32();
                if (n < 0 || n > 100000) return false;
                s.steps.clear();
                for (int i = 0; i < n && c.ok; ++i)
                {
                    SongStep st; st.type = (SongStep::Type) std::min<uint8_t> (c.u8(), 7); st.a = c.i32(); st.b = c.i32(); st.c = c.i32();
                    s.steps.push_back (st);
                }
            }
            if (c.ok && found) *found |= kDiskSequences;
        }
        if (! c.ok) return false;
        r.pos += len;
    }
    return true;
}
} // namespace c12
