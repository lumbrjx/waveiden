#include <waveiden.grpc.pb.h>
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Reads a file into a byte string for sending over the wire.
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    return {std::istreambuf_iterator<char>(f), {}};
}

class WaveidenClient {
public:
    explicit WaveidenClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(waveiden::WaveidenService::NewStub(channel)) {}

    void indexSong(const std::string& name, const std::string& wavPath) {
        waveiden::IndexSongRequest req;
        req.set_name(name);
        req.set_audio_data(readFile(wavPath));

        waveiden::IndexSongResponse resp;
        grpc::ClientContext ctx;
        grpc::Status status = stub_->IndexSong(&ctx, req, &resp);

        checkStatus(status);
        if (!resp.success())
            throw std::runtime_error("Server error: " + resp.error());
        std::cout << "Indexed: " << name
                  << "  (" << resp.fingerprint_count() << " fingerprints)\n";
    }

    void matchSong(const std::string& wavPath) {
        waveiden::MatchSongRequest req;
        req.set_audio_data(readFile(wavPath));

        waveiden::MatchSongResponse resp;
        grpc::ClientContext ctx;
        grpc::Status status = stub_->MatchSong(&ctx, req, &resp);

        checkStatus(status);
        if (!resp.error().empty())
            throw std::runtime_error("Server error: " + resp.error());

        if (resp.found()) {
            std::cout << "MATCH: "      << resp.song_name()
                      << "  confidence=" << resp.confidence()
                      << "  offset="     << resp.time_offset() << "s\n";
        } else {
            std::cout << "NO MATCH FOUND\n";
        }
    }

    void listSongs() {
        waveiden::ListSongsRequest req;
        waveiden::ListSongsResponse resp;
        grpc::ClientContext ctx;
        grpc::Status status = stub_->ListSongs(&ctx, req, &resp);

        checkStatus(status);
        if (resp.songs().empty()) {
            std::cout << "(no songs indexed)\n";
            return;
        }
        for (const auto& name : resp.songs())
            std::cout << "  " << name << "\n";
    }

    void removeSong(const std::string& name) {
        waveiden::RemoveSongRequest req;
        req.set_name(name);

        waveiden::RemoveSongResponse resp;
        grpc::ClientContext ctx;
        grpc::Status status = stub_->RemoveSong(&ctx, req, &resp);

        checkStatus(status);
        if (!resp.success())
            throw std::runtime_error("Server error: " + resp.error());
        std::cout << "Removed: " << name << "\n";
    }

    void clearDatabase() {
        waveiden::ClearDatabaseRequest req;
        waveiden::ClearDatabaseResponse resp;
        grpc::ClientContext ctx;
        grpc::Status status = stub_->ClearDatabase(&ctx, req, &resp);

        checkStatus(status);
        if (!resp.success())
            throw std::runtime_error("Server error: " + resp.error());
        std::cout << "Database cleared\n";
    }

private:
    std::unique_ptr<waveiden::WaveidenService::Stub> stub_;

    static void checkStatus(const grpc::Status& s) {
        if (!s.ok())
            throw std::runtime_error("RPC failed: " + s.error_message());
    }
};

static void usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [--server <host:port>] <mode> [args...]\n"
        << "\n"
        << "  --server <host:port>   Default: localhost:50051\n"
        << "\n"
        << "Modes:\n"
        << "  index  <name> <song.wav> [<name2> <song2.wav> ...]   Index songs\n"
        << "  match  <query.wav>                                    Match a clip\n"
        << "  list                                                  List indexed songs\n"
        << "  remove <name>                                         Remove a song\n"
        << "  clear                                                 Wipe database\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string serverAddr = "localhost:50051";
    int argStart = 1;

    if (std::string(argv[1]) == "--server") {
        if (argc < 4) { usage(argv[0]); return 1; }
        serverAddr = argv[2];
        argStart = 3;
    }

    const std::string mode = argv[argStart];

    WaveidenClient client(
        grpc::CreateChannel(serverAddr, grpc::InsecureChannelCredentials()));

    try {
        if (mode == "index") {
            // Pairs: <name> <path> <name> <path> ...
            if (argc < argStart + 3 || (argc - argStart - 1) % 2 != 0) {
                std::cerr << "index requires pairs of <name> <file.wav>\n";
                return 1;
            }
            for (int i = argStart + 1; i < argc; i += 2)
                client.indexSong(argv[i], argv[i + 1]);

        } else if (mode == "match") {
            if (argc < argStart + 2) { usage(argv[0]); return 1; }
            client.matchSong(argv[argStart + 1]);

        } else if (mode == "list") {
            client.listSongs();

        } else if (mode == "remove") {
            if (argc < argStart + 2) { usage(argv[0]); return 1; }
            client.removeSong(argv[argStart + 1]);

        } else if (mode == "clear") {
            client.clearDatabase();

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
