// External sync and transfer: click sync (SYNC 4), SMPTE/LTC read (SYNC 3) and write (SPECIAL 14),
// and MIDI Sample Dump Standard send/receive (SPECIAL 23/24).
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace c12
{
using Lock = std::lock_guard<std::recursive_mutex>;

// ===================================================================== timecode helpers
int Engine::fpsOf (SmpteRate r)
{
    switch (r) { case SmpteRate::Fps24: return 24; case SmpteRate::Fps25: return 25; default: return 30; }
}

static double exactFps (SmpteRate r) { return r == SmpteRate::Fps30Drop ? 30000.0 / 1001.0 : (double) Engine::fpsOf (r); }

static void framesToFields (int64_t frames, SmpteRate r, int& h, int& m, int& s, int& f)
{
    if (frames < 0) frames = 0;
    const int fps = Engine::fpsOf (r);
    if (r == SmpteRate::Fps30Drop)
    {
        const int64_t d = frames / 17982, mod = frames % 17982;
        frames += 18 * d + (mod < 2 ? 0 : 2 * ((mod - 2) / 1798));
    }
    f = (int) (frames % fps);
    s = (int) ((frames / fps) % 60);
    m = (int) ((frames / (fps * 60)) % 60);
    h = (int) ((frames / ((int64_t) fps * 3600)) % 24);
}

int64_t Engine::timecodeToFrames (int h, int m, int s, int f, SmpteRate r)
{
    const int fps = fpsOf (r);
    int64_t frames = ((int64_t) h * 3600 + m * 60 + s) * fps + f;
    if (r == SmpteRate::Fps30Drop)
    {
        const int64_t mins = (int64_t) h * 60 + m;
        frames -= 2 * (mins - mins / 10);
    }
    return frames;
}

std::string Engine::framesToTimecode (int64_t frames, SmpteRate r)
{
    int h, m, s, f;
    framesToFields (frames, r, h, m, s, f);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%02d:%02d:%02d%c%02d", h, m, s, r == SmpteRate::Fps30Drop ? ';' : ':', f);
    return buf;
}

std::string Engine::smpteNow()
{
    Lock l (mtx);
    return framesToTimecode (ltcWriteOn ? ltcOutFrame : lastTimecodeShown, proj.globals.smpteRate);
}

void Engine::setSmpteWrite (bool on)
{
    Lock l (mtx);
    ltcWriteOn = on;
    ltcOutFrame = proj.globals.smpteStartFrames;
    ltcOutBit = 0;
    ltcOutPhase = 0;
    ltcOutBits[0] = 0xFF;   // force rebuild
    setLcd (on ? "Writing SMPTE" : "SMPTE stopped", framesToTimecode (ltcOutFrame, proj.globals.smpteRate));
}

// ===================================================================== click sync (24 ppq pulses)
void Engine::externalClockPulse (bool)
{
    const int64_t now = sampleCounter + curOffset;
    if (midiWaiting)
    {
        midiClockCount = 0;
        lastClockSample = now;
        midiTicksPerSample = 0;
        startPlayback (0, curOffset, true);
        return;
    }
    if (! playing) return;
    ++midiClockCount;
    if (lastClockSample >= 0 && now > lastClockSample)
        midiTicksPerSample = (double) (kPPQ / 24) / (double) (now - lastClockSample);
    lastClockSample = now;
}

// ===================================================================== LTC read
static const uint8_t kLtcSync[16] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 };

static int bcd (const std::array<uint8_t, 80>& b, int start, int count)
{
    int v = 0;
    for (int i = 0; i < count; ++i) v |= b[(size_t) (start + i)] << i;
    return v;
}

