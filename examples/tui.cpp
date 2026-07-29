#define _XOPEN_SOURCE_EXTENDED 1
#include "tui.hpp"

#include <waveiden/waveiden.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <curses.h>
#include <cwchar>
#include <fcntl.h>
#include <filesystem>
#include <portaudio.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

constexpr short kCyan = 1, kViolet = 2, kPink = 3, kLime = 4, kAmber = 5,
                kMuted = 6, kPanel = 7, kInk = 8;

struct Ui {
  waveiden::SQLiteDatabase db;
  waveiden::FingerprintEngine engine;
  std::string notice = "READY  ·  WAITING FOR A SIGNAL";
  std::string resultTitle;
  std::string resultDetail;
  short resultColor = kLime;
  int selected = 0;
  int frame = 0;
};

struct AnalysisData {
  waveiden::AudioBuffer audio;
  std::vector<std::vector<double>> spectrogram;
  double maxMagnitude = 1.0;
};

struct CaptureState {
  std::vector<double> samples;
  std::atomic<size_t> cursor{0};
  int sampleRate = 0;
  size_t analyzed = 0;
  AnalysisData preview;
};

struct ActionCancelled : std::exception {
  const char *what() const noexcept override { return "Action cancelled"; }
};

void renderRecording(Ui &ui, CaptureState &capture, int seconds);
void drawSpectrogram(const AnalysisData &data, int y, int x, int width,
                     int height, int step);

class StderrSilencer {
public:
  StderrSilencer() {
    saved_ = dup(STDERR_FILENO);
    null_ = open("/dev/null", O_WRONLY);
    if (saved_ >= 0 && null_ >= 0)
      dup2(null_, STDERR_FILENO);
  }
  ~StderrSilencer() {
    if (saved_ >= 0) {
      dup2(saved_, STDERR_FILENO);
      close(saved_);
    }
    if (null_ >= 0)
      close(null_);
  }

private:
  int saved_ = -1;
  int null_ = -1;
};

int captureCallback(const void *input, void *, unsigned long frames,
                    const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags,
                    void *userData) {
  auto *capture = static_cast<CaptureState *>(userData);
  const auto *source = static_cast<const float *>(input);
  size_t cursor = capture->cursor.load(std::memory_order_relaxed);
  for (unsigned long frame = 0;
       frame < frames && cursor < capture->samples.size(); ++frame) {
    capture->samples[cursor++] = source == nullptr ? 0.0 : source[frame];
  }
  capture->cursor.store(cursor, std::memory_order_release);
  return cursor == capture->samples.size() ? paComplete : paContinue;
}

waveiden::AudioBuffer recordMicrophone(Ui &ui, int seconds = 7) {
  CaptureState capture;
  StderrSilencer silenceBackend;
  if (Pa_Initialize() != paNoError)
    throw std::runtime_error("Could not initialize microphone capture");
  const PaDeviceIndex device = Pa_GetDefaultInputDevice();
  const PaDeviceInfo *deviceInfo =
      device == paNoDevice ? nullptr : Pa_GetDeviceInfo(device);
  if (deviceInfo == nullptr || deviceInfo->defaultSampleRate <= 0) {
    Pa_Terminate();
    throw std::runtime_error("No default microphone is available");
  }
  capture.sampleRate = static_cast<int>(std::lround(deviceInfo->defaultSampleRate));
  if (capture.sampleRate < 8000 || capture.sampleRate > 192000) {
    Pa_Terminate();
    throw std::runtime_error("Default microphone has an unsupported sample rate");
  }
  capture.samples.resize(static_cast<size_t>(capture.sampleRate) * seconds);
  PaStream *stream = nullptr;
  const PaError opened = Pa_OpenDefaultStream(
      &stream, 1, 0, paFloat32, capture.sampleRate, 256, captureCallback, &capture);
  if (opened != paNoError) {
    Pa_Terminate();
    throw std::runtime_error("Could not open the default microphone");
  }
  if (Pa_StartStream(stream) != paNoError) {
    Pa_CloseStream(stream);
    Pa_Terminate();
    throw std::runtime_error("Could not start microphone capture");
  }
  while (Pa_IsStreamActive(stream) == 1) {
    const int key = getch();
    if (key == 3 || key == 27) {
      Pa_AbortStream(stream);
      Pa_CloseStream(stream);
      Pa_Terminate();
      throw ActionCancelled();
    }
    renderRecording(ui, capture, seconds);
    refresh();
    Pa_Sleep(40);
  }
  Pa_CloseStream(stream);
  Pa_Terminate();
  capture.samples.resize(capture.cursor.load());
  return {std::move(capture.samples), capture.sampleRate, 1};
}

