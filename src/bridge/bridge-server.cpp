#include "bridge/bridge-server.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bridge/crypto.h"
#include "core/json-utils.h"
#include "core/text-utils.h"
#include "core/url-utils.h"

namespace Flare {
    namespace {
        std::string OriginFromUrl(std::string_view url) {
            if (url.empty()) {
                return "null";
            }
            std::string origin = ExtractOrigin(url);
            return origin.empty() ? "null" : origin;
        }

        std::int64_t NowMilliseconds() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        std::string Truncate(std::string text, std::size_t limit) {
            if (text.size() > limit) {
                text.resize(limit);
            }
            return text;
        }
    }

    struct BridgeServer::Impl {
        struct Session {
            std::string id;
            std::string key;
            std::string origin;
            std::string frameId;
            bool mainFrame = true;
            bool delivered = false;
            std::uint64_t lastSequence = 0;
            std::deque<std::string> nonces;
            std::unordered_set<std::string> knownNonces;
            double tokens = 0.0;
            std::chrono::steady_clock::time_point lastRefill = std::chrono::steady_clock::now();
        };

        struct PendingEvent {
            std::string message;
            std::string origin;
        };

        BridgeOptions options;
        BridgeHost host;
        std::string documentUrl;
        std::string documentOrigin = "null";
        std::shared_ptr<Session> mainSession;
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
        std::deque<PendingEvent> pending;
        std::size_t handshakeAttempts = 0;