void Engine::ltcDecodeSample (float x)
{
    const float th = std::max (0.005f, proj.globals.syncInputThreshold * 0.2f);
    bool edge = false;
    if (! ltcSignHigh && x > th) { ltcSignHigh = true; edge = true; }
    else if (ltcSignHigh && x < -th) { ltcSignHigh = false; edge = true; }
    if (! edge) return;

    const double t = (double) (sampleCounter + curOffset);
    const double d = t - ltcLastEdge;
    ltcLastEdge = t;
    const double nominal = sr / (80.0 * exactFps (proj.globals.smpteRate));
    if (ltcBitLen <= 0 || ltcBitLen < nominal * 0.5 || ltcBitLen > nominal * 1.6) ltcBitLen = nominal;
    if (d < ltcBitLen * 0.3 || d > ltcBitLen * 1.5) { ltcHalf = false; return; }   // noise or silence gap

    auto push = [this] (uint8_t bit) {
        std::rotate (ltcBits.begin(), ltcBits.begin() + 1, ltcBits.end());
        ltcBits[79] = bit;
        if (std::equal (ltcBits.begin() + 64, ltcBits.end(), kLtcSync))
        {
            const int f = bcd (ltcBits, 0, 4) + 10 * bcd (ltcBits, 8, 2);
            const int s = bcd (ltcBits, 16, 4) + 10 * bcd (ltcBits, 24, 3);
            const int m = bcd (ltcBits, 32, 4) + 10 * bcd (ltcBits, 40, 3);
            const int h = bcd (ltcBits, 48, 4) + 10 * bcd (ltcBits, 56, 2);
            if (f < 30 && s < 60 && m < 60 && h < 24)
                onLtcFrame (timecodeToFrames (h, m, s, f, proj.globals.smpteRate));
        }
    };

    if (d < ltcBitLen * 0.75)
    {
        if (ltcHalf) { push (1); ltcHalf = false; ltcBitLen = ltcBitLen * 0.95 + (2.0 * d) * 0.05; }
        else ltcHalf = true;
    }
    else
    {
        push (0);
        ltcHalf = false;
        ltcBitLen = ltcBitLen * 0.95 + d * 0.05;
    }
}

void Engine::onLtcFrame (int64_t frames)
{
    const int64_t now = sampleCounter + curOffset;
    ltcLastFrameSample = now;
    lastTimecodeShown = frames;
    const bool forward = ltcPrevFrame >= 0 && frames == ltcPrevFrame + 1;
    ltcPrevFrame = frames;

    // the frame is complete at its end, so the current position is one frame later
    const int64_t rel = frames + 1 - proj.globals.smpteStartFrames;
    if (rel < 0) return;
    const double seconds = (double) rel / exactFps (proj.globals.smpteRate);
    const double target = seconds * proj.globals.tempo * kPPQ / 60.0;

    if (midiWaiting && forward)
    {
        startPlayback ((int64_t) target, curOffset, true);   // also sends SPP + continue on MIDI out
        tickFrac = target - std::floor (target);
        return;
    }
    if (playing && std::fabs (target - ((double) absTick + tickFrac)) > 3.0)
        locate ((int64_t) target);
}

// ===================================================================== LTC write
float Engine::ltcGenerateSample()
{
    auto build = [this] {
        int h, m, s, f;
        framesToFields (ltcOutFrame, proj.globals.smpteRate, h, m, s, f);
        ltcOutBits.fill (0);
        auto put = [this] (int start, int count, int v) { for (int i = 0; i < count; ++i) ltcOutBits[(size_t) (start + i)] = (uint8_t) ((v >> i) & 1); };
        put (0, 4, f % 10); put (8, 2, f / 10);
        ltcOutBits[10] = proj.globals.smpteRate == SmpteRate::Fps30Drop ? 1 : 0;
        put (16, 4, s % 10); put (24, 3, s / 10);
        put (32, 4, m % 10); put (40, 3, m / 10);
        put (48, 4, h % 10); put (56, 2, h / 10);
        for (int i = 0; i < 16; ++i) ltcOutBits[(size_t) (64 + i)] = kLtcSync[i];
        // bit 27 (biphase correction) keeps the number of zeros even
        int zeros = 0;
        for (int i = 0; i < 80; ++i) if (i != 27 && ltcOutBits[(size_t) i] == 0) ++zeros;
        ltcOutBits[27] = (uint8_t) (zeros % 2);
    };
    if (ltcOutBits[0] == 0xFF) build();

    const double bitLen = sr / (80.0 * exactFps (proj.globals.smpteRate));
    const double prev = ltcOutPhase;
    ltcOutPhase += 1.0 / bitLen;
    if (ltcOutPhase >= 1.0)
    {
        ltcOutPhase -= 1.0;
        if (++ltcOutBit >= 80)
        {
            ltcOutBit = 0;
            ++ltcOutFrame;
            build();
        }
        ltcOutLevel = -ltcOutLevel;                 // transition at every bit boundary
    }
    else if (prev < 0.5 && ltcOutPhase >= 0.5 && ltcOutBits[(size_t) ltcOutBit])
    {
        ltcOutLevel = -ltcOutLevel;                 // extra mid-bit transition for a 1
    }
    ltcOutSmooth += 0.5f * (ltcOutLevel - ltcOutSmooth);   // finite rise time
    return ltcOutSmooth;
}

