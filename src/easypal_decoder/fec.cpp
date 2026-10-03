#include "fec.hpp"
#include "tables.hpp"

#include <algorithm>
#include <cstring>

using namespace Digiham::EasyPal;

std::vector<int> Fec::permutation(int n, int t0) {
    // s = smallest power of two above n; q = s/4 - 1
    int s = 1;
    while (s <= n) s <<= 1;
    int q = s / 4 - 1;

    std::vector<int> t(n);
    t[0] = 0;
    for (int i = 1; i < n; i++) {
        int v = (t0 * t[i - 1] + q) % s;
        while (v >= n) v = (t0 * v + q) % s;
        t[i] = v;
    }
    return t;
}

bool Fec::facCrcOk(const unsigned char bits[48]) {
    unsigned reg = 0xFF;
    for (int i = 0; i < 40; i++) {
        unsigned fb = ((reg >> 7) & 1) ^ bits[i];
        reg = (reg << 1) & 0xFF;
        if (fb) reg ^= 0x1D; // x^8 + x^4 + x^3 + x^2 + 1
    }
    unsigned sent = 0;
    for (int i = 40; i < 48; i++) sent = (sent << 1) | bits[i];
    return sent == (~reg & 0xFF);
}

bool Fec::blockCrcOk(const unsigned char* d, int len) {
    if (len < 3) return false;
    unsigned reg = 0xFFFF;
    for (int i = 0; i < len - 2; i++) {
        reg ^= (unsigned)d[i] << 8;
        for (int b = 0; b < 8; b++)
            reg = (reg & 0x8000) ? ((reg << 1) ^ 0x1021) & 0xFFFF : (reg << 1) & 0xFFFF;
    }
    unsigned sent = (d[len - 2] << 8) | d[len - 1];
    return sent == (~reg & 0xFFFF);
}

void Fec::descramble(unsigned char* bits, int n) {
    uint32_t reg = ~0u;
    for (int i = 0; i < n; i++) {
        uint32_t bit = ((reg >> 4) ^ (reg >> 8)) & 1;
        reg = (reg << 1) | bit;
        bits[i] ^= bit;
    }
}

static int patternPeriod(int rate) {
    return (int)strlen(puncturePatterns[rate][0]);
}

static int patternOnes(int rate) {
    int n = 0;
    for (int r = 0; r < 4; r++)
        for (const char* p = puncturePatterns[rate][r]; *p; p++) n += (*p == '1');
    return n;
}

std::vector<unsigned char> Fec::stepMasks(int rate, int info, int tailIdx) {
    std::vector<unsigned char> m(info + 6);
    int period = patternPeriod(rate);
    for (int i = 0; i < info; i++) {
        unsigned char v = 0;
        for (int r = 0; r < 4; r++)
            if (puncturePatterns[rate][r][i % period] == '1') v |= 1 << r;
        m[i] = v;
    }
    for (int i = 0; i < 6; i++) {
        unsigned char v = 0;
        for (int r = 0; r < 4; r++) {
            char c = (tailIdx >= 0) ? tailPatterns[tailIdx][r][i]
                                    : puncturePatterns[rate][r][(info + i) % period];
            if (c == '1') v |= 1 << r;
        }
        m[info + i] = v;
    }
    return m;
}

void Fec::levelSizing(int rate, int codedBits, int& info, int& tailIdx) {
    int ones = patternOnes(rate), period = patternPeriod(rate);
    int body = codedBits - 12;
    info = period * (body / ones);
    tailIdx = body - ones * (body / ones);
}

// Code generators as taps over a 7-bit window (newest input in bit 0).
static const unsigned generators[4] = {0155, 0117, 0123, 0155};

static inline int parity(unsigned v) {
    return __builtin_parity(v);
}

std::vector<unsigned char> Fec::encode(const std::vector<unsigned char>& info, const std::vector<unsigned char>& masks) {
    std::vector<unsigned char> out;
    unsigned reg = 0;
    for (size_t i = 0; i < masks.size(); i++) {
        reg = ((reg << 1) | (i < info.size() ? info[i] : 0)) & 0x7F;
        for (int k = 0; k < 4; k++)
            if (masks[i] & (1 << k)) out.push_back(parity(reg & generators[k]));
    }
    return out;
}

std::vector<unsigned char> Fec::viterbi(const std::vector<float>& llr, const std::vector<unsigned char>& masks) {
    const int steps = masks.size();
    const int S = 64;

    // Expand the received values to four slots per step (0 where punctured)
    std::vector<float> slot(steps * 4, 0.0f);
    size_t pos = 0;
    for (int i = 0; i < steps; i++)
        for (int k = 0; k < 4; k++)
            if (masks[i] & (1 << k)) slot[i * 4 + k] = pos < llr.size() ? llr[pos++] : 0.0f;

    // Coded outputs for every (state, input): window = state*2 + input
    unsigned char outBits[128];
    for (int w = 0; w < 128; w++) {
        unsigned char v = 0;
        for (int k = 0; k < 4; k++) v |= parity(w & generators[k]) << k;
        outBits[w] = v;
    }

    std::vector<float> metric(S, -1e30f), next(S);
    metric[0] = 0.0f;
    std::vector<unsigned char> from(steps * S); // 1 = predecessor had the high bit set

    for (int i = 0; i < steps; i++) {
        const float* l = &slot[i * 4];

        // Branch metric per possible output word
        float bm[16];
        for (int o = 0; o < 16; o++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += (o & (1 << k)) ? -l[k] : l[k];
            bm[o] = v;
        }

        for (int ns = 0; ns < S; ns++) {
            int in = ns & 1;
            int ps0 = ns >> 1, ps1 = (ns >> 1) | 32;
            float m0 = metric[ps0] + bm[outBits[ps0 * 2 + in]];
            float m1 = metric[ps1] + bm[outBits[ps1 * 2 + in]];
            if (m1 > m0) {
                next[ns] = m1;
                from[i * S + ns] = 1;
            } else {
                next[ns] = m0;
                from[i * S + ns] = 0;
            }
        }
        metric.swap(next);
    }

    // The tail drives the encoder back to state 0
    std::vector<unsigned char> bits(steps);
    int state = 0;
    for (int i = steps - 1; i >= 0; --i) {
        bits[i] = state & 1;
        state = (state >> 1) | (from[i * S + state] << 5);
    }
    return bits;
}

