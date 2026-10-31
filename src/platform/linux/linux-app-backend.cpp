#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "backend/backend.h"
#include "platform/linux/linux-backend.h"

namespace Flare {
    namespace {
        LinuxAppSettings appSettings;

        struct DispatchTask {
            std::function<void()> function;
        };

        gboolean RunDispatchTask(gpointer data) {
            auto* task = static_cast<DispatchTask*>(data);
            try {
                task->function();
            } catch (const std::exception&) {
            } catch (...) {
            }
            delete task;
            return G_SOURCE_REMOVE;
        }

        gboolean QuitMainLoop(gpointer) {
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }

    const LinuxAppSettings& CurrentLinuxAppSettings() {
        return appSettings;
    }

    LinuxAppSettings& MutableLinuxAppSettings() {
        return appSettings;
    }

    class LinuxAppBackend final : public AppBackend {
    public:
        ~LinuxAppBackend() override {
            if (gtk_main_level() > 0) {
                gtk_main_quit();
            }
        }

        Result<void> Initialize(const AppOptions& options) override {
            int argc = 0;
            char** argv = nullptr;
            if (!gtk_init_check(&argc, &argv)) {
                return Error(ErrorCode::PLATFORM_FAILURE, "Unable to initialize GTK; a display server is required");
            }
            uiThread = std::this_thread::get_id();
            appSettings.theme = options.theme;
            appSettings.hardwareAcceleration = options.hardwareAcceleration;
            if (options.theme != Theme::SYSTEM) {
                GtkSettings* settings = gtk_settings_get_default();
                if (settings) {
                    g_object_set(settings, "gtk-application-prefer-dark-theme", options.theme == Theme::DARK, nullptr);
                }
            }

            capabilities.platformName = "linux";
            capabilities.multiWindow = true;
            capabilities.nativeKeyEvents = true;
            capabilities.nativeMouseEvents = true;
            capabilities.nativeWheelEvents = true;
            capabilities.fileDrop = true;
            capabilities.transparentWindows = true;
            capabilities.windowOpacity = true;
            capabilities.roundedCorners = false;
            capabilities.borderlessShadow = false;
            capabilities.perWindowIcon = true;
            capabilities.taskbarVisibility = true;
            capabilities.contentProtection = false;
            capabilities.customTitleBar = true;
            capabilities.draggableRegions = true;
            capabilities.virtualHostHttps = false;
            capabilities.virtualHostScheme = true;
            capabilities.customSchemes = true;
            capabilities.requestInterception = false;
            capabilities.responseInterception = true;
            capabilities.headerInjection = false;
            capabilities.proxy = true;
            capabilities.browserArguments = false;
            capabilities.downloads = true;
            capabilities.printToPdf = false;
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
            capabilities.iframeBridge = false;
            capabilities.devTools = true;
            capabilities.devToolsControl = true;
            capabilities.browserAcceleratorKeysToggle = false;
            capabilities.autofillToggle = false;
            capabilities.swipeNavigation = true;
            capabilities.pinchZoom = false;
            capabilities.statusBar = false;
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
            capabilities.notes.emplace_back("Iframe bridging and closing DevTools are limited on WebKitGTK");
            capabilities.notes.emplace_back(
                "Rounded window corners and borderless shadows follow the system theme on Linux");
            return Result<void>::Ok();
        }

        Result<void> Run() override {
            if (!IsOnUiThread()) {
                return Error(ErrorCode::WRONG_THREAD, "App::Run must be called on the UI thread");
            }
            stopRequested.store(false);
            gtk_main();
            return Result<void>::Ok();
        }

        void Stop() override {
            stopRequested.store(true);
            g_main_context_invoke(nullptr, &QuitMainLoop, nullptr);
        }

        void Dispatch(std::function<void()> task) override {
            if (!task) {
                return;
            }
            auto* payload = new DispatchTask{std::move(task)};
            g_main_context_invoke(nullptr, &RunDispatchTask, payload);
        }

        bool IsOnUiThread() const override { return std::this_thread::get_id() == uiThread; }

        const PlatformCapabilities& Capabilities() const override { return capabilities; }

    private:
        std::thread::id uiThread;
        std::atomic<bool> stopRequested{false};
        PlatformCapabilities capabilities;
    };

    std::unique_ptr<AppBackend> CreatePlatformAppBackend() {
        return std::make_unique<LinuxAppBackend>();
    }
}
