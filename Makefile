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
.PHONY: test test-fast lint-localizations lint-docs test-mace-client test-swift-cli-install test-swift-interaction test-swift-mcp test-swift-export test-swift-highlight test-swift-throttle test-swift-l10n test-slow test-all test-asan test-tsan build clean app dmg cli benchmark-trigram-simd benchmark-posting-simd help

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

mace: MacEverything/CLI/mace_main.cpp MacEverything/CLI/MaceClient.h
	$(CXX) $(CXXFLAGS) -framework CoreFoundation MacEverything/CLI/mace_main.cpp -o $@

cli: mace

# === Lint targets ===
lint-bridge:
	$(CXX) $(CXXFLAGS) -fsyntax-only -fobjc-arc -x objective-c++ \
		-IMacEverything/Core -IMacEverything/Bridge \
		MacEverything/Bridge/MacSearchBridge.mm \
		MacEverything/Bridge/MacSearchBridge+Content.mm

lint-localizations:
	bash scripts/check-localizations.sh

lint-docs:
	bash scripts/check-docs.sh

# === Sanitizer targets ===
test-asan: test_all.cpp $(CORE_SRCS)
	mkdir -p build/tests
	$(CXX) -std=c++20 -O1 -g -fsanitize=address -fno-omit-frame-pointer $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) -IMacEverything/Core $^ -o build/tests/test_all_asan
	./build/tests/test_all_asan --fast --quiet

test-tsan: test_all.cpp $(CORE_SRCS)
	mkdir -p build/tests
	$(CXX) -std=c++20 -O1 -g -fsanitize=thread $(RE2_CFLAGS) $(FRAMEWORKS) $(RE2_LDFLAGS) -IMacEverything/Core $^ -o build/tests/test_all_tsan
	./build/tests/test_all_tsan --fast --quiet

# === Test targets ===
test: test-fast

test-fast: test_all lint-bridge lint-localizations lint-docs test-mace-client test-swift-cli-install test-swift-interaction test-swift-mcp test-swift-export test-swift-highlight test-swift-throttle test-swift-l10n
	./test_all --fast --quiet

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

test-swift-export:
	mkdir -p build/tests
	xcrun swiftc MacEverything/App/SearchExportSerializer.swift tests/test_search_export.swift \
		-o build/tests/test_search_export
	./build/tests/test_search_export

test-swift-highlight:
	mkdir -p build/tests
	xcrun swiftc -DTESTING -framework SwiftUI \
		MacEverything/App/HighlightHint.swift MacEverything/App/TextHighlight.swift tests/test_highlight_ranges.swift \
		-o build/tests/test_highlight_ranges
	./build/tests/test_highlight_ranges

test-swift-throttle:
	mkdir -p build/tests
	xcrun swiftc MacEverything/App/IndexRefreshThrottle.swift tests/test_index_refresh_throttle.swift \
		-o build/tests/test_index_refresh_throttle
	./build/tests/test_index_refresh_throttle

test-swift-l10n:
	mkdir -p build/tests
	xcrun swiftc MacEverything/App/L10n.swift tests/test_l10n_formatting.swift \
		-o build/tests/test_l10n_formatting
	./build/tests/test_l10n_formatting

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
	rm -f test_all test_all_asan test_all_tsan benchmark mace
	rm -rf build/

# === Help ===
help:
	@echo "Available targets:"
	@echo "  make test       - Run fast unit tests (alias for test-fast)"
	@echo "  make test-fast  - Run local fast tests, skipping benchmarks/stress tests"
	@echo "  make test-slow  - Run slow integration tests (Part 1, 4, 6)"
	@echo "  make test-all   - Run all tests"
	@echo "  make test-asan  - Run fast tests with AddressSanitizer"
	@echo "  make test-tsan  - Run fast tests with ThreadSanitizer"
	@echo "  make app        - Build MacEverything.app via Xcode (requires prepared RE2 dependencies)"
	@echo "  make dmg        - Build + package into DMG"
	@echo "  make cli        - Build the short query client (mace)"
	@echo "  make benchmark  - Build benchmark binary"
	@echo "  make benchmark-trigram-simd - Compare scalar, auto-vectorized, and NEON trigram packing"
	@echo "  make benchmark-posting-simd - Compare scalar and NEON posting-list intersection"
	@echo "  make clean      - Remove build artifacts"
