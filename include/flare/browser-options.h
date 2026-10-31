#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "flare/error.h"
#include "flare/export.h"
#include "flare/geometry.h"

namespace Flare {
    enum class PermissionState { ASK, ALLOW, DENY };

    struct PermissionPolicy {
        PermissionState camera = PermissionState::ASK;
        PermissionState microphone = PermissionState::ASK;
        PermissionState geolocation = PermissionState::ASK;
        PermissionState notifications = PermissionState::ASK;
        PermissionState clipboardRead = PermissionState::ASK;
        PermissionState clipboardWrite = PermissionState::ASK;
    };

    struct DownloadSettings {
        std::filesystem::path directory;
        bool askWhereToSave = false;
        bool blocked = false;
    };

    struct ProxySettings {
        bool enabled = false;
        std::string server;
        std::string pacUrl;
        std::vector<std::string> bypassList;
        std::string username;
        std::string password;
    };

    struct VirtualHostMapping {
        std::string host;
        std::filesystem::path folder;
        std::string scheme = "flare-app";
    };

    struct CustomSchemeRequest {
        std::string url;
        std::string scheme;
        std::string method;
        std::map<std::string, std::string> headers;
    };

    struct CustomSchemeResponse {
        int statusCode = 200;
        std::string mimeType = "text/plain";
        std::string body;
        std::map<std::string, std::string> headers;
    };

    using CustomSchemeHandler = std::function<Result<CustomSchemeResponse>(const CustomSchemeRequest&)>;

    enum class CookieSameSite { UNSPECIFIED, NONE, LAX, STRICT_MODE };

    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path = "/";
        std::int64_t expiresUnixSeconds = -1;
        bool secure = false;
        bool httpOnly = false;
        CookieSameSite sameSite = CookieSameSite::UNSPECIFIED;
    };

    enum class ClearDataKind : std::uint32_t {
        NONE = 0,
        COOKIES = 1u << 0,
        CACHE = 1u << 1,
        LOCAL_STORAGE = 1u << 2,
        SESSION_STORAGE = 1u << 3,
        INDEXED_DB = 1u << 4,
        SERVICE_WORKERS = 1u << 5,
        ALL = (1u << 6) - 1
    };

    [[nodiscard]] FLARE_API ClearDataKind operator|(ClearDataKind left, ClearDataKind right) noexcept;
    [[nodiscard]] FLARE_API ClearDataKind operator&(ClearDataKind left, ClearDataKind right) noexcept;
    [[nodiscard]] FLARE_API bool HasClearDataKind(ClearDataKind value, ClearDataKind flag) noexcept;

    enum class ImageFormat { PNG, JPEG };

    struct CaptureOptions {
        ImageFormat format = ImageFormat::PNG;
        int quality = 90;
        bool transparentBackground = false;
    };

    struct PdfOptions {
        bool landscape = false;
        bool printBackground = true;
        double scale = 1.0;
        double pageWidthInches = 8.5;
        double pageHeightInches = 11.0;
        double marginTopInches = 0.4;
        double marginBottomInches = 0.4;
        double marginLeftInches = 0.4;
        double marginRightInches = 0.4;
        bool preferCssPageSize = true;
        bool displayHeaderFooter = false;
        std::string headerTemplate;
        std::string footerTemplate;
    };

    struct FindOptions {
        bool forward = true;
        bool matchCase = false;
        bool findInSelection = false;
        int maxMatches = 0;
    };

    struct FindResult {
        int occurrences = 0;
        int activeMatch = 0;
        bool finished = true;
    };

    struct BrowserOptions {
        std::string startUrl;
        std::string html;
        std::string htmlBaseUrl;
        std::filesystem::path localFile;
        std::string embeddedBundle;
        std::string embeddedStartPage = "index.html";
        std::optional<VirtualHostMapping> virtualHost;
        std::filesystem::path userDataFolder;
        std::string userAgent;
        std::string language;
        double zoomFactor = 1.0;
        bool devToolsEnabled = false;
        bool contextMenuEnabled = true;
        bool defaultScriptDialogsEnabled = true;
        bool statusBarEnabled = false;
        bool zoomControlEnabled = true;
        bool browserAcceleratorKeysEnabled = true;
        bool autofillEnabled = true;
        bool passwordSaveEnabled = true;
        bool pinchZoomEnabled = true;
        bool swipeNavigationEnabled = true;
        bool javascriptEnabled = true;
        bool webSecurityEnabled = true;
        std::vector<std::string> allowedUrlPatterns;
        std::vector<std::string> blockedUrlPatterns;
        PermissionPolicy permissionPolicy;
        DownloadSettings download;
        ProxySettings proxy;
        std::vector<std::string> browserArguments;
        std::map<std::string, CustomSchemeHandler> customSchemes;
        bool incognito = false;
        bool clearCacheOnStart = false;
        Color backgroundColor{0, 0, 0, 0};
        std::vector<std::string> documentStartScripts;
        std::vector<std::string> documentEndScripts;
        std::map<std::string, std::string> extraHeaders;
        bool interceptRequests = false;
        std::vector<std::string> requestFilters;
        bool interceptResponses = false;
        std::vector<std::string> responseFilters;
        bool allowFileAccessFromFileUrls = true;
        bool hideScrollbars = false;
    };

    [[nodiscard]] FLARE_API Result<void> ValidateBrowserOptions(const BrowserOptions& options);
}
