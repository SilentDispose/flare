#include "platform/linux/linux-window.h"

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/json-utils.h"
#include "core/path-utils.h"
#include "core/text-utils.h"
#include "core/url-utils.h"
#include "platform/linux/linux-backend.h"

namespace Flare {
    namespace {
        std::mutex registryMutex;
        std::unordered_map<WebKitWebView*, LinuxWindowBackend*> webViewRegistry;
        std::unordered_map<std::string, WebKitWebContext*> contextCache;
        std::unordered_set<std::string> registeredSchemes;
        std::atomic<unsigned> temporaryCounter{0};

        struct IdlePayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
        };

        struct ResourcePayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            std::string url;
        };

        std::string PointerKey(const void* pointer) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%p", pointer);
            return buffer;
        }

        std::string MimeTypeForPath(const std::filesystem::path& path) {
            const std::string extension = ToLowerAscii(PathToUtf8(path.extension()));
            if (extension == ".html" || extension == ".htm") {
                return "text/html";
            }
            if (extension == ".css") {
                return "text/css";
            }
            if (extension == ".js" || extension == ".mjs") {
                return "text/javascript";
            }
            if (extension == ".json") {
                return "application/json";
            }
            if (extension == ".svg") {
                return "image/svg+xml";
            }
            if (extension == ".png") {
                return "image/png";
            }
            if (extension == ".jpg" || extension == ".jpeg") {
                return "image/jpeg";
            }
            if (extension == ".gif") {
                return "image/gif";
            }
            if (extension == ".webp") {
                return "image/webp";
            }
            if (extension == ".woff") {
                return "font/woff";
            }
            if (extension == ".woff2") {
                return "font/woff2";
            }
            if (extension == ".ttf") {
                return "font/ttf";
            }
            if (extension == ".wasm") {
                return "application/wasm";
            }
            if (extension == ".txt") {
                return "text/plain";
            }
            return "application/octet-stream";
        }

        std::string StatusReason(int status) {
            switch (status) {
                case 200:
                    return "OK";
                case 204:
                    return "No Content";
                case 302:
                    return "Found";
                case 403:
                    return "Forbidden";
                case 404:
                    return "Not Found";
                case 500:
                    return "Internal Server Error";
                default:
                    return "Response";
            }
        }

        std::string LifecycleScript() {
            return "(function(){document.addEventListener('DOMContentLoaded',function(){if(window.top!==window){return;"
                   "}try{window.webkit.messageHandlers.flareLifecycle.postMessage({type:'dom'});}catch(error){}"
                   "function reportIcon(){try{var "
                   "link=document.querySelector('link[rel~=\"icon\"]');window.webkit.messageHandlers.flareLifecycle."
                   "postMessage({type:'favicon',url:link?link.href:''});}catch(error){}}reportIcon();try{var "
                   "observer=new "
                   "MutationObserver(reportIcon);observer.observe(document.head||document.documentElement,{childList:"
                   "true,subtree:true,attributes:true,attributeFilter:['href','rel']});}catch(error){}});})();";
        }

        std::filesystem::path PromptDownloadPath(
            GtkWindow* parent, const std::string& suggested, const std::filesystem::path& initialDirectory) {
            GtkWidget* dialog = gtk_file_chooser_dialog_new("Save File", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel",
                GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested.c_str());
            if (!initialDirectory.empty()) {
                gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), PathToUtf8(initialDirectory).c_str());
            }
            std::filesystem::path selected;
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
                gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (filename) {
                    selected = PathFromUtf8(filename);
                    g_free(filename);
                }
            }
            gtk_widget_destroy(dialog);
            return selected;
        }

        void AddUserScript(WebKitUserContentManager* manager, const std::string& source,
            WebKitUserContentInjectedFrames frames, WebKitUserScriptInjectionTime time) {
            if (!manager || source.empty()) {
                return;
            }
            WebKitUserScript* script = webkit_user_script_new(source.c_str(), frames, time, nullptr, nullptr);
            webkit_user_content_manager_add_script(manager, script);
            webkit_user_script_unref(script);
        }

        gboolean RunCreateWebView(gpointer data) {
            auto* payload = static_cast<IdlePayload*>(data);
            if (const std::shared_ptr<std::atomic<bool>> alive = payload->alive.lock(); alive && alive->load()) {
                payload->self->CreateWebView();
            }
            delete payload;
            return G_SOURCE_REMOVE;
        }

        void RegisterScheme(
            WebKitWebContext* context, const std::string& scheme, WebKitURISchemeRequestCallback callback) {
            if (!context || scheme.empty()) {
                return;
            }
            const std::string key = PointerKey(context) + "|" + ToLowerAscii(scheme);
            if (registeredSchemes.find(key) != registeredSchemes.end()) {
                return;
            }
            webkit_web_context_register_uri_scheme(context, ToLowerAscii(scheme).c_str(), callback, nullptr, nullptr);
            registeredSchemes.insert(key);
        }

        WebKitWebContext* AcquireContext(const BrowserOptions& browserOptions) {
            if (browserOptions.incognito) {
                WebKitWebsiteDataManager* manager = webkit_website_data_manager_new_ephemeral();
                WebKitWebContext* context = webkit_web_context_new_with_website_data_manager(manager);
                g_object_unref(manager);
                if (browserOptions.proxy.enabled && !browserOptions.proxy.server.empty()) {
                    std::vector<const gchar*> ignored;
                    for (const std::string& entry : browserOptions.proxy.bypassList) {
                        ignored.push_back(entry.c_str());
                    }
                    ignored.push_back(nullptr);
                    WebKitNetworkProxySettings* proxy = webkit_network_proxy_settings_new(
                        browserOptions.proxy.server.c_str(), ignored.data());
                    webkit_website_data_manager_set_network_proxy_settings(
                        webkit_web_context_get_website_data_manager(context), WEBKIT_NETWORK_PROXY_MODE_CUSTOM, proxy);
                    webkit_network_proxy_settings_free(proxy);
                }
                if (!browserOptions.language.empty()) {
                    const gchar* languages[] = {browserOptions.language.c_str(), nullptr};
                    webkit_web_context_set_preferred_languages(context, languages);
                }
                return context;
            }

            const std::string folder = PathToUtf8(browserOptions.userDataFolder);
            const auto cached = contextCache.find(folder);
            if (cached != contextCache.end()) {
                return static_cast<WebKitWebContext*>(g_object_ref(cached->second));
            }

            const std::filesystem::path cacheDirectory = browserOptions.userDataFolder / "cache";
            WebKitWebsiteDataManager* manager = webkit_website_data_manager_new("base-data-directory", folder.c_str(),
                "base-cache-directory", PathToUtf8(cacheDirectory).c_str(), nullptr);
            WebKitWebContext* context = webkit_web_context_new_with_website_data_manager(manager);
            g_object_unref(manager);
            if (browserOptions.proxy.enabled && !browserOptions.proxy.server.empty()) {
                std::vector<const gchar*> ignored;
                for (const std::string& entry : browserOptions.proxy.bypassList) {
                    ignored.push_back(entry.c_str());
                }
                ignored.push_back(nullptr);
                WebKitNetworkProxySettings* proxy = webkit_network_proxy_settings_new(
                    browserOptions.proxy.server.c_str(), ignored.data());
                webkit_website_data_manager_set_network_proxy_settings(
                    webkit_web_context_get_website_data_manager(context), WEBKIT_NETWORK_PROXY_MODE_CUSTOM, proxy);
                webkit_network_proxy_settings_free(proxy);
            }
            if (!browserOptions.language.empty()) {
                const gchar* languages[] = {browserOptions.language.c_str(), nullptr};
                webkit_web_context_set_preferred_languages(context, languages);
            }
            contextCache.emplace(folder, context);
            return context;
        }

        std::string AuthenticationSchemeName(WebKitAuthenticationScheme scheme) {
            switch (scheme) {
                case WEBKIT_AUTHENTICATION_SCHEME_HTTP_BASIC:
                    return "basic";
                case WEBKIT_AUTHENTICATION_SCHEME_HTTP_DIGEST:
                    return "digest";
                case WEBKIT_AUTHENTICATION_SCHEME_HTML_FORM:
                    return "html-form";
                case WEBKIT_AUTHENTICATION_SCHEME_NTLM:
                    return "ntlm";
                case WEBKIT_AUTHENTICATION_SCHEME_NEGOTIATE:
                    return "negotiate";
                case WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_REQUESTED:
                    return "client-certificate";
                case WEBKIT_AUTHENTICATION_SCHEME_SERVER_TRUST_EVALUATION_REQUESTED:
                    return "server-trust";
                case WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_PIN_REQUESTED:
                    return "client-certificate-pin";
                case WEBKIT_AUTHENTICATION_SCHEME_DEFAULT:
                case WEBKIT_AUTHENTICATION_SCHEME_UNKNOWN:
                    break;
            }
            return "unknown";
        }

        Modifiers TranslateModifiers(GdkModifierType state) {
            Modifiers modifiers = 0;
            if ((state & GDK_MOD1_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::ALT);
            }
            if ((state & GDK_CONTROL_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::CONTROL);
            }
            if ((state & GDK_SHIFT_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::SHIFT);
            }
            if ((state & GDK_SUPER_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::META);
            }
            if ((state & GDK_LOCK_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::CAPS_LOCK);
            }
            if ((state & GDK_MOD2_MASK) != 0) {
                modifiers |= static_cast<Modifiers>(Modifier::NUM_LOCK);
            }
            return modifiers;
        }

        MouseButton TranslateButton(guint button) {
            switch (button) {
                case 1:
                    return MouseButton::LEFT;
                case 2:
                    return MouseButton::MIDDLE;
                case 3:
                    return MouseButton::RIGHT;
                case 8:
                    return MouseButton::EXTRA_1;
                case 9:
                    return MouseButton::EXTRA_2;
                default:
                    return MouseButton::NONE;
            }
        }

        MouseButtons TranslateButtons(GdkModifierType state) {
            MouseButtons buttons = 0;
            if ((state & GDK_BUTTON1_MASK) != 0) {
                buttons |= MouseButtonMask(MouseButton::LEFT);
            }
            if ((state & GDK_BUTTON2_MASK) != 0) {
                buttons |= MouseButtonMask(MouseButton::MIDDLE);
            }
            if ((state & GDK_BUTTON3_MASK) != 0) {
                buttons |= MouseButtonMask(MouseButton::RIGHT);
            }
            return buttons;
        }

        void AppendResponseHeader(const char* name, const char* value, gpointer data) {
            auto* headers = static_cast<HttpHeaderMap*>(data);
            if (name) {
                (*headers)[name] = value ? value : "";
            }
        }
    }

    LinuxWindowBackend::LinuxWindowBackend() : alive(std::make_shared<std::atomic<bool>>(true)) {
        capabilities.platformName = "linux";
        capabilities.multiWindow = true;
        capabilities.nativeKeyEvents = true;
        capabilities.nativeMouseEvents = true;
        capabilities.nativeWheelEvents = true;
        capabilities.fileDrop = true;
        capabilities.transparentWindows = true;
        capabilities.windowOpacity = true;
        capabilities.perWindowIcon = true;
        capabilities.taskbarVisibility = true;
        capabilities.customTitleBar = true;
        capabilities.draggableRegions = true;
        capabilities.virtualHostScheme = true;
        capabilities.customSchemes = true;
        capabilities.responseInterception = true;
        capabilities.proxy = true;
        capabilities.downloads = true;
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
        capabilities.devTools = true;
        capabilities.devToolsControl = true;
        capabilities.swipeNavigation = true;
        capabilities.zoomControl = true;
        capabilities.dpiEvents = true;
        capabilities.themeEvents = true;
        capabilities.systemThemeChange = true;
        capabilities.documentTitleEvents = true;
        capabilities.faviconEvents = true;
        capabilities.domContentLoadedEvents = true;
#if WEBKIT_CHECK_VERSION(2, 42, 0)
        capabilities.webSecurityToggle = true;
#endif
    }

    LinuxWindowBackend::~LinuxWindowBackend() {
        alive->store(false);
        suppressEvents = true;
        if (webView) {
            std::lock_guard<std::mutex> guard(registryMutex);
            webViewRegistry.erase(webView);
        }
        CancelDownloads();
        if (cancellable) {
            g_cancellable_cancel(cancellable);
        }
        if (context) {
            g_signal_handlers_disconnect_by_data(context, this);
        }
        GtkSettings* gtkSettings = gtk_settings_get_default();
        if (gtkSettings) {
            g_signal_handlers_disconnect_by_data(gtkSettings, this);
        }
        if (window && !destroyed) {
            gtk_widget_destroy(window);
        }
        if (context) {
            g_object_unref(context);
            context = nullptr;
        }
        if (cancellable) {
            g_object_unref(cancellable);
            cancellable = nullptr;
        }
        if (ownsTemporaryFolder && !temporaryFolder.empty()) {
            std::error_code error;
            std::filesystem::remove_all(temporaryFolder, error);
        }
    }

    Result<void> LinuxWindowBackend::Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
        WindowBackendDelegate& backendDelegate, void* parentHandle) {
        options = windowOptions;
        this->browserOptions = browserOptions;
        delegate = &backendDelegate;
        capabilities.platformName = "linux";
        capabilities.multiWindow = true;
        capabilities.nativeKeyEvents = true;
        capabilities.nativeMouseEvents = true;
        capabilities.nativeWheelEvents = true;
        capabilities.fileDrop = true;
        capabilities.transparentWindows = true;
        capabilities.windowOpacity = true;
        capabilities.perWindowIcon = true;
        capabilities.taskbarVisibility = true;
        capabilities.customTitleBar = true;
        capabilities.draggableRegions = true;
        capabilities.virtualHostScheme = true;
        capabilities.customSchemes = true;
        capabilities.responseInterception = true;
        capabilities.proxy = true;
        capabilities.downloads = true;
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
        capabilities.devTools = true;
        capabilities.devToolsControl = true;
        capabilities.swipeNavigation = true;
        capabilities.zoomControl = true;
#if WEBKIT_CHECK_VERSION(2, 42, 0)
        capabilities.webSecurityToggle = true;
#endif
        capabilities.dpiEvents = true;
        capabilities.themeEvents = true;
        capabilities.systemThemeChange = true;
        capabilities.documentTitleEvents = true;
        capabilities.faviconEvents = true;
        capabilities.domContentLoadedEvents = true;
        capabilities.notes.emplace_back(
            "HTTPS virtual hosts are unavailable on WebKitGTK; Flare serves virtual hosts through a custom scheme");
        capabilities.notes.emplace_back(
            "HTTP header injection and arbitrary request interception are not exposed by WebKitGTK");
        capabilities.notes.emplace_back("PDF export is not exposed by the WebKitGTK host API");
        capabilities.notes.emplace_back("Iframe bridging is not supported on WebKitGTK");
        capabilities.notes.emplace_back(
            "Rounded window corners and borderless shadows follow the system theme on Linux");

        if (browserOptions.proxy.enabled && browserOptions.proxy.server.empty()
            && !browserOptions.proxy.pacUrl.empty()) {
            return Error(ErrorCode::NOT_SUPPORTED,
                "WebKitGTK does not expose PAC proxy configuration; configure an explicit proxy server");
        }

        effectiveTheme = options.theme;
        if (effectiveTheme == Theme::SYSTEM && CurrentLinuxAppSettings().theme != Theme::SYSTEM) {
            effectiveTheme = CurrentLinuxAppSettings().theme;
        }
        darkMode = effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemDarkTheme());
        currentBackground = options.backgroundColor;
        windowOpacity = options.opacity;
        userAgent = browserOptions.userAgent;
        zoomFactor = browserOptions.zoomFactor;
        draggableRegions = options.draggableRegions;

        context = AcquireContext(browserOptions);
        if (!context) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the WebKitGTK web context");
        }
        RegisterSchemes();

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the GTK window");
        }
        gtk_window_set_title(GTK_WINDOW(window), options.title.c_str());
        gtk_window_set_default_size(GTK_WINDOW(window), options.width, options.height);
        gtk_window_set_resizable(GTK_WINDOW(window), options.resizable ? TRUE : FALSE);
        gtk_window_set_deletable(GTK_WINDOW(window), options.closable ? TRUE : FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), options.showInTaskbar ? FALSE : TRUE);
        gtk_widget_set_opacity(window, windowOpacity);
        gtk_window_set_keep_above(GTK_WINDOW(window), options.alwaysOnTop ? TRUE : FALSE);
        const bool customChrome = options.frameless || options.titleBarStyle != TitleBarStyle::NATIVE;
        gtk_window_set_decorated(GTK_WINDOW(window), customChrome ? FALSE : TRUE);
        if (!options.icon.IsEmpty()) {
            SetIcon(options.icon);
        }
        if (parentHandle) {
            gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parentHandle));
        }
        if (options.modal) {
            gtk_window_set_modal(GTK_WINDOW(window), TRUE);
        }
        if (options.hasPosition) {
            gtk_window_move(GTK_WINDOW(window), options.x, options.y);
        } else if (options.centered) {
            gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
        }
        ApplyGeometryHints();
        ApplyBackground();

        if (options.transparent) {
            GdkScreen* screen = gtk_widget_get_screen(window);
            GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
            if (visual) {
                gtk_widget_set_visual(window, visual);
            }
            gtk_widget_set_app_paintable(window, TRUE);
        }

        g_signal_connect(window, "delete-event", G_CALLBACK(&LinuxWindowBackend::OnDeleteEvent), this);
        g_signal_connect(window, "destroy", G_CALLBACK(&LinuxWindowBackend::OnDestroy), this);
        g_signal_connect(window, "map", G_CALLBACK(&LinuxWindowBackend::OnMap), this);
        g_signal_connect(window, "unmap", G_CALLBACK(&LinuxWindowBackend::OnUnmap), this);
        g_signal_connect(window, "configure-event", G_CALLBACK(&LinuxWindowBackend::OnConfigure), this);
        g_signal_connect(window, "window-state-event", G_CALLBACK(&LinuxWindowBackend::OnWindowState), this);
        g_signal_connect(window, "notify::scale-factor", G_CALLBACK(&LinuxWindowBackend::OnScaleChanged), this);
        GtkSettings* gtkSettings = gtk_settings_get_default();
        if (gtkSettings) {
            g_signal_connect(
                gtkSettings, "notify::gtk-theme-name", G_CALLBACK(&LinuxWindowBackend::OnThemeChanged), this);
            g_signal_connect(gtkSettings, "notify::gtk-application-prefer-dark-theme",
                G_CALLBACK(&LinuxWindowBackend::OnThemeChanged), this);
        }

        cancellable = g_cancellable_new();
        gtk_widget_realize(window);
        created = true;
        if (delegate) {
            delegate->OnBackendCreated();
        }

        auto* payload = new IdlePayload{alive, this};
        g_idle_add(&RunCreateWebView, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::RegisterSchemes() {
        if (!context) {
            return;
        }
        if (browserOptions.virtualHost.has_value()) {
            RegisterScheme(context, browserOptions.virtualHost->scheme, &LinuxWindowBackend::OnSchemeRequest);
        }
        for (const auto& entry : browserOptions.customSchemes) {
            RegisterScheme(context, entry.first, &LinuxWindowBackend::OnSchemeRequest);
        }
    }

    void LinuxWindowBackend::CreateWebView() {
        if (destroyed || !window || !context) {
            return;
        }
        webView = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
        if (!webView) {
            RaiseFailure(Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the WebKitGTK web view"));
            return;
        }
        {
            std::lock_guard<std::mutex> guard(registryMutex);
            webViewRegistry[webView] = this;
        }
        RegisterMessageHandlers();
        RegisterWebViewSignals();
        ApplySettings();
        gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(webView));
        gtk_widget_show(GTK_WIDGET(webView));
        gtk_widget_add_events(GTK_WIDGET(webView),
            GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_FOCUS_CHANGE_MASK);
        gtk_drag_dest_add_uri_targets(GTK_WIDGET(webView));
        ApplyBackground();

        WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(webView);
        for (const std::string& script : browserOptions.documentStartScripts) {
            AddUserScript(
                manager, script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START);
        }
        for (const std::string& script : browserOptions.documentEndScripts) {
            AddUserScript(
                manager, script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END);
        }
        AddUserScript(manager, LifecycleScript(), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START);

        ConnectContextDownloads();

        if (browserOptions.clearCacheOnStart) {
            if (DataManager()) {
                StartCacheClear();
                return;
            }
        }
        RaiseReady();
    }

    void LinuxWindowBackend::RegisterMessageHandlers() {
        WebKitUserContentManager* manager = webkit_web_view_get_user_content_manager(webView);
        if (!manager) {
            RaiseFailure(Error(ErrorCode::PLATFORM_FAILURE, "The WebKitGTK user content manager is unavailable"));
            return;
        }
        const gboolean bridgeRegistered = webkit_user_content_manager_register_script_message_handler(manager, "flare");
        const gboolean lifecycleRegistered = webkit_user_content_manager_register_script_message_handler(
            manager, "flareLifecycle");
        if (!bridgeRegistered || !lifecycleRegistered) {
            RaiseFailure(
                Error(ErrorCode::PLATFORM_FAILURE, "Unable to register the WebKitGTK script message handlers"));
            return;
        }
        g_signal_connect(
            manager, "script-message-received::flare", G_CALLBACK(&LinuxWindowBackend::OnBridgeMessage), this);
        g_signal_connect(manager, "script-message-received::flareLifecycle",
            G_CALLBACK(&LinuxWindowBackend::OnLifecycleMessage), this);
    }

    void LinuxWindowBackend::RegisterWebViewSignals() {
        g_signal_connect(webView, "decide-policy", G_CALLBACK(&LinuxWindowBackend::OnDecidePolicy), this);
        g_signal_connect(webView, "load-changed", G_CALLBACK(&LinuxWindowBackend::OnLoadChanged), this);
        g_signal_connect(webView, "load-failed", G_CALLBACK(&LinuxWindowBackend::OnLoadFailed), this);
        g_signal_connect(webView, "load-failed-with-tls-errors", G_CALLBACK(&LinuxWindowBackend::OnTlsError), this);
        g_signal_connect(webView, "notify::title", G_CALLBACK(&LinuxWindowBackend::OnTitleChanged), this);
        g_signal_connect(webView, "notify::uri", G_CALLBACK(&LinuxWindowBackend::OnUriChanged), this);
        g_signal_connect(webView, "permission-request", G_CALLBACK(&LinuxWindowBackend::OnPermissionRequest), this);
        g_signal_connect(webView, "script-dialog", G_CALLBACK(&LinuxWindowBackend::OnScriptDialog), this);
        g_signal_connect(webView, "context-menu", G_CALLBACK(&LinuxWindowBackend::OnContextMenu), this);
        g_signal_connect(webView, "authenticate", G_CALLBACK(&LinuxWindowBackend::OnAuthenticate), this);
        g_signal_connect(webView, "enter-fullscreen", G_CALLBACK(&LinuxWindowBackend::OnEnterFullscreen), this);
        g_signal_connect(webView, "leave-fullscreen", G_CALLBACK(&LinuxWindowBackend::OnLeaveFullscreen), this);
        g_signal_connect(webView, "web-process-terminated", G_CALLBACK(&LinuxWindowBackend::OnProcessTerminated), this);
        g_signal_connect(webView, "create", G_CALLBACK(&LinuxWindowBackend::OnCreateWindow), this);
        g_signal_connect(webView, "close", G_CALLBACK(&LinuxWindowBackend::OnCloseRequested), this);
        g_signal_connect(
            webView, "resource-load-started", G_CALLBACK(&LinuxWindowBackend::OnResourceLoadStarted), this);
        g_signal_connect(webView, "key-press-event", G_CALLBACK(&LinuxWindowBackend::OnKeyPress), this);
        g_signal_connect(webView, "key-release-event", G_CALLBACK(&LinuxWindowBackend::OnKeyRelease), this);
        g_signal_connect(webView, "button-press-event", G_CALLBACK(&LinuxWindowBackend::OnButtonPress), this);
        g_signal_connect(webView, "button-release-event", G_CALLBACK(&LinuxWindowBackend::OnButtonRelease), this);
        g_signal_connect(webView, "motion-notify-event", G_CALLBACK(&LinuxWindowBackend::OnMotion), this);
        g_signal_connect(webView, "scroll-event", G_CALLBACK(&LinuxWindowBackend::OnScroll), this);
        g_signal_connect(webView, "focus-in-event", G_CALLBACK(&LinuxWindowBackend::OnFocusIn), this);
        g_signal_connect(webView, "focus-out-event", G_CALLBACK(&LinuxWindowBackend::OnFocusOut), this);
        g_signal_connect(webView, "drag-data-received", G_CALLBACK(&LinuxWindowBackend::OnDragDataReceived), this);
        WebKitFindController* findController = webkit_web_view_get_find_controller(webView);
        if (findController) {
            g_signal_connect(findController, "counted-matches", G_CALLBACK(&LinuxWindowBackend::OnFindCounted), this);
        }
    }

    void LinuxWindowBackend::ApplySettings() {
        WebKitSettings* settings = webkit_settings_new();
        webkit_settings_set_enable_javascript(settings, browserOptions.javascriptEnabled ? TRUE : FALSE);
        webkit_settings_set_enable_developer_extras(settings, browserOptions.devToolsEnabled ? TRUE : FALSE);
        webkit_settings_set_allow_modal_dialogs(settings, browserOptions.defaultScriptDialogsEnabled ? TRUE : FALSE);
#if WEBKIT_CHECK_VERSION(2, 42, 0)
        webkit_settings_set_disable_web_security(settings, browserOptions.webSecurityEnabled ? FALSE : TRUE);
#endif
        webkit_settings_set_enable_fullscreen(settings, TRUE);
        webkit_settings_set_allow_file_access_from_file_urls(
            settings, browserOptions.allowFileAccessFromFileUrls ? TRUE : FALSE);
        webkit_settings_set_enable_back_forward_navigation_gestures(
            settings, browserOptions.swipeNavigationEnabled ? TRUE : FALSE);
        webkit_settings_set_hardware_acceleration_policy(settings,
            CurrentLinuxAppSettings().hardwareAcceleration ? WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS
                                                           : WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
        webkit_settings_set_print_backgrounds(settings, TRUE);
        webkit_settings_set_enable_media_stream(settings, TRUE);
        webkit_settings_set_enable_webrtc(settings, TRUE);
        if (!browserOptions.userAgent.empty()) {
            webkit_settings_set_user_agent(settings, browserOptions.userAgent.c_str());
        }
        webkit_web_view_set_settings(webView, settings);
        g_object_unref(settings);
        webkit_web_view_set_zoom_level(webView, zoomFactor);
    }

    void LinuxWindowBackend::ApplyBackground() {
        if (!window) {
            return;
        }
        if (cssProvider) {
            GtkStyleContext* style = gtk_widget_get_style_context(window);
            gtk_style_context_remove_provider(style, GTK_STYLE_PROVIDER(cssProvider));
            g_object_unref(cssProvider);
            cssProvider = nullptr;
        }
        cssProvider = gtk_css_provider_new();
        const double alpha = options.transparent ? 0.0 : static_cast<double>(currentBackground.a) / 255.0;
        char css[256]{};
        std::snprintf(css, sizeof(css), "window.flare-window { background-color: rgba(%u, %u, %u, %.3f); }",
            currentBackground.r, currentBackground.g, currentBackground.b, alpha);
        gtk_css_provider_load_from_data(cssProvider, css, -1, nullptr);
        GtkStyleContext* style = gtk_widget_get_style_context(window);
        gtk_style_context_add_class(style, "flare-window");
        gtk_style_context_add_provider(style, GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        if (webView) {
            GdkRGBA color{currentBackground.r / 255.0, currentBackground.g / 255.0, currentBackground.b / 255.0,
                options.transparent ? 0.0 : currentBackground.a / 255.0};
            webkit_web_view_set_background_color(webView, &color);
        }
    }

    void LinuxWindowBackend::ApplyGeometryHints() {
        if (!window) {
            return;
        }
        GdkGeometry geometry{};
        GdkWindowHints hints = static_cast<GdkWindowHints>(0);
        if (options.minWidth > 0 || options.minHeight > 0) {
            geometry.min_width = options.minWidth;
            geometry.min_height = options.minHeight;
            hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MIN_SIZE);
        }
        if (options.maxWidth > 0 || options.maxHeight > 0) {
            geometry.max_width = options.maxWidth > 0 ? options.maxWidth : G_MAXINT;
            geometry.max_height = options.maxHeight > 0 ? options.maxHeight : G_MAXINT;
            hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MAX_SIZE);
        }
        gtk_window_set_geometry_hints(GTK_WINDOW(window), nullptr, &geometry, hints);
    }

    void LinuxWindowBackend::RaiseReady() {
        if (readyRaised || failureRaised || destroyed) {
            return;
        }
        readyRaised = true;
        if (delegate) {
            delegate->OnBackendBrowserReady();
        }
    }

    void LinuxWindowBackend::RaiseFailure(const Error& error) {
        if (failureRaised || destroyed) {
            return;
        }
        failureRaised = true;
        if (delegate) {
            delegate->OnBackendBrowserCreationFailed(error);
        }
    }

    void LinuxWindowBackend::ConnectContextDownloads() {
        if (!context || contextDownloadConnected) {
            return;
        }
        g_signal_connect(context, "download-started", G_CALLBACK(&LinuxWindowBackend::OnDownloadStarted), this);
        contextDownloadConnected = true;
    }

    WebKitWebsiteDataManager* LinuxWindowBackend::DataManager() const {
        return context ? webkit_web_context_get_website_data_manager(context) : nullptr;
    }

    std::string LinuxWindowBackend::CurrentUri() const {
        if (!webView) {
            return currentUri;
        }
        const gchar* uri = webkit_web_view_get_uri(webView);
        return uri ? std::string(uri) : currentUri;
    }

    bool LinuxWindowBackend::SystemDarkTheme() const {
        GtkSettings* settings = gtk_settings_get_default();
        if (!settings) {
            return false;
        }
        gboolean preferDark = FALSE;
        g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, nullptr);
        if (preferDark) {
            return true;
        }
        gchar* themeName = nullptr;
        g_object_get(settings, "gtk-theme-name", &themeName, nullptr);
        const bool dark = themeName && ToLowerAscii(themeName).find("dark") != std::string::npos;
        g_free(themeName);
        return dark;
    }

    void LinuxWindowBackend::Close() {
        if (destroyed || closing) {
            return;
        }
        closing = true;
        if (window) {
            gtk_widget_destroy(window);
        }
    }

    void* LinuxWindowBackend::GetNativeHandle() const {
        return window;
    }

    const PlatformCapabilities& LinuxWindowBackend::Capabilities() const {
        return capabilities;
    }

    Result<void> LinuxWindowBackend::RequireWebView() const {
        if (!webView || destroyed) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The WebKitGTK web view is not ready");
        }
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Show() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_widget_show(window);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Hide() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_widget_hide(window);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Minimize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_iconify(GTK_WINDOW(window));
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Maximize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_maximize(GTK_WINDOW(window));
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Restore() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        if (fullscreen) {
            gtk_window_unfullscreen(GTK_WINDOW(window));
        }
        gtk_window_unmaximize(GTK_WINDOW(window));
        gtk_window_deiconify(GTK_WINDOW(window));
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Focus() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        if (gtk_widget_get_window(window)
            && gdk_window_get_state(gtk_widget_get_window(window)) & GDK_WINDOW_STATE_ICONIFIED) {
            gtk_window_deiconify(GTK_WINDOW(window));
        }
        gtk_window_present(GTK_WINDOW(window));
        if (webView) {
            gtk_widget_grab_focus(GTK_WIDGET(webView));
        }
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::StartDrag() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        if (!options.movable) {
            return Error(ErrorCode::PERMISSION_DENIED, "The window is not movable");
        }
        GdkDisplay* display = gdk_display_get_default();
        GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
        GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
        gint x = 0;
        gint y = 0;
        if (pointer) {
            gdk_device_get_position(pointer, nullptr, &x, &y);
        }
        gtk_window_begin_move_drag(GTK_WINDOW(window), 1, x, y, GDK_CURRENT_TIME);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetTitle(const std::string& title) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_set_title(GTK_WINDOW(window), title.c_str());
        options.title = title;
        return Result<void>::Ok();
    }

    Result<std::string> LinuxWindowBackend::GetTitle() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        const gchar* title = gtk_window_get_title(GTK_WINDOW(window));
        return title ? std::string(title) : std::string();
    }

    Result<void> LinuxWindowBackend::SetSize(int width, int height) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_resize(GTK_WINDOW(window), width, height);
        options.width = width;
        options.height = height;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetPosition(int x, int y) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_move(GTK_WINDOW(window), x, y);
        options.x = x;
        options.y = y;
        options.hasPosition = true;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetMinSize(int width, int height) {
        options.minWidth = width;
        options.minHeight = height;
        ApplyGeometryHints();
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetMaxSize(int width, int height) {
        options.maxWidth = width;
        options.maxHeight = height;
        ApplyGeometryHints();
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetFullscreen(bool value) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        if (value) {
            gtk_window_fullscreen(GTK_WINDOW(window));
        } else {
            gtk_window_unfullscreen(GTK_WINDOW(window));
        }
        options.fullscreen = value;
        const bool changed = fullscreen != value;
        fullscreen = value;
        if (changed && !suppressEvents && delegate) {
            delegate->OnBackendFullscreenChanged(value);
        }
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetAlwaysOnTop(bool alwaysOnTop) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gtk_window_set_keep_above(GTK_WINDOW(window), alwaysOnTop ? TRUE : FALSE);
        options.alwaysOnTop = alwaysOnTop;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetOpacity(double opacity) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        windowOpacity = opacity;
        options.opacity = opacity;
        gtk_widget_set_opacity(window, opacity);
        return Result<void>::Ok();
    }

    Result<double> LinuxWindowBackend::GetOpacity() const {
        return window ? gtk_widget_get_opacity(window) : windowOpacity;
    }

    Result<void> LinuxWindowBackend::Center() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        GdkDisplay* display = gdk_display_get_default();
        if (!display) {
            return Error(ErrorCode::PLATFORM_FAILURE, "No display is available");
        }
        GdkWindow* gdkWindow = gtk_widget_get_window(window);
        GdkMonitor* monitor = gdkWindow ? gdk_display_get_monitor_at_window(display, gdkWindow)
                                        : gdk_display_get_primary_monitor(display);
        if (!monitor) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to determine the current monitor");
        }
        GdkRectangle workarea{};
        gdk_monitor_get_workarea(monitor, &workarea);
        gint width = 0;
        gint height = 0;
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        gtk_window_move(
            GTK_WINDOW(window), workarea.x + (workarea.width - width) / 2, workarea.y + (workarea.height - height) / 2);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetBackgroundColor(Color color) {
        currentBackground = color;
        options.backgroundColor = color;
        ApplyBackground();
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetTheme(Theme theme) {
        effectiveTheme = theme;
        options.theme = theme;
        darkMode = effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemDarkTheme());
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetContentProtection(bool) {
        return Error(ErrorCode::NOT_SUPPORTED, "Content protection is not supported by WebKitGTK");
    }

    Result<void> LinuxWindowBackend::SetResizable(bool resizable) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        options.resizable = resizable;
        gtk_window_set_resizable(GTK_WINDOW(window), resizable ? TRUE : FALSE);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetMovable(bool movable) {
        options.movable = movable;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetMinimizable(bool minimizable) {
        options.minimizable = minimizable;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetMaximizable(bool maximizable) {
        options.maximizable = maximizable;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetClosable(bool closable) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        options.closable = closable;
        gtk_window_set_deletable(GTK_WINDOW(window), closable ? TRUE : FALSE);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetShowInTaskbar(bool show) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        options.showInTaskbar = show;
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), show ? FALSE : TRUE);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetIcon(const Icon& iconValue) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        if (iconValue.IsEmpty()) {
            return Error(ErrorCode::INVALID_ARGUMENT, "The window icon is empty");
        }
        if (iconValue.source == IconSource::RESOURCE) {
            gtk_window_set_icon_name(GTK_WINDOW(window), iconValue.resource.c_str());
            options.icon = iconValue;
            return Result<void>::Ok();
        }
        GError* error = nullptr;
        if (!gtk_window_set_icon_from_file(GTK_WINDOW(window), PathToUtf8(iconValue.path).c_str(), &error)) {
            const std::string message = error ? error->message : "Unable to load the icon";
            if (error) {
                g_error_free(error);
            }
            return Error(ErrorCode::IO_FAILURE, message);
        }
        options.icon = iconValue;
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetDraggableRegions(const std::vector<Rect>& regions) {
        draggableRegions = regions;
        options.draggableRegions = regions;
        return Result<void>::Ok();
    }

    Result<Rect> LinuxWindowBackend::GetBounds() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The GTK window is unavailable");
        }
        gint x = 0;
        gint y = 0;
        gint width = 0;
        gint height = 0;
        gtk_window_get_position(GTK_WINDOW(window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        return Rect{
            static_cast<double>(x), static_cast<double>(y), static_cast<double>(width), static_cast<double>(height)};
    }

    bool LinuxWindowBackend::IsVisible() const {
        return window && gtk_widget_get_mapped(window);
    }

    bool LinuxWindowBackend::IsMaximized() const {
        return window && gtk_window_is_maximized(GTK_WINDOW(window));
    }

    bool LinuxWindowBackend::IsMinimized() const {
        if (!window) {
            return false;
        }
        GdkWindow* gdkWindow = gtk_widget_get_window(window);
        return minimized || (gdkWindow && (gdk_window_get_state(gdkWindow) & GDK_WINDOW_STATE_ICONIFIED) != 0);
    }

    bool LinuxWindowBackend::IsFocused() const {
        return window && gtk_window_is_active(GTK_WINDOW(window));
    }

    bool LinuxWindowBackend::IsFullscreen() const {
        return fullscreen;
    }

    double LinuxWindowBackend::GetDpiScale() const {
        return window ? gtk_widget_get_scale_factor(window) : 1.0;
    }

    void LinuxWindowBackend::UpdateBoundsFromWindow() {
        if (!window || destroyed) {
            return;
        }
        gint x = 0;
        gint y = 0;
        gint width = 0;
        gint height = 0;
        gtk_window_get_position(GTK_WINDOW(window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        if (!suppressEvents && delegate) {
            delegate->OnBackendBoundsChanged(Rect{static_cast<double>(x), static_cast<double>(y),
                                                 static_cast<double>(width), static_cast<double>(height)},
                maximized);
        }
    }

    void LinuxWindowBackend::UpdateTheme() {
        const bool changed = darkMode
            != (effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemDarkTheme()));
        darkMode = effectiveTheme == Theme::DARK || (effectiveTheme == Theme::SYSTEM && SystemDarkTheme());
        if (changed && !suppressEvents && delegate) {
            delegate->OnBackendThemeChanged(effectiveTheme, darkMode ? Theme::DARK : Theme::LIGHT);
        }
    }

    void LinuxWindowBackend::CancelDownloads() {
        for (auto& entry : downloads) {
            WebKitDownload* download = entry.first;
            g_signal_handlers_disconnect_by_data(download, this);
            webkit_download_cancel(download);
            g_object_unref(download);
        }
        downloads.clear();
    }

    void LinuxWindowBackend::OnDestroy(GtkWidget*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->destroyed) {
            return;
        }
        self->destroyed = true;
        self->closing = true;
        self->visible = false;
        if (self->webView) {
            std::lock_guard<std::mutex> guard(registryMutex);
            webViewRegistry.erase(self->webView);
            self->webView = nullptr;
        }
        self->CancelDownloads();
        if (self->context && self->contextDownloadConnected) {
            g_signal_handlers_disconnect_by_data(self->context, self);
            self->contextDownloadConnected = false;
        }
        if (self->cancellable) {
            g_cancellable_cancel(self->cancellable);
        }
        GtkSettings* settings = gtk_settings_get_default();
        if (settings) {
            g_signal_handlers_disconnect_by_data(settings, self);
        }
        if (self->cssProvider) {
            GtkStyleContext* style = gtk_widget_get_style_context(self->window);
            gtk_style_context_remove_provider(style, GTK_STYLE_PROVIDER(self->cssProvider));
            g_object_unref(self->cssProvider);
            self->cssProvider = nullptr;
        }
        if (self->context) {
            g_object_unref(self->context);
            self->context = nullptr;
        }
        self->window = nullptr;
        if (!self->suppressEvents && self->delegate) {
            self->delegate->OnBackendClosed();
        }
    }

    gboolean LinuxWindowBackend::OnDeleteEvent(GtkWidget*, GdkEvent*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->suppressEvents) {
            return FALSE;
        }
        if (!self->options.closable) {
            return TRUE;
        }
        if (self->delegate && !self->delegate->OnBackendCloseRequested()) {
            return TRUE;
        }
        return FALSE;
    }

    void LinuxWindowBackend::OnMap(GtkWidget*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->visible) {
            return;
        }
        self->visible = true;
        if (!self->suppressEvents && self->delegate) {
            self->delegate->OnBackendVisibilityChanged(true);
        }
    }

    void LinuxWindowBackend::OnUnmap(GtkWidget*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->visible) {
            return;
        }
        self->visible = false;
        if (!self->suppressEvents && self->delegate) {
            self->delegate->OnBackendVisibilityChanged(false);
        }
    }

    gboolean LinuxWindowBackend::OnConfigure(GtkWidget*, GdkEventConfigure* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->suppressEvents && self->delegate) {
            self->delegate->OnBackendBoundsChanged(
                Rect{static_cast<double>(event->x), static_cast<double>(event->y), static_cast<double>(event->width),
                    static_cast<double>(event->height)},
                self->maximized);
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnWindowState(GtkWidget*, GdkEventWindowState* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const bool nowMinimized = (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) != 0;
        const bool nowMaximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
        const bool nowFullscreen = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
        if (nowMinimized != self->minimized) {
            self->minimized = nowMinimized;
            if (!self->suppressEvents && self->delegate) {
                if (nowMinimized) {
                    self->delegate->OnBackendMinimized();
                }
            }
        }
        if (nowMaximized != self->maximized) {
            self->maximized = nowMaximized;
            if (!self->suppressEvents && self->delegate) {
                if (nowMaximized) {
                    self->delegate->OnBackendMaximized();
                } else {
                    self->delegate->OnBackendRestored();
                }
            }
        }
        if (nowFullscreen != self->fullscreen) {
            self->fullscreen = nowFullscreen;
            if (!self->suppressEvents && self->delegate) {
                self->delegate->OnBackendFullscreenChanged(nowFullscreen);
            }
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnKeyPress(GtkWidget*, GdkEventKey* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const gchar* keyName = gdk_keyval_name(event->keyval);
        const std::string key = keyName ? keyName : std::string();
        const std::string code = "Key" + std::to_string(event->hardware_keycode);
        const bool cancel = self->delegate
            ? self->delegate->OnBackendKey(true, key, code, static_cast<int>(event->keyval),
                  TranslateModifiers(static_cast<GdkModifierType>(event->state)), false)
            : false;
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnKeyRelease(GtkWidget*, GdkEventKey* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const gchar* keyName = gdk_keyval_name(event->keyval);
        const std::string key = keyName ? keyName : std::string();
        const std::string code = "Key" + std::to_string(event->hardware_keycode);
        const bool cancel = self->delegate
            ? self->delegate->OnBackendKey(false, key, code, static_cast<int>(event->keyval),
                  TranslateModifiers(static_cast<GdkModifierType>(event->state)), false)
            : false;
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnButtonPress(GtkWidget*, GdkEventButton* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return FALSE;
        }
        const MouseEventType type = event->type == GDK_2BUTTON_PRESS ? MouseEventType::DOUBLE_CLICK_EVENT
                                                                     : MouseEventType::DOWN;
        const int clicks = event->type == GDK_2BUTTON_PRESS ? 2 : 1;
        const bool cancel = self->delegate->OnBackendMouse(type, event->x, event->y, TranslateButton(event->button),
            TranslateButtons(static_cast<GdkModifierType>(event->state)),
            TranslateModifiers(static_cast<GdkModifierType>(event->state)), clicks);
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnButtonRelease(GtkWidget*, GdkEventButton* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return FALSE;
        }
        const bool cancel = self->delegate->OnBackendMouse(MouseEventType::UP, event->x, event->y,
            TranslateButton(event->button), TranslateButtons(static_cast<GdkModifierType>(event->state)),
            TranslateModifiers(static_cast<GdkModifierType>(event->state)), 1);
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnMotion(GtkWidget*, GdkEventMotion* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return FALSE;
        }
        const bool cancel = self->delegate->OnBackendMouse(MouseEventType::MOVE, event->x, event->y, MouseButton::NONE,
            TranslateButtons(static_cast<GdkModifierType>(event->state)),
            TranslateModifiers(static_cast<GdkModifierType>(event->state)), 0);
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnScroll(GtkWidget*, GdkEventScroll* event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return FALSE;
        }
        gdouble deltaX = 0.0;
        gdouble deltaY = 0.0;
        if (!gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &deltaX, &deltaY)) {
            switch (event->direction) {
                case GDK_SCROLL_UP:
                    deltaY = -1.0;
                    break;
                case GDK_SCROLL_DOWN:
                    deltaY = 1.0;
                    break;
                case GDK_SCROLL_LEFT:
                    deltaX = -1.0;
                    break;
                case GDK_SCROLL_RIGHT:
                    deltaX = 1.0;
                    break;
                default:
                    break;
            }
        }
        const bool cancel = self->delegate->OnBackendWheel(
            event->x, event->y, deltaX, deltaY, TranslateModifiers(static_cast<GdkModifierType>(event->state)));
        return cancel ? TRUE : FALSE;
    }

    gboolean LinuxWindowBackend::OnFocusIn(GtkWidget*, GdkEventFocus*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->focused) {
            self->focused = true;
            if (!self->suppressEvents && self->delegate) {
                self->delegate->OnBackendFocusChanged(true);
            }
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnFocusOut(GtkWidget*, GdkEventFocus*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->focused) {
            self->focused = false;
            if (!self->suppressEvents && self->delegate) {
                self->delegate->OnBackendFocusChanged(false);
            }
        }
        return FALSE;
    }

    void LinuxWindowBackend::OnDragDataReceived(GtkWidget*, GdkDragContext* context, gint x, gint y,
        GtkSelectionData* selection, guint, guint time, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        gchar** uris = gtk_selection_data_get_uris(selection);
        std::vector<std::filesystem::path> files;
        if (uris) {
            for (gchar** entry = uris; *entry; ++entry) {
                gchar* filename = g_filename_from_uri(*entry, nullptr, nullptr);
                if (filename) {
                    files.emplace_back(filename);
                    g_free(filename);
                }
            }
            g_strfreev(uris);
        }
        gtk_drag_finish(context, files.empty() ? FALSE : TRUE, FALSE, time);
        if (!files.empty() && !self->suppressEvents && self->delegate) {
            self->delegate->OnBackendFileDrop(std::move(files), Point{static_cast<double>(x), static_cast<double>(y)});
        }
    }

    void LinuxWindowBackend::OnScaleChanged(GObject*, GParamSpec*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->window || self->suppressEvents || !self->delegate) {
            return;
        }
        gint x = 0;
        gint y = 0;
        gint width = 0;
        gint height = 0;
        gtk_window_get_position(GTK_WINDOW(self->window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(self->window), &width, &height);
        self->delegate->OnBackendDpiChanged(gtk_widget_get_scale_factor(self->window),
            Rect{static_cast<double>(x), static_cast<double>(y), static_cast<double>(width),
                static_cast<double>(height)});
    }

    void LinuxWindowBackend::OnThemeChanged(GObject*, GParamSpec*, gpointer data) {
        static_cast<LinuxWindowBackend*>(data)->UpdateTheme();
    }

    gboolean LinuxWindowBackend::OnDecidePolicy(
        WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
            auto* navigationDecision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
            WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(
                navigationDecision);
            WebKitURIRequest* request = action ? webkit_navigation_action_get_request(action) : nullptr;
            if (!request) {
                webkit_policy_decision_use(decision);
                return TRUE;
            }
            const gchar* uri = webkit_uri_request_get_uri(request);
            const gchar* method = webkit_uri_request_get_http_method(request);
            const std::string url = uri ? uri : std::string();
            const std::string requestMethod = method ? method : std::string("GET");
            const bool redirect = action ? webkit_navigation_action_is_redirect(action) != FALSE : false;
            const bool userGesture = action ? webkit_navigation_action_is_user_gesture(action) != FALSE : false;
            const NavigationDecision result = self->delegate
                ? self->delegate->OnBackendNavigationStarting(url, requestMethod, redirect, true, false, userGesture)
                : NavigationDecision{true, {}};
            if (result.allow) {
                webkit_policy_decision_use(decision);
            } else {
                webkit_policy_decision_ignore(decision);
                if (!result.redirectUrl.empty() && self->webView) {
                    webkit_web_view_load_uri(self->webView, result.redirectUrl.c_str());
                }
            }
            return TRUE;
        }
        if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
            auto* navigationDecision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
            WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(
                navigationDecision);
            WebKitURIRequest* request = action ? webkit_navigation_action_get_request(action) : nullptr;
            const gchar* uri = request ? webkit_uri_request_get_uri(request) : nullptr;
            if (self->delegate) {
                self->delegate->OnBackendNewWindowRequested(uri ? uri : std::string(), true);
            }
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
        return FALSE;
    }

    void LinuxWindowBackend::OnLoadChanged(WebKitWebView* view, WebKitLoadEvent event, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return;
        }
        if (event == WEBKIT_LOAD_STARTED) {
            self->loadFailed = false;
            self->delegate->OnBackendContentLoading(false, true);
        } else if (event == WEBKIT_LOAD_COMMITTED) {
            const gchar* uri = webkit_web_view_get_uri(view);
            self->currentUri = uri ? uri : std::string();
            self->delegate->OnBackendSourceChanged(self->currentUri, true, true);
            self->delegate->OnBackendContentLoading(false, true);
        } else if (event == WEBKIT_LOAD_FINISHED) {
            if (!self->loadFailed) {
                const gchar* uri = webkit_web_view_get_uri(view);
                const std::string url = uri ? uri : self->currentUri;
                self->delegate->OnBackendNavigationCompleted(url, true, 0, Error());
                self->loadFailed = false;
            }
        }
    }

    gboolean LinuxWindowBackend::OnLoadFailed(
        WebKitWebView*, WebKitLoadEvent, gchar* uri, GError* error, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        self->loadFailed = true;
        if (self->delegate) {
            const std::string message = error ? error->message : "Navigation failed";
            self->delegate->OnBackendNavigationCompleted(
                uri ? uri : std::string(), false, 0, Error(ErrorCode::NAVIGATION_BLOCKED, message));
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnTlsError(
        WebKitWebView*, gchar* uri, GTlsCertificate* certificate, GTlsCertificateFlags errors, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return FALSE;
        }
        const bool allow = self->delegate->OnBackendCertificateError(
            uri ? uri : std::string(), std::to_string(static_cast<int>(errors)));
        if (!allow) {
            return FALSE;
        }
        gchar* host = nullptr;
        GError* parseError = nullptr;
        GUri* parsed = uri ? g_uri_parse(uri, G_URI_FLAGS_NONE, &parseError) : nullptr;
        if (parsed) {
            const gchar* parsedHost = g_uri_get_host(parsed);
            if (parsedHost) {
                host = g_strdup(parsedHost);
            }
            g_uri_unref(parsed);
        }
        if (parseError) {
            g_error_free(parseError);
        }
        if (self->context && certificate && host) {
            webkit_web_context_allow_tls_certificate_for_host(self->context, certificate, host);
            if (self->webView) {
                webkit_web_view_reload(self->webView);
            }
        }
        g_free(host);
        return TRUE;
    }

    void LinuxWindowBackend::OnTitleChanged(GObject* object, GParamSpec*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const gchar* title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(object));
        if (self->delegate) {
            self->delegate->OnBackendDocumentTitleChanged(title ? title : std::string());
        }
    }

    void LinuxWindowBackend::OnUriChanged(GObject* object, GParamSpec*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const gchar* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(object));
        if (uri) {
            self->currentUri = uri;
            if (self->delegate) {
                self->delegate->OnBackendSourceChanged(self->currentUri, false, true);
            }
        }
    }

    gboolean LinuxWindowBackend::OnPermissionRequest(
        WebKitWebView* view, WebKitPermissionRequest* request, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate || self->suppressEvents) {
            webkit_permission_request_deny(request);
            return TRUE;
        }
        const gchar* uri = webkit_web_view_get_uri(view);
        const std::string url = uri ? uri : self->currentUri;
        PermissionKind kind = PermissionKind::OTHER;
        if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
            auto* media = WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request);
            if (webkit_user_media_permission_is_for_video_device(media)) {
                kind = PermissionKind::CAMERA;
            } else if (webkit_user_media_permission_is_for_audio_device(media)) {
                kind = PermissionKind::MICROPHONE;
            }
        } else if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
            kind = PermissionKind::GEOLOCATION;
        } else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
            kind = PermissionKind::NOTIFICATIONS;
        }
