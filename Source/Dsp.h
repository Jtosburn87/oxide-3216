#pragma once

#include <cmath>

/** Small real-time DSP helpers. Nothing here allocates. */
namespace Dsp
{
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    /** Fader law from the prototype: position 0..100, unity at 80, +15 dB at the top, off at 0. */
    inline float faderDb (float pos) noexcept   { return (pos - 80.0f) * 0.75f; }
    inline float faderGain (float pos) noexcept { return pos <= 0.0f ? 0.0f : dbToGain (faderDb (pos)); }

    /** Equal-power pan of a mono source, pan -1..+1. Centre gives -3 dB per side. */
    inline void panGains (float pan, float& left, float& right) noexcept
    {
        const float a = (pan + 1.0f) * 0.25f * 3.14159265f;
        left = std::cos (a);
        right = std::sin (a);
    }

    /** One-pole smoother for gains, so knob moves and mutes do not click. ~5 ms. */
    struct Smoothed
    {
        float current = 0.0f, coeff = 0.0f;

        void prepare (double sampleRate, float startValue, float timeMs = 5.0f) noexcept
        {
            coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * timeMs * 0.001));
            current = startValue;
        }

        float next (float target) noexcept
        {
            current += (target - current) * coeff;
            return current;
        }

        /** True when close enough to the target to skip per-sample smoothing. */
        bool settled (float target) const noexcept { return std::abs (target - current) < 1.0e-6f; }
    };

    /** Transposed direct form II biquad (RBJ cookbook coefficients). */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() noexcept { z1 = z2 = 0.0f; }

        void setHighPass (double sampleRate, double freq, double q) noexcept
        {
            const double w = 2.0 * 3.141592653589793 * freq / sampleRate;
            const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = (float) ((1.0 + cs) * 0.5 / a0);
            b1 = (float) (-(1.0 + cs) / a0);
            b2 = b0;
            a1 = (float) (-2.0 * cs / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
}
