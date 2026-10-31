#include "flare/window.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "backend/backend.h"
#include "bridge/bridge-server.h"
#include "bridge-script-data.h"
#include "core/app-runtime.h"
#include "core/icon-utils.h"
#include "core/json-utils.h"
#include "core/text-utils.h"
#include "core/url-utils.h"
#include "core/window-state.h"
#include "flare/version.h"

namespace Flare {
    namespace {
        bool IsSafeNavigationTarget(std::string_view url) {
            if (url.empty() || url.size() > 32768) {
                return false;
            }
            for (unsigned char byte : url) {
                if (byte < 0x20 || byte == 0x7F || byte == 0) {
                    return false;
                }
            }
            const std::string lowered = ToLowerAscii(
                url.substr(0, url.find(':') == std::string_view::npos ? url.size() : url.find(':') + 1));
            return lowered != "javascript:" && lowered != "vbscript:";
        }

        std::string BuildScrollbarScript() {
            return "(function(){function flareHideScrollbars(){var style=document.createElement('style');"
                   "style.textContent='::-webkit-scrollbar{width:0;height:0;display:none}html{scrollbar-width:none}';"
                   "(document.head||document.documentElement).appendChild(style);}"
                   "if(document.head||document.documentElement){flareHideScrollbars();}else{document.addEventListener('"
                   "DOMContentLoaded',flareHideScrollbars);}})();";
        }

        std::string BuildContextMenuScript() {
            return "document.addEventListener('contextmenu',function(event){event.preventDefault();},true);";
        }

        bool IsIntegerInRange(double value, double minimum, double maximum) {
            return std::isfinite(value) && std::floor(value) == value && value >= minimum && value <= maximum;
        }
    }

    struct Window::Impl : WindowBackendDelegate {
        std::shared_ptr<AppRuntime> runtime;
        App* app = nullptr;
        Window* owner = nullptr;
        WindowId id = 0;
        WindowOptions options;
        BrowserOptions browserOptions;
        BridgeOptions bridgeOptions;
        PlatformCapabilities capabilities;

        std::unique_ptr<WindowBackend> backend;
        std::unique_ptr<BridgeServer> bridge;

        std::atomic<bool> created{false};
        std::atomic<bool> browserReady{false};
        std::atomic<bool> browserFailed{false};
        std::atomic<bool> closed{false};
        std::atomic<bool> closing{false};
        std::atomic<bool> finalized{false};
        std::atomic<bool> visible{false};
        std::atomic<bool> focused{false};
        std::atomic<bool> minimized{false};
        std::atomic<bool> maximized{false};
        std::atomic<bool> fullscreen{false};

        bool pendingShow = false;
        bool autoShowAllowed = true;
        bool firstShowDone = false;
        bool titleOverridden = false;
        bool stateSaved = false;
        std::optional<Error> creationError;

        std::string currentUrl;
        std::string documentTitle;
        std::string userAgent;
        double zoomFactor = 1.0;
        Rect lastBounds;
        Rect normalBounds;
        bool hasLastBounds = false;
        bool hasNormalBounds = false;

        mutable std::mutex customEventsMutex;
        std::unordered_map<std::string, std::shared_ptr<Event<const JSON&>>> customEvents;

        mutable std::mutex handlersMutex;
        std::unordered_map<std::string, HandlerRegistration> handlers;

        mutable std::mutex groupNamesMutex;
        std::vector<std::string> groupNames;

        Event<Window&> createdEvent;
        Event<Window&> browserReadyEvent;
        Event<const BrowserCreationFailedEvent&> browserCreationFailedEvent;
        Event<Window&> showEvent;
        Event<Window&> hideEvent;
        Event<CloseEvent&> closeEvent;
        Event<Window&> closedEvent;
        Event<const BoundsEvent&> moveEvent;
        Event<const ResizeEvent&> resizeEvent;
        Event<Window&> minimizeEvent;
        Event<Window&> maximizeEvent;
        Event<Window&> restoreEvent;
        Event<Window&> focusEvent;
        Event<Window&> blurEvent;
        Event<Window&> fullscreenEnterEvent;
        Event<Window&> fullscreenLeaveEvent;
        Event<const DpiChangedEvent&> dpiChangedEvent;
        Event<const ThemeChangedEvent&> themeChangedEvent;
        Event<const FileDropEvent&> fileDropEvent;
        Event<const KeyEvent&> keyDownEvent;
        Event<const KeyEvent&> keyUpEvent;
        Event<const MouseEvent&> mouseDownEvent;
        Event<const MouseEvent&> mouseUpEvent;
        Event<const MouseEvent&> mouseMoveEvent;
        Event<const WheelEvent&> wheelEvent;
        Event<const MouseEvent&> doubleClickEvent;
        Event<NavigationStartingEvent&> navigationStartingEvent;
        Event<const NavigationCompletedEvent&> navigationCompletedEvent;
        Event<const SourceChangedEvent&> sourceChangedEvent;
        Event<const DocumentTitleEvent&> documentTitleEvent;
        Event<const FaviconEvent&> faviconEvent;
        Event<const ContentLoadingEvent&> contentLoadingEvent;
        Event<Window&> domContentLoadedEvent;
        Event<NewWindowRequestedEvent&> newWindowRequestedEvent;
        Event<PermissionRequestEvent&> permissionRequestedEvent;
        Event<DownloadStartingEvent&> downloadStartingEvent;
        Event<const DownloadProgressEvent&> downloadProgressEvent;
        Event<const DownloadCompletedEvent&> downloadCompletedEvent;
        Event<ScriptDialogEvent&> scriptDialogEvent;
        Event<ContextMenuEvent&> contextMenuEvent;
        Event<const ProcessFailedEvent&> processFailedEvent;
        Event<WebResourceRequestEvent&> resourceRequestedEvent;
        Event<const WebResourceResponseEvent&> resourceResponseEvent;
        Event<const WebMessageEvent&> webMessageEvent;
        Event<FullscreenRequestEvent&> fullscreenRequestedEvent;
        Event<const ZoomChangedEvent&> zoomChangedEvent;
        Event<AuthenticationRequestEvent&> authenticationRequestedEvent;
        Event<CertificateErrorEvent&> certificateErrorEvent;

        Impl(std::shared_ptr<AppRuntime> appRuntime, App* owningApp, WindowId windowId, WindowOptions windowOptions,
            BrowserOptions browserOpts, BridgeOptions bridgeOpts)
            : runtime(std::move(appRuntime)),
              app(owningApp),
              id(windowId),
              options(std::move(windowOptions)),
              browserOptions(std::move(browserOpts)),
              bridgeOptions(std::move(bridgeOpts)),
              capabilities(runtime->capabilities),
              documentTitle(options.title),
              userAgent(browserOptions.userAgent),
              zoomFactor(browserOptions.zoomFactor) {
            normalBounds = Rect{static_cast<double>(options.x), static_cast<double>(options.y),
                static_cast<double>(options.width), static_cast<double>(options.height)};
            hasNormalBounds = true;
        }

        template <typename EventType, typename... Args>
        void Raise(EventType& event, Args&&... args) {
            try {
                event.Emit(std::forward<Args>(args)...);
            } catch (const std::exception& exception) {
                runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A window event handler threw an exception", exception.what()));
            } catch (...) {
                runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A window event handler threw an unknown exception"));
            }
        }

        template <typename T>
        std::function<void(Result<T>)> GuardCallback(std::function<void(Result<T>)> callback) {
            std::shared_ptr<AppRuntime> appRuntime = runtime;
            return [appRuntime, callback = std::move(callback)](Result<T> result) mutable {
                if (!callback) {
                    return;
                }
                try {
                    callback(std::move(result));
                } catch (const std::exception& exception) {
                    appRuntime->ReportError(Error(
                        ErrorCode::HANDLER_FAILED, "An asynchronous callback threw an exception", exception.what()));
                } catch (...) {
                    appRuntime->ReportError(
                        Error(ErrorCode::HANDLER_FAILED, "An asynchronous callback threw an unknown exception"));
                }
            };
        }

        Result<void> EnsureUsable() const {
            if (closed.load() || runtime->shuttingDown.load()) {
                return Error(ErrorCode::WINDOW_CLOSED, "The window is closed");
            }
            if (!runtime->IsOnUiThread()) {
                return Error(ErrorCode::WRONG_THREAD, "Window operations must run on the UI thread");
            }
            if (!backend) {
                return Error(ErrorCode::WINDOW_CLOSED, "The native window is not available");
            }
            return Result<void>::Ok();
        }

        Result<void> EnsureBrowser() const {
            Result<void> usable = EnsureUsable();
            if (!usable.IsOk()) {
                return usable;
            }
            if (browserFailed.load()) {
                return Error(ErrorCode::BROWSER_NOT_READY, "The browser failed to initialize");
            }
            if (!browserReady.load()) {
                return Error(ErrorCode::BROWSER_NOT_READY, "The browser is not ready");
            }
            return Result<void>::Ok();
        }

        JSON RegionsJson() const {
            JSON regions = JSON::array();
            for (const Rect& region : options.draggableRegions) {
                regions.push_back(
                    {{"x", region.x}, {"y", region.y}, {"width", region.width}, {"height", region.height}});
            }
            return regions;
        }

        JSON BuildClientConfig() const { return JSON{{"draggableRegions", RegionsJson()}}; }