std::string expandHomePath(const std::string &path) {
  if (path == "~" || (path.size() > 2 && path[0] == '~' && path[1] == '/')) {
    const char *homeDirectory = std::getenv("HOME");
    if (homeDirectory != nullptr)
      return std::string(homeDirectory) + path.substr(1);
  }
  return path;
}

void text(int y, int x, const std::string &value, short color = kInk,
          int attrs = 0) {
  if (y < 0 || y >= LINES || x < 0 || x >= COLS)
    return;
  attron(COLOR_PAIR(color));
  if (attrs)
    attron(attrs);
  mvaddnstr(y, x, value.c_str(), COLS - x - 1);
  if (attrs)
    attroff(attrs);
  attroff(COLOR_PAIR(color));
}

void wideText(int y, int x, const std::wstring &value, short color = kInk,
              int attrs = 0) {
  if (y < 0 || y >= LINES || x < 0 || x >= COLS)
    return;
  attron(COLOR_PAIR(color));
  if (attrs)
    attron(attrs);
  mvaddnwstr(y, x, value.c_str(), COLS - x - 1);
  if (attrs)
    attroff(attrs);
  attroff(COLOR_PAIR(color));
}

void rule(int y, int x, int width) {
  wideText(y, x, std::wstring(std::max(0, width), L'─'), kPanel);
}

void box(int y, int x, int height, int width, const std::string &label) {
  wideText(y, x, L"╭", kViolet);
  wideText(y, x + 1, std::wstring(width - 2, L'─'), kViolet);
  wideText(y, x + width - 1, L"╮", kViolet);
  for (int row = 1; row < height - 1; ++row) {
    wideText(y + row, x, L"│", kViolet);
    wideText(y + row, x + width - 1, L"│", kViolet);
  }
  wideText(y + height - 1, x, L"╰", kViolet);
  wideText(y + height - 1, x + 1, std::wstring(width - 2, L'─'), kViolet);
  wideText(y + height - 1, x + width - 1, L"╯", kViolet);
  text(y, x + 2, " " + label + " ", kViolet, A_BOLD);
}

void fingerprint(int top, int left, int frame) {
  static const char *pixels[] = {"     . . .     ", "   .       .   ",
                                 "  .  . . .  .  ", " .  .     .  . ",
                                 " . .  . .  . . ", "  .  .   .  .  ",
                                 "   .  . .  .   ", "     . . .     "};
  for (int row = 0; row < 8; ++row) {
    std::string value = pixels[row];
    if ((frame / 5 + row) % 3 == 0)
      value[7] = 'o';
    text(top + row, left, value, row % 2 ? kViolet : kCyan);
  }
}

void pixelLogo(int y, int x, int phase = 0) {
  static const wchar_t *rows[] = {
      L"██     ██   █████   ██    ██ ███████ ██ ██████   ███████ ███    ██",
      L"██     ██  ██   ██  ██    ██ ██      ██ ██   ██  ██      ████   ██",
      L"██  █  ██ ███████  ██    ██ █████   ██ ██   ██  █████   ██ ██  ██",
      L"██ ███ ██ ██   ██   ██  ██  ██      ██ ██   ██  ██      ██  ██ ██",
      L" ███ ███  ██   ██    ████   ███████ ██ ██████   ███████ ██   ████"};
  for (int i = 0; i < 5; ++i) {
    wideText(y + i + 1, x + 1, rows[i], kPanel, A_DIM);
    wideText(y + i, x, rows[i], i == 0 ? kInk : kViolet, A_BOLD);
  }
}

