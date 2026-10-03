// Constants of the DRM / HamDRM signal format used by the EasyPal decoder.
//
// These are numeric facts defined by the DRM standard (ETSI ES 201 980) and
// the HamDRM amateur extension. They were collected from public
// transmitter / receiver implementations (Dream, QSSTV) and cross-checked by
// decoding signals produced by an independent transmitter.
#pragma once
namespace Digiham::EasyPal {

    // Puncturing patterns of the rate-1/4 mother code. Each pattern is a set of
    // four rows (one per coded output b0..b3); a '1' keeps that output at that
    // input step, and the columns repeat cyclically.
    static const char* const puncturePatterns[13][4] = {
        {"1", "1", "1", "1"},
        {"111", "111", "111", "100"},
        {"1", "1", "1", "0"},
        {"1111", "1111", "1110", "0000"},
        {"1", "1", "0", "0"},
        {"1111", "1010", "0100", "0000"},
        {"111", "101", "000", "000"},
        {"11", "10", "00", "00"},
        {"11111111", "10010010", "00000000", "00000000"},
        {"111", "100", "000", "000"},
        {"1111", "1000", "0000", "0000"},
        {"1111111", "1000000", "0000000", "0000000"},
        {"11111111", "10000000", "00000000", "00000000"}};

    // Tail-bit puncturing patterns (six trellis steps; one row per output b0..b3).
    static const char* const tailPatterns[12][4] = {
        {"111111", "111111", "000000", "000000"},
        {"111111", "111111", "100000", "000000"},
        {"111111", "111111", "100100", "000000"},
        {"111111", "111111", "110100", "000000"},
        {"111111", "111111", "110110", "000000"},
        {"111111", "111111", "111110", "000000"},
        {"111111", "111111", "111111", "000000"},
        {"111111", "111111", "111111", "100000"},
        {"111111", "111111", "111111", "100100"},
        {"111111", "111111", "111111", "110100"},
        {"111111", "111111", "111111", "110101"},
        {"111111", "111111", "111111", "111101"}};

    // Code-rate indices (into puncturePatterns) for each coding level, per protection
    // level. 4-QAM: a single level; 16-QAM: two; 64-QAM: three.
    static const int rates4Qam[1] = {6};
    static const int rates16Qam[2][2] = {{2, 7}, {4, 9}};
    static const int rates64Qam[4][3] = {{0, 4, 9}, {2, 7, 10}, {4, 9, 11}, {7, 10, 12}};

    // Per-robustness-mode geometry at 12 kHz sampling (mode A = 0, mode B = 1).
    struct ModeInfo {
            int tu, tg;   // useful / guard length in samples
            int symbols;  // OFDM symbols per frame
            int x, y, k0; // scattered pilot grid: k = k0 + x*(s%y) + x*y*p
            int q1024;    // pilot phase constant
            int freqK[3], freqTheta[3];
            int timeCount, timeK[16], timeTheta[16];
            int kmin[2], kmax[2];        // per occupancy (0 = 2.3 kHz, 1 = 2.5 kHz)
            int boost[2][4];             // carriers with 2x pilot amplitude
            int w1024[5][5], z256[5][5]; // pilot phase tables [s%y][s/y]
    };

    static const ModeInfo modes[2] = {
        {288, 32, 15, 4, 5, 2, 36, {9, 27, 36}, {205, 836, 215}, 16, {6, 7, 11, 12, 15, 16, 23, 29, 30, 33, 34, 38, 39, 41, 45, 46}, {973, 205, 717, 264, 357, 357, 952, 440, 856, 88, 88, 68, 836, 836, 836, 1008}, {2, 2}, {54, 58}, {{2, 6, 50, 54}, {2, 6, 54, 58}}, {{228, 341, 455, 0, 0}, {455, 569, 683, 0, 0}, {683, 796, 910, 0, 0}, {910, 0, 114, 0, 0}, {114, 228, 341, 0, 0}}, {{0, 81, 248, 0, 0}, {18, 106, 106, 0, 0}, {122, 116, 31, 0, 0}, {129, 129, 39, 0, 0}, {33, 32, 111, 0, 0}}},
        {256, 64, 15, 2, 3, 1, 12, {8, 24, 32}, {331, 651, 555}, 15, {6, 10, 11, 14, 17, 18, 27, 28, 30, 33, 34, 38, 40, 41, 44, 0}, {304, 331, 108, 620, 192, 704, 44, 432, 588, 844, 651, 651, 651, 460, 950, 0}, {1, 1}, {45, 51}, {{1, 3, 43, 45}, {1, 3, 49, 51}}, {{512, 0, 512, 0, 512}, {0, 512, 0, 512, 0}, {512, 0, 512, 0, 512}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, {{0, 57, 164, 64, 12}, {168, 255, 161, 106, 118}, {25, 232, 132, 233, 38}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}}};

    // FAC cells of mode A as {symbol, carrier}, in transmission order.
    static const int facCellsA[45][2] = {{1, 10}, {1, 22}, {1, 30}, {1, 50}, {2, 14}, {2, 26}, {2, 34}, {3, 18}, {3, 30}, {3, 38}, {4, 22}, {4, 34}, {4, 42}, {5, 18}, {5, 26}, {5, 38}, {5, 46}, {6, 22}, {6, 30}, {6, 42}, {6, 50}, {7, 26}, {7, 34}, {7, 46}, {8, 10}, {8, 30}, {8, 38}, {8, 50}, {9, 14}, {9, 34}, {9, 42}, {10, 18}, {10, 38}, {10, 46}, {11, 10}, {11, 22}, {11, 42}, {11, 50}, {12, 14}, {12, 26}, {12, 46}, {13, 18}, {13, 30}, {14, 22}, {14, 34}};

    // FAC cells of mode B as {symbol, carrier}, in transmission order.
    static const int facCellsB[45][2] = {{0, 21}, {1, 11}, {1, 23}, {1, 35}, {2, 13}, {2, 25}, {2, 37}, {3, 15}, {3, 27}, {3, 39}, {4, 5}, {4, 17}, {4, 29}, {4, 41}, {5, 7}, {5, 19}, {5, 31}, {6, 9}, {6, 21}, {6, 33}, {7, 11}, {7, 23}, {7, 35}, {8, 13}, {8, 25}, {8, 37}, {9, 15}, {9, 27}, {9, 39}, {10, 5}, {10, 17}, {10, 29}, {10, 41}, {11, 7}, {11, 19}, {11, 31}, {12, 9}, {12, 21}, {12, 33}, {13, 11}, {13, 23}, {13, 35}, {14, 13}, {14, 25}, {14, 37}};

}