#ifdef WEBKIT_IS_CLIPBOARD_PERMISSION_REQUEST
        else if (WEBKIT_IS_CLIPBOARD_PERMISSION_REQUEST(request)) {
            kind = PermissionKind::CLIPBOARD_READ;
        }
#endif
        const PermissionDecision decision = self->delegate->OnBackendPermissionRequested(kind, ExtractOrigin(url), url);
        if (decision.allow) {
            webkit_permission_request_allow(request);
        } else {
            webkit_permission_request_deny(request);
        }
        return TRUE;
    }

    gboolean LinuxWindowBackend::OnScriptDialog(WebKitWebView*, WebKitScriptDialog* dialog, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate || self->suppressEvents) {
            return FALSE;
        }
        ScriptDialogType type = ScriptDialogType::ALERT;
        switch (webkit_script_dialog_get_dialog_type(dialog)) {
            case WEBKIT_SCRIPT_DIALOG_ALERT:
                type = ScriptDialogType::ALERT;
                break;
            case WEBKIT_SCRIPT_DIALOG_CONFIRM:
                type = ScriptDialogType::CONFIRM;
                break;
            case WEBKIT_SCRIPT_DIALOG_PROMPT:
                type = ScriptDialogType::PROMPT;
                break;
            case WEBKIT_SCRIPT_DIALOG_BEFORE_UNLOAD_CONFIRM:
                type = ScriptDialogType::BEFORE_UNLOAD;
                break;
        }
        const gchar* message = webkit_script_dialog_get_message(dialog);
        const char* defaultText = type == ScriptDialogType::PROMPT
            ? webkit_script_dialog_prompt_get_default_text(dialog)
            : "";
        const ScriptDialogDecision decision = self->delegate->OnBackendScriptDialog(
            type, message ? message : std::string(), defaultText ? defaultText : std::string());
        if (!decision.handled) {
            return FALSE;
        }
        if (type == ScriptDialogType::CONFIRM || type == ScriptDialogType::BEFORE_UNLOAD) {
            webkit_script_dialog_confirm_set_confirmed(dialog, decision.accepted ? TRUE : FALSE);
        } else if (type == ScriptDialogType::PROMPT) {
            webkit_script_dialog_prompt_set_text(dialog, decision.text.c_str());
        }
        return TRUE;
    }

    gboolean LinuxWindowBackend::OnContextMenu(
        WebKitWebView* view, WebKitContextMenu*, GdkEvent* event, WebKitHitTestResult* hitTest, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate) {
            return self->browserOptions.contextMenuEnabled ? FALSE : TRUE;
        }
        ContextMenuDetails details;
        if (event && gdk_event_get_event_type(event) == GDK_BUTTON_PRESS) {
            auto* button = reinterpret_cast<GdkEventButton*>(event);
            details.x = button->x;
            details.y = button->y;
        }
        if (hitTest) {
            details.isEditable = webkit_hit_test_result_context_is_editable(hitTest) != FALSE;
            details.isImage = webkit_hit_test_result_context_is_image(hitTest) != FALSE;
            details.isVideo = webkit_hit_test_result_context_is_media(hitTest) != FALSE;
            const gchar* link = webkit_hit_test_result_get_link_uri(hitTest);
            details.linkUrl = link ? link : std::string();
        }
        const gchar* uri = webkit_web_view_get_uri(view);
        details.sourceUrl = uri ? uri : self->currentUri;
        const bool cancel = self->delegate->OnBackendContextMenu(details);
        if (!self->browserOptions.contextMenuEnabled || cancel) {
            return TRUE;
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnAuthenticate(WebKitWebView*, WebKitAuthenticationRequest* request, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate || self->suppressEvents) {
            return FALSE;
        }
        AuthenticationInfo info;
        const gchar* host = webkit_authentication_request_get_host(request);
        const gchar* realm = webkit_authentication_request_get_realm(request);
        info.host = host ? host : std::string();
        info.realm = realm ? realm : std::string();
        info.scheme = AuthenticationSchemeName(webkit_authentication_request_get_scheme(request));
        info.port = static_cast<int>(webkit_authentication_request_get_port(request));
        info.isProxy = webkit_authentication_request_is_for_proxy(request) != FALSE;
        const AuthenticationDecision decision = self->delegate->OnBackendAuthenticationRequested(info);
        if (decision.cancel) {
            webkit_authentication_request_cancel(request);
            return TRUE;
        }
        if (!decision.useDefault) {
            WebKitCredential* credential = webkit_credential_new(
                decision.username.c_str(), decision.password.c_str(), WEBKIT_CREDENTIAL_PERSISTENCE_NONE);
            webkit_authentication_request_authenticate(request, credential);
            webkit_credential_free(credential);
            return TRUE;
        }
        return FALSE;
    }

    gboolean LinuxWindowBackend::OnEnterFullscreen(WebKitWebView*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const bool allow = self->delegate ? self->delegate->OnBackendFullscreenRequested(true) : true;
        if (!allow) {
            return FALSE;
        }
        self->html5Fullscreen = true;
        if (self->window) {
            gtk_window_fullscreen(GTK_WINDOW(self->window));
        }
        return TRUE;
    }

    gboolean LinuxWindowBackend::OnLeaveFullscreen(WebKitWebView*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const bool allow = self->delegate ? self->delegate->OnBackendFullscreenRequested(false) : true;
        if (!allow) {
            return FALSE;
        }
        self->html5Fullscreen = false;
        if (self->window) {
            gtk_window_unfullscreen(GTK_WINDOW(self->window));
        }
        return TRUE;
    }

    void LinuxWindowBackend::OnProcessTerminated(
        WebKitWebView*, WebKitWebProcessTerminationReason reason, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->delegate) {
            self->delegate->OnBackendProcessFailed(
                std::to_string(static_cast<int>(reason)), 0, reason == WEBKIT_WEB_PROCESS_CRASHED);
        }
    }

    GtkWidget* LinuxWindowBackend::OnCreateWindow(WebKitWebView*, WebKitNavigationAction*, gpointer) {
        return nullptr;
    }

    void LinuxWindowBackend::OnCloseRequested(WebKitWebView*, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (self->delegate && !self->delegate->OnBackendCloseRequested()) {
            return;
        }
        self->Close();
    }

    void LinuxWindowBackend::OnResourceLoadStarted(
        WebKitWebView*, WebKitWebResource* resource, WebKitURIRequest* request, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->browserOptions.interceptResponses || !self->delegate || !request) {
            return;
        }
        const gchar* uri = webkit_uri_request_get_uri(request);
        auto* payload = new ResourcePayload{self->alive, self, uri ? uri : std::string()};
        g_signal_connect_data(
            resource, "finished", G_CALLBACK(&LinuxWindowBackend::OnResourceFinished), payload,
            [](gpointer pointer, GClosure*) { delete static_cast<ResourcePayload*>(pointer); }, G_CONNECT_AFTER);
    }

    void LinuxWindowBackend::OnResourceFinished(WebKitWebResource* resource, gpointer data) {
        auto* payload = static_cast<ResourcePayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> alive = payload->alive.lock(); !alive || !alive->load()) {
            return;
        }
        LinuxWindowBackend* self = payload->self;
        if (!self->delegate) {
            return;
        }
        WebKitURIResponse* response = webkit_web_resource_get_response(resource);
        if (!response) {
            return;
        }
        const guint status = webkit_uri_response_get_status_code(response);
        const gchar* mime = webkit_uri_response_get_mime_type(response);
        HttpHeaderMap headers;
        SoupMessageHeaders* nativeHeaders = webkit_uri_response_get_http_headers(response);
        if (nativeHeaders) {
            soup_message_headers_foreach(nativeHeaders, &AppendResponseHeader, &headers);
        }
        self->delegate->OnBackendResourceResponse(
            payload->url, static_cast<int>(status), mime ? mime : std::string(), headers);
    }

    void LinuxWindowBackend::OnDownloadStarted(WebKitWebContext*, WebKitDownload* download, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->delegate || self->webView == nullptr || webkit_download_get_web_view(download) != self->webView) {
            return;
        }
        DownloadInfo info;
        info.id = PointerKey(download);
        WebKitURIRequest* request = webkit_download_get_request(download);
        if (request) {
            const gchar* uri = webkit_uri_request_get_uri(request);
            info.url = uri ? uri : std::string();
        }
        WebKitURIResponse* response = webkit_download_get_response(download);
        if (response) {
            const gchar* mime = webkit_uri_response_get_mime_type(response);
            const gchar* suggested = webkit_uri_response_get_suggested_filename(response);
            info.mimeType = mime ? mime : std::string();
            info.suggestedFilename = suggested ? suggested : std::string();
            const guint64 length = webkit_uri_response_get_content_length(response);
            info.totalBytes = length == 0 ? -1 : static_cast<std::int64_t>(length);
        }
        if (info.suggestedFilename.empty()) {
            info.suggestedFilename = "download";
        }
        info.state = DownloadState::PENDING;
        const DownloadStartDecision decision = self->delegate->OnBackendDownloadStarting(info);
        if (self->browserOptions.download.blocked || decision.cancel) {
            webkit_download_cancel(download);
            return;
        }
        std::filesystem::path destination = decision.path;
        if (self->browserOptions.download.askWhereToSave) {
            const std::filesystem::path initialDirectory = destination.empty() ? self->browserOptions.download.directory
                                                                               : destination.parent_path();
            const std::string suggested = destination.empty() ? info.suggestedFilename
                                                              : PathToUtf8(destination.filename());
            destination = PromptDownloadPath(GTK_WINDOW(self->window), suggested, initialDirectory);
            if (destination.empty()) {
                webkit_download_cancel(download);
                return;
            }
        }
        if (!destination.empty()) {
            webkit_download_set_destination(download, PathToUtf8(destination).c_str());
            webkit_download_set_allow_overwrite(download, TRUE);
            info.path = destination;
        }
        info.state = DownloadState::IN_PROGRESS;
        g_object_ref(download);
        self->downloads[download] = info;
        g_signal_connect(download, "received-data", G_CALLBACK(&LinuxWindowBackend::OnDownloadReceivedData), self);
        g_signal_connect(download, "finished", G_CALLBACK(&LinuxWindowBackend::OnDownloadFinished), self);
        g_signal_connect(download, "failed", G_CALLBACK(&LinuxWindowBackend::OnDownloadFailed), self);
        self->delegate->OnBackendDownloadProgress(info);
    }

    void LinuxWindowBackend::OnDownloadReceivedData(WebKitDownload* download, guint64 dataLength, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const auto iterator = self->downloads.find(download);
        if (iterator == self->downloads.end()) {
            return;
        }
        iterator->second.receivedBytes += static_cast<std::int64_t>(dataLength);
        const gchar* destination = webkit_download_get_destination(download);
        if (destination) {
            iterator->second.path = std::filesystem::path(destination);
        }
        if (self->delegate) {
            self->delegate->OnBackendDownloadProgress(iterator->second);
        }
    }

    void LinuxWindowBackend::OnDownloadFinished(WebKitDownload* download, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const auto iterator = self->downloads.find(download);
        if (iterator == self->downloads.end()) {
            return;
        }
        DownloadInfo info = iterator->second;
        info.state = DownloadState::COMPLETED;
        const gchar* destination = webkit_download_get_destination(download);
        if (destination) {
            info.path = std::filesystem::path(destination);
        }
        self->downloads.erase(iterator);
        g_signal_handlers_disconnect_by_data(download, self);
        if (self->delegate) {
            self->delegate->OnBackendDownloadCompleted(info, Error());
        }
        g_object_unref(download);
    }

    void LinuxWindowBackend::OnDownloadFailed(WebKitDownload* download, GError* error, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        const auto iterator = self->downloads.find(download);
        if (iterator == self->downloads.end()) {
            return;
        }
        DownloadInfo info = iterator->second;
        info.state = DownloadState::INTERRUPTED;
        self->downloads.erase(iterator);
        g_signal_handlers_disconnect_by_data(download, self);
        if (self->delegate) {
            self->delegate->OnBackendDownloadCompleted(
                info, Error(ErrorCode::PLATFORM_FAILURE, error ? error->message : "Download failed"));
        }
        g_object_unref(download);
    }

    void LinuxWindowBackend::OnBridgeMessage(WebKitUserContentManager*, JSCValue* value, gpointer data) {
        static_cast<LinuxWindowBackend*>(data)->HandleBridgeValue(value);
    }

    void LinuxWindowBackend::OnLifecycleMessage(WebKitUserContentManager*, JSCValue* value, gpointer data) {
        static_cast<LinuxWindowBackend*>(data)->HandleLifecycleValue(value);
    }

    void LinuxWindowBackend::HandleBridgeValue(JSCValue* value) {
        if (!delegate || suppressEvents) {
            return;
        }
        gchar* json = jsc_value_to_json(value, 0);
        const std::string message = json ? json : std::string("null");
        g_free(json);
        delegate->OnBackendWebMessage(message, CurrentUri(), true, {});
    }

    void LinuxWindowBackend::HandleLifecycleValue(JSCValue* value) {
        if (!delegate || suppressEvents) {
            return;
        }
        gchar* json = jsc_value_to_json(value, 0);
        const std::string text = json ? json : std::string("null");
        g_free(json);
        Result<JSON> parsed = ParseJsonValue(text, 65536, 8, 512);
        if (!parsed.IsOk() || !parsed.Value().is_object()) {
            return;
        }
        const JSON& message = parsed.Value();
        const auto type = message.find("type");
        if (type == message.end() || !type->is_string()) {
            return;
        }
        if (type->get<std::string>() == "dom") {
            delegate->OnBackendDomContentLoaded(true);
        } else if (type->get<std::string>() == "favicon") {
            const auto url = message.find("url");
            if (url != message.end() && url->is_string()) {
                delegate->OnBackendFaviconChanged(url->get<std::string>());
            }
        }
    }

    void LinuxWindowBackend::OnFindCounted(WebKitFindController*, guint count, gpointer data) {
        auto* self = static_cast<LinuxWindowBackend*>(data);
        if (!self->pendingFind) {
            return;
        }
        auto callback = std::move(self->pendingFind);
        self->pendingFind = {};
        FindResult result;
        result.occurrences = static_cast<int>(count);
        result.activeMatch = count > 0 ? 1 : 0;
        result.finished = true;
        callback(result);
    }

    void LinuxWindowBackend::OnSchemeRequest(WebKitURISchemeRequest* request, gpointer) {
        WebKitWebView* view = webkit_uri_scheme_request_get_web_view(request);
        LinuxWindowBackend* backend = nullptr;
        {
            std::lock_guard<std::mutex> guard(registryMutex);
            const auto iterator = webViewRegistry.find(view);
            if (iterator != webViewRegistry.end()) {
                backend = iterator->second;
            }
        }
        const gchar* scheme = webkit_uri_scheme_request_get_scheme(request);
        if (!backend) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "The web view is unavailable");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }
        backend->HandleSchemeRequest(request, scheme ? scheme : std::string());
    }

    void LinuxWindowBackend::HandleSchemeRequest(WebKitURISchemeRequest* request, const std::string& scheme) {
        const gchar* rawUri = webkit_uri_scheme_request_get_uri(request);
        const std::string uri = rawUri ? rawUri : std::string();
        for (const auto& entry : browserOptions.customSchemes) {
            if (!EqualsIgnoreCase(entry.first, scheme)) {
                continue;
            }
            CustomSchemeRequest customRequest;
            customRequest.url = uri;
            customRequest.scheme = scheme;
            const gchar* method = webkit_uri_scheme_request_get_http_method(request);
            customRequest.method = method ? method : std::string("GET");
            Result<CustomSchemeResponse> response = Error(
                ErrorCode::HANDLER_FAILED, "Custom scheme handler was not invoked");
            try {
                response = entry.second(customRequest);
            } catch (const std::exception& exception) {
                response = Error(
                    ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an exception", exception.what());
            } catch (...) {
                response = Error(ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an unknown exception");
            }
            if (!response.IsOk()) {
                GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, response.GetError().message.c_str());
                webkit_uri_scheme_request_finish_error(request, error);
                g_error_free(error);
                return;
            }
            const CustomSchemeResponse& value = response.Value();
            GBytes* bytes = g_bytes_new(value.body.data(), value.body.size());
            GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
            WebKitURISchemeResponse* nativeResponse = webkit_uri_scheme_response_new(
                stream, static_cast<gint64>(value.body.size()));
            webkit_uri_scheme_response_set_status(
                nativeResponse, static_cast<guint>(value.statusCode), StatusReason(value.statusCode).c_str());
            webkit_uri_scheme_response_set_content_type(nativeResponse, value.mimeType.c_str());
            if (!value.headers.empty()) {
                SoupMessageHeaders* headers = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
                for (const auto& header : value.headers) {
                    soup_message_headers_append(headers, header.first.c_str(), header.second.c_str());
                }
                webkit_uri_scheme_response_set_http_headers(nativeResponse, headers);
                soup_message_headers_unref(headers);
            }
            webkit_uri_scheme_request_finish_with_response(request, nativeResponse);
            g_object_unref(nativeResponse);
            g_object_unref(stream);
            g_bytes_unref(bytes);
            return;
        }

        if (!browserOptions.virtualHost.has_value() || !EqualsIgnoreCase(browserOptions.virtualHost->scheme, scheme)) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown custom scheme");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }

        GError* parseError = nullptr;
        GUri* parsed = g_uri_parse(uri.c_str(), G_URI_FLAGS_NONE, &parseError);
        const gchar* host = parsed ? g_uri_get_host(parsed) : nullptr;
        const bool hostMatches = host && EqualsIgnoreCase(host, browserOptions.virtualHost->host);
        if (parsed) {
            g_uri_unref(parsed);
        }
        if (parseError) {
            g_error_free(parseError);
        }
        if (!hostMatches) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Virtual host does not match");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }

        std::string path = webkit_uri_scheme_request_get_path(request) ? webkit_uri_scheme_request_get_path(request)
                                                                       : "/";
        if (path.empty() || path.back() == '/') {
            path += "index.html";
        }
        std::filesystem::path resolved;
        if (!ResolveWithinDirectory(browserOptions.virtualHost->folder, path, resolved)) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }
        std::ifstream input(resolved, std::ios::binary);
        if (!input) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }
        std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        GBytes* bytes = g_bytes_new(data.data(), data.size());
        GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
        webkit_uri_scheme_request_finish(
            request, stream, static_cast<gint64>(data.size()), MimeTypeForPath(resolved).c_str());
        g_object_unref(stream);
        g_bytes_unref(bytes);
    }

    std::unique_ptr<WindowBackend> CreatePlatformWindowBackend() {
        return std::make_unique<LinuxWindowBackend>();
    }
}
