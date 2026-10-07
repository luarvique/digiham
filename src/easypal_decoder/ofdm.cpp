#include "ofdm.hpp"
#include "tables.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Digiham::EasyPal;

static const Complex zero(0, 0);

// Interpolate between two channel values by amplitude and phase. Complex
// linear interpolation would shrink the result whenever the phase differs
// between the points, as it does across a timing offset.
static inline Complex mix(Complex a, Complex b, float t) {
    float ma = std::abs(a), mb = std::abs(b);
    if (ma <= 0 || mb <= 0) return (1 - t) * a + t * b;
    float dphi = std::arg(b * std::conj(a)); // wrapped to (-pi, pi]
    float mag = (1 - t) * ma + t * mb;
    float ph = std::arg(a) + t * dphi;
    return Complex(mag * cosf(ph), mag * sinf(ph));
}

OfdmFrontEnd::OfdmFrontEnd(FrameHandler handler):
    handler(handler) {
    // Hilbert transformer: windowed ideal response, odd taps only
    hilbert.assign(EASYPAL_HILBERT_TAPS, 0.0f);
    int c = EASYPAL_HILBERT_TAPS / 2;
    for (int i = 0; i < EASYPAL_HILBERT_TAPS; i++) {
        int n = i - c;
        if (n & 1) {
            double w = 0.54 + 0.46 * cos(M_PI * n / (c + 1));
            hilbert[i] = (float)(2.0 / (M_PI * n) * w);
        }
    }
    history.assign(EASYPAL_HILBERT_TAPS, 0.0f);

    fftInput = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * 512);
    fftOutput = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * 512);
    for (int m = 0; m < 2; m++)
        plans[m] = fftwf_plan_dft_1d(modes[m].tu, fftInput, fftOutput, FFTW_FORWARD, FFTW_ESTIMATE);

    layout.reset(new FrameLayout(0, occupancy));
}

OfdmFrontEnd::~OfdmFrontEnd() {
    for (int m = 0; m < 2; m++) fftwf_destroy_plan(plans[m]);
    fftwf_free(fftInput);
    fftwf_free(fftOutput);
}

void OfdmFrontEnd::setOccupancy(int occ) {
    if (occ != occupancy && (occ == 0 || occ == 1)) {
        occupancy = occ;
        layout.reset(new FrameLayout(mode, occupancy));
        buildPilots();

        // Symbols already in the history were sized for the old layout
        for (int i = 0; i < EASYPAL_RING_SIZE; i++) ring[i].valid = false;
        frameFill = -1;
    }
}

void OfdmFrontEnd::push(const float* audio, int count) {
    for (int i = 0; i < count; i++) analytic(audio[i]);
    run();
}

// z[n - delay] = x[n-delay] + j * (h * x)[n]
void OfdmFrontEnd::analytic(float x) {
    historyPosition = historyPosition ? historyPosition - 1 : EASYPAL_HILBERT_TAPS - 1;
    history[historyPosition] = x;
    ++inputCount;

    float acc = 0.0f;
    for (int i = 1; i < EASYPAL_HILBERT_TAPS; i += 2) // even-offset taps are zero
        acc += hilbert[i] * history[(historyPosition + i) % EASYPAL_HILBERT_TAPS];
    float re = history[(historyPosition + EASYPAL_HILBERT_TAPS / 2) % EASYPAL_HILBERT_TAPS];
    signal.push_back(Complex(re, acc));
}

void OfdmFrontEnd::buildPilots() {
    pilots.assign(layout->getSymbols(), {});
    for (int s = 0; s < layout->getSymbols(); s++)
        for (int k = layout->getMinCarrier(); k <= layout->getMaxCarrier(); k++) {
            Complex v;
            if (layout->getPilot(s, k, v)) pilots[s].push_back(std::make_pair(k, v));
        }
}

