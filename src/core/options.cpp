#include "flare/bridge-options.h"
#include "flare/browser-options.h"
#include "flare/window-options.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include "core/text-utils.h"
#include "core/url-utils.h"

namespace Flare {
    namespace {
        Result<void> Invalid(std::string message) {
            return Error(ErrorCode::INVALID_ARGUMENT, std::move(message));
        }

        bool IsKnownShowTrigger(ShowTrigger trigger) {
            switch (trigger) {
                case ShowTrigger::BROWSER_CREATED:
                case ShowTrigger::FIRST_NAVIGATION_COMPLETED:
                case ShowTrigger::DOM_CONTENT_LOADED:
                case ShowTrigger::MANUAL:
                    return true;
            }
            return false;
        }

        bool IsKnownTitleBarStyle(TitleBarStyle style) {
            switch (style) {
                case TitleBarStyle::NATIVE:
                case TitleBarStyle::CUSTOM:
                case TitleBarStyle::HIDDEN:
                    return true;
            }
            return false;
        }

        bool IsKnownTheme(Theme theme) {
            switch (theme) {
                case Theme::SYSTEM:
                case Theme::LIGHT:
                case Theme::DARK:
                    return true;
            }
            return false;
        }

        bool IsKnownPermissionState(PermissionState state) {
            switch (state) {
                case PermissionState::ASK:
                case PermissionState::ALLOW:
                case PermissionState::DENY:
                    return true;
            }
            return false;
        }

        bool IsValidEmbeddedBundleName(const std::string& name) {
            if (name.empty() || name.size() > 256 || name == "." || name == "..") {
                return false;
            }
            for (char character : name) {
                const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '-' || character == '_'
                    || character == '.';
                if (!valid) {
                    return false;
                }
            }
            return true;
        }

        bool IsSafeRelativeAssetPath(const std::string& text) {
            if (text.empty() || text.find('\0') != std::string::npos) {
                return false;
            }
            const std::filesystem::path path{text};
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& component : path) {
                if (component == "..") {
                    return false;
                }
            }
            return true;
        }

        bool IsValidSchemeName(const std::string& scheme) {
            if (scheme.empty() || scheme.size() > 32) {
                return false;
            }
            const auto isAlpha = [](char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
            };
            if (!isAlpha(scheme.front())) {
                return false;
            }
            for (char character : scheme) {
                const bool valid = isAlpha(character) || (character >= '0' && character <= '9') || character == '+'
                    || character == '-' || character == '.';
                if (!valid) {
                    return false;
                }
            }
            return true;
        }

        bool IsReservedScheme(const std::string& scheme) {
            static const std::unordered_set<std::string> reserved = {"http", "https", "file", "about", "data",
                "javascript", "blob", "ws", "wss", "ftp", "chrome", "devtools", "mailto", "tel"};
            return reserved.find(ToLowerAscii(scheme)) != reserved.end();
        }

        bool IsValidHostName(const std::string& host) {
            if (host.empty() || host.size() > 253) {
                return false;
            }
            for (char character : host) {
                const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '.' || character == '-';
                if (!valid) {
                    return false;
                }
            }
            return host.front() != '.' && host.back() != '.' && host.front() != '-';
        }

        bool IsHeaderNameValid(const std::string& name) {
            if (name.empty() || name.size() > 256) {
                return false;
            }
            for (char character : name) {
                const bool token = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '!' || character == '#'
                    || character == '$' || character == '%' || character == '&' || character == '\'' || character == '*'
                    || character == '+' || character == '-' || character == '.' || character == '^' || character == '_'
                    || character == '`' || character == '|' || character == '~';
                if (!token) {
                    return false;
                }
            }
            return true;
        }

        bool IsHeaderValueValid(const std::string& value) {
            if (value.size() > 8192) {
                return false;
            }
            return value.find('\r') == std::string::npos && value.find('\n') == std::string::npos
                && value.find('\0') == std::string::npos;
        }

