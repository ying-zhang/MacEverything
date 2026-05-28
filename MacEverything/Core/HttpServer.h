#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>

class SearchEngine;
class ContentIndex;

class HttpServer {
public:
    /// Callbacks for management operations. Injected by the Bridge layer.
    struct AdminCallbacks {
        std::function<void()> onRebuildIndex;
        std::function<void()> onRebuildContentIndex;
        std::function<void(const std::vector<std::string>&, uint64_t)> onSetContentConfig;
        std::function<std::vector<std::string>()> onGetContentExtensions;
        std::function<uint64_t()> onGetContentMaxFileSize;
    };

    HttpServer() = default;
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    using EngineGetter = std::function<std::shared_ptr<SearchEngine>()>;
    using ContentIndexGetter = std::function<std::shared_ptr<ContentIndex>()>;

    bool start(uint16_t port,
               EngineGetter engineGetter,
               ContentIndexGetter contentIndexGetter);
    void stop();
    bool isRunning() const;
    uint16_t port() const;

    void setAdminCallbacks(AdminCallbacks callbacks);

private:
    void acceptLoop();
    void handleConnection(int clientFd);

    struct HttpRequest {
        std::string method;
        std::string path;
        std::unordered_map<std::string, std::string> query;
        std::string body;
    };

    HttpRequest parseRequest(const std::string& raw);
    std::string route(const HttpRequest& req);

    std::string handleSearch(const std::unordered_map<std::string, std::string>& params);
    std::string handleContentSearch(const std::unordered_map<std::string, std::string>& params);
    std::string handleRecent(const std::unordered_map<std::string, std::string>& params);
    std::string handleStatus();
    std::string handleMemory();
    std::string handleHealth();

    // Admin endpoints
    std::string handleRebuildIndex();
    std::string handleRebuildContentIndex();
    std::string handleGetContentConfig();
    std::string handleSetContentConfig(const std::string& body);

    std::string jsonResponse(int status, const std::string& body);
    std::string errorResponse(int status, const std::string& message);

    EngineGetter getEngine_;
    ContentIndexGetter getContentIndex_;
    AdminCallbacks adminCallbacks_;
    std::atomic<bool> running_{false};
    int serverFd_{-1};
    uint16_t port_{0};
    std::thread acceptThread_;
    std::mutex connectionThreadsMutex_;
    std::vector<std::thread> connectionThreads_;
};
