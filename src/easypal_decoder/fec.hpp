#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace Digiham::EasyPal {

    typedef std::complex<float> Complex;

    // Channel coding layer: permutations, CRCs, scrambling, the punctured
    // rate-1/4 convolutional code with a soft-decision Viterbi decoder, and the
    // multilevel decoder for 4, 16 and 64 point QAM.
    class Fec {
        public:
            // DRM pseudo-random permutation: table[i] = Pi(i), for i < n
            static std::vector<int> permutation(int n, int t0);

            // FAC protection: 8-bit CRC over 40 bits, transmitted inverted
            static bool facCrcOk(const unsigned char* bits);
            // Data block / data group CRC-16 (CCITT, inverted) over all but the last two bytes
            static bool blockCrcOk(const unsigned char* data, int length);

            // Energy dispersal: XOR with the x^9 + x^5 + 1 sequence (register preset to ones)
            static void descramble(unsigned char* bits, int length);

            // Per-step output masks (bit k set = coded output k is transmitted) for a
            // trellis of "info" input bits plus six tail steps. A tailIndex of -1
            // keeps using the main puncturing pattern for the tail.
            static std::vector<unsigned char> stepMasks(int rate, int info, int tailIndex);

            // Number of information bits and tail pattern of a level that carries
            // "codedBits" transmitted bits at the given rate index
            static void levelSizing(int rate, int codedBits, int& info, int& tailIndex);

            // Soft Viterbi decoder. "llr" holds one value per transmitted bit, in
            // transmission order (positive = bit 0 more likely). Returns info + 6
            // bits, the last six being the zero tail.
            static std::vector<unsigned char> viterbi(const std::vector<float>& llr, const std::vector<unsigned char>& masks);

            // Encoder for the same code, returns the transmitted bits
            static std::vector<unsigned char> encode(const std::vector<unsigned char>& info, const std::vector<unsigned char>& masks);

            // Decode a FAC block from its 45 equalized cells and their reliabilities
            static bool decodeFac(const Complex* cells, const float* weight, int count, unsigned char* bits);

            // Decode an MSC multiplex frame ("count" equalized cells with reliabilities
            // |H|^2, already time-deinterleaved) coded with 4, 16 or 64 points and the
            // given protection level. On success "bits" holds the descrambled payload
            // bits of all levels in order.
            static bool decodeMsc(const Complex* cells, const float* weight, int count, int qam, int protection,
                                  std::vector<unsigned char>& bits);

            // Undo the MSC cell interleaver: frames[d] holds the cells of transmission
            // frame M+d, and the result is multiplex frame M. The depth is 1 (short)
            // or 5 (long).
            template <typename T>
            static std::vector<T> deinterleave(const std::vector<const T*>& frames, int count, int depth) {
                std::vector<int> pi = permutation(count, 5);
                std::vector<T> result(count);
                for (int i = 0; i < count; i++) result[pi[i]] = frames[i % depth][i];
                return result;
            }
    };

}
