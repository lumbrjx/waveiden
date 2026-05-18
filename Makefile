BUILD_DIR   := build
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release -Wno-dev
CLI         := $(BUILD_DIR)/waveiden_cli
SERVER      := $(BUILD_DIR)/waveiden_grpc_server
CLIENT      := $(BUILD_DIR)/waveiden_grpc_client

.PHONY: all configure build clean rebuild install test help grpc server client

all: build

# ── Build ─────────────────────────────────────────────────────────────────────

configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) --parallel $$(nproc)

rebuild: clean build

clean:
	rm -rf $(BUILD_DIR)

install: build
	cmake --install $(BUILD_DIR)

# Build with gRPC extension (requires gRPC C++ installed on the system)
grpc:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS) -DWAVEIDEN_ENABLE_GRPC=ON
	cmake --build $(BUILD_DIR) --parallel $$(nproc)

# ── CLI shortcuts ─────────────────────────────────────────────────────────────

DB     ?= fingerprint.db
SERVER_ADDR ?= localhost:50051

# make index DB=my.db FILES="song1.wav song2.wav"
index: build
	$(CLI) index $(DB) $(FILES)

# make match DB=my.db QUERY=clip.wav
match: build
	$(CLI) match $(DB) $(QUERY)

# make list DB=my.db
list: build
	$(CLI) list $(DB)

# make remove DB=my.db NAME="song1.wav"
remove: build
	$(CLI) remove $(DB) $(NAME)

# make clear DB=my.db
clear: build
	$(CLI) clear $(DB)

# ── gRPC server/client shortcuts ──────────────────────────────────────────────

# Start the gRPC server: make server DB=library.db
server: grpc
	$(SERVER) --listen 0.0.0.0:50051 --db $(DB)

# make rpc-index SERVER_ADDR=localhost:50051 FILES="MySong song.wav"
rpc-index: grpc
	$(CLIENT) --server $(SERVER_ADDR) index $(FILES)

# make rpc-match SERVER_ADDR=localhost:50051 QUERY=clip.wav
rpc-match: grpc
	$(CLIENT) --server $(SERVER_ADDR) match $(QUERY)

# make rpc-list SERVER_ADDR=localhost:50051
rpc-list: grpc
	$(CLIENT) --server $(SERVER_ADDR) list

# make rpc-remove SERVER_ADDR=localhost:50051 NAME="MySong"
rpc-remove: grpc
	$(CLIENT) --server $(SERVER_ADDR) remove $(NAME)

# make rpc-clear SERVER_ADDR=localhost:50051
rpc-clear: grpc
	$(CLIENT) --server $(SERVER_ADDR) clear

# ── Smoke test (uses the sample WAVs if present) ──────────────────────────────

test: build
	@echo "=== Indexing full_song1.wav and full_song2.wav ==="
	$(CLI) index /tmp/waveiden_test.db full_song1.wav full_song2.wav
	@echo "=== Listing indexed songs ==="
	$(CLI) list /tmp/waveiden_test.db
	@echo "=== Matching half.wav ==="
	$(CLI) match /tmp/waveiden_test.db half.wav
	@echo "=== Matching middle5.wav ==="
	$(CLI) match /tmp/waveiden_test.db middle5.wav
	@echo "=== Cleanup ==="
	$(CLI) clear /tmp/waveiden_test.db
	@rm -f /tmp/waveiden_test.db

help:
	@echo "Targets:"
	@echo "  make build               Build the library and CLI (default)"
	@echo "  make rebuild             Clean then build"
	@echo "  make clean               Remove build directory"
	@echo "  make install             Install library and headers (may need sudo)"
	@echo "  make grpc                Build with gRPC extension (server + client)"
	@echo "  make test                Run a smoke test against the sample WAV files"
	@echo ""
	@echo "Local CLI (DB defaults to fingerprint.db):"
	@echo "  make index  DB=<db> FILES=\"a.wav b.wav\""
	@echo "  make match  DB=<db> QUERY=clip.wav"
	@echo "  make list   DB=<db>"
	@echo "  make remove DB=<db> NAME=song.wav"
	@echo "  make clear  DB=<db>"
	@echo ""
	@echo "gRPC (SERVER_ADDR defaults to localhost:50051):"
	@echo "  make server              Start the gRPC server"
	@echo "  make rpc-index  FILES=\"MySong song.wav MySong2 song2.wav\""
	@echo "  make rpc-match  QUERY=clip.wav"
	@echo "  make rpc-list"
	@echo "  make rpc-remove NAME=MySong"
	@echo "  make rpc-clear"
