#pragma once

namespace spark
{
    constexpr int padCount = 16;
    constexpr int padNoteFirst = 36;
    constexpr int padNoteBankedLast = 43;
    constexpr int padNoteAbsoluteLast = 51;

    constexpr int notePlay = 90;
    constexpr int noteStop = 91;
    constexpr int noteRecord = 92;
    constexpr int noteRewind = 93;
    constexpr int noteForward = 94;
    constexpr int noteLoop = 95;
    constexpr int noteBankB = 96;
    constexpr int noteBankA = 97;

    constexpr int ccVolume = 7;
    constexpr int ccDrive = 16;
    constexpr int ccDecay = 17;
    constexpr int ccTune = 18;
    constexpr int ccTempo = 19;
    constexpr int ccXyX = 20;
    constexpr int ccXyY = 21;

    constexpr int stepCount = 16;

    inline int relative1(int value)
    {
        if (value > 0 && value < 64)
            return value;
        if (value > 64)
            return value - 128;
        return 0;
    }
}
