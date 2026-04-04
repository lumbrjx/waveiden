#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sndfile.h>
#include <unordered_map>
#include <vector>

constexpr double PI = 3.14159265358979323846;

// ====================== Complex ======================
struct Complex {
  double re, im;
  Complex(double r = 0, double i = 0) : re(r), im(i) {}
  Complex operator+(const Complex &o) const { return {re + o.re, im + o.im}; }
  Complex operator-(const Complex &o) const { return {re - o.re, im - o.im}; }
  Complex operator*(const Complex &o) const {
    return {re * o.re - im * o.im, re * o.im + im * o.re};
  }
};

// ====================== FFT ======================
void fft(std::vector<Complex> &a) {
  int n = a.size();
  if (n <= 1)
    return;

  std::vector<Complex> even(n / 2), odd(n / 2);
  for (int i = 0; i < n / 2; i++) {
    even[i] = a[i * 2];
    odd[i] = a[i * 2 + 1];
  }

  fft(even);
  fft(odd);

  for (int k = 0; k < n / 2; k++) {
    double ang = -2 * PI * k / n;
    Complex w(std::cos(ang), std::sin(ang));
    Complex t = w * odd[k];
    a[k] = even[k] + t;
    a[k + n / 2] = even[k] - t;
  }
}

// ====================== WAV ======================
std::vector<double> readWav(const std::string &path, int &sampleRate) {
  SF_INFO info{};
  SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
  if (!file)
    throw std::runtime_error("Cannot open WAV file");

  sampleRate = info.samplerate;

  std::vector<double> buf(info.frames * info.channels);
  sf_readf_double(file, buf.data(), info.frames);
  sf_close(file);

  std::vector<double> mono(info.frames);
  for (int i = 0; i < info.frames; i++)
    mono[i] = buf[i * info.channels];
  return mono;
}

// ====================== Window ======================
void applyHann(std::vector<double> &f) {
  int N = f.size();
  for (int i = 0; i < N; i++)
    f[i] *= 0.5 * (1 - std::cos(2 * PI * i / (N - 1)));
}

// ====================== Spectrogram ======================
std::vector<std::vector<double>>
computeSpectrogram(const std::vector<double> &audio, int frameSize,
                   int hopSize) {

  std::vector<std::vector<double>> spec;

  for (int pos = 0; pos + frameSize <= audio.size(); pos += hopSize) {
    std::vector<double> frame(audio.begin() + pos,
                              audio.begin() + pos + frameSize);
    applyHann(frame);

    std::vector<Complex> c(frame.begin(), frame.end());
    fft(c);

    std::vector<double> mag(frameSize / 2);
    for (int i = 0; i < mag.size(); i++)
      mag[i] = std::sqrt(c[i].re * c[i].re + c[i].im * c[i].im);

    spec.push_back(mag);
  }
  return spec;
}

// ====================== Peak Detection ======================
struct Peak {
  int freqBin;
  int timeBin;
  double magnitude;
};

// More aggressive peak selection
std::vector<Peak> findPeaks(const std::vector<std::vector<double>> &spec,
                            double percentile = 98.5) {
  std::vector<Peak> peaks;

  // Collect all magnitudes for adaptive thresholding
  std::vector<double> allMags;
  for (const auto &frame : spec) {
    for (double v : frame) {
      if (v > 0)
        allMags.push_back(v);
    }
  }

  // Calculate adaptive threshold - USE HIGHER PERCENTILE
  std::sort(allMags.begin(), allMags.end());
  double threshold =
      allMags[static_cast<int>(allMags.size() * percentile / 100.0)];

  std::cout << "Adaptive threshold (" << percentile
            << "th percentile): " << threshold << "\n";

  // Find local maxima
  for (int t = 1; t < spec.size() - 1; t++) {
    for (int f = 1; f < spec[t].size() - 1; f++) {
      double v = spec[t][f];
      if (v > spec[t][f - 1] && v > spec[t][f + 1] && v > spec[t - 1][f] &&
          v > spec[t + 1][f] && v > threshold) {
        peaks.push_back({f, t, v});
      }
    }
  }

  // Further reduce by keeping only top N peaks
  if (peaks.size() > 10000) {
    std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
      return a.magnitude > b.magnitude;
    });
    peaks.resize(10000);
    // Re-sort by time for pairing
    std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
      return a.timeBin < b.timeBin;
    });
  }

  return peaks;
}

