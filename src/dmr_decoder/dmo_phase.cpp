#include "dmo_phase.hpp"
#include "dmr_meta.hpp"
#include "cach.hpp"
#include "emb.hpp"
#include "slottype.hpp"
#include "lc.hpp"

extern "C" {
#include "hamming_distance.h"
#include "bptc_196_96.h"
}

#include <cstring>

using namespace Digiham::Dmr;

int DmoPhase::getMsSyncType(unsigned char *potentialSync) {
    // Direct mode is identified by the MS (mobile station) sync patterns only. A DMO decoder must not accept BS
    // syncs: those come from a repeater and are handled by the repeater FramePhase.
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_ms_data_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_DATA;
    }
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_ms_voice_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_VOICE;
    }
    return -1;
}

int DmoSyncPhase::getRequiredData() {
    return FRAME_SIZE;
}

Digiham::Phase* DmoSyncPhase::process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) {
    if (getMsSyncType(data->getReadPointer() + syncOffset) > 0) {
        return new DmoFramePhase();
    }
    // no sync yet: advance one symbol and keep searching
    data->advance(1);
    return this;
}

DmoFramePhase::DmoFramePhase():
    embCollector(new EmbeddedCollector()),
    talkerAliasCollector(new TalkerAliasCollector())
{}

DmoFramePhase::~DmoFramePhase() {
    delete embCollector;
    delete talkerAliasCollector;
}

int DmoFramePhase::getRequiredData() {
    return FRAME_SIZE;
}

void DmoFramePhase::setSlotFilter(unsigned char filter) {
    slotFilter = filter;
}