        std::string BuildBootstrapScript() const {
            JSON config;
            config["protocol"] = BridgeProtocolVersion;
            config["allowIframes"] = bridgeOptions.allowIframes && capabilities.iframeBridge;
            config["appName"] = runtime->options.applicationName;
            config["appVersion"] = runtime->options.applicationVersion;
            config["flareVersion"] = std::string(LibraryVersion);
            config["platform"] = capabilities.platformName.empty() ? "unknown" : capabilities.platformName;
            config["draggableRegions"] = RegionsJson();
            config["maxMessageBytes"] = bridgeOptions.maxMessageBytes;
            config["maxPayloadBytes"] = bridgeOptions.maxPayloadBytes;
            config["invokeTimeoutMs"] = bridgeOptions.invocationTimeoutMs;
            config["handshakeTimeoutMs"] = 15000;
            return "window.__flareBootstrap = " + SafeJsonDump(config) + ";\n";
        }

        Result<void> Initialize() {
            backend = CreatePlatformWindowBackend();
            if (!backend) {
                return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the platform window backend");
            }
            capabilities = backend->Capabilities();

            if (options.contentProtection && !capabilities.contentProtection) {
                return Error(ErrorCode::NOT_SUPPORTED, "Content protection is not supported on this platform");
            }
            if (options.transparent && !capabilities.transparentWindows) {
                return Error(ErrorCode::NOT_SUPPORTED, "Transparent windows are not supported on this platform");
            }
            if (options.opacity < 1.0 && !capabilities.windowOpacity) {
                return Error(ErrorCode::NOT_SUPPORTED, "Window opacity is not supported on this platform");
            }
            if (!options.draggableRegions.empty() && !capabilities.draggableRegions) {
                return Error(ErrorCode::NOT_SUPPORTED, "Draggable window regions are not supported on this platform");
            }
            if (!options.showInTaskbar && !capabilities.taskbarVisibility) {
                return Error(
                    ErrorCode::NOT_SUPPORTED, "Per-window taskbar visibility is not supported on this platform");
            }
            if (!options.icon.IsEmpty() && !capabilities.perWindowIcon) {
                return Error(ErrorCode::NOT_SUPPORTED, "Per-window icons are not supported on this platform");
            }
            if ((options.frameless || options.titleBarStyle != TitleBarStyle::NATIVE) && !capabilities.customTitleBar) {
                return Error(ErrorCode::NOT_SUPPORTED, "Custom window chrome is not supported on this platform");
            }
            if (browserOptions.incognito && !capabilities.incognito) {
                return Error(ErrorCode::NOT_SUPPORTED, "Incognito browsing is not supported on this platform");
            }
            if (browserOptions.proxy.enabled && !capabilities.proxy) {
                return Error(ErrorCode::NOT_SUPPORTED, "Proxy configuration is not supported on this platform");
            }
            if (!browserOptions.browserArguments.empty() && !capabilities.browserArguments) {
                return Error(
                    ErrorCode::NOT_SUPPORTED, "Browser command-line arguments are not supported on this platform");
            }
            if (!browserOptions.customSchemes.empty() && !capabilities.customSchemes) {
                return Error(ErrorCode::NOT_SUPPORTED, "Custom URL schemes are not supported on this platform");
            }
            if (browserOptions.interceptRequests && !capabilities.requestInterception) {
                return Error(ErrorCode::NOT_SUPPORTED, "Request interception is not supported on this platform");
            }
            if (browserOptions.interceptResponses && !capabilities.responseInterception) {
                return Error(ErrorCode::NOT_SUPPORTED, "Response interception is not supported on this platform");
            }
            if (!browserOptions.extraHeaders.empty() && !capabilities.headerInjection) {
                return Error(ErrorCode::NOT_SUPPORTED, "HTTP header injection is not supported on this platform");
            }
            if (browserOptions.devToolsEnabled && !capabilities.devTools) {
                return Error(ErrorCode::NOT_SUPPORTED, "Developer tools are not supported on this platform");
            }
            if (!browserOptions.webSecurityEnabled && !capabilities.webSecurityToggle) {
                return Error(ErrorCode::NOT_SUPPORTED, "Disabling web security is not supported on this platform");
            }
            if (bridgeOptions.enabled && bridgeOptions.allowIframes && !capabilities.iframeBridge) {
                return Error(ErrorCode::NOT_SUPPORTED, "Iframe bridging is not supported on this platform");
            }

            if (bridgeOptions.enabled && bridgeOptions.hardenedMode) {
                browserOptions.devToolsEnabled = false;
                browserOptions.contextMenuEnabled = false;
                browserOptions.browserAcceleratorKeysEnabled = false;
                browserOptions.autofillEnabled = false;
                browserOptions.passwordSaveEnabled = false;
                browserOptions.statusBarEnabled = false;
            }

            if (bridgeOptions.enabled) {
                BridgeHost host;
                host.appName = runtime->options.applicationName;
                host.appVersion = runtime->options.applicationVersion;
                host.flareVersion = std::string(LibraryVersion);
                host.platform = capabilities.platformName.empty() ? "unknown" : capabilities.platformName;
                host.postToMain = [this](const std::string& message) {
                    if (backend) {
                        backend->PostWebMessage(message);
                    }
                };
                host.postToFrame = [this](const std::string& message, const std::string& frameId) {
                    if (backend) {
                        backend->PostWebMessageToFrame(message, frameId);
                    }
                };
                host.invokeHandler = [this](const std::string& name, const JSON& payload, const std::string& origin,
                                         bool isMainFrame) {
                    return InvokeHandler(name, payload, origin, isMainFrame);
                };
                host.performWindowAction = [this](WindowAction action, const JSON& payload) {
                    return PerformWindowAction(action, payload);
                };
                host.deliverEvent = [this](const std::string& name, const JSON& payload, bool isMainFrame) {
                    DeliverCustomEvent(name, payload, isMainFrame);
                };
                host.clientConfig = [this]() { return BuildClientConfig(); };
                host.reportError = [this](const Error& error) { runtime->ReportError(error); };
                bridge = std::make_unique<BridgeServer>(bridgeOptions, std::move(host));
            }

            std::vector<std::string> startScripts;
            if (bridge) {
                startScripts.push_back(BuildBootstrapScript());
                startScripts.push_back(std::string(bridgeScriptSource));
            }
            if (browserOptions.hideScrollbars) {
                startScripts.push_back(BuildScrollbarScript());
            }
            if (!browserOptions.contextMenuEnabled && !capabilities.contextMenuControl) {
                startScripts.push_back(BuildContextMenuScript());
            }
            for (const std::string& script : browserOptions.documentStartScripts) {
                startScripts.push_back(script);
            }
            browserOptions.documentStartScripts = std::move(startScripts);

            void* parentHandle = nullptr;
            if (options.parent && !options.parent->IsClosed()) {
                Window* parent = options.parent;
                if (parent->impl && parent->impl->backend) {
                    parentHandle = parent->impl->backend->GetNativeHandle();
                }
            }
            Result<void> createdResult = backend->Create(options, browserOptions, *this, parentHandle);
            if (!createdResult.IsOk()) {
                bridge.reset();
                backend.reset();
                return createdResult.GetError();
            }

            Result<Rect> bounds = backend->GetBounds();
            if (bounds.IsOk()) {
                lastBounds = bounds.Value();
                hasLastBounds = true;
                if (!options.maximizeOnStart && !options.fullscreen) {
                    normalBounds = lastBounds;
                    hasNormalBounds = true;
                }
            }
            return Result<void>::Ok();
        }

        Result<void> PerformShow() {
            Result<void> usable = EnsureUsable();
            if (!usable.IsOk()) {
                return usable;
            }
            if (browserFailed.load()) {
                return Error(ErrorCode::BROWSER_NOT_READY, "The browser failed to initialize");
            }
            if (!browserReady.load()) {
                pendingShow = true;
                return Result<void>::Ok();
            }
            if (!firstShowDone) {
                if (options.maximizeOnStart) {
                    Result<void> maximizedResult = backend->Maximize();
                    if (!maximizedResult.IsOk()) {
                        runtime->ReportError(maximizedResult.GetError());
                    }
                }
                if (options.fullscreen) {
                    Result<void> fullscreenResult = backend->SetFullscreen(true);
                    if (!fullscreenResult.IsOk()) {
                        runtime->ReportError(fullscreenResult.GetError());
                    }
                }
                Result<void> shown = backend->Show();
                if (!shown.IsOk()) {
                    return shown;
                }
                firstShowDone = true;
                pendingShow = false;
                if (options.focusOnStart) {
                    Result<void> focusResult = backend->Focus();
                    if (!focusResult.IsOk()) {
                        runtime->ReportError(focusResult.GetError());
                    }
                }
                return Result<void>::Ok();
            }
            pendingShow = false;
            return backend->Show();
        }

        void EvaluateTrigger(ShowTrigger trigger) {
            if (options.showTrigger != trigger || closed.load() || browserFailed.load() || !browserReady.load()
                || firstShowDone) {
                return;
            }
            if (!options.visibleOnStart || !autoShowAllowed) {
                return;
            }
            Result<void> shown = PerformShow();
            if (!shown.IsOk()) {
                runtime->ReportError(shown.GetError());
            }
        }

