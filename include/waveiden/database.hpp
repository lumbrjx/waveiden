#pragma once
#include "types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace waveiden {

class IDatabase {
public:
    virtual ~IDatabase() = default;

    // Establish connection; uri semantics are implementation-defined.
    // SQLite: file path. Future impls: connection string, etc.
    virtual void connect(const std::string& uri) = 0;
    virtual void disconnect() = 0;

    virtual void indexSong(const std::string& name, const std::vector<Fingerprint>& prints) = 0;
    virtual MatchResult match(const std::vector<Fingerprint>& query) const = 0;
    virtual std::vector<std::string> listSongs() const = 0;
    virtual void removeSong(const std::string& name) = 0;
    virtual void clear() = 0;
    virtual bool isEmpty() const = 0;
};

// SQLite-backed implementation
class SQLiteDatabase : public IDatabase {
public:
    SQLiteDatabase();
    ~SQLiteDatabase() override;

    void connect(const std::string& uri) override;
    void disconnect() override;

    void indexSong(const std::string& name, const std::vector<Fingerprint>& prints) override;
    MatchResult match(const std::vector<Fingerprint>& query) const override;
    std::vector<std::string> listSongs() const override;
    void removeSong(const std::string& name) override;
    void clear() override;
    bool isEmpty() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} 
