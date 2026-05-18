#include "waveiden/dsp.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace waveiden {
namespace dsp {

static constexpr double PI = 3.14159265358979323846;

void fft(std::vector<Complex> &a) {
  int n = static_cast<int>(a.size());
  if (n <= 1)
    return;

  // Bit-reversal permutation
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }

  // Iterative Cooley-Tukey butterfly (bottom-up, in-place)
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2.0 * PI / len;
    Complex wlen(std::cos(ang), std::sin(ang));
    for (int i = 0; i < n; i += len) {
      Complex w(1.0, 0.0);
      for (int j = 0; j < len / 2; j++) {
        Complex u = a[i + j];
        Complex v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w = w * wlen;
      }
    }
  }
}

void applyHann(std::vector<double> &frame) {
  int N = static_cast<int>(frame.size());
  for (int i = 0; i < N; i++)
    frame[i] *= 0.5 * (1.0 - std::cos(2.0 * PI * i / (N - 1)));
}

std::vector<std::vector<double>>
computeSpectrogram(const std::vector<double> &audio, int frameSize,
                   int hopSize) {
  std::vector<std::vector<double>> spec;

  for (int pos = 0; pos + frameSize <= static_cast<int>(audio.size());
       pos += hopSize) {
    std::vector<double> frame(audio.begin() + pos,
                              audio.begin() + pos + frameSize);
    applyHann(frame);

    std::vector<Complex> c(frame.begin(), frame.end());
    fft(c);

    std::vector<double> mag(frameSize / 2);
    for (int i = 0; i < static_cast<int>(mag.size()); i++)
      mag[i] = std::sqrt(c[i].re * c[i].re + c[i].im * c[i].im);

    spec.push_back(std::move(mag));
  }
  return spec;
}

std::vector<Peak> findPeaks(const std::vector<std::vector<double>> &spec,
                            double percentile, int maxPeaks) {
  std::vector<double> allMags;
  allMags.reserve(spec.size() * (spec.empty() ? 0 : spec[0].size()));
  for (const auto &frame : spec)
    for (double v : frame)
      if (v > 0)
        allMags.push_back(v);

  if (allMags.empty())
    return {};

  std::sort(allMags.begin(), allMags.end());
  double threshold =
      allMags[static_cast<size_t>(allMags.size() * percentile / 100.0)];

  std::vector<Peak> peaks;
  for (int t = 1; t < static_cast<int>(spec.size()) - 1; t++) {
    for (int f = 1; f < static_cast<int>(spec[t].size()) - 1; f++) {
      double v = spec[t][f];
      if (v > spec[t][f - 1] && v > spec[t][f + 1] && v > spec[t - 1][f] &&
          v > spec[t + 1][f] && v > threshold) {
        peaks.push_back({f, t, v});
      }
    }
  }

  if (static_cast<int>(peaks.size()) > maxPeaks) {
    std::partial_sort(
        peaks.begin(), peaks.begin() + maxPeaks, peaks.end(),
        [](const Peak &a, const Peak &b) { return a.magnitude > b.magnitude; });
    peaks.resize(maxPeaks);
    std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
      return a.timeBin < b.timeBin;
    });
  }

  return peaks;
}

uint64_t makeHash(int f1, int f2, int dt) {
  uint64_t h = 0;
  h |= static_cast<uint64_t>(f1 & 0xFFFF);
  h |= static_cast<uint64_t>(f2 & 0xFFFF) << 16;
  h |= static_cast<uint64_t>(dt & 0xFFFF) << 32;
  return h;
}

std::vector<Fingerprint> createFingerprints(const std::vector<Peak> &peaks,
                                            int maxPairsPerPeak, int maxDt) {
  std::vector<Fingerprint> prints;

  for (int i = 0; i < static_cast<int>(peaks.size()); i++) {
    int paired = 0;
    for (int j = i + 1;
         j < static_cast<int>(peaks.size()) && paired < maxPairsPerPeak; j++) {
      int dt = peaks[j].timeBin - peaks[i].timeBin;
      if (dt >= 1 && dt <= maxDt) {
        prints.push_back({makeHash(peaks[i].freqBin, peaks[j].freqBin, dt),
                          peaks[i].timeBin});
        paired++;
      } else if (dt > maxDt) {
        break;
      }
    }
  }
  return prints;
}

} 
} 