        Result<void> StartInitialNavigation() {
            if (!browserOptions.localFile.empty()) {
                return backend->LoadFile(browserOptions.localFile);
            }
            if (!browserOptions.html.empty()) {
                std::string baseUrl = browserOptions.htmlBaseUrl;
                if (baseUrl.empty() && browserOptions.virtualHost.has_value()) {
                    const std::string scheme = capabilities.virtualHostHttps ? "https"
                                                                             : browserOptions.virtualHost->scheme;
                    baseUrl = scheme + "://" + browserOptions.virtualHost->host + "/";
                }
                return backend->NavigateToString(browserOptions.html, baseUrl);
            }
            if (!browserOptions.startUrl.empty()) {
                std::string target = browserOptions.startUrl;
                if (browserOptions.virtualHost.has_value() && !capabilities.virtualHostHttps) {
                    const std::string httpsPrefix = "https://" + browserOptions.virtualHost->host;
                    if (target.rfind(httpsPrefix, 0) == 0) {
                        target = browserOptions.virtualHost->scheme + "://" + browserOptions.virtualHost->host
                            + target.substr(httpsPrefix.size());
                    }
                }
                return backend->Navigate(target);
            }
            if (browserOptions.virtualHost.has_value()) {
                const std::string scheme = capabilities.virtualHostHttps ? "https" : browserOptions.virtualHost->scheme;
                return backend->Navigate(scheme + "://" + browserOptions.virtualHost->host + "/index.html");
            }
            return backend->Navigate("about:blank");
        }

        bool NavigationPolicyAllows(std::string_view url) const {
            for (const std::string& pattern : browserOptions.blockedUrlPatterns) {
                if (MatchesUrlPattern(url, pattern)) {
                    return false;
                }
            }
            if (!browserOptions.allowedUrlPatterns.empty()) {
                for (const std::string& pattern : browserOptions.allowedUrlPatterns) {
                    if (MatchesUrlPattern(url, pattern)) {
                        return true;
                    }
                }
                return false;
            }
            return true;
        }

        PermissionState PermissionPolicyFor(PermissionKind kind) const {
            switch (kind) {
                case PermissionKind::CAMERA:
                    return browserOptions.permissionPolicy.camera;
                case PermissionKind::MICROPHONE:
                    return browserOptions.permissionPolicy.microphone;
                case PermissionKind::GEOLOCATION:
                    return browserOptions.permissionPolicy.geolocation;
                case PermissionKind::NOTIFICATIONS:
                    return browserOptions.permissionPolicy.notifications;
                case PermissionKind::CLIPBOARD_READ:
                    return browserOptions.permissionPolicy.clipboardRead;
                case PermissionKind::CLIPBOARD_WRITE:
                    return browserOptions.permissionPolicy.clipboardWrite;
                case PermissionKind::AUTOPLAY:
                case PermissionKind::OTHER:
                    break;
            }
            return PermissionState::ASK;
        }

