#pragma once
#import "MacSearchBridge.h"
#include "ServiceEngine.h"
#include <memory>
#include <string>

// macOS filenames can contain arbitrary bytes. Preserve those records at the
// Objective-C boundary by replacing malformed UTF-8 sequences with U+FFFD.
static inline std::string MESanitizeUTF8(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size();) {
        const uint8_t byte = static_cast<uint8_t>(input[i]);
        size_t width = 0;
        if (byte <= 0x7f) width = 1;
        else if (byte >= 0xc2 && byte <= 0xdf) width = 2;
        else if (byte >= 0xe0 && byte <= 0xef) width = 3;
        else if (byte >= 0xf0 && byte <= 0xf4) width = 4;

        bool valid = width != 0 && i + width <= input.size();
        if (valid && width > 1) {
            for (size_t j = 1; j < width; ++j) {
                if ((static_cast<uint8_t>(input[i + j]) & 0xc0) != 0x80) {
                    valid = false;
                    break;
                }
            }
            const uint8_t second = static_cast<uint8_t>(input[i + 1]);
            if ((byte == 0xe0 && second < 0xa0) ||
                (byte == 0xed && second >= 0xa0) ||
                (byte == 0xf0 && second < 0x90) ||
                (byte == 0xf4 && second >= 0x90)) {
                valid = false;
            }
        }
        if (!valid) {
            output.append("\xef\xbf\xbd");
            ++i;
            continue;
        }
        output.append(input, i, width);
        i += width;
    }
    return output;
}

static inline NSString *MEStringFromUTF8(const std::string& input) {
    const std::string safe = MESanitizeUTF8(input);
    return [[NSString alloc] initWithBytes:safe.data()
                                    length:safe.size()
                                  encoding:NSUTF8StringEncoding];
}

/// Class extension: Bridge holds a ServiceEngine and forwards all operations.
@interface MacSearchBridge () {
@public
    std::shared_ptr<ServiceEngine> _serviceEngine;
}

@end
