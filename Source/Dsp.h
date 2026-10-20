// Small real-time DSP building blocks (no allocation in process calls).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace dt2
{
constexpr float kPi = 3.14159265358979f;

/** RBJ-cookbook biquad, transposed direct form II, stereo */
struct Biquad
{
    enum class Type { bypass, lowpass, highpass, bandpass, notch, peaking, lowshelf, highshelf };
    Type type = Type::bypass;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float zL1 = 0, zL2 = 0, zR1 = 0, zR2 = 0;

    void set (Type t, float sr, float freq, float q, float gainDb = 0)
    {
        type = t;
        if (t == Type::bypass) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        freq = std::clamp (freq, 10.0f, sr * 0.49f);
        q = std::max (q, 0.05f);
        const float w = 2 * kPi * freq / sr;
        const float cw = std::cos (w), sw = std::sin (w);
        const float alpha = sw / (2 * q);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        float B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case Type::lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::peaking:  B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Type::lowshelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::highshelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case Type::bypass: break;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    inline void process (float& l, float& r)
    {
        if (type == Type::bypass) return;
        const float yl = b0 * l + zL1;
        zL1 = b1 * l - a1 * yl + zL2;
        zL2 = b2 * l - a2 * yl;
        const float yr = b0 * r + zR1;
        zR1 = b1 * r - a1 * yr + zR2;
        zR2 = b2 * r - a2 * yr;
        l = yl;
        r = yr;
    }
    void reset() { zL1 = zL2 = zR1 = zR2 = 0; }
};

/** exponential approach like Web Audio's setTargetAtTime (tau in seconds) */
inline float approach (float cur, float target, float tau, float sr)
{
    if (tau <= 0) return target;
    return target + (cur - target) * std::exp (-1.0f / (tau * sr));
}

/** fixed-size stereo delay line with linear interpolation */
struct DelayLine
{
    std::vector<float> L, R;
    int w = 0;
    void prepare (int maxSamples) { L.assign ((size_t) maxSamples, 0); R.assign ((size_t) maxSamples, 0); w = 0; }
    void clear() { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); }
    inline void push (float l, float r) { L[(size_t) w] = l; R[(size_t) w] = r; if (++w >= (int) L.size()) w = 0; }
    inline float readL (float d) const { return read (L, d); }
    inline float readR (float d) const { return read (R, d); }
    inline float read (const std::vector<float>& b, float d) const
    {
        const int n = (int) b.size();
        float pos = (float) w - 1 - std::clamp (d, 0.0f, (float) n - 3);
        while (pos < 0) pos += (float) n;
        const int i0 = (int) pos;
        const float fr = pos - (float) i0;
        const int i1 = (i0 + 1) % n;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }
};

inline float driveShape (float x, float amount) // amount 0..127
{
    if (amount <= 0) return x;
    const float k = 1 + amount / 127.0f * 30.0f;
    return std::tanh (k * x) / std::tanh (k);
}
} // namespace dt2
