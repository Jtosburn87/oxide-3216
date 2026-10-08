#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

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

        void setLowPass (double sampleRate, double freq, double q) noexcept
        {
            const double w = 2.0 * 3.141592653589793 * freq / sampleRate;
            const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = (float) ((1.0 - cs) * 0.5 / a0);
            b1 = (float) ((1.0 - cs) / a0);
            b2 = b0;
            a1 = (float) (-2.0 * cs / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        /** Band-pass with 0 dB at the centre frequency. */
        void setBandPass (double sampleRate, double freq, double q) noexcept
        {
            const double w = 2.0 * 3.141592653589793 * freq / sampleRate;
            const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = (float) (alpha / a0);
            b1 = 0.0f;
            b2 = (float) (-alpha / a0);
            a1 = (float) (-2.0 * cs / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        /** Takes another filter's coefficients but keeps this filter's state (no click). */
        void copyCoefficients (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

        /** Shelves use the cookbook's shelf slope S = 1 (as the prototype's Web Audio filters do). */
        void setLowShelf (double sampleRate, double freq, double gainDb) noexcept  { setShelf (sampleRate, freq, gainDb, false); }
        void setHighShelf (double sampleRate, double freq, double gainDb) noexcept { setShelf (sampleRate, freq, gainDb, true); }

        void setPeak (double sampleRate, double freq, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * 3.141592653589793 * freq / sampleRate;
            const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha / A;
            b0 = (float) ((1.0 + alpha * A) / a0);
            b1 = (float) (-2.0 * cs / a0);
            b2 = (float) ((1.0 - alpha * A) / a0);
            a1 = b1;
            a2 = (float) ((1.0 - alpha / A) / a0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        void setShelf (double sampleRate, double freq, double gainDb, bool high) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * 3.141592653589793 * freq / sampleRate;
            const double cs = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
            const double sq = 2.0 * std::sqrt (A) * alpha;
            const double s = high ? -1.0 : 1.0;   // the high shelf mirrors the low shelf's cosine terms
            const double a0 = (A + 1.0) + s * (A - 1.0) * cs + sq;
            b0 = (float) (A * ((A + 1.0) - s * (A - 1.0) * cs + sq) / a0);
            b1 = (float) (s * 2.0 * A * ((A - 1.0) - s * (A + 1.0) * cs) / a0);
            b2 = (float) (A * ((A + 1.0) - s * (A - 1.0) * cs - sq) / a0);
            a1 = (float) (-s * 2.0 * ((A - 1.0) + s * (A + 1.0) * cs) / a0);
            a2 = (float) (((A + 1.0) + s * (A - 1.0) * cs - sq) / a0);
        }
    };

    /** Adds src * gain to dst, ramping the gain linearly from `from` to `to` across the block (no clicks). */
    inline void addRamped (float* dst, const float* src, float from, float to, int n) noexcept
    {
        if (from == to)
        {
            if (to != 0.0f)
                for (int i = 0; i < n; ++i)
                    dst[i] += src[i] * to;
            return;
        }
        const float step = (to - from) / (float) n;
        float g = from;
        for (int i = 0; i < n; ++i)
        {
            g += step;
            dst[i] += src[i] * g;
        }
    }

    /** Fixed-capacity delay for latency compensation. Allocated once; process() never allocates. */
    class DelayLine
    {
    public:
        static constexpr int capacity = 1 << 15;   // 32768 samples: up to ~0.68 s at 48 kHz

        DelayLine() : buffer ((size_t) capacity, 0.0f) {}

        /** Delays x in place by `delay` samples (clamped to the capacity). */
        void process (float* x, int n, int delay) noexcept
        {
            delay = delay < 0 ? 0 : (delay >= capacity ? capacity - 1 : delay);
            for (int i = 0; i < n; ++i)
            {
                buffer[(size_t) write] = x[i];
                x[i] = buffer[(size_t) ((write - delay) & (capacity - 1))];
                write = (write + 1) & (capacity - 1);
            }
        }

        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    private:
        std::vector<float> buffer;
        int write = 0;
    };

    /** Small fast random generator for noise in the audio thread (xorshift32). Returns -1..1. */
    struct Noise
    {
        unsigned int state = 0x9e3779b9u;
        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (int) state * (1.0f / 2147483648.0f);
        }
    };
}
