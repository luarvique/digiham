#include "mot.hpp"
#include "fec.hpp"

using namespace Digiham::EasyPal;

#define MOT_GROUP_HEADER 3
#define MOT_GROUP_BODY 4

void MotAssembler::reset() {
    holding.clear();
    lastContinuity = -1;
    transports.clear();
}

void MotAssembler::addBlock(const unsigned char* b, int len) {
    if (len < 4 || !Fec::blockCrcOk(b, len)) return;

    // Data block: header byte, optional length byte, payload, CRC-16
    bool first = b[0] & 0x80, last = b[0] & 0x40, ppi = b[0] & 0x08;
    int cont = b[0] & 0x07;
    int avail = ppi ? b[1] : len - 3;
    const unsigned char* p = b + (ppi ? 2 : 1);
    if (p + avail > b + len - 2) return;

    if (first) {
        holding.clear();
        lastContinuity = cont;
    } else {
        if (lastContinuity < 0) return;
        lastContinuity = (lastContinuity + 1) % 8;
        if (cont != lastContinuity) {
            lastContinuity = -1;
            return;
        }
    }
    holding.insert(holding.end(), p, p + avail);
    if (!last) return;

    Group g;
    g.data = holding;
    if (!parseGroup(g)) return;
    if (g.type == MOT_GROUP_HEADER)
        header(g);
    else if (g.type == MOT_GROUP_BODY)
        body(g);
}

// Data group: header, optional CRC-16, session header, user access header,
// then an MOT segment (2-byte header followed by the segment data).
bool MotAssembler::parseGroup(Group& g) {
    std::vector<unsigned char>& d = g.data;
    if (d.size() < 2) return false;

    bool ext = d[0] & 0x80, crc = d[0] & 0x40, session = d[0] & 0x20, user = d[0] & 0x10;
    g.type = d[0] & 0x07;

    if (crc) {
        if (d.size() < 4 || !Fec::blockCrcOk(d.data(), d.size())) return false;
        d.resize(d.size() - 2);
    }

    size_t pos = 2 + (ext ? 2 : 0);
    if (session) {
        if (pos + 2 > d.size()) return false;
        g.segmentNumber = (d[pos] & 0x7F) * 256 + d[pos + 1];
        g.lastSegment = d[pos] & 0x80;
        pos += 2;
    }
    if (user) {
        if (pos + 1 > d.size()) return false;
        unsigned char uah = d[pos++];
        unsigned li = uah & 0x0F;
        if ((uah & 0x10) && li >= 2) {
            if (pos + 2 > d.size()) return false;
            g.transportId = d[pos] * 256 + d[pos + 1];
        }
        pos += li;
    }
    if (pos + 2 > d.size()) return false;
    pos += 2; // segment header (size field not needed)
    d.erase(d.begin(), d.begin() + pos);
    return true;
}

void MotAssembler::header(const Group& g) {
    const std::vector<unsigned char>& d = g.data;
    if (d.size() < 7) return;

    Transport& t = transports[g.transportId];
    unsigned size = (d[0] << 20) + (d[1] << 12) + (d[2] << 4) + (d[3] >> 4);

    // A name (and so a transport ID) may be reused for different content;
    // a different body size means a new object.
    if ((t.header || t.delivered) && t.bodySize != size) t = Transport();
    if (t.delivered) return;
    t.header = true;
    t.bodySize = size;

    // Header extension: parameters with a PLI in the top two bits
    size_t pos = 7;
    while (pos < d.size()) {
        unsigned pli = d[pos] >> 6, id = d[pos] & 0x3F;
        size_t len = 0;
        ++pos;
        if (pli == 1)
            len = 1;
        else if (pli == 2)
            len = 4;
        else if (pli == 3) {
            if (pos >= d.size()) return;
            if (d[pos] & 0x80) {
                if (pos + 2 > d.size()) return;
                len = 256 * (d[pos] & 0x7F) + d[pos + 1];
                pos += 2;
            } else {
                len = d[pos] & 0x7F;
                ++pos;
            }
        }
        if (pos + len > d.size()) return;
        if (id == 12 && len > 1) t.name.assign((const char*)&d[pos + 1], len - 1); // content name
        pos += len;
    }
    deliver(g.transportId, t);
}

void MotAssembler::body(const Group& g) {
    Transport& t = transports[g.transportId];
    if (t.delivered || g.segmentNumber > 8192) return;

    if (g.lastSegment) {
        t.total = g.segmentNumber + 1;
        t.last = true;
    }
    if (t.segments.size() < g.segmentNumber + 1) t.segments.resize(g.segmentNumber + 1);
    t.segments[g.segmentNumber].have = true;
    t.segments[g.segmentNumber].data = g.data;
    deliver(g.transportId, t);
}

void MotAssembler::deliver(unsigned short id, Transport& t) {
    if (t.delivered || !t.header || !t.last || t.segments.size() < t.total) return;
    for (unsigned i = 0; i < t.total; i++)
        if (!t.segments[i].have) return;

    File f;
    f.transportId = id;
    f.name = t.name;
    for (unsigned i = 0; i < t.total; i++)
        f.data.insert(f.data.end(), t.segments[i].data.begin(), t.segments[i].data.end());
    if (t.bodySize && f.data.size() > t.bodySize) f.data.resize(t.bodySize);
    t.delivered = true;
    if (onFile) onFile(f);
}
