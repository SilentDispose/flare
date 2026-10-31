#pragma once

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/backend.h"

namespace Flare {
    class LinuxWindowBackend final : public WindowBackend {
    public:
        LinuxWindowBackend();
        ~LinuxWindowBackend() override;

        Result<void> Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
            WindowBackendDelegate& delegate, void* parentHandle) override;
        void Close() override;
        void* GetNativeHandle() const override;
        const PlatformCapabilities& Capabilities() const override;

        Result<void> Show() override;
        Result<void> Hide() override;
        Result<void> Minimize() override;
        Result<void> Maximize() override;
        Result<void> Restore() override;
        Result<void> Focus() override;
        Result<void> StartDrag() override;
        Result<void> SetTitle(const std::string& title) override;
        Result<std::string> GetTitle() const override;
        Result<void> SetSize(int width, int height) override;
        Result<void> SetPosition(int x, int y) override;
        Result<void> SetMinSize(int width, int height) override;
        Result<void> SetMaxSize(int width, int height) override;
        Result<void> SetFullscreen(bool fullscreen) override;
        Result<void> SetAlwaysOnTop(bool alwaysOnTop) override;
        Result<void> SetOpacity(double opacity) override;
        Result<double> GetOpacity() const override;
        Result<void> Center() override;
        Result<void> SetBackgroundColor(Color color) override;
        Result<void> SetTheme(Theme theme) override;
        Result<void> SetContentProtection(bool enabled) override;
        Result<void> SetResizable(bool resizable) override;
        Result<void> SetMovable(bool movable) override;
        Result<void> SetMinimizable(bool minimizable) override;
        Result<void> SetMaximizable(bool maximizable) override;
        Result<void> SetClosable(bool closable) override;
        Result<void> SetShowInTaskbar(bool show) override;
        Result<void> SetIcon(const Icon& icon) override;
        Result<void> SetDraggableRegions(const std::vector<Rect>& regions) override;
        Result<Rect> GetBounds() const override;
        bool IsVisible() const override;
        bool IsMaximized() const override;
        bool IsMinimized() const override;
        bool IsFocused() const override;
        bool IsFullscreen() const override;
        double GetDpiScale() const override;

