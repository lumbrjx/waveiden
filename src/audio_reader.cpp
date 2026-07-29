#include "waveiden/audio_reader.hpp"
#include <sndfile.h>
#include <stdexcept>
#include <cstring>

namespace waveiden {

// libsndfile virtual-IO callbacks for in-memory decoding
namespace {

struct MemoryFile {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

sf_count_t mem_get_filelen(void* user) {
    return static_cast<sf_count_t>(static_cast<MemoryFile*>(user)->size);
}
sf_count_t mem_seek(sf_count_t offset, int whence, void* user) {
    auto* mf = static_cast<MemoryFile*>(user);
    switch (whence) {
        case SEEK_SET: mf->pos = static_cast<size_t>(offset); break;
        case SEEK_CUR: mf->pos += static_cast<size_t>(offset); break;
        case SEEK_END: mf->pos = mf->size + static_cast<size_t>(offset); break;
    }
    return static_cast<sf_count_t>(mf->pos);
}
sf_count_t mem_read(void* ptr, sf_count_t count, void* user) {
    auto* mf = static_cast<MemoryFile*>(user);
    size_t avail = mf->size - mf->pos;
    size_t n = std::min(static_cast<size_t>(count), avail);
    std::memcpy(ptr, mf->data + mf->pos, n);
    mf->pos += n;
    return static_cast<sf_count_t>(n);
}
sf_count_t mem_write(const void*, sf_count_t, void*) { return 0; }
sf_count_t mem_tell(void* user) {
    return static_cast<sf_count_t>(static_cast<MemoryFile*>(user)->pos);
}

AudioBuffer sndfileToBuffer(SNDFILE* file, const SF_INFO& info) {
    std::vector<double> raw(static_cast<size_t>(info.frames) * info.channels);
    sf_readf_double(file, raw.data(), info.frames);
    sf_close(const_cast<SNDFILE*>(file));

    // Downmix to mono. Averaging avoids losing music panned away from the
    // first channel in a stereo master.
    std::vector<double> mono(static_cast<size_t>(info.frames));
    for (sf_count_t i = 0; i < info.frames; i++) {
        double sum = 0.0;
        for (int channel = 0; channel < info.channels; ++channel)
            sum += raw[static_cast<size_t>(i) * info.channels + channel];
        mono[static_cast<size_t>(i)] = sum / info.channels;
    }

    return {std::move(mono), info.samplerate, info.channels};
}

} // namespace

AudioBuffer WavReader::read(const std::string& path) const {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file)
        throw std::runtime_error("Cannot open audio file: " + path);
    return sndfileToBuffer(file, info);
}

AudioBuffer WavReader::readFromMemory(const uint8_t* data, size_t size) const {
    MemoryFile mf{data, size, 0};
    SF_VIRTUAL_IO vio{mem_get_filelen, mem_seek, mem_read, mem_write, mem_tell};
    SF_INFO info{};
    SNDFILE* file = sf_open_virtual(&vio, SFM_READ, &info, &mf);
    if (!file)
        throw std::runtime_error("Cannot decode audio from memory");
    return sndfileToBuffer(file, info);
}

}
