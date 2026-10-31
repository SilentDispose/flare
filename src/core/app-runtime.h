#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "flare/json.h"

#include "backend/backend.h"
#include "core/single-instance.h"
#include "flare/app.h"
#include "flare/event.h"

namespace Flare {
    class App;
    class Window;
    class WindowGroup;

    struct AppRuntime : public std::enable_shared_from_this<AppRuntime> {
        AppOptions options;
        std::filesystem::path stateFolder;
        PlatformCapabilities capabilities;
        App* ownerApp = nullptr;

        std::unique_ptr<AppBackend> backend;
        std::unique_ptr<SingleInstanceGuard> instanceGuard;

        std::atomic<bool> running{false};
        std::atomic<bool> quitRequested{false};
        std::atomic<bool> quitWhenWindowsClose{false};
        std::atomic<bool> shuttingDown{false};
        std::atomic<bool> readyRaised{false};
        WindowId nextWindowId = 1;

        mutable std::mutex windowsMutex;
        std::unordered_map<WindowId, std::shared_ptr<Window>> windows;

        mutable std::mutex groupsMutex;
        std::unordered_map<std::string, std::shared_ptr<WindowGroup>> groups;

        mutable std::mutex handlersMutex;
        std::unordered_map<std::string, HandlerRegistration> handlers;

        Event<> readyEvent;
        Event<> quitEvent;
        Event<> allWindowsClosedEvent;
        Event<const std::vector<std::string>&> secondInstanceEvent;
        Event<const Error&> unhandledErrorEvent;

        std::mutex pendingSecondInstanceMutex;
        std::vector<std::vector<std::string>> pendingSecondInstance;

        Result<void> Initialize(App& app);
        Result<void> Run();
        Result<void> Quit();
        void Shutdown();
        void Dispatch(std::function<void()> task);
        Result<void> DispatchSync(std::function<void()> task);
        [[nodiscard]] bool IsOnUiThread() const;

        Result<std::shared_ptr<Window>> NewWindow(const WindowOptions& windowOptions,
            const BrowserOptions& browserOptions, const BridgeOptions& bridgeOptions);
        Result<void> Emit(std::string_view eventName, const JSON& payload);
        Result<void> Handle(std::string name, RpcHandler handler, HandlerOptions options);
        bool Unhandle(std::string_view name);
        std::optional<HandlerRegistration> FindHandler(std::string_view name) const;

        Result<std::shared_ptr<WindowGroup>> CreateGroup(std::string name);
        Result<std::shared_ptr<WindowGroup>> FindGroup(std::string_view name) const;
        Result<void> JoinGroup(Window& window, std::string_view groupName);
        void LeaveGroup(Window& window, std::string_view groupName);
        void RemoveFromAllGroups(Window& window);

        [[nodiscard]] std::vector<std::shared_ptr<Window>> Windows() const;
        Result<std::shared_ptr<Window>> FindWindow(WindowId id) const;
        Result<std::shared_ptr<Window>> FindWindowByPointer(const Window* window) const;

        void Enqueue(std::function<void()> task);
        void FinalizeWindow(WindowId id);
        void ReportError(const Error& error);
        void ReceiveSecondInstance(std::vector<std::string> arguments);
        void DeliverPendingSecondInstance();

    private:
        Result<void> QuitInternal();
    };
}
