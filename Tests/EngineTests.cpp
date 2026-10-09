// Engine tests: run synthetic audio through AudioEngine without an audio device.
// Usage: EngineTests            run the tests
//        EngineTests --devices  also list the audio drivers and devices on this PC

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PluginManager.h"
#include "Session.h"
#include "AudioImport.h"
#include <cstdio>
#include <deque>
#include <vector>

// The benchmark can run at audio-thread priority. (Declared here: windows.h would break near().)
extern "C" __declspec (dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec (dllimport) int __stdcall SetThreadPriority (void*, int);
constexpr int THREAD_PRIORITY_TIME_CRITICAL = 15;

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

    /** The engine with its input faders at unity: they default to fully down, and the tests were written
        around signals passing straight through. */
    struct TestEngine : AudioEngine
    {
        TestEngine()
        {
            for (auto* s : inputStrips)
                s->controls.fader->set (80.0f);
            preampColour->set (0.0f);   // clean preamps, so levels are exact (testPreampColour covers the colour)
        }
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
        TestEngine e;
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
        TestEngine e;
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
        TestEngine e;
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
        TestEngine e;
        e.mainFader->set (faderFor (-6.0206f));
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        Buffers b (1, 2, 2048);
        std::fill (b.in[0].begin(), b.in[0].end(), 1.0f);
        b.run (e);
        check (near (b.out[0][0], 0.5f * pan) && near (b.out[0][2047], 0.5f * pan), "main fader -6 dB across a 2048-sample block");

        // The test oscillator, patched into return strip 1 (centre pan: -3 dB per side).
        std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
        e.mainFader->set (80.0f);
        e.oscFreq->set (440.0f);
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        e.setPatch (AudioEngine::Patch::returnIn, std::uint64_t (1) << AudioEngine::Patch::oscillator);
        for (int i = 0; i < 4; ++i)
            b.run (e);
        e.takeMainPeak (0);
        b.run (e);
        check (near (e.takeMainPeak (0), 0.125893f * pan, 2.0e-3f), "oscillator sine peaks at -18 dBFS (default level)");

        int crossings = 0;   // 440 Hz over 2048 samples at 48 kHz: about 18.8 cycles
        for (size_t i = 1; i < b.out[0].size(); ++i)
            if (b.out[0][i - 1] < 0.0f && b.out[0][i] >= 0.0f)
                ++crossings;
        check (crossings >= 18 && crossings <= 19, "oscillator at 440 Hz");
    }

    void testStripLink()
    {
        std::puts ("Linked channel pairs: everything follows, pan mirrors");
        AudioEngine e;
        auto& a = *e.inputStrips[2];   // channels 3 and 4
        auto& b = *e.inputStrips[3];
        auto& link = *e.inputLink[1];

        a.controls.trim->set (6.0f);
        a.controls.pan->set (-0.6f);
        a.eq.gain[FourBandEq::HF]->set (3.0f);
        b.controls.trim->set (-4.0f);
        b.controls.fader->set (30.0f);
        check (b.controls.trim->get() == -4.0f, "unlinked strips are independent");

        link.set (1.0f);
        check (b.controls.trim->get() == 6.0f && b.eq.gain[FourBandEq::HF]->get() == 3.0f && b.controls.fader->get() == a.controls.fader->get(),
               "linking copies channel 3's settings onto channel 4");
        check (near (b.controls.pan->get(), 0.6f), "linking mirrors the pan");

        b.controls.fader->set (65.0f);
        a.comp.ratio->set (2.0f);
        a.controls.mute->set (1.0f);
        a.controls.sub[1]->set (1.0f);
        b.controls.auxLevel[4]->set (0.4f);
        check (a.controls.fader->get() == 65.0f && b.comp.ratio->get() == 2.0f && b.controls.mute->on() && b.controls.sub[1]->on()
                   && a.controls.auxLevel[4]->get() == 0.4f, "either strip moves both (fader, compressor, mute, assigns, sends)");

        b.controls.pan->set (-0.25f);
        check (near (a.controls.pan->get(), 0.25f), "pan turns the other way");
        a.controls.pan->set (-1.0f);
        check (near (b.controls.pan->get(), 1.0f), "hard left on one is hard right on the other");

        check (e.inputStrips[1]->controls.fader->get() == 0.0f && e.inputStrips[4]->controls.fader->get() == 0.0f, "other strips untouched");

        link.set (0.0f);
        a.controls.trim->set (0.0f);
        check (b.controls.trim->get() == 6.0f, "unlinking makes them independent again");

        // Return strips link too.
        e.returnLink[0]->set (1.0f);
        e.returnStrips[0]->controls.fader->set (50.0f);
        check (e.returnStrips[1]->controls.fader->get() == 50.0f, "return strips 1 and 2 link");
    }

    void testOscillator()
    {
        std::puts ("Test oscillator: sine sweep, white and pink noise, patching");
        const auto rmsOf = [] (int type, float freq)
        {
            Dsp::Oscillator o;
            o.prepare (48000.0, freq);
            std::vector<float> buf (96000);
            o.process (buf.data(), (int) buf.size(), type, freq, 1.0f);
            double sum = 0.0;
            for (size_t i = 24000; i < buf.size(); ++i)
                sum += (double) buf[i] * buf[i];
            return (float) std::sqrt (sum / (double) (buf.size() - 24000));
        };
        const float sine = rmsOf (Dsp::Oscillator::sine, 1000.0f), white = rmsOf (Dsp::Oscillator::white, 1000.0f), pink = rmsOf (Dsp::Oscillator::pink, 1000.0f);
        std::printf ("      RMS at 0 dBFS: sine %.3f, white %.3f, pink %.3f\n", sine, white, pink);
        check (near (sine, pan, 0.002f) && near (white, pan, 0.02f) && near (pink, pan, 0.07f), "sine, white and pink read the same RMS");

        // Sweeping the knob glides the frequency without a jump in phase.
        Dsp::Oscillator o;
        o.prepare (48000.0, 100.0f);
        std::vector<float> buf (48000);
        o.process (buf.data(), 24000, Dsp::Oscillator::sine, 100.0f, 1.0f);
        o.process (buf.data() + 24000, 24000, Dsp::Oscillator::sine, 5000.0f, 1.0f);
        float maxStep = 0.0f;
        for (size_t i = 1; i < buf.size(); ++i)
            maxStep = juce::jmax (maxStep, std::abs (buf[i] - buf[i - 1]));
        int crossings = 0;
        for (size_t i = 36001; i < buf.size(); ++i)
            crossings += buf[i - 1] < 0.0f && buf[i] >= 0.0f ? 1 : 0;
        check (crossings >= 1249 && crossings <= 1251, "sweeps to 5 kHz (1250 cycles in the last quarter second)");
        check (maxStep < 0.7f, "no clicks while sweeping (largest step is the 5 kHz slope)");

        // In the console: one oscillator feeding a tape in and a channel strip in at once; silent for export.
        TestEngine e;
        e.oscType->set ((float) Dsp::Oscillator::pink);
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        e.setPatch (AudioEngine::Patch::channelIn + 1, std::uint64_t (1) << AudioEngine::Patch::oscillator);
        e.setPatch (AudioEngine::Patch::tapeIn + 2, std::uint64_t (1) << AudioEngine::Patch::oscillator);
        e.tape.arm[2]->set (1.0f);
        Buffers b (1, 2, 512);
        for (int i = 0; i < 10; ++i)
            b.run (e);
        e.takeInputPeak (1);
        b.run (e);
        check (e.takeInputPeak (1) > 0.03f, "pink noise reaches channel strip 2");
        check (e.takeInputPeak (0) == 0.0f, "nothing on channel strip 1");
        e.oscillatorMuted = true;
        for (int i = 0; i < 10; ++i)
            b.run (e);
        e.takeInputPeak (1);
        b.run (e);
        check (e.takeInputPeak (1) < 1.0e-4f, "muted for export");
    }

    void testStripControls()
    {
        std::puts ("Strip controls: trim, pan, mute, solo to the monitor outs, main assign");
        {
            AudioEngine fresh;
            check (fresh.inputStrips[0]->controls.fader->get() == 0.0f && fresh.inputStrips[15]->controls.fader->get() == 0.0f
                       && fresh.returnStrips[0]->controls.fader->get() == 80.0f,
                   "input faders start fully down, return faders at unity");
            fresh.inputStrips[3]->controls.fader->set (60.0f);
            fresh.inputStrips[3]->controls.fader->reset();
            check (fresh.inputStrips[3]->controls.fader->get() == 0.0f, "an input fader resets to fully down");
        }
        TestEngine e;
        auto& s1 = e.inputStrips[0]->controls;
        auto& s2 = e.inputStrips[1]->controls;
        s1.trim->set (6.0206f);
        s1.pan->set (-1.0f);
        e.prepare (48000.0, channels ({ 0, 1 }), channels ({ 0, 1, 2, 3 }));
        mapOnly (e, 0, 0);
        Buffers b (2, 4, 512);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.25f);
        b.run (e);
        check (near (b.out[0][300], 0.5f) && near (b.out[1][300], 0.0f), "trim +6 dB, pan hard left");

        // Meters: input after the trim, output after the fader.
        s1.fader->set (0.0f);
        settle (e, b);
        e.inputStrips[0]->takePeak();        // forget the fade-down
        e.inputStrips[0]->takeInputPeak();
        b.run (e);
        const float inPk = e.inputStrips[0]->takeInputPeak(), outPk = e.inputStrips[0]->takePeak();
        check (near (inPk, 0.5f) && near (outPk, 0.0f, 1.0e-5f),
               "input meter reads after the trim, output meter after the fader");
        s1.fader->set (80.0f);
        settle (e, b);

        s1.mute->set (1.0f);
        settle (e, b);
        check (near (b.out[0][511], 0.0f, 1.0e-5f), "mute silences main");
        s1.mute->set (0.0f);

        // Monitor outs on interface outputs 3/4: a copy of main, or the solo bus while anything is soloed.
        e.setMonitorOutput (0, 2);
        e.setMonitorOutput (1, 3);
        settle (e, b);
        check (near (b.out[2][511], 0.5f) && near (b.out[3][511], 0.0f), "monitor outs copy main when nothing is soloed");
        s2.solo->set (1.0f);   // another (silent) strip soloed
        settle (e, b);
        check (near (b.out[0][511], 0.5f), "solo never changes main");
        check (near (b.out[2][511], 0.0f, 1.0e-5f), "monitor outs play only the soloed strip");
        s1.solo->set (1.0f);
        settle (e, b);
        check (near (b.out[2][511], 0.5f) && near (b.out[3][511], 0.0f), "soloed strip 1 is on the monitor outs, after fader and pan");
        s1.solo->set (0.0f);
        s2.solo->set (0.0f);
        settle (e, b);
        check (near (b.out[2][511], 0.5f), "monitor outs back to main when solo is released");
        e.monitorLevel->set (-6.0206f);
        settle (e, b);
        check (near (b.out[2][511], 0.25f) && near (b.out[0][511], 0.5f), "monitor level -6 dB turns down the monitor outs only");
        e.monitorLevel->set (-60.0f);
        settle (e, b);
        check (near (b.out[2][511], 0.0f, 1.0e-5f), "monitor level fully down is off");
        e.monitorLevel->set (0.0f);
        e.setMonitorOutput (0, -1);
        e.setMonitorOutput (1, -1);

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
        TestEngine e;
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
        TestEngine e;
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
        TestEngine e;
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
        TestEngine e;
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
        {
            // Chain order: an EQ boost ahead of the gate opens it; with the gate first, it stays shut.
            const auto level = [] (int order)
            {
                auto rig = std::make_unique<SineRig>();   // an engine is too big for another one on the stack
                auto& r = *rig;
                auto& s = *r.e.inputStrips[0];
                s.gate.threshold->set (-30.0f);
                s.gate.range->set (-40.0f);
                s.eq.freq[FourBandEq::HMF]->set (1000.0f);
                s.eq.gain[FourBandEq::HMF]->set (15.0f);
                s.controls.order->set ((float) order);
                return toDb (r.measure (1000.0f, 0.01f, 400) / 0.01f);
            };
            const float eqFirst = level (0), gateFirst = level (2);   // EQ-gate-comp, gate-EQ-comp
            std::printf ("      EQ first: %+.1f dB, gate first: %+.1f dB\n", eqFirst, gateFirst);
            check (eqFirst > 10.0f && gateFirst < eqFirst - 30.0f, "chain order: EQ before the gate opens it, gate first keeps it shut");
        }
    }

    void testDeEsser()
    {
        std::puts ("De-esser (return strips)");
        // Feeds a 1 s mix of sines through a de-esser; returns the level (dB) of what comes out after the
        // first half second, optionally through a low-pass that keeps only the part below 1 kHz.
        const auto run = [] (std::vector<std::pair<float, float>> sines, const std::function<void (DeEsser&)>& setup, bool lowOnly = false)
        {
            ParamSet params;
            DeEsser d (params, "t");
            setup (d);
            d.prepare (48000.0);
            Dsp::Biquad low;
            low.setLowPass (48000.0, 1000.0, 0.7071);
            double in = 0.0, out = 0.0;
            for (int i = 0; i < 48000; ++i)
            {
                if (i % 64 == 0)
                    d.beginBlock();
                float x = 0.0f, xl = 0.0f;
                for (auto [f, a] : sines)
                {
                    const float s = a * (float) std::sin (2.0 * 3.141592653589793 * f * i / 48000.0);
                    x += s;
                    if (f < 1000.0f) xl += s;
                }
                float y = d.process (x);
                if (lowOnly)
                    y = low.process (y);
                if (i >= 24000)
                {
                    in += (double) (lowOnly ? xl : x) * (lowOnly ? xl : x);
                    out += (double) y * y;
                }
            }
            return 10.0f * (float) std::log10 (out / in);
        };
        const auto defaults = [] (DeEsser& d) { d.on->set (1.0f); };

        const float ess = run ({ { 6000.0f, 0.1f } }, defaults);
        const float quietEss = run ({ { 6000.0f, 0.01f } }, defaults);
        const float vowel = run ({ { 300.0f, 0.1f } }, defaults);
        std::printf ("      6 kHz: %.1f dB (quiet: %.1f dB), 300 Hz: %.1f dB\n", ess, quietEss, vowel);
        check (std::abs (ess + 8.0f) < 0.6f, "sibilance is turned down by the reduction setting (8 dB)");
        check (std::abs (quietEss - ess) < 0.6f, "it acts the same on a quiet take");
        check (vowel > -0.3f, "low-frequency material passes untouched");

        const float less = run ({ { 6000.0f, 0.1f } }, [] (DeEsser& d) { d.on->set (1.0f); d.reduction->set (3.0f); });
        check (std::abs (less + 3.0f) < 0.6f, "less reduction turns it down less");

        const float above = run ({ { 9000.0f, 0.1f } }, defaults);
        const float aboveWide = run ({ { 9000.0f, 0.1f } }, [] (DeEsser& d) { d.on->set (1.0f); d.wide->set (1.0f); });
        std::printf ("      9 kHz with the band at 6 kHz: narrow %.1f dB, wide %.1f dB\n", above, aboveWide);
        check (aboveWide < above - 2.0f && aboveWide < -6.0f, "wide catches sibilance above the frequency, narrow less so");

        // A vowel and an "s" together: full turns the vowel down too, gentle leaves it alone.
        const std::vector<std::pair<float, float>> both { { 300.0f, 0.1f }, { 6000.0f, 0.1f } };
        const float full = run (both, defaults, true);
        const float gentle = run (both, [] (DeEsser& d) { d.on->set (1.0f); d.gentle->set (1.0f); }, true);
        std::printf ("      300 Hz under an s: full %.1f dB, gentle %.1f dB\n", full, gentle);
        check (full < -2.0f && gentle > -0.5f, "full turns the whole signal down, gentle only the sibilant band");

        ParamSet params;
        DeEsser d (params, "t");
        d.on->set (1.0f);
        d.bypass->set (1.0f);
        check (! d.isActive(), "bypass takes it out");
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
        TestEngine e;
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

    /** A tape machine on its own (tape character off), 16 tracks of input and output. */
    struct TapeRig
    {
        ParamSet params;
        TapeMachine tape { params };
        std::vector<std::vector<float>> in, out;
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        double phase = 0.0;
        static constexpr int block = 512;

        TapeRig()
            : in ((size_t) TapeMachine::numTracks, std::vector<float> ((size_t) block)),
              out ((size_t) TapeMachine::numTracks, std::vector<float> ((size_t) block))
        {
            for (auto& v : in)  inPtrs.push_back (v.data());
            for (auto& v : out) outPtrs.push_back (v.data());
            tape.prepare (48000.0, 0);
            tape.characterOn = false;
        }

        /** Runs blocks, feeding a sine into one track's input (or none); returns the frequency heard on
            another track, from its rising zero crossings. */
        double run (int blocks, int listenTrack, int inputTrack = -1, double inputHz = 0.0)
        {
            int crossings = 0;
            float last = 0.0f;
            for (int k = 0; k < blocks; ++k)
            {
                for (auto& v : in) std::fill (v.begin(), v.end(), 0.0f);
                if (inputTrack >= 0)
                    for (int i = 0; i < block; ++i, phase += juce::MathConstants<double>::twoPi * inputHz / 48000.0)
                        in[(size_t) inputTrack][(size_t) i] = 0.5f * (float) std::sin (phase);
                tape.service();
                tape.process (inPtrs.data(), outPtrs.data(), block);
                for (float x : out[(size_t) listenTrack])
                {
                    crossings += (last < 0.0f && x >= 0.0f) ? 1 : 0;
                    last = x;
                }
            }
            return crossings * 48000.0 / (blocks * block);
        }
    };

    void testSpeedAndPitch()
    {
        std::puts ("Tape speed and pitch: played back at the speed it was recorded at, or not");
        auto rig = std::make_unique<TapeRig>();
        auto& tape = rig->tape;

        // Track 1: 1 kHz, imported at 15 ips (a blank tape takes the speed of its first recording).
        tape.speed->set (1.0f);
        std::vector<float> sine (48000 * 8);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / 48000.0);
        tape.writeTrack (0, 0, sine.data(), (int) sine.size());
        tape.setLength ((juce::int64) sine.size());
        check (tape.getRecordedIps() == 15.0f, "a blank tape takes the speed of its first recording (15 ips)");

        const auto heard = [&] (int speedIndex, float pitch)
        {
            tape.speed->set ((float) speedIndex);
            tape.pitch->set (pitch);
            tape.locate (0);
            tape.play();
            rig->run (100, 0);                             // the speed glides there (about a second)
            const auto before = tape.getPosition();
            const double hz = rig->run (40, 0);
            const double moved = (double) (tape.getPosition() - before) / (40.0 * TapeRig::block);
            float pk = 0.0f;
            for (float x : rig->out[0]) pk = juce::jmax (pk, std::abs (x));
            tape.stop();
            std::printf ("      %s ips, pitch %+.0f %%: %.1f Hz, peak %.3f, tape moves %.3f samples per sample\n",
                         TapeMachine::speeds[speedIndex].name, pitch, hz, pk, moved);
            check (std::abs (pk - 0.5f) < 0.03f, "  level kept through the resampler (0.5)");
            return std::pair { hz, moved };
        };

        auto [hz15, rate15] = heard (1, 0.0f);
        check (std::abs (hz15 - 1000.0) < 15.0 && rate15 == 1.0, "at the recorded speed: 1 kHz, one tape sample per sample");
        auto [hz30, rate30] = heard (2, 0.0f);
        check (std::abs (hz30 - 2000.0) < 25.0 && std::abs (rate30 - 2.0) < 0.01, "15 ips tape at 30 ips: an octave up, twice as fast");
        auto [hz75, rate75] = heard (0, 0.0f);
        check (std::abs (hz75 - 500.0) < 12.0 && std::abs (rate75 - 0.5) < 0.01, "15 ips tape at 7.5 ips: an octave down, half as fast");
        auto [hzUp, rateUp] = heard (1, 10.0f);
        check (std::abs (hzUp - 1100.0) < 15.0 && std::abs (rateUp - 1.1) < 0.01, "pitch +10 %: 1.1 kHz");

        // Overdub at 30 ips onto the 15 ips tape: in tune at 30, an octave down back at 15.
        tape.speed->set (2.0f);
        tape.pitch->set (0.0f);
        tape.arm[1]->set (1.0f);
        tape.locate (0);
        tape.setRecordEnabled (true);
        tape.play();
        rig->run (300, 1, 1, 1000.0);   // 3.2 s of 1 kHz into track 2
        tape.stop();
        tape.setRecordEnabled (false);
        tape.arm[1]->set (0.0f);
        check (tape.getRecordedIps() == 15.0f, "the tape keeps its recorded speed through an overdub at another speed");

        tape.locate (0);
        tape.play();
        rig->run (100, 1);
        const double at30 = rig->run (40, 1);
        tape.stop();
        tape.speed->set (1.0f);
        tape.locate (0);
        tape.play();
        rig->run (100, 1);
        const double at15 = rig->run (40, 1);
        tape.stop();
        std::printf ("      overdub made at 30 ips: %.1f Hz at 30, %.1f Hz at 15\n", at30, at15);
        check (std::abs (at30 - 1000.0) < 15.0 && std::abs (at15 - 500.0) < 10.0, "overdub at 30 ips: 1 kHz at 30, 500 Hz at 15");

        tape.eraseAll();
        check (tape.getRecordedIps() == 0.0f, "erasing the tape forgets its speed");
    }

    void testEraseSections()
    {
        std::puts ("Erase: a section of a track, whole tracks");
        auto rig = std::make_unique<TapeRig>();
        auto& tape = rig->tape;
        std::vector<float> ones (48000, 1.0f);
        for (int t = 0; t < 3; ++t)
            tape.writeTrack (t, 0, ones.data(), (int) ones.size());
        tape.setLength (48000);

        tape.eraseSection (0, 10000, 20000);
        std::vector<float> got (48000);
        tape.readTrack (0, 0, got.data(), 48000);
        const int fade = 240;   // 5 ms at 48 kHz
        check (got[9999] == 1.0f && got[20000] == 1.0f, "audio either side of the section is untouched");
        check (std::all_of (got.begin() + 10000 + fade, got.begin() + 20000 - fade, [] (float x) { return x == 0.0f; }), "the section is silent");
        check (got[10000] > 0.99f && got[10000 + fade / 2] > 0.4f && got[10000 + fade / 2] < 0.6f && got[20000 - 1] > 0.99f,
               "5 ms fades at the section's edges");
        tape.readTrack (1, 0, got.data(), 48000);
        check (std::all_of (got.begin(), got.end(), [] (float x) { return x == 1.0f; }), "other tracks untouched");

        tape.eraseTrack (1);
        tape.service();
        check (! tape.trackHasAudio (1) && tape.trackHasAudio (0) && tape.getLength() == 48000, "erasing one whole track leaves the rest and the length");
        tape.eraseTrack (0);
        tape.eraseTrack (2);
        check (tape.getLength() == 0 && tape.getRecordedIps() == 0.0f, "erasing the last track with audio leaves a blank tape");
    }

    void testTapeOutMeters()
    {
        std::puts ("Meter bridge: each track's tape out peak, read and reset");
        SineRig r;
        r.e.tape.arm[0]->set (1.0f);
        r.measure (1000.0f, 0.25f);
        for (int t = 0; t < TapeMachine::numTracks; ++t)
            r.e.tape.takeOutputPeak (t);
        r.run (1000.0f, 0.25f, 10);
        const float out1 = r.e.tape.takeOutputPeak (0);
        std::printf ("      tape out meter, track 1 (armed, -12 dBFS in): %.3f\n", out1);
        check (out1 > 0.2f && out1 < 0.3f && r.e.tape.takeOutputPeak (1) == 0.0f, "tape out meters: armed track 1 reads, track 2 silent");
        check (r.e.tape.takeOutputPeak (0) == 0.0f, "tape out meter resets after reading");
    }

    /** Harmonic levels (relative to the fundamental) of a sine through a Preamp. amp is in clipLevel units
        (0.5 = +20 dBu). Measured over whole cycles after half a second of settling. */
    struct PreampHarmonics { float h2, h3; };
    PreampHarmonics preampHarmonics (float trimDb, double freq, float amp)
    {
        constexpr double sr = 48000.0, twoPi = juce::MathConstants<double>::twoPi;
        Preamp p;
        p.prepare (sr, trimDb, true);
        const int warm = 24000, len = 24000;   // 0.5 s each: whole cycles at 1 kHz and 40 Hz
        double re[4] {}, im[4] {};
        for (int i = 0; i < warm + len; ++i)
        {
            const float y = p.process (amp * Preamp::clipLevel * (float) std::sin (twoPi * freq * i / sr), true);
            if (i >= warm)
                for (int h = 1; h <= 3; ++h)
                {
                    re[h] += y * std::cos (twoPi * h * freq * i / sr);
                    im[h] += y * std::sin (twoPi * h * freq * i / sr);
                }
        }
        const auto mag = [&] (int h) { return std::sqrt (re[h] * re[h] + im[h] * im[h]); };
        return { (float) (mag (2) / mag (1)), (float) (mag (3) / mag (1)) };
    }

    void testPreampColour()
    {
        std::puts ("Console colour: 1073-style preamps");
        const auto nominal = preampHarmonics (0.0f, 1000.0, 0.5f);
        std::printf ("      +20 dBu, 1 kHz, trim 0: 2nd %.3f %%, 3rd %.3f %%\n", nominal.h2 * 100.0f, nominal.h3 * 100.0f);
        check (nominal.h2 > 0.0008f && nominal.h2 < 0.0012f, "2nd harmonic about 0.1 % at +20 dBu");
        check (nominal.h3 > 0.0004f && nominal.h3 < 0.0006f, "3rd harmonic about 0.05 % at +20 dBu (2nd dominant, class A)");

        const auto quiet = preampHarmonics (0.0f, 1000.0, 0.05f);
        check (quiet.h2 + quiet.h3 < 0.0002f, "nearly clean at normal levels (-18 dBFS)");

        const auto low = preampHarmonics (0.0f, 40.0, 0.5f);
        std::printf ("      +20 dBu, 40 Hz, trim 0: 2nd %.3f %%, 3rd %.3f %%\n", low.h2 * 100.0f, low.h3 * 100.0f);
        check (low.h3 > 0.002f && low.h3 < 0.0045f, "transformers: about 0.3 % 3rd at 40 Hz");

        const auto at20 = preampHarmonics (20.0f, 1000.0, 0.5f), at30 = preampHarmonics (30.0f, 1000.0, 0.5f);
        const float real30 = std::pow (10.0f, 0.15f);   // what the lost feedback alone adds from +20 to +30 dB
        std::printf ("      trim +30 vs +20 at the same level: 2nd x%.2f, 3rd x%.2f (feedback alone x%.2f)\n",
                     at30.h2 / at20.h2, at30.h3 / at20.h3, real30);
        check (near (Preamp::exaggeration (20.0f), 1.0f) && near (Preamp::exaggeration (30.0f), 1.4f), "exaggeration 1.4x only at the top");
        check (near (at30.h2 / at20.h2 / real30, 1.4f, 0.03f) && near (at30.h3 / at20.h3 / real30, 1.4f, 0.03f),
               "trim fully up: harmonics 1.4x a real 1073");

        // Through the engine: on by default, rails hold, and the switch gives back the clean preamp.
        check (AudioEngine().preampColour->on(), "console colour is on by default");
        SineRig r;
        r.e.preampColour->set (1.0f);
        auto& trim = *r.e.inputStrips[0]->controls.trim;
        trim.set (30.0f);   // 0.1 x 31.6 = +10 dBFS = +28 dBu: past the rails
        r.measure (1000.0f, 0.1f);
        r.e.inputStrips[0]->takeInputPeak();
        r.run (1000.0f, 0.1f, 10);
        const float driven = r.e.inputStrips[0]->takeInputPeak();
        std::printf ("      +28 dBu into the preamp: peak %.2f (rails at %.2f)\n", driven, Preamp::clipLevel);
        check (driven < Preamp::clipLevel && driven > 0.8f * Preamp::clipLevel, "driven past the rails: soft-clipped under the ceiling");

        r.e.preampColour->set (0.0f);
        r.run (1000.0f, 0.1f, 40);   // 20 ms crossfade
        r.e.inputStrips[0]->takeInputPeak();
        r.run (1000.0f, 0.1f, 10);
        check (near (r.e.inputStrips[0]->takeInputPeak(), 0.1f * std::pow (10.0f, 1.5f), 1.0e-3f), "colour off: clean trim");
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
            TestEngine e;
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

        TestEngine e;
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

        // Interface input 2 straight into tape in 1 breaks channel 1's normal; a cable adds to it.
        e.setInterfaceInput (P::tapeIn + 0, 1);
        check (near (returnLevel (0), 0.5f), "interface input 2 -> tape in 1 replaces channel 1");
        e.setPatch (P::tapeIn + 0, bit (P::channelOut + 0));
        check (near (returnLevel (0), 0.75f), "interface input and a cable into tape in 1 add up");
        e.setPatch (P::tapeIn + 0, 0);
        e.setInterfaceInput (P::tapeIn + 0, -1);
        check (near (returnLevel (0), 0.25f), "unwiring the interface input restores the normal");

        // An interface input into a return strip in breaks the tape out normal.
        e.setInterfaceInput (P::returnIn + 1, 0);
        check (near (returnLevel (1), 0.25f), "interface input 1 -> return strip in 2 replaces tape out 2");
        e.setInterfaceInput (P::returnIn + 1, -1);

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
            TestEngine e;
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
            TestEngine e;
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }), 0);
            mapOnly (e, 0, 0);
            e.inputStrips[0]->controls.auxLevel[0]->set (1.0f);   // send x 1.5
            auto silent = peaksAbove (clickResponse (e, 12), 0.1f);
            check (silent.size() == 1, "aux not assigned to main: the send is not added to main");

            e.auxInserts[0]->setSlot (0, std::make_unique<TestPlugin> (1.0f, 64), testDescription ("Verb"));
            e.auxMain[0]->set (1.0f);
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
            TestEngine e;
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

    void testBusAssignsAndAuxLinks()
    {
        std::puts ("Sub and aux main assigns, linked aux pairs");
        const auto rig = [] (TestEngine& e, Buffers& b)
        {
            e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1, 2, 3 }));
            mapOnly (e, 0, 0);
            e.inputStrips[0]->controls.main->set (0.0f);
            std::fill (b.in[0].begin(), b.in[0].end(), 0.1f);
        };
        {
            TestEngine e;
            Buffers b (1, 4, 256);
            rig (e, b);
            e.inputStrips[0]->controls.sub[0]->set (1.0f);
            e.setSubOutput (0, 0, 2);
            settle (e, b);
            check (near (b.out[0][200], 0.1f * pan, 1.0e-3f), "sub 1 reaches main (assigned by default)");
            e.subMain[0]->set (0.0f);
            settle (e, b);
            check (near (b.out[0][200], 0.0f) && near (b.out[1][200], 0.0f), "sub 1 unassigned: nothing on main");
            check (near (b.out[2][200], 0.1f * pan, 1.0e-3f), "sub 1 unassigned still feeds its output");
        }
        {
            TestEngine e;
            Buffers b (1, 4, 256);
            rig (e, b);
            e.inputStrips[0]->controls.auxLevel[0]->set (1.0f);
            e.setAuxOutput (0, 2);
            settle (e, b);
            check (near (b.out[0][200], 0.0f) && near (b.out[2][200], 0.15f, 1.0e-3f), "aux 1 not assigned to main by default; its output still works");
            e.auxMain[0]->set (1.0f);
            settle (e, b);
            check (near (b.out[0][200], 0.15f, 1.0e-3f) && near (b.out[1][200], 0.15f, 1.0e-3f), "aux 1 assigned: the return reaches main without a plugin");
            e.auxMain[0]->set (0.0f);
            settle (e, b);
            check (near (b.out[0][200], 0.0f) && near (b.out[2][200], 0.15f, 1.0e-3f), "aux 1 unassigned again: only its output");
        }
        {
            TestEngine e;
            Buffers b (1, 4, 256);
            rig (e, b);
            auto& c = e.inputStrips[0]->controls;
            c.auxLevel[1]->set (0.9f);
            e.setAuxLink (0, true);
            check (c.auxLevel[1]->get() == 0.5f && e.returnStrips[5]->controls.auxLevel[1]->get() == 0.5f, "linking centres every channel's pan (A2 knob)");
            e.auxMaster[0]->set (faderFor (-6.0f));
            e.auxMain[1]->set (1.0f);
            check (e.auxMaster[1]->get() == e.auxMaster[0]->get() && e.auxMain[0]->on(), "linked masters and main assigns move together");
            e.auxMaster[0]->set (80.0f);
            e.auxMain[0]->set (0.0f);

            e.setAuxOutput (0, 2);
            e.setAuxOutput (1, 3);
            c.auxLevel[0]->set (1.0f);
            c.auxLevel[1]->set (0.0f);
            settle (e, b);
            check (near (b.out[2][200], 0.15f, 1.0e-3f) && near (b.out[3][200], 0.0f, 1.0e-4f), "stereo send panned left: all on A1");
            c.auxLevel[1]->set (1.0f);
            settle (e, b);
            check (near (b.out[2][200], 0.0f, 1.0e-4f) && near (b.out[3][200], 0.15f, 1.0e-3f), "stereo send panned right: all on A2");
            c.auxLevel[1]->set (0.5f);
            settle (e, b);
            check (near (b.out[2][200], 0.15f * pan, 1.0e-3f) && near (b.out[3][200], 0.15f * pan, 1.0e-3f), "stereo send centred: -3 dB each side");

            // One stereo chain (A1's) for the pair; A2's own return stays out of the mix.
            e.auxInserts[0]->setSlot (0, std::make_unique<TestPlugin> (0.5f, 0), testDescription ("Half"));
            e.auxInserts[1]->setSlot (0, std::make_unique<TestPlugin> (4.0f, 0), testDescription ("Idle"));
            e.auxMain[0]->set (1.0f);
            c.auxLevel[1]->set (1.0f);
            settle (e, b);
            check (near (b.out[0][200], 0.0f, 1.0e-4f) && near (b.out[1][200], 0.075f, 1.0e-3f), "linked return: A1+A2 through one stereo chain, right stays right");

            e.auxInserts[0]->setSlot (1, std::make_unique<TestPlugin> (1.0f, 0, true), testDescription ("Mono"));
            check (e.auxInserts[0]->isStereoRefused (1), "a mono-input plugin is left out of a linked pair");
            const auto notStereo = e.setAuxLink (0, false);
            check (notStereo.isEmpty() && ! e.auxInserts[0]->isStereoRefused (1) && ! e.auxInserts[0]->isStereoOnly(), "unlinking takes it back in");
            check (e.setAuxLink (0, true).contains ("Mono"), "relinking reports it");
        }
        {
            // Linked channels 1+2 panned hard left/right, then aux pair 1+2 linked: the sends are stereo too.
            TestEngine e;
            auto& a = e.inputStrips[0]->controls;
            auto& b = e.inputStrips[1]->controls;
            e.inputLink[0]->set (1.0f);
            a.pan->set (-1.0f);
            check (b.pan->get() == 1.0f, "linked channels: main pans mirror");
            e.setAuxLink (0, true);
            check (a.auxLevel[1]->get() == 0.0f && b.auxLevel[1]->get() == 1.0f, "linking the aux pair starts each aux pan at the main pan");
            a.auxLevel[1]->set (0.2f);
            check (near (b.auxLevel[1]->get(), 0.8f), "linked channels: aux pans mirror");
            b.auxLevel[1]->set (0.4f);
            check (near (a.auxLevel[1]->get(), 0.6f), "either channel moves the other the opposite way");
            a.auxLevel[0]->set (0.7f);
            check (b.auxLevel[0]->get() == 0.7f, "aux send levels still move together");
            e.setAuxLink (0, false);
            a.auxLevel[1]->set (0.3f);
            check (b.auxLevel[1]->get() == 0.3f, "aux pair unlinked: A2 is a send level again and copies straight");
        }
    }

    void testPunchAndLoop()
    {
        std::puts ("Tape: auto-punch and loop");
        TestEngine e;
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
        TestEngine e;
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
        TestEngine e;
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
            TestEngine a;
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
            TestEngine b;
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
            const auto e = Session::exportMix (b, wav, 0, b.tape.getLength(), 1.0, [] (double) { return true; });
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

            // In to Out, into the Bounced Audio folder (created by the export).
            const auto bounced = Session::bounceFolder (folder);
            check (! bounced.exists(), "no Bounced Audio folder before exporting into it");
            const auto part = bounced.getChildFile ("part.wav");
            const auto e2 = Session::exportMix (b, part, b.tape.getInPoint(), b.tape.getOutPoint(), 0.0, [] (double) { return true; });
            std::unique_ptr<juce::AudioFormatReader> partReader (fm.createReaderFor (part));
            check (e2.ok && bounced.isDirectory() && partReader != nullptr, "In to Out export written into Bounced Audio");
            check (partReader != nullptr && partReader->lengthInSamples == 4000, "length = Out - In (4000 samples), no tail");
            partReader.reset();
            check (b.tape.getPosition() == 0 && ! b.tape.isPlaying(), "transport put back after a region export");
            check (! Session::exportMix (b, part, 5000, 1000, 0.0, [] (double) { return true; }).ok, "Out before In is refused");
        }
        {
            TestEngine c;
            c.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
            c.inputStrips[0]->controls.trim->set (10.0f);
            c.tape.writeTrack (0, 0, take.data(), 1000);
            Session::reset (c);
            check (c.inputStrips[0]->controls.trim->get() == 0.0f && c.tape.getLength() == 0 && ! c.tape.trackHasAudio (0), "new session clears everything");
        }
        folder.deleteRecursively();
    }

    void testImport()
    {
        std::puts ("Import audio onto tape tracks");
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ConsoleTapeTestImport");
        dir.deleteRecursively();
        dir.createDirectory();

        const auto writeWav = [] (const juce::File& f, double rate, const std::vector<std::vector<float>>& chans)
        {
            juce::WavAudioFormat wav;
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), rate, (unsigned) chans.size(), 32, {}, 0));
            if (w == nullptr)
                return false;
            out.release();
            std::vector<const float*> ptrs;
            for (auto& c : chans)
                ptrs.push_back (c.data());
            return w->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) chans[0].size());
        };

        std::vector<float> mono (70000), left (44100), right (44100);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.25f * (float) std::sin ((double) i * 0.05);
        for (size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 441.0 * (double) i / 44100.0);
            right[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 441.0 * (double) i / 44100.0);
        }
        const auto monoFile = dir.getChildFile ("mono.wav"), stereoFile = dir.getChildFile ("stereo.wav");
        check (writeWav (monoFile, 48000.0, { mono }) && writeWav (stereoFile, 44100.0, { left, right }), "test files written");

        TestEngine e;
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        const auto none = [] (double) { return true; };

        auto info = AudioImport::probe (stereoFile);
        check (info.ok && info.channels == 2 && info.tracksNeeded() == 2 && info.sampleRate == 44100.0 && info.length == 44100, "probe: stereo 44.1 kHz, 1 s");
        check (! AudioImport::probe (dir.getChildFile ("missing.wav")).ok, "probe: unreadable file is refused");

        // Mono at the tape's rate: bit for bit, from the start position given.
        auto a = AudioImport::read (monoFile, 48000.0, e.tape.getCapacity(), none);
        check (a.ok && a.channels.size() == 1 && a.length() == (juce::int64) mono.size() && a.channels[0] == mono, "mono, same rate: unchanged");
        AudioImport::place (e.tape, a, 6, 1000);
        std::vector<float> got (mono.size());
        e.tape.readTrack (6, 1000, got.data(), (int) got.size());
        check (got == mono && e.tape.trackHasAudio (6) && ! e.tape.trackHasAudio (5) && ! e.tape.trackHasAudio (7), "mono lands on track 7 only");
        check (e.tape.getLength() == 1000 + (juce::int64) mono.size(), "tape length covers the import");

        // Stereo at 44.1 kHz onto tracks 15 + 16: converted to 48 kHz, left and right on their own tracks.
        a = AudioImport::read (stereoFile, 48000.0, e.tape.getCapacity(), none);
        check (a.ok && a.channels.size() == 2 && a.length() == 48000, "stereo converted to 48 kHz: 1 s = 48000 samples");
        AudioImport::place (e.tape, a, 14, 0);
        std::vector<float> l (48000), r (48000);
        e.tape.readTrack (14, 0, l.data(), 48000);
        e.tape.readTrack (15, 0, r.data(), 48000);
        float peakL = 0, peakR = 0;
        for (int i = 4800; i < 43200; ++i)
        {
            peakL = juce::jmax (peakL, std::abs (l[(size_t) i]));
            peakR = juce::jmax (peakR, std::abs (r[(size_t) i]));
        }
        check (std::abs (peakL - 0.5f) < 0.02f && std::abs (peakR - 0.1f) < 0.01f, "left on track 15, right on track 16, levels kept");
        int crossings = 0;
        for (int i = 4801; i < 43200; ++i)
            crossings += (l[(size_t) i - 1] < 0.0f) != (l[(size_t) i] < 0.0f) ? 1 : 0;
        check (std::abs (crossings - 2 * 441 * 38400 / 48000) <= 2, "pitch kept after rate conversion (441 Hz)");
        check (e.tape.getLength() == 1000 + (juce::int64) mono.size(), "a shorter import does not shorten the tape");

        // Cancel and truncation.
        a = AudioImport::read (monoFile, 48000.0, e.tape.getCapacity(), [] (double) { return false; });
        check (! a.ok && a.channels.empty(), "cancel stops the import");
        a = AudioImport::read (monoFile, 48000.0, 5000, none);
        check (a.ok && a.truncated && a.length() == 5000, "cut off at the end of the tape");

        dir.deleteRecursively();
    }

    void testNoDeviceIsSafe()
    {
        std::puts ("Callback with zero channels");
        TestEngine e;
        e.prepare (48000.0, {}, {});
        Buffers b (0, 0, 64);
        b.run (e);
        check (true, "no crash");
    }

    /** Worst case: every strip with all EQ bands, gate, compressor and all sends on,
        16 tracks playing back, 64-sample blocks. Reports CPU use of one core. */
    /** Times every block: the average says little about dropouts, the slowest blocks do. */
    void reportBlockTimes (const char* what, std::vector<double>& ms)
    {
        const double budget = 64.0 / 48.0;   // ms per 64-sample block at 48 kHz
        std::sort (ms.begin(), ms.end());
        double sum = 0.0;
        for (auto x : ms) sum += x;
        const auto pct = [&] (double p) { return ms[(size_t) juce::jlimit (0.0, (double) ms.size() - 1, p * (double) ms.size())]; };
        const auto over = std::count_if (ms.begin(), ms.end(), [&] (double x) { return x > budget * 0.5; });
        std::printf ("  %s: average %.3f ms, 99%% %.3f ms, 99.9%% %.3f ms, slowest %.3f ms (budget %.3f ms); %d blocks over half the budget\n",
                     what, sum / (double) ms.size(), pct (0.99), pct (0.999), ms.back(), budget, (int) over);
    }

    void benchmark()
    {
        std::puts ("\nBenchmark (worst case, 64-sample blocks, 48 kHz):");
        TestEngine e;
        // BENCH_PRIORITY=1: time-critical, like a real audio thread, so the slowest blocks are the engine's
        // own and not Windows scheduling. BENCH_NO_CHARACTER=1: tape character off.
        if (juce::SystemStats::getEnvironmentVariable ("BENCH_PRIORITY", {}) == "1")
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (juce::SystemStats::getEnvironmentVariable ("BENCH_NO_CHARACTER", {}) == "1")
            e.tape.characterOn = false;
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
        std::vector<double> recordMs;
        for (int i = 0; i < blocks; ++i)
        {
            if (i % 64 == 0)
                e.tape.service();
            fill();
            const auto t0 = juce::Time::getHighResolutionTicks();
            b.run (e);
            recordMs.push_back (1000.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
        }
        reportBlockTimes ("recording 16 tracks", recordMs);
        e.tape.stop();
        e.tape.setRecordEnabled (false);
        for (int t = 0; t < TapeMachine::numTracks; ++t)
            e.tape.arm[(size_t) t]->set (0.0f);
        e.tape.locate (0);
        e.tape.play();

        const auto start = juce::Time::getHighResolutionTicks();
        std::vector<double> playMs;
        for (int i = 0; i < blocks; ++i)
        {
            fill();
            const auto t0 = juce::Time::getHighResolutionTicks();
            b.run (e);
            playMs.push_back (1000.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
            if (juce::SystemStats::getEnvironmentVariable ("BENCH_TRACE", {}) == "1" && playMs.back() > 0.4)
                std::printf ("    slow block %d: %.3f ms at tape sample %lld (%lld into its chunk)\n", i, playMs.back(),
                             (long long) e.tape.getPosition(), (long long) (e.tape.getPosition() % TapeMachine::chunkSize));
        }
        reportBlockTimes ("playing 16 tracks", playMs);
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
    testOscillator();
    testStripLink();
    testStripControls();
    testRecordingIsLatencyCompensated();
    testTapeStorageAndTransport();
    testUnarmedPlaybackAndArmedMonitoring();
    testEq();
    testGate();
    testDeEsser();
    testFetCompressor();
    testSends();
    testTapeCharacter();
    testTapeOutMeters();
    testSpeedAndPitch();
    testEraseSections();
    testPreampColour();
    testBusCompressor();
    testMasterSection();
    testPatchBay();
    testInserts();
    testBusAssignsAndAuxLinks();
    testPunchAndLoop();
    testWinding();
    testWowFlutter();
    testSessions();
    testImport();
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
