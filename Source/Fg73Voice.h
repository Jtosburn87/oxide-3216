#pragma once

#include <cmath>
#include <algorithm>
#include <array>

/** The preamp's "73" voice: a 1073-style preamp, voiced by measurement of a well-known 73 emulation
    (all its drive settings, tones 40 Hz .. 10 kHz from -50 dBFS to deep clipping, sweeps and bursts).
    What it does, in the order it happens:

    - Transformer colour: a 3rd harmonic that falls off above ~32 Hz (strong in the lows, almost nothing
      above 1 kHz) and a quieter 2nd that falls as 1/f. Both grow with level, then level off and fall
      before the clip (the transformer's hysteresis); a slow envelope (300 ms) sets how much.
    - A +0.27 dB low shelf near 116 Hz that shrinks as the level rises.
    - The amp: a hard, lopsided clip. The negative side cuts off at about +25.5 dBu (the Oxide voice clips at +26),
      the positive side gives way softly from about 1.9 dB above that up to about 6.2 dB above it. Driven hard, the stage's bias shifts
      down (blocking): the clip gets more lopsided, and after a big overload the signal ducks out for a
      moment before it recovers (about 150 ms after +40 dB over the rails). Most of the blocking is grid
      conduction: an earlier stage limits what reaches the coupling into the clipping stage, which charges
      while that stage's grid conducts (~35 ms) and drains slowly (~160 ms), so on heavy low notes the wave
      starts lopsided and settles within ~100 ms, as the original does.
    - The output: a low cut around 3-4 Hz with a phase lead into the low end, and a little more level as
      the gain goes up (+0.05 dB at gain 0, +1.4 dB at +30), as the original's drive does.

    It runs at the session rate without oversampling, as the original does (it aliases when it clips). */
class Fg73Voice
{
public:
    /** Internal units per sample unit (the drive calibration): the negative rail lands at about +25.5 dBu, by the Oxide voice's +26. */
    static constexpr double unitsPerSample = 7.1125 / 2.5118864;

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        constexpr double twoPi = 6.283185307179586;
        a3 = 1.0 - std::exp (-twoPi * f3 / sr);
        a2 = 1.0 - std::exp (-twoPi * f2 / sr);
        aShelf = 1.0 - std::exp (-twoPi * shelfHz / sr);
        envRel = std::exp (-1.0 / (sr * envMs * 0.001));
        biasRel = std::exp (-1.0 / (sr * biasMs * 0.001));
        chargeCoef = 1.0 - std::exp (-1.0 / (sr * chargeMs * 0.001));
        drainCoef = 1.0 - std::exp (-1.0 / (sr * drainMs * 0.001));
        // The output filter, fitted (4 poles, 4 zeros) to the measured response; Hz and Q.
        const double wz = twoPi * 0.75, wp = twoPi * 2.99;
        setSection (secA, 1.0, wz / -2.566, wz * wz, 1.0, wp / 0.5285, wp * wp);
        const double za = -twoPi * 0.69, zb = twoPi * 119.77, pa = twoPi * 4.23, pb = twoPi * 116.0;
        setSection (secB, 1.0, za + zb, za * zb, 1.0, pa + pb, pa * pb);
        reset();
    }

    void reset() noexcept
    {
        v3 = v2 = shelf = env = biasEnv = coupling = 0.0;
        secA.z1 = secA.z2 = secB.z1 = secB.z2 = 0.0;
        countdown = 0;
        updateCoefficients();
    }

    /** From the gain knob (dB): sets how hard the voice is driven inside (the knob's gain is applied before
        process, as for the Oxide voice) and the original's small rise in level with drive. */
    void setGain (float gainDb) noexcept
    {
        const double o = std::max (0.0, (double) gainDb + 20.0 * std::log10 (unitsPerSample));   // dB above the original's lowest drive
        outScale = std::pow (10.0, (5.63e-4 * o * o + 9.34e-6 * o * o * o) / 20.0) / unitsPerSample;
    }

    float process (float x) noexcept
    {
        const double u = (double) x * unitsPerSample;
        const double a = std::abs (u);
        env = std::max (a, env * envRel);
        if (--countdown < 0)
        {
            countdown = updateEvery - 1;
            updateCoefficients();
        }

        // Transformer: level-dependent 3rd and 2nd, the shelf shrinking with level.
        v3 += (u - v3) * a3;
        v2 += (u - v2) * a2;
        shelf += (u - shelf) * aShelf;
        double y = u + g3 * v3 * a + g2 * v2 * u - shelfCut * shelf;

        // Blocking: the bias moves down while the stage is driven past the rails.
        const double ab = std::max (0.0, u) * biasPositive + a * (1.0 - biasPositive);
        biasEnv = std::max (ab, biasEnv * biasRel);
        if (biasEnv > biasFrom)
            y -= biasPerUnit * std::pow (biasEnv - biasFrom, biasPower);

        // Grid blocking: an earlier stage gives way softly above stageRail, and the coupling into the clipping
        // stage charges quickly while that stage's grid conducts (above conductFrom), then drains slowly.
        if (std::abs (y) > stageRail)
            y = (y > 0.0 ? 1.0 : -1.0) * (stageRail + stageKnee * std::tanh ((std::abs (y) - stageRail) / stageKnee));
        const double v = y + coupling;
        if (v > conductFrom) coupling -= (v - conductFrom) * chargeCoef;
        else                 coupling -= coupling * drainCoef;

        y = clip (y + coupling);
        y = run (secA, y);
        y = run (secB, y);
        return (float) (y * hfGain * outScale);
    }

