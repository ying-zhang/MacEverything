#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace mace {

struct HttpResponse {
    int status = 0;
    std::string body;
};

inline std::string urlEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0x0f]);
        }
    }
    return result;
}

inline int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline void appendUTF8(std::string& result, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

inline bool decodeJSONString(const std::string& json, size_t quote,
                             std::string& value, size_t& endQuote) {
    if (quote >= json.size() || json[quote] != '"') return false;
    value.clear();
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') {
            endQuote = i;
            return true;
        }
        if (c != '\\') {
            value.push_back(c);
            continue;
        }
        if (++i >= json.size()) return false;
        switch (json[i]) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= json.size()) return false;
                uint32_t codepoint = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    int digitValue = hexValue(json[++i]);
                    if (digitValue < 0) return false;
                    codepoint = (codepoint << 4) | static_cast<uint32_t>(digitValue);
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (i + 6 >= json.size() || json[i + 1] != '\\' || json[i + 2] != 'u') {
                        return false;
                    }
                    i += 2;
                    uint32_t lowSurrogate = 0;
                    for (int digit = 0; digit < 4; ++digit) {
                        int digitValue = hexValue(json[++i]);
                        if (digitValue < 0) return false;
                        lowSurrogate = (lowSurrogate << 4) | static_cast<uint32_t>(digitValue);
                    }
                    if (lowSurrogate < 0xdc00 || lowSurrogate > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (lowSurrogate - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return false;
                }
                appendUTF8(value, codepoint);
                break;
            }
            default: return false;
        }
    }
    return false;
}

inline std::vector<std::string> stringFieldValues(const std::string& json,
                                                   const std::string& key) {
    std::vector<std::string> values;
    for (size_t position = 0; position < json.size(); ++position) {
        if (json[position] != '"') continue;
        std::string decodedKey;
        size_t keyEnd = position;
        if (!decodeJSONString(json, position, decodedKey, keyEnd)) break;
        size_t colon = json.find_first_not_of(" \t\r\n", keyEnd + 1);
        if (decodedKey != key || colon == std::string::npos || json[colon] != ':') {
            position = keyEnd;
            continue;
        }
        size_t quote = json.find_first_not_of(" \t\r\n", colon + 1);
        if (quote == std::string::npos || json[quote] != '"') continue;
        std::string value;
        size_t endQuote = quote;
        if (!decodeJSONString(json, quote, value, endQuote)) break;
        values.push_back(std::move(value));
        position = endQuote + 1;
    }
    return values;
}

inline bool fieldValuePosition(const std::string& json, const std::string& key, size_t& valuePosition) {
    for (size_t position = 0; position < json.size(); ++position) {
        if (json[position] != '"') continue;
        std::string decodedKey;
        size_t keyEnd = position;
        if (!decodeJSONString(json, position, decodedKey, keyEnd)) return false;
        size_t colon = json.find_first_not_of(" \t\r\n", keyEnd + 1);
        if (decodedKey == key && colon != std::string::npos && json[colon] == ':') {
            valuePosition = json.find_first_not_of(" \t\r\n", colon + 1);
            return valuePosition != std::string::npos;
        }
        position = keyEnd;
    }
    return false;
}

inline bool integerField(const std::string& json, const std::string& key, uint64_t& value) {
    size_t position = 0;
    if (!fieldValuePosition(json, key, position) ||
        json[position] < '0' || json[position] > '9') return false;
    value = 0;
    while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
        uint64_t digit = static_cast<uint64_t>(json[position++] - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

inline bool boolField(const std::string& json, const std::string& key, bool& value) {
    size_t position = 0;
    if (!fieldValuePosition(json, key, position)) return false;
    if (json.compare(position, 4, "true") == 0) { value = true; return true; }
    if (json.compare(position, 5, "false") == 0) { value = false; return true; }
    return false;
}

inline bool parseHttpResponse(const std::string& raw, HttpResponse& response,
                              std::string& error) {
    size_t lineEnd = raw.find("\r\n");
    size_t headerEnd = raw.find("\r\n\r\n");
    if (lineEnd == std::string::npos || headerEnd == std::string::npos) {
        error = "invalid HTTP response from MacEverything";
        return false;
    }
    int status = 0;
    if (std::sscanf(raw.substr(0, lineEnd).c_str(), "HTTP/%*s %d", &status) != 1) {
        error = "invalid HTTP status from MacEverything";
        return false;
    }
    response.status = status;
    response.body = raw.substr(headerEnd + 4);
    return true;
}

inline bool httpGet(uint16_t port, const std::string& target,
                    HttpResponse& response, std::string& error) {
    int socketFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }

    timeval timeout{5, 0};
    ::setsockopt(socketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(socketFD, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socketFD, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "cannot connect to MacEverything on 127.0.0.1:" + std::to_string(port) +
            "; make sure the app is running";
        ::close(socketFD);
        return false;
    }

    std::string request = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    request += "Accept: application/json\r\nConnection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < request.size()) {
        ssize_t count = ::send(socketFD, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            error = std::string("send: ") + std::strerror(errno);
            ::close(socketFD);
            return false;
        }
        sent += static_cast<size_t>(count);
    }

    std::string raw;
    char buffer[16384];
    while (true) {
        ssize_t count = ::recv(socketFD, buffer, sizeof(buffer), 0);
        if (count > 0) {
            raw.append(buffer, static_cast<size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                error = "timed out waiting for MacEverything response";
            } else {
                error = std::string("receive: ") + std::strerror(errno);
            }
            ::close(socketFD);
            return false;
        }
    }
    ::close(socketFD);
    return parseHttpResponse(raw, response, error);
}

}  // namespace mace
