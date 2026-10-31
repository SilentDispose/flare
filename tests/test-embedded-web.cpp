#include "test-framework.h"

#include <cstdint>
#include <string>

#include <flare/browser-options.h>
#include <flare/embedded-web.h>

void RunEmbeddedWebTests(Test::Context& context) {
    context.Begin("embedded web");
    static const std::uint8_t indexBytes[] = {'<', 'h', 't', 'm', 'l', '>', 'o', 'k'};
    static const std::uint8_t styleBytes[] = {'b', 'o', 'd', 'y', '{', '}'};
    static const std::uint8_t textBytes[] = {'h', 'e', 'l', 'l', 'o'};
    static const Flare::EmbeddedWebFile files[] = {{"index.html", indexBytes, sizeof(indexBytes)},
        {"assets/style.css", styleBytes, sizeof(styleBytes)}, {"assets/hello world.txt", textBytes, sizeof(textBytes)}};
    Flare::RegisterEmbeddedWebBundle(Flare::EmbeddedWebBundle{"tests-embedded", files, 3});

    const std::string customUrl = Flare::BuildEmbeddedWebUrl("tests-embedded", "index.html", false);
    context.Check(customUrl == "flare-embedded://embedded/tests-embedded/index.html", "custom bundle URL");
    Flare::Result<Flare::CustomSchemeResponse> index = Flare::LoadEmbeddedWebResource("tests-embedded", customUrl);
    context.Check(index.IsOk() && index.Value().statusCode == 200, "embedded index served");
    context.Check(index.IsOk() && index.Value().body == "<html>ok", "embedded bytes preserved");
    context.Check(index.IsOk() && index.Value().mimeType.rfind("text/html", 0) == 0, "embedded MIME type");

    const std::string httpsUrl = Flare::BuildEmbeddedWebUrl("tests-embedded", "assets/style.css", true);
    context.Check(httpsUrl == "https://flare-embedded.invalid/tests-embedded/assets/style.css", "https bundle URL");
    Flare::Result<Flare::CustomSchemeResponse> style = Flare::LoadEmbeddedWebResource("tests-embedded", httpsUrl);
    context.Check(style.IsOk() && style.Value().body == "body{}", "nested embedded resource served");
    context.Check(style.IsOk() && style.Value().mimeType.rfind("text/css", 0) == 0, "nested MIME type");

    const std::string spacedUrl = Flare::BuildEmbeddedWebUrl("tests-embedded", "assets/hello world.txt", false);
    context.Check(spacedUrl.find("hello%20world.txt") != std::string::npos, "path percent encoded");
    Flare::Result<Flare::CustomSchemeResponse> spaced = Flare::LoadEmbeddedWebResource("tests-embedded", spacedUrl);
    context.Check(spaced.IsOk() && spaced.Value().body == "hello", "percent encoded path decoded");

    const std::string missingUrl = Flare::BuildEmbeddedWebUrl("tests-embedded", "missing.txt", false);
    Flare::Result<Flare::CustomSchemeResponse> missing = Flare::LoadEmbeddedWebResource("tests-embedded", missingUrl);
    context.Check(missing.IsOk() && missing.Value().statusCode == 404, "missing embedded resource returns 404");

    const std::string otherBundle = Flare::BuildEmbeddedWebUrl("other-bundle", "index.html", false);
    Flare::Result<Flare::CustomSchemeResponse> mismatch = Flare::LoadEmbeddedWebResource("tests-embedded", otherBundle);
    context.Check(mismatch.IsError() && mismatch.GetError().code == Flare::ErrorCode::NOT_FOUND,
        "request for another bundle rejected");

    Flare::Result<Flare::CustomSchemeResponse> unsafe = Flare::LoadEmbeddedWebResource(
        "tests-embedded", "https://flare-embedded.invalid/tests-embedded/../index.html");
    context.Check(unsafe.IsError() && unsafe.GetError().code == Flare::ErrorCode::INVALID_ARGUMENT,
        "unsafe embedded path rejected");

    Flare::BrowserOptions options;
    options.embeddedBundle = "bad/name";
    context.Check(Flare::ValidateBrowserOptions(options).IsError(), "unsafe bundle name rejected");
    options.embeddedBundle = "tests-embedded";
    options.embeddedStartPage = "../index.html";
    context.Check(Flare::ValidateBrowserOptions(options).IsError(), "unsafe start page rejected");
    options.embeddedStartPage = "index.html";
    context.Check(Flare::ValidateBrowserOptions(options).IsOk(), "valid embedded options accepted");
}