private:
    // Transformer (fitted at the original's lowest drive, where 1.0 = 0 dBFS in).
    static constexpr double f3 = 32.0, f2 = 2.0, c3 = 0.039, c2 = 0.00617;
    static constexpr double envMs = 300.0, shelfHz = 116.0, shelfDelta = 0.0325;
    // Level tables (dB re c3 / c2, and the shelf's share) at -60, -56 ... +36 dB of the envelope.
    static constexpr std::array<double, 25> t3 { 0.0, 0.0, 0.0, -0.04, -0.12, -0.2, -0.4, -0.6, -1.04, -1.72, -2.4, -4.0, -5.6,
                                                 -8.25, -11.567, -15.7, -20.8, -26.5, -32.5, -38.267, -44.0, -48.0, -52.0, -56.0, -60.0 };
    static constexpr std::array<double, 25> t2 { -1.0, -1.0, -1.0, -0.86, -0.58, -0.3, -0.18, -0.06, -0.008, -0.025, -0.042, -0.058, -0.075,
                                                 -0.092, -0.167, -1.5, -4.4, -8.0, -11.85, -15.8, -19.8, -23.8, -27.8, -31.8, -35.8 };
    static constexpr std::array<double, 25> tShelf { 1.0, 1.0, 1.0, 0.998, 0.993, 0.988, 0.979, 0.97, 0.953, 0.928, 0.903, 0.853, 0.802,
                                                     0.737, 0.665, 0.595, 0.537, 0.486, 0.448, 0.421, 0.4, 0.4, 0.4, 0.4, 0.4 };
    // Clip and blocking.
    static constexpr double negRail = 6.65, negKnee = 0.175, posKnee = 8.25, posRail = 13.55;
    static constexpr double biasPerUnit = 0.08, biasFrom = 8.2, biasMs = 170.0, biasPositive = 0.3, biasPower = 1.0;
    static constexpr double stageRail = 20.0, stageKnee = 2.75, conductFrom = 10.0, chargeMs = 35.0, drainMs = 156.0;
    static constexpr double hfGain = 0.99936;
    static constexpr int updateEvery = 8;

    static double lookup (const std::array<double, 25>& t, double db) noexcept
    {
        const double x = std::clamp ((db + 60.0) / 4.0, 0.0, 24.0);
        const int i = std::min (23, (int) x);
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * (x - i);
    }

    void updateCoefficients() noexcept
    {
        const double db = 20.0 * std::log10 (env + 1.0e-9);
        g3 = c3 * std::pow (10.0, lookup (t3, db) / 20.0);
        g2 = c2 * std::pow (10.0, lookup (t2, db) / 20.0);
        shelfCut = (1.0 - lookup (tShelf, db)) * shelfDelta;
    }

    static double clip (double y) noexcept
    {
        if (y < -negRail + negKnee)
            return -negRail + negKnee - negKnee * std::tanh (((-negRail + negKnee) - y) / negKnee);
        if (y > posKnee)
        {
            constexpr double w = posRail - posKnee;
            return posKnee + w * std::tanh ((y - posKnee) / w);
        }
        return y;
    }

    struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };

    /** Bilinear transform of (B2 s^2 + B1 s + B0) / (A2 s^2 + A1 s + A0). */
    void setSection (Section& s, double B2, double B1, double B0, double A2, double A1, double A0) const noexcept
    {
        const double k = 2.0 * sr, k2 = k * k, a0 = A2 * k2 + A1 * k + A0;
        s.b0 = (B2 * k2 + B1 * k + B0) / a0;
        s.b1 = (2.0 * B0 - 2.0 * B2 * k2) / a0;
        s.b2 = (B2 * k2 - B1 * k + B0) / a0;
        s.a1 = (2.0 * A0 - 2.0 * A2 * k2) / a0;
        s.a2 = (A2 * k2 - A1 * k + A0) / a0;
    }

    static double run (Section& s, double x) noexcept
    {
        const double y = s.b0 * x + s.z1;
        s.z1 = s.b1 * x - s.a1 * y + s.z2;
        s.z2 = s.b2 * x - s.a2 * y;
        return y;
    }

    double sr = 48000.0, a3 = 0, a2 = 0, aShelf = 0, envRel = 0, biasRel = 0;
    double v3 = 0, v2 = 0, shelf = 0, env = 0, biasEnv = 0, coupling = 0, chargeCoef = 0, drainCoef = 0;
    double g3 = 0, g2 = 0, shelfCut = 0, outScale = 1.0 / unitsPerSample;
    int countdown = 0;
    Section secA, secB;
};
