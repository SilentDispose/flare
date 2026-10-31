#include "bridge/bridge-protocol.h"

#include <string>

#include "bridge/crypto.h"
#include "core/json-utils.h"
#include "core/text-utils.h"

namespace Flare {
    extern const int BridgeProtocolVersion = 1;

    namespace {
        bool IsBase64UrlText(std::string_view text, std::size_t maxLength) {
            if (text.empty() || text.size() > maxLength) {
                return false;
            }
            for (char character : text) {
                const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '-' || character == '_';
                if (!valid) {
                    return false;
                }
            }
            return true;
        }

        BridgeParseResult Malformed(std::string message) {
            BridgeParseResult result;
            result.kind = BridgeMessageKind::MALFORMED;
            result.error = Error(ErrorCode::BRIDGE_REJECTED, std::move(message));
            return result;
        }

        JSON BaseMessage(std::string_view type) {
            JSON message;
            message["flare"] = 1;
            message["v"] = BridgeProtocolVersion;
            message["type"] = type;
            return message;
        }
    }

    BridgeParseResult ParseBridgeMessage(std::string_view raw, std::size_t maxMessageBytes) {
        BridgeParseResult result;
        if (raw.size() > maxMessageBytes) {
            return Malformed("Bridge message exceeds the size limit");
        }
        const JSON parsed = JSON::parse(raw.begin(), raw.end(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return result;
        }
        const auto marker = parsed.find("flare");
        if (marker == parsed.end() || !marker->is_number_integer() || marker->get<int>() != 1) {
            return result;
        }
        const auto version = parsed.find("v");
        if (version == parsed.end() || !version->is_number_integer() || version->get<int>() != BridgeProtocolVersion) {
            return Malformed("Unsupported bridge protocol version");
        }
        const auto type = parsed.find("type");
        if (type == parsed.end() || !type->is_string()) {
            return Malformed("Bridge message is missing a type");
        }

        BridgeEnvelope envelope;
        envelope.type = type->get<std::string>();

        if (envelope.type == "hello") {
            const auto nonce = parsed.find("nonce");
            if (nonce == parsed.end() || !nonce->is_string()) {
                return Malformed("Handshake is missing a nonce");
            }
            envelope.nonce = nonce->get<std::string>();
            if (!Crypto::IsHex(envelope.nonce) || envelope.nonce.size() < 32 || envelope.nonce.size() > 128) {
                return Malformed("Handshake nonce is invalid");
            }
            envelope.isHello = true;
            result.kind = BridgeMessageKind::HELLO;
            result.envelope = std::move(envelope);
            return result;
        }

        if (envelope.type != "emit" && envelope.type != "invoke" && envelope.type != "windowAction") {
            return Malformed("Unknown bridge message type");
        }

        const auto session = parsed.find("sid");
        const auto sequence = parsed.find("seq");
        const auto timestamp = parsed.find("ts");
        const auto nonce = parsed.find("nonce");
        const auto name = parsed.find("name");
        const auto id = parsed.find("id");
        const auto payload = parsed.find("payload");
        const auto mac = parsed.find("mac");
        if (session == parsed.end() || !session->is_string() || !IsBase64UrlText(session->get<std::string>(), 128)) {
            return Malformed("Bridge message has an invalid session identifier");
        }
        if (sequence == parsed.end() || !sequence->is_number_unsigned()) {
            return Malformed("Bridge message has an invalid sequence number");
        }
        if (timestamp == parsed.end() || !timestamp->is_number_integer()) {
            return Malformed("Bridge message has an invalid timestamp");
        }
        if (nonce == parsed.end() || !nonce->is_string()) {
            return Malformed("Bridge message is missing a nonce");
        }
        if (name == parsed.end() || !name->is_string() || name->get_ref<const std::string&>().size() > 128) {
            return Malformed("Bridge message has an invalid name");
        }
        if (id == parsed.end() || !id->is_string() || id->get_ref<const std::string&>().size() > 128) {
            return Malformed("Bridge message has an invalid request identifier");
        }
        if (payload == parsed.end() || !payload->is_string()) {
            return Malformed("Bridge message payload must be a JSON string");
        }
        if (mac == parsed.end() || !mac->is_string()) {
            return Malformed("Bridge message is missing a MAC");
        }

        envelope.sessionId = session->get<std::string>();
        envelope.sequence = sequence->get<std::uint64_t>();
        envelope.timestamp = timestamp->get<std::int64_t>();
        envelope.nonce = nonce->get<std::string>();
        envelope.name = name->get<std::string>();
        envelope.id = id->get<std::string>();
        envelope.payload = payload->get<std::string>();
        envelope.mac = mac->get<std::string>();

        if (!Crypto::IsHex(envelope.nonce) || envelope.nonce.size() < 32 || envelope.nonce.size() > 128) {
            return Malformed("Bridge message nonce is invalid");
        }
        if (!IsValidName(envelope.name)) {
            return Malformed("Bridge message name is invalid");
        }
        if (envelope.type != "emit" && (envelope.id.empty() || !IsValidName(envelope.id))) {
            return Malformed("Bridge message request identifier is invalid");
        }
        if (!envelope.id.empty() && !IsValidName(envelope.id)) {
            return Malformed("Bridge message request identifier is invalid");
        }
        if (envelope.mac.size() != 43 || !IsBase64UrlText(envelope.mac, 43)) {
            return Malformed("Bridge message MAC is invalid");
        }

        result.kind = BridgeMessageKind::SIGNED;
        result.envelope = std::move(envelope);
        return result;
    }

    std::string BuildMacInput(const BridgeEnvelope& envelope) {
        std::string input;
        input.reserve(envelope.payload.size() + 256);
        input += "v1|";
        input += envelope.sessionId;
        input += '|';
        input += std::to_string(envelope.sequence);
        input += '|';
        input += std::to_string(envelope.timestamp);
        input += '|';
        input += envelope.nonce;
        input += '|';
        input += envelope.type;
        input += '|';
        input += envelope.name;
        input += '|';
        input += envelope.id;
        input += '|';
        input += envelope.payload;
        return input;
    }

    std::string BuildSignedEnvelope(const BridgeEnvelope& envelope, std::string_view key) {
        const std::string mac = Crypto::Base64UrlEncode(Crypto::HmacSha256(key, BuildMacInput(envelope)));
        JSON message = BaseMessage(envelope.type);
        message["sid"] = envelope.sessionId;
        message["seq"] = envelope.sequence;
        message["ts"] = envelope.timestamp;
        message["nonce"] = envelope.nonce;
        message["name"] = envelope.name;
        message["id"] = envelope.id;
        message["payload"] = envelope.payload;
        message["mac"] = mac;
        return SafeJsonDump(message);
    }

    std::string BuildEventMessage(std::string_view name, const JSON& payload) {
        JSON message = BaseMessage("event");
        message["name"] = std::string(name);
        message["payload"] = payload;
        return SafeJsonDump(message);
    }

    std::string BuildResultMessage(std::string_view id, const JSON& payload) {
        JSON message = BaseMessage("result");
        message["id"] = std::string(id);
        message["payload"] = payload;
        return SafeJsonDump(message);
    }

    std::string BuildErrorMessage(std::string_view id, std::string_view code, std::string_view messageText) {
        JSON message = BaseMessage("error");
        message["id"] = std::string(id);
        message["code"] = std::string(code);
        message["message"] = std::string(messageText);
        return SafeJsonDump(message);
    }

    std::string BridgeErrorCode(ErrorCode code) {
        switch (code) {
            case ErrorCode::INVALID_ARGUMENT:
                return "invalid_argument";
            case ErrorCode::NOT_FOUND:
                return "not_found";
            case ErrorCode::NOT_SUPPORTED:
                return "not_supported";
            case ErrorCode::PERMISSION_DENIED:
                return "permission_denied";
            case ErrorCode::TIMEOUT:
                return "timeout";
            case ErrorCode::CANCELLED:
                return "cancelled";
            case ErrorCode::BRIDGE_REJECTED:
                return "rejected";
            case ErrorCode::BRIDGE_RATE_LIMITED:
                return "rate_limited";
            case ErrorCode::HANDLER_FAILED:
                return "handler_failed";
            case ErrorCode::BROWSER_NOT_READY:
                return "browser_not_ready";
            case ErrorCode::WINDOW_CLOSED:
                return "window_closed";
            case ErrorCode::NAVIGATION_BLOCKED:
                return "navigation_blocked";
            case ErrorCode::NONE:
                return "ok";
            case ErrorCode::UNKNOWN:
            case ErrorCode::ALREADY_EXISTS:
            case ErrorCode::WRONG_THREAD:
            case ErrorCode::PLATFORM_FAILURE:
            case ErrorCode::IO_FAILURE:
            case ErrorCode::SINGLE_INSTANCE:
                break;
        }
        return "internal";
    }
}
