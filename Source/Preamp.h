#pragma once

#include <cmath>
#include <algorithm>
#include "Dsp.h"

/** Console colour for a channel's preamp, voiced after a Neve 1073: a class A amp between an input
    and an output transformer. It runs straight after the trim, on every input and return strip.

    Level reference: 0 dBFS = +18 dBu, and the amp clips at +26 dBu (clipLevel below). At normal levels
    the stage is nearly clean, as a 1073 is; the colour shows when the trim drives it hard.

    - Amp: mostly 2nd harmonic with some 3rd. About 0.1 % 2nd and 0.05 % 3rd at +20 dBu with the trim at
      0 dB or below. Turning the trim up takes away negative feedback, as the 1073's gain switch does,
      so the harmonics rise to about 2.8x at +30 dB for the same output level.
    - Transformers: 3rd harmonic that grows as the frequency falls (core flux follows the low end):
      about 0.3 % at 40 Hz, +20 dBu, and almost nothing above a few hundred Hz.
    - Rails: a soft knee into the +26 dBu ceiling.
    - Exaggeration: over the top 10 dB of the trim everything above grows to 1.4x a real 1073
      (harmonics x1.4, a softer knee), so the trim fully up is a touch dirtier than the real thing.

    Harmonics stop at the 3rd below the knee, so this runs at the session rate without oversampling. */
class Preamp
{
public:
    static constexpr float clipLevel = 2.5118864f;   // +26 dBu with 0 dBFS = +18 dBu

    void prepare (double sampleRate, float trimDb, bool on) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        fluxCoeff = (float) (1.0 - std::exp (-twoPi * fluxHz / sampleRate));
        dcCoeff = (float) std::exp (-twoPi * dcHz / sampleRate);
        mix.prepare (sampleRate, on ? 1.0f : 0.0f, 20.0f);
        reset();
        setTrim (trimDb);
    }

    /** Once per block, from the trim knob (dB, -20..+30). */
    void setTrim (float trimDb) noexcept
    {
        const float fb = std::pow (10.0f, feedbackLossPerDb * std::max (0.0f, trimDb));
        const float ex = exaggeration (trimDb);
        a2 = amp2nd * fb * ex;
        k3 = 2.0f * amp3rd * fb * ex;
        kT = 2.0f * transformer3rd * ex;
        knee = 1.0f - kneeWidth * ex;
    }

    /** 1 at the trim fully up, 1 below the top 10 dB; smooth in between. */
    static float exaggeration (float trimDb) noexcept
    {
        const float t = std::clamp ((trimDb - 20.0f) / 10.0f, 0.0f, 1.0f);
        return 1.0f + 0.4f * t * t * (3.0f - 2.0f * t);
    }

    /** x is the trimmed signal. on: the console colour switch (crossfades over 20 ms). */
    float process (float x, bool on) noexcept
    {
        const float target = on ? 1.0f : 0.0f;
        const float m = mix.next (target);
        if (! on && m < 1.0e-6f)
        {
            if (flux != 0.0f || dcIn != 0.0f || dcOut != 0.0f)
                reset();
            return x;
        }

        const float v = x * (1.0f / clipLevel);

        // Transformers: only the low band (the core flux) bends, so the distortion rises as the frequency falls.
        flux += (v - flux) * fluxCoeff;
        const float vt = v - flux + flux / std::sqrt (1.0f + kT * flux * flux);

        // Amp: soft knee into the rails, then the class A curve (3rd: compresses; 2nd: one half leans).
        const float s = softClip (vt);
        const float y = s / std::sqrt (1.0f + k3 * s * s) + a2 * s * s;

        // Output coupling: the 2nd harmonic's DC offset goes, the clean signal stays untouched.
        const float diff = y * clipLevel - x;
        const float d = diff - dcIn + dcCoeff * dcOut;
        dcIn = diff;
        dcOut = d;
        return x + m * d;
    }

    void reset() noexcept { flux = dcIn = dcOut = 0.0f; }

private:
    // Voicing (see the class comment). For a sine of peak A (in clipLevel units): 2nd = a2 A / 2, 3rd = (k3 / 2) A^2 / 4.
    static constexpr float amp2nd = 0.004f;          // 0.1 % 2nd at A = 0.5 (+20 dBu)
    static constexpr float amp3rd = 0.008f;          // 0.05 % 3rd at A = 0.5
    static constexpr float feedbackLossPerDb = 0.015f;   // x2.8 at +30 dB of trim
    static constexpr float transformer3rd = 0.1f;    // about 0.3 % 3rd at 40 Hz, A = 0.5
    static constexpr double fluxHz = 50.0;
    static constexpr double dcHz = 5.0;
    static constexpr float kneeWidth = 0.3f;         // the knee starts 3 dB under the rails (4.7 dB at 1.4x)

    float softClip (float v) const noexcept
    {
        const float a = std::abs (v);
        if (a <= knee)
            return v;
        const float w = 1.0f - knee;
        return std::copysign (knee + w * std::tanh ((a - knee) / w), v);
    }

    float fluxCoeff = 0, dcCoeff = 0;
    float a2 = 0, k3 = 0, kT = 0, knee = 0.7f;
    float flux = 0, dcIn = 0, dcOut = 0;
    Dsp::Smoothed mix;
};
