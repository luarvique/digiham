// OFDM front end of the HamDRM receiver: converts audio into frames of
// equalized cells. It finds the symbol timing by correlating the guard
// interval, locates the carrier grid and the frame start from the known pilot
// pattern, estimates the channel from the pilots, and tracks timing and
// frequency drift.
#pragma once

#include "frame_layout.hpp"

#include <fftw3.h>

#include <functional>
#include <memory>
#include <vector>

#define EASYPAL_SAMPLE_RATE 12000
#define EASYPAL_HILBERT_TAPS 193
#define EASYPAL_RING_SIZE 16
#define EASYPAL_SYNC_SYMBOLS 30

namespace Digiham::EasyPal {

    struct Frame {
            int mode, occupancy;
            int symbols, minCarrier, maxCarrier; // the widest occupancy
            std::vector<Complex> cell;           // equalized, [s * (kmax-kmin+1) + (k-kmin)]
            std::vector<float> weight;           // |H|^2 for each cell
    };

    class OfdmFrontEnd {
        public:
            typedef std::function<void(const Frame&)> FrameHandler;

            explicit OfdmFrontEnd(FrameHandler handler);
            ~OfdmFrontEnd();

            // Feed real audio sampled at 12 kHz
            void push(const float* audio, int count);

            // Spectrum occupancy as announced by the FAC (0 = 2.3 kHz, 1 = 2.5 kHz)
            void setOccupancy(int occ);

            bool isLocked() const {
                return state == LIVE;
            }

        private:
            enum State { ACQUIRE,
                         SYNC,
                         LIVE };

            struct SymbolRec {
                    std::vector<Complex> y;         // spectrum bins
                    std::vector<Complex> hp;        // channel at pilot carriers (k - kmin)
                    std::vector<unsigned char> has; // pilot present at that carrier
                    int fs = 0;                     // position within frame
                    bool valid = false;
            };

            FrameHandler handler;
            State state = ACQUIRE;
            int mode = 0, occupancy = 1;
            std::unique_ptr<FrameLayout> layout;

            // Analytic signal
            std::vector<float> hilbert, history;
            size_t historyPosition = 0;
            long inputCount = 0;
            std::vector<Complex> signal;
            long signalBase = 0; // absolute index of signal[0]

            // Symbol grid and tracking
            long position = 0;                          // absolute start of the next symbol's guard
            double frequencyOffset = 0, mixerPhase = 0; // residual frequency (Hz) and mixer phase
            int binOffset = 0;                          // FFT bin of carrier k=0
            int derotation = 0;                         // bin offset whose carrier phase is removed in spectrum()
            int firstSymbol = 0;                        // frame position of the symbol at `position`
            int margin = 0, windowOffset = 0;
            long symbolCount = 0; // symbols processed since lock
            float timingError = 0;
            bool timingKnown = false;
            int skipFrequency = 0;
            float smoothedSnr = 0;
            int lowCount = 0;

            // FFT
            fftwf_plan plans[2] = {0, 0};
            fftwf_complex *fftInput = 0, *fftOutput = 0;

            SymbolRec ring[EASYPAL_RING_SIZE];
            std::vector<std::vector<std::pair<int, Complex>>> pilots; // per frame position
            Frame frame;
            int frameFill = -1;

            void analytic(float x);
            void run();
            bool acquire();
            bool synchronize();
            void startLive();
            void process();
            void spectrum(long start, double psi, double phi, std::vector<Complex>& out);
            void symbolIn();
            void equalize(long d);
            void dropOld();
            void buildPilots();
            void loseLock();
    };

}
