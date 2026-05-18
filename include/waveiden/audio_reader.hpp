#pragma once
#include "types.hpp"
#include <memory>
#include <string>

namespace waveiden {

class IAudioReader {
public:
    virtual ~IAudioReader() = default;
    virtual AudioBuffer read(const std::string& path) const = 0;
    // Decode raw bytes (e.g. from network/gRPC)
    virtual AudioBuffer readFromMemory(const uint8_t* data, size_t size) const = 0;
};

class WavReader : public IAudioReader {
public:
    AudioBuffer read(const std::string& path) const override;
    AudioBuffer readFromMemory(const uint8_t* data, size_t size) const override;
};

} 
