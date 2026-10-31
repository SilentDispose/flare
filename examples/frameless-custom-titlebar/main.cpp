#include <flare/flare.h>

#include <cstdio>
#include <memory>
#include <string>

int main() {
    Flare::AppOptions appOptions;
    appOptions.applicationName = "Flare Frameless";
    appOptions.applicationId = "dev.flare.frameless";
    auto createdApp = Flare::App::Create(appOptions);
    if (!createdApp.IsOk()) {
        std::printf("Unable to create the application: %s\n", createdApp.GetError().ToString().c_str());
        return 1;
    }
    std::unique_ptr<Flare::App> app = std::move(createdApp.Value());

    Flare::WindowOptions windowOptions;
    windowOptions.title = "Flare Frameless";
    windowOptions.width = 1040;
    windowOptions.height = 700;
    windowOptions.minWidth = 720;
    windowOptions.minHeight = 480;
    windowOptions.centered = true;
    windowOptions.frameless = true;
    windowOptions.titleBarStyle = Flare::TitleBarStyle::HIDDEN;
    windowOptions.borderlessShadow = true;
    windowOptions.roundedCorners = true;
    windowOptions.backgroundColor = Flare::Color::FromArgb(0xFF0A0F18);
    windowOptions.showTrigger = Flare::ShowTrigger::FIRST_NAVIGATION_COMPLETED;
    windowOptions.useDocumentTitle = false;
    windowOptions.draggableRegions = {Flare::Rect{0.0, 0.0, 1040.0, 52.0}};
    windowOptions.persistState = true;
    windowOptions.stateKey = "frameless-demo";

    Flare::BrowserOptions browserOptions;
    browserOptions.allowFileAccessFromFileUrls = true;
    browserOptions.backgroundColor = Flare::Color::FromArgb(0x00000000);

    Flare::BridgeOptions bridgeOptions;
    bridgeOptions.allowedWindowActions = Flare::AllWindowActions();

    auto createdWindow = app->NewWindow(windowOptions, browserOptions, bridgeOptions);
    if (!createdWindow.IsOk()) {
        std::printf("Unable to create the window: %s\n", createdWindow.GetError().ToString().c_str());
        return 1;
    }
    std::shared_ptr<Flare::Window> window = createdWindow.Value();

    window->OnBrowserCreationFailed([](const Flare::BrowserCreationFailedEvent& event) {
        std::printf("Browser creation failed: %s\n", event.error.ToString().c_str());
    });
    window->OnResize([](const Flare::ResizeEvent& event) {
        std::printf("Window resized to %.0fx%.0f\n", event.bounds.width, event.bounds.height);
    });
    window->OnClosed([](Flare::Window&) { std::printf("Frameless window closed\n"); });

    auto runResult = app->Run();
    if (!runResult.IsOk()) {
        std::printf("Application loop failed: %s\n", runResult.GetError().ToString().c_str());
        return 1;
    }
    return 0;
}
