#pragma once

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <WebView2.h>

#if defined(CreateWindow)
#undef CreateWindow
#endif
#if defined(IsMaximized)
#undef IsMaximized
#endif
#if defined(IsMinimized)
#undef IsMinimized
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/backend.h"
#include "platform/windows/windows-com.h"
#include "platform/windows/windows-environment.h"

namespace Flare {
    class WindowsWindowBackend final : public WindowBackend {
    public:
        WindowsWindowBackend();
        ~WindowsWindowBackend() override;

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

    private:
        struct DownloadRecord {
            ComPtr<ICoreWebView2DownloadOperation> operation;
            DownloadInfo info;
        };

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void HandleKeyboardMessage(MSG& message);
        void InstallKeyboardHook();
        void RemoveKeyboardHook();
        void ApplyInitialPlacement();
        void UpdateControllerBounds();
        void UpdateLayer();
        void UpdateTitleBarTheme();
        void UpdateBackgroundBrush(Color color);
        void ApplyRoundedAndShadow();
        void EnterFullscreenWindow();
        void ExitFullscreenWindow();
        void SaveNormalPlacement();
        void CloseInternal(bool notify);
        void ReleaseBrowser();
        void NotifyBrowserReady();
        void NotifyBrowserFailure(const Error& error);
        void CompleteReadyTask();
        void CreateBrowser();
        void OnEnvironmentReady(Result<ComPtr<ICoreWebView2Environment>> result);
        void OnControllerReady(HRESULT result, ICoreWebView2Controller* controller);
        void ConfigureWebView();
        void RegisterWebViewEvents();
        void RegisterFrame(ICoreWebView2Frame* frame);
        void RegisterAcceleratorEvents();
        void RegisterScript(const std::string& script);
        Result<void> RequireWebView() const;
        Result<void> NavigateCore(const std::wstring& url, bool applyExtraHeaders);
        std::wstring ResolveUserDataFolder();
        std::wstring ComposeBrowserArguments() const;
        std::wstring ComposeHeaders(const HttpHeaderMap& headers) const;
        void HandleTopLevelMessage(ICoreWebView2WebMessageReceivedEventArgs* args);
        void HandleFrameMessage(ICoreWebView2Frame* frame, ICoreWebView2WebMessageReceivedEventArgs* args);
        void HandleWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args);
        void HandleWebResourceResponse(ICoreWebView2WebResourceResponseReceivedEventArgs* args);
        void HandleDownloadStarting(ICoreWebView2DownloadStartingEventArgs* args);
        void HandleScriptDialog(ICoreWebView2ScriptDialogOpeningEventArgs* args);
        void HandleContextMenu(ICoreWebView2ContextMenuRequestedEventArgs* args);
        void HandleAuthentication(ICoreWebView2BasicAuthenticationRequestedEventArgs* args);
        void HandleCertificateError(ICoreWebView2ServerCertificateErrorDetectedEventArgs* args);
        std::string FrameId(ICoreWebView2Frame* frame) const;
        bool IsCustomChrome() const;
        void ApplyWindowStyles();
        Result<void> SetStyleFlag(LONG flag, bool enabled);
        std::filesystem::path SelectDownloadPath(
            const std::wstring& suggested, const std::wstring& defaultDirectory) const;
        void RaiseDownloadProgress(const std::shared_ptr<DownloadRecord>& record);
        void CompleteDownload(const std::shared_ptr<DownloadRecord>& record);

        HWND window = nullptr;
        HWND parentWindow = nullptr;
        bool parentWasEnabled = false;
        bool ownsTemporaryDataFolder = false;
        std::filesystem::path temporaryDataFolder;
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
        bool pendingMaximize = false;
        bool controllerConfigured = false;
        bool readyRaised = false;
        bool readyFailed = false;
        int readyPending = 0;

        Theme effectiveTheme = Theme::SYSTEM;
        bool darkMode = false;
        Color currentBackground{30, 30, 30, 255};
        double windowOpacity = 1.0;
        HBRUSH backgroundBrush = nullptr;
        HICON windowIcon = nullptr;
        HHOOK keyboardHook = nullptr;
        UINT dpi = 96;

        LONG savedStyle = 0;
        LONG savedExStyle = 0;
        WINDOWPLACEMENT savedPlacement{};
        RECT normalRect{};
        bool hasNormalRect = false;

        std::vector<Rect> draggableRegions;
        std::wstring userAgent;
        double zoomFactor = 1.0;

        ComPtr<ICoreWebView2Environment> environment;
        ComPtr<ICoreWebView2EnvironmentOptions> environmentOptions;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> webview;
        std::unordered_map<std::string, ComPtr<ICoreWebView2Frame2>> frames;
        std::unordered_map<std::string, std::shared_ptr<DownloadRecord>> downloads;
    };
}