void musicNoteGlyph(int y, int x, int frame, bool listening) {
  // A half block is one terminal cell wide and half a cell tall, so these
  // become near-square, solid pixels instead of dotted braille fragments.
  bool bitmap[28][24]{};
  auto fill = [&bitmap](int left, int top, int right, int bottom) {
    for (int row = std::max(0, top); row <= std::min(27, bottom); ++row)
      for (int col = std::max(0, left); col <= std::min(23, right); ++col)
        bitmap[row][col] = true;
  };
  auto head = [&bitmap](int centerX, int centerY) {
    for (int row = centerY - 5; row <= centerY + 5; ++row)
      for (int col = centerX - 5; col <= centerX + 5; ++col)
        if (row >= 0 && row < 28 && col >= 0 && col < 24 &&
            (col - centerX) * (col - centerX) * 25 +
                    (row - centerY) * (row - centerY) * 25 <=
                625)
          bitmap[row][col] = true;
  };
  // A beamed pair of eighth notes, rasterized at a 24x28 pixel grid and
  // packed into 24 columns by 14 rows of terminal half blocks.
  fill(10, 3, 12, 23);
  head(5, 23);
  head(18, 17);
  fill(21, 10, 23, 17);
  for (int col = 12; col <= 23; ++col)
    fill(col, 3 + (col - 12) / 2, col, 7 + (col - 12) / 2);

  static const int bob[] = {0, 0, -1, 0, 1, 0};
  const int offset = listening ? bob[(frame / 3) % 6] : 0;
  const short ink = listening && (frame / 4) % 2 ? kPink : kCyan;
  for (int row = 0; row < 14; ++row) {
    std::wstring line;
    for (int col = 0; col < 24; ++col) {
      const bool upper = bitmap[row * 2][col], lower = bitmap[row * 2 + 1][col];
      line += upper && lower ? L'█' : upper ? L'▀' : lower ? L'▄' : L' ';
    }
    wideText(y + row + 1, x + 1, line, kPanel, A_DIM);
    wideText(y + row + offset, x, line, row < 3 ? kViolet : ink, A_BOLD);
  }
  if (listening) {
    const int radius = 1 + (frame / 3) % 4;
    wideText(y + 7, x - radius, L"◆", kPink, A_BOLD);
    wideText(y + 3, x + 25 + radius, L"◆", kPink, A_BOLD);
    if (radius > 2) {
      wideText(y + 1, x - radius, L"·", kViolet);
      wideText(y + 11, x + 26 + radius, L"·", kViolet);
    }
  }
}

struct DashboardLayout {
  int margin = 2;
  int width = COLS - margin * 2;
  int topY = 8;
  int footerH = 3;
  int topH = std::min(17, LINES - topY - footerH - 6);
  int spectrumY = topY + topH + 1;
  int spectrumH = LINES - footerH - spectrumY;
  int captureW = std::max(34, width * 2 / 5);
  int libraryX = margin + captureW + 1;
  int libraryW = width - captureW - 1;
};

void drawHeader(const DashboardLayout &layout, const Ui &ui) {
  box(0, layout.margin, 7, layout.width,
      "WAVEIDEN  /  ACOUSTIC FINGERPRINT CONSOLE");
  pixelLogo(1, layout.margin + 2, ui.frame);
}

void drawCapturePanel(const DashboardLayout &layout, const Ui &ui,
                      bool recording, int seconds, size_t recorded,
                      int sampleRate) {
  box(layout.topY, layout.margin, layout.topH, layout.captureW,
      recording ? "CAPTURE  /  LIVE" : "CAPTURE  /  MUSIC ID");
  musicNoteGlyph(layout.topY + 2,
                 layout.margin + std::max(2, layout.captureW / 2 - 12),
                 ui.frame, recording);
  text(layout.topY + 1, layout.margin + 3,
       recording ? "RECORDING NOW" : "[ C ]  START LISTENING",
       recording ? kPink : kLime, A_BOLD);
  if (layout.topH >= 14 && recording) {
    text(layout.topY + 2, layout.margin + 3,
         std::to_string(recorded / static_cast<size_t>(sampleRate)) + " / " +
             std::to_string(seconds) +
             " seconds  ·  Esc cancels",
         kMuted);
  } else if (layout.topH >= 14) {
    text(layout.topY + 2, layout.margin + 3, "7 second capture", kMuted);
  }
}

