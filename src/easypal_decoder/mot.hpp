#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Digiham::EasyPal {

    struct File {
            unsigned short transportId = 0;
            std::string name;     // from the MOT header, may be empty
            std::string callsign; // filled in by the receiver
            std::vector<unsigned char> data;
    };

    // Reassembly of files sent as DAB-MOT objects in DRM packet mode: data blocks
    // form data groups, whose segments make up a header and a body.
    class MotAssembler {
        public:
            std::function<void(const File&)> onFile;

            // One decoded data block (bytes, CRC-16 included)
            void addBlock(const unsigned char* bytes, int len);
            void reset();

        private:
            struct Segment {
                    bool have = false;
                    std::vector<unsigned char> data;
            };
            struct Transport {
                    bool header = false, last = false, delivered = false;
                    unsigned bodySize = 0, total = 0;
                    std::string name;
                    std::vector<Segment> segments;
            };
            struct Group {
                    std::vector<unsigned char> data;
                    int type = 0;
                    bool lastSegment = false;
                    unsigned segmentNumber = 0;
                    unsigned short transportId = 0xFFFF;
            };

            std::vector<unsigned char> holding;
            int lastContinuity = -1;
            std::map<unsigned short, Transport> transports;

            bool parseGroup(Group& g);
            void header(const Group& g);
            void body(const Group& g);
            void deliver(unsigned short id, Transport& t);
    };

}
