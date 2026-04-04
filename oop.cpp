#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sndfile.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

constexpr double PI = 3.14159265358979323846;

// ====================== Complex Number Class ======================
class Complex {
private:
  double re_, im_;

public:
  Complex(double r = 0, double i = 0) : re_(r), im_(i) {}

  double real() const { return re_; }
  double imag() const { return im_; }

  Complex operator+(const Complex &other) const {
    return Complex(re_ + other.re_, im_ + other.im_);
  }

  Complex operator-(const Complex &other) const {
    return Complex(re_ - other.re_, im_ - other.im_);
  }

  Complex operator*(const Complex &other) const {
    return Complex(re_ * other.re_ - im_ * other.im_,
                   re_ * other.im_ + im_ * other.re_);
  }

  double magnitude() const { return std::sqrt(re_ * re_ + im_ * im_); }
};

// ====================== FFT Processor ======================
class FFTProcessor {
public:
  static void compute(std::vector<Complex> &data) {
    int n = data.size();
    int logN = 0;
    while ((1 << logN) < n)
      logN++;

    // Bit reversal permutation
    for (int i = 0; i < n; i++) {
      int rev = 0;
      for (int j = 0; j < logN; j++)
        if (i & (1 << j))
          rev |= 1 << (logN - 1 - j);
      if (i < rev)
        std::swap(data[i], data[rev]);
    }

    // Iterative FFT
    for (int s = 1; s <= logN; s++) {
      int m = 1 << s;
      int m2 = m / 2;
      Complex wm(std::cos(-2 * PI / m), std::sin(-2 * PI / m));

      // Parallelize this loop across threads
      auto worker = [&](int start, int end) {
        for (int k = start; k < end; k += m) {
          Complex w(1, 0);
          for (int j = 0; j < m2; j++) {
            Complex t = w * data[k + j + m2];
            Complex u = data[k + j];
            data[k + j] = u + t;
            data[k + j + m2] = u - t;
            w = w * wm;
          }
        }
      };

      int numThreads = std::thread::hardware_concurrency();
      std::vector<std::thread> threads;
      int chunk = n / numThreads;
      for (int t = 0; t < numThreads; t++) {
        int start = t * chunk;
        int end = (t == numThreads - 1) ? n : start + chunk;
        threads.emplace_back(worker, start, end);
      }
      for (auto &th : threads)
        th.join();
    }
  }

  static void applyHann(std::vector<double> &frame) {
    int N = frame.size();
    for (int i = 0; i < N; i++) {
      frame[i] *= 0.5 * (1 - std::cos(2 * PI * i / (N - 1)));
    }
  }
};

// ====================== Audio Reader ======================
class AudioReader {
private:
  std::string filePath_;
  int sampleRate_;
  std::vector<double> audioData_;

public:
  explicit AudioReader(const std::string &path) : filePath_(path) {}

  void load() {
    SF_INFO info{};
    SNDFILE *file = sf_open(filePath_.c_str(), SFM_READ, &info);
    if (!file) {
      throw std::runtime_error("Cannot open WAV file: " + filePath_);
    }

    sampleRate_ = info.samplerate;

    std::vector<double> buffer(info.frames * info.channels);
    sf_readf_double(file, buffer.data(), info.frames);
    sf_close(file);

    // Convert to mono
    audioData_.resize(info.frames);
    for (int i = 0; i < info.frames; i++) {
      audioData_[i] = buffer[i * info.channels];
    }
  }

  const std::vector<double> &getAudioData() const { return audioData_; }
  int getSampleRate() const { return sampleRate_; }
  size_t getNumSamples() const { return audioData_.size(); }
};

// ====================== Spectrogram Generator ======================
class SpectrogramGenerator {
private:
  int frameSize_;
  int hopSize_;

public:
  SpectrogramGenerator(int frameSize = 2048, int hopSize = 512)
      : frameSize_(frameSize), hopSize_(hopSize) {}

  std::vector<std::vector<double>>
  compute(const std::vector<double> &audio) const {
    std::vector<std::vector<double>> spectrogram;

    for (size_t pos = 0; pos + frameSize_ <= audio.size(); pos += hopSize_) {
      // Extract frame
      std::vector<double> frame(audio.begin() + pos,
                                audio.begin() + pos + frameSize_);

      // Apply window
      FFTProcessor::applyHann(frame);

      // Convert to complex and perform FFT
      std::vector<Complex> complexFrame(frame.begin(), frame.end());
      FFTProcessor::compute(complexFrame);

      // Compute magnitude spectrum
      std::vector<double> magnitude(frameSize_ / 2);
      for (size_t i = 0; i < magnitude.size(); i++) {
        magnitude[i] = complexFrame[i].magnitude();
      }

      spectrogram.push_back(magnitude);
    }

    return spectrogram;
  }

