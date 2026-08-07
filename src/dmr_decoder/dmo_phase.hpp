#pragma once

// Direct Mode Operation (DMO) / simplex DMR decoding.
//
// This is a dedicated decoder path for DMR transmissions that are sourced directly by a mobile station (MS)
// rather than through a repeater (base station, BS). It is deliberately kept separate from the repeater
// FramePhase because the two differ in ways that do not mix cleanly:
//
//   * DMO has no CACH, so the timeslot cannot be (and must not be) derived from a TACT. There is a single
//     logical stream; the slot is pinned rather than alternated. Trying to run the repeater slot-alternation
//     logic on a simplex signal makes the reported timeslot flip on every burst.
//   * DMO is identified by the MS sync patterns (dmr_ms_*), not the BS sync patterns. Real DMO traffic uses
//     the MS sync; the ETSI "DMO TS1/TS2" sync variants are a separate, rarely-seen framing and are not what
//     typical simplex radios transmit.
//   * DMO is burst/gap on air (a ~288-symbol period: an active burst followed by an idle half), but at this
//     point in the chain the demodulator has already produced a continuous symbol stream, so decoding still
//     operates on 144-symbol frames aligned to the sync, exactly as the repeater path does.
//
// The actual payload/AMBE extraction, embedded signalling and LC handling are identical to the repeater case,
// so this module reuses the shared building blocks (Emb, EmbeddedCollector, Lc, SlotType, bptc, the MetaCollector
// and its Slot objects). Only the framing/slot semantics are re-implemented here.
//
// The design mirrors dsd-neo's separation of dmr_ms.c (mobile station / direct mode) from dmr_bs.c (base
// station / repeater), where MS voice is pinned to a single slot and never derives a timeslot from signalling.

#include "phase.hpp"
#include "dmr_phase.hpp"
#include "embedded.hpp"
#include "talkeralias.hpp"

namespace Digiham::Dmr {

    // Shared DMO constants and MS sync matching. DmoPhase inherits the sync patterns and syncOffset from the
    // repeater Phase (they are identical); it only adds direct-mode-specific sync classification.
    class DmoPhase: public Phase {
        protected:
            // Returns SYNCTYPE_VOICE / SYNCTYPE_DATA if the window matches an MS (direct mode) sync, or -1
            // otherwise. Unlike the repeater getSyncType, this only accepts the MS syncs: a DMO decoder must not
            // lock onto a BS sync (that would be a repeater signal, handled by the other path).
            int getMsSyncType(unsigned char* potentialSync);
    };

    // Searches the incoming symbol stream for an MS sync. Once found, hands over to DmoFramePhase. This is the
    // entry state for the DMO decoder.
    class DmoSyncPhase: public DmoPhase {
        public:
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    // Decodes consecutive DMO frames once a sync has been acquired. Pins the slot to a single logical slot (0),
    // flags direct mode in the metadata, extracts AMBE voice and embedded/LC signalling, and tears down back to
    // DmoSyncPhase when the sync is lost.
    class DmoFramePhase: public DmoPhase {
        public:
            DmoFramePhase();
            ~DmoFramePhase() override;
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
            void setSlotFilter(unsigned char filter);
        private:
            void handleLc(Lc* lc);

            // the single logical slot used for all direct mode traffic
            static const int DMO_SLOT = 0;

            int syncCount = 0;
            int syncType = -1;
            int slotSyncCount = 0;
            unsigned char superframeCounter = 0;
            EmbeddedCollector* embCollector;
            TalkerAliasCollector* talkerAliasCollector;
            bool active = false;
            unsigned char slotFilter = 3;
    };

}
