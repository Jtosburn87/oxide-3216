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
        check (p1 == p2, "overdub lines up sample-accurately with the track it was played against");
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

    void testNoDeviceIsSafe()
    {
        std::puts ("Callback with zero channels");
        AudioEngine e;
        e.prepare (48000.0, {}, {});
        Buffers b (0, 0, 64);
        b.run (e);
        check (true, "no crash");
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
    testNoDeviceIsSafe();

    if (argc > 1 && juce::String (argv[1]) == "--devices")
        listDevices();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