void OfdmFrontEnd::run() {
    size_t need, drop;
    const ModeInfo &mi;

    for (;;) {
        switch (state) {

        case ACQUIRE:
            if (signal.size() < 7600) return;
            if (acquire()) {
                state = SYNC;
            } else {
                // Nothing here: slide the window forward
                drop = 3200;
                signal.erase(signal.begin(), signal.begin() + drop);
                signalBase += drop;
            }
            break;

        case SYNC:
            mi = modes[mode];
            need = position - signalBase + (EASYPAL_SYNC_SYMBOLS + 2) * (mi.tu + mi.tg) + mi.tu;
            if (signal.size() < need) return;
            if (synchronize()) {
                startLive();
                state = LIVE;
            } else {
                drop = std::min<size_t>(3200, signal.size());
                signal.erase(signal.begin(), signal.begin() + drop);
                signalBase += drop;
                state = ACQUIRE;
            }
            break;

        case LIVE:
            mi = modes[mode];
            need = position - signalBase + windowOffset + mi.tu + 8;
            if (signal.size() < need) return;
            process();
            if (state == LIVE) dropOld();
            break;

        default:
            // invalid state
            return;
        }
    }
}

void OfdmFrontEnd::dropOld() {
    long keep = position - 4 * 320;
    if (keep > signalBase + 8000) {
        size_t drop = keep - signalBase;
        signal.erase(signal.begin(), signal.begin() + drop);
        signalBase += drop;
    }
}

// Guard-interval correlation on the analytic signal: the cyclic prefix repeats
// the end of the symbol Tu samples later. The magnitude of the correlation is
// insensitive to carrier frequency; its phase gives the offset within one bin.
bool OfdmFrontEnd::acquire() {
    int bestMode = -1;
    float bestScore = 0;
    long bestOff = 0;
    double bestPhi = 0;

    for (int m = 0; m < 2; m++) {
        const ModeInfo& mi = modes[m];
        const int tu = mi.tu, tg = mi.tg, ts = tu + tg;
        long n = (long)signal.size() - tu;
        if (n < 6 * ts) continue;

        std::vector<Complex> cs(n + 1, zero);
        std::vector<double> es(n + 1, 0.0);
        for (long j = 0; j < n; j++) {
            cs[j + 1] = cs[j] + signal[j] * std::conj(signal[j + tu]);
            es[j + 1] = es[j] + 0.5 * (std::norm(signal[j]) + std::norm(signal[j + tu]));
        }
        int syms = (n - tg) / ts;
        std::vector<float> rho(ts);
        std::vector<Complex> sum(ts);
        for (int d = 0; d < ts; d++) {
            Complex num = zero;
            double den = 0;
            for (int k = 0; k < syms; k++) {
                long a = d + (long)k * ts;
                if (a + tg > n) break;
                num += cs[a + tg] - cs[a];
                den += es[a + tg] - es[a];
            }
            sum[d] = num;
            rho[d] = den > 1e-20 ? std::abs(num) / den : 0.0f;
        }
        std::vector<float> tmp(rho);
        std::nth_element(tmp.begin(), tmp.begin() + ts / 2, tmp.end());
        int peak = std::max_element(rho.begin(), rho.end()) - rho.begin();
        float score = rho[peak] - tmp[ts / 2];
        if (score > 0.18f && score > bestScore) {
            bestScore = score;
            bestMode = m;
            bestOff = peak;
            bestPhi = -std::arg(sum[peak]) * EASYPAL_SAMPLE_RATE / (2.0 * M_PI * tu);
        }
    }
    if (bestMode < 0) return false;

    mode = bestMode;
    derotation = 0;
    layout.reset(new FrameLayout(mode, occupancy));
    buildPilots();
    const ModeInfo& mi = modes[mode];
    margin = mi.tg / 2;
    windowOffset = mi.tg - margin;
    position = signalBase + bestOff;
    frequencyOffset = bestPhi;
    mixerPhase = 0;
    return true;
}