bool Fec::decodeFac(const Complex* cells, const float* weight, int nCells, unsigned char bits[48]) {
    if (nCells != 45) return false;

    // Soft bits in transmission order: I then Q of every cell (4-QAM)
    std::vector<float> y(90);
    for (int n = 0; n < 45; n++) {
        y[2 * n] = weight[n] * cells[n].real();
        y[2 * n + 1] = weight[n] * cells[n].imag();
    }

    // Bit de-interleaver: x[Pi(i)] = y[i]
    std::vector<int> pi = permutation(90, 21);
    std::vector<float> x(90);
    for (int i = 0; i < 90; i++) x[pi[i]] = y[i];

    std::vector<unsigned char> dec = viterbi(x, stepMasks(6, 48, -1));
    std::memcpy(bits, dec.data(), 48);
    descramble(bits, 48);
    return facCrcOk(bits);
}

// Constellation amplitude per PAM index; level 0 selects the most significant
// index bit.
static const float pam4[2] = {0.70710678f, -0.70710678f};
static const float pam16[4] = {0.94868330f, -0.31622777f, 0.31622777f, -0.94868330f};
static const float pam64[8] = {1.08012345f, -0.15430335f, 0.46291005f, -0.77151675f,
                               0.77151675f, -0.46291005f, 0.15430335f, -1.08012345f};

bool Fec::decodeMsc(const Complex* cells, const float* weight, int n, int qam, int prot,
                    std::vector<unsigned char>& bits) {
    const float* pam;
    int levels;
    int rate[3], t0[3];

    switch (qam) {
        case 4:
            levels = 1;
            pam = pam4;
            rate[0] = rates4Qam[0];
            t0[0] = 21;
            break;
        case 16:
            if (prot < 0 || prot > 1) return false;
            levels = 2;
            pam = pam16;
            rate[0] = rates16Qam[prot][0];
            rate[1] = rates16Qam[prot][1];
            t0[0] = 13;
            t0[1] = 21;
            break;
        case 64:
            if (prot < 0 || prot > 3) return false;
            levels = 3;
            pam = pam64;
            for (int j = 0; j < 3; j++) rate[j] = rates64Qam[prot][j];
            t0[0] = 0;
            t0[1] = 13;
            t0[2] = 21;
            break;
        default:
            return false;
    }

    const int coded = 2 * n; // coded bits per level
    const int points = 1 << levels;

    // Equalized I and Q amplitudes, interleaved like the transmitted bits
    std::vector<float> amp(coded), rel(coded);
    for (int i = 0; i < n; i++) {
        amp[2 * i] = cells[i].real();
        amp[2 * i + 1] = cells[i].imag();
        rel[2 * i] = rel[2 * i + 1] = weight[i];
    }

    std::vector<unsigned char> hard(coded * levels, 0); // decisions so far, [level][position]
    std::vector<unsigned char> payload;

    for (int j = 0; j < levels; j++) {
        // Soft value of this level's bit, given the decided lower levels
        std::vector<float> y(coded);
        for (int p = 0; p < coded; p++) {
            float best[2] = {1e30f, 1e30f};
            for (int idx = 0; idx < points; idx++) {
                bool match = true;
                for (int l = 0; l < j && match; l++)
                    match = ((idx >> (levels - 1 - l)) & 1) == hard[l * coded + p];
                if (!match) continue;

                float d = amp[p] - pam[idx];
                int b = (idx >> (levels - 1 - j)) & 1;
                best[b] = std::min(best[b], d * d);
            }
            y[p] = rel[p] * (best[1] - best[0]);
        }

        // Undo this level's bit interleaver
        std::vector<float> x(coded);
        if (t0[j]) {
            std::vector<int> pi = permutation(coded, t0[j]);
            for (int i = 0; i < coded; i++) x[pi[i]] = y[i];
        } else
            x = y;

        int info, tailIdx;
        levelSizing(rate[j], coded, info, tailIdx);
        std::vector<unsigned char> masks = stepMasks(rate[j], info, tailIdx);
        std::vector<unsigned char> dec = viterbi(x, masks);
        payload.insert(payload.end(), dec.begin(), dec.begin() + info);

        // Re-encode so that the next level sees this level's decisions
        if (j + 1 < levels) {
            std::vector<unsigned char> tx = encode(dec, masks);
            tx.resize(coded, 0);
            std::vector<unsigned char> ilv(coded);
            if (t0[j]) {
                std::vector<int> pi = permutation(coded, t0[j]);
                for (int i = 0; i < coded; i++) ilv[i] = tx[pi[i]];
            } else
                ilv = tx;
            std::copy(ilv.begin(), ilv.end(), hard.begin() + j * coded);
        }
    }

    descramble(payload.data(), payload.size());
    bits.swap(payload);
    return true;
}
