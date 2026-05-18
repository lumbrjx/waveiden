#pragma once
#include "types.hpp"
#include <vector>

namespace waveiden {
namespace dsp {

void fft(std::vector<Complex>& a);

void applyHann(std::vector<double>& frame);

// Returns a 2D magnitude spectrogram [time][freq_bin]
std::vector<std::vector<double>> computeSpectrogram(
    const std::vector<double>& audio,
    int frameSize,
    int hopSize);

std::vector<Peak> findPeaks(
    const std::vector<std::vector<double>>& spec,
    double percentile = 98.5,
    int maxPeaks = 10000);

uint64_t makeHash(int f1, int f2, int dt);

std::vector<Fingerprint> createFingerprints(
    const std::vector<Peak>& peaks,
    int maxPairsPerPeak = 10,
    int maxDt = 100);

}
} 
