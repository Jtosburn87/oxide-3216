#pragma once

/** Console size. Stage 2 runs with 4 + 4 strips; later stages raise these to 16. */
namespace Config
{
    constexpr int numInputStrips = 4;
    constexpr int numTracks      = 4;
    constexpr int numReturnStrips = numTracks;

    constexpr int maxBlock = 512;          // device blocks larger than this are processed in pieces
    constexpr double maxSampleRate = 192000.0;
    constexpr int maxTapeMinutes = 60;
}
