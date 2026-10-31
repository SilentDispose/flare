#include "flare/embedded-web.h"

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {
    constexpr std::string_view HttpsOrigin = "https://flare-embedded.invalid";
    constexpr std::string_view CustomOrigin = "flare-embedded://embedded";

    struct StoredBundle {
        std::map<std::string, Flare::EmbeddedWebFile, std::less<>> files;
    };

    using Registry = std::map<std::string, StoredBundle, std::less<>>;

    Registry& Bundles() {
        static Registry bundles;
        return bundles;
    }

    std::mutex& BundleMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool HasPrefix(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool HasOriginPrefix(std::string_view url, std::string_view origin) {
        if (!HasPrefix(url, origin)) {
            return false;
        }
        return url.size() == origin.size() || url[origin.size()] == '/';
    }

    bool IsSafeRelativePath(std::string_view path) {
        if (path.empty() || path.find('\0') != std::string_view::npos || path.front() == '/'
            || path.find('\\') != std::string_view::npos) {
            return false;
        }
        std::size_t start = 0;
        while (start <= path.size()) {
            const std::size_t end = path.find('/', start);
            const std::string_view component = path.substr(
                start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (component.empty() || component == "." || component == "..") {
                return false;
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return true;
    }

    int HexValue(char character) {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    }

    bool IsUnreserved(unsigned char byte) {
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '.' || byte == '_' || byte == '~';
    }

    std::string EncodePath(std::string_view path) {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(path.size());
        for (unsigned char byte : path) {
            if (IsUnreserved(byte) || byte == '/') {
                encoded.push_back(static_cast<char>(byte));
            } else {
                encoded.push_back('%');
                encoded.push_back(digits[(byte >> 4) & 0x0Fu]);
                encoded.push_back(digits[byte & 0x0Fu]);
            }
        }
        return encoded;
    }

    bool DecodePercent(std::string_view text, std::string& decoded) {
        decoded.clear();
        decoded.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] != '%') {
                decoded.push_back(text[index]);
                continue;
            }
            if (index + 2 >= text.size()) {
                return false;
            }
            const int high = HexValue(text[index + 1]);
            const int low = HexValue(text[index + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            decoded.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        }
        return true;
    }

    struct ParsedRequest {
        std::string bundle;
        std::string path;
    };

    Flare::Result<ParsedRequest> ParseRequest(std::string_view url) {
        std::string_view rest;
        if (HasOriginPrefix(url, HttpsOrigin)) {
            rest = url.substr(HttpsOrigin.size());
        } else if (HasOriginPrefix(url, CustomOrigin)) {
            rest = url.substr(CustomOrigin.size());
        } else {
            return Flare::Error(Flare::ErrorCode::INVALID_ARGUMENT, "The URL does not target embedded web resources");
        }
        if (!rest.empty() && rest.front() == '/') {
            rest.remove_prefix(1);
        }
        const std::size_t suffix = rest.find_first_of("?#");
        if (suffix != std::string_view::npos) {
            rest = rest.substr(0, suffix);
        }

        std::string decoded;
        if (!DecodePercent(rest, decoded)) {
            return Flare::Error(
                Flare::ErrorCode::INVALID_ARGUMENT, "The embedded web URL contains invalid percent encoding");
        }
        const std::size_t slash = decoded.find('/');
        if (slash == std::string::npos || slash == 0) {
            return Flare::Error(Flare::ErrorCode::INVALID_ARGUMENT, "The embedded web URL does not name a bundle");
        }
        ParsedRequest request;
        request.bundle = decoded.substr(0, slash);
        request.path = decoded.substr(slash + 1);
        if (request.path.empty() || request.path.back() == '/') {
            request.path += "index.html";
        }
        if (!IsSafeRelativePath(request.path)) {
            return Flare::Error(Flare::ErrorCode::INVALID_ARGUMENT, "The embedded web URL contains an unsafe path");
        }
        return request;
    }

    std::string MimeTypeForPath(std::string_view path) {
        const std::size_t dot = path.rfind('.');
        if (dot == std::string_view::npos) {
            return "application/octet-stream";
        }
        std::string extension(path.substr(dot + 1));
        for (char& character : extension) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        if (extension == "html" || extension == "htm") {
            return "text/html; charset=utf-8";
        }
        if (extension == "css") {
            return "text/css; charset=utf-8";
        }
        if (extension == "js" || extension == "mjs") {
            return "application/javascript; charset=utf-8";
        }
        if (extension == "json" || extension == "map") {
            return "application/json; charset=utf-8";
        }
        if (extension == "svg") {
            return "image/svg+xml";
        }
        if (extension == "png") {
            return "image/png";
        }
        if (extension == "jpg" || extension == "jpeg") {
            return "image/jpeg";
        }
        if (extension == "gif") {
            return "image/gif";
        }
        if (extension == "webp") {
            return "image/webp";
        }
        if (extension == "ico") {
            return "image/x-icon";
        }
        if (extension == "wasm") {
            return "application/wasm";
        }
        if (extension == "woff") {
            return "font/woff";
        }
        if (extension == "woff2") {
            return "font/woff2";
        }
        if (extension == "ttf") {
            return "font/ttf";
        }
        if (extension == "otf") {
            return "font/otf";
        }
        if (extension == "txt") {
            return "text/plain; charset=utf-8";
        }
        if (extension == "xml") {
            return "application/xml; charset=utf-8";
        }
        if (extension == "pdf") {
            return "application/pdf";
        }
        return "application/octet-stream";
    }

    Flare::CustomSchemeResponse NotFoundResponse() {
        Flare::CustomSchemeResponse response;
        response.statusCode = 404;
        response.mimeType = "text/plain; charset=utf-8";
        response.body = "Not found";
        return response;
    }
}

namespace Flare {
    void RegisterEmbeddedWebBundle(const EmbeddedWebBundle& bundle) {
        if (bundle.name.empty() || (bundle.fileCount > 0 && bundle.files == nullptr)) {
            return;
        }
        StoredBundle stored;
        for (std::size_t index = 0; index < bundle.fileCount; ++index) {
            const EmbeddedWebFile& file = bundle.files[index];
            if (!IsSafeRelativePath(file.path) || (file.size > 0 && file.data == nullptr)) {
                continue;
            }
            stored.files.insert_or_assign(std::string(file.path), file);
        }
        std::lock_guard<std::mutex> guard(BundleMutex());
        Bundles().insert_or_assign(std::string(bundle.name), std::move(stored));
    }

    std::vector<std::string> EmbeddedWebBundleNames() {
        std::lock_guard<std::mutex> guard(BundleMutex());
        std::vector<std::string> names;
        names.reserve(Bundles().size());
        for (const auto& entry : Bundles()) {
            names.push_back(entry.first);
        }
        return names;
    }

    std::string BuildEmbeddedWebUrl(std::string_view name, std::string_view path, bool useHttps) {
        const std::string encodedBundle = EncodePath(name);
        const std::string encodedPath = EncodePath(path);
        if (useHttps) {
            return std::string(HttpsOrigin) + "/" + encodedBundle + "/" + encodedPath;
        }
        return std::string(CustomOrigin) + "/" + encodedBundle + "/" + encodedPath;
    }

    bool IsEmbeddedWebUrl(std::string_view url) noexcept {
        return HasOriginPrefix(url, HttpsOrigin) || HasOriginPrefix(url, CustomOrigin);
    }

    Result<CustomSchemeResponse> LoadEmbeddedWebResource(std::string_view name, std::string_view url) {
        Result<ParsedRequest> parsed = ParseRequest(url);
        if (!parsed.IsOk()) {
            return parsed.GetError();
        }
        if (parsed.Value().bundle != name) {
            return Flare::Error(Flare::ErrorCode::NOT_FOUND, "The embedded web request targets a different bundle");
        }

        std::lock_guard<std::mutex> guard(BundleMutex());
        const auto bundle = Bundles().find(name);
        if (bundle == Bundles().end()) {
            return Flare::Error(Flare::ErrorCode::NOT_FOUND, "The embedded web bundle is not registered");
        }
        const auto file = bundle->second.files.find(parsed.Value().path);
        if (file == bundle->second.files.end()) {
            return NotFoundResponse();
        }

        Flare::CustomSchemeResponse response;
        response.statusCode = 200;
        response.mimeType = MimeTypeForPath(parsed.Value().path);
        response.headers["Cache-Control"] = "no-store";
        if (file->second.size > 0) {
            response.body.assign(
                reinterpret_cast<const char*>(file->second.data), static_cast<std::size_t>(file->second.size));
        }
        return response;
    }
}