// FFT of one symbol window starting at absolute sample `start`
void OfdmFrontEnd::spectrum(long start, double psi, double phi, std::vector<Complex>& out) {
    const int tu = modes[mode].tu;
    const double w = 2.0 * M_PI * phi / EASYPAL_SAMPLE_RATE;
    const long base = start - signalBase;
    for (int j = 0; j < tu; j++) {
        double a = -(psi + w * j);
        Complex r(cosf(a), sinf(a));
        Complex v = signal[base + j] * r;
        fftInput[j][0] = v.real();
        fftInput[j][1] = v.imag();
    }
    fftwf_execute(plans[mode]);
    out.resize(tu);

    // The audio carrier at bin `derotation` advances the phase of every bin by
    // 2*pi*derotation*n0/Tu with the window start n0. Remove it so that a cell's
    // phase stays put from one symbol to the next.
    Complex rot(1, 0);
    if (derotation) {
        double a = -2.0 * M_PI * derotation * (double)(((start % tu) + tu) % tu) / tu;
        rot = Complex(cosf(a), sinf(a));
    }
    for (int j = 0; j < tu; j++) out[j] = Complex(fftOutput[j][0], fftOutput[j][1]) * rot;
}

// Find the carrier offset and frame start from the pilot pattern
bool OfdmFrontEnd::synchronize() {
    const ModeInfo& mi = modes[mode];
    const int tu = mi.tu, ts = tu + mi.tg, S = layout->getSymbols();
    const int kmin = layout->getMinCarrier();
    const int kmaxNarrow = mi.kmax[0];

    std::vector<std::vector<Complex>> Y(EASYPAL_SYNC_SYMBOLS);
    double psi = mixerPhase;
    for (int s = 0; s < EASYPAL_SYNC_SYMBOLS; s++) {
        spectrum(position + (long)s * ts + windowOffset, psi, frequencyOffset, Y[s]);
        psi += 2.0 * M_PI * frequencyOffset * ts / EASYPAL_SAMPLE_RATE;
    }

    int nLo = std::max(1 - kmin, 0), nHi = tu / 2 - 1 - layout->getMaxCarrier();
    double best = 0, second = 0;
    int bestN = 0, bestS0 = 0;

    // A hypothesis only counts if the bins it assumes carry the signal energy.
    // (Empty bins hold tiny but structured leakage that would otherwise match.)
    std::vector<double> binPower(tu, 0.0);
    for (int s = 0; s < EASYPAL_SYNC_SYMBOLS; s++)
        for (int b = 0; b < tu / 2; b++) binPower[b] += std::norm(Y[s][b]);
    std::vector<double> bandPower(nHi - nLo + 1, 0.0);
    double bandMax = 0;
    for (int N = nLo; N <= nHi; N++) {
        double e = 0;
        for (int k = kmin; k <= kmaxNarrow; k++) e += binPower[k + N];
        bandPower[N - nLo] = e;
        bandMax = std::max(bandMax, e);
    }

    for (int s0 = 0; s0 < S; s0++) {
        std::vector<double> metric(nHi - nLo + 1, 0.0);
        for (int N = nLo; N <= nHi; N++) {
            if (bandPower[N - nLo] < 0.5 * bandMax) continue;
            // Temporal coherence: after removing the known pilot phases, the
            // channel on a carrier must agree between pilots y symbols apart.
            // Only the true carrier offset and frame start make that hold.
            Complex acc = zero;
            double norm = 0;
            for (int s = 0; s + mi.y < EASYPAL_SYNC_SYMBOLS; s++) {
                int fs = (s0 + s) % S, fs2 = (s0 + s + mi.y) % S;
                for (auto& pk : pilots[fs]) {
                    int k = pk.first;
                    if (k > kmaxNarrow || !layout->isGridPilot(fs, k)) continue;
                    Complex p2;
                    if (!layout->getPilot(fs2, k, p2)) continue;
                    Complex r1 = Y[s][k + N] * std::conj(pk.second);
                    Complex r2 = Y[s + mi.y][k + N] * std::conj(p2);
                    acc += r2 * std::conj(r1);
                    norm += std::abs(r1) * std::abs(r2);
                }
            }
            double v = norm > 0 ? std::abs(acc) / norm : 0.0;
            if (v > best) {
                second = std::max(second, best);
                best = v;
                bestN = N;
                bestS0 = s0;
            } else if (v > second)
                second = v;
        }
    }
    // The correct hypothesis stands well clear of all others
    if (best < 0.40 || best - second < 0.15) return false;

    binOffset = bestN;
    firstSymbol = bestS0;

    // Refine the residual frequency with the continuous (frequency reference) pilots
    Complex acc = zero;
    for (int s = 1; s < EASYPAL_SYNC_SYMBOLS; s++)
        for (int i = 0; i < 3; i++) {
            int k = mi.freqK[i];
            Complex a = Y[s][k + binOffset], b = Y[s - 1][k + binOffset];
            acc += a * std::conj(b);
        }
    if (std::abs(acc) > 0) {
        // The carrier itself advances 2*pi*N*Ts/Tu per symbol; what is left is frequency error
        double carrier = 2.0 * M_PI * binOffset * (double)ts / tu;
        double e = std::arg(acc * Complex(cos(-carrier), sin(-carrier)));
        frequencyOffset += e * EASYPAL_SAMPLE_RATE / (2.0 * M_PI * ts);
    }
    derotation = binOffset;
    return true;
}