// ===================================================================== MIDI Sample Dump Standard
void Engine::sdsArmReceive (int sound)
{
    Lock l (mtx);
    sdsRxTarget = sound < 0 ? -1 : std::min (sound, kNumSounds - 1);
    if (sdsRxTarget >= 0) setLcd ("Rx Sound Dump", std::string ("Waiting -> ") + bankLetter (sdsRxTarget / 8) + std::to_string (sdsRxTarget % 8 + 1));
}

void Engine::sdsSend (int sound)
{
    Lock l (mtx);
    sound = std::max (0, std::min (sound, kNumSounds - 1));
    const Sound& s = proj.sounds[(size_t) sound];
    if (s.empty()) { setLcd ("Tx Sound Dump", "Sound is empty"); return; }
    const uint8_t ch = (uint8_t) proj.globals.midiChannel;
    const int start = s.startPos(), end = s.endPos(), len = end - start;
    const int period = (int) std::lround (1.0e9 / kNativeRate);
    const int loopStart = s.loop ? std::max (0, len - s.loopLength) : 0;
    const int loopEnd = s.loop ? len - 1 : 0;
    auto three = [] (std::vector<uint8_t>& v, int x) { v.push_back ((uint8_t) (x & 0x7F)); v.push_back ((uint8_t) ((x >> 7) & 0x7F)); v.push_back ((uint8_t) ((x >> 14) & 0x7F)); };

    std::vector<uint8_t> hdr { 0xF0, 0x7E, ch, 0x01, (uint8_t) (sound & 0x7F), 0x00, 12 };
    three (hdr, period); three (hdr, len); three (hdr, loopStart); three (hdr, loopEnd);
    hdr.push_back (s.loop ? 0x00 : 0x7F);
    hdr.push_back (0xF7);
    sysexOut.push_back (hdr);

    int packet = 0;
    for (int pos = 0; pos < len; pos += 60, ++packet)
    {
        std::vector<uint8_t> p { 0xF0, 0x7E, ch, 0x02, (uint8_t) (packet & 0x7F) };
        for (int k = 0; k < 60; ++k)
        {
            int u = 0;
            if (pos + k < len)
            {
                const int idx = s.reversed ? (end - 1 - (pos + k)) : (start + pos + k);
                u = (int) s.data[(size_t) idx] + 2048;          // unsigned 12-bit
            }
            const int w = u << 2;                                // left-justified in 14 bits
            p.push_back ((uint8_t) ((w >> 7) & 0x7F));
            p.push_back ((uint8_t) (w & 0x7F));
        }
        uint8_t sum = 0;
        for (size_t i = 1; i < p.size(); ++i) sum ^= p[i];
        p.push_back (sum & 0x7F);
        p.push_back (0xF7);
        sysexOut.push_back (std::move (p));
    }
    sdsTxTotal = sysexOut.size();
    sdsProgressPct = 0;
    setLcd ("Tx Sound Dump", std::string ("Sending ") + bankLetter (sound / 8) + std::to_string (sound % 8 + 1));
}

std::vector<std::vector<uint8_t>> Engine::takeSysexOut (int maxMessages)
{
    Lock l (mtx);
    std::vector<std::vector<uint8_t>> out;
    const int n = std::min (maxMessages, (int) sysexOut.size());
    for (int i = 0; i < n; ++i) out.push_back (std::move (sysexOut[(size_t) i]));
    sysexOut.erase (sysexOut.begin(), sysexOut.begin() + n);
    if (sdsTxTotal > 0)
    {
        sdsProgressPct = (int) (100 - 100 * sysexOut.size() / sdsTxTotal);
        if (sysexOut.empty()) { sdsTxTotal = 0; sdsProgressPct = -1; setLcd ("Tx Sound Dump", "Done"); }
    }
    return out;
}

