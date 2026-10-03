#pragma once

#include "fec.hpp"

#include <vector>

namespace Digiham::EasyPal {

    struct CellPosition {
            int symbol;
            int carrier;
    };

    // Frame geometry of a HamDRM transmission: which cell of which OFDM symbol
    // is a pilot, FAC or MSC cell, and the pilot values.
    class FrameLayout {
        public:
            FrameLayout(int mode, int occupancy);

            int getMode() const {
                return mode;
            }
            int getSymbols() const {
                return symbols;
            }
            int getMinCarrier() const {
                return minCarrier;
            }
            int getMaxCarrier() const {
                return maxCarrier;
            }
            int getUsefulLength() const {
                return usefulLength;
            }
            int getGuardLength() const {
                return guardLength;
            }

            // MSC cells of one frame, in transmission order
            const std::vector<CellPosition>& getMscCells() const {
                return mscCells;
            }
            const std::vector<CellPosition>& getFacCells() const {
                return facCells;
            }

            // Known pilot value (complex, including amplitude) at (symbol, carrier),
            // if any. The symbol is the index within the frame.
            bool getPilot(int symbol, int carrier, Complex& value) const;

            // True if (symbol, carrier) lies on the scattered pilot grid
            bool isGridPilot(int symbol, int carrier) const;

        private:
            int mode;
            int occupancy;
            int symbols;
            int minCarrier;
            int maxCarrier;
            int usefulLength;
            int guardLength;
            int width;
            std::vector<CellPosition> mscCells;
            std::vector<CellPosition> facCells;
            // per (symbol, carrier - minCarrier): 0 data, 1 pilot, 2 FAC
            std::vector<signed char> kinds;
            std::vector<Complex> values;
    };

}
