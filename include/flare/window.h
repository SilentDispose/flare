#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "flare/json.h"

#include "flare/bridge-options.h"
#include "flare/browser-options.h"
#include "flare/capabilities.h"
#include "flare/error.h"
#include "flare/event.h"
#include "flare/events.h"
#include "flare/geometry.h"
#include "flare/window-options.h"

namespace Flare {
    class App;
    struct AppRuntime;
    class WindowGroup;

    class FLARE_API Window {
    public:
        ~Window();
        Window(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(const Window&) = delete;
        Window& operator=(Window&&) = delete;

        [[nodiscard]] WindowId GetId() const noexcept;
        [[nodiscard]] App& GetApp() const;
        [[nodiscard]] const WindowOptions& Options() const noexcept;
        [[nodiscard]] const BrowserOptions& BrowserSettings() const noexcept;
        [[nodiscard]] const BridgeOptions& BridgeSettings() const noexcept;
        [[nodiscard]] const PlatformCapabilities& Capabilities() const noexcept;

        Result<void> Show();
        Result<void> Hide();
        Result<void> Minimize();
        Result<void> Maximize();
        Result<void> Restore();
        Result<void> Close();
        Result<void> Focus();
        Result<void> StartDrag();

        Result<void> SetTitle(std::string title);
        Result<std::string> GetTitle() const;
        Result<void> SetSize(int width, int height);
        Result<void> SetPosition(int x, int y);
        Result<void> SetMinSize(int width, int height);
        Result<void> SetMaxSize(int width, int height);
        Result<void> SetFullscreen(bool fullscreen);
        Result<void> SetAlwaysOnTop(bool alwaysOnTop);
        Result<void> SetOpacity(double opacity);
        Result<double> GetOpacity() const;
        Result<void> Center();
        Result<void> SetBackgroundColor(Color color);
        Result<void> SetTheme(Theme theme);
        Result<void> SetContentProtection(bool enabled);
        Result<void> SetResizable(bool resizable);
        Result<void> SetMovable(bool movable);
        Result<void> SetMinimizable(bool minimizable);
        Result<void> SetMaximizable(bool maximizable);
        Result<void> SetClosable(bool closable);
        Result<void> SetShowInTaskbar(bool show);
        Result<void> SetIcon(Icon iconValue);
        Result<void> SetDraggableRegions(std::vector<Rect> regions);

        [[nodiscard]] Result<Rect> GetBounds() const;
        [[nodiscard]] bool IsVisible() const;
        [[nodiscard]] bool IsWindowMaximized() const;
        [[nodiscard]] bool IsWindowMinimized() const;
        [[nodiscard]] bool IsFocused() const;
        [[nodiscard]] bool IsFullscreen() const;
        [[nodiscard]] bool IsBrowserReady() const;
        [[nodiscard]] bool IsClosed() const;
        [[nodiscard]] Result<double> GetDpiScale() const;
        [[nodiscard]] Result<WindowState> GetState() const;

        Result<void> Navigate(std::string url);
        Result<void> NavigateToString(std::string html, std::string baseUrl = {});
        Result<void> LoadFile(std::filesystem::path file);
        Result<void> Reload();
        Result<void> GoBack();
        Result<void> GoForward();
        Result<void> Stop();
        Result<void> ExecuteScript(std::string script, ResultCallback<JSON> callback = {});
        Result<void> OpenDevTools();
        Result<void> CloseDevTools();
        Result<void> PrintToPdf(
            const PdfOptions& options, std::filesystem::path outputPath, CompletionCallback callback = {});
        Result<void> Print();
        Result<void> CapturePreview(
            const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback);
        Result<void> GetCookies(std::string url, std::function<void(Result<std::vector<Cookie>>)> callback);
        Result<void> SetCookie(Cookie cookie, CompletionCallback callback = {});
        Result<void> DeleteCookie(std::string url, std::string name, CompletionCallback callback = {});
        Result<void> ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback = {});
        Result<void> SetZoom(double factor);
        Result<double> GetZoom() const;
        Result<void> FindInPage(
            std::string text, FindOptions options = {}, std::function<void(Result<FindResult>)> callback = {});
        Result<void> StopFindInPage();
        Result<std::string> GetUrl() const;
        Result<std::string> GetBrowserTitle() const;
        Result<void> SendWebMessage(const JSON& message = JSON::object());
        Result<void> SetUserAgent(std::string userAgent);
        Result<std::string> GetUserAgent() const;

        Result<void> Emit(std::string_view eventName, const JSON& payload = JSON::object());
        Subscription On(std::string_view eventName, std::function<void(const JSON&)> handler);
        bool Off(std::string_view eventName);
        Result<void> Handle(std::string name, RpcHandler handler, HandlerOptions options = {});
        bool Unhandle(std::string_view name);

