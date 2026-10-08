#pragma once

/** Console size and fixed limits. */
namespace Config
{
    constexpr int numInputStrips  = 16;
    constexpr int numTracks       = 16;
    constexpr int numReturnStrips = numTracks;
    constexpr int numSubs = 4;   // stereo sub groups, each with a group EQ
    constexpr int numAux  = 8;   // mono aux buses

    constexpr int maxBlock = 512;          // device blocks larger than this are processed in pieces
    constexpr double maxSampleRate = 192000.0;
    constexpr int maxTapeMinutes = 60;
}

/** Where a channel strip adds its outputs. */
struct BusPointers
{
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* subL[Config::numSubs] {};
    float* subR[Config::numSubs] {};
    float* aux[Config::numAux] {};
};