void OfdmFrontEnd::startLive() {
    // Estimate the occupancy from the power just above the narrow band
    const ModeInfo& mi = modes[mode];
    const int ts = mi.tu + mi.tg;
    std::vector<Complex> Y;
    double inEdge = 0, outEdge = 0;
    double psi = mixerPhase;
    for (int s = 0; s < EASYPAL_SYNC_SYMBOLS; s++) {
        spectrum(position + (long)s * ts + windowOffset, psi, frequencyOffset, Y);
        psi += 2.0 * M_PI * frequencyOffset * ts / EASYPAL_SAMPLE_RATE;
        for (int k = mi.kmax[0] + 1; k <= mi.kmax[1]; k++) inEdge += std::norm(Y[k + binOffset]);
        for (int k = mi.kmax[1] + 3; k <= mi.kmax[1] + 3 + (mi.kmax[1] - mi.kmax[0] - 1); k++)
            if (k + binOffset < mi.tu / 2) outEdge += std::norm(Y[k + binOffset]);
    }
    setOccupancy(inEdge > 3.0 * outEdge ? 1 : 0);
    // Pilot amplitudes depend on the occupancy, so it must be set before use
    buildPilots();

    symbolCount = 0;
    frameFill = -1;
    timingError = 0;
    timingKnown = false;
    lowCount = 0;
    skipFrequency = 0;
    for (int i = 0; i < EASYPAL_RING_SIZE; i++) ring[i].valid = false;
    frame = Frame();
}

