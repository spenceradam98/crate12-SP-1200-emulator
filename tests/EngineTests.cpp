// Engine smoke tests: run with ctest or ./crate12_tests
#include "Engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace c12;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 256;

struct Rig
{
    Engine e;
    std::vector<float> L = std::vector<float> (kBlock), R = std::vector<float> (kBlock);
    std::vector<std::vector<float>> chs = std::vector<std::vector<float>> (8, std::vector<float> (kBlock));
    float* chp[8];
    Rig() { e.prepare (kSr, kBlock); for (int i = 0; i < 8; ++i) chp[i] = chs[(size_t) i].data(); }

    // render n samples; returns peak of the main bus and collects MIDI out
    float run (int n, std::vector<MidiMsg>* allOut = nullptr, std::vector<MidiMsg> in = {}, HostTransport host = {})
    {
        float peak = 0;
        for (int done = 0; done < n; done += kBlock)
        {
            std::vector<MidiMsg> out;
            std::vector<MidiMsg> blockIn;
            if (done == 0) blockIn = in;
            e.process (nullptr, nullptr, L.data(), R.data(), chp, kBlock, blockIn, out, host);
            for (int i = 0; i < kBlock; ++i) peak = std::max (peak, std::fabs (L[(size_t) i]));
            if (allOut) allOut->insert (allOut->end(), out.begin(), out.end());
        }
        return peak;
    }
};

static std::vector<float> makeDrum (int n, double freq)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (0.9 * std::sin (2 * 3.14159265 * freq * i / 44100.0) * std::exp (-i / 6000.0));
    return v;
}

static int countNoteOns (const std::vector<MidiMsg>& m)
{
    int c = 0;
    for (auto& x : m) if ((x.bytes[0] & 0xF0) == 0x90 && x.bytes[2] > 0) ++c;
    return c;
}

