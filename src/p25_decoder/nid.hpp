#pragma once

#include <cstdint>

namespace Digiham::P25 {

    // The NID (Network Identifier) is the 64-bit field that follows the frame
    // sync in every P25 data unit. It is BCH(63,16,23) protected and carries:
    //   - NAC  : 12-bit Network Access Code
    //   - DUID :  4-bit Data Unit ID (tells us what kind of frame follows)
    //
    // parse() takes the 64 logical bits of the NID (one bit per array element,
    // status symbols already removed) and returns a Nid instance, or nullptr if
    // the DUID is not one of the values defined by the standard.
    class Nid {
        public:
            static Nid* parse(const uint8_t* bits);
            uint16_t getNac() const;
            uint8_t getDataUnitId() const;
        private:
            Nid(uint16_t nac, uint8_t duid);
            uint16_t nac;
            uint8_t duid;
    };

}
