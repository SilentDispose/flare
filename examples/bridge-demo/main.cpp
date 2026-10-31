#include <flare/flare.h>

#include <cstdio>
#include <memory>
#include <string>

int main() {
    Flare::AppOptions appOptions;
    appOptions.applicationName = "Flare Bridge Demo";
    appOptions.applicationVersion = "1.4.0";
    appOptions.applicationId = "dev.flare.bridgedemo";
    auto createdApp = Flare::App::Create(appOptions);
    if (!createdApp.IsOk()) {
        std::printf("Unable to create the application: %s\n", createdApp.GetError().ToString().c_str());
        return 1;
    }
    std::unique_ptr<Flare::App> app = std::move(createdApp.Value());

    Flare::BridgeOptions bridgeOptions;
    bridgeOptions.allowedWindowActions = Flare::AllWindowActions();

    Flare::WindowOptions windowOptions;
    windowOptions.title = "Flare Bridge Demo";
    windowOptions.width = 980;
    windowOptions.height = 700;
    windowOptions.minWidth = 640;
    windowOptions.minHeight = 460;
    windowOptions.centered = true;
    windowOptions.backgroundColor = Flare::Color::FromArgb(0xFF0B1220);
    windowOptions.showTrigger = Flare::ShowTrigger::FIRST_NAVIGATION_COMPLETED;
    windowOptions.useDocumentTitle = false;
    windowOptions.persistState = true;
    windowOptions.stateKey = "bridge-demo";

    Flare::BrowserOptions browserOptions;
    browserOptions.devToolsEnabled = true;
    browserOptions.contextMenuEnabled = true;
    browserOptions.allowFileAccessFromFileUrls = true;

    auto createdWindow = app->NewWindow(windowOptions, browserOptions, bridgeOptions);
    if (!createdWindow.IsOk()) {
        std::printf("Unable to create the window: %s\n", createdWindow.GetError().ToString().c_str());
        return 1;
    }
    std::shared_ptr<Flare::Window> window = createdWindow.Value();

    window->OnBrowserCreationFailed([](const Flare::BrowserCreationFailedEvent& event) {
        std::printf("Browser creation failed: %s\n", event.error.ToString().c_str());
    });

    Flare::Result<void> addHandler = window->Handle(
        "add", [](const Flare::JSON& payload) -> Flare::Result<Flare::JSON> {
            const auto left = payload.find("left");
            const auto right = payload.find("right");
            if (left == payload.end() || right == payload.end() || !left->is_number() || !right->is_number()) {
                return Flare::Error(Flare::ErrorCode::INVALID_ARGUMENT, "add requires numeric left and right values");
            }
            Flare::JSON result;
            result["sum"] = left->get<double>() + right->get<double>();
            return result;
        });
    if (!addHandler.IsOk()) {
        std::printf("Unable to register add: %s\n", addHandler.GetError().ToString().c_str());
    }

    Flare::HandlerOptions profileOptions;
    profileOptions.permission = "profile:read";
    Flare::Result<void> profileHandler = window->Handle(
        "profile",
        [](const Flare::JSON&) -> Flare::Result<Flare::JSON> {
            Flare::JSON profile;
            profile["name"] = "Ada Lovelace";
            profile["role"] = "Engineer";
            profile["projects"] = 4;
            return profile;
        },
        profileOptions);
    if (!profileHandler.IsOk()) {
        std::printf("Unable to register profile: %s\n", profileHandler.GetError().ToString().c_str());
    }

    Flare::Result<void> secretHandler = window->Handle(
        "secret",
        [](const Flare::JSON&) -> Flare::Result<Flare::JSON> {
            Flare::JSON data;
            data["value"] = "restricted";
            return data;
        },
        Flare::HandlerOptions{"profile:write", {}});
    if (!secretHandler.IsOk()) {
        std::printf("Unable to register secret: %s\n", secretHandler.GetError().ToString().c_str());
    }

    Flare::Result<void> echoHandler = window->Handle(
        "echo", [](const Flare::JSON& payload) -> Flare::Result<Flare::JSON> {
            Flare::JSON result;
            result["echo"] = payload;
            return result;
        });
    if (!echoHandler.IsOk()) {
        std::printf("Unable to register echo: %s\n", echoHandler.GetError().ToString().c_str());
    }

    window->On("button-clicked", [window](const Flare::JSON& payload) {
        std::printf("JavaScript emitted button-clicked: %s\n", payload.dump().c_str());
        Flare::JSON acknowledgement;
        acknowledgement["received"] = payload;
        acknowledgement["source"] = "native";
        window->Emit("button-clicked-ack", acknowledgement);
    });

    window->OnDomContentLoaded([window](Flare::Window&) {
        Flare::JSON welcome;
        welcome["message"] = "Bridge connected";
        welcome["protocol"] = 1;
        welcome["features"] = {"emit", "invoke", "window"};
        window->Emit("welcome", welcome);
    });

    window->OnNavigationCompleted([](const Flare::NavigationCompletedEvent& event) {
        std::printf("Loaded %s (success=%d)\n", event.url.c_str(), event.isSuccess ? 1 : 0);
    });

    auto runResult = app->Run();
    if (!runResult.IsOk()) {
        std::printf("Application loop failed: %s\n", runResult.GetError().ToString().c_str());
        return 1;
    }
    return 0;
}
