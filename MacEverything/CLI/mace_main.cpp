#include "MaceClient.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

enum class Mode { Search, Content, Recent, Status };

struct Options {
    Mode mode = Mode::Search;
    uint16_t port = 19860;
    uint32_t limit = 100;
    bool json = false;
    bool nullSeparated = false;
    std::vector<std::string> queryParts;
};

void usage(std::ostream& output) {
    output <<
        "Usage: mace [options] QUERY...\n"
        "       mace --recent [options]\n"
        "       mace --status [options]\n\n"
        "Search the index of the running MacEverything app.\n\n"
        "Options:\n"
        "  -c, --content      Search indexed file contents\n"
        "  -r, --recent       List recently modified files\n"
        "  -s, --status       Show index status\n"
        "  -n, --limit N      Maximum results (default 100, max 10000)\n"
        "  -p, --port PORT    HTTP API port (default 19860)\n"
        "  -j, --json         Print the raw JSON response\n"
        "  -0, --null         Separate result paths with NUL bytes\n"
        "  -h, --help         Show this help\n";
}

bool parsePositiveInteger(const std::string& text, uint64_t maximum, uint64_t& value) {
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > maximum) return false;
    }
    return value > 0;
}

bool parseOptions(int argc, char* argv[], Options& options) {
    bool positionalOnly = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (!positionalOnly && argument == "--") {
            positionalOnly = true;
        } else if (!positionalOnly && (argument == "-h" || argument == "--help")) {
            usage(std::cout);
            std::exit(0);
        } else if (!positionalOnly && (argument == "-c" || argument == "--content")) {
            options.mode = Mode::Content;
        } else if (!positionalOnly && (argument == "-r" || argument == "--recent")) {
            options.mode = Mode::Recent;
        } else if (!positionalOnly && (argument == "-s" || argument == "--status")) {
            options.mode = Mode::Status;
        } else if (!positionalOnly && (argument == "-j" || argument == "--json")) {
            options.json = true;
        } else if (!positionalOnly && (argument == "-0" || argument == "--null")) {
            options.nullSeparated = true;
        } else if (!positionalOnly && (argument == "-n" || argument == "--limit" ||
                                       argument == "-p" || argument == "--port")) {
            if (++i >= argc) {
                std::cerr << "mace: missing value for " << argument << '\n';
                return false;
            }
            uint64_t value = 0;
            uint64_t maximum = (argument == "-p" || argument == "--port") ? 65535 : 10000;
            if (!parsePositiveInteger(argv[i], maximum, value)) {
                std::cerr << "mace: value for " << argument << " must be between 1 and "
                          << maximum << ": " << argv[i] << '\n';
                return false;
            }
            if (argument == "-p" || argument == "--port") {
                options.port = static_cast<uint16_t>(value);
            } else {
                options.limit = static_cast<uint32_t>(value);
            }
        } else if (!positionalOnly && !argument.empty() && argument[0] == '-') {
            std::cerr << "mace: unknown option: " << argument << '\n';
            return false;
        } else {
            options.queryParts.push_back(std::move(argument));
        }
    }
    if ((options.mode == Mode::Search || options.mode == Mode::Content) &&
        options.queryParts.empty()) {
        std::cerr << "mace: a search query is required\n";
        return false;
    }
    if ((options.mode == Mode::Recent || options.mode == Mode::Status) &&
        !options.queryParts.empty()) {
        std::cerr << "mace: this mode does not accept a search query\n";
        return false;
    }
    if (options.json && options.nullSeparated) {
        std::cerr << "mace: --json and --null cannot be used together\n";
        return false;
    }
    return true;
}

std::string joinedQuery(const std::vector<std::string>& parts) {
    std::string query;
    for (const auto& part : parts) {
        if (!query.empty()) query.push_back(' ');
        query += part;
    }
    return query;
}

std::string endpoint(const Options& options) {
    switch (options.mode) {
        case Mode::Search:
            return "/api/search?q=" + mace::urlEncode(joinedQuery(options.queryParts)) +
                "&limit=" + std::to_string(options.limit);
        case Mode::Content:
            return "/api/search/content?q=" + mace::urlEncode(joinedQuery(options.queryParts)) +
                "&limit=" + std::to_string(options.limit);
        case Mode::Recent:
            return "/api/recent?limit=" + std::to_string(options.limit);
        case Mode::Status:
            return "/api/status";
    }
}

void printStatus(const std::string& json) {
    uint64_t records = 0;
    uint64_t liveRecords = 0;
    uint64_t contentFiles = 0;
    bool phase2Pending = false;
    mace::integerField(json, "recordCount", records);
    mace::integerField(json, "liveRecordCount", liveRecords);
    mace::integerField(json, "contentIndexedFileCount", contentFiles);
    mace::boolField(json, "phase2Pending", phase2Pending);
    std::cout << "records\t" << records << '\n'
              << "live\t" << liveRecords << '\n'
              << "content\t" << contentFiles << '\n'
              << "phase2\t" << (phase2Pending ? "pending" : "ready") << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        usage(std::cerr);
        return 2;
    }

    mace::HttpResponse response;
    std::string error;
    if (!mace::httpGet(options.port, endpoint(options), response, error)) {
        std::cerr << "mace: " << error << '\n';
        return 3;
    }
    if (response.status < 200 || response.status >= 300) {
        std::cerr << "mace: server returned HTTP " << response.status;
        if (!response.body.empty()) std::cerr << ": " << response.body;
        std::cerr << '\n';
        return 4;
    }
    if (options.json) {
        std::cout << response.body << '\n';
        return 0;
    }
    if (options.mode == Mode::Status) {
        printStatus(response.body);
        return 0;
    }

    const char* field = options.mode == Mode::Content ? "filePath" : "path";
    for (const auto& path : mace::stringFieldValues(response.body, field)) {
        std::cout << path;
        std::cout.put(options.nullSeparated ? '\0' : '\n');
    }
    return 0;
}
