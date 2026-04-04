#include "dsp.hpp"
#include <cmath>

std::vector<std::vector<double>> DSP::computeSpectrogram() {}

void DSP::applyHann() {
  int N = DSP::signal.size();
  for (int i = 0; i < N; i++)
    DSP::signal[i] *= 0.5 * (1 - std::cos(2 * PI * i / (N - 1)));
}

void DSP::fft() {
  int n = DSP::fft_vector.size();
  if (n <= 1)
    return;

  std::vector<Complex> even(n / 2), odd(n / 2);
  for (int i = 0; i < n / 2; i++) {
    even[i] = DSP::fft_vector[i * 2];
    odd[i] = DSP::fft_vector[i * 2 + 1];
  }

  fft(even);
  fft(odd);

  for (int k = 0; k < n / 2; k++) {
    double ang = -2 * PI * k / n;
    Complex w(std::cos(ang), std::sin(ang));
    Complex t = w * odd[k];
    DSP::fft_vector[k] = even[k] + t;
    DSP::fft_vector[k + n / 2] = even[k] - t;
  }
}
