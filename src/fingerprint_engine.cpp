#include "waveiden/fingerprint_engine.hpp"
#include "waveiden/dsp.hpp"
#include <stdexcept>

namespace waveiden {

FingerprintEngine::FingerprintEngine(Config config)
    : config_(config), reader_(std::make_unique<WavReader>()) {}

FingerprintEngine::FingerprintEngine(Config config, std::unique_ptr<IAudioReader> reader)
    : config_(config), reader_(std::move(reader)) {}

std::vector<Fingerprint> FingerprintEngine::process(const AudioBuffer& buf) const {
    auto spec  = dsp::computeSpectrogram(buf.samples, config_.frameSize, config_.hopSize);
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
