#include "core/json-utils.h"

#include <string>
#include <utility>
#include <vector>

namespace Flare {
    bool ValidateJsonTree(const JSON& value, std::size_t maxDepth, std::size_t maxNodes) {
        std::vector<std::pair<const JSON*, std::size_t>> pending;
        pending.emplace_back(&value, 0);
        std::size_t nodes = 0;
        while (!pending.empty()) {
            const auto entry = pending.back();
            pending.pop_back();
            if (entry.second > maxDepth) {
                return false;
            }
            ++nodes;
            if (nodes > maxNodes) {
                return false;
            }
            if (entry.first->is_array()) {
                for (const JSON& child : *entry.first) {
                    pending.emplace_back(&child, entry.second + 1);
                }
            } else if (entry.first->is_object()) {
                for (const auto& child : entry.first->items()) {
                    pending.emplace_back(&child.value(), entry.second + 1);
                }
            }
        }
        return true;
    }

    Result<JSON> ParseJsonValue(
        std::string_view text, std::size_t maxBytes, std::size_t maxDepth, std::size_t maxNodes) {
        if (text.size() > maxBytes) {
            return Error(ErrorCode::INVALID_ARGUMENT, "JSON value exceeds the size limit");
        }
        JSON parsed = JSON::parse(text.begin(), text.end(), nullptr, false);
        if (parsed.is_discarded()) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Malformed JSON value");
        }
        if (!ValidateJsonTree(parsed, maxDepth, maxNodes)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "JSON value exceeds the complexity limit");
        }
        return parsed;
    }

    std::string SafeJsonDump(const JSON& value) {
        return value.dump(-1, ' ', false, JSON::error_handler_t::replace);
    }
}
