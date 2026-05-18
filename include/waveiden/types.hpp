#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace waveiden {

struct Complex {
    double re, im;
    Complex(double r = 0, double i = 0) : re(r), im(i) {}
    Complex operator+(const Complex& o) const { return {re + o.re, im + o.im}; }
    Complex operator-(const Complex& o) const { return {re - o.re, im - o.im}; }
    Complex operator*(const Complex& o) const {
        return {re * o.re - im * o.im, re * o.im + im * o.re};
    }
};

struct Peak {
    int freqBin;
    int timeBin;
    double magnitude;
};

struct Fingerprint {
    uint64_t hash;
    int timeOffset;
};

struct MatchResult {
    bool found = false;
    std::string songName;
    int confidence = 0;
    double timeOffset = 0.0;
};

struct AudioBuffer {
    std::vector<double> samples;
    int sampleRate = 0;
    int channels = 0;
};

}
