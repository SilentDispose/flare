#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "flare/json.h"

#include "bridge/bridge-protocol.h"
#include "flare/bridge-options.h"
#include "flare/error.h"

namespace Flare {
    struct BridgeMessageContext {
        std::string sourceUrl;
        bool isMainFrame = true;
        std::string frameId;
    };

    struct BridgeHost {
        std::string appName;
        std::string appVersion;
        std::string flareVersion;
        std::string platform;
        std::function<void(const std::string&)> postToMain;
        std::function<void(const std::string&, const std::string&)> postToFrame;
        std::function<Result<JSON>(
            const std::string& name, const JSON& payload, const std::string& origin, bool isMainFrame)>
            invokeHandler;
        std::function<Result<JSON>(WindowAction action, const JSON& payload)> performWindowAction;
        std::function<void(const std::string& name, const JSON& payload, bool isMainFrame)> deliverEvent;
        std::function<JSON()> clientConfig;
        std::function<void(const Error&)> reportError;
    };

    class BridgeServer {
    public:
        BridgeServer(BridgeOptions options, BridgeHost host);
        ~BridgeServer();
        BridgeServer(const BridgeServer&) = delete;
        BridgeServer(BridgeServer&&) = delete;
        BridgeServer& operator=(const BridgeServer&) = delete;
        BridgeServer& operator=(BridgeServer&&) = delete;

        void StartDocument(std::string url);
        void CommitDocument(std::string url);
        void Reset();
        bool HandleMessage(const std::string& rawMessage, const BridgeMessageContext& context);
        Result<void> Emit(std::string_view eventName, const JSON& payload);
        void UpdateClientConfig(const JSON& config);
        [[nodiscard]] bool IsHandshaken() const;
        [[nodiscard]] bool IsEnabled() const;
        [[nodiscard]] const BridgeOptions& Options() const;
        [[nodiscard]] std::string MainOrigin() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
