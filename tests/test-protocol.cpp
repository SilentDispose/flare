#include "test-framework.h"

#include <string>

#include "bridge/bridge-protocol.h"
#include "bridge/crypto.h"

void RunProtocolTests(Test::Context& context) {
    context.Begin("protocol");
    const auto raw = Flare::ParseBridgeMessage("\"plain message\"", 1024 * 1024);
    context.Check(raw.kind == Flare::BridgeMessageKind::RAW, "plain json is raw");

    const auto rawObject = Flare::ParseBridgeMessage("{\"hello\":true}", 1024 * 1024);
    context.Check(rawObject.kind == Flare::BridgeMessageKind::RAW, "object without marker is raw");

    const auto hello = Flare::ParseBridgeMessage(
        "{\"flare\":1,\"v\":1,\"type\":\"hello\",\"nonce\":\"00112233445566778899aabbccddeeff\"}", 1024 * 1024);
    context.Check(hello.kind == Flare::BridgeMessageKind::HELLO, "hello parsed");
    context.Check(hello.envelope.isHello, "hello flag set");

    const auto malformedVersion = Flare::ParseBridgeMessage(
        "{\"flare\":1,\"v\":9,\"type\":\"hello\",\"nonce\":\"00112233445566778899aabbccddeeff\"}", 1024 * 1024);
    context.Check(malformedVersion.kind == Flare::BridgeMessageKind::MALFORMED, "unknown version rejected");

    Flare::BridgeEnvelope envelope;
    envelope.type = "invoke";
    envelope.sessionId = "session-abc";
    envelope.sequence = 12;
    envelope.timestamp = 1700000000000;
    envelope.nonce = "00112233445566778899aabbccddeeff";
    envelope.name = "add";
    envelope.id = "req-7";
    envelope.payload = "{\"left\":1}";
    const std::string key = "secret-key";
    const std::string macInput = Flare::BuildMacInput(envelope);
    context.Check(macInput.find("v1|session-abc|12|1700000000000|") == 0, "mac input format");
    context.Check(macInput.find("|invoke|add|req-7|") != std::string::npos, "mac input fields");

    const std::string signedMessage = Flare::BuildSignedEnvelope(envelope, key);
    const auto parsed = Flare::ParseBridgeMessage(signedMessage, 1024 * 1024);
    context.Check(parsed.kind == Flare::BridgeMessageKind::SIGNED, "signed envelope parsed");
    context.Check(parsed.envelope.sessionId == envelope.sessionId, "session id round trip");
    context.Check(parsed.envelope.sequence == envelope.sequence, "sequence round trip");
    context.Check(parsed.envelope.payload == envelope.payload, "payload round trip");
    context.Check(parsed.envelope.mac == Flare::Crypto::Base64UrlEncode(Flare::Crypto::HmacSha256(key, macInput)),
        "mac is deterministic");

    const auto oversized = Flare::ParseBridgeMessage(std::string(2048, 'a'), 1024);
    context.Check(oversized.kind == Flare::BridgeMessageKind::MALFORMED, "oversized message rejected");

    context.Check(
        Flare::BuildErrorMessage("id-1", "permission_denied", "no").find("permission_denied") != std::string::npos,
        "error message contains code");
    context.Check(
        Flare::BridgeErrorCode(Flare::ErrorCode::INVALID_ARGUMENT) == "invalid_argument", "error code mapping");
    context.Check(Flare::BridgeErrorCode(Flare::ErrorCode::UNKNOWN) == "internal", "unknown error mapping");
}
