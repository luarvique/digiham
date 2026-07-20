#pragma once

#include "decoder.hpp"

namespace Digiham {

    namespace P25 {

        // Protocol-layer decoder for APCO Project 25 (P25) Phase 1 (C4FM).
        //
        // It consumes the dibit stream produced by gfsk_demodulator, locks onto the
        // P25 frame sync, decodes the NID (NAC + DUID) and, for the voice data units
        // (LDU1 / LDU2), emits the raw IMBE voice code words downstream (18 bytes per
        // frame, same packing convention the other digiham decoders use) so that
        // mbe_synthesizer / codecserver can turn them into audio.
        //
        // Link Control (LDU1) and Encryption Sync (LDU2) are parsed for call
        // metadata (NAC, talkgroup, source unit, encryption).
        class Decoder: public Digiham::Decoder {
            public:
                Decoder();
        };

    }

}
