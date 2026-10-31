#include "test-framework.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "core/url-utils.h"

void RunUrlTests(Test::Context& context) {
    context.Begin("url");
    context.Check(Flare::ExtractOrigin("https://example.com/path?q=1#frag") == "https://example.com", "https origin");
    context.Check(
        Flare::ExtractOrigin("https://example.com:443/path") == "https://example.com", "default https port removed");
    context.Check(
        Flare::ExtractOrigin("http://example.com:8080/path") == "http://example.com:8080", "custom port preserved");
    context.Check(
        Flare::ExtractOrigin("HTTPS://EXAMPLE.COM/Path") == "https://example.com", "scheme and host lowercased");
    context.Check(Flare::ExtractOrigin("file:///tmp/page.html") == "file://", "file origin");
    context.Check(Flare::ExtractOrigin("about:blank") == "null", "about origin");
    context.Check(Flare::ExtractOrigin("data:text/html,hello") == "null", "data origin");
    context.Check(Flare::ExtractOrigin("blob:https://example.com/id") == "https://example.com", "blob origin");
    context.Check(
        Flare::ExtractOrigin("https://user:pass@example.com/path") == "https://example.com", "user info stripped");
    context.Check(Flare::ExtractOrigin("https://[::1]:8443/path") == "https://[::1]:8443", "ipv6 origin");

    context.Check(Flare::MatchesUrlPattern("https://app.flare/index.html", "https://app.flare/*"), "url wildcard");
    context.Check(
        Flare::MatchesUrlPattern("https://api.example.com/v1/users", "https://*.example.com/*"), "host wildcard");
    context.Check(!Flare::MatchesUrlPattern("https://example.org/", "https://example.com/*"), "url mismatch");
    context.Check(Flare::MatchesPattern("abc", "a?c"), "single character wildcard");

    context.Check(Flare::OriginMatchesPattern("https://example.com", "https://example.com"), "origin exact");
    context.Check(Flare::OriginMatchesPattern("https://api.example.com", "https://*.example.com"), "origin wildcard");
    context.Check(Flare::OriginMatchesPattern("file://", "file://"), "file origin pattern");
    context.Check(Flare::OriginMatchesPattern("https://example.com", "*"), "any origin");
    context.Check(!Flare::OriginMatchesPattern("https://evil.example", "https://example.com"), "origin denial");

    context.Check(Flare::IsValidOriginPattern("https://*.example.com"), "valid wildcard origin");
    context.Check(Flare::IsValidOriginPattern("https://app.flare/"), "valid origin with slash");
    context.Check(!Flare::IsValidOriginPattern("example.com"), "origin requires a scheme");
    context.Check(!Flare::IsValidOriginPattern("https://example.com/\"bad\""), "origin rejects quotes");

    context.Check(Flare::IsHttpLikeUrl("https://example.com"), "http-like url");
    context.Check(!Flare::IsHttpLikeUrl("not a url"), "invalid url");

    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "flare-url-tests";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder / "nested", error);
    {
        std::ofstream file(folder / "index.html");
        file << "ok";
    }
    {
        std::ofstream file(folder / "nested" / "script.js");
        file << "ok";
    }
    {
        std::ofstream outside(folder.parent_path() / "flare-url-outside.txt");
        outside << "outside";
    }
    std::filesystem::path resolved;
    context.Check(Flare::ResolveWithinDirectory(folder, "/nested/script.js", resolved), "resolves nested file");
    context.Check(Flare::ResolveWithinDirectory(folder, "index.html", resolved), "resolves root file");
    context.Check(!Flare::ResolveWithinDirectory(folder, "../flare-url-outside.txt", resolved), "blocks traversal");

    const std::string fileUrl = Flare::FileUrlFromPath(folder / "index.html");
    context.Check(fileUrl.rfind("file://", 0) == 0, "file url prefix");
    context.Check(Flare::PathFromFileUrl(fileUrl) == (folder / "index.html"), "file url round trip");
    std::filesystem::remove_all(folder, error);
    std::filesystem::remove(folder.parent_path() / "flare-url-outside.txt", error);
}
