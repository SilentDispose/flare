#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flare/json.h"

#include "flare/app.h"
#include "flare/bridge-options.h"
#include "flare/browser-options.h"
#include "flare/capabilities.h"
#include "flare/error.h"
#include "flare/events.h"
#include "flare/geometry.h"
#include "flare/icon.h"
#include "flare/input.h"
#include "flare/window-options.h"

namespace Flare {
    struct NavigationDecision {
        bool allow = true;
        std::string redirectUrl;
    };

    struct PermissionDecision {
        bool allow = false;
        bool remember = false;
    };

    struct DownloadStartDecision {
        bool cancel = false;
        std::filesystem::path path;
    };

    struct ScriptDialogDecision {
        bool handled = false;
        bool accepted = true;
        std::string text;
    };

    struct ResourceRequestDecision {
        bool cancel = false;
        std::string redirectUrl;
    };

    struct AuthenticationDecision {
        bool useDefault = true;
        bool cancel = false;
        std::string username;
        std::string password;
    };

    struct ContextMenuDetails {
        double x = 0.0;
        double y = 0.0;
        std::string linkUrl;
        std::string sourceUrl;
        std::string selectionText;
        bool isEditable = false;
        bool isImage = false;
        bool isVideo = false;
    };

    struct AuthenticationInfo {
        std::string url;
        std::string host;
        int port = 0;
        std::string realm;
        std::string scheme;
        bool isProxy = false;
    };

    class WindowBackendDelegate {
    public:
        virtual ~WindowBackendDelegate() = default;

        virtual void OnBackendCreated() = 0;
        virtual void OnBackendBrowserReady() = 0;
        virtual void OnBackendBrowserCreationFailed(const Error& error) = 0;
        virtual void OnBackendVisibilityChanged(bool visible) = 0;
        virtual bool OnBackendCloseRequested() = 0;
        virtual void OnBackendClosed() = 0;
        virtual void OnBackendBoundsChanged(const Rect& bounds, bool maximized) = 0;
        virtual void OnBackendMinimized() = 0;
        virtual void OnBackendMaximized() = 0;
        virtual void OnBackendRestored() = 0;
        virtual void OnBackendFocusChanged(bool focused) = 0;
        virtual void OnBackendFullscreenChanged(bool fullscreen) = 0;
        virtual void OnBackendDpiChanged(double scaleFactor, const Rect& bounds) = 0;
        virtual void OnBackendThemeChanged(Theme theme, Theme systemTheme) = 0;
        virtual void OnBackendFileDrop(std::vector<std::filesystem::path> files, Point position) = 0;
        virtual bool OnBackendKey(bool down, const std::string& key, const std::string& code, int virtualKey,
            Modifiers modifiers, bool repeat) = 0;
        virtual bool OnBackendMouse(MouseEventType type, double x, double y, MouseButton button, MouseButtons buttons,
            Modifiers modifiers, int clickCount) = 0;
        virtual bool OnBackendWheel(double x, double y, double deltaX, double deltaY, Modifiers modifiers) = 0;
        virtual NavigationDecision OnBackendNavigationStarting(const std::string& url, const std::string& method,
            bool isRedirect, bool isTopLevel, bool isSameDocument, bool isUserInitiated) = 0;
        virtual void OnBackendNavigationCompleted(
            const std::string& url, bool success, int statusCode, const Error& error) = 0;
        virtual void OnBackendSourceChanged(const std::string& url, bool isNewDocument, bool isMainFrame) = 0;
        virtual void OnBackendContentLoading(bool isErrorPage, bool isMainFrame) = 0;
        virtual void OnBackendDomContentLoaded(bool isMainFrame) = 0;
        virtual void OnBackendDocumentTitleChanged(const std::string& title) = 0;
        virtual void OnBackendFaviconChanged(const std::string& url) = 0;
        virtual void OnBackendNewWindowRequested(const std::string& uri, bool isUserInitiated) = 0;
        virtual PermissionDecision OnBackendPermissionRequested(
            PermissionKind kind, const std::string& origin, const std::string& url) = 0;
        virtual DownloadStartDecision OnBackendDownloadStarting(const DownloadInfo& info) = 0;
        virtual void OnBackendDownloadProgress(const DownloadInfo& info) = 0;
        virtual void OnBackendDownloadCompleted(const DownloadInfo& info, const Error& error) = 0;
        virtual ScriptDialogDecision OnBackendScriptDialog(
            ScriptDialogType type, const std::string& message, const std::string& defaultText) = 0;
        virtual bool OnBackendContextMenu(const ContextMenuDetails& details) = 0;
        virtual void OnBackendProcessFailed(const std::string& reason, int exitCode, bool crashed) = 0;
        virtual ResourceRequestDecision OnBackendResourceRequested(const std::string& url, const std::string& method,
            ResourceType type, bool isMainFrame, const HttpHeaderMap& headers) = 0;
        virtual void OnBackendResourceResponse(
            const std::string& url, int statusCode, const std::string& mimeType, const HttpHeaderMap& headers) = 0;
        virtual void OnBackendWebMessage(const std::string& messageJson, const std::string& sourceUrl, bool isMainFrame,
            const std::string& frameId) = 0;
        virtual bool OnBackendFullscreenRequested(bool enter) = 0;
        virtual void OnBackendZoomChanged(double zoomFactor) = 0;
        virtual AuthenticationDecision OnBackendAuthenticationRequested(const AuthenticationInfo& info) = 0;
        virtual bool OnBackendCertificateError(const std::string& url, const std::string& errorText) = 0;
    };