        Result<JSON> InvokeHandler(
            const std::string& name, const JSON& payload, const std::string& origin, bool isMainFrame) {
            (void)isMainFrame;
            HandlerRegistration registration;
            bool found = false;
            {
                std::lock_guard<std::mutex> guard(handlersMutex);
                const auto iterator = handlers.find(name);
                if (iterator != handlers.end()) {
                    registration = iterator->second;
                    found = true;
                }
            }
            if (!found) {
                std::optional<HandlerRegistration> global = runtime->FindHandler(name);
                if (!global.has_value()) {
                    return Error(ErrorCode::NOT_FOUND, "No bridge handler is registered with this name");
                }
                registration = std::move(global.value());
                found = true;
            }
            if (!found) {
                return Error(ErrorCode::NOT_FOUND, "No bridge handler is registered with this name");
            }
            if (!registration.options.allowedOrigins.empty()) {
                bool matched = false;
                for (const std::string& pattern : registration.options.allowedOrigins) {
                    if (OriginMatchesPattern(origin, pattern)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    return Error(ErrorCode::PERMISSION_DENIED, "The bridge handler does not allow this origin");
                }
            }
            const bool restrictPermissions = !bridgeOptions.grantedPermissions.empty() || bridgeOptions.hardenedMode;
            if (restrictPermissions && !registration.options.permission.empty()
                && bridgeOptions.grantedPermissions.find(registration.options.permission)
                    == bridgeOptions.grantedPermissions.end()) {
                return Error(
                    ErrorCode::PERMISSION_DENIED, "The bridge permission required by this handler was not granted");
            }
            try {
                return registration.handler(payload);
            } catch (const std::exception& exception) {
                runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A bridge handler threw an exception", exception.what()));
                return Error(ErrorCode::HANDLER_FAILED, "The bridge handler threw an exception");
            } catch (...) {
                runtime->ReportError(Error(ErrorCode::HANDLER_FAILED, "A bridge handler threw an unknown exception"));
                return Error(ErrorCode::HANDLER_FAILED, "The bridge handler threw an unknown exception");
            }
        }

        Result<JSON> PerformWindowAction(WindowAction action, const JSON& payload) {
            const JSON arguments = payload.is_null() ? JSON::object() : payload;
            if (!arguments.is_object()) {
                return Error(ErrorCode::INVALID_ARGUMENT, "Window action payload must be a JSON object");
            }
            const auto number = [&arguments](const char* key, double minimum, double maximum, double& output) {
                const auto iterator = arguments.find(key);
                if (iterator == arguments.end() || !iterator->is_number()) {
                    return false;
                }
                const double value = iterator->get<double>();
                if (!std::isfinite(value) || value < minimum || value > maximum) {
                    return false;
                }
                output = value;
                return true;
            };
            const auto boolean = [&arguments](const char* key, bool& output) {
                const auto iterator = arguments.find(key);
                if (iterator == arguments.end() || !iterator->is_boolean()) {
                    return false;
                }
                output = iterator->get<bool>();
                return true;
            };
            const auto text = [&arguments](const char* key, std::size_t maximum, std::string& output) {
                const auto iterator = arguments.find(key);
                if (iterator == arguments.end() || !iterator->is_string()) {
                    return false;
                }
                output = iterator->get<std::string>();
                return output.size() <= maximum;
            };

            const auto toJsonResult = [](Result<void> result) -> Result<JSON> {
                if (!result.IsOk()) {
                    return result.GetError();
                }
                return JSON();
            };

            switch (action) {
                case WindowAction::SHOW:
                    return toJsonResult(owner->Show());
                case WindowAction::HIDE:
                    return toJsonResult(owner->Hide());
                case WindowAction::MINIMIZE:
                    return toJsonResult(owner->Minimize());
                case WindowAction::MAXIMIZE:
                    return toJsonResult(owner->Maximize());
                case WindowAction::RESTORE:
                    return toJsonResult(owner->Restore());
                case WindowAction::CLOSE:
                    return toJsonResult(owner->Close());
                case WindowAction::FOCUS:
                    return toJsonResult(owner->Focus());
                case WindowAction::CENTER:
                    return toJsonResult(owner->Center());
                case WindowAction::START_DRAG:
                    return toJsonResult(owner->StartDrag());
                case WindowAction::SET_TITLE: {
                    std::string title;
                    if (!text("title", 4096, title)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setTitle requires a string title");
                    }
                    return toJsonResult(owner->SetTitle(std::move(title)));
                }
                case WindowAction::SET_SIZE: {
                    double width = 0.0;
                    double height = 0.0;
                    if (!number("width", 1.0, 32767.0, width) || !number("height", 1.0, 32767.0, height)
                        || !IsIntegerInRange(width, 1.0, 32767.0) || !IsIntegerInRange(height, 1.0, 32767.0)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setSize requires integer width and height");
                    }
                    return toJsonResult(owner->SetSize(static_cast<int>(width), static_cast<int>(height)));
                }
                case WindowAction::SET_POSITION: {
                    double x = 0.0;
                    double y = 0.0;
                    if (!number("x", -1000000.0, 1000000.0, x) || !number("y", -1000000.0, 1000000.0, y)
                        || !IsIntegerInRange(x, -1000000.0, 1000000.0) || !IsIntegerInRange(y, -1000000.0, 1000000.0)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setPosition requires integer x and y coordinates");
                    }
                    return toJsonResult(owner->SetPosition(static_cast<int>(x), static_cast<int>(y)));
                }
                case WindowAction::SET_OPACITY: {
                    double opacity = 0.0;
                    if (!number("opacity", 0.0, 1.0, opacity)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setOpacity requires a number between 0 and 1");
                    }
                    return toJsonResult(owner->SetOpacity(opacity));
                }
                case WindowAction::SET_FULLSCREEN: {
                    bool value = false;
                    if (!boolean("fullscreen", value)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setFullscreen requires a boolean");
                    }
                    return toJsonResult(owner->SetFullscreen(value));
                }
                case WindowAction::SET_ALWAYS_ON_TOP: {
                    bool value = false;
                    if (!boolean("alwaysOnTop", value)) {
                        return Error(ErrorCode::INVALID_ARGUMENT, "setAlwaysOnTop requires a boolean");
                    }
                    return toJsonResult(owner->SetAlwaysOnTop(value));
                }
                case WindowAction::GET_BOUNDS: {
                    Result<Rect> bounds = owner->GetBounds();
                    if (!bounds.IsOk()) {
                        return bounds.GetError();
                    }
                    const Rect& rect = bounds.Value();
                    return JSON{{"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}};
                }
                case WindowAction::IS_MAXIMIZED:
                    return JSON(owner->IsWindowMaximized());
                case WindowAction::IS_MINIMIZED:
                    return JSON(owner->IsWindowMinimized());
                case WindowAction::IS_VISIBLE:
                    return JSON(owner->IsVisible());
                case WindowAction::IS_FOCUSED:
                    return JSON(owner->IsFocused());
            }
            return Error(ErrorCode::INVALID_ARGUMENT, "Unknown window action");
        }

        void DeliverCustomEvent(const std::string& name, const JSON& payload, bool isMainFrame) {
            (void)isMainFrame;
            std::shared_ptr<Event<const JSON&>> event;
            {
                std::lock_guard<std::mutex> guard(customEventsMutex);
                const auto iterator = customEvents.find(name);
                if (iterator == customEvents.end()) {
                    return;
                }
                event = iterator->second;
            }
            Raise(*event, payload);
        }

        void SaveState() {
            if (stateSaved || !options.persistState || options.stateKey.empty()) {
                return;
            }
            stateSaved = true;
            const Rect& bounds = ((maximized.load() || fullscreen.load()) && hasNormalBounds) ? normalBounds
                                                                                              : lastBounds;
            if (!hasLastBounds && !hasNormalBounds) {
                return;
            }
            if (bounds.width < 1.0 || bounds.height < 1.0) {
                return;
            }
            SavedWindowState state;
            state.valid = true;
            state.x = static_cast<int>(std::lround(bounds.x));
            state.y = static_cast<int>(std::lround(bounds.y));
            state.width = static_cast<int>(std::lround(bounds.width));
            state.height = static_cast<int>(std::lround(bounds.height));
            state.maximized = maximized.load() && !fullscreen.load();
            Result<void> saved = SaveWindowState(runtime->stateFolder, options.stateKey, state);
            if (!saved.IsOk()) {
                runtime->ReportError(saved.GetError());
            }
        }

        void OnBackendCreated() override {
            if (closed.load()) {
                return;
            }
            created.store(true);
            Raise(createdEvent, *owner);
        }

        void OnBackendBrowserReady() override {
            if (closed.load()) {
                return;
            }
            browserReady.store(true);
            Result<void> navigation = StartInitialNavigation();
            if (!navigation.IsOk()) {
                runtime->ReportError(navigation.GetError());
            }
            Raise(browserReadyEvent, *owner);
            if (pendingShow) {
                Result<void> shown = PerformShow();
                if (!shown.IsOk()) {
                    runtime->ReportError(shown.GetError());
                }
                return;
            }
            EvaluateTrigger(ShowTrigger::BROWSER_CREATED);
        }

        void OnBackendBrowserCreationFailed(const Error& error) override {
            if (closed.load()) {
                return;
            }
            browserFailed.store(true);
            browserReady.store(false);
            pendingShow = false;
            creationError = error;
            if (bridge) {
                bridge->Reset();
            }
            BrowserCreationFailedEvent event{*owner, error};
            Raise(browserCreationFailedEvent, event);
        }

        void OnBackendVisibilityChanged(bool isVisible) override {
            if (closed.load()) {
                return;
            }
            const bool changed = visible.exchange(isVisible) != isVisible;
            if (!changed) {
                return;
            }
            if (isVisible) {
                Raise(showEvent, *owner);
            } else {
                Raise(hideEvent, *owner);
            }
        }

        bool OnBackendCloseRequested() override {
            if (closed.load() || closing.load()) {
                return true;
            }
            CloseEvent event{*owner};
            Raise(closeEvent, event);
            return !event.cancel;
        }

        void OnBackendClosed() override {
            if (closed.exchange(true)) {
                return;
            }
            browserReady.store(false);
            closing.store(true);
            visible.store(false);
            focused.store(false);
            if (bridge) {
                bridge->Reset();
            }
            std::shared_ptr<AppRuntime> appRuntime = runtime;
            const WindowId windowId = id;
            appRuntime->Enqueue([appRuntime, windowId]() { appRuntime->FinalizeWindow(windowId); });
        }

        void OnBackendBoundsChanged(const Rect& bounds, bool isWindowMaximized) override {
            if (closed.load()) {
                return;
            }
            const bool positionChanged = hasLastBounds
                && (std::abs(bounds.x - lastBounds.x) > 0.5 || std::abs(bounds.y - lastBounds.y) > 0.5);
            const bool sizeChanged = hasLastBounds
                && (std::abs(bounds.width - lastBounds.width) > 0.5
                    || std::abs(bounds.height - lastBounds.height) > 0.5);
            lastBounds = bounds;
            hasLastBounds = true;
            if (!maximized.load() && !fullscreen.load() && bounds.width > 0.0 && bounds.height > 0.0) {
                normalBounds = bounds;
                hasNormalBounds = true;
            }
            if (positionChanged) {
                Raise(moveEvent, BoundsEvent{*owner, bounds});
            }
            if (sizeChanged) {
                Raise(resizeEvent, ResizeEvent{*owner, bounds, isWindowMaximized});
            }
        }

        void OnBackendMinimized() override {
            if (closed.load()) {
                return;
            }
            minimized.store(true);
            Raise(minimizeEvent, *owner);
        }

        void OnBackendMaximized() override {
            if (closed.load()) {
                return;
            }
            maximized.store(true);
            minimized.store(false);
            Raise(maximizeEvent, *owner);
        }

        void OnBackendRestored() override {
            if (closed.load()) {
                return;
            }
            minimized.store(false);
            maximized.store(false);
            Raise(restoreEvent, *owner);
        }

        void OnBackendFocusChanged(bool isFocused) override {
            if (closed.load()) {
                return;
            }
            const bool changed = focused.exchange(isFocused) != isFocused;
            if (!changed) {
                return;
            }
            if (isFocused) {
                Raise(focusEvent, *owner);
            } else {
                Raise(blurEvent, *owner);
            }
        }

        void OnBackendFullscreenChanged(bool isFullscreen) override {
            if (closed.load()) {
                return;
            }
            const bool changed = fullscreen.exchange(isFullscreen) != isFullscreen;
            if (!changed) {
                return;
            }
            if (isFullscreen) {
                Raise(fullscreenEnterEvent, *owner);
            } else {
                Raise(fullscreenLeaveEvent, *owner);
            }
        }

        void OnBackendDpiChanged(double scaleFactor, const Rect& bounds) override {
            if (closed.load()) {
                return;
            }
            Raise(dpiChangedEvent, DpiChangedEvent{*owner, scaleFactor, bounds});
        }

        void OnBackendThemeChanged(Theme theme, Theme systemTheme) override {
            if (closed.load()) {
                return;
            }
            Raise(themeChangedEvent, ThemeChangedEvent{*owner, theme, systemTheme});
        }

        void OnBackendFileDrop(std::vector<std::filesystem::path> files, Point position) override {
            if (closed.load()) {
                return;
            }
            Raise(fileDropEvent, FileDropEvent{*owner, std::move(files), position});
        }

        bool OnBackendKey(bool down, const std::string& key, const std::string& code, int virtualKey,
            Modifiers modifiers, bool repeat) override {
            if (closed.load()) {
                return false;
            }
            KeyEvent event{*owner, key, code, virtualKey, modifiers, repeat, false};
            Raise(down ? keyDownEvent : keyUpEvent, event);
            return event.cancel;
        }

        bool OnBackendMouse(MouseEventType type, double x, double y, MouseButton button, MouseButtons buttons,
            Modifiers modifiers, int clickCount) override {
            if (closed.load()) {
                return false;
            }
            MouseEvent event{*owner, x, y, button, buttons, modifiers, clickCount, false};
            switch (type) {
                case MouseEventType::DOWN:
                    Raise(mouseDownEvent, event);
                    break;
                case MouseEventType::UP:
                    Raise(mouseUpEvent, event);
                    break;
                case MouseEventType::MOVE:
                    Raise(mouseMoveEvent, event);
                    break;
                case MouseEventType::DOUBLE_CLICK_EVENT:
                    Raise(doubleClickEvent, event);
                    break;
            }
            return event.cancel;
        }

        bool OnBackendWheel(double x, double y, double deltaX, double deltaY, Modifiers modifiers) override {
            if (closed.load()) {
                return false;
            }
            WheelEvent event{*owner, x, y, deltaX, deltaY, modifiers, false};
            Raise(wheelEvent, event);
            return event.cancel;
        }

        NavigationDecision OnBackendNavigationStarting(const std::string& url, const std::string& method,
            bool isRedirect, bool isTopLevel, bool isSameDocument, bool isUserInitiated) override {
            (void)method;
            if (closed.load()) {
                return NavigationDecision{false, {}};
            }
            NavigationStartingEvent event{
                *owner, url, isRedirect, isTopLevel, isSameDocument, isUserInitiated, false, {}};
            const bool policyAllows = !isTopLevel || NavigationPolicyAllows(url);
            event.cancel = !policyAllows;
            Raise(navigationStartingEvent, event);
            if (!policyAllows || event.cancel) {
                return NavigationDecision{false, {}};
            }
            if (!event.redirectUrl.empty()) {
                if (!IsSafeNavigationTarget(event.redirectUrl)) {
                    return NavigationDecision{false, {}};
                }
                return NavigationDecision{false, event.redirectUrl};
            }
            if (isTopLevel && !isSameDocument) {
                currentUrl = url;
                if (bridge) {
                    bridge->StartDocument(url);
                }
            }
            return NavigationDecision{true, {}};
        }

        void OnBackendNavigationCompleted(
            const std::string& url, bool success, int statusCode, const Error& error) override {
            if (closed.load()) {
                return;
            }
            if (!url.empty()) {
                currentUrl = url;
                if (bridge && success) {
                    bridge->CommitDocument(url);
                }
            }
            Raise(navigationCompletedEvent, NavigationCompletedEvent{*owner, url, success, statusCode, error});
            EvaluateTrigger(ShowTrigger::FIRST_NAVIGATION_COMPLETED);
        }

        void OnBackendSourceChanged(const std::string& url, bool isNewDocument, bool isMainFrame) override {
            if (closed.load()) {
                return;
            }
            if (isMainFrame) {
                currentUrl = url;
                if (bridge && isNewDocument) {
                    bridge->CommitDocument(url);
                }
            }
            Raise(sourceChangedEvent, SourceChangedEvent{*owner, url, isNewDocument});
        }

        void OnBackendContentLoading(bool isErrorPage, bool isMainFrame) override {
            if (closed.load() || !isMainFrame) {
                return;
            }
            Raise(contentLoadingEvent, ContentLoadingEvent{*owner, isErrorPage});
        }

        void OnBackendDomContentLoaded(bool isMainFrame) override {
            if (closed.load() || !isMainFrame) {
                return;
            }
            Raise(domContentLoadedEvent, *owner);
            EvaluateTrigger(ShowTrigger::DOM_CONTENT_LOADED);
        }

        void OnBackendDocumentTitleChanged(const std::string& title) override {
            if (closed.load()) {
                return;
            }
            documentTitle = title;
            Raise(documentTitleEvent, DocumentTitleEvent{*owner, title});
            if (options.useDocumentTitle && !titleOverridden) {
                options.title = title;
                if (backend) {
                    Result<void> applied = backend->SetTitle(title);
                    if (!applied.IsOk()) {
                        runtime->ReportError(applied.GetError());
                    }
                }
            }
        }

        void OnBackendFaviconChanged(const std::string& url) override {
            if (closed.load()) {
                return;
            }
            Raise(faviconEvent, FaviconEvent{*owner, url});
        }

        void OnBackendNewWindowRequested(const std::string& uri, bool isUserInitiated) override {
            if (closed.load() || !NavigationPolicyAllows(uri)) {
                return;
            }
            NewWindowRequestedEvent event{*owner, uri, isUserInitiated, false};
            Raise(newWindowRequestedEvent, event);
        }

        PermissionDecision OnBackendPermissionRequested(
            PermissionKind kind, const std::string& origin, const std::string& url) override {
            if (closed.load()) {
                return PermissionDecision{false, false};
            }
            const PermissionState policy = PermissionPolicyFor(kind);
            if (policy == PermissionState::ALLOW) {
                return PermissionDecision{true, true};
            }
            if (policy == PermissionState::DENY) {
                return PermissionDecision{false, false};
            }
            if (!permissionRequestedEvent.HasSubscribers()) {
                return PermissionDecision{false, false};
            }
            PermissionRequestEvent event{*owner, kind, origin, url, false, false};
            Raise(permissionRequestedEvent, event);
            return PermissionDecision{event.allow, event.remember};
        }

        DownloadStartDecision OnBackendDownloadStarting(const DownloadInfo& info) override {
            if (closed.load()) {
                return DownloadStartDecision{true, {}};
            }
            DownloadStartingEvent event{*owner, info, {}, browserOptions.download.blocked};
            if (!browserOptions.download.directory.empty()) {
                event.savePath = browserOptions.download.directory / info.suggestedFilename;
            }
            Raise(downloadStartingEvent, event);
            if (browserOptions.download.blocked) {
                event.cancel = true;
            }
            return DownloadStartDecision{event.cancel, event.savePath};
        }

        void OnBackendDownloadProgress(const DownloadInfo& info) override {
            if (closed.load()) {
                return;
            }
            Raise(downloadProgressEvent, DownloadProgressEvent{*owner, info});
        }

        void OnBackendDownloadCompleted(const DownloadInfo& info, const Error& error) override {
            if (closed.load()) {
                return;
            }
            Raise(downloadCompletedEvent, DownloadCompletedEvent{*owner, info, error});
        }

        ScriptDialogDecision OnBackendScriptDialog(
            ScriptDialogType type, const std::string& message, const std::string& defaultText) override {
            if (closed.load() || !scriptDialogEvent.HasSubscribers()) {
                return ScriptDialogDecision{false, true, {}};
            }
            ScriptDialogEvent event{*owner, type, message, defaultText, {}, true, true};
            Raise(scriptDialogEvent, event);
            return ScriptDialogDecision{
                true, event.accepted, event.resultText.empty() ? event.defaultText : event.resultText};
        }

        bool OnBackendContextMenu(const ContextMenuDetails& details) override {
            if (closed.load() || !contextMenuEvent.HasSubscribers()) {
                return false;
            }
            ContextMenuEvent event{*owner, details.x, details.y, details.linkUrl, details.sourceUrl,
                details.selectionText, details.isEditable, details.isImage, details.isVideo, false};
            Raise(contextMenuEvent, event);
            return event.cancel;
        }

        void OnBackendProcessFailed(const std::string& reason, int exitCode, bool crashed) override {
            if (closed.load()) {
                return;
            }
            Raise(processFailedEvent, ProcessFailedEvent{*owner, reason, exitCode, crashed});
        }

        ResourceRequestDecision OnBackendResourceRequested(const std::string& url, const std::string& method,
            ResourceType type, bool isMainFrame, const HttpHeaderMap& headers) override {
            if (closed.load()) {
                return ResourceRequestDecision{false, {}};
            }
            const bool policyAllows = !isMainFrame || NavigationPolicyAllows(url);
            WebResourceRequestEvent event{*owner, url, method, type, isMainFrame, headers, !policyAllows, {}};
            if (resourceRequestedEvent.HasSubscribers()) {
                Raise(resourceRequestedEvent, event);
            }
            if (!policyAllows) {
                event.cancel = true;
            }
            if (event.cancel) {
                return ResourceRequestDecision{true, {}};
            }
            if (!event.redirectUrl.empty() && !IsSafeNavigationTarget(event.redirectUrl)) {
                return ResourceRequestDecision{true, {}};
            }
            return ResourceRequestDecision{false, event.redirectUrl};
        }

        void OnBackendResourceResponse(const std::string& url, int statusCode, const std::string& mimeType,
            const HttpHeaderMap& headers) override {
            if (closed.load()) {
                return;
            }
            if (!browserOptions.responseFilters.empty()) {
                bool matched = false;
                for (const std::string& pattern : browserOptions.responseFilters) {
                    if (MatchesUrlPattern(url, pattern)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    return;
                }
            }
            Raise(resourceResponseEvent, WebResourceResponseEvent{*owner, url, statusCode, mimeType, headers});
        }

        void OnBackendWebMessage(const std::string& messageJson, const std::string& sourceUrl, bool isMainFrame,
            const std::string& frameId) override {
            if (closed.load()) {
                return;
            }
            if (bridge) {
                BridgeMessageContext context{sourceUrl, isMainFrame, frameId};
                if (bridge->HandleMessage(messageJson, context)) {
                    return;
                }
            }
            Result<JSON> parsed = ParseJsonValue(messageJson, 4 * 1024 * 1024, 32, 100000);
            const JSON message = parsed.IsOk() ? parsed.Value() : JSON(messageJson);
            Raise(webMessageEvent, WebMessageEvent{*owner, message, sourceUrl, isMainFrame});
        }

        bool OnBackendFullscreenRequested(bool enter) override {
            if (closed.load()) {
                return true;
            }
            if (!fullscreenRequestedEvent.HasSubscribers()) {
                return true;
            }
            FullscreenRequestEvent event{*owner, enter, true};
            Raise(fullscreenRequestedEvent, event);
            return event.allow;
        }

        void OnBackendZoomChanged(double factor) override {
            if (closed.load()) {
                return;
            }
            zoomFactor = factor;
            Raise(zoomChangedEvent, ZoomChangedEvent{*owner, factor});
        }

        AuthenticationDecision OnBackendAuthenticationRequested(const AuthenticationInfo& info) override {
            if (closed.load() || !authenticationRequestedEvent.HasSubscribers()) {
                return AuthenticationDecision{true, false, {}, {}};
            }
            AuthenticationRequestEvent event{
                *owner, info.url, info.host, info.port, info.realm, info.scheme, info.isProxy, true, false, {}, {}};
            Raise(authenticationRequestedEvent, event);
            return AuthenticationDecision{event.useDefault, event.cancel, event.username, event.password};
        }

        bool OnBackendCertificateError(const std::string& url, const std::string& errorText) override {
            if (closed.load()) {
                return false;
            }
            if (!certificateErrorEvent.HasSubscribers()) {
                return false;
            }
            CertificateErrorEvent event{*owner, url, errorText, false};
            Raise(certificateErrorEvent, event);
            return event.allow;
        }
    };

    Window::Window(std::unique_ptr<Impl> windowImpl) : impl(std::move(windowImpl)) {}

    Window::~Window() {
        ShutdownInternal();
    }

    Result<std::shared_ptr<Window>> Window::CreateInternal(std::shared_ptr<AppRuntime> runtime, App& app, WindowId id,
        const WindowOptions& windowOptions, const BrowserOptions& browserOptions, const BridgeOptions& bridgeOptions) {
        auto windowImpl = std::make_unique<Impl>(
            std::move(runtime), &app, id, windowOptions, browserOptions, bridgeOptions);
        auto window = std::shared_ptr<Window>(new Window(std::move(windowImpl)));
        window->impl->owner = window.get();
        Result<void> initialized = window->impl->Initialize();
        if (!initialized.IsOk()) {
            return initialized.GetError();
        }
        return window;
    }

    void Window::FinalizeClosed() {
        if (impl->finalized.exchange(true)) {
            return;
        }
        impl->closed.store(true);
        impl->closing.store(true);
        impl->browserReady.store(false);
        impl->Raise(impl->closedEvent, *this);
        impl->SaveState();
        if (impl->bridge) {
            impl->bridge->Reset();
            impl->bridge.reset();
        }
        impl->backend.reset();
        {
            std::lock_guard<std::mutex> guard(impl->customEventsMutex);
            impl->customEvents.clear();
        }
        {
            std::lock_guard<std::mutex> guard(impl->handlersMutex);
            impl->handlers.clear();
        }
    }

    void Window::ShutdownInternal() {
        if (!impl || impl->finalized.exchange(true)) {
            return;
        }
        impl->closed.store(true);
        impl->closing.store(true);
        impl->browserReady.store(false);
        impl->SaveState();
        if (impl->bridge) {
            impl->bridge->Reset();
            impl->bridge.reset();
        }
        if (impl->backend) {
            impl->backend->Close();
            impl->backend.reset();
        }
    }

    WindowId Window::GetId() const noexcept {
        return impl->id;
    }

    App& Window::GetApp() const {
        return *impl->app;
    }

    const WindowOptions& Window::Options() const noexcept {
        return impl->options;
    }

    const BrowserOptions& Window::BrowserSettings() const noexcept {
        return impl->browserOptions;
    }

    const BridgeOptions& Window::BridgeSettings() const noexcept {
        return impl->bridgeOptions;
    }

    const PlatformCapabilities& Window::Capabilities() const noexcept {
        return impl->capabilities;
    }

    Result<void> Window::Show() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (impl->browserFailed.load()) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The browser failed to initialize");
        }
        impl->pendingShow = true;
        impl->autoShowAllowed = true;
        if (!impl->browserReady.load()) {
            return Result<void>::Ok();
        }
        return impl->PerformShow();
    }

    Result<void> Window::Hide() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        impl->pendingShow = false;
        impl->autoShowAllowed = false;
        if (impl->browserReady.load() && impl->visible.load()) {
            return impl->backend->Hide();
        }
        return Result<void>::Ok();
    }