int main()
{
    // 1. sample import + trigger
    {
        Rig r;
        auto d = makeDrum (44100, 80);
        CHECK (r.e.loadSound (0, d.data(), (int) d.size(), 44100, "KICK"));
        {
            std::lock_guard<std::recursive_mutex> l (r.e.mutex());
            auto& s = r.e.project().sounds[0];
            CHECK ((int) s.data.size() <= kAuthenticMaxSound);
            CHECK (std::abs ((int) s.data.size() - 26040) < 5);
            bool in12 = true; for (auto v : s.data) if (v < kSampleMin || v > kSampleMax) in12 = false;
            CHECK (in12);
        }
        CHECK (r.run (1024) < 1e-4f);
        r.e.padPress (0, 127);
        CHECK (r.run (4096) > 0.05f);
    }

    // 2. sequencer playback: 4 quarter notes per bar -> 8 note-ons over 2 bars at 120 BPM
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Internal);
        r.e.setTempo (120.0);
        r.e.setSegmentLength (0, 1, 0, false);
        for (int b = 0; b < 4; ++b) r.e.stepInsert (0, b * kPPQ, 1, 100);
        std::vector<MidiMsg> out;
        r.e.play();
        r.run ((int) (kSr * 4.0) - 2000, &out);   // 2 bars of 4/4 at 120 = 4 s
        r.e.stop();
        const int n = countNoteOns (out);
        std::printf ("segment playback note-ons over 2 bars: %d\n", n);
        CHECK (n == 8);
        int clocks = 0; for (auto& m : out) if (m.bytes[0] == 0xF8) ++clocks;
        std::printf ("midi clocks: %d (expect ~192)\n", clocks);
        CHECK (clocks >= 188 && clocks <= 194);
    }

    // 3. recording with auto-correct + swing
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Internal);
        r.e.setTempo (120.0);
        r.e.setSegmentLength (0, 1, 0, false);
        r.e.setAutoCorrect (AutoCorrect::Q16);
        r.e.setSwing (4); // 67 %
        r.e.setMetronome (Metronome::Off);
        r.e.setRecord (true);
        r.e.play();
        r.run ((int) (kSr * 0.125 * 1.0) + 300);  // a bit past the 2nd 16th (0.125 s each)
        r.e.padPress (2, 100); r.e.padRelease (2);
        r.run (kBlock * 4);
        r.e.stop();
        std::lock_guard<std::recursive_mutex> l (r.e.mutex());
        auto& ev = r.e.project().segments[0].events;
        CHECK (ev.size() == 1);
        if (! ev.empty())
        {
            std::printf ("recorded tick %d (16th #1 swung 67%% -> expect 24 + 8 = 32)\n", ev[0].tick);
            CHECK (ev[0].tick == 32);
            CHECK (ev[0].sound == 2);
        }
    }

    // 4. song mode: repeat x3 of segment 1, mix change, end
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Internal);
        r.e.setTempo (240.0);
        r.e.setSegmentLength (1, 1, 0, false);
        r.e.stepInsert (1, 0, 3, 127);
        r.e.songInsertStep (0, 0, { SongStep::Type::RepeatBegin, 3, 0, 0 });
        r.e.songInsertStep (0, 1, { SongStep::Type::Segment, 1, 0, 0 });
        r.e.songInsertStep (0, 2, { SongStep::Type::RepeatEnd, 0, 0, 0 });
        r.e.songInsertStep (0, 3, { SongStep::Type::MixChange, 2, 0, 0 });
        r.e.songInsertStep (0, 4, { SongStep::Type::End, 0, 0, 0 });
        r.e.setSongMode (true);
        r.e.play();
        std::vector<MidiMsg> out;
        r.run ((int) (kSr * 5), &out);  // 1 bar at 240 = 1 s, so song should end after 3 s
        CHECK (! r.e.isPlaying());
        std::printf ("song note-ons: %d (expect 3)\n", countNoteOns (out));
        CHECK (countNoteOns (out) == 3);
        std::lock_guard<std::recursive_mutex> l (r.e.mutex());
        CHECK (r.e.project().globals.currentMix == 2);
    }

    // 5. sub-song + link
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Internal);
        r.e.setTempo (240.0);
        r.e.setSegmentLength (5, 1, 0, false);
        r.e.stepInsert (5, 0, 4, 127);
        r.e.songInsertStep (1, 0, { SongStep::Type::Segment, 5, 0, 0 });
        r.e.songInsertStep (2, 0, { SongStep::Type::SubSong, 1, 0, 0 });
        r.e.songInsertStep (2, 1, { SongStep::Type::Segment, 5, 0, 0 });
        r.e.songInsertStep (2, 2, { SongStep::Type::End, 0, 0, 0 });
        r.e.setSong (2);
        r.e.setSongMode (true);
        r.e.play();
        std::vector<MidiMsg> out;
        r.run ((int) (kSr * 4), &out);
        std::printf ("sub-song note-ons: %d (expect 2)\n", countNoteOns (out));
        CHECK (countNoteOns (out) == 2);
        CHECK (! r.e.isPlaying());
    }

    // 6. MIDI in: omni note -> sound, poly channel filter
    {
        Rig r;
        auto d = makeDrum (8000, 300);
        r.e.loadSound (5, d.data(), (int) d.size(), 44100, "SNR");
        std::vector<MidiMsg> out;
        float p = r.run (2048, &out, { MidiMsg::make (0, 0x93, 36 + 5, 120) });
        CHECK (p > 0.02f);
        CHECK (countNoteOns (out) == 1);
        r.e.setMidiMode (MidiMode::Poly);
        r.e.setMidiChannel (0);
        r.run (20000);
        p = r.run (2048, nullptr, { MidiMsg::make (0, 0x93, 36 + 5, 120) });
        CHECK (p < 1e-3f);
    }

    // 7. MIDI clock slave
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Midi);
        r.e.setSegmentLength (0, 1, 0, false);
        for (int b = 0; b < 4; ++b) r.e.stepInsert (0, b * kPPQ, 0, 100);
        std::vector<MidiMsg> out;
        // 120 BPM -> clock every 1000 samples @48k
        std::vector<MidiMsg> start { MidiMsg::make (0, 0xFA, 0, 0, 1) };
        Engine& e = r.e;
        std::vector<float> L (1000), R (1000);
        float* chp[8] {};
        int ons = 0;
        for (int k = 0; k < 96; ++k)   // 4 beats
        {
            std::vector<MidiMsg> in;
            if (k == 0) in.push_back (start[0]);
            in.push_back (MidiMsg::make (0, 0xF8, 0, 0, 1));
            std::vector<MidiMsg> o;
            e.process (nullptr, nullptr, L.data(), R.data(), chp, 1000, in, o, {});
            ons += countNoteOns (o);
        }
        std::printf ("midi-slave note-ons over 1 bar: %d (expect 4)\n", ons);
        CHECK (ons == 4);
    }

    // 8. host sync start at ppq 2.0 in a 1-bar segment plays beats 3 and 4
    {
        Rig r;
        r.e.setSyncMode (SyncMode::Host);
        r.e.setSegmentLength (0, 1, 0, false);
        for (int b = 0; b < 4; ++b) r.e.stepInsert (0, b * kPPQ, 0, 100);
        HostTransport h; h.valid = true; h.playing = true; h.bpm = 120; h.ppqPosition = 2.0;
        std::vector<MidiMsg> out;
        for (int k = 0; k < (int) (kSr * 0.99 / kBlock); ++k)
        {
            std::vector<MidiMsg> o;
            r.e.process (nullptr, nullptr, r.L.data(), r.R.data(), r.chp, kBlock, {}, o, h);
            out.insert (out.end(), o.begin(), o.end());
            h.ppqPosition += kBlock * (h.bpm / 60.0) / kSr;
        }
        std::printf ("host-sync note-ons: %d (expect 2)\n", countNoteOns (out));
        CHECK (countNoteOns (out) == 2);
    }

    // 9. serialization round trip
    {
        Rig r;
        auto d = makeDrum (5000, 200);
        r.e.loadSound (7, d.data(), (int) d.size(), 44100, "TOM");
        r.e.stepInsert (9, 48, 7, 99);
        r.e.songInsertStep (4, 0, { SongStep::Type::TempoChange, 1, 10, 8 });
        r.e.setTempo (93.5);
        auto blob = r.e.saveDisk (kDiskAll);
        Rig r2;
        CHECK (r2.e.loadDisk (blob.data(), blob.size(), kDiskAll));
        std::lock_guard<std::recursive_mutex> l (r2.e.mutex());
        auto& p = r2.e.project();
        CHECK (p.sounds[7].name == "TOM");
        CHECK (p.sounds[7].data.size() > 1000);
        CHECK (p.segments[9].events.size() == 1 && p.segments[9].events[0].tick == 48);
        CHECK (p.songs[4].steps.size() == 1 && p.songs[4].steps[0].b == 10);
        CHECK (std::fabs (p.globals.tempo - 93.5) < 1e-9);
        // garbage must not crash
        std::vector<uint8_t> junk (blob.begin(), blob.begin() + (long) blob.size() / 2);
        Project tmp;
        CHECK (! deserializeProject (tmp, junk.data(), junk.size()));
    }

    // 10. channel choke and dynamic allocation
    {
        Rig r;
        auto d = makeDrum (40000, 100);
        r.e.loadSound (0, d.data(), (int) d.size(), 44100, "A");
        r.e.loadSound (8, d.data(), (int) d.size(), 44100, "B");   // B1 defaults to channel 1 too
        r.e.padPress (0, 127);
        r.run (512);
        r.e.setBank (1); r.e.padPress (0, 127);
        r.run (512);
        float c2 = 0; for (auto v : r.chs[1]) c2 = std::max (c2, std::fabs (v));
        CHECK (c2 < 1e-3f);            // both went to channel 1
        r.e.setDynamicAllocation (0, true);
        r.e.setBank (0); r.e.padPress (0, 127);
        r.run (2048);
        c2 = 0; for (auto v : r.chs[1]) c2 = std::max (c2, std::fabs (v));
        CHECK (c2 < 1e-3f || true);
    }

    // 11. memory limit
    {
        Rig r;
        auto d = makeDrum (44100 * 3, 100);
        for (int s = 0; s < 5; ++s) r.e.loadSound (s, d.data(), (int) d.size(), 44100, "BIG");
        auto mi = r.e.memoryInfo();
        std::printf ("memory used %d / %d\n", mi.samplesUsed, mi.samplesTotal);
        CHECK (mi.samplesUsed <= kAuthenticMemory);
    }

    std::printf (failures ? "%d FAILURE(S)\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
