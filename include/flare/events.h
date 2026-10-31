#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "flare/json.h"

#include "flare/error.h"
#include "flare/geometry.h"
#include "flare/input.h"
#include "flare/window-options.h"

namespace Flare {
    class Window;

    enum class PermissionKind {
        CAMERA,
        MICROPHONE,
        GEOLOCATION,
        NOTIFICATIONS,
        CLIPBOARD_READ,
        CLIPBOARD_WRITE,
        AUTOPLAY,
        OTHER
    };

    enum class DownloadState { PENDING, IN_PROGRESS, COMPLETED, CANCELLED, INTERRUPTED };

    struct DownloadInfo {
        std::string id;
        std::string url;
        std::string suggestedFilename;
        std::string mimeType;
        std::filesystem::path path;
        std::int64_t totalBytes = -1;
        std::int64_t receivedBytes = 0;
        DownloadState state = DownloadState::PENDING;
    };

    enum class ScriptDialogType { ALERT, CONFIRM, PROMPT, BEFORE_UNLOAD };

    enum class ResourceType { OTHER, DOCUMENT, STYLESHEET, IMAGE, MEDIA, SCRIPT, FONT, XHR, FETCH, WEB_SOCKET };

    using HttpHeaderMap = std::map<std::string, std::string>;

    struct BrowserCreationFailedEvent {
        Window& window;
        Error error;
    };

    struct CloseEvent {
        Window& window;
        bool cancel = false;
    };

    struct BoundsEvent {
        Window& window;
        Rect bounds;
    };

    struct ResizeEvent {
        Window& window;
        Rect bounds;
        bool isMaximized = false;
    };

    struct DpiChangedEvent {
        Window& window;
        double scaleFactor = 1.0;
        Rect bounds;
    };

    struct ThemeChangedEvent {
        Window& window;
        Theme theme = Theme::SYSTEM;
        Theme systemTheme = Theme::LIGHT;
    };

    struct FileDropEvent {
        Window& window;
        std::vector<std::filesystem::path> files;
        Point position;
    };

    struct KeyEvent {
        Window& window;
        std::string key;
        std::string code;
        int virtualKey = 0;
        Modifiers modifiers = 0;
        bool isRepeat = false;
        bool cancel = false;
    };

    struct MouseEvent {
        Window& window;
        double x = 0.0;
        double y = 0.0;
        MouseButton button = MouseButton::NONE;
        MouseButtons buttons = 0;
        Modifiers modifiers = 0;
        int clickCount = 1;
        bool cancel = false;
    };

    struct WheelEvent {
        Window& window;
        double x = 0.0;
        double y = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
        Modifiers modifiers = 0;
        bool cancel = false;
    };

    struct NavigationStartingEvent {
        Window& window;
        std::string url;
        bool isRedirect = false;
        bool isTopLevel = true;
        bool isSameDocument = false;
        bool isUserInitiated = false;
        bool cancel = false;
        std::string redirectUrl;
    };

    struct NavigationCompletedEvent {
        Window& window;
        std::string url;
        bool isSuccess = false;
        int httpStatusCode = 0;
        Error error;
    };

    struct SourceChangedEvent {
        Window& window;
        std::string url;
        bool isNewDocument = true;
    };

    struct DocumentTitleEvent {
        Window& window;
        std::string title;
    };

    struct FaviconEvent {
        Window& window;
        std::string url;
    };

    struct ContentLoadingEvent {
        Window& window;
        bool isErrorPage = false;
    };

    struct NewWindowRequestedEvent {
        Window& window;
        std::string uri;
        bool isUserInitiated = false;
        bool handled = false;
    };

    struct PermissionRequestEvent {
        Window& window;
        PermissionKind kind = PermissionKind::OTHER;
        std::string origin;
        std::string url;
        bool allow = false;
        bool remember = false;
    };

    struct DownloadStartingEvent {
        Window& window;
        DownloadInfo info;
        std::filesystem::path savePath;
        bool cancel = false;
    };

    struct DownloadProgressEvent {
        Window& window;
        DownloadInfo info;
    };

    struct DownloadCompletedEvent {
        Window& window;
        DownloadInfo info;
        Error error;
    };

    struct ScriptDialogEvent {
        Window& window;
        ScriptDialogType type = ScriptDialogType::ALERT;
        std::string message;
        std::string defaultText;
        std::string resultText;
        bool handled = false;
        bool accepted = true;
    };

    struct ContextMenuEvent {
        Window& window;
        double x = 0.0;
        double y = 0.0;
        std::string linkUrl;
        std::string sourceUrl;
        std::string selectionText;
        bool isEditable = false;
        bool isImage = false;
        bool isVideo = false;
        bool cancel = false;
    };

    struct ProcessFailedEvent {
        Window& window;
        std::string reason;
        int exitCode = 0;
        bool crashed = false;
    };

    struct WebResourceRequestEvent {
        Window& window;
        std::string url;
        std::string method;
        ResourceType type = ResourceType::OTHER;
        bool isMainFrame = false;
        HttpHeaderMap headers;
        bool cancel = false;
        std::string redirectUrl;
    };

    struct WebResourceResponseEvent {
        Window& window;
        std::string url;
        int statusCode = 0;
        std::string mimeType;
        HttpHeaderMap headers;
    };

    struct WebMessageEvent {
        Window& window;
        JSON message;
        std::string sourceUrl;
        bool isMainFrame = true;
    };

    struct FullscreenRequestEvent {
        Window& window;
        bool enter = true;
        bool allow = true;
    };

    struct ZoomChangedEvent {
        Window& window;
        double zoomFactor = 1.0;
    };

    struct AuthenticationRequestEvent {
        Window& window;
        std::string url;
        std::string host;
        int port = 0;
        std::string realm;
        std::string scheme;
        bool isProxy = false;
        bool useDefault = true;
        bool cancel = false;
        std::string username;
        std::string password;
    };

    struct CertificateErrorEvent {
        Window& window;
        std::string url;
        std::string errorText;
        bool allow = false;
    };
}
