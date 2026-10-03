#pragma once

#include "mot.hpp"
#include "ofdm.hpp"

#include <deque>
#include <string>
#include <vector>

namespace Digiham::EasyPal {

    // HamDRM receiver: audio in, complete files out
    class Receiver {
        public:
            Receiver();

            std::function<void(const File&)> onFile;

            // Feed real audio sampled at 12 kHz
            void push(const float* audio, int count);

            bool isLocked() const {
                return frontEnd.isLocked();
            }

        private:
            void onFrame(const Frame& frame);
            bool parseFac(const unsigned char* bits);
            void decodeMsc(int count);

            OfdmFrontEnd frontEnd;
            MotAssembler assembler;

            // Channel parameters from the FAC
            bool facValid = false;
            int facAge = 0;
            int occupancy = 1;
            int interleaverDepth = 1;
            int qam = 16;
            int protection = 0;
            char callsignChars[3][4] = {};
            bool haveChars[3] = {false, false, false};
            std::string callsign;

            // Recent MSC frames for the time deinterleaver
            std::deque<std::vector<Complex>> cellHistory;
            std::deque<std::vector<float>> weightHistory;
    };

}