void drawLibraryPanel(const DashboardLayout &layout, Ui &ui) {
  box(layout.topY, layout.libraryX, layout.topH, layout.libraryW,
      "LIBRARY  /  INDEXED TRACKS");
  const auto songs = ui.db.listSongs();
  text(layout.topY + 2, layout.libraryX + 3, "TRACK", kMuted, A_BOLD);
  text(layout.topY + 2, layout.libraryX + layout.libraryW - 13, "STATE", kMuted,
       A_BOLD);
  rule(layout.topY + 3, layout.libraryX + 2, layout.libraryW - 4);
  const int rows = std::max(1, layout.topH - 9);
  if (songs.empty()) {
    fingerprint(layout.topY + 5, layout.libraryX + 4, ui.frame);
    text(layout.topY + 6, layout.libraryX + 23, "No tracks indexed", kInk,
         A_BOLD);
    text(layout.topY + 8, layout.libraryX + 23, "[ I ] add your first track",
         kViolet);
  } else {
    ui.selected =
        std::clamp(ui.selected, 0, static_cast<int>(songs.size()) - 1);
    const int visible = std::min(static_cast<int>(songs.size()), rows);
    const int first = std::clamp(ui.selected - visible + 1, 0,
                                 static_cast<int>(songs.size()) - visible);
    for (int row = 0; row < visible; ++row) {
      const int songIndex = first + row;
      const bool selected = songIndex == ui.selected;
      text(layout.topY + 5 + row, layout.libraryX + 3, selected ? ">" : " ",
           selected ? kViolet : kMuted, A_BOLD);
      text(layout.topY + 5 + row, layout.libraryX + 5,
           std::to_string(songIndex + 1), kMuted);
      text(layout.topY + 5 + row, layout.libraryX + 8, songs[songIndex],
           selected ? kViolet : kInk, selected ? A_BOLD : 0);
      text(layout.topY + 5 + row, layout.libraryX + layout.libraryW - 13,
           "READY", kLime, A_BOLD);
    }
  }
  const int resultY = layout.topY + layout.topH - 3;
  text(resultY, layout.libraryX + 3,
       ui.resultTitle.empty() ? "STATUS  " + ui.notice : ui.resultTitle,
       ui.resultTitle.empty() ? kAmber : ui.resultColor, A_BOLD);
  if (layout.topH >= 12 && !ui.resultDetail.empty())
    text(resultY + 1, layout.libraryX + 3, ui.resultDetail, kMuted);
}

void drawSpectrumPanel(const DashboardLayout &layout, const Ui &ui,
                       const AnalysisData *signal, bool recording) {
  box(layout.spectrumY, layout.margin, layout.spectrumH, layout.width,
      recording ? "SPECTROGRAM  /  RECORDING STREAM"
                : "SPECTROGRAM  /  SIGNAL MONITOR");
  text(layout.spectrumY + 1, layout.margin + 3, "LOW Hz", kMuted);
  text(layout.spectrumY + 1, COLS - 14, "HIGH Hz", kMuted);
  if (signal)
    drawSpectrogram(*signal, layout.spectrumY + 2, layout.margin + 3,
                    layout.width - 6, std::max(1, layout.spectrumH - 4),
                    ui.frame);
  else
    text(layout.spectrumY + layout.spectrumH / 2,
         layout.margin + layout.width / 2 - 12,
         "· · · WAITING FOR SIGNAL · · ·", kPanel, A_BOLD);
  text(layout.spectrumY + layout.spectrumH - 2, layout.margin + 3,
       "░ quiet   ▒ texture   ▓ harmonic   █ peak", kMuted);
}