void OfdmFrontEnd::process() {
    const ModeInfo& mi = modes[mode];
    const int ts = mi.tu + mi.tg;
    const int width = layout->getMaxCarrier() - layout->getMinCarrier() + 1;

    SymbolRec& r = ring[symbolCount % EASYPAL_RING_SIZE];
    spectrum(position + windowOffset, mixerPhase, frequencyOffset, r.y);
    r.fs = (int)((firstSymbol + symbolCount) % layout->getSymbols());
    r.valid = true;
    r.hp.assign(width, zero);
    r.has.assign(width, 0);

    // Channel at the pilots, and a power-based signal quality figure
    double inband = 0, outband = 0;
    int nin = 0, nout = 0;
    for (auto& pk : pilots[r.fs]) {
        r.hp[pk.first - layout->getMinCarrier()] = r.y[pk.first + binOffset] / pk.second;
        r.has[pk.first - layout->getMinCarrier()] = 1;
    }
    for (int k = layout->getMinCarrier(); k <= layout->getMaxCarrier(); k++) {
        inband += std::norm(r.y[k + binOffset]);
        ++nin;
    }
    for (int b = layout->getMaxCarrier() + binOffset + 8; b < mi.tu / 2; b++) {
        outband += std::norm(r.y[b]);
        ++nout;
    }
    float snr = (nout > 0 && outband > 0) ? (float)((inband / nin) / (outband / nout)) : 100.0f;
    smoothedSnr = 0.9f * smoothedSnr + 0.1f * snr;

    // Frequency loop from the continuous pilots
    if (symbolCount > 0 && skipFrequency == 0) {
        const SymbolRec& p = ring[(symbolCount - 1) % EASYPAL_RING_SIZE];
        Complex acc = zero;
        for (int i = 0; i < 3; i++) {
            int j = mi.freqK[i] - layout->getMinCarrier();
            if (r.has[j] && p.has[j]) acc += r.hp[j] * std::conj(p.hp[j]);
        }
        if (std::abs(acc) > 0) {
            double err = std::arg(acc) * EASYPAL_SAMPLE_RATE / (2.0 * M_PI * ts);
            frequencyOffset += 0.06 * err;
        }
    }
    if (skipFrequency > 0) --skipFrequency;

    mixerPhase += 2.0 * M_PI * frequencyOffset * ts / EASYPAL_SAMPLE_RATE;
    position += ts;
    ++symbolCount;

    // Equalize the symbol that now has pilots on both sides
    long d = symbolCount - 1 - (layout->getMode() == 0 ? 4 : 2);
    if (d >= 0) equalize(d);

    // Lock supervision: the signal must stand clear of the noise
    if (smoothedSnr < 2.0f) {
        if (++lowCount > 40) loseLock();
    } else
        lowCount = 0;
}

void OfdmFrontEnd::loseLock() {
    state = ACQUIRE;
    signal.erase(signal.begin(), signal.begin() + std::min<size_t>(signal.size(), position - signalBase));
    signalBase = position;
    frameFill = -1;
}

