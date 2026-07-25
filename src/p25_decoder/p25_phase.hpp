#pragma once

#include "phase.hpp"

#include <cstdint>

#include <csdr/reader.hpp>
#include <csdr/writer.hpp>

// The P25 frame sync is 48 bits (24 C4FM symbols / dibits). In digiham's symbol
// convention (+1 -> 0, +3 -> 1, -1 -> 2, -3 -> 3) the sync 0x5575F5FF77FF maps
// to the 24 dibit values below.
#define P25_SYNC_SIZE 24

// A P25 Phase 1 LDU (voice) frame is 1728 bits = 864 dibits over the air,
// including the frame sync and the interspersed status symbols.
#define P25_LDU_DIBITS 864

namespace Digiham::P25 {

    class Phase: public Digiham::Phase {
        protected:
            static const uint8_t frameSync[P25_SYNC_SIZE];
    };

    // searches the incoming dibit stream for the P25 frame sync
    class SyncPhase: public Phase {
        public:
            int getRequiredData() override { return P25_SYNC_SIZE; }
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    // sync has been acquired; decode NID and dispatch on the DUID
    class FramePhase: public Phase {
        public:
            int getRequiredData() override { return P25_LDU_DIBITS; }
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
        private:
            int syncCount = 0;
            bool encrypted = false;
    };

}
