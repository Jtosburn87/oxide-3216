// Engine tests: run synthetic audio through AudioEngine without an audio device.
// Usage: EngineTests            run the tests
//        EngineTests --devices  also list the audio drivers and devices on this PC

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PluginManager.h"
#include "Session.h"
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
        tape.wowFlutter->set (0.0f);   // wow and flutter move playback by a few samples, like real tape

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

    /** Stand-in for a VST3: gain plus a fixed latency (a pure delay), optionally with a mono input. */
    struct TestPlugin : juce::AudioProcessor
    {
        TestPlugin (float g, int latencySamples, bool monoIn = false)
            : AudioProcessor (BusesProperties().withInput ("In", monoIn ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)
                                               .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
              gain (g), delay (latencySamples), mono (monoIn)
        {
            setLatencySamples (delay);
        }

        bool isBusesLayoutSupported (const BusesLayout& l) const override
        {
            return l.getMainInputChannelSet() == (mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())
                && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
        }

        void prepareToPlay (double, int) override { lines.assign (2, std::vector<float> ((size_t) delay + 1, 0.0f)); pos = 0; }
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const int n = b.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                float in[2] = { b.getSample (0, i), mono ? b.getSample (0, i) : b.getSample (1, i) };
                for (int c = 0; c < 2; ++c)
                {
                    auto& line = lines[(size_t) c];
                    line[(size_t) pos] = in[c];
                    b.setSample (c, i, gain * line[(size_t) ((pos + 1) % (int) line.size())]);
                }
                pos = (pos + 1) % (delay + 1);
            }
        }

        const juce::String getName() const override { return "Test plugin"; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock& m) override { m.append (&gain, sizeof (gain)); }
        void setStateInformation (const void* d, int size) override { if (size == sizeof (gain)) std::memcpy (&gain, d, sizeof (gain)); }

        float gain;
        const int delay;
        const bool mono;
        std::vector<std::vector<float>> lines;
        int pos = 0;
    };

    juce::PluginDescription testDescription (const juce::String& name)
    {
        juce::PluginDescription d;
        d.name = name;
        d.pluginFormatName = "Test";
        return d;
    }

    /** Sends one click into interface input 1 and records main L for `blocks` blocks. */
    std::vector<float> clickResponse (AudioEngine& e, int blocks)
    {
        Buffers b (1, 2, 256);
        std::vector<float> out;
        for (int k = 0; k < blocks; ++k)
        {
            std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
            if (k == 2)
                b.in[0][10] = 0.25f;   // quiet enough that doubled paths stay under the -3 dBFS limiter
            b.run (e);
            out.insert (out.end(), b.out[0].begin(), b.out[0].end());
        }
        return out;
    }

    std::vector<int> peaksAbove (const std::vector<float>& x, float threshold)
    {
        std::vector<int> idx;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > threshold)
                idx.push_back ((int) i);
        return idx;
    }

    void testInserts()
    {
        std::puts ("Insert slots, aux returns, latency compensation");
        {
            SineRig r;
            r.e.masterInserts.setSlot (0, std::make_unique<TestPlugin> (0.5f, 0), testDescription ("Half"));
            check (nearDb (r.measure (1000.0f, 0.1f) / 0.1f, -6.02f, 0.05f), "master insert (gain 0.5) halves the output");
            r.e.masterInserts.setBypassed (0, true);
            check (nearDb (r.measure (1000.0f, 0.1f) / 0.1f, 0.0f, 0.05f), "bypass passes audio untouched");
            r.e.masterInserts.setBypassed (0, false);
            r.e.masterInserts.clearSlot (0);
            check (nearDb (r.measure (1000.0f, 0.1f) / 0.1f, 0.0f, 0.05f), "empty slot passes audio untouched");
        }
        {
            // Strip 1 feeds main directly AND through sub 1, which has a 100-sample plugin on it.
            AudioEngine e;
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
            mapOnly (e, 0, 0);
            e.inputStrips[0]->controls.sub[0]->set (1.0f);
            e.subInserts[0]->setSlot (0, std::make_unique<TestPlugin> (1.0f, 100), testDescription ("Delay 100"));
            clickResponse (e, 4);   // let the chain report its latency
            const auto out = clickResponse (e, 12);
            const auto p = peaksAbove (out, 0.1f);
            std::printf ("      sub plugin latency 100: click arrives at %s (plugin latency now %d)\n",
                         p.size() == 1 ? juce::String (p[0]).toRawUTF8() : "several places", e.getPluginLatency());
            for (auto i : p) std::printf ("      peak at %d = %.4f\n", i, out[(size_t) i]);
            check (p.size() == 1 && near (out[(size_t) p[0]], 0.25f * 2.0f * pan, 1.0e-3f), "direct and sub paths line up (one click, double height)");
            check (p.size() == 1 && p[0] == 2 * 256 + 10 + 100, "everything is delayed by the slowest chain (100 samples)");
            check (e.getPluginLatency() == 100 && e.tape.getLatency() >= 100, "overdub compensation includes the plugin delay");
        }
        {
            // Aux 1 send with a 64-sample "reverb" on the return.
            AudioEngine e;
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
            mapOnly (e, 0, 0);
            e.inputStrips[0]->controls.auxLevel[0]->set (1.0f);   // send x 1.5
            auto silent = peaksAbove (clickResponse (e, 12), 0.1f);
            check (silent.size() == 1, "no plugin on the return: the send is not added to main");

            e.auxInserts[0]->setSlot (0, std::make_unique<TestPlugin> (1.0f, 64), testDescription ("Verb"));
            clickResponse (e, 4);
            const auto out = clickResponse (e, 12);
            const auto p = peaksAbove (out, 0.1f);
            check (p.size() == 1 && near (out[(size_t) p[0]], 0.25f * (pan + 1.5f), 1.0e-3f), "aux return lines up with the dry signal");

            e.auxReturnPan[0]->set (-1.0f);
            clickResponse (e, 8);
            const auto left = clickResponse (e, 12);
            check (near (left[2 * 256 + 10 + 64], 0.25f * (pan + 1.5f), 1.0e-3f), "return panned left: the left side keeps the whole return");

            e.auxReturnPan[0]->set (1.0f);
            clickResponse (e, 8);   // let the left side of the return glide down
            const auto quiet = clickResponse (e, 12);
            check (near (quiet[2 * 256 + 10 + 64], 0.25f * pan, 1.0e-3f), "return panned right: only the (compensated) dry click on the left");
        }
        {
            // A plugin with a mono input on the stereo master: gets (L+R)/2, output on both sides.
            AudioEngine e;
            e.inputStrips[0]->controls.pan->set (-1.0f);
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
            mapOnly (e, 0, 0);
            e.masterInserts.setSlot (0, std::make_unique<TestPlugin> (1.0f, 0, true), testDescription ("Mono"));
            Buffers b (1, 2, 256);
            std::fill (b.in[0].begin(), b.in[0].end(), 0.4f);
            settle (e, b);
            check (near (b.out[0][100], 0.2f, 1.0e-3f) && near (b.out[1][100], 0.2f, 1.0e-3f), "mono-input plugin: (L+R)/2 in, both sides out");
        }
        {
            InsertChain chain ("Test");
            chain.prepare (48000.0, 512);
            chain.setSlot (0, std::make_unique<TestPlugin> (0.5f, 0), testDescription ("A"));
            chain.setSlot (2, std::make_unique<TestPlugin> (2.0f, 0), testDescription ("B"));
            chain.moveSlot (2, 1);
            check (chain.getDescription (1).name == "B" && chain.getDescription (2).name.isEmpty(), "move a plugin up");
            const auto xml = chain.toXml();
            check (xml->getNumChildElements() == 2 && xml->getChildElement (1)->getIntAttribute ("index") == 1, "chain saves its slots");
        }
    }

    void testPunchAndLoop()
    {
        std::puts ("Tape: auto-punch and loop");
        AudioEngine e;
        e.inputStrips[0]->controls.main->set (0.0f);   // hear tape (return 1) only
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
        mapOnly (e, 0, 0);
        auto& tape = e.tape;
        tape.characterOn = false;

        // An old take on track 1: 0.5 everywhere.
        std::vector<float> old (48000, 0.5f);
        tape.loadTrack (0, old.data(), (juce::int64) old.size());
        tape.setLength ((juce::int64) old.size());

        // Punch in at 10000, out at 20000, playing a new take of 0.25 from the start.
        tape.setInPoint (10000);
        tape.setOutPoint (20000);
        tape.punchOn = true;
        tape.arm[0]->set (1.0f);
        tape.setRecordEnabled (true);
        tape.service();
        tape.play();
        Buffers b (1, 2, 500);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.25f);
        float heardBefore = 0.0f, heardDuring = 0.0f;
        for (int k = 0; k < 96; ++k)   // 48000 samples
        {
            tape.service();
            b.run (e);
            if (k == 10) heardBefore = b.out[0][250] / pan;   // around sample 5250
            if (k == 30) heardDuring = b.out[0][250] / pan;   // around sample 15250
        }
        tape.stop();
        tape.setRecordEnabled (false);
        tape.arm[0]->set (0.0f);

        std::vector<float> got (48000);
        tape.readTrack (0, 0, got.data(), 48000);
        check (got[9999] == 0.5f && got[20000] == 0.5f && got[47999] == 0.5f, "nothing outside In..Out was touched");
        check (got[10480] == 0.25f && got[15000] == 0.25f && got[19519] == 0.25f, "the new take fills the region between the fades");
        constexpr int fade = 480;   // 10 ms at 48 kHz
        std::printf ("      punch-in fade: %.3f at the In point, %.3f halfway, %.3f after 10 ms\n",
                     got[10000], got[10000 + fade / 2], got[10000 + fade]);
        check (near (got[10000], 0.5f, 1.0e-3f) && near (got[10000 + fade / 2], 0.375f, 0.01f) && near (got[19999], 0.5f, 1.0e-3f),
               "10 ms crossfades at punch-in and punch-out");
        float maxStep = 0.0f;
        for (int i = 9990; i < 20010; ++i)
            maxStep = juce::jmax (maxStep, std::abs (got[(size_t) i + 1] - got[(size_t) i]));
        check (maxStep < 0.002f, "no jumps at the punch points (smooth)");
        check (near (heardBefore, 0.5f) && near (heardDuring, 0.25f), "armed track plays the old take before the punch, the input during it");

        // Loop: a ramp on track 2, In 1000, Out 3000.
        std::vector<float> ramp (8000);
        for (size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = (float) i / 8000.0f;
        tape.loadTrack (1, ramp.data(), (juce::int64) ramp.size());
        tape.setLength (48000);
        for (int r = 0; r < Config::numReturnStrips; ++r)
            e.returnStrips[r]->controls.main->set (r == 1 ? 1.0f : 0.0f);
        tape.punchOn = false;
        tape.setInPoint (1000);
        tape.setOutPoint (3000);
        tape.loopOn = true;
        tape.locate (0);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
        settle (e, b);   // let the return strip mutes settle (tape not rolling yet)
        tape.locate (0);
        tape.play();
        std::vector<float> stream;
        for (int k = 0; k < 12; ++k)   // 6000 samples
        {
            b.run (e);
            stream.insert (stream.end(), b.out[0].begin(), b.out[0].end());
        }
        tape.stop();
        check (near (stream[2999] / pan, ramp[2999]) && near (stream[3000] / pan, ramp[1000]) && near (stream[5000] / pan, ramp[1000]),
               "loop jumps from Out back to In on the exact sample");
        check (tape.getPosition() == 1000 + (6000 - 3000) % 2000, "position after looping");
        tape.loopOn = false;
    }

    /** Clicks every 4800 samples on track 1, played back: how much the click positions wander. */
    int wowWander (int speedIndex, bool wowOn)
    {
        AudioEngine e;
        e.inputStrips[0]->controls.main->set (0.0f);
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
        mapOnly (e, 0, 0);
        e.tape.speed->set ((float) speedIndex);
        e.tape.wowFlutter->set (wowOn ? 1.0f : 0.0f);
        std::vector<float> clicks (48000 * 4, 0.0f);
        for (size_t i = 2400; i < clicks.size(); i += 4800)
            clicks[i] = 0.5f;
        e.tape.loadTrack (0, clicks.data(), (juce::int64) clicks.size());
        e.tape.setLength ((juce::int64) clicks.size());
        e.tape.play();

        Buffers b (1, 2, 480);
        std::vector<float> stream;
        for (int k = 0; k < 400; ++k)
        {
            b.run (e);
            stream.insert (stream.end(), b.out[0].begin(), b.out[0].end());
        }

        int lo = 1 << 30, hi = -(1 << 30);
        for (size_t c = 2400; c + 4800 < stream.size(); c += 4800)
        {
            size_t best = c;
            for (size_t i = c; i < c + 200; ++i)
                if (std::abs (stream[i]) > std::abs (stream[best]))
                    best = i;
            lo = juce::jmin (lo, (int) (best - c));
            hi = juce::jmax (hi, (int) (best - c));
        }
        return hi - lo;
    }

    void testWinding()
    {
        std::puts ("Tape: rewind and fast forward wind up and brake like a real transport");
        AudioEngine e;
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
        auto& tape = e.tape;
        Buffers b (1, 2, 480);   // 10 ms blocks
        const double sr = 48000.0;
        const auto seconds = [&] { return (double) tape.getPosition() / sr; };
        const auto runFor = [&] (double s) { for (int i = 0; i < juce::roundToInt (s * 100.0); ++i) b.run (e); };
        const auto runUntilStopped = [&] { for (int i = 0; i < 6000 && tape.isWinding(); ++i) b.run (e); };

        tape.setLength ((juce::int64) (60.0 * sr));   // a minute of recording
        tape.locate (0);
        tape.fastForward();
        runFor (1.0);
        // Speeding up by 20x over 3 s: after 1 s it runs at 6.7x and has wound 3.3 s of tape.
        check (std::abs (tape.getWindSpeed() - 6.67f) < 0.1f && std::abs (seconds() - 3.33) < 0.1, "fast forward speeds up gradually");
        runFor (2.5);
        check (std::abs (tape.getWindSpeed() - 20.0f) < 0.01f, "full winding speed is 20x play speed");
        runUntilStopped();
        check (tape.getPosition() == tape.getLength() && ! tape.isWinding() && tape.getWindDirection() == 0,
               "fast forward arrives exactly at the end of the recording and stops");

        tape.rewind();
        runFor (1.0);
        check (std::abs (seconds() - (60.0 - 3.33)) < 0.1 && tape.getWindSpeed() < 0.0f, "rewind speeds up the same way");
        runFor (3.0);
        const double atStop = seconds();
        tape.stop();
        runUntilStopped();
        // From 20x, braking over 0.6 s: v^2 / 2b = 6 s of tape.
        check (std::abs ((atStop - seconds()) - 6.0) < 0.3 && tape.getWindSpeed() == 0.0f, "stop brakes the tape over about 6 s of tape");

        tape.rewind();
        runUntilStopped();
        check (tape.getPosition() == 0 && tape.getWindSpeed() == 0.0f, "rewind arrives exactly at the start and stops");

        tape.fastForward();
        runFor (0.5);
        tape.play();
        b.run (e);
        check (tape.isPlaying() && ! tape.isWinding(), "play takes over from a wind at once");
        tape.stop();

        tape.locate ((juce::int64) (30.0 * sr));
        tape.rewind();
        runFor (0.5);
        tape.locate (0);
        b.run (e);
        check (tape.getPosition() == 0 && ! tape.isWinding(), "locate cancels a wind");
    }

    void testWowFlutter()
    {
        std::puts ("Tape: wow and flutter");
        const int off = wowWander (0, false), slow = wowWander (0, true), fast = wowWander (2, true);
        std::printf ("      click timing wander over 4 s: W&F off %d samples, 7.5 ips %d, 30 ips %d\n", off, slow, fast);
        check (off == 0, "off: perfectly steady");
        check (slow >= 6 && slow <= 22, "7.5 ips: subtle wander (about +/-0.2 ms)");
        check (fast < slow && fast >= 1, "30 ips: steadier than 7.5 ips");
    }

    void testSessions()
    {
        std::puts ("Sessions: save, load, export");
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ConsoleTapeTestSession");
        folder.deleteRecursively();

        std::vector<float> take (100000);
        for (size_t i = 0; i < take.size(); ++i)
            take[i] = 0.3f * (float) std::sin ((double) i * 0.0523);   // ~400 Hz at -10 dBFS

        Session::TrackRevisions written {};
        {
            AudioEngine a;
            a.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
            a.projectName = "My Song";
            a.inputStrips[2]->controls.trim->set (7.5f);
            a.inputStrips[2]->eq.gain[FourBandEq::HMF]->set (-4.0f);
            a.returnStrips[5]->controls.fader->set (64.0f);
            a.busComp.ratio->set (4.0f);
            a.tape.speed->set (0.0f);
            a.inputStrips[0]->name = "Kick";
            a.subNames[1] = "Drums";
            a.auxNames[0] = "Plate";
            a.setPatch (AudioEngine::Patch::returnIn + 3, std::uint64_t (1) << (AudioEngine::Patch::auxOut + 1));
            a.tape.writeTrack (4, 0, take.data(), (int) take.size());
            a.tape.setLength ((juce::int64) take.size());
            a.tape.setInPoint (1000);
            a.tape.setOutPoint (5000);
            a.tape.loopOn = true;
            const auto r = Session::save (a, folder, &written);
            check (r.ok, "session saved");
            check (folder.getChildFile ("session.json").existsAsFile() && folder.getChildFile ("Track 05.wav").existsAsFile()
                   && ! folder.getChildFile ("Track 01.wav").exists(), "session.json plus a WAV for each track with audio");

            // Saving again without changes does not rewrite the track.
            const auto before = folder.getChildFile ("Track 05.wav").getLastModificationTime();
            juce::Thread::sleep (1100);
            Session::save (a, folder, &written);
            check (folder.getChildFile ("Track 05.wav").getLastModificationTime() == before, "unchanged tracks are not rewritten");
        }
        {
            AudioEngine b;
            b.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
            const auto r = Session::load (b, nullptr, folder);
            check (r.ok, "session loaded");
            check (b.projectName.toString() == "My Song", "project name");
            check (b.inputStrips[2]->controls.trim->get() == 7.5f && b.inputStrips[2]->eq.gain[FourBandEq::HMF]->get() == -4.0f
                   && b.returnStrips[5]->controls.fader->get() == 64.0f && b.busComp.ratio->get() == 4.0f && b.tape.speed->get() == 0.0f,
                   "controls restored");
            check (b.inputStrips[0]->name == "Kick" && b.inputStrips[1]->name == "Input 2", "strip names restored");
            check (b.subNames[1] == "Drums" && b.subNames[0] == "Sub 1" && b.auxNames[0] == "Plate" && b.auxNames[1] == "A2", "sub and aux names restored");
            check (b.getPatch (AudioEngine::Patch::returnIn + 3) == (std::uint64_t (1) << (AudioEngine::Patch::auxOut + 1)), "patch cables restored");
            check (b.tape.getLength() == (juce::int64) take.size() && b.tape.getInPoint() == 1000 && b.tape.getOutPoint() == 5000 && b.tape.loopOn,
                   "tape length and locators restored");
            std::vector<float> got (take.size());
            b.tape.readTrack (4, 0, got.data(), (int) got.size());
            check (got == take, "track audio restored bit for bit (32-bit float WAV)");

            // Export the mix (track 5 through return 5, everything else at defaults).
            const auto wav = folder.getChildFile ("mix.wav");
            const auto e = Session::exportMix (b, wav, 1.0, [] (double) { return true; });
            check (e.ok, "export finished");
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
            check (reader != nullptr && reader->bitsPerSample == 24 && reader->numChannels == 2, "24-bit stereo WAV");
            if (reader != nullptr)
            {
                check (reader->lengthInSamples == (juce::int64) take.size() + 48000, "length = tape + 1 s tail");
                float lo[2] {}, hi[2] {};
                juce::Range<float> ranges[2];
                reader->readMaxLevels (0, reader->lengthInSamples, ranges, 2);
                const float peak = juce::jmax (ranges[0].getEnd(), -ranges[0].getStart());
                std::printf ("      exported peak %.1f dBFS\n", toDb (peak));
                check (peak > 0.05f && peak <= AudioEngine::limiterCeiling + 1.0e-3f, "mix is in the file, under the -3 dBFS limiter");
                juce::ignoreUnused (lo, hi);
            }
            check (! b.tape.isPlaying() && b.tape.getPosition() == 0 && ! b.tape.exporting, "transport put back after export");
        }
        {
            AudioEngine c;
            c.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
            c.inputStrips[0]->controls.trim->set (10.0f);
            c.tape.writeTrack (0, 0, take.data(), 1000);
            Session::reset (c);
            check (c.inputStrips[0]->controls.trim->get() == 0.0f && c.tape.getLength() == 0 && ! c.tape.trackHasAudio (0), "new session clears everything");
        }
        folder.deleteRecursively();
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

    /** Loads a real VST3 onto the master insert and runs a sine through it. */
    void hostRealPlugin (const juce::String& path)
    {
        std::printf ("\nHosting %s\n", path.toRawUTF8());
        juce::AudioPluginFormatManager fm;
        fm.addFormat (new juce::VST3PluginFormat());
        juce::OwnedArray<juce::PluginDescription> found;
        fm.getFormat (0)->findAllTypesForFile (found, path);
        check (! found.isEmpty(), "plugin found");
        if (found.isEmpty())
            return;

        SineRig r;
        juce::String error;
        auto instance = fm.createPluginInstance (*found[0], 48000.0, Config::maxBlock, error);
        check (instance != nullptr, "plugin created");
        if (instance == nullptr)
        {
            std::printf ("      %s\n", error.toRawUTF8());
            return;
        }
        auto* raw = instance.get();
        r.e.masterInserts.setSlot (0, std::move (instance), *found[0]);
        std::printf ("      %s by %s: main bus %d in / %d out\n", found[0]->name.toRawUTF8(), found[0]->manufacturerName.toRawUTF8(),
                     raw->getMainBusNumInputChannels(), raw->getMainBusNumOutputChannels());

        const float g = r.measure (1000.0f, 0.1f) / 0.1f;
        std::printf ("      1 kHz at -20 dBFS through it: %+.1f dB, plugin latency %d samples, overdub compensation +%d\n",
                     toDb (g), raw->getLatencySamples(), r.e.getPluginLatency());
        check (std::isfinite (g) && g > 0.001f, "audio passes through the plugin");

        const auto xml = r.e.masterInserts.toXml();
        check (xml->getNumChildElements() == 1 && xml->getChildElement (0)->getChildByName ("STATE") != nullptr, "plugin state saved");
        r.e.masterInserts.clearSlot (0);
        check (r.e.masterInserts.numLoaded() == 0, "plugin removed cleanly");
    }

    /** Save a chain with a real plugin, restore it into a fresh chain, and check a missing plugin is reported. */
    void testRestoreInserts (const juce::String& realPluginPath)
    {
        std::puts ("\nRestoring saved inserts");
        juce::TemporaryFile settingsFile ("settings");
        juce::PropertiesFile props (settingsFile.getFile(), {});
        PluginManager pm (props);

        juce::OwnedArray<juce::PluginDescription> found;
        pm.formats.getFormat (0)->findAllTypesForFile (found, realPluginPath);
        if (found.isEmpty())
        {
            check (false, "real plugin found for the restore test");
            return;
        }

        InsertChain original ("Master");
        original.prepare (48000.0, Config::maxBlock);
        juce::String error;
        original.setSlot (2, pm.create (*found[0], 48000.0, Config::maxBlock, error), *found[0]);
        original.setBypassed (2, true);
        auto saved = original.toXml();

        // Add a slot for a plugin that is not installed.
        auto* ghost = saved->createNewChildElement ("SLOT");
        ghost->setAttribute ("index", 0);
        juce::PluginDescription missing;
        missing.name = "Imaginary Verb";
        missing.manufacturerName = "Nobody";
        missing.pluginFormatName = "VST3";
        missing.fileOrIdentifier = "C:\\nowhere\\Imaginary Verb.vst3";
        ghost->addChildElement (missing.createXml().release());

        InsertChain restored ("Master");
        restored.prepare (48000.0, Config::maxBlock);
        const auto notFound = pm.restore (restored, *saved, 48000.0, Config::maxBlock);
        std::printf ("      missing: %s\n", notFound.joinIntoString (", ").toRawUTF8());
        check (restored.getProcessor (2) != nullptr && restored.getDescription (2).name == found[0]->name, "real plugin restored into its slot");
        check (restored.isBypassed (2), "bypass restored");
        check (notFound.size() == 1 && notFound[0].contains ("Imaginary Verb"), "missing plugin reported by name");
        check (restored.getProcessor (0) == nullptr, "missing plugin's slot left empty");
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
    testInserts();
    testPunchAndLoop();
    testWinding();
    testWowFlutter();
    testSessions();
    testNoDeviceIsSafe();

    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--devices") listDevices();
        if (juce::String (argv[i]) == "--bench")   benchmark();
        if (juce::String (argv[i]) == "--vst3" && i + 1 < argc) hostRealPlugin (argv[++i]);
        if (juce::String (argv[i]) == "--restore" && i + 1 < argc) testRestoreInserts (argv[++i]);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
