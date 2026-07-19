#pragma once

// P25 Phase 1 Common Air Interface constants (TIA-102.BAAA)

// Data Unit IDs (DUID), carried in the NID
#define P25_DUID_HDU    0x0   // Header Data Unit
#define P25_DUID_TDU    0x3   // Simple Terminator Data Unit (no Link Control)
#define P25_DUID_LDU1   0x5   // Logical Link Data Unit 1 (voice + Link Control)
#define P25_DUID_TSDU   0x7   // Trunking Signalling Data Unit
#define P25_DUID_LDU2   0xA   // Logical Link Data Unit 2 (voice + Encryption Sync)
#define P25_DUID_PDU    0xC   // Packet Data Unit
#define P25_DUID_TDULC  0xF   // Terminator Data Unit with Link Control
#define P25_DUID_BAD    0xFF  // Illegal or unknown data unit

// Link Control Format (LCF) values we care about
#define P25_LCF_GROUP        0x00   // Group Voice Channel User
#define P25_LCF_UNIT_TO_UNIT 0x03   // Unit to Unit Voice Channel User

// Encryption algorithm IDs (ALGID)
#define P25_ALGID_UNENCRYPTED 0x80  // clear / no encryption

namespace Digiham::P25 {

    // collect given number of bits into a big-endian word
    static uint32_t bitsToUint(const uint8_t* bits, int count) {
        uint32_t v = 0;
        for (int i = 0; i < count; i++) v = (v << 1) | (bits[i] & 1);
        return v;
    }

    // pack an array of 6-bit hexbits into a big-endian bit array
    static void hexbitsToBits(const uint8_t* hexbits, int count, uint8_t* bits) {
        for (int i = 0; i < count; i++) {
            for (int b = 0; b < 6; b++) {
                bits[i * 6 + b] = (hexbits[i] >> (5 - b)) & 1;
            }
        }
    }

    // pack given number of bits (one bit per byte) into big-endian bytes
    static void packBits(const uint8_t* bits, int count, unsigned char* out) {
        count &= ~7;
        for (int i = 0; i < count; i += 8) {
            out[i >> 3] = bitsToUint(bits + i, 8);
        }
    }

}