void drawCommands(const DashboardLayout &layout) {
  box(LINES - layout.footerH, layout.margin, layout.footerH, layout.width,
      "COMMANDS  /  HELP + LEGEND");
  text(LINES - 2, layout.margin + 3,
       "[C] CAPTURE   [M] MATCH   [I] INDEX   [↑↓] SELECT   [D] DELETE   [Q] "
       "QUIT",
       kInk, A_BOLD);
}

void dashboard(Ui &ui, const AnalysisData *signal = nullptr,
               bool recording = false, int seconds = 7, size_t recorded = 0,
               int sampleRate = 44100) {
  erase();
  const DashboardLayout layout;
  drawHeader(layout, ui);
  drawCapturePanel(layout, ui, recording, seconds, recorded, sampleRate);
  drawLibraryPanel(layout, ui);
  drawSpectrumPanel(layout, ui, signal, recording);
  drawCommands(layout);
}

void deck(Ui &ui) { dashboard(ui); }

std::string prompt(const std::string &label) {
  curs_set(1);
  nodelay(stdscr, FALSE);
  text(LINES - 2, 5, label, kViolet, A_BOLD);
  clrtoeol();
  std::string value;
  const int inputX = 5 + static_cast<int>(label.size());
  for (;;) {
    move(LINES - 2, inputX + static_cast<int>(value.size()));
    const int key = getch();
    if (key == 3 || key == 27) { // Ctrl-C or Esc: cancel the active action.
      curs_set(0);
      nodelay(stdscr, TRUE);
      return {};
    }
    if (key == '\n' || key == KEY_ENTER || key == '\r')
      break;
    if ((key == KEY_BACKSPACE || key == 127 || key == 8) && !value.empty()) {
      value.pop_back();
    } else if (key >= 32 && key <= 126 && value.size() < 1023) {
      value += static_cast<char>(key);
    } else {
      continue;
    }
    text(LINES - 2, inputX, value, kInk);
    clrtoeol();
  }
  curs_set(0);
  nodelay(stdscr, TRUE);
  return value;
}

AnalysisData analyzeBuffer(const Ui &ui, waveiden::AudioBuffer audio) {
  AnalysisData data;
  data.audio = std::move(audio);
  data.spectrogram = waveiden::dsp::computeSpectrogram(
      data.audio.samples, ui.engine.config().frameSize,
      ui.engine.config().hopSize);
  for (const auto &frame : data.spectrogram)
    for (double magnitude : frame)
      data.maxMagnitude = std::max(data.maxMagnitude, magnitude);
  return data;
}

AnalysisData loadAnalysis(const Ui &ui, const std::string &path) {
  waveiden::WavReader reader;
  return analyzeBuffer(ui, reader.read(path));
}

void drawSpectrogram(const AnalysisData &data, int y, int x, int width,
                     int height, int step) {
  if (data.spectrogram.empty() || height <= 0)
    return;
  static const wchar_t *glyphs[] = {L"░", L"▒", L"▓", L"█"};
  const int visibleFrames =
      std::max(1, std::min(static_cast<int>(data.spectrogram.size()), width));
  const int offset = std::max(0, static_cast<int>(data.spectrogram.size()) -
                                     visibleFrames - step * 4);
  const int bins = static_cast<int>(data.spectrogram.front().size());
  for (int col = 0; col < width; ++col) {
    const int frame = offset + (col * visibleFrames / std::max(1, width));
    for (int row = 0; row < height; ++row) {
      const int bin =
          std::clamp((height - row - 1) * bins / height, 0, bins - 1);
      const double level = std::log1p(data.spectrogram[frame][bin]) /
                           std::log1p(data.maxMagnitude);
      if (level < .035)
        continue;
      const int glyph = std::min(3, static_cast<int>(level * 4));
      const short color = level > .72 ? kPink : level > .42 ? kViolet : kCyan;
      wideText(y + row, x + col, glyphs[glyph], color,
               level > .42 ? A_BOLD : 0);
    }
  }
}

