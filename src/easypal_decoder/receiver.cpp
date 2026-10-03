#include "receiver.hpp"

#include <cmath>

using namespace Digiham::EasyPal;

Receiver::Receiver():
    frontEnd([this](const Frame& f) { onFrame(f); }) {
    assembler.onFile = [this](const File& f) {
        File out = f;
        out.callsign = callsign;
        if (onFile) onFile(out);
    };
}

void Receiver::push(const float* audio, int count) {
    bool wasLocked = frontEnd.isLocked();
    frontEnd.push(audio, count);
    if (wasLocked && !frontEnd.isLocked()) {
        // Lost the signal: forget what depended on it
        cellHistory.clear();
        weightHistory.clear();
        facValid = false;
    }
}

static char facChar(const unsigned char* b) {
    int v = 0;
    for (int i = 0; i < 7; i++) v = (v << 1) | b[i];
    return (char)v;
}

bool Receiver::parseFac(const unsigned char* bits) {
    int identity = (2 * bits[0] + bits[1]) % 3;
    int newOccupancy = bits[2];
    int newDepth = bits[3];
    int mscMode = bits[4];
    if (bits[6]) mscMode += 2 * bits[9];
    if (newOccupancy > 1) return false;

    int newQam;
    switch (mscMode) {
        case 0:
            newQam = 64;
            break;
        case 1:
            newQam = 16;
            break;
        case 3:
            newQam = 4;
            break;
        default:
            return false;
    }
    // audio services are not supported
    if (!bits[6]) return false;

    if (newQam != qam || newDepth != interleaverDepth) {
        cellHistory.clear();
        weightHistory.clear();
    }
    occupancy = newOccupancy;
    interleaverDepth = newDepth;
    qam = newQam;
    protection = bits[5];

    for (int i = 0; i < 3; i++) callsignChars[identity][i] = facChar(&bits[10 + 7 * i]);
    haveChars[identity] = true;
    if (haveChars[0] && haveChars[1] && haveChars[2]) {
        std::string text;
        for (int j = 0; j < 3; j++) {
            for (int i = 0; i < 3; i++) {
                if (callsignChars[j][i] >= 32) text += callsignChars[j][i];
            }
        }
        callsign = text;
    }
    return true;
}

void Receiver::onFrame(const Frame& f) {
    const int width = f.maxCarrier - f.minCarrier + 1;

    // FAC first: it tells how to read everything else
    {
        FrameLayout probe(f.mode, f.occupancy);
        Complex c[45];
        float w[45];
        for (int i = 0; i < 45; i++) {
            const CellPosition& p = probe.getFacCells()[i];
            c[i] = f.cell[p.symbol * width + p.carrier - f.minCarrier];
            w[i] = f.weight[p.symbol * width + p.carrier - f.minCarrier];
        }
        unsigned char bits[48];
        if (Fec::decodeFac(c, w, 45, bits) && parseFac(bits)) {
            facValid = true;
            facAge = 0;
            frontEnd.setOccupancy(occupancy);
        } else if (++facAge > 8)
            facValid = false;
    }
    if (!facValid) return;

    // MSC cells of this frame
    FrameLayout layout(f.mode, occupancy);

    // The front end builds frames for the occupancy it knows about. If the FAC
    // announced a wider one, this frame does not hold all the cells. The front
    // end uses the announced occupancy from the next frame on.
    if (layout.getMinCarrier() < f.minCarrier || layout.getMaxCarrier() > f.maxCarrier) return;

    const std::vector<CellPosition>& msc = layout.getMscCells();
    const int n = msc.size();
    std::vector<Complex> c(n);
    std::vector<float> w(n);
    for (int i = 0; i < n; i++) {
        c[i] = f.cell[msc[i].symbol * width + msc[i].carrier - f.minCarrier];
        w[i] = f.weight[msc[i].symbol * width + msc[i].carrier - f.minCarrier];
    }
    cellHistory.push_back(c);
    weightHistory.push_back(w);

    // Interleaving depth: 5 frames (long) or 1 frame (short)
    int D = interleaverDepth ? 1 : 5;
    while ((int)cellHistory.size() > D) {
        cellHistory.pop_front();
        weightHistory.pop_front();
    }
    if ((int)cellHistory.size() < D) return;
    decodeMsc(n);
}

void Receiver::decodeMsc(int n) {
    int D = cellHistory.size();
    std::vector<const Complex*> cp;
    std::vector<const float*> wp;
    for (int d = 0; d < D; d++) {
        cp.push_back(cellHistory[d].data());
        wp.push_back(weightHistory[d].data());
    }

    std::vector<Complex> mux = Fec::deinterleave<Complex>(cp, n, D);
    std::vector<float> rel = Fec::deinterleave<float>(wp, n, D);

    std::vector<unsigned char> bits;
    if (!Fec::decodeMsc(mux.data(), rel.data(), n, qam, protection, bits)) return;

    int len = bits.size() / 8;
    std::vector<unsigned char> bytes(len);
    for (int i = 0; i < len; i++) {
        unsigned char v = 0;
        for (int j = 0; j < 8; j++) v = (v << 1) | bits[8 * i + j];
        bytes[i] = v;
    }
    assembler.addBlock(bytes.data(), len);
}
