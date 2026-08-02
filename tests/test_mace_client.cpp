#include "../MacEverything/CLI/MaceClient.h"

#include <iostream>
#include <string>
#include <thread>

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

    // A peer that streams an oversized response must be cut off at the client limit.
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    expect(listener >= 0, "oversized response test creates listener");
    if (listener >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bool bound = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        expect(bound, "oversized response test binds listener");
        if (!bound) {
            ::close(listener);
        } else {
            socklen_t addressLength = sizeof(address);
            ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength);
            ::listen(listener, 1);

            std::thread server([listener] {
                int client = ::accept(listener, nullptr, nullptr);
                if (client >= 0) {
                    char request[4096];
                    (void)::recv(client, request, sizeof(request), 0);
                    const std::string header =
                        "HTTP/1.1 200 OK\r\nContent-Length: 20971520\r\n\r\n";
                    (void)::send(client, header.data(), header.size(), MSG_NOSIGNAL);
                    std::string chunk(64 * 1024, 'x');
                    for (int i = 0; i < 321; ++i) {
                        if (::send(client, chunk.data(), chunk.size(), MSG_NOSIGNAL) <= 0) break;
                    }
                    ::close(client);
                }
                ::close(listener);
            });

            mace::HttpResponse oversizedResponse;
            std::string oversizedError;
            bool oversizedOK = mace::httpGet(ntohs(address.sin_port), "/oversized",
                                              oversizedResponse, oversizedError);
            expect(!oversizedOK && oversizedError.find("16 MB") != std::string::npos,
                   "HTTP client rejects oversized streamed response");
            server.join();
        }
    }

    if (failures != 0) return 1;
    std::cout << "mace client tests passed\n";
    return 0;
}
