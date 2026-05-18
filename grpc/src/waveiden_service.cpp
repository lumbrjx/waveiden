#include "waveiden_service.hpp"
#include <grpcpp/server_builder.h>
#include <stdexcept>

namespace waveiden {
namespace grpc_ext {

WaveidenServiceImpl::WaveidenServiceImpl(IDatabase& db, FingerprintEngine engine)
    : db_(db), engine_(std::move(engine)) {}

::grpc::Status WaveidenServiceImpl::IndexSong(
    ::grpc::ServerContext*,
    const waveiden::IndexSongRequest* req,
    waveiden::IndexSongResponse* resp)
{
    try {
        const auto& raw = req->audio_data();
        AudioBuffer buf = reader_.readFromMemory(
            reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
        int count = engine_.indexBuffer(req->name(), buf, db_);
        resp->set_success(true);
        resp->set_fingerprint_count(count);
    } catch (const std::exception& e) {
        resp->set_success(false);
        resp->set_error(e.what());
    }
    return ::grpc::Status::OK;
}

::grpc::Status WaveidenServiceImpl::MatchSong(
    ::grpc::ServerContext*,
    const waveiden::MatchSongRequest* req,
    waveiden::MatchSongResponse* resp)
{
    try {
        const auto& raw = req->audio_data();
        AudioBuffer buf = reader_.readFromMemory(
            reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
        MatchResult result = engine_.matchBuffer(buf, db_);
        resp->set_found(result.found);
        resp->set_song_name(result.songName);
        resp->set_confidence(result.confidence);
        resp->set_time_offset(result.timeOffset);
    } catch (const std::exception& e) {
        resp->set_found(false);
        resp->set_error(e.what());
    }
    return ::grpc::Status::OK;
}

::grpc::Status WaveidenServiceImpl::ListSongs(
    ::grpc::ServerContext*,
    const waveiden::ListSongsRequest*,
    waveiden::ListSongsResponse* resp)
{
    try {
        for (const auto& name : db_.listSongs())
            resp->add_songs(name);
    } catch (const std::exception& e) {
        // ListSongs never throws in practice; log and continue
    }
    return ::grpc::Status::OK;
}

::grpc::Status WaveidenServiceImpl::RemoveSong(
    ::grpc::ServerContext*,
    const waveiden::RemoveSongRequest* req,
    waveiden::RemoveSongResponse* resp)
{
    try {
        db_.removeSong(req->name());
        resp->set_success(true);
    } catch (const std::exception& e) {
        resp->set_success(false);
        resp->set_error(e.what());
    }
    return ::grpc::Status::OK;
}

::grpc::Status WaveidenServiceImpl::ClearDatabase(
    ::grpc::ServerContext*,
    const waveiden::ClearDatabaseRequest*,
    waveiden::ClearDatabaseResponse* resp)
{
    try {
        db_.clear();
        resp->set_success(true);
    } catch (const std::exception& e) {
        resp->set_success(false);
        resp->set_error(e.what());
    }
    return ::grpc::Status::OK;
}

void runServer(const std::string& address, IDatabase& db,
               FingerprintEngine::Config config)
{
    WaveidenServiceImpl service(db, FingerprintEngine{config});
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    if (!server)
        throw std::runtime_error("Failed to start gRPC server on " + address);
    server->Wait();
}

} // namespace grpc_ext
} // namespace waveiden
