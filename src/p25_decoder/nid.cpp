#include "nid.hpp"
#include "types.hpp"

using namespace Digiham::P25;

Nid::Nid(uint16_t nac, uint8_t duid): nac(nac), duid(duid) {}

static bool isKnownDuid(uint8_t duid) {
    switch (duid) {
        case P25_DUID_HDU:
        case P25_DUID_TDU:
        case P25_DUID_LDU1:
        case P25_DUID_TSDU:
        case P25_DUID_LDU2:
        case P25_DUID_PDU:
        case P25_DUID_TDULC:
            return true;
        default:
            return false;
    }
}

Nid* Nid::parse(const uint8_t* bits) {
    // NAC is the first 12 bits, DUID the following 4 bits. The remaining 48 bits
    // are BCH(63,16,23) parity plus a trailing overall-parity bit.
    //
    // Note: this reads the information bits directly. The BCH parity gives room
    // for full error correction (Berlekamp-Massey + Chien search over GF(64));
    // that is a natural enhancement. For now we validate the DUID against the
    // set of values defined by the standard, and the phase layer additionally
    // gates on repeated frame-sync detection, which keeps noise out.
    uint16_t nac = 0;
    for (int i = 0; i < 12; i++) {
        nac = (nac << 1) | (bits[i] & 1);
    }

    uint8_t duid = 0;
    for (int i = 12; i < 16; i++) {
        duid = (duid << 1) | (bits[i] & 1);
    }

    if (!isKnownDuid(duid)) {
        return nullptr;
    }

    return new Nid(nac, duid);
}

uint16_t Nid::getNac() const {
    return nac;
}

uint8_t Nid::getDataUnitId() const {
    return duid;
}
