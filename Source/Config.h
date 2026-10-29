#pragma once

/** Console size and fixed limits. */
namespace Config
{
    constexpr int numInputStrips  = 16;
    constexpr int numTracks       = 16;
    constexpr int numReturnStrips = numTracks;
    constexpr int numSubs = 4;   // stereo sub groups, each with a group EQ
    constexpr int numAux  = 8;   // mono aux buses
    constexpr int numMidiTracks = 16;   // MIDI page tracks, each with an instrument (two patch bay outputs)

    constexpr int maxBlock = 512;          // device blocks larger than this are processed in pieces
    constexpr double maxSampleRate = 192000.0;
    constexpr int maxTapeMinutes = 60;

    /** The plugin formats the insert slots take, as the UI names them. */
   #if defined (__APPLE__)
    constexpr const char* pluginFormats = "VST3 / AU";
   #else
    constexpr const char* pluginFormats = "VST3";
   #endif
}

/** Where a channel strip adds its outputs. */
struct BusPointers
{
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* subL[Config::numSubs] {};
    float* subR[Config::numSubs] {};
    float* aux[Config::numAux] {};
    float* soloL = nullptr;   // solo bus: what the monitor outs carry while anything is soloed
    float* soloR = nullptr;
    /** While anything is soloed: bit k = aux k returns to main (an effect). It is heard with the solo, and only the
        soloed strips send to it. */
    unsigned soloAuxes = 0;
};
