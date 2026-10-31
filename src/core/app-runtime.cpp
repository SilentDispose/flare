#include "core/app-runtime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

#include "core/icon-utils.h"
#include "core/platform-paths.h"
#include "core/text-utils.h"
#include "core/window-state.h"
#include "flare/embedded-web.h"
#include "flare/window-group.h"
#include "flare/window.h"

namespace Flare {
    namespace {
        void EmitGuarded(Event<>& event, AppRuntime& runtime) {
            try {
                event.Emit();
            } catch (const std::exception& exception) {
                runtime.ReportError(Error(
                    ErrorCode::HANDLER_FAILED, "An application event handler threw an exception", exception.what()));
            } catch (...) {
                runtime.ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "An application event handler threw an unknown exception"));
            }
        }
    }

    Result<void> AppRuntime::Initialize(App& app) {
        ownerApp = &app;
        if (options.launchArguments.empty()) {
            options.launchArguments = CurrentProcessArguments();
        }
        if (options.stateFolder.empty()) {
            stateFolder = DefaultStateFolder(options.applicationId);
        } else {
            stateFolder = options.stateFolder;
        }
        std::error_code error;
        const std::filesystem::path absoluteState = std::filesystem::absolute(stateFolder, error);
        if (!error) {
            stateFolder = absoluteState;
        }
        options.stateFolder = stateFolder;
        if (!options.assetsFolder.empty()) {
            const std::filesystem::path absoluteAssets = std::filesystem::absolute(options.assetsFolder, error);
            if (!error) {
                options.assetsFolder = absoluteAssets;
            }
        }
        if (!options.icon.IsEmpty()) {
            Result<Icon> resolvedIcon = ResolveIcon(options.icon, options.assetsFolder);
            if (!resolvedIcon.IsOk()) {
                return resolvedIcon.GetError();
            }
            options.icon = resolvedIcon.Value();
        }

        if (options.singleInstance) {
            const std::string identifier = options.singleInstanceId.empty() ? options.applicationId
                                                                            : options.singleInstanceId;
            std::weak_ptr<AppRuntime> weak = weak_from_this();
            Result<std::unique_ptr<SingleInstanceGuard>> guard = SingleInstanceGuard::Create(
                identifier, options.launchArguments, [weak](std::vector<std::string> arguments) {
                    if (std::shared_ptr<AppRuntime> self = weak.lock()) {
                        self->ReceiveSecondInstance(std::move(arguments));
                    }
                });
            if (!guard.IsOk()) {
                return guard.GetError();
            }
            instanceGuard = std::move(guard.Value());
        }

        std::filesystem::create_directories(stateFolder, error);
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to create the application state folder", error.message());
        }

        backend = CreatePlatformAppBackend();
        if (!backend) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the platform application backend");
        }
        Result<void> initialized = backend->Initialize(options);
        if (!initialized.IsOk()) {
            backend.reset();
            instanceGuard.reset();
            return initialized.GetError();
        }
        capabilities = backend->Capabilities();
        return Result<void>::Ok();
    }

    Result<void> AppRuntime::Run() {
        if (!IsOnUiThread()) {
            return Error(ErrorCode::WRONG_THREAD, "App::Run must be called on the UI thread");
        }
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true)) {
            return Error(ErrorCode::ALREADY_EXISTS, "The application event loop is already running");
        }

        readyRaised.store(true);
        EmitGuarded(readyEvent, *this);
        DeliverPendingSecondInstance();

        if (quitRequested.load() && Windows().empty()) {
            running.store(false);
            EmitGuarded(quitEvent, *this);
            return Result<void>::Ok();
        }

        if (!backend) {
            running.store(false);
            return Error(ErrorCode::PLATFORM_FAILURE, "The platform application backend is unavailable");
        }

        Result<void> loopResult = backend->Run();
        running.store(false);
        EmitGuarded(quitEvent, *this);
        return loopResult;
    }

    Result<void> AppRuntime::Quit() {
        if (shuttingDown.load()) {
            return Error(ErrorCode::WINDOW_CLOSED, "The application is shutting down");
        }
        if (!IsOnUiThread()) {
            if (!running.load()) {
                if (!Windows().empty()) {
                    return Error(ErrorCode::WRONG_THREAD,
                        "App::Quit with open windows must be called on the UI thread before the event loop starts");
                }
                quitRequested.store(true);
                return Result<void>::Ok();
            }
            std::shared_ptr<AppRuntime> self = shared_from_this();
            Dispatch([self]() { self->QuitInternal(); });
            return Result<void>::Ok();
        }
        return QuitInternal();
    }

    Result<void> AppRuntime::QuitInternal() {
        if (shuttingDown.load()) {
            return Result<void>::Ok();
        }
        quitWhenWindowsClose.store(true);
        const std::vector<std::shared_ptr<Window>> snapshot = Windows();
        if (snapshot.empty()) {
            quitRequested.store(true);
            if (running.load() && backend) {
                backend->Stop();
            }
            return Result<void>::Ok();
        }
        for (const std::shared_ptr<Window>& window : snapshot) {
            Result<void> closed = window->Close();
            if (!closed.IsOk()) {
                quitWhenWindowsClose.store(false);
                return closed.GetError();
            }
        }
        if (Windows().empty()) {
            quitRequested.store(true);
            if (running.load() && backend) {
                backend->Stop();
            }
        }
        return Result<void>::Ok();
    }

    void AppRuntime::Shutdown() {
        if (shuttingDown.exchange(true)) {
            return;
        }
        quitRequested.store(true);
        instanceGuard.reset();

        std::vector<std::shared_ptr<Window>> openWindows;
        {
            std::lock_guard<std::mutex> guard(windowsMutex);
            openWindows.reserve(windows.size());
            for (auto& entry : windows) {
                openWindows.push_back(std::move(entry.second));
            }
            windows.clear();
        }
        {
            std::lock_guard<std::mutex> guard(groupsMutex);
            groups.clear();
        }
        for (const std::shared_ptr<Window>& window : openWindows) {
            window->ShutdownInternal();
        }
        openWindows.clear();

        if (running.load() && backend) {
            backend->Stop();
        }
        backend.reset();

        {
            std::lock_guard<std::mutex> guard(handlersMutex);
            handlers.clear();
        }
    }

    void AppRuntime::Dispatch(std::function<void()> task) {
        if (!task || shuttingDown.load() || !backend) {
            return;
        }
        backend->Dispatch(std::move(task));
    }

    Result<void> AppRuntime::DispatchSync(std::function<void()> task) {
        if (!task) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Dispatch task is empty");
        }
        if (IsOnUiThread()) {
            try {
                task();
            } catch (const std::exception& exception) {
                ReportError(Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an exception", exception.what()));
            } catch (...) {
                ReportError(Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an unknown exception"));
            }
            return Result<void>::Ok();
        }
        if (!running.load() || !backend) {
            return Error(
                ErrorCode::WRONG_THREAD, "DispatchSync requires a running event loop when called from another thread");
        }
        auto completion = std::make_shared<std::promise<void>>();
        std::future<void> future = completion->get_future();
        std::shared_ptr<AppRuntime> self = shared_from_this();
        Dispatch([self, task = std::move(task), completion]() mutable {
            try {
                task();
            } catch (const std::exception& exception) {
                self->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an exception", exception.what()));
            } catch (...) {
                self->ReportError(Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an unknown exception"));
            }
            completion->set_value();
        });
        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
            return Error(ErrorCode::TIMEOUT, "DispatchSync timed out waiting for the UI thread");
        }
        return Result<void>::Ok();
    }

    bool AppRuntime::IsOnUiThread() const {
        return backend && backend->IsOnUiThread();
    }

    Result<std::shared_ptr<Window>> AppRuntime::NewWindow(
        const WindowOptions& windowOptions, const BrowserOptions& browserOptions, const BridgeOptions& bridgeOptions) {
        if (!IsOnUiThread()) {
            return Error(ErrorCode::WRONG_THREAD, "App::NewWindow must be called on the UI thread");
        }
        if (shuttingDown.load() || quitRequested.load()) {
            return Error(ErrorCode::WINDOW_CLOSED, "The application is shutting down");
        }
        Result<void> validation = ValidateWindowOptions(windowOptions);
        if (!validation.IsOk()) {
            return validation.GetError();
        }
        BrowserOptions browser = browserOptions;
        if (browser.embeddedBundle.empty() && browser.startUrl.empty() && browser.html.empty()
            && browser.localFile.empty() && !browser.virtualHost.has_value()) {
            const std::vector<std::string> bundles = EmbeddedWebBundleNames();
            if (bundles.size() == 1) {
                browser.embeddedBundle = bundles.front();
            } else if (bundles.size() > 1) {
                return Error(ErrorCode::INVALID_ARGUMENT,
                    "Multiple embedded web bundles are registered; set BrowserOptions::embeddedBundle explicitly");
            }
        }
        if (!browser.embeddedBundle.empty()) {
            if (!browser.startUrl.empty() || !browser.html.empty() || !browser.localFile.empty()
                || browser.virtualHost.has_value()) {
                return Error(ErrorCode::INVALID_ARGUMENT,
                    "embeddedBundle cannot be combined with startUrl, html, localFile, or virtualHost");
            }
            const std::string startPage = browser.embeddedStartPage.empty() ? "index.html" : browser.embeddedStartPage;
            const bool useHttps = capabilities.platformName == "windows";
            if (!useHttps) {
                const std::string scheme = "flare-embedded";
                if (browser.customSchemes.find(scheme) != browser.customSchemes.end()) {
                    return Error(ErrorCode::INVALID_ARGUMENT,
                        "The flare-embedded scheme is reserved for embedded web resources");
                }
                const std::string bundleName = browser.embeddedBundle;
                browser.customSchemes.emplace(scheme, [bundleName](const CustomSchemeRequest& request) {
                    return LoadEmbeddedWebResource(bundleName, request.url);
                });
            }
            browser.startUrl = BuildEmbeddedWebUrl(browser.embeddedBundle, startPage, useHttps);
        }
        validation = ValidateBrowserOptions(browser);
        if (!validation.IsOk()) {
            return validation.GetError();
        }
        validation = ValidateBridgeOptions(bridgeOptions);
        if (!validation.IsOk()) {
            return validation.GetError();
        }
        if (bridgeOptions.enabled && bridgeOptions.allowIframes && !capabilities.iframeBridge) {
            return Error(ErrorCode::NOT_SUPPORTED, "Iframe bridging is not supported by this platform backend");
        }

        WindowOptions resolved = windowOptions;
        if (resolved.icon.IsEmpty() && capabilities.perWindowIcon) {
            resolved.icon = options.icon;
        }
        Result<Icon> resolvedIcon = ResolveIcon(resolved.icon, options.assetsFolder);
        if (!resolvedIcon.IsOk()) {
            return resolvedIcon.GetError();
        }
        resolved.icon = resolvedIcon.Value();
        if (resolved.parent) {
            Result<std::shared_ptr<Window>> parent = FindWindowByPointer(resolved.parent);
            if (!parent.IsOk()) {
                return Error(ErrorCode::INVALID_ARGUMENT, "The parent window is not managed by this application");
            }
            if (parent.Value()->IsClosed()) {
                return Error(ErrorCode::INVALID_ARGUMENT, "The parent window is closed");
            }
            resolved.parent = parent.Value().get();
        }

        std::string stateKey = resolved.stateKey.empty() ? resolved.title : resolved.stateKey;
        resolved.stateKey = stateKey;
        if (resolved.persistState) {
            Result<SavedWindowState> saved = LoadWindowState(stateFolder, stateKey);
            if (saved.IsOk()) {
                const SavedWindowState& state = saved.Value();
                if (state.valid) {
                    resolved.width = std::max(state.width, resolved.minWidth > 0 ? resolved.minWidth : 1);
                    resolved.height = std::max(state.height, resolved.minHeight > 0 ? resolved.minHeight : 1);
                    if (resolved.maxWidth > 0) {
                        resolved.width = std::min(resolved.width, resolved.maxWidth);
                    }
                    if (resolved.maxHeight > 0) {
                        resolved.height = std::min(resolved.height, resolved.maxHeight);
                    }
                    resolved.x = state.x;
                    resolved.y = state.y;
                    resolved.hasPosition = true;
                    resolved.centered = false;
                    if (state.maximized) {
                        resolved.maximizeOnStart = true;
                    }
                }
            } else {
                ReportError(saved.GetError());
            }
        }

        if (!browser.incognito) {
            std::filesystem::path folder = browser.userDataFolder;
            if (folder.empty()) {
                folder = stateFolder / "browser-data";
                browser.userDataFolder = folder;
            } else if (folder.is_relative()) {
                folder = stateFolder / folder;
                browser.userDataFolder = folder;
            }
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (error) {
                return Error(ErrorCode::IO_FAILURE, "Unable to create the browser data folder", error.message());
            }
        } else if (!browser.userDataFolder.empty() && browser.userDataFolder.is_relative()) {
            browser.userDataFolder = stateFolder / browser.userDataFolder;
        }
        if (!browser.download.directory.empty()) {
            std::error_code error;
            if (browser.download.directory.is_relative()) {
                browser.download.directory = std::filesystem::absolute(browser.download.directory, error);
            }
        }
        if (browser.virtualHost.has_value()) {
            std::error_code error;
            if (browser.virtualHost->folder.is_relative()) {
                browser.virtualHost->folder = std::filesystem::absolute(browser.virtualHost->folder, error);
            }
        }
        if (!browser.localFile.empty()) {
            std::error_code error;
            if (browser.localFile.is_relative()) {
                browser.localFile = std::filesystem::absolute(browser.localFile, error);
            }
        }

        WindowId id = 0;
        {
            std::lock_guard<std::mutex> guard(windowsMutex);
            id = nextWindowId++;
        }
        Result<std::shared_ptr<Window>> created = Window::CreateInternal(
            shared_from_this(), *ownerApp, id, resolved, browser, bridgeOptions);
        if (!created.IsOk()) {
            return created.GetError();
        }
        std::shared_ptr<Window> window = std::move(created.Value());
        {
            std::lock_guard<std::mutex> guard(windowsMutex);
            windows[id] = window;
        }
        return window;
    }

    Result<void> AppRuntime::Emit(std::string_view eventName, const JSON& payload) {
        if (shuttingDown.load()) {
            return Error(ErrorCode::WINDOW_CLOSED, "The application is shutting down");
        }
        if (!IsOnUiThread()) {
            std::shared_ptr<AppRuntime> self = shared_from_this();
            const std::string name(eventName);
            JSON copy = payload;
            Dispatch([self, name, payload = std::move(copy)]() mutable { self->Emit(name, payload); });
            return Result<void>::Ok();
        }
        Result<void> outcome = Result<void>::Ok();
        for (const std::shared_ptr<Window>& window : Windows()) {
            Result<void> result = window->Emit(eventName, payload);
            if (!result.IsOk() && outcome.IsOk()) {
                outcome = result;
            }
        }
        return outcome;
    }

    Result<void> AppRuntime::Handle(std::string name, RpcHandler handler, HandlerOptions handlerOptions) {
        if (!IsValidName(name)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Handler name is invalid");
        }
        if (!handler) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Handler function is empty");
        }
        std::lock_guard<std::mutex> guard(handlersMutex);
        if (handlers.find(name) != handlers.end()) {
            return Error(ErrorCode::ALREADY_EXISTS, "A handler with this name is already registered");
        }
        const std::string key = name;
        handlers.emplace(key, HandlerRegistration{std::move(name), std::move(handler), std::move(handlerOptions)});
        return Result<void>::Ok();
    }

    bool AppRuntime::Unhandle(std::string_view name) {
        std::lock_guard<std::mutex> guard(handlersMutex);
        return handlers.erase(std::string(name)) > 0;
    }

    std::optional<HandlerRegistration> AppRuntime::FindHandler(std::string_view name) const {
        std::lock_guard<std::mutex> guard(handlersMutex);
        const auto iterator = handlers.find(std::string(name));
        if (iterator == handlers.end()) {
            return std::nullopt;
        }
        return iterator->second;
    }

    Result<std::shared_ptr<WindowGroup>> AppRuntime::CreateGroup(std::string name) {
        if (!IsValidName(name)) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window group name is invalid");
        }
        std::lock_guard<std::mutex> guard(groupsMutex);
        const auto iterator = groups.find(name);
        if (iterator != groups.end()) {
            return iterator->second;
        }
        std::shared_ptr<WindowGroup> group(new WindowGroup(std::move(name)));
        groups[group->Name()] = group;
        return group;
    }

    Result<std::shared_ptr<WindowGroup>> AppRuntime::FindGroup(std::string_view name) const {
        std::lock_guard<std::mutex> guard(groupsMutex);
        const auto iterator = groups.find(std::string(name));
        if (iterator == groups.end()) {
            return Error(ErrorCode::NOT_FOUND, "Window group was not found");
        }
        return iterator->second;
    }

    Result<void> AppRuntime::JoinGroup(Window& window, std::string_view groupName) {
        Result<std::shared_ptr<WindowGroup>> group = CreateGroup(std::string(groupName));
        if (!group.IsOk()) {
            return group.GetError();
        }
        Result<std::shared_ptr<Window>> shared = FindWindow(window.GetId());
        if (!shared.IsOk()) {
            return shared.GetError();
        }
        return group.Value()->Add(shared.Value());
    }

    void AppRuntime::LeaveGroup(Window& window, std::string_view groupName) {
        Result<std::shared_ptr<WindowGroup>> group = FindGroup(groupName);
        if (group.IsOk()) {
            group.Value()->Remove(&window);
        }
    }

    void AppRuntime::RemoveFromAllGroups(Window& window) {
        std::vector<std::shared_ptr<WindowGroup>> snapshot;
        {
            std::lock_guard<std::mutex> guard(groupsMutex);
            snapshot.reserve(groups.size());
            for (const auto& entry : groups) {
                snapshot.push_back(entry.second);
            }
        }
        for (const std::shared_ptr<WindowGroup>& group : snapshot) {
            group->Remove(&window);
        }
    }

    std::vector<std::shared_ptr<Window>> AppRuntime::Windows() const {
        std::lock_guard<std::mutex> guard(windowsMutex);
        std::vector<std::shared_ptr<Window>> result;
        result.reserve(windows.size());
        for (const auto& entry : windows) {
            result.push_back(entry.second);
        }
        return result;
    }

    Result<std::shared_ptr<Window>> AppRuntime::FindWindow(WindowId id) const {
        std::lock_guard<std::mutex> guard(windowsMutex);
        const auto iterator = windows.find(id);
        if (iterator == windows.end()) {
            return Error(ErrorCode::NOT_FOUND, "Window was not found");
        }
        return iterator->second;
    }

    Result<std::shared_ptr<Window>> AppRuntime::FindWindowByPointer(const Window* window) const {
        std::lock_guard<std::mutex> guard(windowsMutex);
        for (const auto& entry : windows) {
            if (entry.second.get() == window) {
                return entry.second;
            }
        }
        return Error(ErrorCode::NOT_FOUND, "Window was not found");
    }

    void AppRuntime::Enqueue(std::function<void()> task) {
        if (!task || shuttingDown.load() || !backend) {
            return;
        }
        backend->Dispatch(std::move(task));
    }

    void AppRuntime::FinalizeWindow(WindowId id) {
        std::shared_ptr<Window> window;
        {
            std::lock_guard<std::mutex> guard(windowsMutex);
            const auto iterator = windows.find(id);
            if (iterator == windows.end()) {
                return;
            }
            window = iterator->second;
            windows.erase(iterator);
        }
        RemoveFromAllGroups(*window);
        window->FinalizeClosed();

        bool empty = false;
        {
            std::lock_guard<std::mutex> guard(windowsMutex);
            empty = windows.empty();
        }
        if (empty) {
            try {
                allWindowsClosedEvent.Emit();
            } catch (const std::exception& exception) {
                ReportError(Error(
                    ErrorCode::HANDLER_FAILED, "An all-windows-closed handler threw an exception", exception.what()));
            } catch (...) {
                ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "An all-windows-closed handler threw an unknown exception"));
            }
            {
                std::lock_guard<std::mutex> guard(windowsMutex);
                empty = windows.empty();
            }
            if (empty && (quitWhenWindowsClose.load() || options.quitOnAllWindowsClosed)) {
                quitRequested.store(true);
                if (running.load() && backend) {
                    backend->Stop();
                }
            }
        }
    }

    void AppRuntime::ReportError(const Error& error) {
        if (error.code == ErrorCode::NONE) {
            return;
        }
        try {
            if (unhandledErrorEvent.HasSubscribers()) {
                unhandledErrorEvent.Emit(error);
                return;
            }
        } catch (...) {
            return;
        }
        std::cerr << "Flare: " << error.ToString() << '\n';
    }

    void AppRuntime::ReceiveSecondInstance(std::vector<std::string> arguments) {
        {
            std::lock_guard<std::mutex> guard(pendingSecondInstanceMutex);
            pendingSecondInstance.push_back(std::move(arguments));
        }
        std::shared_ptr<AppRuntime> self = shared_from_this();
        Enqueue([self]() { self->DeliverPendingSecondInstance(); });
    }

    void AppRuntime::DeliverPendingSecondInstance() {
        std::vector<std::vector<std::string>> pending;
        {
            std::lock_guard<std::mutex> guard(pendingSecondInstanceMutex);
            if (!secondInstanceEvent.HasSubscribers()) {
                return;
            }
            pending.swap(pendingSecondInstance);
        }
        for (const std::vector<std::string>& arguments : pending) {
            try {
                secondInstanceEvent.Emit(arguments);
            } catch (const std::exception& exception) {
                ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A second-instance handler threw an exception", exception.what()));
            } catch (...) {
                ReportError(Error(ErrorCode::HANDLER_FAILED, "A second-instance handler threw an unknown exception"));
            }
        }
    }
}