// ====================== Hash ======================
uint64_t makeHash(int f1, int f2, int dt) {
  uint64_t h = 0;
  h |= (uint64_t)(f1 & 0xFFFF);
  h |= ((uint64_t)(f2 & 0xFFFF)) << 16;
  h |= ((uint64_t)(dt & 0xFFFF)) << 32;
  return h;
}

// ====================== Fingerprint Database ======================
struct Fingerprint {
  uint64_t hash;
  int timeOffset;
};

class FingerprintDB {
public:
  void addSong(const std::string &name,
               const std::vector<Fingerprint> &prints) {
    songs[name] = prints;

    // Build hash index for fast lookup
    for (const auto &fp : prints) {
      hashIndex[fp.hash].push_back({name, fp.timeOffset});
    }
  }

  void save(const std::string &filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out)
      throw std::runtime_error("Cannot write database file");

    size_t numSongs = songs.size();
    out.write((char *)&numSongs, sizeof(numSongs));

    for (const auto &[name, prints] : songs) {
      size_t nameLen = name.size();
      out.write((char *)&nameLen, sizeof(nameLen));
      out.write(name.data(), nameLen);

      size_t numPrints = prints.size();
      out.write((char *)&numPrints, sizeof(numPrints));

      for (const auto &fp : prints) {
        out.write((char *)&fp.hash, sizeof(fp.hash));
        out.write((char *)&fp.timeOffset, sizeof(fp.timeOffset));
      }
    }

    std::cout << "Database saved to " << filename << "\n";
  }

  void load(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in)
      throw std::runtime_error("Cannot read database file");

    songs.clear();
    hashIndex.clear();

    size_t numSongs;
    in.read((char *)&numSongs, sizeof(numSongs));

    for (size_t i = 0; i < numSongs; i++) {
      size_t nameLen;
      in.read((char *)&nameLen, sizeof(nameLen));
      std::string name(nameLen, '\0');
      in.read(&name[0], nameLen);

      size_t numPrints;
      in.read((char *)&numPrints, sizeof(numPrints));

      std::vector<Fingerprint> prints(numPrints);
      for (auto &fp : prints) {
        in.read((char *)&fp.hash, sizeof(fp.hash));
        in.read((char *)&fp.timeOffset, sizeof(fp.timeOffset));
      }

      addSong(name, prints); // This builds the index
    }

    std::cout << "Database loaded: " << numSongs << " songs, "
              << hashIndex.size() << " unique hashes\n";
  }

  std::string match(const std::vector<Fingerprint> &query) {
    std::map<std::string, std::map<int, int>> timeDeltaCounts;

    int matches = 0;

    // Use hash index for O(1) lookup instead of nested loops!
    for (const auto &qp : query) {
      auto it = hashIndex.find(qp.hash);
      if (it != hashIndex.end()) {
        matches++;
        for (const auto &[songName, songTime] : it->second) {
          int timeDelta = songTime - qp.timeOffset;
          timeDeltaCounts[songName][timeDelta]++;
        }
      }
    }

    std::cout << "Hash matches found: " << matches << " / " << query.size()
              << "\n";

    // Find song with strongest time-consistent matches
    std::string bestMatch;
    int maxCount = 0;

    std::cout << "\nMatch results:\n";
    for (const auto &[songName, deltaCounts] : timeDeltaCounts) {
      int totalMatches = 0;
      int bestDelta = 0;
      int bestDeltaCount = 0;

      for (const auto &[delta, count] : deltaCounts) {
        totalMatches += count;
        if (count > bestDeltaCount) {
          bestDeltaCount = count;
          bestDelta = delta;
        }
      }

      if (bestDeltaCount > maxCount) {
        maxCount = bestDeltaCount;
        bestMatch = songName;
      }

      std::cout << "  " << songName << ": " << totalMatches << " total, "
                << bestDeltaCount << " at Δt=" << bestDelta << " (~"
                << (bestDelta * 512 / 44100.0) << "s)\n";
    }

    return bestMatch;
  }

  bool isEmpty() const { return songs.empty(); }

