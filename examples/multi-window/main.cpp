#include <flare/flare.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {
    std::string WindowHtml(const std::string& name, const std::string& color) {
        return std::string("<!doctype html><html><head><meta charset='utf-8'><title>") + name +
           "</title><style>html,body{height:100%;margin:0;background:#0f1420;color:#e8eef8;font-family:Segoe UI,system-ui,sans-serif}"
           ".wrap{max-width:640px;margin:0 auto;padding:32px}h1{font-size:26px;margin:0 0 6px}"
           ".badge{display:inline-block;padding:4px 10px;border-radius:999px;background:" + color + ";color:#07111f;font-weight:600;font-size:12px}"
           "button{margin:16px 8px 0 0;padding:11px 18px;border-radius:10px;border:1px solid #324158;background:#1b2536;color:#e8eef8;font-size:14px;cursor:pointer}"
           "button:hover{background:#243149}#messages{margin-top:24px;padding:16px;border-radius:12px;background:#121a27;min-height:120px;border:1px solid #223148}"
           "#messages div{padding:7px 0;border-bottom:1px solid #1d2a3d;font-size:14px;color:#b9c7db}"
           "#messages div:last-child{border-bottom:0}</style></head><body><div class='wrap'>"
           "<span class='badge'>Native window</span><h1>" + name + "</h1>"
           "<p>Windows are created and managed only by C++. Messages between windows travel through a C++ WindowGroup.</p>"
           "<button id='send'>Broadcast a message</button><button id='close'>Close this window</button>"
           "<div id='messages'></div></div><script>"
           "var messages=document.getElementById('messages');"
           "function append(text){var row=document.createElement('div');row.textContent=text;messages.prepend(row);}"
           "flare.on('peer-message',function(payload){append(payload.from + ': ' + payload.text);});"
           "document.getElementById('send').addEventListener('click',function(){"
           "flare.invoke('broadcast',{text:'Hello from " + name + "'}).then(function(result){append('Delivered to ' + result.delivered + ' windows');});"
           "});"
           "document.getElementById('close').addEventListener('click',function(){flare.window.close();});"
           "append('Ready. Other windows will appear shortly.');"
           "</script></body></html>";
    }
}

int main() {
    Flare::AppOptions appOptions;
    appOptions.applicationName = "Flare Multi Window";
    appOptions.applicationId = "dev.flare.multiwindow";
    auto createdApp = Flare::App::Create(appOptions);
    if (!createdApp.IsOk()) {
        std::printf("Unable to create the application: %s\n", createdApp.GetError().ToString().c_str());
        return 1;
    }
    std::unique_ptr<Flare::App> app = std::move(createdApp.Value());

    auto createdGroup = app->CreateWindowGroup("demo-windows");
    if (!createdGroup.IsOk()) {
        std::printf("Unable to create the window group: %s\n", createdGroup.GetError().ToString().c_str());
        return 1;
    }
    std::shared_ptr<Flare::WindowGroup> group = createdGroup.Value();

    Flare::BrowserOptions browserOptions;
    browserOptions.devToolsEnabled = true;
    Flare::WindowOptions baseOptions;
    baseOptions.width = 760;
    baseOptions.height = 560;
    baseOptions.centered = true;
    baseOptions.backgroundColor = Flare::Color::FromArgb(0xFF0F1420);
    baseOptions.showTrigger = Flare::ShowTrigger::FIRST_NAVIGATION_COMPLETED;
    baseOptions.useDocumentTitle = false;

    auto configure = [group](
                         const std::shared_ptr<Flare::Window>& window, const std::string& windowName, bool mainWindow) {
        window->SetTitle(windowName);
        window->OnBrowserCreationFailed([](const Flare::BrowserCreationFailedEvent& event) {
            std::printf("Browser creation failed: %s\n", event.error.ToString().c_str());
        });
        window->OnClosed([](Flare::Window& closedWindow) {
            std::printf("Window %llu closed\n", static_cast<unsigned long long>(closedWindow.GetId()));
        });
        auto registration = window->Handle(
            "broadcast", [group, windowName](const Flare::JSON& payload) -> Flare::Result<Flare::JSON> {
                Flare::JSON message;
                message["from"] = windowName;
                message["text"] = payload.value("text", std::string());
                const std::size_t count = group->Size();
                Flare::Result<void> emitted = group->Emit("peer-message", message);
                if (!emitted.IsOk()) {
                    return emitted.GetError();
                }
                Flare::JSON result;
                result["delivered"] = count;
                return result;
            });
        if (!registration.IsOk()) {
            std::printf("Handler registration failed: %s\n", registration.GetError().ToString().c_str());
        }
        Flare::Result<void> joined = window->JoinGroup("demo-windows");
        if (!joined.IsOk()) {
            std::printf("Group join failed: %s\n", joined.GetError().ToString().c_str());
        }
        (void)mainWindow;
    };

    Flare::WindowOptions mainOptions = baseOptions;
    mainOptions.title = "Main Window";
    Flare::BrowserOptions mainBrowser = browserOptions;
    mainBrowser.html = WindowHtml("Main Window", "#7cc7ff");
    mainBrowser.htmlBaseUrl = "https://flare.local/main";

    auto createdMain = app->NewWindow(mainOptions, mainBrowser);
    if (!createdMain.IsOk()) {
        std::printf("Unable to create the main window: %s\n", createdMain.GetError().ToString().c_str());
        return 1;
    }
    std::shared_ptr<Flare::Window> mainWindow = createdMain.Value();
    configure(mainWindow, "Main Window", true);

    auto peersCreated = std::make_shared<bool>(false);
    Flare::App* appPointer = app.get();
    mainWindow->OnBrowserReady([appPointer, peersCreated, configure, baseOptions, browserOptions](Flare::Window&) {
        if (*peersCreated) {
            return;
        }
        *peersCreated = true;
        for (int index = 1; index <= 2; ++index) {
            Flare::WindowOptions options = baseOptions;
            options.title = "Peer " + std::to_string(index);
            options.x = 120 + index * 60;
            options.y = 120 + index * 50;
            options.hasPosition = true;
            options.centered = false;
            Flare::BrowserOptions browser = browserOptions;
            browser.html = WindowHtml("Peer " + std::to_string(index), index == 1 ? "#8ce99a" : "#ffd166");
            browser.htmlBaseUrl = "https://flare.local/peer" + std::to_string(index);
            auto createdPeer = appPointer->NewWindow(options, browser);
            if (!createdPeer.IsOk()) {
                std::printf("Unable to create a peer window: %s\n", createdPeer.GetError().ToString().c_str());
                continue;
            }
            configure(createdPeer.Value(), "Peer " + std::to_string(index), false);
        }
    });

    app->OnAllWindowsClosed([]() { std::printf("All windows closed\n"); });

    auto runResult = app->Run();
    if (!runResult.IsOk()) {
        std::printf("Application loop failed: %s\n", runResult.GetError().ToString().c_str());
        return 1;
    }
    return 0;
}
