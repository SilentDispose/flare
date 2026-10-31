#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "flare/json.h"

#include "flare/error.h"

namespace Flare {
    extern const int BridgeProtocolVersion;

    struct BridgeEnvelope {
        int version = 1;
        std::string type;
        std::string sessionId;
        std::string name;
        std::string id;
        std::string payload;
        std::string nonce;
        std::string mac;
        std::uint64_t sequence = 0;
        std::int64_t timestamp = 0;
        bool isHello = false;
    };

    enum class BridgeMessageKind { RAW, MALFORMED, HELLO, SIGNED };

    struct BridgeParseResult {
        BridgeMessageKind kind = BridgeMessageKind::RAW;
        BridgeEnvelope envelope;
        Error error;
    };

    BridgeParseResult ParseBridgeMessage(std::string_view raw, std::size_t maxMessageBytes);
    std::string BuildMacInput(const BridgeEnvelope& envelope);
    std::string BuildSignedEnvelope(const BridgeEnvelope& envelope, std::string_view key);
    std::string BuildEventMessage(std::string_view name, const JSON& payload);
    std::string BuildResultMessage(std::string_view id, const JSON& payload);
    std::string BuildErrorMessage(std::string_view id, std::string_view code, std::string_view message);
    std::string BridgeErrorCode(ErrorCode code);
}
