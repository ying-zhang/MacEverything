#pragma once
// Part 39: HttpServer engine swap test
// Verifies that HttpServer uses getter functions to always fetch the latest
// engine/contentIndex, so that swapping the underlying engine is transparent.

// All Core headers included by test_all.cpp
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>

static std::string httpGet(uint16_t port, const std::string& path) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n"
        "Authorization: Bearer test-token\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    ::close(fd);
    return response;
}

static std::string httpPost(uint16_t port, const std::string& path, const std::string& body) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "";
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }
    std::string req = "POST " + path + " HTTP/1.1\r\nHost: localhost\r\n"
        "Authorization: Bearer test-token\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    ::send(fd, req.data(), req.size(), 0);
    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) response.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return response;
}

static bool runPart39() {
    std::cout << "\n=== Part 39: HttpServer engine swap ===\n";
    // Create two engines with different data
    auto engine1 = std::make_shared<SearchEngine>();
    { FileRecord rec; rec.name = "file1.txt"; rec.path = "/tmp"; rec.type = 1; rec.size = 100; rec.modTime = time(nullptr); engine1->addRecord(std::move(rec)); }

    auto engine2 = std::make_shared<SearchEngine>();
    { FileRecord rec; rec.name = "file2.txt"; rec.path = "/tmp"; rec.type = 1; rec.size = 200; rec.modTime = time(nullptr); engine2->addRecord(std::move(rec)); }
    { FileRecord rec; rec.name = "other.txt"; rec.path = "/tmp"; rec.type = 1; rec.size = 300; rec.modTime = time(nullptr); engine2->addRecord(std::move(rec)); }

    // Shared pointer that can be swapped
    auto currentEngine = std::make_shared<std::shared_ptr<SearchEngine>>(engine1);
    auto contentIndex = std::make_shared<ContentIndex>();

    HttpServer server;
    uint16_t port = 19870;
    server.setAuthToken("test-token");
    server.setAuthenticationRequired(true);

    // Start with getter functions
    server.start(port,
        [currentEngine]() -> std::shared_ptr<SearchEngine> { return *currentEngine; },
        [contentIndex]() -> std::shared_ptr<ContentIndex> { return contentIndex; });
    HttpServer::AdminCallbacks callbacks;
    callbacks.onSetContentConfig = [](const std::vector<std::string>&, uint64_t) {};
    callbacks.onGetContentExtensions = [] { return std::vector<std::string>{"txt"}; };
    callbacks.onGetContentMaxFileSize = [] { return uint64_t{1024}; };
    server.setAdminCallbacks(std::move(callbacks));

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Test 1: Query should use engine1
    {
        auto resp = httpGet(port, "/api/status");
        check(resp.find("\"recordCount\":1") != std::string::npos,
              "Status with engine1 shows 1 record");
    }

    // Test 2: Swap engine, same server should reflect new engine
    *currentEngine = engine2;
    {
        auto resp = httpGet(port, "/api/status");
        check(resp.find("\"recordCount\":2") != std::string::npos,
              "Status after engine swap shows 2 records");
    }

    // Test 3: Search should use new engine
    {
        auto resp = httpGet(port, "/api/search?q=file2");
        check(resp.find("file2.txt") != std::string::npos,
              "Search on swapped engine finds file2.txt");
    }

    // Test 4: Health endpoint always works (no engine needed)
    {
        auto resp = httpGet(port, "/api/health");
        check(resp.find("\"status\":\"ok\"") != std::string::npos,
              "Health endpoint returns ok status");
    }

    // Oversized numbers must produce a client error instead of throwing.
    {
        auto resp = httpPost(port, "/api/content/config",
            "{\"maxFileSize\":999999999999999999999999999999999999}");
        check(resp.find("400 Bad Request") != std::string::npos,
              "Oversized content config number returns HTTP 400");
    }

    // Completed connections are handled by the fixed worker pool and stop remains prompt.
    bool allHealthRequestsSucceeded = true;
    for (int i = 0; i < 100; ++i) {
        auto resp = httpGet(port, "/api/health");
        if (resp.empty()) allHealthRequestsSucceeded = false;
    }
    check(allHealthRequestsSucceeded, "Worker pool serves sequential health requests");

    // A connected client that sends no request bytes must not hold stop() until
    // the five-second receive timeout expires.
    int slowFd = ::socket(AF_INET, SOCK_STREAM, 0);
    bool slowConnected = false;
    if (slowFd >= 0) {
        struct sockaddr_in slowAddr{};
        slowAddr.sin_family = AF_INET;
        slowAddr.sin_port = htons(port);
        slowAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        slowConnected = ::connect(slowFd,
            reinterpret_cast<struct sockaddr*>(&slowAddr), sizeof(slowAddr)) == 0;
    }
    check(slowConnected, "Slow client connects before HTTP shutdown");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto stopStart = std::chrono::steady_clock::now();
    server.stop();
    auto stopElapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - stopStart).count();
    check(stopElapsed < 2.0, "HTTP stop promptly interrupts a slow client");
    if (slowFd >= 0) ::close(slowFd);
    return true;
}
