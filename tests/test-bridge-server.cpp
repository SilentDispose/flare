#include "test-framework.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "flare/json.h"

#include "bridge/bridge-protocol.h"
#include "bridge/bridge-server.h"
#include "bridge/crypto.h"

namespace {
    struct Capture {
        std::vector<std::string> mainMessages;
        std::vector<std::pair<std::string, std::string>> frameMessages;
        std::vector<std::string> events;
        std::vector<Flare::JSON> eventPayloads;
        std::vector<bool> eventMainFrame;
        int handlerCalls = 0;
        int actionCalls = 0;
        Flare::WindowAction lastAction = Flare::WindowAction::SHOW;
        Flare::Result<Flare::JSON> handlerResult = Flare::JSON{{"ok", true}};
        Flare::Result<Flare::JSON> actionResult = Flare::JSON{{"ok", true}};

        Flare::BridgeHost Host() {
            Flare::BridgeHost host;
            host.appName = "Bridge Test";
            host.appVersion = "1.0.0";
            host.flareVersion = "1.0.0";
            host.platform = "test";
            host.postToMain = [this](const std::string& message) { mainMessages.push_back(message); };
            host.postToFrame = [this](const std::string& message, const std::string& frameId) {
                frameMessages.emplace_back(message, frameId);
            };
            host.invokeHandler = [this](const std::string&, const Flare::JSON&, const std::string&, bool) {
                ++handlerCalls;
                return handlerResult;
            };
            host.performWindowAction = [this](Flare::WindowAction action, const Flare::JSON&) {
                ++actionCalls;
                lastAction = action;
                return actionResult;
            };
            host.deliverEvent = [this](const std::string& name, const Flare::JSON& payload, bool isMainFrame) {
                events.push_back(name);
                eventPayloads.push_back(payload);
                eventMainFrame.push_back(isMainFrame);
            };
            host.clientConfig = []() { return Flare::JSON{{"draggableRegions", Flare::JSON::array()}}; };
            host.reportError = [](const Flare::Error&) {};
            return host;
        }
    };

    std::int64_t NowMilliseconds() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::string HexNonce() {
        return Flare::Crypto::ToHex(Flare::Crypto::SystemRandomBytes(16));
    }

    Flare::BridgeEnvelope Envelope(const std::string& type, const std::string& sid, std::uint64_t sequence,
        const std::string& name, const std::string& id, const std::string& payload) {
        Flare::BridgeEnvelope envelope;
        envelope.type = type;
        envelope.sessionId = sid;
        envelope.sequence = sequence;
        envelope.timestamp = NowMilliseconds();
        envelope.nonce = HexNonce();
        envelope.name = name;
        envelope.id = id;
        envelope.payload = payload;
        return envelope;
    }

    struct BridgeHarness {
        Capture capture;
        Flare::BridgeOptions options;
        std::string documentUrl = "https://app.local/index.html";
        std::string sourceUrl = "https://app.local/page";
        std::string sid;
        std::string token;
        std::uint64_t sequence = 1;
        std::unique_ptr<Flare::BridgeServer> server;

        BridgeHarness(Flare::BridgeOptions bridgeOptions = {}) : options(std::move(bridgeOptions)) {
            server = std::make_unique<Flare::BridgeServer>(options, capture.Host());
        }

        Flare::BridgeMessageContext MainContext() const { return Flare::BridgeMessageContext{sourceUrl, true, {}}; }

        Flare::BridgeMessageContext FrameContext(const std::string& frameId) const {
            return Flare::BridgeMessageContext{sourceUrl, false, frameId};
        }

        void Start() { server->StartDocument(documentUrl); }

