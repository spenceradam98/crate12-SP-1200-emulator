// Original "factory-style" kit, synthesized from scratch (no third-party samples).
// Rendered at 44.1 kHz then pushed through the normal 26.04 kHz / 12-bit import path.
#include "Engine.h"

#include <cmath>
#include <random>

namespace c12
{
namespace
{
constexpr double SR = 44100.0;
using Buf = std::vector<float>;

struct Noise { std::mt19937 rng { 1200 }; std::uniform_real_distribution<float> d { -1.0f, 1.0f }; float operator()() { return d (rng); } };

Buf make (double secs) { return Buf ((size_t) (secs * SR), 0.0f); }
float env (double t, double attack, double decay) { return (float) ((t < attack ? t / attack : 1.0) * std::exp (-t / decay)); }

// one-pole helpers
struct HP { float a, x1 = 0, y1 = 0; explicit HP (double fc) : a ((float) std::exp (-2 * kPi * fc / SR)) {} float operator() (float x) { y1 = a * (y1 + x - x1); x1 = x; return y1; } };
struct LP { float a, y = 0; explicit LP (double fc) : a ((float) (1 - std::exp (-2 * kPi * fc / SR))) {} float operator() (float x) { y += a * (x - y); return y; } };
struct BP { Biquad b; BP (double fc, double q) { const double w = 2 * kPi * fc / SR, al = std::sin (w) / (2 * q), a0 = 1 + al;
    b.b0 = al / a0; b.b1 = 0; b.b2 = -al / a0; b.a1 = -2 * std::cos (w) / a0; b.a2 = (1 - al) / a0; } float operator() (float x) { return b.process (x); } };

void normalize (Buf& b, float peak = 0.95f)
{
    float m = 0; for (auto v : b) m = std::max (m, std::fabs (v));
    if (m > 0) for (auto& v : b) v *= peak / m;
}

Buf kick (double f0, double f1, double sweep, double dec, double click, double drive)
{
    auto b = make (dec * 5); Noise n; double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        const double f = f1 + (f0 - f1) * std::exp (-t / sweep);
        ph += 2 * kPi * f / SR;
        double s = std::sin (ph) * env (t, 0.001, dec);
        s += click * n() * std::exp (-t / 0.002);
        b[i] = (float) std::tanh (s * drive);
    }
    normalize (b); return b;
}

Buf snare (double tone, double toneDec, double noiseDec, double noiseAmt, double bright)
{
    auto b = make (std::max (toneDec, noiseDec) * 5); Noise n; HP hp (bright); double p1 = 0, p2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        p1 += 2 * kPi * tone * (1 + 0.5 * std::exp (-t / 0.01)) / SR; p2 += 2 * kPi * tone * 1.6 / SR;
        const double body = (std::sin (p1) + 0.5 * std::sin (p2)) * env (t, 0.0005, toneDec);
        const double nz = hp (n()) * noiseAmt * env (t, 0.0005, noiseDec);
        b[i] = (float) std::tanh (1.4 * (body + nz));
    }
    normalize (b); return b;
}

Buf clap()
{
    auto b = make (0.45); Noise n; BP bp (1200, 1.2);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        double e = 0;
        for (int k = 0; k < 3; ++k) { const double tk = t - k * 0.011; if (tk >= 0 && tk < 0.011) e = std::max (e, std::exp (-tk / 0.003)); }
        if (t >= 0.033) e = std::max (e, std::exp (-(t - 0.033) / 0.09));
        b[i] = bp (n()) * (float) e;
    }
    normalize (b); return b;
}

Buf metallic (double dec, double hpf, double amp = 1.0)
{
    // six detuned square oscillators, classic analog-hat style
    const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    auto b = make (dec * 6); HP hp1 (hpf), hp2 (hpf); BP bp (9000, 0.8); Noise n; double ph[6] {};
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR; double s = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += fr[k] * 1.7 / SR; s += (std::fmod (ph[k], 1.0) < 0.5 ? 1 : -1); }
        s = s / 6 + 0.3 * n();
        b[i] = hp2 (hp1 (bp ((float) s))) * env (t, 0.0005, dec) * (float) amp;
    }
    normalize (b, 0.85f); return b;
}

Buf tom (double f0, double dec)
{
    auto b = make (dec * 5); Noise n; double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        ph += 2 * kPi * f0 * (1 + 0.6 * std::exp (-t / 0.03)) / SR;
        b[i] = (float) ((std::sin (ph) + 0.05 * n() * std::exp (-t / 0.01)) * env (t, 0.001, dec));
    }
    normalize (b); return b;
}

Buf tone (double f, double dec, double f2 = 0, bool square = false)
{
    auto b = make (dec * 5); double p1 = 0, p2 = 0; LP lp (3500);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        p1 += f / SR; p2 += f2 / SR;
        auto osc = [square] (double p) { return square ? (std::fmod (p, 1.0) < 0.5 ? 1.0 : -1.0) : std::sin (2 * kPi * p); };
        double s = osc (p1) + (f2 > 0 ? osc (p2) : 0);
        b[i] = lp ((float) s) * env (t, 0.0005, dec);
    }
    normalize (b); return b;
}