        Result<void> Navigate(const std::string& url) override;
        Result<void> NavigateToString(const std::string& html, const std::string& baseUrl) override;
        Result<void> LoadFile(const std::filesystem::path& file) override;
        Result<void> Reload() override;
        Result<void> GoBack() override;
        Result<void> GoForward() override;
        Result<void> Stop() override;
        Result<void> ExecuteScript(const std::string& script, ResultCallback<JSON> callback) override;
        Result<void> OpenDevTools() override;
        Result<void> CloseDevTools() override;
        Result<void> PrintToPdf(
            const PdfOptions& options, const std::filesystem::path& outputPath, CompletionCallback callback) override;
        Result<void> Print() override;
        Result<void> CapturePreview(
            const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) override;
        Result<void> GetCookies(
            const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) override;
        Result<void> SetCookie(const Cookie& cookie, CompletionCallback callback) override;
        Result<void> DeleteCookie(
            const std::string& url, const std::string& name, CompletionCallback callback) override;
        Result<void> ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) override;
        Result<void> SetZoom(double factor) override;
        Result<double> GetZoom() const override;
        Result<void> FindInPage(const std::string& text, const FindOptions& options,
            std::function<void(Result<FindResult>)> callback) override;
        Result<void> StopFindInPage() override;
        Result<std::string> GetUrl() const override;
        Result<std::string> GetBrowserTitle() const override;
        Result<void> SendWebMessage(const JSON& message) override;
        Result<void> SetUserAgent(const std::string& userAgent) override;
        Result<std::string> GetUserAgent() const override;
        void PostWebMessage(const std::string& message) override;
        void PostWebMessageToFrame(const std::string& message, const std::string& frameId) override;

        void CreateWebView();

    private:
        static gboolean OnDeleteEvent(GtkWidget* widget, GdkEvent* event, gpointer data);
        static void OnDestroy(GtkWidget* widget, gpointer data);
        static void OnMap(GtkWidget* widget, gpointer data);
        static void OnUnmap(GtkWidget* widget, gpointer data);
        static gboolean OnConfigure(GtkWidget* widget, GdkEventConfigure* event, gpointer data);
        static gboolean OnWindowState(GtkWidget* widget, GdkEventWindowState* event, gpointer data);
        static gboolean OnKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer data);
        static gboolean OnKeyRelease(GtkWidget* widget, GdkEventKey* event, gpointer data);
        static gboolean OnButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer data);
        static gboolean OnButtonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data);
        static gboolean OnMotion(GtkWidget* widget, GdkEventMotion* event, gpointer data);
        static gboolean OnScroll(GtkWidget* widget, GdkEventScroll* event, gpointer data);
        static gboolean OnFocusIn(GtkWidget* widget, GdkEventFocus* event, gpointer data);
        static gboolean OnFocusOut(GtkWidget* widget, GdkEventFocus* event, gpointer data);
        static void OnDragDataReceived(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
            GtkSelectionData* selection, guint info, guint time, gpointer data);
        static void OnScaleChanged(GObject* object, GParamSpec* spec, gpointer data);
        static void OnThemeChanged(GObject* object, GParamSpec* spec, gpointer data);
        static gboolean OnDecidePolicy(
            WebKitWebView* webView, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer data);
        static void OnLoadChanged(WebKitWebView* webView, WebKitLoadEvent event, gpointer data);
        static gboolean OnLoadFailed(
            WebKitWebView* webView, WebKitLoadEvent event, gchar* uri, GError* error, gpointer data);
        static gboolean OnTlsError(WebKitWebView* webView, gchar* uri, GTlsCertificate* certificate,
            GTlsCertificateFlags errors, gpointer data);
        static void OnTitleChanged(GObject* object, GParamSpec* spec, gpointer data);
        static void OnUriChanged(GObject* object, GParamSpec* spec, gpointer data);
        static gboolean OnPermissionRequest(WebKitWebView* webView, WebKitPermissionRequest* request, gpointer data);
        static gboolean OnScriptDialog(WebKitWebView* webView, WebKitScriptDialog* dialog, gpointer data);
        static gboolean OnContextMenu(WebKitWebView* webView, WebKitContextMenu* menu, GdkEvent* event,
            WebKitHitTestResult* hitTest, gpointer data);
        static gboolean OnAuthenticate(WebKitWebView* webView, WebKitAuthenticationRequest* request, gpointer data);
        static gboolean OnEnterFullscreen(WebKitWebView* webView, gpointer data);
        static gboolean OnLeaveFullscreen(WebKitWebView* webView, gpointer data);
        static void OnProcessTerminated(
            WebKitWebView* webView, WebKitWebProcessTerminationReason reason, gpointer data);
        static GtkWidget* OnCreateWindow(WebKitWebView* webView, WebKitNavigationAction* action, gpointer data);
        static void OnCloseRequested(WebKitWebView* webView, gpointer data);
        static void OnResourceLoadStarted(
            WebKitWebView* webView, WebKitWebResource* resource, WebKitURIRequest* request, gpointer data);
        static void OnResourceFinished(WebKitWebResource* resource, gpointer data);
        static void OnDownloadStarted(WebKitWebContext* context, WebKitDownload* download, gpointer data);
        static void OnDownloadReceivedData(WebKitDownload* download, guint64 dataLength, gpointer data);
        static void OnDownloadFinished(WebKitDownload* download, gpointer data);
        static void OnDownloadFailed(WebKitDownload* download, GError* error, gpointer data);
        static void OnBridgeMessage(WebKitUserContentManager* manager, JSCValue* value, gpointer data);
        static void OnLifecycleMessage(WebKitUserContentManager* manager, JSCValue* value, gpointer data);
        static void OnFindCounted(WebKitFindController* controller, guint count, gpointer data);
        static void OnSchemeRequest(WebKitURISchemeRequest* request, gpointer data);
        static void OnEvaluateFinished(GObject* source, GAsyncResult* result, gpointer data);
        static void OnSnapshotFinished(GObject* source, GAsyncResult* result, gpointer data);
        static void OnCookiesReceived(GObject* source, GAsyncResult* result, gpointer data);
        static void OnCookieAdded(GObject* source, GAsyncResult* result, gpointer data);
        static void OnDeleteCookieLookup(GObject* source, GAsyncResult* result, gpointer data);
        static void OnCookieDeleted(GObject* source, GAsyncResult* result, gpointer data);
        static void OnClearDataFinished(GObject* source, GAsyncResult* result, gpointer data);
        static void OnStartupCacheCleared(GObject* source, GAsyncResult* result, gpointer data);

        Result<void> RequireWebView() const;
        void RegisterSchemes();
        void RegisterMessageHandlers();
        void ApplySettings();
        void ApplyBackground();
        void ApplyGeometryHints();
        void RaiseReady();
        void RaiseFailure(const Error& error);
        void RegisterWebViewSignals();
        void ConnectContextDownloads();
        void StartCacheClear();
        void HandleSchemeRequest(WebKitURISchemeRequest* request, const std::string& scheme);
        void HandleBridgeValue(JSCValue* value);
        void HandleLifecycleValue(JSCValue* value);
        void UpdateBoundsFromWindow();
        void UpdateTheme();
        void CancelDownloads();
        WebKitWebsiteDataManager* DataManager() const;
        std::string CurrentUri() const;
        bool SystemDarkTheme() const;

        GtkWidget* window = nullptr;
        WebKitWebView* webView = nullptr;
        WebKitWebContext* context = nullptr;
        GtkCssProvider* cssProvider = nullptr;
        GCancellable* cancellable = nullptr;
        WindowBackendDelegate* delegate = nullptr;
        std::shared_ptr<std::atomic<bool>> alive;
        WindowOptions options;
        BrowserOptions browserOptions;
        PlatformCapabilities capabilities;

        bool created = false;
        bool destroyed = false;
        bool closing = false;
        bool suppressEvents = false;
        bool visible = false;
        bool focused = false;
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        bool html5Fullscreen = false;
        bool readyRaised = false;
        bool failureRaised = false;
        bool loadFailed = false;
        bool contextDownloadConnected = false;
        bool ownsTemporaryFolder = false;

        Theme effectiveTheme = Theme::SYSTEM;
        bool darkMode = false;
        Color currentBackground{30, 30, 30, 255};
        double windowOpacity = 1.0;
        std::string currentUri;
        std::string userAgent;
        double zoomFactor = 1.0;
        std::vector<Rect> draggableRegions;
        std::filesystem::path temporaryFolder;
        std::unordered_map<WebKitDownload*, DownloadInfo> downloads;
        std::function<void(Result<FindResult>)> pendingFind;
    };

    std::unique_ptr<WindowBackend> CreatePlatformWindowBackend();
}