        std::string Handshake(bool mainFrame = true, const std::string& frameId = {}) {
            Flare::JSON hello;
            hello["flare"] = 1;
            hello["v"] = 1;
            hello["type"] = "hello";
            hello["nonce"] = HexNonce();
            Flare::BridgeMessageContext context = mainFrame ? MainContext() : FrameContext(frameId);
            server->HandleMessage(hello.dump(), context);
            if (mainFrame) {
                Flare::JSON ack;
                for (auto iterator = capture.mainMessages.rbegin(); iterator != capture.mainMessages.rend();
                    ++iterator) {
                    const Flare::JSON candidate = Flare::JSON::parse(*iterator);
                    if (candidate.value("type", std::string()) == "handshake") {
                        ack = candidate;
                        break;
                    }
                }
                sid = ack["sid"].get<std::string>();
                token = ack["token"].get<std::string>();
            } else {
                const Flare::JSON ack = Flare::JSON::parse(capture.frameMessages.back().first);
                sid = ack["sid"].get<std::string>();
                token = ack["token"].get<std::string>();
            }
            return sid;
        }

        std::uint64_t NextSequence() { return sequence++; }
    };

    std::string Send(BridgeHarness& harness, const Flare::BridgeEnvelope& envelope, const std::string& key,
        const std::string& sourceUrl, bool isMainFrame, const std::string& frameId) {
        const std::string raw = Flare::BuildSignedEnvelope(envelope, key);
        harness.server->HandleMessage(raw, Flare::BridgeMessageContext{sourceUrl, isMainFrame, frameId});
        return raw;
    }
}

