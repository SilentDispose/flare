#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "flare/error.h"
#include "flare/json.h"

namespace Flare {
    enum class WindowAction {
        SHOW,
        HIDE,
        MINIMIZE,
        MAXIMIZE,
        RESTORE,
        CLOSE,
        FOCUS,
        SET_TITLE,
        SET_SIZE,
        SET_POSITION,
        SET_OPACITY,
        SET_FULLSCREEN,
        SET_ALWAYS_ON_TOP,
        CENTER,
        GET_BOUNDS,
        IS_MAXIMIZED,
        IS_MINIMIZED,
        IS_VISIBLE,
        IS_FOCUSED,
        START_DRAG
    };

    [[nodiscard]] FLARE_API std::string_view WindowActionName(WindowAction action) noexcept;
    [[nodiscard]] FLARE_API std::optional<WindowAction> ParseWindowAction(std::string_view name) noexcept;
    [[nodiscard]] FLARE_API std::set<WindowAction> AllWindowActions();

    struct BridgeOptions {
        bool enabled = true;
        bool hardenedMode = false;
        bool allowIframes = false;
        std::vector<std::string> allowedOrigins;
        std::set<WindowAction> allowedWindowActions = AllWindowActions();
        std::set<std::string> allowedHandlerNames;
        std::set<std::string> grantedPermissions;
        std::size_t maxMessageBytes = 1024 * 1024;
        std::size_t maxPayloadBytes = 512 * 1024;
        double maxMessagesPerSecond = 120.0;
        std::size_t maxBurst = 240;
        int invocationTimeoutMs = 15000;
        int clockSkewSeconds = 300;
        std::size_t maxPendingEvents = 256;
        std::size_t maxJsonDepth = 32;
        std::size_t maxJsonNodes = 100000;
        std::size_t maxHandshakeAttemptsPerDocument = 16;
    };

    struct HandlerOptions {
        std::string permission;
        std::vector<std::string> allowedOrigins;
    };

    using RpcHandler = std::function<Result<JSON>(const JSON&)>;

    struct HandlerRegistration {
        std::string name;
        RpcHandler handler;
        HandlerOptions options;
    };

    [[nodiscard]] FLARE_API Result<void> ValidateBridgeOptions(const BridgeOptions& options);
}
