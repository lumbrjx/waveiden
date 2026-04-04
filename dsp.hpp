#ifndef DSP

#include "consts.hpp"
#include <sndfile.h>
#include <vector>

class DSP {

public:
  const std::vector<double> &audio;
  int frameSize;
  int hopSize;

  DSP(const std::vector<double> &audio, int frameSize, int hopSize)
      : audio(audio), frameSize(frameSize), hopSize(hopSize) {}

  std::vector<std::vector<double>> computeSpectrogram();

private:
  std::vector<Complex> fft_vector;
  std::vector<double> signal;

  void fft();

  void applyHann();
};

#endif