  int getFrameSize() const { return frameSize_; }
  int getHopSize() const { return hopSize_; }
};

// ====================== Peak Structure ======================
struct Peak {
  int freqBin;
  int timeBin;
  double magnitude;

  Peak(int f, int t, double m) : freqBin(f), timeBin(t), magnitude(m) {}
};

// ====================== Peak Detector ======================
class PeakDetector {
private:
  double percentile_;

  double calculateThreshold(const std::vector<std::vector<double>> &spectrogram,
                            double percentile) const {
    std::vector<double> allMagnitudes;

    for (const auto &frame : spectrogram) {
      for (double value : frame) {
        if (value > 0) {
          allMagnitudes.push_back(value);
        }
      }
    }

    if (allMagnitudes.empty())
      return 0.0;

    std::sort(allMagnitudes.begin(), allMagnitudes.end());
    size_t index =
        static_cast<size_t>(allMagnitudes.size() * percentile / 100.0);
    return allMagnitudes[index];
  }

  bool isLocalMaximum(const std::vector<std::vector<double>> &spec, int t,
                      int f, double threshold) const {
    double value = spec[t][f];

    return value > spec[t][f - 1] && value > spec[t][f + 1] &&
           value > spec[t - 1][f] && value > spec[t + 1][f] &&
           value > threshold;
  }

public:
  explicit PeakDetector(double percentile = 98.5) : percentile_(percentile) {}

  std::vector<Peak>
  findPeaks(const std::vector<std::vector<double>> &spectrogram) const {
    double threshold = calculateThreshold(spectrogram, percentile_);

    std::cout << "Adaptive threshold (" << percentile_
              << "th percentile): " << threshold << "\n";

    std::vector<Peak> peaks;

    // Find local maxima
    for (size_t t = 1; t < spectrogram.size() - 1; t++) {
      for (size_t f = 1; f < spectrogram[t].size() - 1; f++) {
        if (isLocalMaximum(spectrogram, t, f, threshold)) {
          peaks.emplace_back(f, t, spectrogram[t][f]);
        }
      }
    }

    // Limit to top peaks if too many
    if (peaks.size() > 10000) {
      std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
        return a.magnitude > b.magnitude;
      });
      peaks.resize(10000);

      // Re-sort by time for fingerprinting
      std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
        return a.timeBin < b.timeBin;
      });
    }

    return peaks;
  }
};

// ====================== Fingerprint Structure ======================
struct Fingerprint {
  uint64_t hash;
  int timeOffset;

  Fingerprint(uint64_t h, int t) : hash(h), timeOffset(t) {}
};

// ====================== Hash Generator ======================
class HashGenerator {
public:
  static uint64_t generate(int freq1, int freq2, int deltaTime) {
    uint64_t hash = 0;
    hash |= (uint64_t)(freq1 & 0xFFFF);
    hash |= ((uint64_t)(freq2 & 0xFFFF)) << 16;
    hash |= ((uint64_t)(deltaTime & 0xFFFF)) << 32;
    return hash;
  }
};

// ====================== Fingerprint Generator ======================
class FingerprintGenerator {
private:
  int maxPairs_;
  int minDeltaTime_;
  int maxDeltaTime_;

public:
  FingerprintGenerator(int maxPairs = 10, int minDT = 1, int maxDT = 100)
      : maxPairs_(maxPairs), minDeltaTime_(minDT), maxDeltaTime_(maxDT) {}

  std::vector<Fingerprint> generate(const std::vector<Peak> &peaks) const {
    std::vector<Fingerprint> fingerprints;

    for (size_t i = 0; i < peaks.size(); i++) {
      int pairedCount = 0;

      for (size_t j = i + 1; j < peaks.size() && pairedCount < maxPairs_; j++) {
        int deltaTime = peaks[j].timeBin - peaks[i].timeBin;

        if (deltaTime >= minDeltaTime_ && deltaTime <= maxDeltaTime_) {
          uint64_t hash = HashGenerator::generate(peaks[i].freqBin,
                                                  peaks[j].freqBin, deltaTime);
          fingerprints.emplace_back(hash, peaks[i].timeBin);
          pairedCount++;
        } else if (deltaTime > maxDeltaTime_) {
          break;
        }
      }
    }

    return fingerprints;
  }
};