void renderRecording(Ui &ui, CaptureState &capture, int seconds) {
  const size_t recorded = capture.cursor.load(std::memory_order_acquire);
  if (recorded >= 4096 &&
      recorded - capture.analyzed >= static_cast<size_t>(capture.sampleRate / 4)) {
    const size_t start = recorded > static_cast<size_t>(capture.sampleRate * 2)
                             ? recorded - capture.sampleRate * 2
                             : 0;
    waveiden::AudioBuffer recent;
    recent.sampleRate = capture.sampleRate;
    recent.channels = 1;
    recent.samples.assign(capture.samples.begin() + start,
                          capture.samples.begin() + recorded);
    capture.preview = analyzeBuffer(ui, std::move(recent));
    capture.analyzed = recorded;
  }
  dashboard(ui, &capture.preview, true, seconds, recorded, capture.sampleRate);
  ++ui.frame;
}

AnalysisData animateAnalysis(Ui &ui, const std::string &title,
                             const std::string &subject, AnalysisData data) {
  for (int step = 0; step < 14; ++step) {
    const int key = getch();
    if (key == 3 || key == 27)
      throw ActionCancelled();
    const std::string priorNotice = ui.notice;
    ui.notice = title + "  ·  " + std::to_string((step + 1) * 100 / 14) + "%";
    dashboard(ui, &data, true);
    ui.notice = priorNotice;
    refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(55));
  }
  return data;
}

AnalysisData animateWork(Ui &ui, const std::string &title,
                         const std::string &subject) {
  return animateAnalysis(ui, title, subject, loadAnalysis(ui, subject));
}

void setResult(Ui &ui, std::string title, std::string detail, short color,
               std::string notice) {
  ui.resultTitle = std::move(title);
  ui.resultDetail = std::move(detail);
  ui.resultColor = color;
  ui.notice = std::move(notice);
}

void moveSelection(Ui &ui, int direction) {
  const auto songs = ui.db.listSongs();
  if (songs.empty())
    return;
  ui.selected = std::clamp(ui.selected + direction, 0,
                           static_cast<int>(songs.size()) - 1);
}

void deleteSelectedSong(Ui &ui) {
  const auto songs = ui.db.listSongs();
  if (songs.empty()) {
    ui.notice = "Library is empty";
    return;
  }
  ui.selected = std::clamp(ui.selected, 0, static_cast<int>(songs.size()) - 1);
  const std::string name = songs[ui.selected];
  try {
    ui.db.removeSong(name);
    setResult(ui, "REMOVED  ·  " + name,
              "Track and its fingerprints were removed from the local library",
              kAmber, "Track removed");
  } catch (const std::exception &error) {
    setResult(ui, "REMOVE FAILED", error.what(), kPink, "Remove failed");
  }
}

void indexFile(Ui &ui) {
  const auto enteredPath = prompt("Index file  [Esc/Ctrl-C cancel]: ");
  if (enteredPath.empty())
    return;
  const std::string path = expandHomePath(enteredPath);
  ui.resultTitle.clear();
  try {
    const AnalysisData data = animateWork(ui, "INDEXING", path);
    const int count = ui.engine.indexBuffer(path, data.audio, ui.db);
    const std::string name = std::filesystem::path(path).filename().string();
    setResult(ui, "INDEXED  ·  " + name,
              std::to_string(count) +
                  " fingerprint hashes committed to the local library",
              kLime, "Index complete");
  } catch (const ActionCancelled &) {
    setResult(ui, "INDEX CANCELED", "No changes were made to the library.",
              kAmber, "Index canceled");
  } catch (const std::exception &error) {
    setResult(ui, "INDEX FAILED", error.what(), kPink, "Index failed");
  }
}

