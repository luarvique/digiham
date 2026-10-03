#include "frame_layout.hpp"
#include "tables.hpp"

#include <cmath>

using namespace Digiham::EasyPal;

FrameLayout::FrameLayout(int mode, int occupancy):
    mode(mode),
    occupancy(occupancy) {
    const ModeInfo& m = modes[mode];
    symbols = m.symbols;
    usefulLength = m.tu;
    guardLength = m.tg;
    minCarrier = m.kmin[occupancy];
    maxCarrier = m.kmax[occupancy];
    width = maxCarrier - minCarrier + 1;

    kinds.assign(symbols * width, 0);
    values.assign(symbols * width, Complex(0, 0));

    auto set = [&](int s, int k, int kind, Complex v) {
        if (k < minCarrier || k > maxCarrier) return;
        kinds[s * width + k - minCarrier] = kind;
        values[s * width + k - minCarrier] = v;
    };
    auto phasor = [](int theta1024, float amp) {
        double a = 2.0 * M_PI * theta1024 / 1024.0;
        return Complex((float)(amp * cos(a)), (float)(amp * sin(a)));
    };
    const float root2 = std::sqrt(2.0f);

    for (int s = 0; s < symbols; s++) {
        // Scattered gain-reference grid
        int n = s % m.y, blk = s / m.y;
        for (int k = minCarrier; k <= maxCarrier; k++) {
            if (((k - m.k0 - m.x * n) % (m.x * m.y) + m.x * m.y) % (m.x * m.y) != 0) continue;
            int p = (k - m.k0 - m.x * n) / (m.x * m.y);
            int theta = (4 * m.z256[n][blk] + p * m.w1024[n][blk] + p * p * (1 + s) * m.q1024) % 1024;
            float amp = root2;
            for (int b = 0; b < 4; b++)
                if (k == m.boost[occupancy][b]) amp = 2.0f;
            set(s, k, 1, phasor(theta, amp));
        }
        // Frequency references: every symbol
        for (int i = 0; i < 3; i++)
            set(s, m.freqK[i], 1, phasor(m.freqTheta[i], root2));
    }
    // Time references: first symbol only
    for (int i = 0; i < m.timeCount; i++)
        set(0, m.timeK[i], 1, phasor(m.timeTheta[i], root2));

    // FAC cells
    const int (*fac)[2] = (mode == 0) ? facCellsA : facCellsB;
    for (int i = 0; i < 45; i++) {
        CellPosition c = {fac[i][0], fac[i][1]};
        facCells.push_back(c);
        kinds[c.symbol * width + c.carrier - minCarrier] = 2;
    }

    // Everything else carries the MSC
    for (int s = 0; s < symbols; s++)
        for (int k = minCarrier; k <= maxCarrier; k++)
            if (kinds[s * width + k - minCarrier] == 0) {
                CellPosition c = {s, k};
                mscCells.push_back(c);
            }
}

bool FrameLayout::getPilot(int symbol, int carrier, Complex& value) const {
    if (carrier < minCarrier || carrier > maxCarrier) return false;
    if (kinds[symbol * width + carrier - minCarrier] != 1) return false;
    value = values[symbol * width + carrier - minCarrier];
    return true;
}

bool FrameLayout::isGridPilot(int symbol, int carrier) const {
    const ModeInfo& m = modes[mode];
    int n = symbol % m.y;
    int d = carrier - m.k0 - m.x * n;
    int period = m.x * m.y;
    return ((d % period) + period) % period == 0;
}
