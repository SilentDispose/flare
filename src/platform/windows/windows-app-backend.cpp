#include <windows.h>

#if defined(CreateWindow)
#undef CreateWindow
#endif

#include <atomic>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "backend/backend.h"
#include "platform/windows/windows-backend.h"
#include "platform/windows/webview2-loader.h"
#include "platform/windows/windows-com.h"

namespace Flare {
    namespace {
        WindowsAppSettings appSettings;

        constexpr UINT DispatchMessageId = WM_APP + 1;
        constexpr UINT StopMessageId = WM_APP + 2;
        constexpr wchar_t DispatchWindowClass[] = L"FlareDispatchWindow";
    }

    const WindowsAppSettings& CurrentWindowsAppSettings() {
        return appSettings;
    }

    WindowsAppSettings& MutableWindowsAppSettings() {
        return appSettings;
    }

    class WindowsAppBackend final : public AppBackend {
    public:
        ~WindowsAppBackend() override {
            if (dispatchWindow) {
                DestroyWindow(dispatchWindow);
                dispatchWindow = nullptr;
            }
            if (classRegistered) {
                UnregisterClassW(DispatchWindowClass, instance);
            }
            if (comInitialized) {
                CoUninitialize();
            }
        }

        Result<void> Initialize(const AppOptions& options) override {
            instance = GetModuleHandleW(nullptr);
            if (!options.stateFolder.empty()) {
                WebView2Loader::SetCacheDirectory(options.stateFolder / "loader-cache");
            }
            uiThreadId = GetCurrentThreadId();
            appSettings.theme = options.theme;
            appSettings.hardwareAcceleration = options.hardwareAcceleration;
            appSettings.dpiAwareness = options.dpiAwareness;
            ApplyDpiAwareness(options.dpiAwareness);

            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (comResult == RPC_E_CHANGED_MODE) {
                return Error(ErrorCode::PLATFORM_FAILURE,
                    "The UI thread was already initialized with a different COM apartment; Flare requires a "
                    "single-threaded apartment");
            }
            if (FAILED(comResult)) {
                return Error(ErrorCode::PLATFORM_FAILURE, "Unable to initialize COM", FormatHResult(comResult));
            }
            comInitialized = true;

            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &WindowsAppBackend::WindowProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = DispatchWindowClass;
            if (!RegisterClassExW(&windowClass)) {
                if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                    return Error(
                        ErrorCode::PLATFORM_FAILURE, "Unable to register the application dispatch window class");
                }
            }
            classRegistered = true;

            dispatchWindow = CreateWindowExW(
                0, DispatchWindowClass, L"FlareDispatch", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
            if (!dispatchWindow) {
                return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the application dispatch window");
            }

            capabilities.platformName = "windows";
            capabilities.multiWindow = true;
            capabilities.nativeKeyEvents = true;
            capabilities.nativeMouseEvents = false;
            capabilities.nativeWheelEvents = false;
            capabilities.fileDrop = true;
            capabilities.transparentWindows = true;
            capabilities.windowOpacity = true;
            capabilities.roundedCorners = true;
            capabilities.borderlessShadow = true;
            capabilities.perWindowIcon = true;
            capabilities.taskbarVisibility = true;
            capabilities.contentProtection = true;
            capabilities.customTitleBar = true;
            capabilities.draggableRegions = true;
            capabilities.virtualHostHttps = true;
            capabilities.virtualHostScheme = true;
            capabilities.customSchemes = true;
            capabilities.requestInterception = true;
            capabilities.responseInterception = true;
            capabilities.headerInjection = true;
            capabilities.proxy = true;
            capabilities.browserArguments = true;
            capabilities.downloads = true;
            capabilities.printToPdf = true;
            capabilities.print = true;
            capabilities.capturePreview = true;
            capabilities.findInPage = true;
            capabilities.cookies = true;
            capabilities.clearBrowsingData = true;
            capabilities.incognito = true;
            capabilities.perWindowDataStore = true;
            capabilities.windowFullscreen = true;
            capabilities.html5FullscreenEvents = true;
            capabilities.permissionRequests = true;
            capabilities.scriptDialogs = true;
            capabilities.contextMenuControl = true;
            capabilities.httpAuthentication = true;
            capabilities.certificateErrors = true;
            capabilities.webMessages = true;
            capabilities.iframeBridge = true;
            capabilities.devTools = true;
            capabilities.devToolsControl = false;
            capabilities.browserAcceleratorKeysToggle = true;
            capabilities.autofillToggle = true;
            capabilities.swipeNavigation = true;
            capabilities.pinchZoom = true;
            capabilities.statusBar = true;
            capabilities.zoomControl = true;
            capabilities.webSecurityToggle = true;
            capabilities.dpiEvents = true;
            capabilities.themeEvents = true;
            capabilities.systemThemeChange = true;
            capabilities.documentTitleEvents = true;
            capabilities.faviconEvents = true;
            capabilities.domContentLoadedEvents = true;
            capabilities.notes.emplace_back("WebView2Loader.dll must be deployed next to the application executable");
            capabilities.notes.emplace_back(
                "WebView2 does not expose mouse, wheel, or double-click events to the host");
            capabilities.notes.emplace_back(
                "Find-in-page is executed through a page script because the WebView2 host API does not expose a find "
                "controller");
            capabilities.notes.emplace_back("Closing DevTools programmatically is not exposed by WebView2");
            return Result<void>::Ok();
        }

