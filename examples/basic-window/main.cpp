#include <flare/flare.h>

#include <cstdio>
#include <memory>
#include <string>

int main() {
    Flare::AppOptions appOptions;
    appOptions.applicationName = "Flare Basic Window";
    appOptions.applicationId = "dev.flare.basic";
    auto createdApp = Flare::App::Create(appOptions);
    if (!createdApp.IsOk()) {
        std::printf("Unable to create the application: %s\n", createdApp.GetError().ToString().c_str());
        return 1;
    }
    std::unique_ptr<Flare::App> app = std::move(createdApp.Value());

    Flare::WindowOptions windowOptions;
    windowOptions.title = "Flare Basic Window";
    windowOptions.width = 960;
    windowOptions.height = 640;
    windowOptions.minWidth = 480;
    windowOptions.minHeight = 320;
    windowOptions.centered = true;
    windowOptions.backgroundColor = Flare::Color::FromArgb(0xFF12151C);
    windowOptions.showTrigger = Flare::ShowTrigger::FIRST_NAVIGATION_COMPLETED;
    windowOptions.persistState = true;
    windowOptions.stateKey = "basic-window";

    Flare::BrowserOptions browserOptions;
    browserOptions.html =
        "<!doctype html><html><head><meta charset='utf-8'><title>Flare</title>"
        "<style>html,body{height:100%;margin:0;background:#12151c;color:#e7ecf5;font-family:Segoe "
        "UI,system-ui,sans-serif;display:grid;place-items:center}"
        ".card{padding:48px 56px;border-radius:18px;background:#1b2230;box-shadow:0 24px 70px "
        "rgba(0,0,0,.45);max-width:560px;text-align:center}"
        "h1{margin:0 0 12px;font-size:34px;letter-spacing:.5px}p{margin:0;color:#9fb0c9;line-height:1.6}"
        "code{color:#7cc7ff}</style></head><body><div class='card'><h1>Flare is running</h1>"
        "<p>This native window hosts a WebView and was created entirely from C++. "
        "Resize or move it, then close it to end the application.</p></div></body></html>";
    browserOptions.htmlBaseUrl = "https://flare.local/";
    browserOptions.devToolsEnabled = true;
    browserOptions.contextMenuEnabled = true;

    Flare::BridgeOptions bridgeOptions;

    auto createdWindow = app->NewWindow(windowOptions, browserOptions, bridgeOptions);
    if (!createdWindow.IsOk()) {
        std::printf("Unable to create the window: %s\n", createdWindow.GetError().ToString().c_str());
        return 1;
    }
    std::shared_ptr<Flare::Window> window = createdWindow.Value();

    window->OnBrowserCreationFailed([](const Flare::BrowserCreationFailedEvent& event) {
        std::printf("Browser creation failed: %s\n", event.error.ToString().c_str());
    });
    window->OnBrowserReady([](Flare::Window& readyWindow) {
        std::printf("Browser ready for window %llu\n", static_cast<unsigned long long>(readyWindow.GetId()));
    });
    window->OnNavigationCompleted([](const Flare::NavigationCompletedEvent& event) {
        std::printf("Navigation completed: success=%d url=%s\n", event.isSuccess ? 1 : 0, event.url.c_str());
    });
    window->OnResize([](const Flare::ResizeEvent& event) {
        std::printf("Resized to %.0fx%.0f\n", event.bounds.width, event.bounds.height);
    });
    window->OnClosed([](Flare::Window&) { std::printf("Window closed\n"); });

    app->OnAllWindowsClosed([]() { std::printf("All windows closed\n"); });

    auto runResult = app->Run();
    if (!runResult.IsOk()) {
        std::printf("Application loop failed: %s\n", runResult.GetError().ToString().c_str());
        return 1;
    }
    return 0;
}
