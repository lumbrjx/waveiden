#include <waveiden/waveiden.hpp>
#include <iostream>
#include <stdexcept>
#ifdef WAVEIDEN_HAS_TUI
#include "tui.hpp"
#endif

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <mode> [args...]\n"
              << "Modes:\n"
              << "  index <db_path> <song.wav> [song2.wav ...]  Index songs\n"
              << "  match <db_path> <query.wav>                  Match a clip\n"
              << "  list  <db_path>                              List indexed songs\n"
              << "  remove <db_path> <name>                      Remove a song\n"
              << "  clear <db_path>                              Wipe database\n"
              << "  tui    <db_path>                              Open animated dashboard\n";
}

int main(int argc, char* argv[]) {
    // The dashboard is Waveiden's primary experience; command modes remain
    // available for scripting and automation.
    if (argc == 1) {
#ifdef WAVEIDEN_HAS_TUI
        return runTui("fingerprint.db");
#else
        std::cerr << "This build was configured with WAVEIDEN_BUILD_TUI=OFF\n";
        return 1;
#endif
    }
    if (argc >= 2 && std::string(argv[1]) == "tui") {
#ifdef WAVEIDEN_HAS_TUI
        return runTui(argc >= 3 ? argv[2] : "fingerprint.db");
#else
        std::cerr << "This build was configured with WAVEIDEN_BUILD_TUI=OFF\n";
        return 1;
#endif
    }
    if (argc < 3) { usage(argv[0]); return 1; }

    const std::string mode   = argv[1];
    const std::string dbPath = argv[2];

    waveiden::SQLiteDatabase db;
    waveiden::FingerprintEngine engine;

    try {
        db.connect(dbPath);
    } catch (const std::exception& e) {
        std::cerr << "DB error: " << e.what() << "\n";
        return 1;
    }

    try {
        if (mode == "index") {
            if (argc < 4) { usage(argv[0]); return 1; }
            for (int i = 3; i < argc; i++) {
                std::cout << "Indexing: " << argv[i] << "\n";
                int n = engine.indexFile(argv[i], db);
                std::cout << "  -> " << n << " fingerprints stored\n";
            }

        } else if (mode == "match") {
            if (argc != 4) { usage(argv[0]); return 1; }
            if (db.isEmpty()) { std::cerr << "Database is empty\n"; return 1; }

            auto result = engine.matchFile(argv[3], db);
            if (result.found) {
                std::cout << "MATCH: " << result.songName
                          << "  confidence=" << result.confidence
                          << "  offset=" << result.timeOffset << "s\n";
            } else {
                std::cout << "NO MATCH FOUND\n";
            }

        } else if (mode == "list") {
            for (const auto& name : db.listSongs())
                std::cout << "  " << name << "\n";

        } else if (mode == "remove") {
            if (argc != 4) { usage(argv[0]); return 1; }
            db.removeSong(argv[3]);
            std::cout << "Removed: " << argv[3] << "\n";

        } else if (mode == "clear") {
            db.clear();
            std::cout << "Database cleared\n";

        } else {
            std::cerr << "Unknown mode: " << mode << "\n";
            usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
