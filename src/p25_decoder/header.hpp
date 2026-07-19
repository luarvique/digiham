#pragma once

#include <cstdint>

namespace Digiham::P25 {

    // Header Word carried in an HDU (Header Data Unit), the frame that precedes
    // the first LDU1 of a P25 Phase 1 voice transmission.
    //
    // On air the header body is a 648-bit block: 36 hexbits, each expanded to an
    // 18-bit codeword by the shortened (18,6,8) Golay code, the 36 hexbits
    // together forming an RS(36,20,17) codeword (20 data hexbits + 16 parity
    // hexbits). parse() takes those 648 logical bits, recovers the 120-bit data
    // word and interprets it.
    //
    // The 120-bit data word is laid out as:
    //   MI (72) | MFID (8) | ALGID (8) | KID (16) | TGID (16)
    // where MI is the encryption Message Indicator (not interpreted here, same
    // as EncryptionSync::parse in link_control.hpp), MFID the manufacturer ID,
    // ALGID/KID identify the encryption algorithm and key, and TGID is the
    // talkgroup that the following LDU1/LDU2 pair belongs to -- it is sent here
    // so a receiver can identify the call before the Link Control word arrives
    // in LDU1.
    class Header {
        public:
            static Header parse(const uint8_t* bits);
            uint8_t getManufacturerId() const;
            uint8_t getAlgorithmId() const;
            uint16_t getKeyId() const;
            uint32_t getTalkgroup() const;
            bool isEncrypted() const;
        private:
            Header(uint8_t mfid, uint8_t algid, uint16_t kid, uint32_t talkgroup);
            uint8_t mfid;
            uint8_t algid;
            uint16_t kid;
            uint32_t talkgroup;
    };

}
