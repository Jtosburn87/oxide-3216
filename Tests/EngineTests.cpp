// Engine tests: run synthetic audio through AudioEngine without an audio device.
// Usage: EngineTests            run the tests
//        EngineTests --devices  also list the audio drivers and devices on this PC

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include <cstdio>
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

    void testPassThrough()
    {
        std::puts ("Pass-through: input 1 to main L/R");
        AudioEngine e;
        e.prepare (48000.0, channels ({ 0, 1, 2, 3 }), channels ({ 0, 1 }));
        Buffers b (4, 2, 256);
        std::fill (b.in[0].begin(), b.in[0].end(), 0.5f);
        b.run (e);
        check (near (b.out[0][0], 0.5f * pan) && near (b.out[0][255], 0.5f * pan), "left = 0.5 at -3 dB pan");
        check (near (b.out[1][100], 0.5f * pan), "right = 0.5 at -3 dB pan");
        check (near (e.takeInputPeak (0), 0.5f), "input 1 meter reads 0.5");
        check (near (e.takeInputPeak (1), 0.0f), "input 2 meter reads 0");
        check (near (e.takeInputPeak (0), 0.0f), "meter resets after reading");
        check (near (e.takeMainPeak (0), 0.5f * pan), "main L meter");
    }

    void testInputMappingWithInactiveChannels()
    {
        std::puts ("Input mapping when only some interface inputs are active");
        AudioEngine e;
        // Device inputs 0, 2 and 5 are active, so the callback receives them packed as slots 0, 1, 2.
        e.prepare (48000.0, channels ({ 0, 2, 5 }), channels ({ 0, 1 }));
        for (int c = 0; c < AudioEngine::numConsoleInputs; ++c)
            e.setInputSource (c, -1);
        e.setInputSource (3, 5);   // console channel 4 <- device input 6

        Buffers b (3, 2, 64);
        std::fill (b.in[2].begin(), b.in[2].end(), 0.25f);   // slot 2 = device input 5
        std::fill (b.in[0].begin(), b.in[0].end(), 0.9f);    // device input 0 is not routed anywhere
        b.run (e);
        check (near (b.out[0][10], 0.25f * pan), "only the routed input reaches main");
        check (near (e.takeInputPeak (3), 0.25f), "console channel 4 meter");

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

    void testGainToneAndLargeBlocks()
    {
        std::puts ("Main gain, test tone, blocks larger than the internal chunk");
        AudioEngine e;
        e.prepare (48000.0, channels ({ 0 }), channels ({ 0, 1 }));
        e.mainGain.store (0.5f);
        Buffers b (1, 2, 2048);
        std::fill (b.in[0].begin(), b.in[0].end(), 1.0f);
        b.run (e);
        check (near (b.out[0][0], 0.5f * pan) && near (b.out[0][2047], 0.5f * pan), "gain applied across a 2048-sample block");

        std::fill (b.in[0].begin(), b.in[0].end(), 0.0f);
        e.mainGain.store (1.0f);
        e.testToneOn.store (true);
        e.takeMainPeak (0);
        b.run (e);
        const float peak = e.takeMainPeak (0);
        check (near (peak, 0.125893f, 2.0e-3f), "test tone peaks at -18 dBFS");

        // 440 Hz over 2048 samples at 48 kHz: count rising zero crossings (expect about 18.8).
        int crossings = 0;
        for (size_t i = 1; i < b.out[0].size(); ++i)
            if (b.out[0][i - 1] < 0.0f && b.out[0][i] >= 0.0f)
                ++crossings;
        check (crossings >= 18 && crossings <= 19, "test tone is 440 Hz");
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
    testGainToneAndLargeBlocks();
    testNoDeviceIsSafe();

    if (argc > 1 && juce::String (argv[1]) == "--devices")
        listDevices();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
