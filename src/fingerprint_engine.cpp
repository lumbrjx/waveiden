#include "waveiden/fingerprint_engine.hpp"
#include "waveiden/dsp.hpp"
#include <cmath>
#include <stdexcept>

namespace waveiden {

FingerprintEngine::FingerprintEngine(Config config)
    : config_(config), reader_(std::make_unique<WavReader>()) {}

FingerprintEngine::FingerprintEngine(Config config, std::unique_ptr<IAudioReader> reader)
    : config_(config), reader_(std::move(reader)) {}

std::vector<Fingerprint> FingerprintEngine::process(const AudioBuffer& buf) const {
    if (buf.sampleRate <= 0)
        throw std::runtime_error("Audio buffer has no valid sample rate");
    if (config_.targetSampleRate <= 0)
        throw std::runtime_error("Fingerprint target sample rate must be positive");

    std::vector<double> samples;
    if (buf.sampleRate == config_.targetSampleRate) {
        samples = buf.samples;
    } else if (!buf.samples.empty()) {
        const double ratio = static_cast<double>(buf.sampleRate) /
                             config_.targetSampleRate;
        const size_t outputSize = static_cast<size_t>(std::llround(
            buf.samples.size() / ratio));
        samples.resize(outputSize);
        for (size_t i = 0; i < outputSize; ++i) {
            const double sourceIndex = i * ratio;
            const size_t left = static_cast<size_t>(sourceIndex);
            const size_t right = std::min(left + 1, buf.samples.size() - 1);
            const double fraction = sourceIndex - left;
            samples[i] = buf.samples[left] * (1.0 - fraction) +
                         buf.samples[right] * fraction;
        }
    }

    auto spec  = dsp::computeSpectrogram(samples, config_.frameSize, config_.hopSize);
    auto peaks = dsp::findPeaks(spec, config_.peakPct, config_.maxPeaks);
    return dsp::createFingerprints(peaks, config_.maxPairsPerPeak, config_.maxDt);
}

int FingerprintEngine::indexFile(const std::string& path, IDatabase& db) const {
    auto buf    = reader_->read(path);
    auto prints = process(buf);
    db.indexSong(path, prints);
    return static_cast<int>(prints.size());
}

int FingerprintEngine::indexBuffer(
    const std::string& name, const AudioBuffer& buf, IDatabase& db) const
{
    auto prints = process(buf);
    db.indexSong(name, prints);
    return static_cast<int>(prints.size());
}

MatchResult FingerprintEngine::matchFile(const std::string& path, const IDatabase& db) const {
    auto buf    = reader_->read(path);
    auto prints = process(buf);
    return db.match(prints);
}

MatchResult FingerprintEngine::matchBuffer(const AudioBuffer& buf, const IDatabase& db) const {
    auto prints = process(buf);
    return db.match(prints);
}

} 
