#pragma once
#import "MacSearchBridge.h"
#include "ServiceEngine.h"
#include <memory>

/// Class extension: Bridge holds a ServiceEngine and forwards all operations.
@interface MacSearchBridge () {
@public
    std::shared_ptr<ServiceEngine> _serviceEngine;
}

@end