Digiham::Phase* DmoFramePhase::process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) {
    // Direct mode uses a single pinned logical slot. There is no CACH and therefore no slot alternation: this is
    // the core difference from the repeater path, and it is what stops the timeslot from flipping on DMO traffic.
    auto metaCollector = (MetaCollector*) meta;
    metaCollector->setDirectMode(true);

    int detectedSync = getMsSyncType(data->getReadPointer() + syncOffset);
    if (detectedSync > 0) {
        // a fresh sync: cap the sync counters and update the slot state
        if (++syncCount > 5) syncCount = 5;
        if (++slotSyncCount > 5) slotSyncCount = 5;

        bool softReset = syncType == SYNCTYPE_VOICE && detectedSync != syncType;
        syncType = detectedSync;
        metaCollector->withSlot(DMO_SLOT, [detectedSync, softReset] (Slot* s) {
            s->setSync(detectedSync);
            if (softReset) s->softReset();
        });
        superframeCounter = 0;
        embCollector->reset();
    } else if (syncType == SYNCTYPE_VOICE && superframeCounter < 5) {
        // voice superframe: the frames between syncs (B..F) carry embedded signalling instead of a sync
        superframeCounter++;

        uint16_t emb_data = 0;
        for (int i = 0; i < 2; i++) {
            unsigned int offset = syncOffset + i * 20;
            unsigned char* raw = data->getReadPointer() + offset;
            for (int k = 0; k < 4; k++) {
                emb_data = (emb_data << 2) | raw[k];
            }
        }

        Emb* emb = Emb::parse(emb_data);
        if (emb != nullptr) {
            // a valid EMB counts as continued sync
            if (++syncCount > 5) syncCount = 5;
            if (++slotSyncCount > 5) slotSyncCount = 5;

            unsigned char embedded_data[4] = {0};
            unsigned char* emb_raw = data->getReadPointer() + syncOffset + 4;
            for (int i = 0; i < 16; i++) {
                embedded_data[i / 4] |= emb_raw[i] << (6 - (i % 4) * 2);
            }

            switch (emb->getLcss()) {
                case LCSS_SINGLE:
                    break;
                case LCSS_START:
                    embCollector->reset();
                    // fall through
                case LCSS_CONTINUATION:
                    embCollector->collect(embedded_data);
                    break;
                case LCSS_STOP: {
                    embCollector->collect(embedded_data);
                    Lc* lc = embCollector->getLc();
                    if (lc != nullptr) {
                        handleLc(lc);
                        delete lc;
                    }
                    embCollector->reset();
                    break;
                }
            }
            delete emb;
        } else {
            // no sync and no EMB: decay the sync counters
            if (--slotSyncCount < 0) {
                slotSyncCount = 0;
                syncType = -1;
                metaCollector->withSlot(DMO_SLOT, [] (Slot* s) { s->reset(); });
                active = false;
            }
            if (--syncCount < 0) {
                metaCollector->reset();
                return new DmoSyncPhase();
            }
        }
    } else {
        // a sync was expected here but not found: decay the sync counters
        superframeCounter = 0;
        embCollector->reset();
        if (--slotSyncCount < 0) {
            slotSyncCount = 0;
            syncType = -1;
            metaCollector->withSlot(DMO_SLOT, [] (Slot* s) { s->reset(); });
            active = false;
        }
        if (--syncCount < 0) {
            metaCollector->reset();
            return new DmoSyncPhase();
        }
    }

    if (syncType == SYNCTYPE_VOICE) {
        // emit AMBE voice unless this slot is muted by the slot filter
        if ((DMO_SLOT + 1) & slotFilter) {
            active = true;
            unsigned char* payload = output->getWritePointer();
            std::memset(payload, 0, 27);

            // first half (payload immediately after where the CACH would be in a repeater frame; the offset is
            // sync-relative so this matches the repeater extraction exactly)
            unsigned char* payloadRaw = data->getReadPointer() + CACH_SIZE;
            for (int i = 0; i < 54; i++) {
                payload[i / 4] |= (payloadRaw[i] & 3) << (6 - 2 * (i % 4));
            }
            // second half (after the sync)
            payloadRaw += 54 + SYNC_SIZE;
            for (int i = 0; i < 54; i++) {
                payload[(i + 54) / 4] |= (payloadRaw[i] & 3) << (6 - 2 * ((i + 54) % 4));
            }
            output->advance(27);
        }
    } else {
        active = false;
        talkerAliasCollector->reset();

        if (syncType == SYNCTYPE_DATA) {
            uint32_t slot_type = 0;
            unsigned char* slot_type_raw = data->getReadPointer() + syncOffset - 5;
            for (int i = 0; i < 5; i++) {
                slot_type = (slot_type << 2) | (slot_type_raw[i] & 3);
            }
            slot_type_raw = data->getReadPointer() + syncOffset + SYNC_SIZE;
            for (int i = 0; i < 5; i++) {
                slot_type = (slot_type << 2) | (slot_type_raw[i] & 3);
            }

            SlotType* slotType = SlotType::parse(slot_type);
            if (slotType != nullptr) {
                uint8_t data_type = slotType->getDataType();
                if (data_type != DATA_TYPE_RATE_3_4_DATA) {
                    uint8_t payload[25] = { 0 };
                    unsigned char* payloadRaw = data->getReadPointer() + CACH_SIZE;
                    for (int k = 0; k < 49; k++) {
                        payload[k / 4] |= (payloadRaw[k] & 3) << (6 - 2 * (k % 4));
                    }
                    payloadRaw += 54 + SYNC_SIZE + 5;
                    for (int k = 0; k < 49; k++) {
                        payload[(k + 49) / 4] |= (payloadRaw[k] & 3) << (6 - 2 * ((k + 49) % 4));
                    }

                    uint8_t lc_data[12] = { 0 };
                    if (bptc_196_96(payload, lc_data)) {
                        if (data_type == DATA_TYPE_VOICE_LC) {
                            Lc* lc = Lc::parseFromVoiceHeader(lc_data);
                            if (lc != nullptr) {
                                handleLc(lc);
                                delete lc;
                            }
                        } else if (data_type == DATA_TYPE_TERMINATOR_LC || data_type == DATA_TYPE_IDLE) {
                            metaCollector->withSlot(DMO_SLOT, [] (Slot* s) { s->softReset(); });
                        }
                    }
                }
                delete slotType;
            }
        } else {
            metaCollector->withSlot(DMO_SLOT, [] (Slot* s) { s->reset(); });
        }
    }

    data->advance(FRAME_SIZE);
    return this;
}

void DmoFramePhase::handleLc(Lc* lc) {
    unsigned char opcode = lc->getOpCode();
    switch (opcode) {
        case LC_OPCODE_GROUP:
        case LC_OPCODE_UNIT_TO_UNIT:
            ((MetaCollector*) meta)->withSlot(DMO_SLOT, [lc] (Slot* s) { s->setFromLc(lc); });
            break;
    }
}
