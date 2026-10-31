#pragma once

#include <cstddef>
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
#include "flare/icon.h"
#include "flare/window-options.h"

namespace Flare {
    class Window;
    class WindowGroup;

    struct AppOptions {
        std::string applicationName = "Flare Application";
        std::string applicationId = "com.example.flare";
        std::string applicationVersion = "1.0.0";
        Icon icon;
        std::filesystem::path assetsFolder;
        std::filesystem::path stateFolder;
        bool singleInstance = false;
        std::string singleInstanceId;
        DpiAwareness dpiAwareness = DpiAwareness::PER_MONITOR_V2;
        Theme theme = Theme::SYSTEM;
        bool quitOnAllWindowsClosed = true;
        bool hardwareAcceleration = true;
        std::vector<std::string> launchArguments;
    };

    class FLARE_API App {
    public:
        ~App();
        App(const App&) = delete;
        App(App&&) = delete;
        App& operator=(const App&) = delete;
        App& operator=(App&&) = delete;

        static Result<std::unique_ptr<App>> Create(AppOptions options = {});

        Result<std::shared_ptr<Window>> NewWindow(
            const WindowOptions& windowOptions, const BrowserOptions& browserOptions);
        Result<std::shared_ptr<Window>> NewWindow(const WindowOptions& windowOptions,
            const BrowserOptions& browserOptions, const BridgeOptions& bridgeOptions);

        Result<void> Run();
        Result<void> Quit();
        void Dispatch(std::function<void()> task);
        Result<void> DispatchSync(std::function<void()> task);
        [[nodiscard]] bool IsOnUiThread() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

        Result<void> Emit(std::string_view eventName, const JSON& payload = JSON::object());
        Result<void> Handle(std::string name, RpcHandler handler, HandlerOptions options = {});
        bool Unhandle(std::string_view name);

        Result<std::shared_ptr<WindowGroup>> CreateWindowGroup(std::string name);
        Result<std::shared_ptr<WindowGroup>> FindWindowGroup(std::string_view name) const;

        [[nodiscard]] std::vector<std::shared_ptr<Window>> Windows() const;
        [[nodiscard]] Result<std::shared_ptr<Window>> FindWindow(WindowId id) const;

        [[nodiscard]] const AppOptions& Options() const noexcept;
        [[nodiscard]] const PlatformCapabilities& Capabilities() const noexcept;
        [[nodiscard]] std::filesystem::path StateFolder() const;

        Subscription OnReady(std::function<void()> handler);
        Subscription OnQuit(std::function<void()> handler);
        Subscription OnAllWindowsClosed(std::function<void()> handler);
        Subscription OnSecondInstance(std::function<void(const std::vector<std::string>&)> handler);
        Subscription OnUnhandledError(std::function<void(const Error&)> handler);

    private:
        struct Impl;

        explicit App(std::unique_ptr<Impl> appImpl);

        friend class Window;
        friend class WindowGroup;

        std::unique_ptr<Impl> impl;
    };

    [[nodiscard]] FLARE_API std::string FlareVersionString();
}
