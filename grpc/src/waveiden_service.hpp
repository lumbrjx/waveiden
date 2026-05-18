#pragma once
#include <waveiden/waveiden.hpp>
#include "waveiden.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace waveiden {
namespace grpc_ext {

// gRPC service implementation.
// Owns (or holds a reference to) an IDatabase and a FingerprintEngine.
// Construct with a connected IDatabase instance; the caller is responsible
// for keeping the database alive as long as the server runs.
class WaveidenServiceImpl final : public waveiden::WaveidenService::Service {
public:
    WaveidenServiceImpl(
        IDatabase& db,
        FingerprintEngine engine = FingerprintEngine{});

    ::grpc::Status IndexSong(
        ::grpc::ServerContext* ctx,
        const waveiden::IndexSongRequest* req,
        waveiden::IndexSongResponse* resp) override;

    ::grpc::Status MatchSong(
        ::grpc::ServerContext* ctx,
        const waveiden::MatchSongRequest* req,
        waveiden::MatchSongResponse* resp) override;

    ::grpc::Status ListSongs(
        ::grpc::ServerContext* ctx,
        const waveiden::ListSongsRequest* req,
        waveiden::ListSongsResponse* resp) override;

    ::grpc::Status RemoveSong(
        ::grpc::ServerContext* ctx,
        const waveiden::RemoveSongRequest* req,
        waveiden::RemoveSongResponse* resp) override;

    ::grpc::Status ClearDatabase(
        ::grpc::ServerContext* ctx,
        const waveiden::ClearDatabaseRequest* req,
        waveiden::ClearDatabaseResponse* resp) override;

private:
    IDatabase& db_;
    FingerprintEngine engine_;
    WavReader reader_;
};

// Helper: build and start the gRPC server on `address` (e.g. "0.0.0.0:50051").
// Blocks until the server shuts down.
void runServer(const std::string& address, IDatabase& db,
               FingerprintEngine::Config config = {});

} // namespace grpc_ext
} // namespace waveiden
