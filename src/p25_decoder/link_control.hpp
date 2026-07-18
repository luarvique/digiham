#pragma once

#include <cstdint>

namespace Digiham::P25 {

    // Link Control word carried in an LDU1.
    //
    // On air the LC is a 240-bit block: 24 hexbits, each expanded to 10 bits by
    // Hamming(10,6,3), the 24 hexbits together forming a shortened RS(24,12,13)
    // codeword (12 data hexbits + 12 parity hexbits). parse() takes those 240
    // logical bits, recovers the 72-bit LC word and interprets it.
    //
    // The 72-bit LC word is laid out as:
    //   LCF  (8)  | MFID (8) | 56 bits of format-dependent payload
    // For a Group Voice Channel User (LCF 0x00):
    //   ... | service options (8) | reserved (8) | group address (16) | source (24)
    // For a Unit-to-Unit Voice Channel User (LCF 0x03):
    //   ... | target address (24) | source (24)
    class LinkControl {
        public:
            static LinkControl parse(const uint8_t* bits);
            uint8_t getFormat() const;
            uint8_t getManufacturerId() const;
            uint32_t getTalkgroup() const;   // group / target address
            uint32_t getSource() const;      // source unit address
        private:
            LinkControl(uint8_t lcf, uint8_t mfid, uint32_t talkgroup, uint32_t source);
            uint8_t lcf;
            uint8_t mfid;
            uint32_t talkgroup;
            uint32_t source;
    };

    // Encryption Sync word carried in an LDU2 (240-bit block, same FEC family as
    // the LC but a shortened RS(24,16,9) codeword: 16 data hexbits + 8 parity).
    // The 96-bit data word carries the 72-bit Message Indicator, an 8-bit
    // Algorithm ID (ALGID) and a 16-bit Key ID (KID).
    class EncryptionSync {
        public:
            static EncryptionSync parse(const uint8_t* bits);
            uint8_t getAlgorithmId() const;
            uint16_t getKeyId() const;
            bool isEncrypted() const;
        private:
            EncryptionSync(uint8_t algid, uint16_t kid);
            uint8_t algid;
            uint16_t kid;
    };

}
