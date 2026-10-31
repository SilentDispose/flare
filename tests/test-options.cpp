#include "test-framework.h"

#include <filesystem>
#include <string>

#include <flare/bridge-options.h>
#include <flare/browser-options.h>
#include <flare/window-options.h>

#include "core/icon-utils.h"

void RunOptionTests(Test::Context& context) {
    context.Begin("options");
    Flare::WindowOptions window;
    window.width = 800;
    window.height = 600;
    window.minWidth = 400;
    window.minHeight = 300;
    context.Check(Flare::ValidateWindowOptions(window).IsOk(), "default window options valid");

    Flare::WindowOptions invalidWidth;
    invalidWidth.width = 0;
    context.Check(Flare::ValidateWindowOptions(invalidWidth).IsError(), "zero width rejected");

    Flare::WindowOptions invalidConstraints;
    invalidConstraints.minWidth = 900;
    invalidConstraints.maxWidth = 800;
    context.Check(Flare::ValidateWindowOptions(invalidConstraints).IsError(), "min greater than max rejected");

    Flare::WindowOptions invalidOpacity;
    invalidOpacity.opacity = 1.5;
    context.Check(Flare::ValidateWindowOptions(invalidOpacity).IsError(), "invalid opacity rejected");

    Flare::WindowOptions invalidRegion;
    invalidRegion.draggableRegions.push_back(Flare::Rect{0, 0, -1, 10});
    context.Check(Flare::ValidateWindowOptions(invalidRegion).IsError(), "invalid draggable region rejected");

    Flare::BrowserOptions browser;
    browser.html = "<html></html>";
    context.Check(Flare::ValidateBrowserOptions(browser).IsOk(), "html browser options valid");

    Flare::BrowserOptions multipleContent;
    multipleContent.html = "<html></html>";
    multipleContent.startUrl = "https://example.com";
    context.Check(
        Flare::ValidateBrowserOptions(multipleContent).IsError(), "multiple initial content sources rejected");

    Flare::BrowserOptions invalidHeader;
    invalidHeader.extraHeaders["X-Test"] = "ok\r\nInjected: yes";
    context.Check(Flare::ValidateBrowserOptions(invalidHeader).IsError(), "header injection rejected");

    Flare::BrowserOptions invalidProxy;
    invalidProxy.proxy.enabled = true;
    context.Check(Flare::ValidateBrowserOptions(invalidProxy).IsError(), "proxy without server rejected");

    Flare::BrowserOptions invalidScheme;
    invalidScheme.customSchemes["http"] = [](const Flare::CustomSchemeRequest&) {
        return Flare::Result<Flare::CustomSchemeResponse>(Flare::CustomSchemeResponse{});
    };
    context.Check(Flare::ValidateBrowserOptions(invalidScheme).IsError(), "reserved custom scheme rejected");

    Flare::BridgeOptions bridge;
    context.Check(Flare::ValidateBridgeOptions(bridge).IsOk(), "default bridge options valid");

    Flare::BridgeOptions hardened;
    hardened.hardenedMode = true;
    context.Check(Flare::ValidateBridgeOptions(hardened).IsError(), "hardened mode requires origins");

    Flare::BridgeOptions invalidOrigin;
    invalidOrigin.allowedOrigins.push_back("example.com");
    context.Check(Flare::ValidateBridgeOptions(invalidOrigin).IsError(), "invalid origin rejected");

    Flare::BridgeOptions invalidHandler;
    invalidHandler.allowedHandlerNames.insert("bad name");
    context.Check(Flare::ValidateBridgeOptions(invalidHandler).IsError(), "invalid handler name rejected");

    Flare::BridgeOptions invalidSize;
    invalidSize.maxPayloadBytes = invalidSize.maxMessageBytes + 1;
    context.Check(
        Flare::ValidateBridgeOptions(invalidSize).IsError(), "payload limit larger than message limit rejected");

    Flare::BridgeOptions invalidAction;
    invalidAction.allowedWindowActions.insert(static_cast<Flare::WindowAction>(999));
    context.Check(Flare::ValidateBridgeOptions(invalidAction).IsError(), "unknown window action rejected");

    context.Check(Flare::WindowActionName(Flare::WindowAction::SET_TITLE) == "setTitle", "window action name");
    context.Check(Flare::ParseWindowAction("setSize").has_value(), "window action parse");
    context.Check(!Flare::ParseWindowAction("unknown-action").has_value(), "unknown window action parse");
    context.Check(Flare::AllWindowActions().size() == 20, "all window actions count");

    context.Check(Flare::Icon().IsEmpty(), "empty icon");
    const Flare::Icon pathIcon = Flare::Icon::FromPath("icon.png");
    context.Check(pathIcon.source == Flare::IconSource::PATH && !pathIcon.IsEmpty(), "path icon");
    const Flare::Icon assetIcon = Flare::Icon::FromAsset("icons/app.png");
    context.Check(assetIcon.source == Flare::IconSource::ASSET, "asset icon source");
    const Flare::Icon resourceIcon = Flare::Icon::FromResource("IDI_APP");
    context.Check(resourceIcon.source == Flare::IconSource::RESOURCE, "resource icon source");
    Flare::Result<Flare::Icon> resolved = Flare::ResolveIcon(assetIcon, std::filesystem::path("assets"));
    context.Check(resolved.IsOk() && resolved.Value().source == Flare::IconSource::PATH
            && resolved.Value().path.filename() == "app.png",
        "asset icon resolved");
    context.Check(Flare::ResolveIcon(assetIcon, {}).IsError(), "asset icon requires assets folder");
}
