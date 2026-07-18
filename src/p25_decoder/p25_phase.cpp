#include "p25_phase.hpp"
#include "p25_meta.hpp"
#include "nid.hpp"
#include "link_control.hpp"
#include "types.hpp"

#include <cstring>

extern "C" {
#include "hamming_distance.h"
}

using namespace Digiham::P25;

// P25 frame sync 0x5575F5FF77FF as 24 digiham dibit symbols.
// (+3 -> 1, -3 -> 3; the sync uses only the outer two C4FM symbols)
const uint8_t Phase::frameSync[P25_SYNC_SIZE] = {
    1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3
};

// Walks the raw dibit buffer that starts at the frame sync, transparently
// skipping the status symbols P25 interposes every 36th dibit (one status
// symbol per 70 information bits). Tracks how many raw dibits were consumed so
// the caller knows exactly how far to advance the reader.
namespace {
    class DibitStream {
        public:
            explicit DibitStream(const unsigned char* base): base(base) {}

            unsigned char nextDibit() {
                // a status symbol sits at every raw position where pos % 36 == 35
                while ((rawPos % 36) == 35) rawPos++;
                return base[rawPos++];
            }

            // read `count` dibits, expanding each into two bits (MSB first)
            void readBits(uint8_t* out, int count) {
                for (int i = 0; i < count; i++) {
                    unsigned char d = nextDibit();
                    out[i * 2]     = (d >> 1) & 1;
                    out[i * 2 + 1] = d & 1;
                }
            }

            void skipDibits(int count) {
                for (int i = 0; i < count; i++) nextDibit();
            }

            int rawConsumed() const { return rawPos; }

        private:
            const unsigned char* base;
            int rawPos = 0;
    };

    // pack `count` bits (one bit per byte) into `count/8` output bytes, MSB first
    void packBits(const uint8_t* bits, int count, unsigned char* out) {
        std::memset(out, 0, count / 8);
        for (int i = 0; i < count; i++) {
            out[i / 8] |= (bits[i] & 1) << (7 - (i % 8));
        }
    }
}

Digiham::Phase* SyncPhase::process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) {
    uint8_t* candidate = data->getReadPointer();

    if (hamming_distance(candidate, (uint8_t*) frameSync, P25_SYNC_SIZE) <= 4) {
        return new FramePhase();
    }

    // no sync here: slide forward one dibit and try again
    data->advance(1);
    return this;
}

Digiham::Phase* FramePhase::process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) {
    unsigned char* base = data->getReadPointer();

    // confirm we are still aligned to a frame sync
    if (hamming_distance(base, (uint8_t*) frameSync, P25_SYNC_SIZE) <= 4) {
        if (++syncCount > 6) syncCount = 6;
    } else {
        if (--syncCount < 0) {
            ((MetaCollector*) meta)->reset();
            return new SyncPhase();
        }
    }

    DibitStream stream(base);

    // skip the 24 sync dibits, then read the 64-bit NID
    stream.skipDibits(P25_SYNC_SIZE);
    uint8_t nid_bits[64];
    stream.readBits(nid_bits, 32);

    Nid* nid = Nid::parse(nid_bits);
    if (nid == nullptr) {
        // NID didn't decode to a known DUID; drop this sync and resync
        data->advance(stream.rawConsumed());
        if (--syncCount < 0) {
            ((MetaCollector*) meta)->reset();
            return new SyncPhase();
        }
        return this;
    }

    ((MetaCollector*) meta)->setNac(nid->getNac());
    uint8_t duid = nid->getDataUnitId();
    delete nid;

    if (duid == P25_DUID_LDU1 || duid == P25_DUID_LDU2) {
        // read the whole LDU body (1568 logical bits = 784 dibits)
        uint8_t body[1568];
        stream.readBits(body, 784);

        // field sequence within the body:
        //   VF1 VF2 D VF3 D VF4 D VF5 D VF6 D VF7 D VF8 LSD VF9
        // where each VF is a 144-bit IMBE code word, each D a 40-bit slice of
        // the 240-bit LC/ES block, and LSD is 32 bits of low speed data.
        static const int voiceLen = 144;
        int pos = 0;
        uint8_t voice[9][144];
        uint8_t lces[240];
        int lcesPos = 0;

        for (int vf = 0; vf < 9; vf++) {
            std::memcpy(voice[vf], body + pos, voiceLen);
            pos += voiceLen;
            if (vf >= 1 && vf <= 6) {
                // a 40-bit LC/ES slice follows voice frames 2..7
                std::memcpy(lces + lcesPos, body + pos, 40);
                lcesPos += 40;
                pos += 40;
            } else if (vf == 7) {
                // low speed data follows voice frame 8
                pos += 32;
            }
        }

        // emit the voice code words (only when we trust the sync)
        if (syncCount >= 1) {
            ((MetaCollector*) meta)->setSync("voice");
            for (int vf = 0; vf < 9; vf++) {
                unsigned char* out = output->getWritePointer();
                packBits(voice[vf], 144, out);
                output->advance(18);
            }
        }

        // interpret the signalling block for call metadata
        if (duid == P25_DUID_LDU1) {
            LinkControl* lc = LinkControl::parse(lces);
            if (lc->getFormat() == P25_LCF_GROUP) {
                ((MetaCollector*) meta)->setType("group");
                ((MetaCollector*) meta)->setDestination(lc->getTalkgroup());
                ((MetaCollector*) meta)->setSource(lc->getSource());
            } else if (lc->getFormat() == P25_LCF_UNIT_TO_UNIT) {
                ((MetaCollector*) meta)->setType("individual");
                ((MetaCollector*) meta)->setDestination(lc->getTalkgroup());
                ((MetaCollector*) meta)->setSource(lc->getSource());
            }
            delete lc;
        } else { // LDU2
            EncryptionSync* es = EncryptionSync::parse(lces);
            ((MetaCollector*) meta)->setEncrypted(es->isEncrypted(), es->getAlgorithmId(), es->getKeyId());
            delete es;
        }

        // an LDU is exactly P25_LDU_DIBITS raw dibits; advance by the fixed
        // length so a trailing status symbol can't drift our alignment
        data->advance(P25_LDU_DIBITS);
        return this;
    }

    if (duid == P25_DUID_TDU || duid == P25_DUID_TDULC) {
        // end of transmission
        data->advance(stream.rawConsumed());
        ((MetaCollector*) meta)->reset();
        return new SyncPhase();
    }

    // HDU / TSDU / PDU: we've captured the NAC; skip past this unit's header and
    // resync (each subsequent data unit carries its own frame sync anyway).
    data->advance(stream.rawConsumed());
    return new SyncPhase();
}
