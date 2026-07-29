#include "waveiden/database.hpp"
#include <sqlite3.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace waveiden {

// A single landmark collision is not evidence of the same recording. Require
// a meaningful cluster of hashes at one consistent time offset before naming a
// match; this prevents a one-song library from becoming a default answer.
static constexpr int kMinimumConsistentHits = 20;

struct SQLiteDatabase::Impl {
    sqlite3* db = nullptr;

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("SQLite: " + msg);
        }
    }

    void initSchema() {
        exec(R"(
            CREATE TABLE IF NOT EXISTS songs (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT UNIQUE NOT NULL
            );
            CREATE TABLE IF NOT EXISTS fingerprints (
                hash        INTEGER NOT NULL,
                time_offset INTEGER NOT NULL,
                song_id     INTEGER NOT NULL,
                FOREIGN KEY(song_id) REFERENCES songs(id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS idx_fp_hash ON fingerprints(hash);
            PRAGMA foreign_keys = ON;
            PRAGMA journal_mode = WAL;
        )");
    }
};

SQLiteDatabase::SQLiteDatabase() : impl_(std::make_unique<Impl>()) {}

SQLiteDatabase::~SQLiteDatabase() {
    disconnect();
}

void SQLiteDatabase::connect(const std::string& uri) {
    if (impl_->db)
        throw std::runtime_error("Already connected");
    if (sqlite3_open(uri.c_str(), &impl_->db) != SQLITE_OK)
        throw std::runtime_error("Cannot open database: " + uri);
    impl_->initSchema();
}

void SQLiteDatabase::disconnect() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

void SQLiteDatabase::indexSong(const std::string& name, const std::vector<Fingerprint>& prints) {
    impl_->exec("BEGIN");
    try {
        // Upsert song
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(impl_->db,
            "INSERT OR IGNORE INTO songs(name) VALUES(?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Get song id
        sqlite3_prepare_v2(impl_->db,
            "SELECT id FROM songs WHERE name = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        int64_t songId = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);

        // Remove old fingerprints for this song
        sqlite3_prepare_v2(impl_->db,
            "DELETE FROM fingerprints WHERE song_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, songId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Bulk-insert fingerprints
        sqlite3_prepare_v2(impl_->db,
            "INSERT INTO fingerprints(hash, time_offset, song_id) VALUES(?,?,?)",
            -1, &stmt, nullptr);
        for (const auto& fp : prints) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(fp.hash));
            sqlite3_bind_int(stmt, 2, fp.timeOffset);
            sqlite3_bind_int64(stmt, 3, songId);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);

        impl_->exec("COMMIT");
    } catch (...) {
        impl_->exec("ROLLBACK");
        throw;
    }
}

MatchResult SQLiteDatabase::match(const std::vector<Fingerprint>& query) const {
    // hash → [(song_name, song_time)]
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "SELECT s.name, f.time_offset "
        "FROM fingerprints f "
        "JOIN songs s ON s.id = f.song_id "
        "WHERE f.hash = ?",
        -1, &stmt, nullptr);

    // song → {timeDelta → count}
    std::map<std::string, std::map<int, int>> deltaCounts;
    int totalHits = 0;

    for (const auto& qp : query) {
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(qp.hash));
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string songName(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            int songTime = sqlite3_column_int(stmt, 1);
            deltaCounts[songName][songTime - qp.timeOffset]++;
            totalHits++;
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    MatchResult result;
    for (const auto& [song, deltas] : deltaCounts) {
        for (const auto& [delta, count] : deltas) {
            if (count > result.confidence) {
                result.confidence = count;
                result.songName   = song;
                result.timeOffset = delta * 512.0 / 44100.0;
                result.found      = true;
            }
        }
    }
    if (result.confidence < kMinimumConsistentHits)
        return {};
    return result;
}

std::vector<std::string> SQLiteDatabase::listSongs() const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db, "SELECT name FROM songs ORDER BY name", -1, &stmt, nullptr);
    std::vector<std::string> songs;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        songs.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return songs;
}

void SQLiteDatabase::removeSong(const std::string& name) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db, "DELETE FROM songs WHERE name = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void SQLiteDatabase::clear() {
    impl_->exec("DELETE FROM fingerprints; DELETE FROM songs;");
}

bool SQLiteDatabase::isEmpty() const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db, "SELECT COUNT(*) FROM songs", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count == 0;
}

} 
