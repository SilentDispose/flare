#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "flare/json.h"

#include "flare/error.h"

namespace Flare {
    bool ValidateJsonTree(const JSON& value, std::size_t maxDepth, std::size_t maxNodes);
    Result<JSON> ParseJsonValue(
        std::string_view text, std::size_t maxBytes, std::size_t maxDepth, std::size_t maxNodes);
    std::string SafeJsonDump(const JSON& value);
}