void RunBridgeServerTests(Test::Context& context) {
    context.Begin("bridge server");
    {
        Flare::BridgeOptions options;
        options.allowedWindowActions = {Flare::WindowAction::SHOW};
        BridgeHarness harness(options);
        harness.Start();
        context.Check(harness.server->Emit("queued", {{"number", 1}}).IsOk(), "emit queues before handshake");
        context.Check(harness.capture.mainMessages.empty(), "no main message before handshake");
        harness.Handshake();
        context.Check(harness.server->IsHandshaken(), "main session handshaken");
        context.Check(harness.capture.mainMessages.size() == 2, "handshake and queued event posted");
        const Flare::JSON ack = Flare::JSON::parse(harness.capture.mainMessages[0]);
        context.Check(ack["type"] == "handshake", "handshake message type");
        context.Check(ack["actions"].is_array(), "handshake advertises actions");
        context.Check(ack["config"]["draggableRegions"].is_array(), "handshake includes client config");
        const Flare::JSON queuedEvent = Flare::JSON::parse(harness.capture.mainMessages[1]);
        context.Check(queuedEvent["type"] == "event" && queuedEvent["name"] == "queued", "queued event flushed");
        harness.capture.mainMessages.clear();

        const Flare::BridgeEnvelope emit = Envelope(
            "emit", harness.sid, harness.NextSequence(), "button-clicked", "", "{\"label\":\"go\"}");
        Send(harness, emit, harness.token, harness.sourceUrl, true, {});
        context.Check(harness.capture.events.size() == 1 && harness.capture.events.back() == "button-clicked",
            "emit delivered to native handlers");
        context.Check(harness.capture.eventPayloads.back()["label"] == "go", "emit payload delivered");
        context.Check(harness.capture.eventMainFrame.back(), "main-frame emit flagged");
        context.Check(harness.capture.mainMessages.empty(), "emit has no response");

        harness.capture.handlerResult = Flare::JSON{{"sum", 7}};
        const Flare::BridgeEnvelope invoke = Envelope(
            "invoke", harness.sid, harness.NextSequence(), "add", "req-1", "{\"left\":3,\"right\":4}");
        Send(harness, invoke, harness.token, harness.sourceUrl, true, {});
        context.Check(harness.capture.handlerCalls == 1, "invoke reached the handler");
        const Flare::JSON result = Flare::JSON::parse(harness.capture.mainMessages.back());
        context.Check(result["type"] == "result" && result["payload"]["sum"] == 7, "invoke result returned");

        harness.capture.handlerResult = Flare::Error(Flare::ErrorCode::HANDLER_FAILED, "handler boom");
        const Flare::BridgeEnvelope failing = Envelope(
            "invoke", harness.sid, harness.NextSequence(), "add", "req-2", "{}");
        Send(harness, failing, harness.token, harness.sourceUrl, true, {});
        const Flare::JSON failure = Flare::JSON::parse(harness.capture.mainMessages.back());
        context.Check(failure["type"] == "error" && failure["code"] == "handler_failed", "handler failure propagated");

        harness.capture.handlerResult = Flare::JSON{{"ok", true}};
        harness.capture.mainMessages.clear();
        Flare::BridgeEnvelope badMac = Envelope("invoke", harness.sid, harness.NextSequence(), "add", "req-3", "{}");
        const std::string raw = Flare::BuildSignedEnvelope(badMac, harness.token);
        Flare::JSON tampered = Flare::JSON::parse(raw);
        tampered["mac"] = Flare::Crypto::Base64UrlEncode(std::vector<std::uint8_t>(32, 0));
        harness.server->HandleMessage(tampered.dump(), harness.MainContext());
        context.Check(harness.capture.mainMessages.empty(), "invalid MAC gets no response");
        const std::uint64_t callsBefore = static_cast<std::uint64_t>(harness.capture.handlerCalls);

        Flare::BridgeEnvelope replayed = Envelope("emit", harness.sid, harness.NextSequence(), "replay-test", "", "{}");
        const std::string replayRaw = Send(harness, replayed, harness.token, harness.sourceUrl, true, {});
        const std::size_t eventsBefore = harness.capture.events.size();
        harness.server->HandleMessage(replayRaw, harness.MainContext());
        context.Check(harness.capture.events.size() == eventsBefore, "replayed message dropped");

        Flare::BridgeEnvelope originMismatch = Envelope(
            "emit", harness.sid, harness.NextSequence(), "origin-test", "", "{}");
        Send(harness, originMismatch, harness.token, "https://evil.local/page", true, {});
        context.Check(harness.capture.events.empty() || harness.capture.events.back() != "origin-test",
            "origin mismatch rejected");

        Flare::BridgeEnvelope unknownSession = Envelope(
            "emit", "unknown-session", harness.NextSequence(), "x", "", "{}");
        Send(harness, unknownSession, "wrong-token", harness.sourceUrl, true, {});
        context.Check(harness.capture.handlerCalls == static_cast<int>(callsBefore), "unknown session rejected");

        harness.capture.actionCalls = 0;
        const Flare::BridgeEnvelope show = Envelope(
            "windowAction", harness.sid, harness.NextSequence(), "show", "act-1", "{}");
        Send(harness, show, harness.token, harness.sourceUrl, true, {});
        context.Check(harness.capture.actionCalls == 1, "allowed window action performed");
        const Flare::BridgeEnvelope close = Envelope(
            "windowAction", harness.sid, harness.NextSequence(), "close", "act-2", "{}");
        Send(harness, close, harness.token, harness.sourceUrl, true, {});
        const Flare::JSON denied = Flare::JSON::parse(harness.capture.mainMessages.back());
        context.Check(
            denied["type"] == "error" && denied["code"] == "permission_denied", "disallowed window action rejected");
    }

    {
        Flare::BridgeOptions options;
        options.maxPendingEvents = 1;
        BridgeHarness harness(options);
        harness.Start();
        context.Check(harness.server->Emit("one", Flare::JSON::object()).IsOk(), "first pending event accepted");
        context.Check(harness.server->Emit("two", Flare::JSON::object()).IsError(), "pending queue overflow rejected");
    }

    {
        Flare::BridgeOptions options;
        options.allowedHandlerNames = {"add"};
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.capture.mainMessages.clear();
        const Flare::BridgeEnvelope denied = Envelope(
            "invoke", harness.sid, harness.NextSequence(), "denied", "req-9", "{}");
        Send(harness, denied, harness.token, harness.sourceUrl, true, {});
        const Flare::JSON response = Flare::JSON::parse(harness.capture.mainMessages.back());
        context.Check(
            response["type"] == "error" && response["code"] == "permission_denied", "handler whitelist enforced");
        context.Check(harness.capture.handlerCalls == 0, "denied handler not invoked");
    }

    {
        Flare::BridgeOptions options;
        options.maxPayloadBytes = 16;
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.capture.mainMessages.clear();
        const Flare::BridgeEnvelope oversized = Envelope("invoke", harness.sid, harness.NextSequence(), "add", "req-10",
            "{\"payload\":\"012345678901234567890123456789\"}");
        Send(harness, oversized, harness.token, harness.sourceUrl, true, {});
        const Flare::JSON response = Flare::JSON::parse(harness.capture.mainMessages.back());
        context.Check(response["type"] == "error" && response["code"] == "invalid_argument", "payload limit enforced");
    }

    {
        Flare::BridgeOptions options;
        options.maxBurst = 1;
        options.maxMessagesPerSecond = 0.0001;
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.capture.mainMessages.clear();
        const Flare::BridgeEnvelope first = Envelope(
            "invoke", harness.sid, harness.NextSequence(), "add", "req-11", "{}");
        Send(harness, first, harness.token, harness.sourceUrl, true, {});
        context.Check(
            Flare::JSON::parse(harness.capture.mainMessages.back())["type"] == "result", "first message within burst");
        const Flare::BridgeEnvelope second = Envelope(
            "invoke", harness.sid, harness.NextSequence(), "add", "req-12", "{}");
        Send(harness, second, harness.token, harness.sourceUrl, true, {});
        context.Check(
            Flare::JSON::parse(harness.capture.mainMessages.back())["code"] == "rate_limited", "rate limit enforced");
    }

    {
        Flare::BridgeOptions options;
        options.allowIframes = false;
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.capture.frameMessages.clear();
        Flare::JSON hello;
        hello["flare"] = 1;
        hello["v"] = 1;
        hello["type"] = "hello";
        hello["nonce"] = HexNonce();
        harness.server->HandleMessage(hello.dump(), Flare::BridgeMessageContext{harness.sourceUrl, false, "frame-1"});
        context.Check(harness.capture.frameMessages.empty(), "iframe handshake rejected when disabled");
    }

    {
        Flare::BridgeOptions options;
        options.allowIframes = true;
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.Handshake(false, "frame-1");
        context.Check(!harness.capture.frameMessages.empty(), "iframe handshake acknowledged when enabled");
        const Flare::JSON frameAck = Flare::JSON::parse(harness.capture.frameMessages.back().first);
        const std::string frameSid = frameAck["sid"].get<std::string>();
        const std::string frameToken = frameAck["token"].get<std::string>();
        harness.capture.events.clear();
        const Flare::BridgeEnvelope emit = Envelope("emit", frameSid, 1, "frame-event", "", "{\"ok\":true}");
        Send(harness, emit, frameToken, harness.sourceUrl, false, "frame-1");
        context.Check(harness.capture.events.size() == 1 && !harness.capture.eventMainFrame.back(),
            "iframe emit delivered with frame flag");
        harness.capture.frameMessages.clear();
        const Flare::BridgeEnvelope action = Envelope("windowAction", frameSid, 2, "show", "frame-action", "{}");
        Send(harness, action, frameToken, harness.sourceUrl, false, "frame-1");
        const Flare::JSON frameError = Flare::JSON::parse(harness.capture.frameMessages.back().first);
        context.Check(frameError["code"] == "permission_denied", "iframe window action rejected");
    }

    {
        Flare::BridgeOptions options;
        options.clockSkewSeconds = 1;
        BridgeHarness harness(options);
        harness.Start();
        harness.Handshake();
        harness.capture.events.clear();
        Flare::BridgeEnvelope stale = Envelope("emit", harness.sid, harness.NextSequence(), "stale", "", "{}");
        stale.timestamp = NowMilliseconds() - 10000;
        Send(harness, stale, harness.token, harness.sourceUrl, true, {});
        context.Check(harness.capture.events.empty(), "stale timestamp rejected");
    }

    {
        Flare::BridgeOptions options;
        BridgeHarness harness(options);
        harness.Start();
        context.Check(!harness.server->Emit("bad name", Flare::JSON::object()).IsOk(), "invalid event name rejected");
    }
}