void identifyFile(Ui &ui) {
  const auto enteredPath = prompt("Identify clip  [Esc/Ctrl-C cancel]: ");
  if (enteredPath.empty())
    return;
  ui.resultTitle.clear();
  try {
    const AnalysisData data =
        animateWork(ui, "IDENTIFYING", expandHomePath(enteredPath));
    const auto result = ui.engine.matchBuffer(data.audio, ui.db);
    if (result.found) {
      setResult(ui, "MATCH FOUND  ·  " + result.songName,
                std::to_string(result.confidence) +
                    " consistent landmark votes  ·  offset " +
                    std::to_string(result.timeOffset) + "s",
                kLime, "Match found");
    } else {
      setResult(ui, "NO MATCH FOUND",
                "No time-consistent fingerprint candidate was found.", kAmber,
                "No match found");
    }
  } catch (const ActionCancelled &) {
    setResult(ui, "MATCH CANCELED", "No lookup result was recorded.", kAmber,
              "Match canceled");
  } catch (const std::exception &error) {
    setResult(ui, "IDENTIFICATION FAILED", error.what(), kPink, "Match failed");
  }
}

void identifyRecording(Ui &ui) {
  ui.resultTitle.clear();
  try {
    ui.notice = "Recording microphone for 7 seconds";
    const AnalysisData data = analyzeBuffer(ui, recordMicrophone(ui));
    const auto result = ui.engine.matchBuffer(data.audio, ui.db);
    if (result.found) {
      setResult(ui, "LIVE MATCH  ·  " + result.songName,
                std::to_string(result.confidence) +
                    " consistent landmark votes  ·  offset " +
                    std::to_string(result.timeOffset) + "s",
                kLime, "Live match found");
    } else {
      setResult(ui, "NO LIVE MATCH",
                "The recorded sample did not match the local library.", kAmber,
                "No live match");
    }
  } catch (const ActionCancelled &) {
    setResult(ui, "RECORDING CANCELED", "The microphone capture was discarded.",
              kAmber, "Recording canceled");
  } catch (const std::exception &error) {
    setResult(ui, "MICROPHONE FAILED", error.what(), kPink,
              "Microphone failed");
  }
}

bool handleKey(Ui &ui, int key) {
  if (key == 'q' || key == 'Q')
    return false;
  if (key == KEY_UP || key == 'k')
    moveSelection(ui, -1);
  else if (key == KEY_DOWN || key == 'j')
    moveSelection(ui, 1);
  else if (key == 'd' || key == 'D')
    deleteSelectedSong(ui);
  else if (key == 'i' || key == 'I')
    indexFile(ui);
  else if (key == 'm' || key == 'M')
    identifyFile(ui);
  else if (key == 'c' || key == 'C')
    identifyRecording(ui);
  return true;
}

void configureTerminal() {
  std::setlocale(LC_ALL, "");
  initscr();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  nodelay(stdscr, TRUE);
  start_color();
  use_default_colors();
  init_pair(kCyan, COLOR_CYAN, -1);
  init_pair(kViolet, COLOR_MAGENTA, -1);
  init_pair(kPink, COLOR_RED, -1);
  init_pair(kLime, COLOR_GREEN, -1);
  init_pair(kAmber, COLOR_YELLOW, -1);
  init_pair(kMuted, COLOR_BLUE, -1);
  init_pair(kPanel, COLOR_BLUE, -1);
  init_pair(kInk, COLOR_WHITE, -1);
}

bool terminalIsTooSmall() { return COLS < 76 || LINES < 34; }

void drawMinimumSizeWarning() {
  erase();
  text(2, 2, "Waveiden needs a terminal at least 76 x 34. Press q to quit.",
       kAmber, A_BOLD);
  refresh();
}

int runTui(const std::string &databasePath) {
  Ui ui;
  try {
    ui.db.connect(databasePath);
  } catch (const std::exception &) {
    return 1;
  }
  configureTerminal();
  bool running = true;
  while (running) {
    if (terminalIsTooSmall()) {
      drawMinimumSizeWarning();
      if (getch() == 'q')
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      continue;
    }
    deck(ui);
    refresh();
    running = handleKey(ui, getch());
    ++ui.frame;
    std::this_thread::sleep_for(std::chrono::milliseconds(65));
  }
  endwin();
  ui.db.disconnect();
  return 0;
}
