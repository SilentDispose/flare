#include "platform/windows/windows-window.h"

#include <windows.h>
#include <wincodec.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/path-utils.h"
#include "core/url-utils.h"
#include "platform/windows/windows-backend.h"
#include "platform/windows/windows-callbacks.h"
#include "platform/windows/windows-com.h"
#include "platform/windows/windows-utils.h"
#include "platform/windows/webview2-loader.h"

#if defined(IsMaximized)
#undef IsMaximized
#endif
#if defined(IsMinimized)
#undef IsMinimized
#endif
#if defined(CreateWindow)
#undef CreateWindow
#endif

namespace Flare {
    namespace {
        HICON LoadIconFromResource(const std::string& name) {
            if (name.empty()) {
                return nullptr;
            }
            HINSTANCE module = GetModuleHandleW(nullptr);
            bool numeric = true;
            for (char character : name) {
                if (character < '0' || character > '9') {
                    numeric = false;
                    break;
                }
            }
            if (numeric) {
                unsigned long identifier = 0;
                for (char character : name) {
                    identifier = identifier * 10 + static_cast<unsigned long>(character - '0');
                }
                return LoadIconW(module, MAKEINTRESOURCEW(identifier));
            }
            return static_cast<HICON>(LoadImageW(module, Utf8ToWide(name).c_str(), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        }

        HICON LoadIconFromImageFile(const std::wstring& path) {
            ComPtr<IWICImagingFactory> factory;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                    reinterpret_cast<void**>(factory.Put())))) {
                return nullptr;
            }
            ComPtr<IWICBitmapDecoder> decoder;
            if (FAILED(factory->CreateDecoderFromFilename(
                    path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.Put()))) {
                return nullptr;
            }
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0, frame.Put()))) {
                return nullptr;
            }
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateFormatConverter(converter.Put()))) {
                return nullptr;
            }
            if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                    nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                return nullptr;
            }
            UINT width = 0;
            UINT height = 0;
            if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 || width > 1024
                || height > 1024) {
                return nullptr;
            }

            BITMAPV5HEADER header{};
            header.bV5Size = sizeof(header);
            header.bV5Width = static_cast<LONG>(width);
            header.bV5Height = -static_cast<LONG>(height);
            header.bV5Planes = 1;
            header.bV5BitCount = 32;
            header.bV5Compression = BI_BITFIELDS;
            header.bV5RedMask = 0x00FF0000;
            header.bV5GreenMask = 0x0000FF00;
            header.bV5BlueMask = 0x000000FF;
            header.bV5AlphaMask = 0xFF000000;

            void* pixels = nullptr;
            HBITMAP color = CreateDIBSection(
                nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (!color || !pixels) {
                if (color) {
                    DeleteObject(color);
                }
                return nullptr;
            }
            const UINT stride = width * 4;
            if (FAILED(converter->CopyPixels(nullptr, stride, stride * height, static_cast<BYTE*>(pixels)))) {
                DeleteObject(color);
                return nullptr;
            }

            std::vector<BYTE> maskBits(((width + 15) / 16) * 2 * height, 0);
            HBITMAP mask = CreateBitmap(width, height, 1, 1, maskBits.data());
            if (!mask) {
                DeleteObject(color);
                return nullptr;
            }
            ICONINFO info{};
            info.fIcon = TRUE;
            info.hbmMask = mask;
            info.hbmColor = color;
            HICON icon = CreateIconIndirect(&info);
            DeleteObject(mask);
            DeleteObject(color);
            return icon;
        }
        constexpr wchar_t FlareWindowClass[] = L"FlareWebViewWindow";
        constexpr DWORD DisplayAffinityExclude = 0x00000011;

        void EnsureWindowClass(HINSTANCE instance, WNDPROC windowProc) {
            static bool registered = false;
            if (registered) {
                return;
            }
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_HREDRAW | CS_VREDRAW;
            windowClass.lpfnWndProc = windowProc;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            windowClass.lpszClassName = FlareWindowClass;
            registered = RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }

        std::wstring QuoteArgument(const std::wstring& argument) {
            if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
                return argument;
            }
            std::wstring quoted = L"\"";
            for (wchar_t character : argument) {
                if (character == L'"') {
                    quoted += L'\\';
                }
                quoted += character;
            }
            quoted += L'"';
            return quoted;
        }

        bool ContainsArgument(const std::wstring& arguments, const std::wstring& value) {
            return arguments.find(value) != std::wstring::npos;
        }

        void AppendArgument(std::wstring& arguments, const std::wstring& argument) {
            if (!arguments.empty()) {
                arguments += L' ';
            }
            arguments += QuoteArgument(argument);
        }

        struct VirtualKeyInfo {
            const char* key;
            const char* code;
        };

        VirtualKeyInfo DescribeVirtualKey(UINT key, LPARAM lParam) {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool extended = (lParam & (1L << 24)) != 0;
            if (key >= 'A' && key <= 'Z') {
                static thread_local char keyName[2] = {0, 0};
                static thread_local char codeName[5] = {'K', 'e', 'y', 0, 0};
                keyName[0] = static_cast<char>('a' + (key - 'A'));
                codeName[3] = static_cast<char>(key);
                return {keyName, codeName};
            }
            if (key >= '0' && key <= '9') {
                static thread_local char keyName[2] = {0, 0};
                static thread_local char codeName[7] = {'D', 'i', 'g', 'i', 't', 0, 0};
                keyName[0] = static_cast<char>(key);
                codeName[5] = static_cast<char>(key);
                return {keyName, codeName};
            }
            if (key >= VK_F1 && key <= VK_F24) {
                static thread_local char keyName[8] = {0};
                static thread_local char codeName[8] = {0};
                const int number = static_cast<int>(key - VK_F1 + 1);
                std::snprintf(keyName, sizeof(keyName), "F%d", number);
                std::snprintf(codeName, sizeof(codeName), "F%d", number);
                return {keyName, codeName};
            }
            if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
                static thread_local char keyName[2] = {0, 0};
                static thread_local char codeName[8] = {'N', 'u', 'm', 'p', 'a', 'd', 0, 0};
                keyName[0] = static_cast<char>('0' + (key - VK_NUMPAD0));
                codeName[6] = keyName[0];
                return {keyName, codeName};
            }
            switch (key) {
                case VK_RETURN:
                    return {"Enter", "Enter"};
                case VK_ESCAPE:
                    return {"Escape", "Escape"};
                case VK_TAB:
                    return {"Tab", "Tab"};
                case VK_SPACE:
                    return {" ", "Space"};
                case VK_BACK:
                    return {"Backspace", "Backspace"};
                case VK_DELETE:
                    return {"Delete", "Delete"};
                case VK_INSERT:
                    return {"Insert", "Insert"};
                case VK_CLEAR:
                    return {"Clear", "NumpadClear"};
                case VK_LEFT:
                    return {"ArrowLeft", "ArrowLeft"};
                case VK_RIGHT:
                    return {"ArrowRight", "ArrowRight"};
                case VK_UP:
                    return {"ArrowUp", "ArrowUp"};
                case VK_DOWN:
                    return {"ArrowDown", "ArrowDown"};
                case VK_HOME:
                    return {"Home", "Home"};
                case VK_END:
                    return {"End", "End"};
                case VK_PRIOR:
                    return {"PageUp", "PageUp"};
                case VK_NEXT:
                    return {"PageDown", "PageDown"};
                case VK_SHIFT:
                case VK_LSHIFT:
                case VK_RSHIFT:
                    return {"Shift", ((lParam >> 16) & 0xFF) == 0x36 ? "ShiftRight" : "ShiftLeft"};
                case VK_CONTROL:
                case VK_LCONTROL:
                case VK_RCONTROL:
                    return {"Control", key == VK_RCONTROL || extended ? "ControlRight" : "ControlLeft"};
                case VK_MENU:
                case VK_LMENU:
                case VK_RMENU:
                    return {"Alt", key == VK_RMENU || extended ? "AltRight" : "AltLeft"};
                case VK_LWIN:
                    return {"Meta", "MetaLeft"};
                case VK_RWIN:
                    return {"Meta", "MetaRight"};
                case VK_APPS:
                    return {"ContextMenu", "ContextMenu"};
                case VK_CAPITAL:
                    return {"CapsLock", "CapsLock"};
                case VK_NUMLOCK:
                    return {"NumLock", "NumLock"};
                case VK_SCROLL:
                    return {"ScrollLock", "ScrollLock"};
                case VK_PAUSE:
                    return {"Pause", "Pause"};
                case VK_SNAPSHOT:
                    return {"PrintScreen", "PrintScreen"};
                case VK_HELP:
                    return {"Help", "Help"};
                case VK_SLEEP:
                    return {"Sleep", "Sleep"};
                case VK_MULTIPLY:
                    return {"*", "NumpadMultiply"};
                case VK_ADD:
                    return {"+", "NumpadAdd"};
                case VK_SEPARATOR:
                    return {"Enter", "NumpadEnter"};
                case VK_SUBTRACT:
                    return {"-", "NumpadSubtract"};
                case VK_DECIMAL:
                    return {".", "NumpadDecimal"};
                case VK_DIVIDE:
                    return {"/", "NumpadDivide"};
                case VK_OEM_1:
                    return {shift ? ":" : ";", "Semicolon"};
                case VK_OEM_PLUS:
                    return {shift ? "+" : "=", "Equal"};
                case VK_OEM_COMMA:
                    return {shift ? "<" : ",", "Comma"};
                case VK_OEM_MINUS:
                    return {shift ? "_" : "-", "Minus"};
                case VK_OEM_PERIOD:
                    return {shift ? ">" : ".", "Period"};
                case VK_OEM_2:
                    return {shift ? "?" : "/", "Slash"};
                case VK_OEM_3:
                    return {shift ? "~" : "`", "Backquote"};
                case VK_OEM_4:
                    return {shift ? "{" : "[", "BracketLeft"};
                case VK_OEM_5:
                    return {shift ? "|" : "\\", "Backslash"};
                case VK_OEM_6:
                    return {shift ? "}" : "]", "BracketRight"};
                case VK_OEM_7:
                    return {shift ? "\"" : "'", "Quote"};
                case VK_OEM_102:
                    return {shift ? "|" : "\\", "IntlBackslash"};
                case VK_BROWSER_BACK:
                    return {"BrowserBack", "BrowserBack"};
                case VK_BROWSER_FORWARD:
                    return {"BrowserForward", "BrowserForward"};
                case VK_BROWSER_REFRESH:
                    return {"BrowserRefresh", "BrowserRefresh"};
                case VK_BROWSER_STOP:
                    return {"BrowserStop", "BrowserStop"};
                case VK_BROWSER_SEARCH:
                    return {"BrowserSearch", "BrowserSearch"};
                case VK_BROWSER_FAVORITES:
                    return {"BrowserFavorites", "BrowserFavorites"};
                case VK_BROWSER_HOME:
                    return {"BrowserHome", "BrowserHome"};
                case VK_VOLUME_MUTE:
                    return {"AudioVolumeMute", "AudioVolumeMute"};
                case VK_VOLUME_DOWN:
                    return {"AudioVolumeDown", "AudioVolumeDown"};
                case VK_VOLUME_UP:
                    return {"AudioVolumeUp", "AudioVolumeUp"};
                case VK_MEDIA_NEXT_TRACK:
                    return {"MediaTrackNext", "MediaTrackNext"};
                case VK_MEDIA_PREV_TRACK:
                    return {"MediaTrackPrevious", "MediaTrackPrevious"};
                case VK_MEDIA_STOP:
                    return {"MediaStop", "MediaStop"};
                case VK_MEDIA_PLAY_PAUSE:
                    return {"MediaPlayPause", "MediaPlayPause"};
                case VK_LAUNCH_MAIL:
                    return {"LaunchMail", "LaunchMail"};
                case VK_LAUNCH_MEDIA_SELECT:
                    return {"LaunchMediaPlayer", "LaunchMediaPlayer"};
                case VK_LAUNCH_APP1:
                    return {"LaunchApplication1", "LaunchApplication1"};
                case VK_LAUNCH_APP2:
                    return {"LaunchApplication2", "LaunchApplication2"};
                default:
                    break;
            }
            static thread_local char keyName[16] = {0};
            static thread_local char codeName[16] = {0};
            std::snprintf(keyName, sizeof(keyName), "VK_%u", key);
            std::snprintf(codeName, sizeof(codeName), "VK_%u", key);
            return {keyName, codeName};
        }

        bool IsKeyboardMessage(UINT message) {
            switch (message) {
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                    return true;
                default:
                    return false;
            }
        }

        Modifiers CurrentModifiers() {
            Modifiers modifiers = 0;
            if (GetKeyState(VK_MENU) & 0x8000) {
                modifiers |= static_cast<Modifiers>(Modifier::ALT);
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                modifiers |= static_cast<Modifiers>(Modifier::CONTROL);
            }
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                modifiers |= static_cast<Modifiers>(Modifier::SHIFT);
            }
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) {
                modifiers |= static_cast<Modifiers>(Modifier::META);
            }
            if (GetKeyState(VK_CAPITAL) & 0x0001) {
                modifiers |= static_cast<Modifiers>(Modifier::CAPS_LOCK);
            }
            if (GetKeyState(VK_NUMLOCK) & 0x0001) {
                modifiers |= static_cast<Modifiers>(Modifier::NUM_LOCK);
            }
            return modifiers;
        }

        PlatformCapabilities BuildWindowCapabilities() {
            PlatformCapabilities value;
            value.platformName = "windows";
            value.multiWindow = true;
            value.nativeKeyEvents = true;
            value.nativeMouseEvents = false;
            value.nativeWheelEvents = false;
            value.fileDrop = true;
            value.transparentWindows = true;
            value.windowOpacity = true;
            value.roundedCorners = true;
            value.borderlessShadow = true;
            value.perWindowIcon = true;
            value.taskbarVisibility = true;
            value.contentProtection = true;
            value.customTitleBar = true;
            value.draggableRegions = true;
            value.virtualHostHttps = true;
            value.virtualHostScheme = true;
            value.customSchemes = true;
            value.requestInterception = true;
            value.responseInterception = true;
            value.headerInjection = true;
            value.proxy = true;
            value.browserArguments = true;
            value.downloads = true;
            value.printToPdf = true;
            value.print = true;
            value.capturePreview = true;
            value.findInPage = true;
            value.cookies = true;
            value.clearBrowsingData = true;
            value.incognito = true;
            value.perWindowDataStore = true;
            value.windowFullscreen = true;
            value.html5FullscreenEvents = true;
            value.permissionRequests = true;
            value.scriptDialogs = true;
            value.contextMenuControl = true;
            value.httpAuthentication = true;
            value.certificateErrors = true;
            value.webMessages = true;
            value.iframeBridge = true;
            value.devTools = true;
            value.devToolsControl = false;
            value.browserAcceleratorKeysToggle = true;
            value.autofillToggle = true;
            value.swipeNavigation = true;
            value.pinchZoom = true;
            value.statusBar = true;
            value.zoomControl = true;
            value.webSecurityToggle = true;
            value.dpiEvents = true;
            value.themeEvents = true;
            value.systemThemeChange = true;
            value.documentTitleEvents = true;
            value.faviconEvents = true;
            value.domContentLoadedEvents = true;
            value.notes.emplace_back("WebView2Loader.dll must be deployed next to the application executable");
            value.notes.emplace_back("WebView2 does not expose mouse, wheel, or double-click events to the host");
            value.notes.emplace_back(
                "Find-in-page is executed through a page script because the WebView2 host API does not expose a find "
                "controller");
            value.notes.emplace_back("Closing DevTools programmatically is not exposed by WebView2");
            return value;
        }
    }

    WindowsWindowBackend::WindowsWindowBackend()
        : alive(std::make_shared<std::atomic<bool>>(true)),
          capabilities(BuildWindowCapabilities()) {}

    WindowsWindowBackend::~WindowsWindowBackend() {
        alive->store(false);
        RemoveKeyboardHook();
        suppressEvents = true;
        if (window && IsWindow(window)) {
            ReleaseBrowser();
            DestroyWindow(window);
            window = nullptr;
        }
        ReleaseBrowser();
        if (parentWindow && parentWasEnabled && IsWindow(parentWindow)) {
            EnableWindow(parentWindow, TRUE);
        }
        if (backgroundBrush) {
            DeleteObject(backgroundBrush);
            backgroundBrush = nullptr;
        }
        if (windowIcon) {
            DestroyIcon(windowIcon);
            windowIcon = nullptr;
        }
        if (ownsTemporaryDataFolder && !temporaryDataFolder.empty()) {
            std::error_code error;
            std::filesystem::remove_all(temporaryDataFolder, error);
        }
    }

    Result<void> WindowsWindowBackend::Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
        WindowBackendDelegate& backendDelegate, void* parentHandle) {
        options = windowOptions;
        this->browserOptions = browserOptions;
        delegate = &backendDelegate;
        parentWindow = static_cast<HWND>(parentHandle);
        effectiveTheme = options.theme;
        if (effectiveTheme == Theme::SYSTEM && CurrentWindowsAppSettings().theme != Theme::SYSTEM) {
            effectiveTheme = CurrentWindowsAppSettings().theme;
        }
        darkMode = effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemUsesDarkMode());
        currentBackground = options.backgroundColor;
        windowOpacity = options.opacity;
        userAgent = Utf8ToWide(browserOptions.userAgent);
        zoomFactor = browserOptions.zoomFactor;
        draggableRegions = options.draggableRegions;
        dpi = 96;

        HINSTANCE instance = GetModuleHandleW(nullptr);
        EnsureWindowClass(instance, &WindowsWindowBackend::WindowProc);

        DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        const bool customChrome = IsCustomChrome();
        if (customChrome) {
            style |= WS_POPUP;
            if (options.resizable) {
                style |= WS_THICKFRAME;
            }
            if (options.minimizable) {
                style |= WS_MINIMIZEBOX;
            }
            if (options.maximizable) {
                style |= WS_MAXIMIZEBOX;
            }
            if (options.closable) {
                style |= WS_SYSMENU;
            }
        } else {
            style |= WS_OVERLAPPEDWINDOW;
            if (!options.resizable) {
                style &= ~WS_THICKFRAME;
            }
            if (!options.minimizable) {
                style &= ~WS_MINIMIZEBOX;
            }
            if (!options.maximizable) {
                style &= ~WS_MAXIMIZEBOX;
            }
        }

        DWORD extendedStyle = WS_EX_NOPARENTNOTIFY;
        extendedStyle |= options.showInTaskbar ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW;
        if (options.transparent || options.opacity < 1.0) {
            extendedStyle |= WS_EX_LAYERED;
        }

        dpi = parentWindow ? WindowDpi(parentWindow) : 96;
        int windowWidth = ScaleForDpi(options.width, dpi);
        int windowHeight = ScaleForDpi(options.height, dpi);
        if (!customChrome) {
            RECT clientRect{0, 0, windowWidth, windowHeight};
            using AdjustRectFunction = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
            static const auto adjustForDpi = reinterpret_cast<AdjustRectFunction>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
            if (adjustForDpi) {
                adjustForDpi(&clientRect, style, FALSE, extendedStyle, dpi);
            } else {
                AdjustWindowRectEx(&clientRect, style, FALSE, extendedStyle);
            }
            windowWidth = clientRect.right - clientRect.left;
            windowHeight = clientRect.bottom - clientRect.top;
        }

        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        if (options.hasPosition) {
            x = ScaleForDpi(options.x, dpi);
            y = ScaleForDpi(options.y, dpi);
        } else if (options.centered) {
            HMONITOR monitor = parentWindow ? MonitorFromWindow(parentWindow, MONITOR_DEFAULTTOPRIMARY)
                                            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                x = monitorInfo.rcWork.left + ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - windowWidth) / 2;
                y = monitorInfo.rcWork.top + ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - windowHeight) / 2;
            }
        }

        window = CreateWindowExW(extendedStyle, FlareWindowClass, Utf8ToWide(options.title).c_str(), style, x, y,
            windowWidth, windowHeight, parentWindow, nullptr, instance, this);
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the native window");
        }

        dpi = WindowDpi(window);
        UpdateBackgroundBrush(currentBackground);
        UpdateLayer();
        ApplyRoundedAndShadow();
        UpdateTitleBarTheme();
        ApplyInitialPlacement();
        DragAcceptFiles(window, TRUE);

        if (!options.icon.IsEmpty()) {
            Result<void> iconResult = SetIcon(options.icon);
            if (!iconResult.IsOk()) {
                return iconResult;
            }
        }
        if (options.contentProtection) {
            if (!SetWindowDisplayAffinity(window, DisplayAffinityExclude)) {
                return Error(ErrorCode::PLATFORM_FAILURE, "Unable to enable content protection");
            }
        }
        if (options.alwaysOnTop) {
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (parentWindow && options.modal) {
            parentWasEnabled = IsWindowEnabled(parentWindow) != FALSE;
            EnableWindow(parentWindow, FALSE);
        }

        InstallKeyboardHook();
        created = true;
        if (delegate) {
            delegate->OnBackendCreated();
        }
        CreateBrowser();
        return Result<void>::Ok();
    }

    void WindowsWindowBackend::CreateBrowser() {
        const std::wstring userDataFolder = ResolveUserDataFolder();
        const std::wstring arguments = ComposeBrowserArguments();
        const std::wstring language = Utf8ToWide(browserOptions.language);

        auto* environmentOptionsRaw = new WebView2EnvironmentOptions(arguments, language);
        environmentOptions.Attach(environmentOptionsRaw);
        const std::string key = WideToUtf8(userDataFolder) + "|" + WideToUtf8(arguments) + "|" + WideToUtf8(language);
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;
        RequestWebView2Environment(key, userDataFolder, environmentOptions,
            [weakAlive, self](Result<ComPtr<ICoreWebView2Environment>> result) {
                if (const std::shared_ptr<std::atomic<bool>> state = weakAlive.lock(); state && state->load()) {
                    self->OnEnvironmentReady(std::move(result));
                }
            });
    }

    void WindowsWindowBackend::OnEnvironmentReady(Result<ComPtr<ICoreWebView2Environment>> result) {
        if (destroyed) {
            return;
        }
        if (!result.IsOk()) {
            NotifyBrowserFailure(result.GetError());
            return;
        }
        environment = result.Value();
        environmentOptions.Reset();

        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;
        auto* handler = new ControllerCompletedHandler(
            [weakAlive, self](HRESULT createResult, ICoreWebView2Controller* createdController) {
                if (const std::shared_ptr<std::atomic<bool>> state = weakAlive.lock(); state && state->load()) {
                    self->OnControllerReady(createResult, createdController);
                } else if (createdController) {
                    createdController->Close();
                }
            });

        HRESULT createResult = E_FAIL;
        if (browserOptions.incognito) {
            ComPtr<ICoreWebView2Environment10> environment10;
            if (FAILED(environment.As(IID_ICoreWebView2Environment10, environment10))) {
                handler->Release();
                NotifyBrowserFailure(
                    Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support in-private controllers"));
                return;
            }
            ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
            if (FAILED(environment10->CreateCoreWebView2ControllerOptions(controllerOptions.Put()))) {
                handler->Release();
                NotifyBrowserFailure(
                    Error(ErrorCode::PLATFORM_FAILURE, "Unable to create WebView2 controller options"));
                return;
            }
            controllerOptions->put_IsInPrivateModeEnabled(TRUE);
            createResult = environment10->CreateCoreWebView2ControllerWithOptions(
                window, controllerOptions.Get(), handler);
        } else {
            createResult = environment->CreateCoreWebView2Controller(window, handler);
        }
        handler->Release();
        if (FAILED(createResult)) {
            NotifyBrowserFailure(Error(
                ErrorCode::PLATFORM_FAILURE, "Unable to create the WebView2 controller", FormatHResult(createResult)));
        }
    }

    void WindowsWindowBackend::OnControllerReady(HRESULT createResult, ICoreWebView2Controller* createdController) {
        if (destroyed) {
            if (createdController) {
                createdController->Close();
            }
            return;
        }
        if (FAILED(createResult) || !createdController) {
            NotifyBrowserFailure(
                Error(ErrorCode::PLATFORM_FAILURE, "WebView2 controller creation failed", FormatHResult(createResult)));
            return;
        }

        controller = createdController;
        controller->AddRef();
        if (FAILED(controller->get_CoreWebView2(webview.Put())) || !webview) {
            NotifyBrowserFailure(Error(ErrorCode::PLATFORM_FAILURE, "Unable to access the WebView2 core"));
            return;
        }

        controller->put_IsVisible(TRUE);
        UpdateControllerBounds();

        ComPtr<ICoreWebView2Controller2> controller2;
        if (SUCCEEDED(controller.As(IID_ICoreWebView2Controller2, controller2))) {
            Color browserColor = browserOptions.backgroundColor;
            if (options.transparent || browserColor.a == 0) {
                if (browserColor.a == 0 && !options.transparent) {
                    browserColor = currentBackground;
                }
                browserColor.a = options.transparent ? 0 : 255;
            }
            COREWEBVIEW2_COLOR nativeColor{browserColor.a, browserColor.r, browserColor.g, browserColor.b};
            controller2->put_DefaultBackgroundColor(nativeColor);
        }
        ComPtr<ICoreWebView2Controller3> controller3;
        if (SUCCEEDED(controller.As(IID_ICoreWebView2Controller3, controller3))) {
            controller3->put_RasterizationScale(static_cast<double>(dpi) / 96.0);
            controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
        }
        ComPtr<ICoreWebView2Controller4> controller4;
        if (SUCCEEDED(controller.As(IID_ICoreWebView2Controller4, controller4))) {
            controller4->put_AllowExternalDrop(FALSE);
        }

        ConfigureWebView();
    }

    void WindowsWindowBackend::ConfigureWebView() {
        if (!webview) {
            NotifyBrowserFailure(Error(ErrorCode::PLATFORM_FAILURE, "The WebView2 core is unavailable"));
            return;
        }

        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webview->get_Settings(settings.Put())) && settings) {
            settings->put_IsScriptEnabled(browserOptions.javascriptEnabled ? TRUE : FALSE);
            settings->put_AreDefaultScriptDialogsEnabled(browserOptions.defaultScriptDialogsEnabled ? TRUE : FALSE);
            settings->put_AreDevToolsEnabled(browserOptions.devToolsEnabled ? TRUE : FALSE);
            settings->put_AreDefaultContextMenusEnabled(browserOptions.contextMenuEnabled ? TRUE : FALSE);
            settings->put_IsStatusBarEnabled(browserOptions.statusBarEnabled ? TRUE : FALSE);
            settings->put_IsZoomControlEnabled(browserOptions.zoomControlEnabled ? TRUE : FALSE);
            settings->put_IsBuiltInErrorPageEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);

            ComPtr<ICoreWebView2Settings2> settings2;
            if (SUCCEEDED(settings.As(IID_ICoreWebView2Settings2, settings2))) {
                if (!browserOptions.userAgent.empty()) {
                    settings2->put_UserAgent(Utf8ToWide(browserOptions.userAgent).c_str());
                }
            }
            ComPtr<ICoreWebView2Settings3> settings3;
            if (SUCCEEDED(settings.As(IID_ICoreWebView2Settings3, settings3))) {
                settings3->put_AreBrowserAcceleratorKeysEnabled(
                    browserOptions.browserAcceleratorKeysEnabled ? TRUE : FALSE);
            }
            ComPtr<ICoreWebView2Settings4> settings4;
            if (SUCCEEDED(settings.As(IID_ICoreWebView2Settings4, settings4))) {
                settings4->put_IsGeneralAutofillEnabled(browserOptions.autofillEnabled ? TRUE : FALSE);
                settings4->put_IsPasswordAutosaveEnabled(browserOptions.passwordSaveEnabled ? TRUE : FALSE);
            }
            ComPtr<ICoreWebView2Settings5> settings5;
            if (SUCCEEDED(settings.As(IID_ICoreWebView2Settings5, settings5))) {
                settings5->put_IsPinchZoomEnabled(browserOptions.pinchZoomEnabled ? TRUE : FALSE);
            }
            ComPtr<ICoreWebView2Settings6> settings6;
            if (SUCCEEDED(settings.As(IID_ICoreWebView2Settings6, settings6))) {
                settings6->put_IsSwipeNavigationEnabled(browserOptions.swipeNavigationEnabled ? TRUE : FALSE);
            }
        }

        if (controller) {
            controller->put_ZoomFactor(browserOptions.zoomFactor);
        }

        RegisterWebViewEvents();
        RegisterAcceleratorEvents();

        if (browserOptions.virtualHost.has_value()) {
            ComPtr<ICoreWebView2_3> webview3;
            if (SUCCEEDED(webview.As(IID_ICoreWebView2_3, webview3))) {
                const std::wstring host = Utf8ToWide(browserOptions.virtualHost->host);
                const std::wstring folder = Utf8ToWide(PathToUtf8(browserOptions.virtualHost->folder));
                if (FAILED(webview3->SetVirtualHostNameToFolderMapping(
                        host.c_str(), folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
                    NotifyBrowserFailure(Error(ErrorCode::PLATFORM_FAILURE, "Unable to map the virtual host folder"));
                    return;
                }
            } else {
                NotifyBrowserFailure(
                    Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support virtual host mapping"));
                return;
            }
        }

        const bool needsRequestFilter = browserOptions.interceptRequests || !browserOptions.customSchemes.empty()
            || !browserOptions.embeddedBundle.empty() || browserOptions.interceptResponses;
        if (needsRequestFilter) {
            webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        }

        if (browserOptions.clearCacheOnStart) {
            ComPtr<ICoreWebView2_13> webview13;
            ComPtr<ICoreWebView2Profile2> profile2;
            if (SUCCEEDED(webview.As(IID_ICoreWebView2_13, webview13))) {
                ComPtr<ICoreWebView2Profile> profile;
                if (SUCCEEDED(webview13->get_Profile(profile.Put()))
                    && SUCCEEDED(profile.As(IID_ICoreWebView2Profile2, profile2))) {
                    ++readyPending;
                    std::weak_ptr<std::atomic<bool>> weakAlive = alive;
                    WindowsWindowBackend* self = this;
                    auto* clearHandler = new ClearBrowsingDataCompletedHandler([weakAlive, self](HRESULT) {
                        if (const std::shared_ptr<std::atomic<bool>> state = weakAlive.lock(); state && state->load()) {
                            self->CompleteReadyTask();
                        }
                    });
                    profile2->ClearBrowsingDataAll(clearHandler);
                    clearHandler->Release();
                }
            }
        }

        for (const std::string& script : browserOptions.documentStartScripts) {
            RegisterScript(script);
        }
        for (const std::string& script : browserOptions.documentEndScripts) {
            const std::string wrapped = "(function(){var code=" + JSON(script).dump() + ";function run(){try{(new Function(code))();}catch(error){if(window.console){console.error(error);}}}if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',run);}else{run();}})();";
            RegisterScript(wrapped);
        }

        controllerConfigured = true;
        if (readyPending == 0) {
            NotifyBrowserReady();
        }
    }

    void WindowsWindowBackend::RegisterScript(const std::string& script) {
        if (!webview) {
            return;
        }
        ++readyPending;
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;
        auto* handler = new AddScriptCompletedHandler([weakAlive, self](HRESULT result, LPCWSTR) {
            if (const std::shared_ptr<std::atomic<bool>> state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            if (FAILED(result) && !self->readyFailed) {
                self->readyFailed = true;
                self->NotifyBrowserFailure(
                    Error(ErrorCode::PLATFORM_FAILURE, "Unable to inject a document script", FormatHResult(result)));
            }
            self->CompleteReadyTask();
        });
        webview->AddScriptToExecuteOnDocumentCreated(Utf8ToWide(script).c_str(), handler);
        handler->Release();
    }

    void WindowsWindowBackend::CompleteReadyTask() {
        if (readyPending > 0) {
            --readyPending;
        }
        if (!destroyed && !readyFailed && controllerConfigured && readyPending == 0 && !readyRaised) {
            NotifyBrowserReady();
        }
    }

    void WindowsWindowBackend::NotifyBrowserReady() {
        if (readyRaised || destroyed || readyFailed) {
            return;
        }
        readyRaised = true;
        if (delegate) {
            delegate->OnBackendBrowserReady();
        }
    }

    void WindowsWindowBackend::NotifyBrowserFailure(const Error& error) {
        if (destroyed) {
            return;
        }
        readyFailed = true;
        if (delegate) {
            delegate->OnBackendBrowserCreationFailed(error);
        }
    }

    void WindowsWindowBackend::RegisterWebViewEvents() {
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;

        ComPtr<ICoreWebView2_2> webview2;
        webview.As(IID_ICoreWebView2_2, webview2);
        ComPtr<ICoreWebView2_4> webview4;
        webview.As(IID_ICoreWebView2_4, webview4);
        ComPtr<ICoreWebView2_11> webview11;
        webview.As(IID_ICoreWebView2_11, webview11);
        ComPtr<ICoreWebView2_15> webview15;
        webview.As(IID_ICoreWebView2_15, webview15);

        EventRegistrationToken token{};
        auto* navigationStarting = new NavigationStartingHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const std::string url = uri ? WideToUtf8(uri) : std::string();
                CoTaskMemFree(uri);
                BOOL redirected = FALSE;
                BOOL userInitiated = FALSE;
                args->get_IsRedirected(&redirected);
                args->get_IsUserInitiated(&userInitiated);
                const NavigationDecision decision = self->delegate
                    ? self->delegate->OnBackendNavigationStarting(
                          url, "GET", redirected != FALSE, true, false, userInitiated != FALSE)
                    : NavigationDecision{true, {}};
                if (!decision.allow) {
                    args->put_Cancel(TRUE);
                    if (!decision.redirectUrl.empty()) {
                        const std::wstring redirect = Utf8ToWide(decision.redirectUrl);
                        self->NavigateCore(redirect, true);
                    }
                }
            });
        webview->add_NavigationStarting(navigationStarting, &token);
        navigationStarting->Release();

        auto* navigationCompleted = new NavigationCompletedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                int statusCode = 0;
                ComPtr<ICoreWebView2NavigationCompletedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(
                        IID_ICoreWebView2NavigationCompletedEventArgs2, reinterpret_cast<void**>(args2.Put())))) {
                    args2->get_HttpStatusCode(&statusCode);
                }
                LPWSTR uri = nullptr;
                self->webview->get_Source(&uri);
                const std::string url = uri ? WideToUtf8(uri) : std::string();
                CoTaskMemFree(uri);
                Error error;
                if (!success) {
                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    args->get_WebErrorStatus(&status);
                    error = Error(
                        ErrorCode::NAVIGATION_BLOCKED, "Navigation failed", std::to_string(static_cast<int>(status)));
                }
                if (self->delegate) {
                    self->delegate->OnBackendNavigationCompleted(url, success != FALSE, statusCode, error);
                }
            });
        webview->add_NavigationCompleted(navigationCompleted, &token);
        navigationCompleted->Release();

        auto* sourceChanged = new SourceChangedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                BOOL newDocument = TRUE;
                args->get_IsNewDocument(&newDocument);
                LPWSTR uri = nullptr;
                self->webview->get_Source(&uri);
                const std::string url = uri ? WideToUtf8(uri) : std::string();
                CoTaskMemFree(uri);
                if (self->delegate) {
                    self->delegate->OnBackendSourceChanged(url, newDocument != FALSE, true);
                }
            });
        webview->add_SourceChanged(sourceChanged, &token);
        sourceChanged->Release();

        auto* contentLoading = new ContentLoadingHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                BOOL errorPage = FALSE;
                args->get_IsErrorPage(&errorPage);
                if (self->delegate) {
                    self->delegate->OnBackendContentLoading(errorPage != FALSE, true);
                }
            });
        webview->add_ContentLoading(contentLoading, &token);
        contentLoading->Release();

        auto* domContentLoaded = new DomContentLoadedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2DOMContentLoadedEventArgs*) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                if (self->delegate) {
                    self->delegate->OnBackendDomContentLoaded(true);
                }
            });
        if (webview2) {
            webview2->add_DOMContentLoaded(domContentLoaded, &token);
        }
        domContentLoaded->Release();

        auto* titleChanged = new DocumentTitleChangedHandler([weakAlive, self](ICoreWebView2*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            LPWSTR title = nullptr;
            self->webview->get_DocumentTitle(&title);
            const std::string value = title ? WideToUtf8(title) : std::string();
            CoTaskMemFree(title);
            if (self->delegate) {
                self->delegate->OnBackendDocumentTitleChanged(value);
            }
        });
        webview->add_DocumentTitleChanged(titleChanged, &token);
        titleChanged->Release();

        auto* faviconChanged = new FaviconChangedHandler([weakAlive, self](ICoreWebView2*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            ComPtr<ICoreWebView2_15> webview15;
            std::string value;
            if (SUCCEEDED(self->webview.As(IID_ICoreWebView2_15, webview15))) {
                LPWSTR uri = nullptr;
                webview15->get_FaviconUri(&uri);
                value = uri ? WideToUtf8(uri) : std::string();
                CoTaskMemFree(uri);
            }
            if (self->delegate) {
                self->delegate->OnBackendFaviconChanged(value);
            }
        });
        if (webview15) {
            webview15->add_FaviconChanged(faviconChanged, &token);
        }
        faviconChanged->Release();

        auto* newWindow = new NewWindowRequestedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                const std::string url = uri ? WideToUtf8(uri) : std::string();
                CoTaskMemFree(uri);
                BOOL userInitiated = FALSE;
                args->get_IsUserInitiated(&userInitiated);
                if (self->delegate) {
                    self->delegate->OnBackendNewWindowRequested(url, userInitiated != FALSE);
                }
            });
        webview->add_NewWindowRequested(newWindow, &token);
        newWindow->Release();

        auto* permission = new PermissionRequestedHandler([weakAlive, self](ICoreWebView2*,
                                                              ICoreWebView2PermissionRequestedEventArgs* args) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            COREWEBVIEW2_PERMISSION_KIND kind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
            args->get_PermissionKind(&kind);
            PermissionKind mapped = PermissionKind::OTHER;
            switch (kind) {
                case COREWEBVIEW2_PERMISSION_KIND_CAMERA:
                    mapped = PermissionKind::CAMERA;
                    break;
                case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:
                    mapped = PermissionKind::MICROPHONE;
                    break;
                case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:
                    mapped = PermissionKind::GEOLOCATION;
                    break;
                case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
                    mapped = PermissionKind::NOTIFICATIONS;
                    break;
                case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
                    mapped = PermissionKind::CLIPBOARD_READ;
                    break;
                case COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY:
                    mapped = PermissionKind::AUTOPLAY;
                    break;
                default:
                    break;
            }
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const std::string url = uri ? WideToUtf8(uri) : std::string();
            CoTaskMemFree(uri);
            const std::string origin = ExtractOrigin(url);
            PermissionDecision decision{false, false};
            if (self->delegate) {
                decision = self->delegate->OnBackendPermissionRequested(mapped, origin, url);
            }
            args->put_State(decision.allow ? COREWEBVIEW2_PERMISSION_STATE_ALLOW : COREWEBVIEW2_PERMISSION_STATE_DENY);
            ComPtr<ICoreWebView2PermissionRequestedEventArgs2> args2;
            if (SUCCEEDED(args->QueryInterface(
                    IID_ICoreWebView2PermissionRequestedEventArgs2, reinterpret_cast<void**>(args2.Put())))) {
                args2->put_Handled(TRUE);
            }
        });
        webview->add_PermissionRequested(permission, &token);
        permission->Release();

        auto* processFailed = new ProcessFailedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                args->get_ProcessFailedKind(&kind);
                int exitCode = 0;
                ComPtr<ICoreWebView2ProcessFailedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(
                        IID_ICoreWebView2ProcessFailedEventArgs2, reinterpret_cast<void**>(args2.Put())))) {
                    args2->get_ExitCode(&exitCode);
                }
                const std::string reason = std::to_string(static_cast<int>(kind));
                if (self->delegate) {
                    self->delegate->OnBackendProcessFailed(
                        reason, exitCode, kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED);
                }
            });
        webview->add_ProcessFailed(processFailed, &token);
        processFailed->Release();

        auto* message = new WebMessageReceivedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleTopLevelMessage(args);
            });
        webview->add_WebMessageReceived(message, &token);
        message->Release();

        auto* resourceRequested = new WebResourceRequestedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleWebResourceRequested(args);
            });
        webview->add_WebResourceRequested(resourceRequested, &token);
        resourceRequested->Release();

        auto* responseReceived = new WebResourceResponseReceivedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleWebResourceResponse(args);
            });
        if (webview2) {
            webview2->add_WebResourceResponseReceived(responseReceived, &token);
        }
        responseReceived->Release();

        auto* downloadStarting = new DownloadStartingHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleDownloadStarting(args);
            });
        if (webview4) {
            webview4->add_DownloadStarting(downloadStarting, &token);
        }
        downloadStarting->Release();

        auto* scriptDialog = new ScriptDialogOpeningHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleScriptDialog(args);
            });
        webview->add_ScriptDialogOpening(scriptDialog, &token);
        scriptDialog->Release();

        auto* fullscreen = new ContainsFullScreenElementChangedHandler([weakAlive, self](ICoreWebView2*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            BOOL contains = FALSE;
            self->webview->get_ContainsFullScreenElement(&contains);
            const bool enter = contains != FALSE;
            const bool allow = self->delegate ? self->delegate->OnBackendFullscreenRequested(enter) : true;
            if (enter && allow) {
                self->html5Fullscreen = true;
                self->EnterFullscreenWindow();
            } else if (enter && !allow) {
                self->webview->ExecuteScript(L"document.exitFullscreen && document.exitFullscreen();", nullptr);
            } else if (!enter && self->html5Fullscreen) {
                self->html5Fullscreen = false;
                self->ExitFullscreenWindow();
            }
        });
        webview->add_ContainsFullScreenElementChanged(fullscreen, &token);
        fullscreen->Release();

        auto* contextMenu = new ContextMenuRequestedHandler(
            [weakAlive, self](ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleContextMenu(args);
            });
        if (webview11) {
            webview11->add_ContextMenuRequested(contextMenu, &token);
        }
        contextMenu->Release();

        ComPtr<ICoreWebView2_10> webview10;
        if (SUCCEEDED(webview.As(IID_ICoreWebView2_10, webview10))) {
            auto* authentication = new BasicAuthenticationRequestedHandler(
                [weakAlive, self](ICoreWebView2*, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) {
                    if (const auto state = weakAlive.lock(); !state || !state->load()) {
                        return;
                    }
                    self->HandleAuthentication(args);
                });
            webview10->add_BasicAuthenticationRequested(authentication, &token);
            authentication->Release();
        }

        ComPtr<ICoreWebView2_14> webview14;
        if (SUCCEEDED(webview.As(IID_ICoreWebView2_14, webview14))) {
            auto* certificate = new ServerCertificateErrorDetectedHandler(
                [weakAlive, self](ICoreWebView2*, ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) {
                    if (const auto state = weakAlive.lock(); !state || !state->load()) {
                        return;
                    }
                    self->HandleCertificateError(args);
                });
            webview14->add_ServerCertificateErrorDetected(certificate, &token);
            certificate->Release();
        }

        if (webview4) {
            auto* frameCreated = new FrameCreatedHandler(
                [weakAlive, self](ICoreWebView2*, ICoreWebView2FrameCreatedEventArgs* args) {
                    if (const auto state = weakAlive.lock(); !state || !state->load()) {
                        return;
                    }
                    ComPtr<ICoreWebView2Frame> frame;
                    if (SUCCEEDED(args->get_Frame(frame.Put())) && frame) {
                        self->RegisterFrame(frame.Get());
                    }
                });
            webview4->add_FrameCreated(frameCreated, &token);
            frameCreated->Release();
        }
    }

    void WindowsWindowBackend::RegisterAcceleratorEvents() {
        if (!controller) {
            return;
        }
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;
        EventRegistrationToken token{};

        auto* accelerator = new AcceleratorKeyPressedHandler(
            [weakAlive, self](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                if (self->keyboardHook) {
                    return;
                }
                COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                args->get_KeyEventKind(&kind);
                UINT virtualKey = 0;
                args->get_VirtualKey(&virtualKey);
                COREWEBVIEW2_PHYSICAL_KEY_STATUS status{};
                args->get_PhysicalKeyStatus(&status);
                const bool down = kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                    || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN;
                const bool repeat = status.WasKeyDown != FALSE;
                const VirtualKeyInfo info = DescribeVirtualKey(virtualKey, 0);
                const bool cancel = self->delegate ? self->delegate->OnBackendKey(down, info.key, info.code,
                                                         static_cast<int>(virtualKey), CurrentModifiers(), repeat)
                                                   : false;
                if (cancel) {
                    args->put_Handled(TRUE);
                }
            });
        controller->add_AcceleratorKeyPressed(accelerator, &token);
        accelerator->Release();

        auto* gotFocus = new GotFocusHandler([weakAlive, self](ICoreWebView2Controller*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            if (!self->focused) {
                self->focused = true;
                if (self->delegate) {
                    self->delegate->OnBackendFocusChanged(true);
                }
            }
        });
        controller->add_GotFocus(gotFocus, &token);
        gotFocus->Release();

        auto* lostFocus = new LostFocusHandler([weakAlive, self](ICoreWebView2Controller*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            if (self->focused) {
                self->focused = false;
                if (self->delegate) {
                    self->delegate->OnBackendFocusChanged(false);
                }
            }
        });
        controller->add_LostFocus(lostFocus, &token);
        lostFocus->Release();

        auto* zoom = new ZoomFactorChangedHandler([weakAlive, self](ICoreWebView2Controller*, IUnknown*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            double factor = 1.0;
            self->controller->get_ZoomFactor(&factor);
            self->zoomFactor = factor;
            if (self->delegate) {
                self->delegate->OnBackendZoomChanged(factor);
            }
        });
        controller->add_ZoomFactorChanged(zoom, &token);
        zoom->Release();
    }

    void WindowsWindowBackend::RegisterFrame(ICoreWebView2Frame* frame) {
        if (!frame) {
            return;
        }
        ComPtr<ICoreWebView2Frame2> frame2;
        if (FAILED(frame->QueryInterface(IID_ICoreWebView2Frame2, reinterpret_cast<void**>(frame2.Put())))) {
            return;
        }
        const std::string identifier = FrameId(frame);
        frames[identifier] = frame2;

        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        WindowsWindowBackend* self = this;
        EventRegistrationToken token{};
        auto* frameDestroyed = new FrameDestroyedHandler([weakAlive, self, identifier](ICoreWebView2Frame*) {
            if (const auto state = weakAlive.lock(); !state || !state->load()) {
                return;
            }
            self->frames.erase(identifier);
        });
        frame->add_Destroyed(frameDestroyed, &token);
        frameDestroyed->Release();

        auto* frameMessage = new FrameWebMessageReceivedHandler(
            [weakAlive, self](ICoreWebView2Frame* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
                if (const auto state = weakAlive.lock(); !state || !state->load()) {
                    return;
                }
                self->HandleFrameMessage(sender, args);
            });
        frame2->add_WebMessageReceived(frameMessage, &token);
        frameMessage->Release();
    }

    std::string WindowsWindowBackend::FrameId(ICoreWebView2Frame* frame) const {
        return PointerToHex(frame);
    }

    void WindowsWindowBackend::UpdateControllerBounds() {
        if (!controller || !window) {
            return;
        }
        RECT client{};
        GetClientRect(window, &client);
        controller->put_Bounds(client);
        controller->NotifyParentWindowPositionChanged();
    }

    void WindowsWindowBackend::UpdateBackgroundBrush(Color color) {
        currentBackground = color;
        HBRUSH brush = CreateSolidBrush(ToColorRef(color));
        if (backgroundBrush) {
            DeleteObject(backgroundBrush);
        }
        backgroundBrush = brush;
        if (window) {
            InvalidateRect(window, nullptr, TRUE);
        }
    }

    void WindowsWindowBackend::UpdateLayer() {
        if (!window) {
            return;
        }
        const bool useLayer = options.transparent || windowOpacity < 1.0;
        LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (useLayer) {
            if ((extended & WS_EX_LAYERED) == 0) {
                SetWindowLongPtrW(window, GWL_EXSTYLE, extended | WS_EX_LAYERED);
            }
            const COLORREF key = ToColorRef(currentBackground);
            const BYTE alpha = static_cast<BYTE>(std::clamp(windowOpacity, 0.0, 1.0) * 255.0);
            const DWORD flags = (windowOpacity < 1.0 ? LWA_ALPHA : 0u) | (options.transparent ? LWA_COLORKEY : 0u);
            SetLayeredWindowAttributes(window, options.transparent ? key : 0, alpha, flags);
        } else if ((extended & WS_EX_LAYERED) != 0) {
            SetWindowLongPtrW(window, GWL_EXSTYLE, extended & ~WS_EX_LAYERED);
        }
    }

    void WindowsWindowBackend::UpdateTitleBarTheme() {
        if (!window) {
            return;
        }
        darkMode = effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemUsesDarkMode());
        ApplyDarkTitleBar(window, darkMode);
    }

    void WindowsWindowBackend::ApplyRoundedAndShadow() {
        if (!window) {
            return;
        }
        ApplyRoundedCorners(window, options.roundedCorners);
        ApplyBorderlessShadow(window, IsCustomChrome() && options.borderlessShadow);
    }

    bool WindowsWindowBackend::IsCustomChrome() const {
        return options.frameless || options.titleBarStyle != TitleBarStyle::NATIVE;
    }

    void WindowsWindowBackend::ApplyInitialPlacement() {
        if (!window) {
            return;
        }
        if (options.hasPosition) {
            SetWindowPos(window, nullptr, ScaleForDpi(options.x, dpi), ScaleForDpi(options.y, dpi), 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        GetWindowRect(window, &normalRect);
        hasNormalRect = true;
    }

    void WindowsWindowBackend::ApplyWindowStyles() {
        if (!window) {
            return;
        }
        const LONG_PTR current = GetWindowLongPtrW(window, GWL_STYLE);
        DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        if (IsCustomChrome()) {
            style |= WS_POPUP;
            if (options.resizable) {
                style |= WS_THICKFRAME;
            }
            if (options.minimizable) {
                style |= WS_MINIMIZEBOX;
            }
            if (options.maximizable) {
                style |= WS_MAXIMIZEBOX;
            }
            if (options.closable) {
                style |= WS_SYSMENU;
            }
        } else {
            style |= WS_OVERLAPPEDWINDOW;
            if (!options.resizable) {
                style &= ~WS_THICKFRAME;
            }
            if (!options.minimizable) {
                style &= ~WS_MINIMIZEBOX;
            }
            if (!options.maximizable) {
                style &= ~WS_MAXIMIZEBOX;
            }
        }
        if ((current & WS_VISIBLE) != 0) {
            style |= WS_VISIBLE;
        }
        SetWindowLongPtrW(window, GWL_STYLE, static_cast<LONG_PTR>(style));
        SetWindowPos(
            window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    Result<void> WindowsWindowBackend::SetStyleFlag(LONG flag, bool enabled) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (enabled) {
            style |= flag;
        } else {
            style &= ~flag;
        }
        SetWindowLongPtrW(window, GWL_STYLE, style);
        SetWindowPos(
            window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return Result<void>::Ok();
    }

    void WindowsWindowBackend::EnterFullscreenWindow() {
        if (!window || fullscreen) {
            return;
        }
        savedStyle = static_cast<LONG>(GetWindowLongPtrW(window, GWL_STYLE));
        savedExStyle = static_cast<LONG>(GetWindowLongPtrW(window, GWL_EXSTYLE));
        savedPlacement.length = sizeof(savedPlacement);
        GetWindowPlacement(window, &savedPlacement);
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(monitor, &monitorInfo);
        SetWindowLongPtrW(window, GWL_STYLE,
            static_cast<LONG_PTR>(
                (savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_POPUP
                | (savedStyle & WS_VISIBLE)));
        SetWindowLongPtrW(window, GWL_EXSTYLE, static_cast<LONG_PTR>(savedExStyle));
        SetWindowPos(window, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        fullscreen = true;
        if (delegate) {
            delegate->OnBackendFullscreenChanged(true);
        }
    }

    void WindowsWindowBackend::ExitFullscreenWindow() {
        if (!window || !fullscreen) {
            return;
        }
        SetWindowLongPtrW(window, GWL_STYLE, savedStyle);
        SetWindowLongPtrW(window, GWL_EXSTYLE, savedExStyle);
        savedPlacement.length = sizeof(savedPlacement);
        SetWindowPlacement(window, &savedPlacement);
        SetWindowPos(
            window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        fullscreen = false;
        if (delegate) {
            delegate->OnBackendFullscreenChanged(false);
        }
    }

    void WindowsWindowBackend::SaveNormalPlacement() {
        if (!window || maximized || fullscreen) {
            return;
        }
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(window, &placement)) {
            normalRect = placement.rcNormalPosition;
            hasNormalRect = true;
        }
    }

    void WindowsWindowBackend::ReleaseBrowser() {
        downloads.clear();
        frames.clear();
        webview.Reset();
        if (controller) {
            controller->Close();
            controller.Reset();
        }
        environment.Reset();
        environmentOptions.Reset();
        readyRaised = false;
        controllerConfigured = false;
    }

    void WindowsWindowBackend::CloseInternal(bool notify) {
        if (destroyed || closing) {
            return;
        }
        closing = true;
        if (!notify) {
            suppressEvents = true;
        }
        RemoveKeyboardHook();
        ReleaseBrowser();
        if (window && IsWindow(window)) {
            DestroyWindow(window);
        }
        window = nullptr;
    }

    void WindowsWindowBackend::Close() {
        CloseInternal(true);
    }

    void* WindowsWindowBackend::GetNativeHandle() const {
        return window;
    }

    const PlatformCapabilities& WindowsWindowBackend::Capabilities() const {
        return capabilities;
    }

    Result<void> WindowsWindowBackend::Show() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (pendingMaximize) {
            pendingMaximize = false;
            ShowWindow(window, SW_SHOWMAXIMIZED);
        } else {
            ShowWindow(window, SW_SHOW);
        }
        UpdateControllerBounds();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Hide() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        ShowWindow(window, SW_HIDE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Minimize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        ShowWindow(window, SW_MINIMIZE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Maximize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (!IsWindowVisible(window)) {
            pendingMaximize = true;
            return Result<void>::Ok();
        }
        ShowWindow(window, SW_MAXIMIZE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Restore() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (fullscreen) {
            ExitFullscreenWindow();
        }
        ShowWindow(window, SW_RESTORE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Focus() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (IsIconic(window)) {
            ShowWindow(window, SW_RESTORE);
        }
        SetForegroundWindow(window);
        if (controller) {
            controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::StartDrag() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (!options.movable) {
            return Error(ErrorCode::PERMISSION_DENIED, "The window is not movable");
        }
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetTitle(const std::string& title) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        SetWindowTextW(window, Utf8ToWide(title).c_str());
        options.title = title;
        return Result<void>::Ok();
    }

    Result<std::string> WindowsWindowBackend::GetTitle() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        const int length = GetWindowTextLengthW(window);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
        text.resize(static_cast<std::size_t>(length));
        return WideToUtf8(text);
    }

    Result<void> WindowsWindowBackend::SetSize(int width, int height) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        SetWindowPos(window, nullptr, 0, 0, ScaleForDpi(width, dpi), ScaleForDpi(height, dpi),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetPosition(int x, int y) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        SetWindowPos(window, nullptr, ScaleForDpi(x, dpi), ScaleForDpi(y, dpi), 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetMinSize(int width, int height) {
        options.minWidth = width;
        options.minHeight = height;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetMaxSize(int width, int height) {
        options.maxWidth = width;
        options.maxHeight = height;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetFullscreen(bool value) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (value) {
            EnterFullscreenWindow();
        } else {
            ExitFullscreenWindow();
        }
        options.fullscreen = value;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetAlwaysOnTop(bool alwaysOnTop) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        SetWindowPos(
            window, alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        options.alwaysOnTop = alwaysOnTop;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetOpacity(double opacity) {
        windowOpacity = opacity;
        options.opacity = opacity;
        UpdateLayer();
        return Result<void>::Ok();
    }

    Result<double> WindowsWindowBackend::GetOpacity() const {
        return windowOpacity;
    }

    Result<void> WindowsWindowBackend::Center() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (maximized || fullscreen) {
            return Result<void>::Ok();
        }
        RECT bounds{};
        GetWindowRect(window, &bounds);
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to query the monitor work area");
        }
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        const int x = monitorInfo.rcWork.left + ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - width) / 2;
        const int y = monitorInfo.rcWork.top + ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - height) / 2;
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetBackgroundColor(Color color) {
        options.backgroundColor = color;
        UpdateBackgroundBrush(color);
        UpdateLayer();
        if (controller) {
            ComPtr<ICoreWebView2Controller2> controller2;
            if (SUCCEEDED(controller.As(IID_ICoreWebView2Controller2, controller2))) {
                Color browserColor = color;
                if (options.transparent) {
                    browserColor.a = 0;
                }
                COREWEBVIEW2_COLOR nativeColor{browserColor.a, browserColor.r, browserColor.g, browserColor.b};
                controller2->put_DefaultBackgroundColor(nativeColor);
            }
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetTheme(Theme theme) {
        effectiveTheme = theme;
        options.theme = theme;
        UpdateTitleBarTheme();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetContentProtection(bool enabled) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (!SetWindowDisplayAffinity(window, enabled ? DisplayAffinityExclude : WDA_NONE)) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to change content protection");
        }
        options.contentProtection = enabled;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetResizable(bool resizable) {
        options.resizable = resizable;
        ApplyWindowStyles();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetMovable(bool movable) {
        options.movable = movable;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetMinimizable(bool minimizable) {
        options.minimizable = minimizable;
        ApplyWindowStyles();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetMaximizable(bool maximizable) {
        options.maximizable = maximizable;
        ApplyWindowStyles();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetClosable(bool closable) {
        options.closable = closable;
        ApplyWindowStyles();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetShowInTaskbar(bool show) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
        extended &= ~(WS_EX_APPWINDOW | WS_EX_TOOLWINDOW);
        extended |= show ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(window, GWL_EXSTYLE, extended);
        SetWindowPos(
            window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        options.showInTaskbar = show;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetIcon(const Icon& iconValue) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        if (iconValue.IsEmpty()) {
            return Error(ErrorCode::INVALID_ARGUMENT, "The window icon is empty");
        }
        HICON icon = nullptr;
        if (iconValue.source == IconSource::RESOURCE) {
            icon = LoadIconFromResource(iconValue.resource);
        } else {
            const std::wstring widePath = Utf8ToWide(PathToUtf8(iconValue.path));
            icon = static_cast<HICON>(
                LoadImageW(nullptr, widePath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
            if (!icon) {
                icon = LoadIconFromImageFile(widePath);
            }
        }
        if (!icon) {
            return Error(ErrorCode::IO_FAILURE,
                "Unable to load the window icon; use ICO, PNG, JPEG, GIF, BMP, or a native resource");
        }
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        if (windowIcon) {
            DestroyIcon(windowIcon);
        }
        windowIcon = icon;
        options.icon = iconValue;
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetDraggableRegions(const std::vector<Rect>& regions) {
        draggableRegions = regions;
        options.draggableRegions = regions;
        return Result<void>::Ok();
    }

    Result<Rect> WindowsWindowBackend::GetBounds() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The native window is unavailable");
        }
        RECT rect{};
        GetWindowRect(window, &rect);
        return NativeToRect(rect);
    }

    bool WindowsWindowBackend::IsVisible() const {
        return window && IsWindowVisible(window);
    }

    bool WindowsWindowBackend::IsMaximized() const {
        return window && IsZoomed(window);
    }

    bool WindowsWindowBackend::IsMinimized() const {
        return window && IsIconic(window);
    }

    bool WindowsWindowBackend::IsFocused() const {
        return window && GetForegroundWindow() == window;
    }

    bool WindowsWindowBackend::IsFullscreen() const {
        return fullscreen;
    }

    double WindowsWindowBackend::GetDpiScale() const {
        return static_cast<double>(window ? WindowDpi(window) : dpi) / 96.0;
    }

    Result<void> WindowsWindowBackend::RequireWebView() const {
        if (!webview || !controller) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The WebView2 browser is not ready");
        }
        return Result<void>::Ok();
    }

    std::wstring WindowsWindowBackend::ResolveUserDataFolder() {
        std::filesystem::path folder = browserOptions.userDataFolder;
        if (folder.empty()) {
            wchar_t temporaryPath[MAX_PATH]{};
            const DWORD length = GetTempPathW(MAX_PATH, temporaryPath);
            std::filesystem::path base = length > 0 && length < MAX_PATH
                ? std::filesystem::path(std::wstring(temporaryPath, length))
                : std::filesystem::temp_directory_path();
            static std::atomic<unsigned> counter{0};
            folder = base
                / ("flare-webview2-" + std::to_string(GetCurrentProcessId()) + "-"
                    + std::to_string(counter.fetch_add(1)));
            ownsTemporaryDataFolder = true;
            temporaryDataFolder = folder;
        }
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        return Utf8ToWide(PathToUtf8(folder));
    }

    std::wstring WindowsWindowBackend::ComposeBrowserArguments() const {
        std::wstring arguments;
        for (const std::string& argument : browserOptions.browserArguments) {
            AppendArgument(arguments, Utf8ToWide(argument));
        }
        if (!CurrentWindowsAppSettings().hardwareAcceleration && !ContainsArgument(arguments, L"--disable-gpu")) {
            AppendArgument(arguments, L"--disable-gpu");
        }
        if (!browserOptions.webSecurityEnabled) {
            AppendArgument(arguments, L"--disable-web-security");
        }
        if (browserOptions.allowFileAccessFromFileUrls) {
            AppendArgument(arguments, L"--allow-file-access-from-files");
        }
        if (browserOptions.proxy.enabled) {
            if (!browserOptions.proxy.server.empty()) {
                AppendArgument(arguments, L"--proxy-server=" + Utf8ToWide(browserOptions.proxy.server));
            }
            if (!browserOptions.proxy.pacUrl.empty()) {
                AppendArgument(arguments, L"--proxy-pac-url=" + Utf8ToWide(browserOptions.proxy.pacUrl));
            }
            if (!browserOptions.proxy.bypassList.empty()) {
                std::wstring bypass;
                for (const std::string& entry : browserOptions.proxy.bypassList) {
                    if (!bypass.empty()) {
                        bypass += L';';
                    }
                    bypass += Utf8ToWide(entry);
                }
                AppendArgument(arguments, L"--proxy-bypass-list=" + bypass);
            }
        }
        if (!browserOptions.language.empty()) {
            AppendArgument(arguments, L"--lang=" + Utf8ToWide(browserOptions.language));
        }
        return arguments;
    }

    LRESULT CALLBACK WindowsWindowBackend::WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
        WindowsWindowBackend* self = reinterpret_cast<WindowsWindowBackend*>(
            GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<WindowsWindowBackend*>(create->lpCreateParams);
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self) {
                self->window = windowHandle;
            }
        }
        if (!self) {
            return DefWindowProcW(windowHandle, message, wParam, lParam);
        }
        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT CALLBACK WindowsWindowBackend::KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
        static thread_local bool handling = false;
        if (code == HC_ACTION && !handling) {
            auto* message = reinterpret_cast<MSG*>(lParam);
            if (message && IsKeyboardMessage(message->message)) {
                HWND root = GetAncestor(message->hwnd, GA_ROOT);
                auto* self = root ? reinterpret_cast<WindowsWindowBackend*>(GetWindowLongPtrW(root, GWLP_USERDATA))
                                  : nullptr;
                if (self && self->window == root) {
                    handling = true;
                    self->HandleKeyboardMessage(*message);
                    handling = false;
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void WindowsWindowBackend::HandleKeyboardMessage(MSG& message) {
        if (!delegate || destroyed || closing) {
            return;
        }
        const bool down = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
        const bool repeat = (message.lParam & (1L << 30)) != 0;
        const UINT virtualKey = static_cast<UINT>(message.wParam);
        const VirtualKeyInfo info = DescribeVirtualKey(virtualKey, message.lParam);
        const bool cancel = delegate->OnBackendKey(
            down, info.key, info.code, static_cast<int>(virtualKey), CurrentModifiers(), repeat);
        if (cancel) {
            message.message = WM_NULL;
            message.wParam = 0;
            message.lParam = 0;
        }
    }

    void WindowsWindowBackend::InstallKeyboardHook() {
        if (keyboardHook || !window) {
            return;
        }
        keyboardHook = SetWindowsHookExW(
            WH_GETMESSAGE, &WindowsWindowBackend::KeyboardHookProc, nullptr, GetCurrentThreadId());
    }

    void WindowsWindowBackend::RemoveKeyboardHook() {
        if (keyboardHook) {
            UnhookWindowsHookEx(keyboardHook);
            keyboardHook = nullptr;
        }
    }

    LRESULT WindowsWindowBackend::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_NCCALCSIZE:
                if (wParam != FALSE && IsCustomChrome()) {
                    if (IsZoomed(window)) {
                        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                        MONITORINFO monitorInfo{};
                        monitorInfo.cbSize = sizeof(monitorInfo);
                        if (params
                            && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
                            params->rgrc[0] = monitorInfo.rcWork;
                        }
                    }
                    return 0;
                }
                return DefWindowProcW(window, message, wParam, lParam);
            case WM_ERASEBKGND: {
                if (!backgroundBrush || !window) {
                    return 0;
                }
                RECT rect{};
                GetClientRect(window, &rect);
                HDC device = reinterpret_cast<HDC>(wParam);
                FillRect(device, &rect, backgroundBrush);
                return 1;
            }
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                HDC device = BeginPaint(window, &paint);
                RECT rect{};
                GetClientRect(window, &rect);
                if (backgroundBrush) {
                    FillRect(device, &rect, backgroundBrush);
                }
                EndPaint(window, &paint);
                return 0;
            }
            case WM_SIZE: {
                UpdateControllerBounds();
                const bool nowMinimized = wParam == SIZE_MINIMIZED;
                const bool nowMaximized = wParam == SIZE_MAXIMIZED;
                if (nowMinimized && !minimized && !suppressEvents && delegate) {
                    delegate->OnBackendMinimized();
                }
                if (nowMaximized && !maximized && !suppressEvents && delegate) {
                    delegate->OnBackendMaximized();
                }
                if (wParam == SIZE_RESTORED && (minimized || maximized) && !suppressEvents && delegate) {
                    delegate->OnBackendRestored();
                }
                minimized = nowMinimized;
                maximized = nowMaximized;
                if (!minimized && !maximized && !fullscreen) {
                    SaveNormalPlacement();
                }
                if (!suppressEvents && delegate) {
                    RECT rect{};
                    GetWindowRect(window, &rect);
                    delegate->OnBackendBoundsChanged(NativeToRect(rect), maximized);
                }
                return 0;
            }
            case WM_MOVE: {
                if (!suppressEvents && delegate && !IsIconic(window)) {
                    RECT rect{};
                    GetWindowRect(window, &rect);
                    delegate->OnBackendBoundsChanged(NativeToRect(rect), maximized);
                }
                return 0;
            }
            case WM_DPICHANGED: {
                const UINT newDpi = HIWORD(wParam);
                dpi = newDpi > 0 ? newDpi : dpi;
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested) {
                    SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                        suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
                }
                if (controller) {
                    ComPtr<ICoreWebView2Controller3> controller3;
                    if (SUCCEEDED(controller.As(IID_ICoreWebView2Controller3, controller3))) {
                        controller3->put_RasterizationScale(static_cast<double>(dpi) / 96.0);
                    }
                }
                if (!suppressEvents && delegate) {
                    RECT rect{};
                    GetWindowRect(window, &rect);
                    delegate->OnBackendDpiChanged(static_cast<double>(dpi) / 96.0, NativeToRect(rect));
                }
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                if (!info) {
                    return 0;
                }
                info->ptMinTrackSize.x = ScaleForDpi(options.minWidth > 0 ? options.minWidth : 1, dpi);
                info->ptMinTrackSize.y = ScaleForDpi(options.minHeight > 0 ? options.minHeight : 1, dpi);
                if (options.maxWidth > 0) {
                    info->ptMaxTrackSize.x = ScaleForDpi(options.maxWidth, dpi);
                }
                if (options.maxHeight > 0) {
                    info->ptMaxTrackSize.y = ScaleForDpi(options.maxHeight, dpi);
                }
                return 0;
            }
            case WM_SHOWWINDOW: {
                const bool nowVisible = wParam != FALSE;
                if (visible != nowVisible) {
                    visible = nowVisible;
                    if (!suppressEvents && delegate) {
                        delegate->OnBackendVisibilityChanged(nowVisible);
                    }
                }
                return 0;
            }
            case WM_SETFOCUS: {
                if (!focused) {
                    focused = true;
                    if (!suppressEvents && delegate) {
                        delegate->OnBackendFocusChanged(true);
                    }
                }
                return 0;
            }
            case WM_KILLFOCUS: {
                if (focused) {
                    focused = false;
                    if (!suppressEvents && delegate) {
                        delegate->OnBackendFocusChanged(false);
                    }
                }
                return 0;
            }
            case WM_ENTERSIZEMOVE:
                return 0;
            case WM_EXITSIZEMOVE:
                UpdateControllerBounds();
                SaveNormalPlacement();
                return 0;
            case WM_NCHITTEST: {
                const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
                if (result == HTCLIENT && options.movable && !draggableRegions.empty()) {
                    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ScreenToClient(window, &point);
                    for (const Rect& region : draggableRegions) {
                        if (region.Contains(Point{static_cast<double>(point.x), static_cast<double>(point.y)})) {
                            return HTCAPTION;
                        }
                    }
                }
                if (!options.movable && result == HTCAPTION) {
                    return HTCLIENT;
                }
                return result;
            }
            case WM_NCLBUTTONDOWN:
                if (!options.movable && wParam == HTCAPTION) {
                    return 0;
                }
                break;
            case WM_SYSCOMMAND: {
                const WPARAM command = wParam & 0xFFF0;
                if (command == SC_MOVE && !options.movable) {
                    return 0;
                }
                if (command == SC_MINIMIZE && !options.minimizable) {
                    return 0;
                }
                if (command == SC_MAXIMIZE && !options.maximizable) {
                    return 0;
                }
                if (command == SC_CLOSE && !options.closable) {
                    return 0;
                }
                break;
            }
            case WM_CLOSE: {
                if (suppressEvents) {
                    CloseInternal(false);
                    return 0;
                }
                if (!options.closable) {
                    return 0;
                }
                if (delegate && !delegate->OnBackendCloseRequested()) {
                    return 0;
                }
                CloseInternal(true);
                return 0;
            }
            case WM_DESTROY: {
                if (!destroyed) {
                    destroyed = true;
                    visible = false;
                    if (parentWindow && parentWasEnabled && IsWindow(parentWindow)) {
                        EnableWindow(parentWindow, TRUE);
                        parentWasEnabled = false;
                    }
                    ReleaseBrowser();
                    DragAcceptFiles(window, FALSE);
                    if (!suppressEvents && delegate) {
                        delegate->OnBackendClosed();
                    }
                }
                return 0;
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                return DefWindowProcW(window, message, wParam, lParam);
            case WM_SETTINGCHANGE: {
                if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                    UpdateTitleBarTheme();
                    if (!suppressEvents && delegate) {
                        delegate->OnBackendThemeChanged(effectiveTheme, darkMode ? Theme::DARK : Theme::LIGHT);
                    }
                }
                break;
            }
            case WM_DROPFILES: {
                auto drop = reinterpret_cast<HDROP>(wParam);
                if (!drop) {
                    return 0;
                }
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                std::vector<std::filesystem::path> files;
                files.reserve(count);
                for (UINT index = 0; index < count; ++index) {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    if (length == 0) {
                        continue;
                    }
                    std::wstring path(length + 1, L'\0');
                    DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
                    path.resize(length);
                    files.emplace_back(path);
                }
                POINT point{};
                DragQueryPoint(drop, &point);
                DragFinish(drop);
                if (!suppressEvents && delegate) {
                    delegate->OnBackendFileDrop(
                        std::move(files), Point{static_cast<double>(point.x), static_cast<double>(point.y)});
                }
                return 0;
            }
            default:
                break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    std::unique_ptr<WindowBackend> CreatePlatformWindowBackend() {
        return std::make_unique<WindowsWindowBackend>();
    }
}