private:
  std::map<std::string, std::vector<Fingerprint>> songs;
  // Hash index: hash -> list of (song_name, time_offset)
  std::unordered_map<uint64_t, std::vector<std::pair<std::string, int>>>
      hashIndex;
};

// ====================== Create Fingerprints ======================
std::vector<Fingerprint> createFingerprints(const std::vector<Peak> &peaks) {
  std::vector<Fingerprint> prints;

  // Reduce pairing to avoid explosion
  for (int i = 0; i < peaks.size(); i++) {
    int paired = 0;
    for (int j = i + 1; j < peaks.size() && paired < 10;
         j++) { // Reduced from 20 to 10
      int dt = peaks[j].timeBin - peaks[i].timeBin;

      if (dt >= 1 && dt <= 100) { // Reduced from 200 to 100
        uint64_t h = makeHash(peaks[i].freqBin, peaks[j].freqBin, dt);
        prints.push_back({h, peaks[i].timeBin});
        paired++;
      } else if (dt > 100) {
        break;
      }
    }
  }

  return prints;
}

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
    int frameSize = 2048;
    int hopSize = 512;
    std::string dbFile = "fingerprint.db";

    FingerprintDB db;

    if (mode == "index") {
      try {
        db.load(dbFile);
      } catch (...) {
        std::cout << "Creating new database\n";
      }

      for (int i = 2; i < argc; i++) {
        std::cout << "\n=== Indexing: " << argv[i] << " ===\n";

        int sampleRate;
        auto audio = readWav(argv[i], sampleRate);
        std::cout << "Audio: " << audio.size() << " samples, " << sampleRate
                  << " Hz\n";

        auto spec = computeSpectrogram(audio, frameSize, hopSize);
        auto peaks = findPeaks(spec, 98.5); // Higher percentile
        auto prints = createFingerprints(peaks);

        db.addSong(argv[i], prints);

        std::cout << "Peaks: " << peaks.size() << "\n";
        std::cout << "Fingerprints: " << prints.size() << "\n";
      }

      db.save(dbFile);
      std::cout << "\n=== Database saved ===\n";

    } else if (mode == "match") {
      if (argc != 3) {
        std::cerr << "Match mode requires exactly one query file\n";
        return 1;
      }

      db.load(dbFile);

      if (db.isEmpty()) {
        std::cerr << "Database is empty! Run 'index' first.\n";
        return 1;
      }

      std::cout << "\n=== Matching: " << argv[2] << " ===\n";

      int sampleRate;
      auto audio = readWav(argv[2], sampleRate);
      auto spec = computeSpectrogram(audio, frameSize, hopSize);
      auto peaks = findPeaks(spec, 98.5); // Higher percentile
      auto prints = createFingerprints(peaks);

      std::cout << "Query peaks: " << peaks.size() << "\n";
      std::cout << "Query fingerprints: " << prints.size() << "\n";

      std::string match = db.match(prints);

      if (!match.empty()) {
        std::cout << "\n*** BEST MATCH: " << match << " ***\n";
      } else {
        std::cout << "\n*** NO MATCH FOUND ***\n";
      }

    } else if (mode == "clear") {
      std::remove(dbFile.c_str());
      std::cout << "Database cleared\n";
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