// ====================== Fingerprint Database ======================
class FingerprintDatabase {
private:
  std::map<std::string, std::vector<Fingerprint>> songs_;
  std::unordered_map<uint64_t, std::vector<std::pair<std::string, int>>>
      hashIndex_;

  void buildIndex(const std::string &songName,
                  const std::vector<Fingerprint> &fingerprints) {
    for (const auto &fp : fingerprints) {
      hashIndex_[fp.hash].emplace_back(songName, fp.timeOffset);
    }
  }

public:
  void addSong(const std::string &name,
               const std::vector<Fingerprint> &fingerprints) {
    songs_[name] = fingerprints;
    buildIndex(name, fingerprints);
  }

  void save(const std::string &filename) const {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
      throw std::runtime_error("Cannot write database file: " + filename);
    }

    size_t numSongs = songs_.size();
    out.write(reinterpret_cast<const char *>(&numSongs), sizeof(numSongs));

    for (const auto &[name, fingerprints] : songs_) {
      // Write song name
      size_t nameLength = name.size();
      out.write(reinterpret_cast<const char *>(&nameLength),
                sizeof(nameLength));
      out.write(name.data(), nameLength);

      // Write fingerprints
      size_t numFingerprints = fingerprints.size();
      out.write(reinterpret_cast<const char *>(&numFingerprints),
                sizeof(numFingerprints));

      for (const auto &fp : fingerprints) {
        out.write(reinterpret_cast<const char *>(&fp.hash), sizeof(fp.hash));
        out.write(reinterpret_cast<const char *>(&fp.timeOffset),
                  sizeof(fp.timeOffset));
      }
    }

    std::cout << "Database saved to " << filename << "\n";
  }

  void load(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
      throw std::runtime_error("Cannot read database file: " + filename);
    }

    clear();

    size_t numSongs;
    in.read(reinterpret_cast<char *>(&numSongs), sizeof(numSongs));

    for (size_t i = 0; i < numSongs; i++) {
      // Read song name
      size_t nameLength;
      in.read(reinterpret_cast<char *>(&nameLength), sizeof(nameLength));
      std::string name(nameLength, '\0');
      in.read(&name[0], nameLength);

      // Read fingerprints
      size_t numFingerprints;
      in.read(reinterpret_cast<char *>(&numFingerprints),
              sizeof(numFingerprints));

      std::vector<Fingerprint> fingerprints;
      fingerprints.reserve(numFingerprints);

      for (size_t j = 0; j < numFingerprints; j++) {
        uint64_t hash;
        int timeOffset;
        in.read(reinterpret_cast<char *>(&hash), sizeof(hash));
        in.read(reinterpret_cast<char *>(&timeOffset), sizeof(timeOffset));
        fingerprints.emplace_back(hash, timeOffset);
      }

      addSong(name, fingerprints);
    }

    std::cout << "Database loaded: " << numSongs << " songs, "
              << hashIndex_.size() << " unique hashes\n";
  }

  std::string match(const std::vector<Fingerprint> &queryFingerprints) const {
    std::map<std::string, std::map<int, int>> timeDeltaCounts;
    int totalMatches = 0;

    // Find matching hashes
    for (const auto &queryFp : queryFingerprints) {
      auto it = hashIndex_.find(queryFp.hash);
      if (it != hashIndex_.end()) {
        totalMatches++;
        for (const auto &[songName, songTime] : it->second) {
          int timeDelta = songTime - queryFp.timeOffset;
          timeDeltaCounts[songName][timeDelta]++;
        }
      }
    }

    std::cout << "Hash matches found: " << totalMatches << " / "
              << queryFingerprints.size() << "\n";

    // Find best match
    std::string bestMatch;
    int maxCount = 0;

    std::cout << "\nMatch results:\n";
    for (const auto &[songName, deltaCounts] : timeDeltaCounts) {
      int songTotalMatches = 0;
      int bestDelta = 0;
      int bestDeltaCount = 0;

      for (const auto &[delta, count] : deltaCounts) {
        songTotalMatches += count;
        if (count > bestDeltaCount) {
          bestDeltaCount = count;
          bestDelta = delta;
        }
      }

      if (bestDeltaCount > maxCount) {
        maxCount = bestDeltaCount;
        bestMatch = songName;
      }

      std::cout << "  " << songName << ": " << songTotalMatches << " total, "
                << bestDeltaCount << " at Δt=" << bestDelta << " (~"
                << (bestDelta * 512 / 44100.0) << "s)\n";
    }

    return bestMatch;
  }

  void clear() {
    songs_.clear();
    hashIndex_.clear();
  }

  bool isEmpty() const { return songs_.empty(); }

  size_t getNumSongs() const { return songs_.size(); }
};