    class WindowBackend {
    public:
        virtual ~WindowBackend() = default;

        virtual Result<void> Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
            WindowBackendDelegate& delegate, void* parentHandle) = 0;
        virtual void Close() = 0;
        virtual void* GetNativeHandle() const = 0;
        virtual const PlatformCapabilities& Capabilities() const = 0;

        virtual Result<void> Show() = 0;
        virtual Result<void> Hide() = 0;
        virtual Result<void> Minimize() = 0;
        virtual Result<void> Maximize() = 0;
        virtual Result<void> Restore() = 0;
        virtual Result<void> Focus() = 0;
        virtual Result<void> StartDrag() = 0;
        virtual Result<void> SetTitle(const std::string& title) = 0;
        virtual Result<std::string> GetTitle() const = 0;
        virtual Result<void> SetSize(int width, int height) = 0;
        virtual Result<void> SetPosition(int x, int y) = 0;
        virtual Result<void> SetMinSize(int width, int height) = 0;
        virtual Result<void> SetMaxSize(int width, int height) = 0;
        virtual Result<void> SetFullscreen(bool fullscreen) = 0;
        virtual Result<void> SetAlwaysOnTop(bool alwaysOnTop) = 0;
        virtual Result<void> SetOpacity(double opacity) = 0;
        virtual Result<double> GetOpacity() const = 0;
        virtual Result<void> Center() = 0;
        virtual Result<void> SetBackgroundColor(Color color) = 0;
        virtual Result<void> SetTheme(Theme theme) = 0;
        virtual Result<void> SetContentProtection(bool enabled) = 0;
        virtual Result<void> SetResizable(bool resizable) = 0;
        virtual Result<void> SetMovable(bool movable) = 0;
        virtual Result<void> SetMinimizable(bool minimizable) = 0;
        virtual Result<void> SetMaximizable(bool maximizable) = 0;
        virtual Result<void> SetClosable(bool closable) = 0;
        virtual Result<void> SetShowInTaskbar(bool show) = 0;
        virtual Result<void> SetIcon(const Icon& icon) = 0;
        virtual Result<void> SetDraggableRegions(const std::vector<Rect>& regions) = 0;
        virtual Result<Rect> GetBounds() const = 0;
        virtual bool IsVisible() const = 0;
        virtual bool IsMaximized() const = 0;
        virtual bool IsMinimized() const = 0;
        virtual bool IsFocused() const = 0;
        virtual bool IsFullscreen() const = 0;
        virtual double GetDpiScale() const = 0;

        virtual Result<void> Navigate(const std::string& url) = 0;
        virtual Result<void> NavigateToString(const std::string& html, const std::string& baseUrl) = 0;
        virtual Result<void> LoadFile(const std::filesystem::path& file) = 0;
        virtual Result<void> Reload() = 0;
        virtual Result<void> GoBack() = 0;
        virtual Result<void> GoForward() = 0;
        virtual Result<void> Stop() = 0;
        virtual Result<void> ExecuteScript(const std::string& script, ResultCallback<JSON> callback) = 0;
        virtual Result<void> OpenDevTools() = 0;
        virtual Result<void> CloseDevTools() = 0;
        virtual Result<void> PrintToPdf(
            const PdfOptions& options, const std::filesystem::path& outputPath, CompletionCallback callback) = 0;
        virtual Result<void> Print() = 0;
        virtual Result<void> CapturePreview(
            const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) = 0;
        virtual Result<void> GetCookies(
            const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) = 0;
        virtual Result<void> SetCookie(const Cookie& cookie, CompletionCallback callback) = 0;
        virtual Result<void> DeleteCookie(
            const std::string& url, const std::string& name, CompletionCallback callback) = 0;
        virtual Result<void> ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) = 0;
        virtual Result<void> SetZoom(double factor) = 0;
        virtual Result<double> GetZoom() const = 0;
        virtual Result<void> FindInPage(
            const std::string& text, const FindOptions& options, std::function<void(Result<FindResult>)> callback) = 0;
        virtual Result<void> StopFindInPage() = 0;
        virtual Result<std::string> GetUrl() const = 0;
        virtual Result<std::string> GetBrowserTitle() const = 0;
        virtual Result<void> SendWebMessage(const JSON& message) = 0;
        virtual Result<void> SetUserAgent(const std::string& userAgent) = 0;
        virtual Result<std::string> GetUserAgent() const = 0;
        virtual void PostWebMessage(const std::string& message) = 0;
        virtual void PostWebMessageToFrame(const std::string& message, const std::string& frameId) = 0;
    };

    class AppBackend {
    public:
        virtual ~AppBackend() = default;

        virtual Result<void> Initialize(const AppOptions& options) = 0;
        virtual Result<void> Run() = 0;
        virtual void Stop() = 0;
        virtual void Dispatch(std::function<void()> task) = 0;
        virtual bool IsOnUiThread() const = 0;
        virtual const PlatformCapabilities& Capabilities() const = 0;
    };

    std::unique_ptr<AppBackend> CreatePlatformAppBackend();
    std::unique_ptr<WindowBackend> CreatePlatformWindowBackend();
}
