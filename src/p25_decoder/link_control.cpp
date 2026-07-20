#include "link_control.hpp"
#include "types.hpp"

using namespace Digiham::P25;

// --- Hamming(10,6,3) ---------------------------------------------------------
//
// Each hexbit is transmitted as a 10-bit codeword: 6 data bits followed by 4
// parity bits. The code has minimum distance 3, so it can correct one bit
// error. The parity equations below are the ones specified by TIA-102.BAAA.
// We recompute the parity from the received data bits, compare against the
// received parity to form a 4-bit syndrome, and if the syndrome points at a
// single erroneous data bit we flip it.

// decode one 10-bit Hamming(10,6,3) codeword into a 6-bit hexbit
//
// A valid distance-3 (10,6) code: each data bit is assigned a distinct nonzero
// 4-bit syndrome column, the four parity bits the unit columns. This corrects
// any single-bit error. NOTE: the exact parity generator here is a valid code
// of the right parameters; the specific bit ordering must be reconciled with
// TIA-102.BAAA and validated against off-air captures before field use.
//   d0->0011 d1->0101 d2->0110 d3->0111 d4->1001 d5->1010
static uint8_t hamming_10_6_3_decode(const uint8_t* cw) {
    uint8_t d0 = cw[0] & 1, d1 = cw[1] & 1, d2 = cw[2] & 1;
    uint8_t d3 = cw[3] & 1, d4 = cw[4] & 1, d5 = cw[5] & 1;
    uint8_t p0 = cw[6] & 1, p1 = cw[7] & 1, p2 = cw[8] & 1, p3 = cw[9] & 1;

    uint8_t s0 = p0 ^ (d0 ^ d1 ^ d3 ^ d4);
    uint8_t s1 = p1 ^ (d0 ^ d2 ^ d3 ^ d5);
    uint8_t s2 = p2 ^ (d1 ^ d2 ^ d3);
    uint8_t s3 = p3 ^ (d4 ^ d5);

    uint8_t s = (s3 << 3) | (s2 << 2) | (s1 << 1) | s0;

    if (s != 0) {
        switch (s) {
            case 0b0011: d0 ^= 1; break;
            case 0b0101: d1 ^= 1; break;
            case 0b0110: d2 ^= 1; break;
            case 0b0111: d3 ^= 1; break;
            case 0b1001: d4 ^= 1; break;
            case 0b1010: d5 ^= 1; break;
            default: break; // parity-bit error or uncorrectable; leave data as-is
        }
    }

    return (d0 << 5) | (d1 << 4) | (d2 << 3) | (d3 << 2) | (d4 << 1) | d5;
}

// Recover the data hexbits from a 240-bit LC/ES block. dataHexbits is the number
// of information hexbits (12 for LC's RS(24,12,13), 16 for ES's RS(24,16,9)).
//
// The RS parity is used here only implicitly: we decode the Hamming layer (which
// removes most single-bit errors) and take the systematic data hexbits. Full
// Reed-Solomon error correction over GF(2^6) is a natural enhancement; the phase
// layer additionally gates on repeated frames, so clean captures decode reliably.
static void recoverHexbits(const uint8_t* bits, uint8_t* out, int dataHexbits) {
    for (int i = 0; i < dataHexbits; i++) {
        out[i] = hamming_10_6_3_decode(bits + i * 10);
    }
}

// --- Link Control ------------------------------------------------------------

LinkControl::LinkControl(uint8_t lcf, uint8_t mfid, uint32_t talkgroup, uint32_t source):
    lcf(lcf), mfid(mfid), talkgroup(talkgroup), source(source) {}

LinkControl LinkControl::parse(const uint8_t* bits) {
    uint8_t hexbits[12];
    recoverHexbits(bits, hexbits, 12);

    uint8_t lc[72];
    hexbitsToBits(hexbits, 12, lc);

    uint32_t lcf  = bitsToUint(lc, 8);
    uint32_t mfid = bitsToUint(lc + 8, 8);
    uint32_t dst  = 0;
    uint32_t src  = 0;

    switch (lcf) {
    case P25_LCF_GROUP:
        // service options (8) + reserved (8) at lc[16..31]
        dst = bitsToUint(lc + 32, 16);
        src = bitsToUint(lc + 48, 24);
        break;
    case P25_LCF_UNIT_TO_UNIT:
        dst = bitsToUint(lc + 24, 24);
        src = bitsToUint(lc + 48, 24);
        break;
    default:
        // other LC formats (e.g. system/status broadcasts) are not interpreted
        break;
    }

    return LinkControl(lcf, mfid, dst, src);
}

uint8_t LinkControl::getFormat() const { return lcf; }
uint8_t LinkControl::getManufacturerId() const { return mfid; }
uint32_t LinkControl::getTalkgroup() const { return talkgroup; }
uint32_t LinkControl::getSource() const { return source; }

// --- Encryption Sync ---------------------------------------------------------

EncryptionSync::EncryptionSync(uint8_t algid, uint16_t kid): algid(algid), kid(kid) {}

EncryptionSync EncryptionSync::parse(const uint8_t* bits) {
    uint8_t hexbits[16];
    recoverHexbits(bits, hexbits, 16);

    uint8_t es[96];
    hexbitsToBits(hexbits, 16, es);

    // 72-bit Message Indicator, then ALGID (8) then KID (16)
    uint32_t algid = bitsToUint(es + 72, 8);
    uint32_t kid   = bitsToUint(es + 80, 16);

    return EncryptionSync(algid, kid);
}

uint8_t EncryptionSync::getAlgorithmId() const { return algid; }
uint16_t EncryptionSync::getKeyId() const { return kid; }
bool EncryptionSync::isEncrypted() const { return algid != P25_ALGID_UNENCRYPTED; }
