// Analog output-stage models. Approximations of the SP-1200 channel filters:
//  ch 1-2: SSM2044 4-pole low-pass whose cutoff follows the decay envelope
//  ch 3-4: fixed low-pass, lower cutoff
//  ch 5-6: fixed low-pass, higher cutoff
//  ch 7-8: unfiltered (raw DAC with zero-order-hold imaging)
#pragma once

#include <cmath>

namespace c12
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double fs, double fc, double q)
    {
        fc = std::fmin (fc, fs * 0.49);
        const double w = 2.0 * kPi * fc / fs, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0; b2 = b0;
        a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
    }
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// 4-pole Butterworth as two biquads.
struct Butter4
{
    Biquad s1, s2;
    void set (double fs, double fc) { s1.setLowpass (fs, fc, 0.5412); s2.setLowpass (fs, fc, 1.3066); }
    void reset() { s1.reset(); s2.reset(); }
    float process (float x) { return s2.process (s1.process (x)); }
};

// Transistor/OTA-style 4-pole ladder with resonance (SSM2044 flavour), TPT one-poles.
struct Ladder4
{
    double s[4] {};
    double last = 0;
    double fs = 48000;
    void reset() { for (auto& v : s) v = 0; last = 0; }
    float process (float x, double fc, double reso /*0..1*/)
    {
        fc = std::fmin (std::fmax (fc, 20.0), fs * 0.45);
        const double g = std::tan (kPi * fc / fs);
        const double G = g / (1.0 + g);
        const double k = reso * 3.6;
        // unit-delay feedback with input saturation; adequate for the modest SSM2044 resonance
        double in = std::tanh (x - k * last) * (1.0 + 0.5 * k);
        for (int i = 0; i < 4; ++i)
        {
            const double v = (in - s[i]) * G;
            const double y = v + s[i];
            s[i] = y + v;
            in = y;
        }
        last = in;
        return (float) in;
    }
};

struct DcBlocker
{
    double x1 = 0, y1 = 0, r = 0.9995;
    void reset() { x1 = y1 = 0; }
    float process (float x) { const double y = x - x1 + r * y1; x1 = x; y1 = y; return (float) y; }
};

inline float softClip (float x) { return std::tanh (x); }
} // namespace c12