        Result<void> ValidatePatternList(const std::vector<std::string>& patterns, const char* label) {
            if (patterns.size() > 128) {
                return Invalid(std::string(label) + " contains too many patterns");
            }
            for (const std::string& pattern : patterns) {
                if (pattern.empty() || pattern.size() > 2048) {
                    return Invalid(std::string(label) + " contains an invalid pattern");
                }
                for (unsigned char byte : pattern) {
                    if (byte < 0x20 || byte == 0x7F) {
                        return Invalid(std::string(label) + " contains a control character");
                    }
                }
            }
            return Result<void>::Ok();
        }
    }

    Result<void> ValidateWindowOptions(const WindowOptions& options) {
        if (options.width <= 0 || options.height <= 0 || options.width > 32767 || options.height > 32767) {
            return Invalid("Window width and height must be between 1 and 32767");
        }
        if (options.minWidth < 0 || options.minHeight < 0 || options.maxWidth < 0 || options.maxHeight < 0
            || options.minWidth > 32767 || options.minHeight > 32767 || options.maxWidth > 32767
            || options.maxHeight > 32767) {
            return Invalid("Window size constraints are out of range");
        }
        if (options.minWidth > 0 && options.maxWidth > 0 && options.minWidth > options.maxWidth) {
            return Invalid("Minimum width exceeds maximum width");
        }
        if (options.minHeight > 0 && options.maxHeight > 0 && options.minHeight > options.maxHeight) {
            return Invalid("Minimum height exceeds maximum height");
        }
        if (options.minWidth > 0 && options.width < options.minWidth) {
            return Invalid("Initial width is below the minimum width");
        }
        if (options.minHeight > 0 && options.height < options.minHeight) {
            return Invalid("Initial height is below the minimum height");
        }
        if (options.maxWidth > 0 && options.width > options.maxWidth) {
            return Invalid("Initial width exceeds the maximum width");
        }
        if (options.maxHeight > 0 && options.height > options.maxHeight) {
            return Invalid("Initial height exceeds the maximum height");
        }
        if (options.x < -1000000 || options.x > 1000000 || options.y < -1000000 || options.y > 1000000) {
            return Invalid("Window position is out of range");
        }
        if (options.opacity < 0.0 || options.opacity > 1.0) {
            return Invalid("Window opacity must be between 0 and 1");
        }
        if (options.title.size() > 4096 || options.title.find('\0') != std::string::npos) {
            return Invalid("Window title is invalid");
        }
        if (!IsKnownShowTrigger(options.showTrigger)) {
            return Invalid("Unknown show trigger");
        }
        if (!IsKnownTitleBarStyle(options.titleBarStyle)) {
            return Invalid("Unknown title bar style");
        }
        if (!IsKnownTheme(options.theme)) {
            return Invalid("Unknown theme");
        }
        if (options.stateKey.size() > 128) {
            return Invalid("Window state key is too long");
        }
        if (options.draggableRegions.size() > 64) {
            return Invalid("Too many draggable regions");
        }
        for (const Rect& region : options.draggableRegions) {
            if (region.width <= 0.0 || region.height <= 0.0 || region.x < -32768.0 || region.y < -32768.0
                || region.x > 32768.0 || region.y > 32768.0 || region.width > 32768.0 || region.height > 32768.0) {
                return Invalid("Draggable region is out of range");
            }
        }
        return Result<void>::Ok();
    }

    Result<void> ValidateBrowserOptions(const BrowserOptions& options) {
        if (!options.embeddedBundle.empty() && !IsValidEmbeddedBundleName(options.embeddedBundle)) {
            return Invalid("Embedded web bundle name is invalid");
        }
        if (!options.embeddedBundle.empty() && !options.embeddedStartPage.empty()
            && !IsSafeRelativeAssetPath(options.embeddedStartPage)) {
            return Invalid("Embedded web start page must be a safe relative path");
        }
        int initialContent = 0;
        if (!options.startUrl.empty()) {
            ++initialContent;
        }
        if (!options.html.empty()) {
            ++initialContent;
        }
        if (!options.localFile.empty()) {
            ++initialContent;
        }
        if (initialContent > 1) {
            return Invalid("Only one of startUrl, html, and localFile may be set");
        }
        if (!options.localFile.empty()) {
            std::error_code error;
            if (!std::filesystem::exists(options.localFile, error) || error
                || !std::filesystem::is_regular_file(options.localFile, error)) {
                return Invalid("Local file does not exist");
            }
        }
        if (options.zoomFactor < 0.25 || options.zoomFactor > 5.0) {
            return Invalid("Browser zoom factor must be between 0.25 and 5.0");
        }
        if (options.userAgent.size() > 2048 || options.userAgent.find('\0') != std::string::npos) {
            return Invalid("User agent string is invalid");
        }
        if (options.language.size() > 35) {
            return Invalid("Language tag is too long");
        }
        for (char character : options.language) {
            const bool valid = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9') || character == '-' || character == '_';
            if (!valid) {
                return Invalid("Language tag contains invalid characters");
            }
        }
        if (!IsKnownPermissionState(options.permissionPolicy.camera)
            || !IsKnownPermissionState(options.permissionPolicy.microphone)
            || !IsKnownPermissionState(options.permissionPolicy.geolocation)
            || !IsKnownPermissionState(options.permissionPolicy.notifications)
            || !IsKnownPermissionState(options.permissionPolicy.clipboardRead)
            || !IsKnownPermissionState(options.permissionPolicy.clipboardWrite)) {
            return Invalid("Permission policy contains an unknown state");
        }
        if (!options.proxy.server.empty() && options.proxy.server.size() > 1024) {
            return Invalid("Proxy server address is too long");
        }
        if (options.proxy.enabled && options.proxy.server.empty() && options.proxy.pacUrl.empty()) {
            return Invalid("Proxy is enabled but no server or PAC URL is configured");
        }
        if (options.proxy.bypassList.size() > 128) {
            return Invalid("Proxy bypass list is too large");
        }
        for (const std::string& entry : options.proxy.bypassList) {
            if (entry.empty() || entry.size() > 512) {
                return Invalid("Proxy bypass entry is invalid");
            }
        }
        if (options.browserArguments.size() > 64) {
            return Invalid("Too many browser arguments");
        }
        for (const std::string& argument : options.browserArguments) {
            if (argument.empty() || argument.size() > 4096 || argument.find('\0') != std::string::npos) {
                return Invalid("Browser argument is invalid");
            }
        }
        if (options.customSchemes.size() > 32) {
            return Invalid("Too many custom schemes");
        }
        for (const auto& entry : options.customSchemes) {
            if (!IsValidSchemeName(entry.first) || IsReservedScheme(entry.first)) {
                return Invalid("Custom scheme name is invalid or reserved");
            }
            if (!entry.second) {
                return Invalid("Custom scheme handler is empty");
            }
        }
        if (options.virtualHost.has_value()) {
            if (!IsValidHostName(options.virtualHost->host)) {
                return Invalid("Virtual host name is invalid");
            }
            std::error_code error;
            if (options.virtualHost->folder.empty() || !std::filesystem::exists(options.virtualHost->folder, error)
                || error || !std::filesystem::is_directory(options.virtualHost->folder, error)) {
                return Invalid("Virtual host folder does not exist");
            }
            if (!IsValidSchemeName(options.virtualHost->scheme) || IsReservedScheme(options.virtualHost->scheme)) {
                return Invalid("Virtual host scheme is invalid or reserved");
            }
        }
        if (options.documentStartScripts.size() > 32 || options.documentEndScripts.size() > 32) {
            return Invalid("Too many injected scripts");
        }
        for (const std::string& script : options.documentStartScripts) {
            if (script.empty() || script.size() > 1024 * 1024) {
                return Invalid("Document start script is invalid");
            }
        }
        for (const std::string& script : options.documentEndScripts) {
            if (script.empty() || script.size() > 1024 * 1024) {
                return Invalid("Document end script is invalid");
            }
        }
        if (options.extraHeaders.size() > 64) {
            return Invalid("Too many extra HTTP headers");
        }
        for (const auto& entry : options.extraHeaders) {
            if (!IsHeaderNameValid(entry.first) || !IsHeaderValueValid(entry.second)) {
                return Invalid("Extra HTTP header is invalid");
            }
        }
        Result<void> patterns = ValidatePatternList(options.allowedUrlPatterns, "Allowed URL patterns");
        if (!patterns.IsOk()) {
            return patterns;
        }
        patterns = ValidatePatternList(options.blockedUrlPatterns, "Blocked URL patterns");
        if (!patterns.IsOk()) {
            return patterns;
        }
        patterns = ValidatePatternList(options.requestFilters, "Request filters");
        if (!patterns.IsOk()) {
            return patterns;
        }
        return ValidatePatternList(options.responseFilters, "Response filters");
    }

    Result<void> ValidateBridgeOptions(const BridgeOptions& options) {
        if (options.maxMessageBytes < 1024 || options.maxMessageBytes > 16 * 1024 * 1024) {
            return Invalid("Bridge maximum message size is out of range");
        }
        if (options.maxPayloadBytes == 0 || options.maxPayloadBytes > options.maxMessageBytes) {
            return Invalid("Bridge maximum payload size is out of range");
        }
        if (!(options.maxMessagesPerSecond > 0.0) || options.maxMessagesPerSecond > 100000.0) {
            return Invalid("Bridge rate limit is out of range");
        }
        if (options.maxBurst == 0 || options.maxBurst > 1000000) {
            return Invalid("Bridge burst limit is out of range");
        }
        if (options.invocationTimeoutMs < 100 || options.invocationTimeoutMs > 600000) {
            return Invalid("Bridge invocation timeout is out of range");
        }
        if (options.clockSkewSeconds < 0 || options.clockSkewSeconds > 3600) {
            return Invalid("Bridge clock skew allowance is out of range");
        }
        if (options.maxPendingEvents > 1000000) {
            return Invalid("Bridge pending event limit is out of range");
        }
        if (options.maxJsonDepth == 0 || options.maxJsonDepth > 128) {
            return Invalid("Bridge JSON depth limit is out of range");
        }
        if (options.maxJsonNodes == 0 || options.maxJsonNodes > 1000000) {
            return Invalid("Bridge JSON node limit is out of range");
        }
        if (options.maxHandshakeAttemptsPerDocument == 0 || options.maxHandshakeAttemptsPerDocument > 1000) {
            return Invalid("Bridge handshake attempt limit is out of range");
        }
        if (options.allowedOrigins.size() > 64) {
            return Invalid("Too many allowed bridge origins");
        }
        for (const std::string& origin : options.allowedOrigins) {
            if (!IsValidOriginPattern(origin)) {
                return Invalid("Allowed bridge origin is invalid");
            }
        }
        if (options.hardenedMode && options.allowedOrigins.empty()) {
            return Invalid("Hardened bridge mode requires an explicit allowedOrigins list");
        }
        if (options.allowedHandlerNames.size() > 256) {
            return Invalid("Too many allowed bridge handlers");
        }
        for (const std::string& name : options.allowedHandlerNames) {
            if (!IsValidName(name)) {
                return Invalid("Allowed bridge handler name is invalid");
            }
        }
        if (options.grantedPermissions.size() > 128) {
            return Invalid("Too many granted bridge permissions");
        }
        for (const std::string& permission : options.grantedPermissions) {
            if (permission.empty() || permission.size() > 64) {
                return Invalid("Granted bridge permission is invalid");
            }
            for (char character : permission) {
                if (std::isspace(static_cast<unsigned char>(character)) != 0) {
                    return Invalid("Granted bridge permission contains whitespace");
                }
            }
        }
        for (WindowAction action : options.allowedWindowActions) {
            if (WindowActionName(action) == "unknown") {
                return Invalid("Allowed window action is invalid");
            }
        }
        return Result<void>::Ok();
    }

    std::string WindowStateName(WindowState state) {
        switch (state) {
            case WindowState::NORMAL:
                return "normal";
            case WindowState::MINIMIZED:
                return "minimized";
            case WindowState::MAXIMIZED:
                return "maximized";
            case WindowState::FULLSCREEN:
                return "fullscreen";
        }
        return "unknown";
    }

    std::string ThemeName(Theme theme) {
        switch (theme) {
            case Theme::SYSTEM:
                return "system";
            case Theme::LIGHT:
                return "light";
            case Theme::DARK:
                return "dark";
        }
        return "unknown";
    }

    std::string_view WindowActionName(WindowAction action) noexcept {
        switch (action) {
            case WindowAction::SHOW:
                return "show";
            case WindowAction::HIDE:
                return "hide";
            case WindowAction::MINIMIZE:
                return "minimize";
            case WindowAction::MAXIMIZE:
                return "maximize";
            case WindowAction::RESTORE:
                return "restore";
            case WindowAction::CLOSE:
                return "close";
            case WindowAction::FOCUS:
                return "focus";
            case WindowAction::SET_TITLE:
                return "setTitle";
            case WindowAction::SET_SIZE:
                return "setSize";
            case WindowAction::SET_POSITION:
                return "setPosition";
            case WindowAction::SET_OPACITY:
                return "setOpacity";
            case WindowAction::SET_FULLSCREEN:
                return "setFullscreen";
            case WindowAction::SET_ALWAYS_ON_TOP:
                return "setAlwaysOnTop";
            case WindowAction::CENTER:
                return "center";
            case WindowAction::GET_BOUNDS:
                return "getBounds";
            case WindowAction::IS_MAXIMIZED:
                return "isMaximized";
            case WindowAction::IS_MINIMIZED:
                return "isMinimized";
            case WindowAction::IS_VISIBLE:
                return "isVisible";
            case WindowAction::IS_FOCUSED:
                return "isFocused";
            case WindowAction::START_DRAG:
                return "startDrag";
        }
        return "unknown";
    }

    std::optional<WindowAction> ParseWindowAction(std::string_view name) noexcept {
        for (WindowAction action : AllWindowActions()) {
            if (WindowActionName(action) == name) {
                return action;
            }
        }
        return std::nullopt;
    }

    std::set<WindowAction> AllWindowActions() {
        return {WindowAction::SHOW, WindowAction::HIDE, WindowAction::MINIMIZE, WindowAction::MAXIMIZE,
            WindowAction::RESTORE, WindowAction::CLOSE, WindowAction::FOCUS, WindowAction::SET_TITLE,
            WindowAction::SET_SIZE, WindowAction::SET_POSITION, WindowAction::SET_OPACITY, WindowAction::SET_FULLSCREEN,
            WindowAction::SET_ALWAYS_ON_TOP, WindowAction::CENTER, WindowAction::GET_BOUNDS, WindowAction::IS_MAXIMIZED,
            WindowAction::IS_MINIMIZED, WindowAction::IS_VISIBLE, WindowAction::IS_FOCUSED, WindowAction::START_DRAG};
    }

    ClearDataKind operator|(ClearDataKind left, ClearDataKind right) noexcept {
        return static_cast<ClearDataKind>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    ClearDataKind operator&(ClearDataKind left, ClearDataKind right) noexcept {
        return static_cast<ClearDataKind>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }

    bool HasClearDataKind(ClearDataKind value, ClearDataKind flag) noexcept {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
    }
}
