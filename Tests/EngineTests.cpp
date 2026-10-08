// Engine tests: run synthetic audio through AudioEngine without an audio device.
// Usage: EngineTests            run the tests
//        EngineTests --devices  also list the audio drivers and devices on this PC

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include <cstdio>
#include <deque>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        if (! ok)
            ++failures;
    }

    bool near (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }

    juce::BigInteger channels (std::initializer_list<int> list)
    {
        juce::BigInteger b;
        for (int c : list)
            b.setBit (c);
        return b;
    }

    struct Buffers
    {
        Buffers (int numIn, int numOut, int numSamples)
            : in ((size_t) numIn, std::vector<float> ((size_t) numSamples, 0.0f)),
              out ((size_t) numOut, std::vector<float> ((size_t) numSamples, 1.0f)),   // garbage that must be overwritten
              n (numSamples)
        {
            for (auto& v : in)  inPtrs.push_back (v.data());
            for (auto& v : out) outPtrs.push_back (v.data());
        }

        void run (AudioEngine& e)
        {
            e.audioDeviceIOCallbackWithContext (inPtrs.data(), (int) inPtrs.size(), outPtrs.data(), (int) outPtrs.size(), n, {});
        }

        std::vector<std::vector<float>> in, out;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        int n;
    };

    constexpr float pan = 0.70710678f;

    /** Run enough blocks for the 5 ms control smoothing to settle completely. */
    void settle (AudioEngine& e, struct Buffers& b) { for (int i = 0; i < 40; ++i) b.run (e); }

    /** Fader position for a gain in dB (inverse of the fader law). */
    float faderFor (float db) { return 80.0f + db / 0.75f; }

    void mapOnly (AudioEngine& e, int consoleChannel, int deviceInput)
    {
        for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
            e.setInputSource (c, -1);
        e.setInputSource (consoleChannel, deviceInput);
    }

    //==========================================================================
    void testPassThrough()
    {
        std::puts ("Pass-through: input 1 -> strip 1 -> main L/R");
        AudioEngine e;
        e.prepare (48000.0, channels ({ 0, 1, 2, 3 }), channels ({ 0, 1 }));
        Buffers b (4, 2, 256);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.5f);
        b.run (e);
        check (near (b.out[0][0], 0.5f * pan) && near (b.out[0][255], 0.5f * pan), "left = 0.5 at -3 dB pan");
        check (near (b.out[1][100], 0.5f * pan), "right = 0.5 at -3 dB pan");
        check (near (e.takeInputPeak (0), 0.5f), "strip 1 meter reads 0.5");
        check (near (e.takeInputPeak (1), 0.0f), "strip 2 meter reads 0");
        check (near (e.takeInputPeak (0), 0.0f), "meter resets after reading");
        check (near (e.takeMainPeak (0), 0.5f * pan), "main L meter");
    }

    void testInputMappingWithInactiveChannels()
    {
        std::puts ("Input mapping when only some interface inputs are active");
        AudioEngine e;
        // Device inputs 0, 2 and 5 are active, so the callback receives them packed as slots 0, 1, 2.
        e.prepare (48000.0, channels ({ 0, 2, 5 }), channels ({ 0, 1 }));
        mapOnly (e, 3, 5);   // console channel 4 <- device input 6

        Buffers b (3, 2, 64);
        std::fill (b.in[2].begin(), b.in[2].end(), 0.25f);   // slot 2 = device input 5
        std::fill (b.in[0].begin(), b.in[0].end(), 0.9f);    // device input 0 is not routed anywhere
        b.run (e);
        check (near (b.out[0][10], 0.25f * pan), "only the routed input reaches main");
        check (near (e.takeInputPeak (3), 0.25f), "strip 4 meter");

        e.setInputSource (3, 1);   // device input 1 is inactive
        b.run (e);
        check (near (b.out[0][10], 0.0f), "inactive input gives silence");
    }

    void testOutputMapping()
    {
        std::puts ("Main outputs on interface outputs 3/4");
        AudioEngine e;
        e.prepare (44100.0, channels ({ 0 }), channels ({ 0, 1, 2, 3 }));
        e.setMainOutput (0, 2);
        e.setMainOutput (1, 3);
        Buffers b (1, 4, 128);
        std::fill (b.in[0].begin(), b.in[0].end(), 1.0f);
        b.run (e);
        check (near (b.out[0][5], 0.0f) && near (b.out[1][5], 0.0f), "outputs 1/2 are cleared");
        check (near (b.out[2][5], pan) && near (b.out[3][5], pan), "outputs 3/4 carry main");

        e.setMainOutput (1, -1);
        b.run (e);
        check (near (b.out[3][5], 0.0f), "unassigned right output is silent");
        check (e.takeMainPeak (1) > 0.7f, "main R meter still works when unassigned");
    }

    void testMainFaderToneAndLargeBlocks()
    {
        std::puts ("Main fader, test tone, blocks larger than the internal chunk");
        AudioEngine e;
        e.mainFader->set (faderFor (-6.0206f));
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        Buffers b (1, 2, 2048);
        std::fill (b.in[0].begin(), b.in[0].end(), 1.0f);
        b.run (e);
        check (near (b.out[0][0], 0.5f * pan) && near (b.out[0][2047], 0.5f * pan), "main fader -6 dB across a 2048-sample block");

        std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
        e.mainFader->set (80.0f);
        e.testToneOn.store (true);
        b.run (e);
        e.takeMainPeak (0);
        b.run (e);
        check (near (e.takeMainPeak (0), 0.125893f, 2.0e-3f), "test tone peaks at -18 dBFS");

        int crossings = 0;   // 440 Hz over 2048 samples at 48 kHz: about 18.8 cycles
        for (size_t i = 1; i < b.out[0].size(); ++i)
            if (b.out[0][i - 1] < 0.0f && b.out[0][i] >= 0.0f)
                ++crossings;
        check (crossings >= 18 && crossings <= 19, "test tone is 440 Hz");
    }

    void testStripControls()
    {
        std::puts ("Strip controls: trim, pan, mute, solo, main assign");
        AudioEngine e;
        auto& s1 = e.inputStrips[0]->controls;
        auto& s2 = e.inputStrips[1]->controls;
        s1.trim->set (6.0206f);
        s1.pan->set (-1.0f);
        e.prepare (48000.0, channels ({ 0, 1 }), channels ({ 0, 1 }));
        mapOnly (e, 0, 0);
        Buffers b (2, 2, 512);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.25f);
        b.run (e);
        check (near (b.out[0][300], 0.5f) && near (b.out[1][300], 0.0f), "trim +6 dB, pan hard left");

        s1.mute->set (1.0f);
        settle (e, b);
        check (near (b.out[0][511], 0.0f, 1.0e-5f), "mute silences main");
        s1.mute->set (0.0f);

        s2.solo->set (1.0f);   // another strip soloed: strip 1 leaves main but still feeds tape
        settle (e, b);
        check (near (b.out[0][511], 0.0f, 1.0e-5f), "solo on another strip cuts strip 1 from main");
        s1.solo->set (1.0f);
        settle (e, b);
        check (near (b.out[0][511], 0.5f), "soloed strip 1 is heard");
        s1.solo->set (0.0f);
        s2.solo->set (0.0f);

        s1.main->set (0.0f);
        settle (e, b);
        check (near (b.out[0][511], 0.0f, 1.0e-5f), "main assign off");
    }

    //==========================================================================
    /** Simulates an interface with a loopback cable from output 1 to input 2, and a round-trip
        latency of L samples: whatever the app writes to output 1 comes back on input 2 L samples later. */
    struct LoopbackRig
    {
        static constexpr int block = 256;
        AudioEngine e;
        Buffers b { 2, 2, block };
        std::deque<float> wire;   // output 1 -> input 2
        juce::int64 now = 0;      // stream time in samples

        explicit LoopbackRig (int latency)
        {
            e.prepare (48000.0, channels ({ 0, 1 }), channels ({ 0, 1 }), latency);
            wire.assign ((size_t) latency, 0.0f);
            e.setInputSource (0, 0);
            e.setInputSource (1, 1);
        }

        /** clickAt: stream time of a click on input 1 (-1 for none). */
        void run (int numSamples, juce::int64 clickAt = -1)
        {
            for (int done = 0; done < numSamples; done += block)
            {
                for (int i = 0; i < block; ++i)
                {
                    b.in[0][(size_t) i] = (now + i == clickAt) ? 1.0f : 0.0f;
                    b.in[1][(size_t) i] = wire.front();
                    wire.pop_front();
                }
                e.tape.service();
                b.run (e);
                for (int i = 0; i < block; ++i)
                    wire.push_back (b.out[0][(size_t) i]);
                now += block;
            }
        }

        juce::int64 peakPosition (int track, juce::int64 searchLength)
        {
            std::vector<float> data ((size_t) searchLength);
            e.tape.readTrack (track, 0, data.data(), (int) searchLength);
            juce::int64 best = -1;
            float bestValue = 0.0f;
            for (size_t i = 0; i < data.size(); ++i)
                if (std::abs (data[i]) > bestValue)
                {
                    bestValue = std::abs (data[i]);
                    best = (juce::int64) i;
                }
            return best;
        }
    };

    void testRecordingIsLatencyCompensated()
    {
        std::puts ("Tape: recording is compensated for interface latency (loopback cable test)");
        constexpr int L = 300;
        LoopbackRig rig (L);
        auto& tape = rig.e.tape;

        // Take 1: a click arriving on input 1 at stream time 10000 is recorded to track 1.
        tape.arm[0]->set (1.0f);
        tape.setRecordEnabled (true);
        tape.service();
        tape.play();
        rig.run (20000, 10000);
        tape.stop();
        tape.setRecordEnabled (false);
        tape.arm[0]->set (0.0f);
        rig.run (LoopbackRig::block);

        check (rig.peakPosition (0, 40000) == 10000 - L, "take 1: click written at its arrival time minus the round trip");
        check (tape.getLength() >= 19000, "tape length covers the take");

        // Take 2 (overdub): track 1 plays out of output 1 through the cable into input 2,
        // which is recorded on track 2. Monitoring of input 2 is kept off main so the cable cannot feed back.
        rig.e.inputStrips[1]->controls.main->set (0.0f);
        rig.e.returnStrips[1]->controls.main->set (0.0f);
        tape.arm[1]->set (1.0f);
        tape.locate (0);
        tape.setRecordEnabled (true);
        tape.service();
        tape.play();
        rig.run (20000);
        tape.stop();

        const auto p1 = rig.peakPosition (0, 40000), p2 = rig.peakPosition (1, 40000);
        std::printf ("      track 1 click at %lld, track 2 click at %lld\n", (long long) p1, (long long) p2);
        check (std::abs (p1 - p2) <= 1, "overdub lines up with the track it was played against (within 1 sample, through the tape character filters)");
        check (tape.getDroppedSamples() == 0, "no samples dropped");
    }

    void testTapeStorageAndTransport()
    {
        std::puts ("Tape: storage across chunk boundaries, auto-stop, erase");
        AudioEngine e;
        constexpr int L = 100;
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), L);
        mapOnly (e, 0, 0);
        auto& tape = e.tape;
        tape.characterOn = false;   // check storage bit for bit

        // Record a known ramp across the first chunk boundary (65536).
        Buffers b (1, 2, 500);
        tape.arm[0]->set (1.0f);
        tape.setRecordEnabled (true);
        tape.service();
        tape.play();
        juce::int64 t = 0;
        while (t < 70000)
        {
            for (int i = 0; i < b.n; ++i)
                b.in[0][(size_t) i] = (float) ((t + i) % 1000) / 1000.0f;
            tape.service();
            b.run (e);
            t += b.n;
        }
        tape.stop();
        tape.setRecordEnabled (false);
        tape.arm[0]->set (0.0f);

        std::vector<float> got (2000);
        tape.readTrack (0, 65536 - 1000, got.data(), 2000);
        bool exact = true;
        for (int i = 0; i < 2000; ++i)
            exact = exact && got[(size_t) i] == (float) ((65536 - 1000 + i + L) % 1000) / 1000.0f;
        check (exact, "recorded audio reads back bit-exact across a chunk boundary");
        check (tape.getLength() == 70000 - L, "length = recorded samples minus latency");

        // Play from near the end with record off: transport stops by itself at the end.
        tape.locate (tape.getLength() - 1000);
        tape.play();
        std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
        for (int i = 0; i < 4; ++i)
            b.run (e);
        check (! tape.isPlaying(), "playback stops at the end of the recording");

        tape.eraseAll();
        settle (e, b);
        b.run (e);
        tape.service();
        tape.readTrack (0, 65536 - 1000, got.data(), 2000);
        check (std::all_of (got.begin(), got.end(), [] (float x) { return x == 0.0f; }), "erase clears the tape");
        check (tape.getLength() == 0 && tape.getPosition() == 0, "erase resets length and position");
    }

    void testUnarmedPlaybackAndArmedMonitoring()
    {
        std::puts ("Tape: unarmed tracks play back, armed tracks monitor input");
        AudioEngine e;
        e.inputStrips[0]->controls.main->set (0.0f);   // hear only the return strip
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
        e.tape.characterOn = false;   // check plain monitoring and record level
        mapOnly (e, 0, 0);
        Buffers b (1, 2, 256);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.5f);

        b.run (e);
        check (near (b.out[0][100], 0.0f), "stopped and unarmed: return is silent");

        e.tape.arm[0]->set (1.0f);
        settle (e, b);
        check (near (b.out[0][100], 0.5f * pan), "armed: return strip hears the input");

        e.tape.recordLevel->set (6.0206f);
        settle (e, b);
        check (near (b.out[0][255], 1.0f * pan, 1.0e-3f), "record level +6 dB");
    }

    //==========================================================================
    /** Feeds a sine into interface input 1 (-> strip 1) and returns the steady-state peak at main L,
        divided by the centre pan gain, i.e. the strip's own gain. */
    struct SineRig
    {
        AudioEngine e;
        Buffers b { 1, 2, 256 };
        double phase = 0.0;

        SineRig() { e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 })); mapOnly (e, 0, 0); }

        void run (float freq, float amp, int blocks)
        {
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < b.n; ++i)
                {
                    b.in[0][(size_t) i] = amp * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * freq / 48000.0;
                }
                b.run (e);
            }
        }

        float measure (float freq, float amp, int settleBlocks = 100)
        {
            run (freq, amp, settleBlocks);   // settle (100 blocks = about half a second)
            float pk = 0.0f;
            for (int k = 0; k < 20; ++k)
            {
                run (freq, amp, 1);
                for (float x : b.out[0])
                    pk = juce::jmax (pk, std::abs (x));
            }
            return pk / pan;
        }
    };

    float toDb (float g) { return 20.0f * std::log10 (juce::jmax (g, 1.0e-9f)); }
    bool nearDb (float gain, float expectedDb, float tolDb) { return std::abs (toDb (gain) - expectedDb) <= tolDb; }

    void testEq()
    {
        std::puts ("Channel EQ and group EQ");
        {
            SineRig r;
            auto& eq = r.e.inputStrips[0]->eq;
            check (nearDb (r.measure (1000.0f, 0.01f) / 0.01f, 0.0f, 0.01f), "flat EQ is unity");
            eq.gain[FourBandEq::HMF]->set (15.0f);   // bell at 3 kHz
            const float g = r.measure (3000.0f, 0.01f) / 0.01f;
            std::printf ("      HMF +15 dB at 3 kHz measures %.2f dB\n", toDb (g));
            check (nearDb (g, 15.0f, 0.3f), "HMF bell gives +15 dB at its centre frequency");
            eq.gain[FourBandEq::HMF]->set (0.0f);

            eq.gain[FourBandEq::LF]->set (12.0f);    // shelf at 100 Hz
            check (nearDb (r.measure (25.0f, 0.01f) / 0.01f, 12.0f, 0.8f), "LF shelf +12 dB well below its corner");
            eq.gain[FourBandEq::LF]->set (0.0f);

            eq.gain[FourBandEq::HF]->set (-15.0f);   // shelf at 8 kHz
            check (nearDb (r.measure (20000.0f, 0.01f) / 0.01f, -15.0f, 1.5f), "HF shelf -15 dB well above its corner");
        }
        {
            SineRig r;
            auto& c = r.e.inputStrips[0]->controls;
            c.main->set (0.0f);
            c.sub[0]->set (1.0f);
            check (nearDb (r.measure (500.0f, 0.01f) / 0.01f, 0.0f, 0.05f), "strip reaches main through sub 1 at unity");
            r.e.groupEq[0]->gain[FourBandEq::LMF]->set (12.0f);   // bell at 500 Hz
            check (nearDb (r.measure (500.0f, 0.01f) / 0.01f, 12.0f, 0.3f), "sub 1 group EQ LMF +12 dB");
        }
    }

    void testGate()
    {
        std::puts ("Gate: threshold, range, fast attack pop");
        {
            SineRig r;
            auto& gate = r.e.inputStrips[0]->gate;
            gate.threshold->set (-30.0f);
            gate.range->set (-40.0f);
            check (nearDb (r.measure (1000.0f, 0.1f) / 0.1f, 0.0f, 0.1f), "signal above threshold passes");
            const float cut = r.measure (1000.0f, 0.001f, 400) / 0.001f;   // 120 ms release: give it 2 s
            std::printf ("      gate closed: %.1f dB\n", toDb (cut));
            check (nearDb (cut, -40.0f, 1.0f), "signal below threshold is cut by the range (-40 dB)");
            check (gate.popCount.load() == 0, "no pop without fast attack");
        }
        {
            SineRig r;
            auto& gate = r.e.inputStrips[0]->gate;
            gate.threshold->set (-30.0f);
            gate.fast->set (1.0f);
            r.run (1000.0f, 0.0f, 40);           // silence: gate closes
            r.run (1000.0f, 0.5f, 4);            // loud burst: gate opens with a pop
            check (gate.popCount.load() == 1, "loud transient into a closed gate fires one pop");
            r.run (1000.0f, 0.5f, 4);
            check (gate.popCount.load() == 1, "no further pops while the gate stays open");

            SineRig quiet;
            auto& g2 = quiet.e.inputStrips[0]->gate;
            g2.threshold->set (-40.0f);
            g2.fast->set (1.0f);
            quiet.run (1000.0f, 0.0f, 40);
            quiet.run (1000.0f, 0.05f, 4);       // -26 dBFS: opens, but below the -12 dBFS pop trigger
            check (g2.popCount.load() == 0, "quiet transient opens without a pop");
        }
    }

    void testFetCompressor()
    {
        std::puts ("1176-style compressor");
        SineRig r;
        auto& comp = r.e.inputStrips[0]->comp;
        comp.input->set (10.0f);   // +30 dB drive
        check (nearDb (r.measure (1000.0f, 0.1f) / 0.1f, 0.0f, 0.01f), "compressor out (default) is bypassed");

        comp.in->set (1.0f);
        // -20 dBFS + 30 dB drive = +10, 24 dB over the -14 threshold, ratio 4 -> 18 dB reduction -> about -8 dBFS.
        const float out = r.measure (1000.0f, 0.1f) * 0.1f / 0.1f;
        const float gr = comp.takeGainReduction();
        std::printf ("      ratio 4: output %.1f dBFS, gain reduction %.1f dB\n", toDb (out), gr);
        check (std::abs (toDb (out) - (-8.0f)) < 1.5f, "ratio 4 output level");
        check (std::abs (gr - 18.0f) < 1.5f, "ratio 4 gain reduction");

        comp.ratio->set (3.0f);   // 20:1
        r.measure (1000.0f, 0.1f);
        const float gr20 = comp.takeGainReduction();
        comp.all->set (1.0f);
        r.measure (1000.0f, 0.1f);
        const float grAll = comp.takeGainReduction();
        std::printf ("      ratio 20: %.1f dB, all buttons: %.1f dB\n", gr20, grAll);
        check (gr20 > gr + 3.0f && grAll > gr20, "higher ratios compress harder; all buttons hardest");

        comp.output->set (6.0f);
        const float louder = r.measure (1000.0f, 0.1f);
        comp.output->set (0.0f);
        const float base = r.measure (1000.0f, 0.1f);
        check (nearDb (louder / base, 6.0f, 0.2f), "output knob +6 dB");
    }

    void testSends()
    {
        std::puts ("Aux sends, pre/post");
        SineRig r;
        auto& c = r.e.inputStrips[0]->controls;
        c.auxLevel[0]->set (1.0f);
        r.measure (1000.0f, 0.1f);
        r.e.takeAuxPeak (0);
        r.run (1000.0f, 0.1f, 10);
        check (near (r.e.takeAuxPeak (0), 0.15f, 2.0e-3f), "aux 1 full send = level x 1.5");

        c.fader->set (0.0f);
        r.run (1000.0f, 0.1f, 40);
        r.e.takeAuxPeak (0);
        r.run (1000.0f, 0.1f, 10);
        check (r.e.takeAuxPeak (0) < 1.0e-4f, "post-fader send follows the fader down");

        c.auxPre[0]->set (1.0f);
        r.run (1000.0f, 0.1f, 40);
        r.e.takeAuxPeak (0);
        r.run (1000.0f, 0.1f, 10);
        check (near (r.e.takeAuxPeak (0), 0.15f, 2.0e-3f), "pre-fader send ignores the fader");
        check (r.e.takeAuxPeak (1) == 0.0f, "aux 2 untouched");
    }

    /** Gain of the tape path (armed track monitoring, heard through return strip 1 only). */
    float tapeGain (int speedIndex, float freq, float amp)
    {
        SineRig r;
        r.e.inputStrips[0]->controls.main->set (0.0f);
        r.e.tape.speed->set ((float) speedIndex);
        r.e.tape.arm[0]->set (1.0f);
        return r.measure (freq, amp) / amp;
    }

    void testTapeCharacter()
    {
        std::puts ("Tape character by speed");
        const float g30 = tapeGain (2, 1000.0f, 0.01f);
        std::printf ("      30 ips, 1 kHz small signal: %.2f dB\n", toDb (g30));
        check (nearDb (g30, 0.0f, 0.3f), "30 ips: small signals pass at about unity");

        const float b90 = tapeGain (0, 90.0f, 0.01f), b1k = tapeGain (0, 1000.0f, 0.01f), b10k = tapeGain (0, 10000.0f, 0.01f);
        std::printf ("      7.5 ips: 90 Hz %+.1f dB, 1 kHz %+.1f dB, 10 kHz %+.1f dB\n", toDb (b90), toDb (b1k), toDb (b10k));
        check (toDb (b90) - toDb (b1k) > 3.0f && toDb (b90) - toDb (b1k) < 5.0f, "7.5 ips: head bump about +4 dB at 90 Hz");
        check (toDb (b10k) - toDb (b1k) < -3.0f, "7.5 ips: highs rolled off (8 kHz shelf and 12 kHz low-pass)");

        const float hot75 = tapeGain (0, 1000.0f, 0.5f), hot30 = tapeGain (2, 1000.0f, 0.5f);
        std::printf ("      -6 dBFS into tape: 7.5 ips %.1f dB, 30 ips %.1f dB\n", toDb (hot75), toDb (hot30));
        check (toDb (hot75) - toDb (b1k) < -1.0f, "7.5 ips: hot signals saturate (asymmetric curve: the gentler half sets the peak)");
        check (hot30 > hot75, "30 ips saturates less than 7.5 ips");

        // Hiss: only while the tape rolls, only on tracks playing back.
        AudioEngine e;
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        for (int r = 1; r < Config::numReturnStrips; ++r)
            e.returnStrips[r]->controls.main->set (0.0f);   // listen to track 1 only
        Buffers b (1, 2, 512);
        const auto rmsDb = [&]
        {
            double sum = 0.0;
            int count = 0;
            for (int k = 0; k < 40; ++k)
            {
                b.run (e);
                for (float x : b.out[0]) { sum += (double) (x / pan) * (x / pan); ++count; }
            }
            return 10.0f * (float) std::log10 (sum / count + 1.0e-30);
        };
        e.tape.setRecordEnabled (true);   // keeps the empty tape rolling
        e.tape.play();
        const float hissDb = rmsDb();
        std::printf ("      hiss at 15 ips: %.1f dBFS RMS\n", hissDb);
        check (hissDb > -66.0f && hissDb < -58.0f, "15 ips hiss level (-57 dBFS peak noise)");
        e.tape.stop();
        rmsDb();
        check (rmsDb() < -150.0f, "no hiss when the tape is stopped");
    }

    /** Steady-state output peak of a BusCompressor fed with the same sine on both sides. */
    struct BusCompRig
    {
        ParamSet params;
        BusCompressor bc { params };
        std::vector<float> l = std::vector<float> (512), r = std::vector<float> (512);
        double phase = 0.0;

        BusCompRig() { bc.prepare (48000.0); }

        float measure (float freq, float amp, int blocks = 400)
        {
            float pk = 0.0f;
            for (int k = 0; k < blocks; ++k)
            {
                for (size_t i = 0; i < l.size(); ++i)
                {
                    l[i] = r[i] = amp * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * freq / 48000.0;
                }
                bc.process (l.data(), r.data(), (int) l.size());
                if (k >= blocks - 20)
                    for (float x : l)
                        pk = juce::jmax (pk, std::abs (x));
            }
            return pk;
        }
    };

    void testBusCompressor()
    {
        std::puts ("Stereo bus compressor");
        {
            BusCompRig r;
            check (nearDb (r.measure (1000.0f, 0.5f) / 0.5f, 0.0f, 0.001f), "out (default): audio untouched");

            r.bc.in->set (1.0f);
            r.bc.ratio->set (3.0f);   // 4:1
            // -6 dBFS against the -18 dBFS threshold (knob at 0): 12 dB over at 4:1 -> 9 dB of reduction -> -15 dBFS.
            const float out = r.measure (1000.0f, 0.5f);
            const float gr = r.bc.takeGainReduction();
            std::printf ("      4:1, -6 dBFS in: %.1f dBFS out, %.1f dB reduction\n", toDb (out), gr);
            check (std::abs (toDb (out) + 15.0f) < 0.7f, "4:1 output level");
            check (std::abs (gr - 9.0f) < 0.7f, "gain reduction for the needle");

            r.bc.threshold->set (20.0f);   // knob +20 -> 0 dBFS threshold
            check (nearDb (r.measure (1000.0f, 0.25f) / 0.25f, 0.0f, 0.1f), "threshold knob at +20: -12 dBFS passes");

            r.bc.threshold->set (0.0f);
            r.bc.makeup->set (6.0f);
            const float withMakeup = r.measure (1000.0f, 0.5f);
            check (nearDb (withMakeup / out, 6.0f, 0.2f), "make up +6 dB");

            r.bc.release->set (5.0f);   // auto
            check (std::abs (toDb (r.measure (1000.0f, 0.5f) / withMakeup)) < 0.5f, "auto release settles to the same steady level");
        }
        {
            // Side-chain HPF: a 40 Hz bass line triggers far less reduction with the HPF at 185 Hz.
            BusCompRig a, b;
            for (auto* rig : { &a, &b })
            {
                rig->bc.in->set (1.0f);
                rig->bc.ratio->set (5.0f);   // 10:1
            }
            b.bc.hpf->set (5.0f);
            const float plain = a.measure (40.0f, 0.5f), filtered = b.measure (40.0f, 0.5f);
            std::printf ("      40 Hz at 10:1: %.1f dB reduction without HPF, %.1f dB with 185 Hz HPF\n",
                         toDb (0.5f / plain), toDb (0.5f / filtered));
            check (toDb (0.5f / filtered) < toDb (0.5f / plain) - 6.0f, "side-chain HPF lets the bass through");
        }
    }

    void testMasterSection()
    {
        std::puts ("Master: limiter, sub fader, aux and sub outputs");
        {
            SineRig r;
            r.e.inputStrips[0]->controls.trim->set (20.0f);   // drive main far over 0 dBFS
            r.run (1000.0f, 1.0f, 100);
            r.e.takeMainPeak (0);
            float pk = 0.0f;
            for (int k = 0; k < 50; ++k)
            {
                r.run (1000.0f, 1.0f, 1);
                for (float x : r.b.out[0])
                    pk = juce::jmax (pk, std::abs (x));
            }
            std::printf ("      +20 dB over: output peak %.2f dBFS\n", toDb (pk));
            check (pk <= AudioEngine::limiterCeiling + 1.0e-6f, "nothing gets past the -3 dBFS limiter");
            check (r.e.takeLimiting(), "limiter indicator");
        }
        {
            SineRig r;
            auto& c = r.e.inputStrips[0]->controls;
            c.main->set (0.0f);
            c.sub[0]->set (1.0f);
            r.e.subFader[0]->set (faderFor (-6.0f));
            check (nearDb (r.measure (1000.0f, 0.01f) / 0.01f, -6.0f, 0.05f), "sub 1 fader -6 dB");
        }
        {
            AudioEngine e;
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1, 2, 3, 4, 5 }));
            mapOnly (e, 0, 0);
            auto& c = e.inputStrips[0]->controls;
            c.auxLevel[0]->set (1.0f);
            c.sub[1]->set (1.0f);
            e.setAuxOutput (0, 2);
            e.setSubOutput (1, 0, 4);
            e.setSubOutput (1, 1, 5);
            Buffers b (1, 6, 256);
            std::fill (b.in[0].begin(), b.in[0].end(), 0.1f);
            settle (e, b);
            check (near (b.out[2][200], 0.15f, 1.0e-3f), "aux 1 on interface output 3");
            check (near (b.out[3][200], 0.0f), "unassigned output 4 is silent");
            check (near (b.out[4][200], 0.1f * pan, 1.0e-3f) && near (b.out[5][200], 0.1f * pan, 1.0e-3f), "sub 2 on outputs 5/6");
        }
    }

    void testPatchBay()
    {
        using P = AudioEngine::Patch;
        std::puts ("Patch bay");
        const auto bit = [] (int source) { return std::uint64_t (1) << source; };

        AudioEngine e;
        e.prepare (48000.0, channels ({ 0, 1 }), channels ({ 0, 1, 2 }));
        e.setInputSource (0, 0);
        e.setInputSource (1, 1);
        for (int c = 2; c < AudioEngine::numConsoleInputs; ++c)
            e.setInputSource (c, -1);
        for (auto* s : e.inputStrips)  s->controls.main->set (0.0f);   // listen to the return bank only
        for (auto* s : e.returnStrips) s->controls.main->set (0.0f);
        e.tape.characterOn = false;
        e.tape.arm[0]->set (1.0f);                                     // track 1 passes its input through
        e.tape.arm[1]->set (1.0f);
        Buffers b (2, 3, 256);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.25f);
        std::fill (b.in[1].begin(), b.in[1].end(), 0.5f);

        const auto returnLevel = [&] (int r) { e.returnStrips[r]->takePeak(); settle (e, b); return e.returnStrips[r]->takePeak(); };

        check (near (returnLevel (0), 0.25f), "normalled: input 1 -> tape 1 -> return 1");

        // Channel 2's direct out into return strip in 1 breaks the tape 1 normal.
        e.setPatch (P::returnIn + 0, bit (P::channelOut + 1));
        check (near (returnLevel (0), 0.5f), "cable into return in 1 replaces tape out 1");

        // Two cables into one input are summed.
        e.setPatch (P::returnIn + 0, bit (P::channelOut + 0) | bit (P::channelOut + 1));
        check (near (returnLevel (0), 0.75f), "two cables into one input add up");

        // Channel 1 into tape in 2 breaks channel 2's normal to tape 2.
        e.setPatch (P::returnIn + 0, 0);
        e.setPatch (P::tapeIn + 1, bit (P::channelOut + 0));
        check (near (returnLevel (1), 0.25f), "cable into tape in 2 replaces channel 2");
        e.setPatch (P::tapeIn + 1, 0);
        check (near (returnLevel (1), 0.5f), "pulling the cable restores the normal");

        // Channel strip ins are not normalled: a cable adds to the interface input.
        e.setPatch (P::channelIn + 1, bit (P::channelOut + 0));
        check (near (returnLevel (1), 0.75f), "cable into channel strip in 2 adds to interface input 2");
        e.setPatch (P::channelIn + 1, 0);

        // Backwards patch: tape out 1 into channel strip in 3 (one block later), on to return 3 via tape 3.
        e.tape.arm[2]->set (1.0f);
        e.setPatch (P::channelIn + 2, bit (P::tapeOut + 0));
        check (near (returnLevel (2), 0.25f), "backwards patch (tape out 1 -> channel in 3) works");

        // Mono sub out = (L + R) / 2: channel 1 panned hard left into sub 1, patched into return in 4.
        e.setPatch (P::channelIn + 2, 0);
        auto& c1 = e.inputStrips[0]->controls;
        c1.sub[0]->set (1.0f);
        c1.pan->set (-1.0f);
        e.setPatch (P::returnIn + 3, bit (P::subOut + 0));
        check (near (returnLevel (3), 0.125f, 2.0e-3f), "sub 1 out is mono (L + R) / 2");

        // Main L patched back into a return (feedback-safe: main is off for every strip here).
        e.setPatch (P::returnIn + 4, bit (P::mainL));
        settle (e, b);
        check (std::isfinite (b.out[0][10]), "patching main back in stays stable");
    }

    void testNoDeviceIsSafe()
    {
        std::puts ("Callback with zero channels");
        AudioEngine e;
        e.prepare (48000.0, {}, {});
        Buffers b (0, 0, 64);
        b.run (e);
        check (true, "no crash");
    }

    /** Worst case: every strip with all EQ bands, gate, compressor and all sends on,
        16 tracks playing back, 64-sample blocks. Reports CPU use of one core. */
    void benchmark()
    {
        std::puts ("\nBenchmark (worst case, 64-sample blocks, 48 kHz):");
        AudioEngine e;
        auto all = [&] (auto fn) { for (auto* s : e.inputStrips) fn (*s); for (auto* s : e.returnStrips) fn (*s); };
        all ([] (ChannelStrip& s)
        {
            for (int b = 0; b < FourBandEq::numBands; ++b)
                s.eq.gain[b]->set (3.0f);
            s.controls.hpf->set (80.0f);
            s.gate.threshold->set (-50.0f);
            s.comp.in->set (1.0f);
            s.comp.input->set (5.0f);
            for (int k = 0; k < Config::numAux; ++k)
                s.controls.auxLevel[k]->set (0.5f);
            s.controls.sub[0]->set (1.0f);
        });
        for (auto* eq : e.groupEq)
            eq->gain[0]->set (2.0f);

        juce::BigInteger ins;
        ins.setRange (0, 16, true);
        e.prepare (48000.0, ins, channels ({ 0, 1 }));

        // Fill 16 tracks with 10 s of noise by recording, then play them back.
        Buffers b (16, 2, 64);
        juce::Random rnd (1);
        auto fill = [&] { for (auto& ch : b.in) for (auto& x : ch) x = rnd.nextFloat() * 0.2f - 0.1f; };
        for (int t = 0; t < TapeMachine::numTracks; ++t)
            e.tape.arm[(size_t) t]->set (1.0f);
        e.tape.setRecordEnabled (true);
        e.tape.play();
        const int blocks = 48000 * 10 / 64;
        for (int i = 0; i < blocks; ++i)
        {
            if (i % 64 == 0)
                e.tape.service();
            fill();
            b.run (e);
        }
        e.tape.stop();
        e.tape.setRecordEnabled (false);
        for (int t = 0; t < TapeMachine::numTracks; ++t)
            e.tape.arm[(size_t) t]->set (0.0f);
        e.tape.locate (0);
        e.tape.play();

        const auto start = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < blocks; ++i)
        {
            fill();
            b.run (e);
        }
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
        std::printf ("  10 s of audio processed in %.2f s: %.0f%% of one CPU core\n", secs, secs / 10.0 * 100.0);
    }

    void listDevices()
    {
        std::puts ("\nAudio drivers and devices on this PC:");
        juce::AudioDeviceManager dm;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        dm.createAudioDeviceTypes (types);
        for (auto* t : types)
        {
            t->scanForDevices();
            std::printf ("  %s\n", t->getTypeName().toRawUTF8());
            for (auto& name : t->getDeviceNames (false))
                std::printf ("      out: %s\n", name.toRawUTF8());
            if (t->hasSeparateInputsAndOutputs())
                for (auto& name : t->getDeviceNames (true))
                    std::printf ("      in:  %s\n", name.toRawUTF8());
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testPassThrough();
    testInputMappingWithInactiveChannels();
    testOutputMapping();
    testMainFaderToneAndLargeBlocks();
    testStripControls();
    testRecordingIsLatencyCompensated();
    testTapeStorageAndTransport();
    testUnarmedPlaybackAndArmedMonitoring();
    testEq();
    testGate();
    testFetCompressor();
    testSends();
    testTapeCharacter();
    testBusCompressor();
    testMasterSection();
    testPatchBay();
    testNoDeviceIsSafe();

    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--devices") listDevices();
        if (juce::String (argv[i]) == "--bench")   benchmark();
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