    Result<void> Window::Minimize() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Minimize();
    }

    Result<void> Window::Maximize() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Maximize();
    }

    Result<void> Window::Restore() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Restore();
    }

    Result<void> Window::Close() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (impl->closing.load()) {
            return Result<void>::Ok();
        }
        CloseEvent event{*this};
        impl->Raise(impl->closeEvent, event);
        if (event.cancel) {
            return Error(ErrorCode::CANCELLED, "Window close was cancelled by a handler");
        }
        impl->closing.store(true);
        impl->autoShowAllowed = false;
        impl->pendingShow = false;
        impl->backend->Close();
        return Result<void>::Ok();
    }

    Result<void> Window::Focus() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Focus();
    }

    Result<void> Window::StartDrag() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->StartDrag();
    }

    Result<void> Window::SetTitle(std::string title) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (title.size() > 4096) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window title is too long");
        }
        Result<void> applied = impl->backend->SetTitle(title);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.title = title;
        impl->titleOverridden = true;
        return Result<void>::Ok();
    }

    Result<std::string> Window::GetTitle() const {
        return impl->options.title;
    }

    Result<void> Window::SetSize(int width, int height) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (width <= 0 || height <= 0 || width > 32767 || height > 32767) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window size is out of range");
        }
        Result<void> applied = impl->backend->SetSize(width, height);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.width = width;
        impl->options.height = height;
        return Result<void>::Ok();
    }

    Result<void> Window::SetPosition(int x, int y) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetPosition(x, y);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.x = x;
        impl->options.y = y;
        impl->options.hasPosition = true;
        return Result<void>::Ok();
    }

    Result<void> Window::SetMinSize(int width, int height) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (width < 0 || height < 0 || width > 32767 || height > 32767) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Minimum window size is out of range");
        }
        if (impl->options.maxWidth > 0 && width > impl->options.maxWidth) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Minimum width exceeds maximum width");
        }
        if (impl->options.maxHeight > 0 && height > impl->options.maxHeight) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Minimum height exceeds maximum height");
        }
        Result<void> applied = impl->backend->SetMinSize(width, height);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.minWidth = width;
        impl->options.minHeight = height;
        return Result<void>::Ok();
    }

    Result<void> Window::SetMaxSize(int width, int height) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (width < 0 || height < 0 || width > 32767 || height > 32767) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Maximum window size is out of range");
        }
        if (impl->options.minWidth > 0 && width > 0 && width < impl->options.minWidth) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Maximum width is below minimum width");
        }
        if (impl->options.minHeight > 0 && height > 0 && height < impl->options.minHeight) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Maximum height is below minimum height");
        }
        Result<void> applied = impl->backend->SetMaxSize(width, height);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.maxWidth = width;
        impl->options.maxHeight = height;
        return Result<void>::Ok();
    }

    Result<void> Window::SetFullscreen(bool value) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetFullscreen(value);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.fullscreen = value;
        return Result<void>::Ok();
    }

    Result<void> Window::SetAlwaysOnTop(bool alwaysOnTop) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetAlwaysOnTop(alwaysOnTop);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.alwaysOnTop = alwaysOnTop;
        return Result<void>::Ok();
    }

    Result<void> Window::SetOpacity(double opacity) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (opacity < 0.0 || opacity > 1.0) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window opacity must be between 0 and 1");
        }
        if (!impl->capabilities.windowOpacity && opacity < 1.0) {
            return Error(ErrorCode::NOT_SUPPORTED, "Window opacity is not supported on this platform");
        }
        Result<void> applied = impl->backend->SetOpacity(opacity);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.opacity = opacity;
        return Result<void>::Ok();
    }

    Result<double> Window::GetOpacity() const {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable.GetError();
        }
        return impl->backend->GetOpacity();
    }

    Result<void> Window::Center() {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Center();
    }

    Result<void> Window::SetBackgroundColor(Color color) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetBackgroundColor(color);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.backgroundColor = color;
        return Result<void>::Ok();
    }

    Result<void> Window::SetTheme(Theme theme) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetTheme(theme);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.theme = theme;
        return Result<void>::Ok();
    }

    Result<void> Window::SetContentProtection(bool enabled) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.contentProtection && enabled) {
            return Error(ErrorCode::NOT_SUPPORTED, "Content protection is not supported on this platform");
        }
        Result<void> applied = impl->backend->SetContentProtection(enabled);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.contentProtection = enabled;
        return Result<void>::Ok();
    }

    Result<void> Window::SetResizable(bool resizable) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetResizable(resizable);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.resizable = resizable;
        return Result<void>::Ok();
    }

    Result<void> Window::SetMovable(bool movable) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetMovable(movable);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.movable = movable;
        return Result<void>::Ok();
    }

    Result<void> Window::SetMinimizable(bool minimizable) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetMinimizable(minimizable);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.minimizable = minimizable;
        return Result<void>::Ok();
    }

    Result<void> Window::SetMaximizable(bool maximizable) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetMaximizable(maximizable);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.maximizable = maximizable;
        return Result<void>::Ok();
    }

    Result<void> Window::SetClosable(bool closable) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<void> applied = impl->backend->SetClosable(closable);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.closable = closable;
        return Result<void>::Ok();
    }

    Result<void> Window::SetShowInTaskbar(bool show) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!show && !impl->capabilities.taskbarVisibility) {
            return Error(ErrorCode::NOT_SUPPORTED, "Per-window taskbar visibility is not supported on this platform");
        }
        Result<void> applied = impl->backend->SetShowInTaskbar(show);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.showInTaskbar = show;
        return Result<void>::Ok();
    }

    Result<void> Window::SetIcon(Icon iconValue) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        Result<Icon> resolved = ResolveIcon(iconValue, impl->runtime->options.assetsFolder);
        if (!resolved.IsOk()) {
            return resolved.GetError();
        }
        if (!resolved.Value().IsEmpty() && !impl->capabilities.perWindowIcon) {
            return Error(ErrorCode::NOT_SUPPORTED, "Per-window icons are not supported on this platform");
        }
        Result<void> applied = impl->backend->SetIcon(resolved.Value());
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.icon = std::move(resolved.Value());
        return Result<void>::Ok();
    }

    Result<void> Window::SetDraggableRegions(std::vector<Rect> regions) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.draggableRegions) {
            return Error(ErrorCode::NOT_SUPPORTED, "Draggable window regions are not supported on this platform");
        }
        if (regions.size() > 64) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Too many draggable regions");
        }
        for (const Rect& region : regions) {
            if (region.width <= 0.0 || region.height <= 0.0 || !std::isfinite(region.x) || !std::isfinite(region.y)
                || !std::isfinite(region.width) || !std::isfinite(region.height)) {
                return Error(ErrorCode::INVALID_ARGUMENT, "Draggable region is invalid");
            }
        }
        Result<void> applied = impl->backend->SetDraggableRegions(regions);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->options.draggableRegions = regions;
        if (impl->bridge) {
            impl->bridge->UpdateClientConfig(impl->BuildClientConfig());
        }
        return Result<void>::Ok();
    }

    Result<Rect> Window::GetBounds() const {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable.GetError();
        }
        return impl->backend->GetBounds();
    }

    bool Window::IsVisible() const {
        return impl->visible.load();
    }

    bool Window::IsWindowMaximized() const {
        return impl->maximized.load();
    }

    bool Window::IsWindowMinimized() const {
        return impl->minimized.load();
    }

    bool Window::IsFocused() const {
        return impl->focused.load();
    }

    bool Window::IsFullscreen() const {
        return impl->fullscreen.load();
    }

    bool Window::IsBrowserReady() const {
        return impl->browserReady.load();
    }

    bool Window::IsClosed() const {
        return impl->closed.load();
    }

    Result<double> Window::GetDpiScale() const {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable.GetError();
        }
        return impl->backend->GetDpiScale();
    }

    Result<WindowState> Window::GetState() const {
        if (impl->closed.load()) {
            return Error(ErrorCode::WINDOW_CLOSED, "The window is closed");
        }
        if (impl->fullscreen.load()) {
            return WindowState::FULLSCREEN;
        }
        if (impl->minimized.load()) {
            return WindowState::MINIMIZED;
        }
        if (impl->maximized.load()) {
            return WindowState::MAXIMIZED;
        }
        return WindowState::NORMAL;
    }

    Result<void> Window::Navigate(std::string url) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!IsSafeNavigationTarget(url)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Navigation URL is invalid");
        }
        return impl->backend->Navigate(url);
    }

    Result<void> Window::NavigateToString(std::string html, std::string baseUrl) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (html.size() > 16 * 1024 * 1024) {
            return Error(ErrorCode::INVALID_ARGUMENT, "HTML content exceeds the size limit");
        }
        if (!baseUrl.empty() && !IsSafeNavigationTarget(baseUrl)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "HTML base URL is invalid");
        }
        return impl->backend->NavigateToString(html, baseUrl);
    }

    Result<void> Window::LoadFile(std::filesystem::path file) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        std::error_code error;
        if (!std::filesystem::exists(file, error) || error || !std::filesystem::is_regular_file(file, error)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Local file does not exist");
        }
        return impl->backend->LoadFile(file);
    }

    Result<void> Window::Reload() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Reload();
    }

    Result<void> Window::GoBack() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->GoBack();
    }

    Result<void> Window::GoForward() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->GoForward();
    }

    Result<void> Window::Stop() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->Stop();
    }

    Result<void> Window::ExecuteScript(std::string script, ResultCallback<JSON> callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (script.empty() || script.size() > 10 * 1024 * 1024) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Script is empty or exceeds the size limit");
        }
        return impl->backend->ExecuteScript(script, impl->GuardCallback<JSON>(std::move(callback)));
    }

    Result<void> Window::OpenDevTools() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.devTools) {
            return Error(ErrorCode::NOT_SUPPORTED, "Developer tools are not supported on this platform");
        }
        if (!impl->browserOptions.devToolsEnabled) {
            return Error(ErrorCode::PERMISSION_DENIED, "Developer tools are disabled for this window");
        }
        return impl->backend->OpenDevTools();
    }

    Result<void> Window::CloseDevTools() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->CloseDevTools();
    }

    Result<void> Window::PrintToPdf(
        const PdfOptions& options, std::filesystem::path outputPath, CompletionCallback callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.printToPdf) {
            return Error(ErrorCode::NOT_SUPPORTED, "PDF export is not supported on this platform");
        }
        if (outputPath.empty()) {
            return Error(ErrorCode::INVALID_ARGUMENT, "PDF output path is empty");
        }
        if (!std::isfinite(options.scale) || options.scale <= 0.0 || options.pageWidthInches <= 0.0
            || options.pageHeightInches <= 0.0 || options.marginTopInches < 0.0 || options.marginBottomInches < 0.0
            || options.marginLeftInches < 0.0 || options.marginRightInches < 0.0) {
            return Error(ErrorCode::INVALID_ARGUMENT, "PDF options are invalid");
        }
        return impl->backend->PrintToPdf(options, outputPath, impl->GuardCallback<void>(std::move(callback)));
    }

    Result<void> Window::Print() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.print) {
            return Error(ErrorCode::NOT_SUPPORTED, "Printing is not supported on this platform");
        }
        return impl->backend->Print();
    }

    Result<void> Window::CapturePreview(
        const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.capturePreview) {
            return Error(ErrorCode::NOT_SUPPORTED, "Page capture is not supported on this platform");
        }
        if (!callback) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Capture callback is empty");
        }
        if (options.quality < 1 || options.quality > 100) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Capture quality must be between 1 and 100");
        }
        return impl->backend->CapturePreview(
            options, impl->GuardCallback<std::vector<std::uint8_t>>(std::move(callback)));
    }

    Result<void> Window::GetCookies(std::string url, std::function<void(Result<std::vector<Cookie>>)> callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.cookies) {
            return Error(ErrorCode::NOT_SUPPORTED, "Cookie access is not supported on this platform");
        }
        if (!callback) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Cookie callback is empty");
        }
        if (url.empty()) {
            url = impl->currentUrl;
        }
        if (!IsSafeNavigationTarget(url)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Cookie URL is invalid");
        }
        return impl->backend->GetCookies(url, impl->GuardCallback<std::vector<Cookie>>(std::move(callback)));
    }

    Result<void> Window::SetCookie(Cookie cookie, CompletionCallback callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.cookies) {
            return Error(ErrorCode::NOT_SUPPORTED, "Cookie access is not supported on this platform");
        }
        if (cookie.name.empty() || cookie.name.size() > 256 || cookie.domain.size() > 256
            || cookie.value.size() > 8192) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Cookie is invalid");
        }
        if (cookie.name.find('\0') != std::string::npos || cookie.value.find('\0') != std::string::npos) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Cookie contains invalid characters");
        }
        return impl->backend->SetCookie(cookie, impl->GuardCallback<void>(std::move(callback)));
    }

    Result<void> Window::DeleteCookie(std::string url, std::string name, CompletionCallback callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.cookies) {
            return Error(ErrorCode::NOT_SUPPORTED, "Cookie access is not supported on this platform");
        }
        if (name.empty() || !IsSafeNavigationTarget(url)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Cookie deletion arguments are invalid");
        }
        return impl->backend->DeleteCookie(url, name, impl->GuardCallback<void>(std::move(callback)));
    }

    Result<void> Window::ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.clearBrowsingData) {
            return Error(ErrorCode::NOT_SUPPORTED, "Clearing browsing data is not supported on this platform");
        }
        if (kinds == ClearDataKind::NONE) {
            return Result<void>::Ok();
        }
        return impl->backend->ClearBrowsingData(kinds, impl->GuardCallback<void>(std::move(callback)));
    }

    Result<void> Window::SetZoom(double factor) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!std::isfinite(factor) || factor < 0.25 || factor > 5.0) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Zoom factor must be between 0.25 and 5.0");
        }
        return impl->backend->SetZoom(factor);
    }

    Result<double> Window::GetZoom() const {
        if (!impl->browserReady.load()) {
            return impl->zoomFactor;
        }
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable.GetError();
        }
        return impl->backend->GetZoom();
    }

    Result<void> Window::FindInPage(
        std::string text, FindOptions options, std::function<void(Result<FindResult>)> callback) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.findInPage) {
            return Error(ErrorCode::NOT_SUPPORTED, "Find-in-page is not supported on this platform");
        }
        if (text.empty() || text.size() > 4096 || options.maxMatches < 0) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Find-in-page arguments are invalid");
        }
        return impl->backend->FindInPage(text, options, impl->GuardCallback<FindResult>(std::move(callback)));
    }

    Result<void> Window::StopFindInPage() {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        return impl->backend->StopFindInPage();
    }

    Result<std::string> Window::GetUrl() const {
        if (impl->browserReady.load()) {
            Result<void> usable = impl->EnsureUsable();
            if (!usable.IsOk()) {
                return usable.GetError();
            }
            Result<std::string> url = impl->backend->GetUrl();
            if (url.IsOk() && !url.Value().empty()) {
                return url;
            }
        }
        return impl->currentUrl;
    }

    Result<std::string> Window::GetBrowserTitle() const {
        return impl->documentTitle;
    }

    Result<void> Window::SendWebMessage(const JSON& message) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!impl->capabilities.webMessages) {
            return Error(ErrorCode::NOT_SUPPORTED, "Web messages are not supported on this platform");
        }
        const std::string serialized = SafeJsonDump(message);
        if (serialized.size() > impl->bridgeOptions.maxMessageBytes) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Web message exceeds the size limit");
        }
        return impl->backend->SendWebMessage(message);
    }

    Result<void> Window::SetUserAgent(std::string userAgent) {
        Result<void> usable = impl->EnsureBrowser();
        if (!usable.IsOk()) {
            return usable;
        }
        if (userAgent.size() > 2048 || userAgent.find('\0') != std::string::npos) {
            return Error(ErrorCode::INVALID_ARGUMENT, "User agent string is invalid");
        }
        Result<void> applied = impl->backend->SetUserAgent(userAgent);
        if (!applied.IsOk()) {
            return applied;
        }
        impl->userAgent = std::move(userAgent);
        return Result<void>::Ok();
    }

    Result<std::string> Window::GetUserAgent() const {
        if (!impl->browserReady.load()) {
            return impl->userAgent;
        }
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable.GetError();
        }
        return impl->backend->GetUserAgent();
    }

    Result<void> Window::Emit(std::string_view eventName, const JSON& payload) {
        if (impl->closed.load()) {
            return Error(ErrorCode::WINDOW_CLOSED, "The window is closed");
        }
        if (!impl->bridge) {
            return Error(ErrorCode::NOT_SUPPORTED, "The JavaScript bridge is disabled for this window");
        }
        if (impl->browserFailed.load()) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The browser failed to initialize");
        }
        if (!impl->runtime->IsOnUiThread()) {
            std::shared_ptr<AppRuntime> runtime = impl->runtime;
            const WindowId windowId = impl->id;
            const std::string name(eventName);
            JSON copy = payload;
            runtime->Enqueue([runtime, windowId, name, copy = std::move(copy)]() mutable {
                Result<std::shared_ptr<Window>> window = runtime->FindWindow(windowId);
                if (window.IsOk()) {
                    window.Value()->Emit(name, copy);
                }
            });
            return Result<void>::Ok();
        }
        return impl->bridge->Emit(eventName, payload);
    }

    Subscription Window::On(std::string_view eventName, std::function<void(const JSON&)> handler) {
        if (!IsValidName(eventName) || !handler) {
            return Subscription();
        }
        std::shared_ptr<Event<const JSON&>> event;
        {
            std::lock_guard<std::mutex> guard(impl->customEventsMutex);
            auto& entry = impl->customEvents[std::string(eventName)];
            if (!entry) {
                entry = std::make_shared<Event<const JSON&>>();
            }
            event = entry;
        }
        return event->Subscribe(std::move(handler));
    }

    bool Window::Off(std::string_view eventName) {
        if (!IsValidName(eventName)) {
            return false;
        }
        std::lock_guard<std::mutex> guard(impl->customEventsMutex);
        return impl->customEvents.erase(std::string(eventName)) > 0;
    }

    Result<void> Window::Handle(std::string name, RpcHandler handler, HandlerOptions options) {
        if (!IsValidName(name)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Handler name is invalid");
        }
        if (!handler) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Handler function is empty");
        }
        std::lock_guard<std::mutex> guard(impl->handlersMutex);
        if (impl->handlers.find(name) != impl->handlers.end()) {
            return Error(ErrorCode::ALREADY_EXISTS, "A handler with this name is already registered");
        }
        const std::string key = name;
        impl->handlers.emplace(key, HandlerRegistration{std::move(name), std::move(handler), std::move(options)});
        return Result<void>::Ok();
    }

    bool Window::Unhandle(std::string_view name) {
        std::lock_guard<std::mutex> guard(impl->handlersMutex);
        return impl->handlers.erase(std::string(name)) > 0;
    }

    Result<void> Window::JoinGroup(std::string_view groupName) {
        Result<void> usable = impl->EnsureUsable();
        if (!usable.IsOk()) {
            return usable;
        }
        if (!IsValidName(groupName)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window group name is invalid");
        }
        Result<void> joined = impl->runtime->JoinGroup(*this, groupName);
        if (!joined.IsOk()) {
            return joined;
        }
        std::lock_guard<std::mutex> guard(impl->groupNamesMutex);
        const std::string name(groupName);
        if (std::find(impl->groupNames.begin(), impl->groupNames.end(), name) == impl->groupNames.end()) {
            impl->groupNames.push_back(name);
        }
        return Result<void>::Ok();
    }

    void Window::LeaveGroup(std::string_view groupName) {
        impl->runtime->LeaveGroup(*this, groupName);
        std::lock_guard<std::mutex> guard(impl->groupNamesMutex);
        const auto iterator = std::find(impl->groupNames.begin(), impl->groupNames.end(), groupName);
        if (iterator != impl->groupNames.end()) {
            impl->groupNames.erase(iterator);
        }
    }

    std::vector<std::string> Window::Groups() const {
        std::lock_guard<std::mutex> guard(impl->groupNamesMutex);
        return impl->groupNames;
    }

    Subscription Window::OnCreated(std::function<void(Window&)> handler) {
        auto callback = handler;
        Subscription subscription = impl->createdEvent.Subscribe(std::move(handler));
        if (subscription.IsActive() && impl->created.load() && impl->runtime->IsOnUiThread()) {
            try {
                if (callback) {
                    callback(*this);
                }
            } catch (const std::exception& exception) {
                impl->runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A created-event handler threw an exception", exception.what()));
            } catch (...) {
                impl->runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A created-event handler threw an unknown exception"));
            }
        }
        return subscription;
    }

    Subscription Window::OnBrowserReady(std::function<void(Window&)> handler) {
        auto callback = handler;
        Subscription subscription = impl->browserReadyEvent.Subscribe(std::move(handler));
        if (subscription.IsActive() && impl->browserReady.load() && impl->runtime->IsOnUiThread()) {
            try {
                if (callback) {
                    callback(*this);
                }
            } catch (const std::exception& exception) {
                impl->runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A browser-ready handler threw an exception", exception.what()));
            } catch (...) {
                impl->runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A browser-ready handler threw an unknown exception"));
            }
        }
        return subscription;
    }

    Subscription Window::OnBrowserCreationFailed(std::function<void(const BrowserCreationFailedEvent&)> handler) {
        auto callback = handler;
        Subscription subscription = impl->browserCreationFailedEvent.Subscribe(std::move(handler));
        if (subscription.IsActive() && impl->creationError.has_value() && impl->runtime->IsOnUiThread()) {
            try {
                if (callback) {
                    BrowserCreationFailedEvent event{*this, impl->creationError.value()};
                    callback(event);
                }
            } catch (const std::exception& exception) {
                impl->runtime->ReportError(Error(ErrorCode::HANDLER_FAILED,
                    "A browser-creation-failed handler threw an exception", exception.what()));
            } catch (...) {
                impl->runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A browser-creation-failed handler threw an unknown exception"));
            }
        }
        return subscription;
    }

    Subscription Window::OnShow(std::function<void(Window&)> handler) {
        return impl->showEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnHide(std::function<void(Window&)> handler) {
        return impl->hideEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnClose(std::function<void(CloseEvent&)> handler) {
        return impl->closeEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnClosed(std::function<void(Window&)> handler) {
        return impl->closedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMove(std::function<void(const BoundsEvent&)> handler) {
        return impl->moveEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnResize(std::function<void(const ResizeEvent&)> handler) {
        return impl->resizeEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMinimize(std::function<void(Window&)> handler) {
        return impl->minimizeEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMaximize(std::function<void(Window&)> handler) {
        return impl->maximizeEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnRestore(std::function<void(Window&)> handler) {
        return impl->restoreEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFocus(std::function<void(Window&)> handler) {
        return impl->focusEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnBlur(std::function<void(Window&)> handler) {
        return impl->blurEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFullscreenEnter(std::function<void(Window&)> handler) {
        return impl->fullscreenEnterEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFullscreenLeave(std::function<void(Window&)> handler) {
        return impl->fullscreenLeaveEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDpiChanged(std::function<void(const DpiChangedEvent&)> handler) {
        return impl->dpiChangedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnThemeChanged(std::function<void(const ThemeChangedEvent&)> handler) {
        return impl->themeChangedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFileDrop(std::function<void(const FileDropEvent&)> handler) {
        return impl->fileDropEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnKeyDown(std::function<void(const KeyEvent&)> handler) {
        return impl->keyDownEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnKeyUp(std::function<void(const KeyEvent&)> handler) {
        return impl->keyUpEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMouseDown(std::function<void(const MouseEvent&)> handler) {
        return impl->mouseDownEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMouseUp(std::function<void(const MouseEvent&)> handler) {
        return impl->mouseUpEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMouseMove(std::function<void(const MouseEvent&)> handler) {
        return impl->mouseMoveEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnMouseWheel(std::function<void(const WheelEvent&)> handler) {
        return impl->wheelEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDoubleClick(std::function<void(const MouseEvent&)> handler) {
        return impl->doubleClickEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnNavigationStarting(std::function<void(NavigationStartingEvent&)> handler) {
        return impl->navigationStartingEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnNavigationCompleted(std::function<void(const NavigationCompletedEvent&)> handler) {
        return impl->navigationCompletedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnSourceChanged(std::function<void(const SourceChangedEvent&)> handler) {
        return impl->sourceChangedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDocumentTitleChanged(std::function<void(const DocumentTitleEvent&)> handler) {
        return impl->documentTitleEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFaviconChanged(std::function<void(const FaviconEvent&)> handler) {
        return impl->faviconEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnContentLoading(std::function<void(const ContentLoadingEvent&)> handler) {
        return impl->contentLoadingEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDomContentLoaded(std::function<void(Window&)> handler) {
        return impl->domContentLoadedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnNewWindowRequested(std::function<void(NewWindowRequestedEvent&)> handler) {
        return impl->newWindowRequestedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnPermissionRequested(std::function<void(PermissionRequestEvent&)> handler) {
        return impl->permissionRequestedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDownloadStarting(std::function<void(DownloadStartingEvent&)> handler) {
        return impl->downloadStartingEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDownloadProgress(std::function<void(const DownloadProgressEvent&)> handler) {
        return impl->downloadProgressEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnDownloadCompleted(std::function<void(const DownloadCompletedEvent&)> handler) {
        return impl->downloadCompletedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnScriptDialog(std::function<void(ScriptDialogEvent&)> handler) {
        return impl->scriptDialogEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnContextMenuRequested(std::function<void(ContextMenuEvent&)> handler) {
        return impl->contextMenuEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnProcessFailed(std::function<void(const ProcessFailedEvent&)> handler) {
        return impl->processFailedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnWebResourceRequested(std::function<void(WebResourceRequestEvent&)> handler) {
        return impl->resourceRequestedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnWebResourceResponseReceived(std::function<void(const WebResourceResponseEvent&)> handler) {
        return impl->resourceResponseEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnWebMessageReceived(std::function<void(const WebMessageEvent&)> handler) {
        return impl->webMessageEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnFullscreenRequested(std::function<void(FullscreenRequestEvent&)> handler) {
        return impl->fullscreenRequestedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnZoomChanged(std::function<void(const ZoomChangedEvent&)> handler) {
        return impl->zoomChangedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnAuthenticationRequested(std::function<void(AuthenticationRequestEvent&)> handler) {
        return impl->authenticationRequestedEvent.Subscribe(std::move(handler));
    }

    Subscription Window::OnCertificateError(std::function<void(CertificateErrorEvent&)> handler) {
        return impl->certificateErrorEvent.Subscribe(std::move(handler));
    }
}
