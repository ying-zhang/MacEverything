CXX = clang++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra
FRAMEWORKS = -framework CoreServices
CORE_SRCS = $(wildcard MacEverything/Core/*.cpp)
CORE_HEADERS = $(wildcard MacEverything/Core/*.h)
TEST_HEADERS = $(wildcard tests/*.h)
HOMEBREW_PREFIX ?= /opt/homebrew
RE2_PREFIX ?= $(CURDIR)/third_party/re2
RE2_CFLAGS = -I$(RE2_PREFIX)/include
RE2_LDFLAGS = -L$(RE2_PREFIX)/lib -Wl,-rpath,$(RE2_PREFIX)/lib -lre2

# === Build targets ===
.PHONY: test test-fast test-mace-client test-swift-cli-install test-swift-interaction test-swift-mcp test-slow test-all test-asan test-tsan build clean app dmg daemon cli benchmark-trigram-simd benchmark-posting-simd help

test_all: test_all.cpp $(CORE_SRCS) $(CORE_HEADERS) $(TEST_HEADERS)
	$(CXX) $(CXXFLAGS) $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) \
		-IMacEverything/Core test_all.cpp $(CORE_SRCS) -o $@

benchmark: benchmark.cpp $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) $^ -o $@

benchmark-trigram-simd: benchmarks/bench_trigram_simd.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -IMacEverything/Core $^ -o build/benchmark-trigram-simd
	./build/benchmark-trigram-simd

benchmark-posting-simd: benchmarks/bench_posting_intersection.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -IMacEverything/Core $^ -o build/benchmark-posting-simd
	./build/benchmark-posting-simd

maceverything-daemon: MacEverything/CLI/daemon_main.cpp $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) -IMacEverything/Core $^ -o $@

daemon: maceverything-daemon

mace: MacEverything/CLI/mace_main.cpp MacEverything/CLI/MaceClient.h
	$(CXX) $(CXXFLAGS) MacEverything/CLI/mace_main.cpp -o $@

cli: mace

# === Lint targets ===
lint-bridge:
	$(CXX) $(CXXFLAGS) -fsyntax-only -fobjc-arc -x objective-c++ \
		-IMacEverything/Core -IMacEverything/Bridge \
		MacEverything/Bridge/MacSearchBridge.mm \
		MacEverything/Bridge/MacSearchBridge+Content.mm

# === Sanitizer targets ===
test-asan: test_all.cpp $(CORE_SRCS)
	$(CXX) -std=c++20 -O1 -g -fsanitize=address -fno-omit-frame-pointer $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) -IMacEverything/Core $^ -o test_all_asan
	./test_all_asan --fast

test-tsan: test_all.cpp $(CORE_SRCS)
	$(CXX) -std=c++20 -O1 -g -fsanitize=thread $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) -IMacEverything/Core $^ -o test_all_tsan
	./test_all_tsan --fast

# === Test targets ===
test: test-fast

test-fast: test_all lint-bridge test-mace-client test-swift-cli-install test-swift-interaction test-swift-mcp
	./test_all --fast

test-mace-client:
	mkdir -p build/tests
	$(CXX) $(CXXFLAGS) tests/test_mace_client.cpp -o build/tests/test_mace_client
	./build/tests/test_mace_client

test-swift-cli-install:
	mkdir -p build/tests
	xcrun swiftc MacEverything/App/CLIInstallManager.swift tests/test_cli_install.swift \
		-o build/tests/test_cli_install
	./build/tests/test_cli_install

test-swift-interaction:
	mkdir -p build/tests
	xcrun swiftc -framework AppKit -framework SwiftUI \
		MacEverything/App/ResultInteraction.swift tests/test_result_interaction.swift \
		-o build/tests/test_result_interaction
	./build/tests/test_result_interaction

test-swift-mcp:
	mkdir -p build/tests
	xcrun swiftc MacEverything/App/MCPConfigManager.swift tests/test_mcp_config.swift \
		-o build/tests/test_mcp_config
	./build/tests/test_mcp_config

test-slow: test_all
	./test_all --slow

test-all: test_all
	./test_all

# === Xcode build ===
app:
	xcodebuild -scheme MacEverything -configuration Release build

# === Package ===
dmg:
	scripts/build-release-dmgs.sh arm64

# === Cleanup ===
clean:
	rm -f test_all test_all_asan test_all_tsan benchmark maceverything-daemon mace
	rm -rf build/

# === Help ===
help:
	@echo "Available targets:"
	@echo "  make test       - Run fast unit tests (alias for test-fast)"
	@echo "  make test-fast  - Run local fast tests, skipping benchmarks/stress tests"
	@echo "  make test-slow  - Run slow integration tests (Part 1, 4, 6)"
	@echo "  make test-all   - Run all tests"
	@echo "  make app        - Build MacEverything.app via Xcode (requires prepared RE2 dependencies)"
	@echo "  make dmg        - Build + package into DMG"
	@echo "  make daemon     - Build CLI daemon (maceverything-daemon)"
	@echo "  make cli        - Build the short query client (mace)"
	@echo "  make benchmark  - Build benchmark binary"
	@echo "  make benchmark-trigram-simd - Compare scalar, auto-vectorized, and NEON trigram packing"
	@echo "  make benchmark-posting-simd - Compare scalar and NEON posting-list intersection"
	@echo "  make clean      - Remove build artifacts"