Buf shaker (double len, double dec)
{
    auto b = make (len); Noise n; HP hp (5000);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        b[i] = hp (n()) * (float) (std::min (1.0, t / 0.02) * std::exp (-t / dec));
    }
    normalize (b, 0.8f); return b;
}

Buf zap()
{
    auto b = make (0.25); double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / SR;
        ph += 2 * kPi * (100 + 3000 * std::exp (-t / 0.015)) / SR;
        b[i] = (float) (std::sin (ph) * std::exp (-t / 0.05));
    }
    normalize (b); return b;
}
} // namespace

void Engine::loadFactoryKit()
{
    struct Item { int slot; const char* name; Buf buf; };
    std::vector<Item> kit;
    // Bank A: core kit
    kit.push_back ({ 0, "KICK 1",  kick (160, 48, 0.025, 0.12, 0.6, 1.6) });
    kit.push_back ({ 1, "KICK 2",  kick (220, 55, 0.012, 0.09, 1.0, 2.5) });
    kit.push_back ({ 2, "SNARE 1", snare (190, 0.05, 0.11, 0.8, 1500) });
    kit.push_back ({ 3, "SNARE 2", snare (240, 0.03, 0.16, 1.1, 2500) });
    kit.push_back ({ 4, "CLAP",    clap() });
    kit.push_back ({ 5, "RIM",     tone (1700, 0.008, 820) });
    kit.push_back ({ 6, "CL HAT",  metallic (0.035, 6000) });
    kit.push_back ({ 7, "OP HAT",  metallic (0.16, 6000) });
    // Bank B: toms and percussion
    kit.push_back ({ 8,  "TOM LO", tom (95, 0.18) });
    kit.push_back ({ 9,  "TOM MID", tom (140, 0.15) });
    kit.push_back ({ 10, "TOM HI", tom (200, 0.12) });
    kit.push_back ({ 11, "CONGA L", tom (240, 0.08) });
    kit.push_back ({ 12, "CONGA H", tom (360, 0.06) });
    kit.push_back ({ 13, "COWBELL", tone (540, 0.07, 800, true) });
    kit.push_back ({ 14, "CLAVE",  tone (2500, 0.02) });
    kit.push_back ({ 15, "SHAKER", shaker (0.12, 0.03) });
    // Bank C: cymbals and extras
    kit.push_back ({ 16, "RIDE",   metallic (0.35, 3500, 0.8) });
    kit.push_back ({ 17, "CRASH",  metallic (0.45, 2500) });
    kit.push_back ({ 18, "TAMB",   shaker (0.25, 0.06) });
    kit.push_back ({ 19, "BOOM",   kick (90, 42, 0.04, 0.35, 0.2, 1.2) });
    kit.push_back ({ 20, "SNARE 3", snare (170, 0.07, 0.07, 0.6, 900) });
    kit.push_back ({ 21, "SNAP",   snare (900, 0.004, 0.03, 1.2, 3000) });
    kit.push_back ({ 22, "PERC",   tone (700, 0.04, 1050) });
    kit.push_back ({ 23, "ZAP",    zap() });
    // Bank D left empty for your own samples.

    for (auto& it : kit)
    {
        // trim the tail once it drops below the 12-bit floor, and keep every sound short
        size_t last = 0;
        for (size_t i = 0; i < it.buf.size(); ++i) if (std::fabs (it.buf[i]) > 0.008f) last = i;
        const bool longOne = it.slot == 7 || it.slot == 16 || it.slot == 17 || it.slot == 19;
        it.buf.resize (std::min<size_t> (last + 1, (size_t) ((longOne ? 0.6 : 0.42) * SR)));
        for (size_t i = 0, n = std::min<size_t> (it.buf.size(), 2000); i < n; ++i)   // tiny fade at the cut
            it.buf[it.buf.size() - 1 - i] *= (float) i / (float) n;
    }
    for (auto& it : kit)
        loadSound (it.slot, it.buf.data(), (int) it.buf.size(), SR, it.name);

    std::lock_guard<std::recursive_mutex> l (mtx);
    // Sensible default channel routing: kicks/toms through the dynamic filters, hats/cymbals unfiltered.
    const uint8_t chans[24] = { 0, 0, 2, 2, 4, 5, 6, 7,  1, 1, 1, 3, 3, 5, 5, 6,  7, 7, 6, 0, 2, 4, 5, 3 };
    for (int i = 0; i < 24; ++i) proj.working.channel[(size_t) i] = chans[i];
    for (auto& m : proj.mixes) m = proj.working;
    setLcd ("Factory Kit", "Loaded");
}
} // namespace c12