        Result<void> Run() override {
            if (!IsOnUiThread()) {
                return Error(ErrorCode::WRONG_THREAD, "App::Run must be called on the UI thread");
            }
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            return Result<void>::Ok();
        }

        void Stop() override {
            if (dispatchWindow) {
                PostMessageW(dispatchWindow, StopMessageId, 0, 0);
            } else {
                PostQuitMessage(0);
            }
        }

        void Dispatch(std::function<void()> task) override {
            if (!task) {
                return;
            }
            {
                std::lock_guard<std::mutex> guard(queueMutex);
                tasks.push_back(std::move(task));
            }
            if (dispatchWindow) {
                PostMessageW(dispatchWindow, DispatchMessageId, 0, 0);
            }
        }

        bool IsOnUiThread() const override { return GetCurrentThreadId() == uiThreadId; }

        const PlatformCapabilities& Capabilities() const override { return capabilities; }

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            WindowsAppBackend* self = reinterpret_cast<WindowsAppBackend*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                self = static_cast<WindowsAppBackend*>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (message == DispatchMessageId && self) {
                self->DrainTasks();
                return 0;
            }
            if (message == StopMessageId) {
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        void DrainTasks() {
            std::deque<std::function<void()>> pending;
            {
                std::lock_guard<std::mutex> guard(queueMutex);
                pending.swap(tasks);
            }
            for (const std::function<void()>& task : pending) {
                try {
                    task();
                } catch (const std::exception&) {
                } catch (...) {
                }
            }
        }

        void ApplyDpiAwareness(DpiAwareness awareness) {
            using SetContextFunction = BOOL(WINAPI*)(HANDLE);
            static const auto setContext = reinterpret_cast<SetContextFunction>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
            const INT_PTR value = [awareness]() -> INT_PTR {
                switch (awareness) {
                    case DpiAwareness::UNAWARE:
                        return -1;
                    case DpiAwareness::SYSTEM:
                        return -2;
                    case DpiAwareness::PER_MONITOR:
                        return -3;
                    case DpiAwareness::PER_MONITOR_V2:
                        return -4;
                }
                return -4;
            }();
            if (setContext) {
                setContext(reinterpret_cast<HANDLE>(value));
                return;
            }
            if (awareness != DpiAwareness::UNAWARE) {
                SetProcessDPIAware();
            }
        }

        HINSTANCE instance = nullptr;
        HWND dispatchWindow = nullptr;
        DWORD uiThreadId = 0;
        bool classRegistered = false;
        bool comInitialized = false;
        std::mutex queueMutex;
        std::deque<std::function<void()>> tasks;
        PlatformCapabilities capabilities;
    };

    std::unique_ptr<AppBackend> CreatePlatformAppBackend() {
        return std::make_unique<WindowsAppBackend>();
    }
}
