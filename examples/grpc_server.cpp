#include <waveiden/waveiden.hpp>
#include <waveiden_service.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0:50051";
    std::string dbPath  = "fingerprint.db";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) address = argv[++i];
        else if (arg == "--db"     && i + 1 < argc) dbPath  = argv[++i];
        else {
            std::cerr << "Usage: " << argv[0]
                      << " [--listen <addr:port>] [--db <path>]\n"
                      << "  --listen  Address to bind  (default: 0.0.0.0:50051)\n"
                      << "  --db      SQLite DB path   (default: fingerprint.db)\n";
            return 1;
        }
    }

    waveiden::SQLiteDatabase db;
    try {
        db.connect(dbPath);
    } catch (const std::exception& e) {
        std::cerr << "DB error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "waveiden gRPC server\n"
              << "  listening on : " << address << "\n"
              << "  database     : " << dbPath  << "\n";

    waveiden::grpc_ext::runServer(address, db);
    return 0;
}
