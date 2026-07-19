#include "header.hpp"
#include "types.hpp"

using namespace Digiham::P25;

// --- shortened Golay(18,6,8) -------------------------------------------------
//
// The HDU protects its 36 hexbits with the shortened (18,6,8) Golay code
// rather than the Hamming(10,6,3) code used for the LC/ES blocks in
// link_control.cpp: a 6-bit hexbit is expanded to an 18-bit systematic
// codeword (6 data bits followed by 12 parity bits).
//
// This is the standard extended (24,12,8) binary Golay code, shortened by
// fixing the upper 6 message bits to zero and dropping them from the
// transmitted word. That means there are only 64 valid codewords, so rather
// than hand-deriving syndrome equations (error-prone for a distance-8 code)
// we decode by nearest-codeword search: encode all 64 possible hexbits and
// keep whichever candidate is closest, in Hamming distance, to the received
// word. With a minimum distance of 8 this reliably corrects up to 3 bit
// errors, same guarantee a syndrome decoder would give, but the encoder
// (and therefore the whole search space) is built directly from the
// well-known Golay generator matrix, which is easy to verify independently.
//
// B is the matrix appearing in the systematic generator matrix G = [I12 | B]
// of the extended binary Golay code (only its first 6 rows are used here,
// since the other 6 message bits are always zero in the shortened code).
// As with the Hamming code in link_control.cpp, the algebraic code itself is
// exact, but the bit ordering actually used on air still needs to be
// reconciled with TIA-102.BAAA and validated against off-air captures
// before field use.

static const uint8_t B[6][12] = {
    {1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 1},
    {1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0},
    {1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1},
    {1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 0},
    {1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1},
};

// encode a 6-bit hexbit into its 18-bit systematic Golay(18,6,8) codeword
static void golay_18_6_8_encode(uint8_t data, uint8_t* out /* 18 */) {
    for (int i = 0; i < 6; i++) out[i] = (data >> (5 - i)) & 1;
    for (int j = 0; j < 12; j++) {
        uint8_t p = 0;
        for (int i = 0; i < 6; i++) p ^= (out[i] & B[i][j]);
        out[6 + j] = p;
    }
}

// decode an 18-bit received word to the nearest of the 64 valid hexbit
// codewords; corrects up to 3 bit errors
static uint8_t golay_18_6_8_decode(const uint8_t* cw) {
    uint8_t best = 0;
    int bestDist = 19;

    for (int candidate = 0; candidate < 64; candidate++) {
        uint8_t codeword[18];
        golay_18_6_8_encode((uint8_t) candidate, codeword);

        int dist = 0;
        for (int i = 0; i < 18; i++) {
            if (codeword[i] != (cw[i] & 1)) dist++;
        }

        if (dist < bestDist) {
            bestDist = dist;
            best = (uint8_t) candidate;
        }
    }
    return best;
}

// Recover the 20 data hexbits from the 648-bit HDU body. As with
// recoverHexbits() in link_control.cpp, this only decodes the Golay
// layer and takes the systematic data hexbits; full Reed-Solomon error
// correction over the 36-hexbit RS(36,20,17) codeword is a natural
// enhancement.
static void recoverHexbits(const uint8_t* bits, uint8_t* out) {
    for (int i = 0; i < 20; i++) {
        out[i] = golay_18_6_8_decode(bits + i * 18);
    }
}

// --- Header --------------------------------------------------------------

Header::Header(uint8_t mfid, uint8_t algid, uint16_t kid, uint32_t talkgroup):
    mfid(mfid), algid(algid), kid(kid), talkgroup(talkgroup) {}

Header Header::parse(const uint8_t* bits) {
    uint8_t hexbits[20];
    recoverHexbits(bits, hexbits);

    uint8_t data[120];
    hexbitsToBits(hexbits, 20, data);

    // data[0..71] is the Message Indicator; not interpreted here (see
    // EncryptionSync::parse in link_control.cpp)
    uint32_t mfid  = bitsToUint(data + 72, 8);
    uint32_t algid = bitsToUint(data + 80, 8);
    uint32_t kid   = bitsToUint(data + 88, 16);
    uint32_t tgid  = bitsToUint(data + 104, 16);

    return Header(mfid, algid, kid, tgid);
}

uint8_t Header::getManufacturerId() const { return mfid; }
uint8_t Header::getAlgorithmId() const { return algid; }
uint16_t Header::getKeyId() const { return kid; }
uint32_t Header::getTalkgroup() const { return talkgroup; }
bool Header::isEncrypted() const { return algid != P25_ALGID_UNENCRYPTED; }
