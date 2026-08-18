#include <cfloat>
#include "gfsk_demodulator.hpp"

using namespace Digiham::Fsk;

GfskDemodulator::GfskDemodulator(unsigned int samplesPerSymbol):
    samplesPerSymbol(samplesPerSymbol),
    lowestEval((int) roundf((float)samplesPerSymbol / 3)),
    highestEval((int) roundf((float)samplesPerSymbol * 2 / 3)),
    variance_rb_size(VARIANCE_SYMBOLS * samplesPerSymbol),
    variance_rb((float*) malloc(sizeof(float) * variance_rb_size))
{}

GfskDemodulator::~GfskDemodulator() {
    free(variance_rb);
}

bool GfskDemodulator::canProcess() {
    std::lock_guard<std::mutex> lock(processMutex);
    // +1 for variance calculation "jumps"
    return reader->available() > samplesPerSymbol + 1 && writer->writeable() > 0;
}

void GfskDemodulator::process() {
    std::lock_guard<std::mutex> lock(processMutex);
    float* input = reader->getReadPointer();

    float sum = 0.0f;
    float volume_sum = 0.0f;
    for (size_t i = 0; i < samplesPerSymbol; i++) {
        float value = input[i];
        if (i >= lowestEval && i < highestEval) sum += value;
        volume_sum += value;
        variance_rb[variance_rb_pos + i] = value;
    }
    reader->advance(samplesPerSymbol + variance_offset);
    // reset until next variance evaluation
    variance_offset = 0;

    variance_rb_pos += samplesPerSymbol;
    if (variance_rb_pos >= variance_rb_size) {

        double vmin;
        size_t vmin_pos;

        for (size_t i = 0; i < samplesPerSymbol; i++) {
            //fprintf(stderr, "variance calc @ %i: ", i);
            float total = 0;
            int k;
            for (k = 0; k < VARIANCE_SYMBOLS; k++) {
                total += variance_rb[k * samplesPerSymbol + i];
            }
            double mean = total / VARIANCE_SYMBOLS;
            //fprintf(stderr, "total: %i, mean: %.0f ", total, mean);

            double dsum = 0;
            for (k = 0; k < VARIANCE_SYMBOLS; k++) {
                dsum += pow(mean - variance_rb[k * samplesPerSymbol + i], 2);
            }
            double variance = dsum / VARIANCE_SYMBOLS;
            //fprintf(stderr, "variance: %.0f\n", variance);

            if (i == 0 || variance < vmin) {
                vmin = variance;
                vmin_pos = i;
            }
        }

        if (vmin <= 0 || vmin > 5000000) {
            // NOOP
        } else if (vmin_pos > 0 && vmin_pos < samplesPerSymbol / 2) {
            // variance indicates stepping to the left
            variance_offset = +1;
        } else if (vmin_pos >= samplesPerSymbol / 2 && vmin_pos < samplesPerSymbol - 1) {
            // variance indicates stepping to the right
            variance_offset = -1;
        }

        variance_rb_pos %= variance_rb_size;
    }

    float volume_average = volume_sum / samplesPerSymbol;
    volume_rb[volume_rb_pos] = volume_average;

    volume_rb_pos += 1;
    if (volume_rb_pos >= VOLUME_RB_SIZE) volume_rb_pos = 0;

    calibrateAudio();

    float average = sum / (highestEval - lowestEval);

    if (average > center) {
        if (average > umid) {
            *writer->getWritePointer() = 1;
        } else {
            *writer->getWritePointer() = 0;
        }
    } else {
        if (average < lmid) {
            *writer->getWritePointer() = 3;
        } else {
            *writer->getWritePointer() = 2;
        }
    }

    writer->advance(1);
}

void GfskDemodulator::calibrateAudio() {
    // Robust level calibration.
    //
    // The original version derived the slicing thresholds from the raw min/max of the last
    // VOLUME_RB_SIZE per-symbol averages. That is fragile when the discriminator audio is hot or
    // noisy: a few outlier symbols stretch min/max, which pushes umid/lmid outward, so genuine
    // outer symbols (+/-1) get misclassified as inner (+/-1/3). The symptom is a collapsed symbol
    // distribution (too many inner symbols, too few outer), which breaks sync detection.
    //
    // Instead we estimate the outer levels from a high/low PERCENTILE of the recent averages,
    // which ignores a handful of hot outliers, and we center on the MEDIAN rather than the
    // midpoint of the extremes. This makes the slicer amplitude-independent and far more tolerant
    // of scaling/gain differences (e.g. a 2.5x hotter demod), matching the robustness of other
    // decoders without changing the symbol mapping.
    int i;

    // copy the ring buffer so we can sort it without disturbing acquisition order
    float sorted[VOLUME_RB_SIZE];
    for (i = 0; i < VOLUME_RB_SIZE; i++) sorted[i] = volume_rb[i];

    // simple insertion sort (VOLUME_RB_SIZE is small and fixed)
    for (i = 1; i < VOLUME_RB_SIZE; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // robust low/high estimates: trim the extreme 10% on each side.
    // idx_lo/idx_hi approximate the true outer symbol levels without being dragged by outliers.
    int idx_lo = VOLUME_RB_SIZE / 10;               // ~10th percentile
    int idx_hi = VOLUME_RB_SIZE - 1 - idx_lo;       // ~90th percentile
    float robust_min = sorted[idx_lo];
    float robust_max = sorted[idx_hi];
    float median = sorted[VOLUME_RB_SIZE / 2];

    // keep min/max updated (used nowhere else now, but preserved for clarity/inspection)
    min = robust_min;
    max = robust_max;

    // center on the median of the level distribution rather than the midpoint of the extremes;
    // for balanced 4FSK data this coincides with 0, but the median resists DC/asymmetry drift.
    center = median;

    // Place the inner/outer boundary at 2/3 of the way to the (robust) outer level. For ideal
    // DMR 4FSK the inner levels sit at 1/3 and the outer at 1, so the correct split is at 2/3.
    // This is slightly wider than the old 0.625 and, combined with the robust outer estimate,
    // classifies outer vs inner correctly even when the input is hot.
    const float RATIO = 0.667f;
    umid = (robust_max - center) * RATIO + center;
    lmid = (robust_min - center) * RATIO + center;
}