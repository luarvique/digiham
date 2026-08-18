#include "dmr_decoder.hpp"
#include "dmr_phase.hpp"
#include "dmo_phase.hpp"
#include "dmr_meta.hpp"

using namespace Digiham::Dmr;

Decoder::Decoder(): Digiham::Decoder(new SyncPhase(), new MetaCollector()) {}

void Decoder::setSlotFilter(unsigned char filter) {
    slotFilter = filter;
    auto framePhase = dynamic_cast<FramePhase*>(currentPhase);
    if (framePhase != nullptr) {
        framePhase->setSlotFilter(slotFilter);
    }
    // DMO phases don't derive from FramePhase, so they need to be handled separately.
    auto dmoPhase = dynamic_cast<DmoPhase*>(currentPhase);
    if (dmoPhase != nullptr) {
        dmoPhase->setSlotFilter(slotFilter);
    }
}

void Decoder::setPhase(Digiham::Phase *phase) {
    Digiham::Decoder::setPhase(phase);
    auto framePhase = dynamic_cast<FramePhase*>(currentPhase);
    if (framePhase != nullptr) {
        framePhase->setSlotFilter(slotFilter);
    }
    // DMO phases don't derive from FramePhase, so they need to be handled separately.
    auto dmoPhase = dynamic_cast<DmoPhase*>(currentPhase);
    if (dmoPhase != nullptr) {
        dmoPhase->setSlotFilter(slotFilter);
    }
}