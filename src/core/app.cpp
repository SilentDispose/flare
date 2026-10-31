#include "flare/app.h"

#include <exception>
#include <utility>

#include "core/app-runtime.h"
#include "flare/version.h"
#include "flare/window-group.h"

namespace Flare {
    struct App::Impl {
        std::shared_ptr<AppRuntime> runtime;
    };

    Result<std::unique_ptr<App>> App::Create(AppOptions options) {
        auto runtime = std::make_shared<AppRuntime>();
        runtime->options = std::move(options);
        auto application = std::unique_ptr<App>(new App(std::make_unique<Impl>()));
        application->impl->runtime = runtime;
        Result<void> initialized = runtime->Initialize(*application);
        if (!initialized.IsOk()) {
            return initialized.GetError();
        }
        return application;
    }

    App::App(std::unique_ptr<Impl> appImpl) : impl(std::move(appImpl)) {}

    App::~App() {
        if (impl && impl->runtime) {
            impl->runtime->Shutdown();
        }
    }

    Result<std::shared_ptr<Window>> App::NewWindow(
        const WindowOptions& windowOptions, const BrowserOptions& browserOptions) {
        return NewWindow(windowOptions, browserOptions, BridgeOptions{});
    }

    Result<std::shared_ptr<Window>> App::NewWindow(
        const WindowOptions& windowOptions, const BrowserOptions& browserOptions, const BridgeOptions& bridgeOptions) {
        return impl->runtime->NewWindow(windowOptions, browserOptions, bridgeOptions);
    }

    Result<void> App::Run() {
        return impl->runtime->Run();
    }

    Result<void> App::Quit() {
        return impl->runtime->Quit();
    }

    void App::Dispatch(std::function<void()> task) {
        if (!task) {
            return;
        }
        std::shared_ptr<AppRuntime> runtime = impl->runtime;
        impl->runtime->Dispatch([runtime, task = std::move(task)]() mutable {
            try {
                task();
            } catch (const std::exception& exception) {
                runtime->ReportError(
                    Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an exception", exception.what()));
            } catch (...) {
                runtime->ReportError(Error(ErrorCode::HANDLER_FAILED, "A dispatched task threw an unknown exception"));
            }
        });
    }

    Result<void> App::DispatchSync(std::function<void()> task) {
        return impl->runtime->DispatchSync(std::move(task));
    }

    bool App::IsOnUiThread() const noexcept {
        return impl->runtime->IsOnUiThread();
    }

    bool App::IsRunning() const noexcept {
        return impl->runtime->running.load();
    }

    Result<void> App::Emit(std::string_view eventName, const JSON& payload) {
        return impl->runtime->Emit(eventName, payload);
    }

    Result<void> App::Handle(std::string name, RpcHandler handler, HandlerOptions options) {
        return impl->runtime->Handle(std::move(name), std::move(handler), std::move(options));
    }

    bool App::Unhandle(std::string_view name) {
        return impl->runtime->Unhandle(name);
    }

    Result<std::shared_ptr<WindowGroup>> App::CreateWindowGroup(std::string name) {
        return impl->runtime->CreateGroup(std::move(name));
    }

    Result<std::shared_ptr<WindowGroup>> App::FindWindowGroup(std::string_view name) const {
        return impl->runtime->FindGroup(name);
    }

    std::vector<std::shared_ptr<Window>> App::Windows() const {
        return impl->runtime->Windows();
    }

    Result<std::shared_ptr<Window>> App::FindWindow(WindowId id) const {
        return impl->runtime->FindWindow(id);
    }

    const AppOptions& App::Options() const noexcept {
        return impl->runtime->options;
    }

    const PlatformCapabilities& App::Capabilities() const noexcept {
        return impl->runtime->capabilities;
    }

    std::filesystem::path App::StateFolder() const {
        return impl->runtime->stateFolder;
    }

    Subscription App::OnReady(std::function<void()> handler) {
        auto callback = handler;
        Subscription subscription = impl->runtime->readyEvent.Subscribe(std::move(handler));
        if (impl->runtime->readyRaised.load() && impl->runtime->running.load() && subscription.IsActive()) {
            std::shared_ptr<AppRuntime> runtime = impl->runtime;
            impl->runtime->Enqueue([runtime, callback = std::move(callback)]() {
                if (!callback) {
                    return;
                }
                try {
                    callback();
                } catch (const std::exception& exception) {
                    runtime->ReportError(Error(ErrorCode::HANDLER_FAILED,
                        "An application ready handler threw an exception", exception.what()));
                } catch (...) {
                    runtime->ReportError(
                        Error(ErrorCode::HANDLER_FAILED, "An application ready handler threw an unknown exception"));
                }
            });
        }
        return subscription;
    }

    Subscription App::OnQuit(std::function<void()> handler) {
        return impl->runtime->quitEvent.Subscribe(std::move(handler));
    }

    Subscription App::OnAllWindowsClosed(std::function<void()> handler) {
        return impl->runtime->allWindowsClosedEvent.Subscribe(std::move(handler));
    }

    Subscription App::OnSecondInstance(std::function<void(const std::vector<std::string>&)> handler) {
        Subscription subscription = impl->runtime->secondInstanceEvent.Subscribe(std::move(handler));
        std::shared_ptr<AppRuntime> runtime = impl->runtime;
        impl->runtime->Enqueue([runtime]() { runtime->DeliverPendingSecondInstance(); });
        return subscription;
    }

    Subscription App::OnUnhandledError(std::function<void(const Error&)> handler) {
        return impl->runtime->unhandledErrorEvent.Subscribe(std::move(handler));
    }

    std::string FlareVersionString() {
        return std::string(LibraryVersion);
    }
}