        bool RefillAndConsume(Session& session) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - session.lastRefill).count();
            session.tokens = std::min(
                static_cast<double>(options.maxBurst), session.tokens + elapsed * options.maxMessagesPerSecond);
            session.lastRefill = now;
            if (session.tokens < 1.0) {
                return false;
            }
            session.tokens -= 1.0;
            return true;
        }

        std::shared_ptr<Session> CreateSession(const std::string& origin, bool mainFrame, const std::string& frameId) {
            try {
                auto session = std::make_shared<Session>();
                session->id = Crypto::RandomToken(16);
                session->key = Crypto::RandomToken(32);
                session->origin = origin;
                session->frameId = frameId;
                session->mainFrame = mainFrame;
                session->tokens = static_cast<double>(options.maxBurst);
                return session;
            } catch (const std::exception& exception) {
                if (host.reportError) {
                    host.reportError(Error(
                        ErrorCode::PLATFORM_FAILURE, "Unable to generate a bridge session token", exception.what()));
                }
                return nullptr;
            }
        }

        bool OriginAllowed(const std::string& origin, bool mainFrame) const {
            if (!options.allowedOrigins.empty()) {
                for (const std::string& pattern : options.allowedOrigins) {
                    if (OriginMatchesPattern(origin, pattern)) {
                        return true;
                    }
                }
                return false;
            }
            if (options.hardenedMode) {
                return false;
            }
            if (mainFrame) {
                return origin == documentOrigin;
            }
            return origin == documentOrigin;
        }

        void SendToSession(const Session& session, const std::string& message, const std::string& frameId) const {
            if (session.mainFrame) {
                if (host.postToMain) {
                    host.postToMain(message);
                }
                return;
            }
            if (!frameId.empty() && host.postToFrame) {
                host.postToFrame(message, frameId);
            }
        }

        void SendHandshake(const Session& session, const std::string& frameId) const {
            JSON limits;
            limits["maxMessageBytes"] = options.maxMessageBytes;
            limits["maxPayloadBytes"] = options.maxPayloadBytes;
            limits["rateLimitPerSecond"] = options.maxMessagesPerSecond;
            limits["invokeTimeoutMs"] = options.invocationTimeoutMs;
            limits["clockSkewSeconds"] = options.clockSkewSeconds;

            JSON actions = JSON::array();
            for (WindowAction action : AllWindowActions()) {
                if (options.allowedWindowActions.find(action) != options.allowedWindowActions.end()) {
                    actions.push_back(std::string(WindowActionName(action)));
                }
            }

            JSON config = JSON::object();
            if (host.clientConfig) {
                JSON extra = host.clientConfig();
                if (extra.is_object()) {
                    config = std::move(extra);
                }
            }
            config["allowIframes"] = options.allowIframes;
            config["iframeSession"] = !session.mainFrame;

            JSON message;
            message["flare"] = 1;
            message["v"] = BridgeProtocolVersion;
            message["type"] = "handshake";
            message["sid"] = session.id;
            message["token"] = session.key;
            message["limits"] = limits;
            message["actions"] = actions;
            message["app"] = {{"name", host.appName}, {"version", host.appVersion}, {"flareVersion", host.flareVersion},
                {"platform", host.platform}};
            message["config"] = config;

            SendToSession(session, SafeJsonDump(message), frameId);
        }

        void FlushPending(const Session& session) {
            for (PendingEvent& event : pending) {
                if (event.origin.empty() || event.origin == session.origin) {
                    if (host.postToMain) {
                        host.postToMain(event.message);
                    }
                }
            }
            pending.clear();
        }

        void SendError(
            const Session& session, const std::string& frameId, const std::string& id, const Error& error) const {
            if (id.empty()) {
                return;
            }
            const std::string message = BuildErrorMessage(
                id, BridgeErrorCode(error.code), Truncate(error.message, 512));
            SendToSession(session, message, frameId);
        }

        void HandleHello(const BridgeMessageContext& context) {
            ++handshakeAttempts;
            if (handshakeAttempts > options.maxHandshakeAttemptsPerDocument) {
                return;
            }
            const std::string sourceOrigin = context.sourceUrl.empty() ? documentOrigin
                                                                       : OriginFromUrl(context.sourceUrl);
            if (!OriginAllowed(sourceOrigin, context.isMainFrame)) {
                return;
            }
            if (context.isMainFrame) {
                if (!mainSession) {
                    mainSession = CreateSession(documentOrigin, true, {});
                    if (!mainSession) {
                        return;
                    }
                    sessions[mainSession->id] = mainSession;
                }
                if (mainSession->origin != sourceOrigin) {
                    return;
                }
                const bool firstDelivery = !mainSession->delivered;
                mainSession->delivered = true;
                mainSession->tokens = static_cast<double>(options.maxBurst);
                mainSession->lastRefill = std::chrono::steady_clock::now();
                SendHandshake(*mainSession, {});
                if (firstDelivery) {
                    FlushPending(*mainSession);
                }
                return;
            }

            if (!options.allowIframes || context.frameId.empty()) {
                return;
            }
            if (sessions.size() >= 128) {
                return;
            }
            std::shared_ptr<Session> session = CreateSession(sourceOrigin, false, context.frameId);
            if (!session) {
                return;
            }
            sessions[session->id] = session;
            SendHandshake(*session, context.frameId);
        }

        Result<std::shared_ptr<Session>> Authenticate(
            const BridgeEnvelope& envelope, const BridgeMessageContext& context) {
            const auto iterator = sessions.find(envelope.sessionId);
            if (iterator == sessions.end()) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Unknown bridge session");
            }
            const std::shared_ptr<Session> session = iterator->second;
            if (session->mainFrame != context.isMainFrame) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge session frame mismatch");
            }
            const std::string sourceOrigin = context.sourceUrl.empty() ? session->origin
                                                                       : OriginFromUrl(context.sourceUrl);
            if (sourceOrigin != session->origin) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge message origin mismatch");
            }
            if (!session->mainFrame && context.frameId.empty()) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge frame identifier is missing");
            }
            if (options.clockSkewSeconds > 0) {
                const std::int64_t now = NowMilliseconds();
                const std::int64_t skew = static_cast<std::int64_t>(options.clockSkewSeconds) * 1000;
                const std::int64_t difference = now >= envelope.timestamp ? now - envelope.timestamp
                                                                          : envelope.timestamp - now;
                if (difference > skew) {
                    return Error(ErrorCode::BRIDGE_REJECTED, "Bridge message timestamp is outside the accepted window");
                }
            }
            if (envelope.sequence == 0 || envelope.sequence <= session->lastSequence) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge message sequence was replayed");
            }
            if (session->knownNonces.find(envelope.nonce) != session->knownNonces.end()) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge message nonce was replayed");
            }

            const std::string expectedMac = Crypto::Base64UrlEncode(
                Crypto::HmacSha256(session->key, BuildMacInput(envelope)));
            if (!Crypto::ConstantTimeEquals(expectedMac, envelope.mac)) {
                return Error(ErrorCode::BRIDGE_REJECTED, "Bridge message MAC is invalid");
            }
            if (!RefillAndConsume(*session)) {
                return Error(ErrorCode::BRIDGE_RATE_LIMITED, "Bridge message rate limit exceeded");
            }

            session->lastSequence = envelope.sequence;
            session->knownNonces.insert(envelope.nonce);
            session->nonces.push_back(envelope.nonce);
            while (session->nonces.size() > 1024) {
                session->knownNonces.erase(session->nonces.front());
                session->nonces.pop_front();
            }
            return session;
        }

        bool HandleSigned(const BridgeEnvelope& envelope, const BridgeMessageContext& context) {
            Result<std::shared_ptr<Session>> authenticated = Authenticate(envelope, context);
            if (!authenticated.IsOk()) {
                if (authenticated.GetError().code == ErrorCode::BRIDGE_RATE_LIMITED) {
                    const auto iterator = sessions.find(envelope.sessionId);
                    if (iterator != sessions.end()) {
                        SendError(*iterator->second, context.frameId, envelope.id, authenticated.GetError());
                    }
                }
                return true;
            }
            const std::shared_ptr<Session> session = authenticated.Value();
            const std::string frameId = context.frameId;

            Result<JSON> payload = ParseJsonValue(
                envelope.payload, options.maxPayloadBytes, options.maxJsonDepth, options.maxJsonNodes);
            if (!payload.IsOk()) {
                SendError(*session, frameId, envelope.id, payload.GetError());
                return true;
            }

            const bool allowedName = options.allowedHandlerNames.empty()
                || options.allowedHandlerNames.find(envelope.name) != options.allowedHandlerNames.end();

            if (envelope.type == "emit") {
                if (allowedName && host.deliverEvent) {
                    host.deliverEvent(envelope.name, payload.Value(), session->mainFrame);
                }
                return true;
            }

            if (envelope.type == "invoke") {
                if (!allowedName) {
                    SendError(*session, frameId, envelope.id,
                        Error(ErrorCode::PERMISSION_DENIED, "Bridge handler is not permitted for this window"));
                    return true;
                }
                if (!host.invokeHandler) {
                    SendError(*session, frameId, envelope.id,
                        Error(ErrorCode::NOT_SUPPORTED, "Bridge handler dispatch is unavailable"));
                    return true;
                }
                Result<JSON> result = host.invokeHandler(
                    envelope.name, payload.Value(), session->origin, session->mainFrame);
                if (!result.IsOk()) {
                    SendError(*session, frameId, envelope.id, result.GetError());
                    return true;
                }
                const std::string message = BuildResultMessage(envelope.id, result.Value());
                if (message.size() > options.maxMessageBytes) {
                    SendError(*session, frameId, envelope.id,
                        Error(ErrorCode::INVALID_ARGUMENT, "Bridge handler result exceeds the message size limit"));
                    return true;
                }
                SendToSession(*session, message, frameId);
                return true;
            }

            if (!session->mainFrame) {
                SendError(*session, frameId, envelope.id,
                    Error(ErrorCode::PERMISSION_DENIED, "Window actions are only available to the top-level document"));
                return true;
            }
            const std::optional<WindowAction> action = ParseWindowAction(envelope.name);
            if (!action.has_value()) {
                SendError(*session, frameId, envelope.id, Error(ErrorCode::INVALID_ARGUMENT, "Unknown window action"));
                return true;
            }
            if (options.allowedWindowActions.find(*action) == options.allowedWindowActions.end()) {
                SendError(*session, frameId, envelope.id,
                    Error(ErrorCode::PERMISSION_DENIED, "Window action is not permitted for this window"));
                return true;
            }
            if (!host.performWindowAction) {
                SendError(*session, frameId, envelope.id,
                    Error(ErrorCode::NOT_SUPPORTED, "Window action dispatch is unavailable"));
                return true;
            }
            Result<JSON> result = host.performWindowAction(*action, payload.Value());
            if (!result.IsOk()) {
                SendError(*session, frameId, envelope.id, result.GetError());
                return true;
            }
            SendToSession(*session, BuildResultMessage(envelope.id, result.Value()), frameId);
            return true;
        }
    };

    BridgeServer::BridgeServer(BridgeOptions options, BridgeHost host) : impl(std::make_unique<Impl>()) {
        impl->options = std::move(options);
        impl->host = std::move(host);
    }

    BridgeServer::~BridgeServer() = default;

    void BridgeServer::StartDocument(std::string url) {
        impl->documentUrl = std::move(url);
        impl->documentOrigin = OriginFromUrl(impl->documentUrl);
        impl->sessions.clear();
        impl->mainSession.reset();
        impl->handshakeAttempts = 0;
        impl->mainSession = impl->CreateSession(impl->documentOrigin, true, {});
        if (impl->mainSession) {
            impl->sessions[impl->mainSession->id] = impl->mainSession;
        }
    }

    void BridgeServer::CommitDocument(std::string url) {
        if (url.empty()) {
            return;
        }
        impl->documentUrl = std::move(url);
        if (impl->mainSession && !impl->mainSession->delivered) {
            impl->documentOrigin = OriginFromUrl(impl->documentUrl);
            impl->mainSession->origin = impl->documentOrigin;
        }
    }

    void BridgeServer::Reset() {
        impl->sessions.clear();
        impl->mainSession.reset();
        impl->pending.clear();
        impl->documentUrl.clear();
        impl->documentOrigin = "null";
        impl->handshakeAttempts = 0;
    }

    bool BridgeServer::HandleMessage(const std::string& rawMessage, const BridgeMessageContext& context) {
        if (!impl->options.enabled) {
            return false;
        }
        const BridgeParseResult parsed = ParseBridgeMessage(rawMessage, impl->options.maxMessageBytes);
        if (parsed.kind == BridgeMessageKind::RAW) {
            return false;
        }
        if (parsed.kind == BridgeMessageKind::MALFORMED) {
            return true;
        }
        if (parsed.kind == BridgeMessageKind::HELLO) {
            impl->HandleHello(context);
            return true;
        }
        return impl->HandleSigned(parsed.envelope, context);
    }

    Result<void> BridgeServer::Emit(std::string_view eventName, const JSON& payload) {
        if (!impl->options.enabled) {
            return Error(ErrorCode::NOT_SUPPORTED, "The JavaScript bridge is disabled for this window");
        }
        const std::string name(eventName);
        if (!IsValidName(name)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Bridge event name is invalid");
        }
        if (!ValidateJsonTree(payload, impl->options.maxJsonDepth, impl->options.maxJsonNodes)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Bridge event payload exceeds the complexity limit");
        }
        const std::string message = BuildEventMessage(name, payload);
        if (message.size() > impl->options.maxMessageBytes) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Bridge event payload exceeds the size limit");
        }
        if (impl->mainSession && impl->mainSession->delivered) {
            if (impl->host.postToMain) {
                impl->host.postToMain(message);
            }
            return Result<void>::Ok();
        }
        if (impl->pending.size() >= impl->options.maxPendingEvents) {
            return Error(ErrorCode::BRIDGE_REJECTED, "Bridge pending event queue is full");
        }
        const std::string origin = impl->documentUrl.empty() ? std::string() : impl->documentOrigin;
        impl->pending.push_back(Impl::PendingEvent{message, origin});
        return Result<void>::Ok();
    }

    void BridgeServer::UpdateClientConfig(const JSON& config) {
        if (!impl->options.enabled || !impl->mainSession || !impl->mainSession->delivered) {
            return;
        }
        JSON message;
        message["flare"] = 1;
        message["v"] = BridgeProtocolVersion;
        message["type"] = "config";
        message["config"] = config;
        if (impl->host.postToMain) {
            impl->host.postToMain(SafeJsonDump(message));
        }
    }

    bool BridgeServer::IsHandshaken() const {
        return impl->mainSession && impl->mainSession->delivered;
    }

    bool BridgeServer::IsEnabled() const {
        return impl->options.enabled;
    }

    const BridgeOptions& BridgeServer::Options() const {
        return impl->options;
    }

    std::string BridgeServer::MainOrigin() const {
        return impl->documentOrigin;
    }
}