// Channel estimate for every carrier of symbol `d`, and the equalized cells
void OfdmFrontEnd::equalize(long d) {
    const ModeInfo& mi = modes[mode];
    const int S = layout->getSymbols();
    const int kmin = layout->getMinCarrier(), kmax = layout->getMaxCarrier(), width = kmax - kmin + 1;
    const int Y = mi.y;
    const SymbolRec& cur = ring[d % EASYPAL_RING_SIZE];
    if (!cur.valid) return;
    const long newest = symbolCount - 1;

    // 1. Time interpolation along each carrier that has pilots
    std::vector<Complex> hk(width, zero);
    std::vector<unsigned char> known(width, 0);
    for (int k = kmin; k <= kmax; k++) {
        int j = k - kmin;
        long tp = -1, tn = -1;
        for (long t = d; t >= 0 && t >= d - Y && t > newest - EASYPAL_RING_SIZE + 1; --t)
            if (ring[t % EASYPAL_RING_SIZE].valid && ring[t % EASYPAL_RING_SIZE].has[j]) {
                tp = t;
                break;
            }
        for (long t = d + 1; t <= newest && t <= d + Y; t++)
            if (ring[t % EASYPAL_RING_SIZE].valid && ring[t % EASYPAL_RING_SIZE].has[j]) {
                tn = t;
                break;
            }
        if (tp >= 0 && tn >= 0 && tp != d) {
            float a = (float)(d - tp) / (float)(tn - tp);
            hk[j] = mix(ring[tp % EASYPAL_RING_SIZE].hp[j], ring[tn % EASYPAL_RING_SIZE].hp[j], a);
            known[j] = 1;
        } else if (tp == d) {
            hk[j] = cur.hp[j];
            known[j] = 1;
        } else if (tp >= 0) {
            hk[j] = ring[tp % EASYPAL_RING_SIZE].hp[j];
            known[j] = 1;
        } else if (tn >= 0) {
            hk[j] = ring[tn % EASYPAL_RING_SIZE].hp[j];
            known[j] = 1;
        }
    }

    // 2. Frequency interpolation between the carriers that are known
    std::vector<Complex> H(width, zero);
    int prev = -1;
    for (int j = 0; j < width; j++) {
        if (!known[j]) continue;
        if (prev < 0)
            for (int i = 0; i < j; i++) H[i] = hk[j];
        else
            for (int i = prev + 1; i < j; i++) {
                float a = (float)(i - prev) / (float)(j - prev);
                H[i] = mix(hk[prev], hk[j], a);
            }
        H[j] = hk[j];
        prev = j;
    }
    if (prev < 0) return;
    for (int i = prev + 1; i < width; i++) H[i] = hk[prev];

    // 3. Timing loop: phase slope between adjacent grid carriers
    {
        Complex acc = zero;
        int last = -1;
        double norm = 0;
        for (int j = 0; j < width; j++) {
            if (!known[j] || ((j + kmin - mi.k0) % mi.x) != 0) continue;
            if (last >= 0 && j - last == mi.x) {
                acc += H[last] * std::conj(H[j]);
                norm += std::abs(H[last]) * std::abs(H[j]);
            }
            last = j;
        }
        // Only trust the slope when there is a signal to measure (pilot
        // history complete, in-band power well above the noise) and the
        // phase step is consistent from carrier to carrier.
        if (norm > 0 && std::abs(acc) > 0.6 * norm && symbolCount >= 4 * mi.y && smoothedSnr > 4.0f) {
            float tau = -std::arg(acc) * mi.tu / (2.0f * (float)M_PI * mi.x);
            if (!timingKnown) {
                timingError = tau + margin;
                timingKnown = true;
            } else
                timingError = 0.9f * timingError + 0.1f * (tau + margin);
            if (std::fabs(timingError) > margin / 2.0f) {
                int delta = (int)lroundf(timingError);
                if (delta != 0) {
                    // Move the window and re-express the stored channel estimates
                    position -= delta;
                    // Symbols already extracted (and waiting for the equalizer)
                    // still carry the old timing: re-express both their spectra
                    // and their channel estimates in the new convention, or the
                    // error would be measured again and again.
                    for (int t = 0; t < EASYPAL_RING_SIZE; t++) {
                        if (!ring[t].valid) continue;
                        for (int j = 0; j < width; j++) {
                            double a = -2.0 * M_PI * (j + kmin) * (double)delta / mi.tu;
                            ring[t].hp[j] *= Complex(cosf(a), sinf(a));
                        }
                        for (int b = 0; b < mi.tu; b++) {
                            double a = -2.0 * M_PI * (b - binOffset) * (double)delta / mi.tu;
                            ring[t].y[b] *= Complex(cosf(a), sinf(a));
                        }
                    }
                    timingError = 0;
                    timingKnown = false;
                    skipFrequency = 2;
                    return;
                }
            }
        }
    }

    // 4. Equalize and hand over
    if (cur.fs == 0) {
        frameFill = 0;
        frame.cell.assign(S * width, zero);
        frame.weight.assign(S * width, 0.0f);
    }
    if (frameFill < 0 || cur.fs != frameFill) {
        frameFill = -1;
        return;
    }

    for (int j = 0; j < width; j++) {
        float p = std::norm(H[j]);
        frame.cell[cur.fs * width + j] = p > 1e-12f ? cur.y[j + kmin + binOffset] / H[j] : zero;
        frame.weight[cur.fs * width + j] = p;
    }
    ++frameFill;

    if (frameFill == S) {
        frame.mode = mode;
        frame.occupancy = occupancy;
        frame.symbols = S;
        frame.minCarrier = kmin;
        frame.maxCarrier = kmax;
        frameFill = -1;
        if (handler) handler(frame);
    }
}
