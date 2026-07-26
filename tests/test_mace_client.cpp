#include "../MacEverything/CLI/MaceClient.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    expect(mace::urlEncode("中文 file.txt") == "%E4%B8%AD%E6%96%87%20file.txt",
           "URL encoding handles UTF-8 and spaces");

    const std::string search =
        R"({"results":[{"path":"/tmp/a b.txt"},{"path":"/tmp/quote\"name"}],)"
        R"("timing":{"searchPath":"trigram"}})";
    auto paths = mace::stringFieldValues(search, "path");
    expect(paths.size() == 2, "path parser ignores searchPath");
    expect(paths.size() > 1 && paths[1] == "/tmp/quote\"name", "path parser decodes escapes");

    const auto emojiPaths = mace::stringFieldValues(
        R"({"path":"/tmp/emoji-\uD83D\uDE00.txt"})", "path");
    expect(emojiPaths.size() == 1 && emojiPaths[0] == "/tmp/emoji-\xF0\x9F\x98\x80.txt",
           "path parser decodes JSON surrogate pairs");
    std::string invalidSurrogate;
    size_t invalidEnd = 0;
    expect(!mace::decodeJSONString(R"("\uD83D")", 0, invalidSurrogate, invalidEnd),
           "path parser rejects an unpaired high surrogate");

    const std::string adversarial =
        R"({"name":"a \"path\": \"fake\" value","path":"/tmp/real"})";
    auto adversarialPaths = mace::stringFieldValues(adversarial, "path");
    expect(adversarialPaths.size() == 1 && adversarialPaths[0] == "/tmp/real",
           "path-like text inside a value is ignored");

    const std::string content =
        R"({"results":[{"filePath":"/tmp/中文.txt","snippet":"line\ntext"}]})";
    auto contentPaths = mace::stringFieldValues(content, "filePath");
    expect(contentPaths.size() == 1 && contentPaths[0] == "/tmp/中文.txt",
           "content path parser preserves UTF-8");

    uint64_t count = 0;
    bool pending = false;
    expect(mace::integerField(R"({"recordCount":123})", "recordCount", count) && count == 123,
           "integer status field");
    expect(mace::boolField(R"({"phase2Pending":true})", "phase2Pending", pending) && pending,
           "boolean status field");

    mace::HttpResponse response;
    std::string error;
    expect(mace::parseHttpResponse(
               "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", response, error),
           "HTTP response parser accepts valid response");
    expect(response.status == 200 && response.body == "{}", "HTTP response fields");
    expect(!mace::parseHttpResponse("broken", response, error), "invalid HTTP response rejected");

    if (failures != 0) return 1;
    std::cout << "mace client tests passed\n";
    return 0;
}