        Result<void> JoinGroup(std::string_view groupName);
        void LeaveGroup(std::string_view groupName);
        [[nodiscard]] std::vector<std::string> Groups() const;

        Subscription OnCreated(std::function<void(Window&)> handler);
        Subscription OnBrowserReady(std::function<void(Window&)> handler);
        Subscription OnBrowserCreationFailed(std::function<void(const BrowserCreationFailedEvent&)> handler);
        Subscription OnShow(std::function<void(Window&)> handler);
        Subscription OnHide(std::function<void(Window&)> handler);
        Subscription OnClose(std::function<void(CloseEvent&)> handler);
        Subscription OnClosed(std::function<void(Window&)> handler);
        Subscription OnMove(std::function<void(const BoundsEvent&)> handler);
        Subscription OnResize(std::function<void(const ResizeEvent&)> handler);
        Subscription OnMinimize(std::function<void(Window&)> handler);
        Subscription OnMaximize(std::function<void(Window&)> handler);
        Subscription OnRestore(std::function<void(Window&)> handler);
        Subscription OnFocus(std::function<void(Window&)> handler);
        Subscription OnBlur(std::function<void(Window&)> handler);
        Subscription OnFullscreenEnter(std::function<void(Window&)> handler);
        Subscription OnFullscreenLeave(std::function<void(Window&)> handler);
        Subscription OnDpiChanged(std::function<void(const DpiChangedEvent&)> handler);
        Subscription OnThemeChanged(std::function<void(const ThemeChangedEvent&)> handler);
        Subscription OnFileDrop(std::function<void(const FileDropEvent&)> handler);
        Subscription OnKeyDown(std::function<void(const KeyEvent&)> handler);
        Subscription OnKeyUp(std::function<void(const KeyEvent&)> handler);
        Subscription OnMouseDown(std::function<void(const MouseEvent&)> handler);
        Subscription OnMouseUp(std::function<void(const MouseEvent&)> handler);
        Subscription OnMouseMove(std::function<void(const MouseEvent&)> handler);
        Subscription OnMouseWheel(std::function<void(const WheelEvent&)> handler);
        Subscription OnDoubleClick(std::function<void(const MouseEvent&)> handler);
        Subscription OnNavigationStarting(std::function<void(NavigationStartingEvent&)> handler);
        Subscription OnNavigationCompleted(std::function<void(const NavigationCompletedEvent&)> handler);
        Subscription OnSourceChanged(std::function<void(const SourceChangedEvent&)> handler);
        Subscription OnDocumentTitleChanged(std::function<void(const DocumentTitleEvent&)> handler);
        Subscription OnFaviconChanged(std::function<void(const FaviconEvent&)> handler);
        Subscription OnContentLoading(std::function<void(const ContentLoadingEvent&)> handler);
        Subscription OnDomContentLoaded(std::function<void(Window&)> handler);
        Subscription OnNewWindowRequested(std::function<void(NewWindowRequestedEvent&)> handler);
        Subscription OnPermissionRequested(std::function<void(PermissionRequestEvent&)> handler);
        Subscription OnDownloadStarting(std::function<void(DownloadStartingEvent&)> handler);
        Subscription OnDownloadProgress(std::function<void(const DownloadProgressEvent&)> handler);
        Subscription OnDownloadCompleted(std::function<void(const DownloadCompletedEvent&)> handler);
        Subscription OnScriptDialog(std::function<void(ScriptDialogEvent&)> handler);
        Subscription OnContextMenuRequested(std::function<void(ContextMenuEvent&)> handler);
        Subscription OnProcessFailed(std::function<void(const ProcessFailedEvent&)> handler);
        Subscription OnWebResourceRequested(std::function<void(WebResourceRequestEvent&)> handler);
        Subscription OnWebResourceResponseReceived(std::function<void(const WebResourceResponseEvent&)> handler);
        Subscription OnWebMessageReceived(std::function<void(const WebMessageEvent&)> handler);
        Subscription OnFullscreenRequested(std::function<void(FullscreenRequestEvent&)> handler);
        Subscription OnZoomChanged(std::function<void(const ZoomChangedEvent&)> handler);
        Subscription OnAuthenticationRequested(std::function<void(AuthenticationRequestEvent&)> handler);
        Subscription OnCertificateError(std::function<void(CertificateErrorEvent&)> handler);

    private:
        struct Impl;

        explicit Window(std::unique_ptr<Impl> windowImpl);

        static Result<std::shared_ptr<Window>> CreateInternal(std::shared_ptr<AppRuntime> runtime, App& app,
            WindowId id, const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
            const BridgeOptions& bridgeOptions);
        void FinalizeClosed();
        void ShutdownInternal();

        friend class App;
        friend struct AppRuntime;
        friend class WindowGroup;

        std::unique_ptr<Impl> impl;
    };
}
