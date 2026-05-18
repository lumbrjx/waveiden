#pragma once
#include "types.hpp"
#include "audio_reader.hpp"
#include "database.hpp"
#include <memory>
#include <string>
#include <vector>

namespace waveiden {

struct FingerprintConfig {
    int frameSize       = 2048;
    int hopSize         = 512;
    double peakPct      = 98.5;
    int maxPeaks        = 10000;
    int maxPairsPerPeak = 10;
    int maxDt           = 100;
};

class FingerprintEngine {
public:
    using Config = FingerprintConfig;

    explicit FingerprintEngine(Config config = Config{});
    FingerprintEngine(Config config, std::unique_ptr<IAudioReader> reader);

    // Core pipeline: AudioBuffer → fingerprints
    std::vector<Fingerprint> process(const AudioBuffer& buf) const;

    // Convenience: read from file, fingerprint, store in db
    int indexFile(const std::string& path, IDatabase& db) const;
    // Same but with a pre-loaded AudioBuffer (useful for gRPC)
    int indexBuffer(const std::string& name, const AudioBuffer& buf, IDatabase& db) const;

    // Read from file, fingerprint, query db
    MatchResult matchFile(const std::string& path, const IDatabase& db) const;
    MatchResult matchBuffer(const AudioBuffer& buf, const IDatabase& db) const;

    const Config& config() const { return config_; }

private:
    Config config_;
    std::unique_ptr<IAudioReader> reader_;
};

}