void Engine::handleSysex (const uint8_t* m, size_t n)
{
    Lock l (mtx);
    if (n < 6 || m[0] != 0xF0 || m[1] != 0x7E) return;
    const uint8_t ch = m[2], sub = m[3];
    auto ack = [this, ch] (uint8_t pkt) { sysexOut.push_back ({ 0xF0, 0x7E, ch, 0x7F, (uint8_t) (pkt & 0x7F), 0xF7 }); };
    auto three = [m] (size_t i) { return (int) m[i] | ((int) m[i + 1] << 7) | ((int) m[i + 2] << 14); };

    if (sub == 0x03 && n >= 7)                     // dump request
    {
        sdsSend ((m[4] | (m[5] << 7)) % kNumSounds);
        return;
    }
    if (sub == 0x01 && n >= 21)                    // dump header
    {
        sdsRx = {};
        sdsRx.active = true;
        sdsRx.sampleNo = m[4] | (m[5] << 7);
        sdsRx.bits = std::max (8, std::min ((int) m[6], 28));
        const int period = three (7);
        sdsRx.rate = period > 0 ? 1.0e9 / period : kNativeRate;
        sdsRx.length = three (10);
        const int ls = three (13), le = three (16);
        const uint8_t type = m[19];
        sdsRx.data.reserve ((size_t) std::min (sdsRx.length, 4000000));
        loopStartRx = (type != 0x7F && le > ls) ? ls : -1;
        loopEndRx = le;
        sdsProgressPct = 0;
        setLcd ("Rx Sound Dump", "Receiving...");
        ack (0);
        return;
    }
    if (sub == 0x02 && sdsRx.active && n >= 127)   // data packet
    {
        uint8_t sum = 0;
        for (size_t i = 1; i < 125; ++i) sum ^= m[i];
        const int bytesPerWord = (sdsRx.bits + 6) / 7;
        const int wordsPerPacket = 120 / bytesPerWord;
        if ((sum & 0x7F) == m[125])
        {
            const double half = std::ldexp (1.0, sdsRx.bits - 1);
            for (int w = 0; w < wordsPerPacket && (int) sdsRx.data.size() < sdsRx.length; ++w)
            {
                int64_t v = 0;
                for (int b = 0; b < bytesPerWord; ++b) v = (v << 7) | m[(size_t) (5 + w * bytesPerWord + b)];
                v >>= (7 * bytesPerWord - sdsRx.bits);
                sdsRx.data.push_back ((float) (((double) v - half) / half));
            }
        }
        ack (m[4]);
        sdsProgressPct = sdsRx.length > 0 ? (int) (100 * sdsRx.data.size() / (size_t) sdsRx.length) : 0;
        if ((int) sdsRx.data.size() >= sdsRx.length) sdsFinishReceive();
    }
}

void Engine::sdsFinishReceive()
{
    sdsRx.active = false;
    sdsProgressPct = -1;
    const int target = sdsRxTarget >= 0 ? sdsRxTarget : sdsRx.sampleNo % kNumSounds;
    char nm[16];
    std::snprintf (nm, sizeof (nm), "SDS %03d", sdsRx.sampleNo % 1000);
    if (loadSound (target, sdsRx.data.data(), (int) sdsRx.data.size(), sdsRx.rate, nm) && loopStartRx >= 0)
    {
        auto& s = proj.sounds[(size_t) target];
        const double scale = kNativeRate / sdsRx.rate;
        const int newLen = (int) s.data.size();
        const int loopLen = (int) std::lround ((loopEndRx - loopStartRx + 1) * scale);
        s.truncEnd = std::min (newLen, (int) std::lround ((loopEndRx + 1) * scale));
        s.loop = loopLen > 0;
        s.loopLength = std::min (loopLen, s.endPos());
    }
    sdsRx.data.clear();
    sdsRx.data.shrink_to_fit();
    sdsRxTarget = -1;
}
} // namespace c12
