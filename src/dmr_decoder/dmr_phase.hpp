#pragma once

#include "phase.hpp"
#include "embedded.hpp"
#include "talkeralias.hpp"

#define SYNC_SIZE 24
#define CACH_SIZE 12

// full frame including CACH, SYNC and the two payload blocks
#define FRAME_SIZE 144

#define SYNCTYPE_DATA 1
#define SYNCTYPE_VOICE 2

#define META_TYPE_DIRECT 1
#define META_TYPE_GROUP 2

namespace Digiham::Dmr {

    class Phase: public Digiham::Phase {
        protected:
            // detects the type of sync (SYNCTYPE_DATA / SYNCTYPE_VOICE).
            // if dmoSlot is passed, it will be set to the timeslot (0 or 1) encoded in a direct mode (DMO) sync
            // pattern, or -1 for repeater (BS / MS sourced) sync patterns.
            int getSyncType(unsigned char* potentialSync, int* dmoSlot = nullptr);

            const unsigned char dmr_bs_data_sync[SYNC_SIZE] =  { 3,1,3,3,3,3,1,1,1,3,3,1,1,3,1,1,3,1,3,3,1,1,3,1 };
            const unsigned char dmr_bs_voice_sync[SYNC_SIZE] = { 1,3,1,1,1,1,3,3,3,1,1,3,3,1,3,3,1,3,1,1,3,3,1,3 };
            const unsigned char dmr_ms_data_sync[SYNC_SIZE] =  { 3,1,1,1,3,1,1,3,3,3,1,3,1,3,3,3,3,1,1,3,1,1,1,3 };
            const unsigned char dmr_ms_voice_sync[SYNC_SIZE] = { 1,3,3,3,1,3,3,1,1,1,3,1,3,1,1,1,1,3,3,1,3,3,3,1 };

            // direct mode operation (DMO) sync patterns as per ETSI TS 102 361-1, 9.1.1
            // TS1 data:  F7FDD5DDFD55 / TS1 voice: 5D577F7757FF
            // TS2 data:  D7557F5FF7F5 / TS2 voice: 7DFFD5F55D5F
            const unsigned char dmr_dmo_ts1_data_sync[SYNC_SIZE] =  { 3,3,1,3,3,3,3,1,3,1,1,1,3,1,3,1,3,3,3,1,1,1,1,1 };
            const unsigned char dmr_dmo_ts1_voice_sync[SYNC_SIZE] = { 1,1,3,1,1,1,1,3,1,3,3,3,1,3,1,3,1,1,1,3,3,3,3,3 };
            const unsigned char dmr_dmo_ts2_data_sync[SYNC_SIZE] =  { 3,1,1,3,1,1,1,1,1,3,3,3,1,1,3,3,3,3,1,3,3,3,1,1 };
            const unsigned char dmr_dmo_ts2_voice_sync[SYNC_SIZE] = { 1,3,3,1,3,3,3,3,3,1,1,1,3,3,1,1,1,1,3,1,1,1,3,3 };

            // in DMR frames, the sync is in the middle. therefor, we need to be able to look at previous data once we
            // find a sync.
            // The data of one channel is 54 symbols, and the length of the CACH in basestation transmissions is 12.
            unsigned int syncOffset = 54 + CACH_SIZE;
    };

    class SyncPhase: public Phase {
        public:
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    class FramePhase: public Phase {
        public:
            FramePhase();
            ~FramePhase() override;
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
            void setSlotFilter(unsigned char filter);
        private:
            void handleLc(Lc* lc);
            int syncCount = 0;
            int slot = -1;
            int slotStability = 0;
            int syncTypes[2] = {-1, -1};
            int slotSyncCount[2] = {0, 0};
            EmbeddedCollector* embCollectors[2];
            TalkerAliasCollector* talkerAliasCollector[2];
            int activeSlot = -1;
            unsigned char slotFilter = 3;
            unsigned char superframeCounter[2] = {0, 0};

            // set as soon as a direct mode (DMO) sync pattern is detected.
            // in direct mode, there is no CACH, and typically only one of the two timeslots carries a transmission,
            // so slot tracking and sync loss detection work differently.
            bool directMode = false;

            // number of frames without any sync or embedded signalling; used to detect the end of a direct mode
            // transmission since the idle timeslot must not count against the global sync counter.
            int idleFrames = 0;
    };

}