// ====================== Audio Fingerprinter ======================
class AudioFingerprinter {
private:
  SpectrogramGenerator spectrogramGen_;
  PeakDetector peakDetector_;
  FingerprintGenerator fingerprintGen_;

public:
  AudioFingerprinter(int frameSize = 2048, int hopSize = 512,
                     double peakPercentile = 98.5)
      : spectrogramGen_(frameSize, hopSize), peakDetector_(peakPercentile) {}

  std::vector<Fingerprint> process(const std::vector<double> &audio) const {
    auto spectrogram = spectrogramGen_.compute(audio);
    auto peaks = peakDetector_.findPeaks(spectrogram);
    auto fingerprints = fingerprintGen_.generate(peaks);

    std::cout << "Peaks: " << peaks.size() << "\n";
    std::cout << "Fingerprints: " << fingerprints.size() << "\n";

    return fingerprints;
  }
};

// ====================== Application Controller ======================
class AudioFingerprintApp {
private:
  FingerprintDatabase database_;
  AudioFingerprinter fingerprinter_;
  std::string dbFilePath_;

public:
  explicit AudioFingerprintApp(const std::string &dbFile = "fingerprint.db")
      : dbFilePath_(dbFile) {}

  void indexSongs(const std::vector<std::string> &songPaths) {
    // Try to load existing database
    try {
      database_.load(dbFilePath_);
    } catch (...) {
      std::cout << "Creating new database\n";
    }

    // Index each song
    for (const auto &path : songPaths) {
      std::cout << "\n=== Indexing: " << path << " ===\n";

      AudioReader reader(path);
      reader.load();

      std::cout << "Audio: " << reader.getNumSamples() << " samples, "
                << reader.getSampleRate() << " Hz\n";

      auto fingerprints = fingerprinter_.process(reader.getAudioData());
      database_.addSong(path, fingerprints);
    }

    // Save database
    database_.save(dbFilePath_);
    std::cout << "\n=== Database saved ===\n";
  }

  void matchQuery(const std::string &queryPath) {
    // Load database
    database_.load(dbFilePath_);

    if (database_.isEmpty()) {
      throw std::runtime_error("Database is empty! Run 'index' first.");
    }

    std::cout << "\n=== Matching: " << queryPath << " ===\n";

    // Process query
    AudioReader reader(queryPath);
    reader.load();

    auto fingerprints = fingerprinter_.process(reader.getAudioData());

    std::cout << "Query fingerprints: " << fingerprints.size() << "\n";

    // Match against database
    std::string match = database_.match(fingerprints);

    if (!match.empty()) {
      std::cout << "\n*** BEST MATCH: " << match << " ***\n";
    } else {
      std::cout << "\n*** NO MATCH FOUND ***\n";
    }
  }

  void clearDatabase() {
    std::remove(dbFilePath_.c_str());
    std::cout << "Database cleared\n";
  }
};

// ====================== MAIN ======================
int main(int argc, char *argv[]) {
  try {
    if (argc < 2) {
      std::cerr << "Usage: " << argv[0] << " <mode> [files...]\n";
      std::cerr << "Modes:\n";
      std::cerr
          << "  index <song.wav> [song2.wav ...] - Add songs to database\n";
      std::cerr << "  match <query.wav> - Match against database\n";
      std::cerr << "  clear - Clear database\n";
      return 1;
    }

    std::string mode = argv[1];
    AudioFingerprintApp app;

    if (mode == "index") {
      std::vector<std::string> songPaths;
      for (int i = 2; i < argc; i++) {
        songPaths.push_back(argv[i]);
      }
      app.indexSongs(songPaths);

    } else if (mode == "match") {
      if (argc != 3) {
        std::cerr << "Match mode requires exactly one query file\n";
        return 1;
      }
      app.matchQuery(argv[2]);

    } else if (mode == "clear") {
      app.clearDatabase();

    } else {
      std::cerr << "Unknown mode: " << mode << "\n";
      return 1;
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
