#include "platform/windows/windows-window.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json-utils.h"
#include "core/path-utils.h"
#include "core/text-utils.h"
#include "core/url-utils.h"
#include "flare/embedded-web.h"
#include "platform/windows/windows-callbacks.h"
#include "platform/windows/windows-com.h"
#include "platform/windows/windows-utils.h"

namespace Flare {
    namespace {
        std::string HResultText(HRESULT result) {
            return "WebView2 call failed with " + FormatHResult(result);
        }

        std::string LowerScheme(const std::string& url) {
            const std::size_t colon = url.find(':');
            return ToLowerAscii(colon == std::string::npos ? std::string() : url.substr(0, colon));
        }

        std::string StatusReason(int status) {
            switch (status) {
                case 200:
                    return "OK";
                case 201:
                    return "Created";
                case 204:
                    return "No Content";
                case 301:
                    return "Moved Permanently";
                case 302:
                    return "Found";
                case 304:
                    return "Not Modified";
                case 400:
                    return "Bad Request";
                case 401:
                    return "Unauthorized";
                case 403:
                    return "Forbidden";
                case 404:
                    return "Not Found";
                case 405:
                    return "Method Not Allowed";
                case 500:
                    return "Internal Server Error";
                default:
                    return "Response";
            }
        }

        std::string EscapeHtmlAttribute(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (char character : value) {
                switch (character) {
                    case '&':
                        escaped += "&amp;";
                        break;
                    case '"':
                        escaped += "&quot;";
                        break;
                    case '<':
                        escaped += "&lt;";
                        break;
                    case '>':
                        escaped += "&gt;";
                        break;
                    default:
                        escaped.push_back(character);
                        break;
                }
            }
            return escaped;
        }

        std::string FileNameFromUrl(const std::string& url) {
            const std::size_t query = url.find_first_of("?#");
            const std::string path = url.substr(0, query);
            const std::size_t slash = path.find_last_of('/');
            const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
            return name.empty() ? "download" : name;
        }

        ResourceType ResourceTypeFromContext(COREWEBVIEW2_WEB_RESOURCE_CONTEXT context) {
            switch (context) {
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT:
                    return ResourceType::DOCUMENT;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_STYLESHEET:
                    return ResourceType::STYLESHEET;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_IMAGE:
                    return ResourceType::IMAGE;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_MEDIA:
                    return ResourceType::MEDIA;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_SCRIPT:
                    return ResourceType::SCRIPT;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FONT:
                    return ResourceType::FONT;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_XML_HTTP_REQUEST:
                    return ResourceType::XHR;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FETCH:
                    return ResourceType::FETCH;
                case COREWEBVIEW2_WEB_RESOURCE_CONTEXT_WEBSOCKET:
                    return ResourceType::WEB_SOCKET;
                default:
                    break;
            }
            return ResourceType::OTHER;
        }

        DownloadState DownloadStateFromNative(COREWEBVIEW2_DOWNLOAD_STATE state) {
            switch (state) {
                case COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS:
                    return DownloadState::IN_PROGRESS;
                case COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED:
                    return DownloadState::INTERRUPTED;
                case COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED:
                    return DownloadState::COMPLETED;
            }
            return DownloadState::PENDING;
        }
    }

    Result<void> WindowsWindowBackend::Navigate(const std::string& url) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        return NavigateCore(Utf8ToWide(url), true);
    }

    Result<void> WindowsWindowBackend::NavigateToString(const std::string& html, const std::string& baseUrl) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        std::string content = html;
        if (!baseUrl.empty()) {
            const std::string tag = "<base href=\"" + EscapeHtmlAttribute(baseUrl) + "\">";
            const std::size_t head = ToLowerAscii(html).find("<head");
            if (head != std::string::npos) {
                const std::size_t close = html.find('>', head);
                if (close != std::string::npos) {
                    content.insert(close + 1, tag);
                } else {
                    content = "<head>" + tag + "</head>" + html;
                }
            } else {
                content = "<head>" + tag + "</head>" + html;
            }
        }
        const HRESULT result = webview->NavigateToString(Utf8ToWide(content).c_str());
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::LoadFile(const std::filesystem::path& file) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        return NavigateCore(Utf8ToWide(FileUrlFromPath(file)), false);
    }

    Result<void> WindowsWindowBackend::Reload() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webview->Reload();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::GoBack() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webview->GoBack();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::GoForward() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webview->GoForward();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Stop() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webview->Stop();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::ExecuteScript(const std::string& script, ResultCallback<JSON> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new ExecuteScriptCompletedHandler(
            [weakAlive, callback = std::move(callback)](HRESULT result, LPCWSTR value) mutable {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                if (!callback) {
                    return;
                }
                if (FAILED(result)) {
                    callback(Error(ErrorCode::PLATFORM_FAILURE, HResultText(result)));
                    return;
                }
                const std::string text = value ? WideToUtf8(value) : std::string("null");
                Result<JSON> parsed = ParseJsonValue(text, 16 * 1024 * 1024, 64, 500000);
                if (!parsed.IsOk()) {
                    callback(JSON(text));
                    return;
                }
                callback(std::move(parsed.Value()));
            });
        webview->ExecuteScript(Utf8ToWide(script).c_str(), handler);
        handler->Release();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::OpenDevTools() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webview->OpenDevToolsWindow();
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::CloseDevTools() {
        return Error(ErrorCode::NOT_SUPPORTED, "WebView2 does not expose a host API for closing the DevTools window");
    }

    Result<void> WindowsWindowBackend::PrintToPdf(
        const PdfOptions& options, const std::filesystem::path& outputPath, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        ComPtr<ICoreWebView2_7> webview7;
        if (FAILED(webview.As(IID_ICoreWebView2_7, webview7)) || !webview7) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support PDF export");
        }
        ComPtr<ICoreWebView2Environment6> environment6;
        ComPtr<ICoreWebView2PrintSettings> printSettings;
        if (FAILED(environment.As(IID_ICoreWebView2Environment6, environment6))
            || FAILED(environment6->CreatePrintSettings(printSettings.Put()))) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support print settings");
        }
        printSettings->put_Orientation(
            options.landscape ? COREWEBVIEW2_PRINT_ORIENTATION_LANDSCAPE : COREWEBVIEW2_PRINT_ORIENTATION_PORTRAIT);
        printSettings->put_ScaleFactor(options.scale);
        printSettings->put_PageWidth(options.pageWidthInches);
        printSettings->put_PageHeight(options.pageHeightInches);
        printSettings->put_MarginTop(options.marginTopInches);
        printSettings->put_MarginBottom(options.marginBottomInches);
        printSettings->put_MarginLeft(options.marginLeftInches);
        printSettings->put_MarginRight(options.marginRightInches);
        printSettings->put_ShouldPrintBackgrounds(options.printBackground ? TRUE : FALSE);
        printSettings->put_ShouldPrintHeaderAndFooter(options.displayHeaderFooter ? TRUE : FALSE);
        printSettings->put_HeaderTitle(Utf8ToWide(options.headerTemplate).c_str());
        printSettings->put_FooterUri(Utf8ToWide(options.footerTemplate).c_str());

        std::error_code error;
        const std::filesystem::path parent = outputPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
            if (error) {
                return Error(ErrorCode::IO_FAILURE, "Unable to create the PDF output directory", error.message());
            }
        }

        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new PrintToPdfCompletedHandler(
            [weakAlive, callback = std::move(callback)](HRESULT result, BOOL success) mutable {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                if (!callback) {
                    return;
                }
                if (FAILED(result) || !success) {
                    callback(Error(ErrorCode::PLATFORM_FAILURE, "PDF export failed", FormatHResult(result)));
                    return;
                }
                callback(Result<void>::Ok());
            });
        const HRESULT result = webview7->PrintToPdf(
            Utf8ToWide(PathToUtf8(outputPath)).c_str(), printSettings.Get(), handler);
        handler->Release();
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::Print() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        ComPtr<ICoreWebView2_16> webview16;
        if (FAILED(webview.As(IID_ICoreWebView2_16, webview16)) || !webview16) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support printing");
        }
        const HRESULT result = webview16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::CapturePreview(
        const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        IStream* rawStream = SHCreateMemStream(nullptr, 0);
        if (!rawStream) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to allocate the capture stream");
        }
        ComPtr<IStream> stream;
        stream.Attach(rawStream);
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new CapturePreviewCompletedHandler(
            [weakAlive, stream, callback = std::move(callback)](HRESULT result) mutable {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                if (!callback) {
                    return;
                }
                if (FAILED(result)) {
                    callback(Error(ErrorCode::PLATFORM_FAILURE, HResultText(result)));
                    return;
                }
                LARGE_INTEGER start{};
                if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
                    callback(Error(ErrorCode::IO_FAILURE, "Unable to read the capture stream"));
                    return;
                }
                STATSTG statistics{};
                if (FAILED(stream->Stat(&statistics, STATFLAG_NONAME))) {
                    callback(Error(ErrorCode::IO_FAILURE, "Unable to inspect the capture stream"));
                    return;
                }
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(statistics.cbSize.QuadPart));
                ULONG read = 0;
                if (!bytes.empty() && FAILED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read))) {
                    callback(Error(ErrorCode::IO_FAILURE, "Unable to read captured image bytes"));
                    return;
                }
                bytes.resize(read);
                callback(std::move(bytes));
            });
        const COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT format = options.format == ImageFormat::JPEG
            ? COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_JPEG
            : COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG;
        const HRESULT result = webview->CapturePreview(format, stream.Get(), handler);
        handler->Release();
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::GetCookies(
        const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        ComPtr<ICoreWebView2_2> webview2;
        ComPtr<ICoreWebView2CookieManager> cookieManager;
        if (FAILED(webview.As(IID_ICoreWebView2_2, webview2))
            || FAILED(webview2->get_CookieManager(cookieManager.Put())) || !cookieManager) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support cookie access");
        }
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new GetCookiesCompletedHandler(
            [weakAlive, callback = std::move(callback)](HRESULT result, ICoreWebView2CookieList* list) mutable {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                if (!callback) {
                    return;
                }
                if (FAILED(result) || !list) {
                    callback(Error(ErrorCode::PLATFORM_FAILURE, "Unable to read cookies", FormatHResult(result)));
                    return;
                }
                UINT count = 0;
                list->get_Count(&count);
                std::vector<Cookie> cookies;
                cookies.reserve(count);
                for (UINT index = 0; index < count; ++index) {
                    ComPtr<ICoreWebView2Cookie> native;
                    if (FAILED(list->GetValueAtIndex(index, native.Put())) || !native) {
                        continue;
                    }
                    Cookie cookie;
                    LPWSTR name = nullptr;
                    LPWSTR value = nullptr;
                    LPWSTR domain = nullptr;
                    LPWSTR path = nullptr;
                    native->get_Name(&name);
                    native->get_Value(&value);
                    native->get_Domain(&domain);
                    native->get_Path(&path);
                    cookie.name = name ? WideToUtf8(name) : std::string();
                    cookie.value = value ? WideToUtf8(value) : std::string();
                    cookie.domain = domain ? WideToUtf8(domain) : std::string();
                    cookie.path = path ? WideToUtf8(path) : std::string("/");
                    CoTaskMemFree(name);
                    CoTaskMemFree(value);
                    CoTaskMemFree(domain);
                    CoTaskMemFree(path);
                    BOOL secure = FALSE;
                    BOOL httpOnly = FALSE;
                    BOOL session = TRUE;
                    double expires = 0.0;
                    native->get_IsSecure(&secure);
                    native->get_IsHttpOnly(&httpOnly);
                    native->get_IsSession(&session);
                    native->get_Expires(&expires);
                    cookie.secure = secure != FALSE;
                    cookie.httpOnly = httpOnly != FALSE;
                    cookie.expiresUnixSeconds = session != FALSE ? -1 : static_cast<std::int64_t>(expires);
                    COREWEBVIEW2_COOKIE_SAME_SITE_KIND sameSite = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE;
                    native->get_SameSite(&sameSite);
                    if (sameSite == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX) {
                        cookie.sameSite = CookieSameSite::LAX;
                    } else if (sameSite == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT) {
                        cookie.sameSite = CookieSameSite::STRICT_MODE;
                    } else if (sameSite == COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE) {
                        cookie.sameSite = CookieSameSite::NONE;
                    }
                    cookies.push_back(std::move(cookie));
                }
                callback(std::move(cookies));
            });
        const HRESULT result = cookieManager->GetCookies(Utf8ToWide(url).c_str(), handler);
        handler->Release();
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetCookie(const Cookie& cookie, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        ComPtr<ICoreWebView2_2> webview2;
        ComPtr<ICoreWebView2CookieManager> cookieManager;
        if (FAILED(webview.As(IID_ICoreWebView2_2, webview2))
            || FAILED(webview2->get_CookieManager(cookieManager.Put())) || !cookieManager) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support cookie access");
        }
        ComPtr<ICoreWebView2Cookie> native;
        const HRESULT created = cookieManager->CreateCookie(Utf8ToWide(cookie.name).c_str(),
            Utf8ToWide(cookie.value).c_str(), Utf8ToWide(cookie.domain).c_str(), Utf8ToWide(cookie.path).c_str(),
            native.Put());
        if (FAILED(created) || !native) {
            const Error error(ErrorCode::PLATFORM_FAILURE, "Unable to create the cookie", FormatHResult(created));
            if (callback) {
                callback(error);
            }
            return error;
        }
        native->put_IsSecure(cookie.secure ? TRUE : FALSE);
        native->put_IsHttpOnly(cookie.httpOnly ? TRUE : FALSE);
        if (cookie.expiresUnixSeconds >= 0) {
            native->put_Expires(static_cast<double>(cookie.expiresUnixSeconds));
        }
        switch (cookie.sameSite) {
            case CookieSameSite::NONE:
                native->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE);
                break;
            case CookieSameSite::LAX:
                native->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
                break;
            case CookieSameSite::STRICT_MODE:
                native->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT);
                break;
            case CookieSameSite::UNSPECIFIED:
                break;
        }
        const HRESULT added = cookieManager->AddOrUpdateCookie(native.Get());
        if (FAILED(added)) {
            const Error error(ErrorCode::PLATFORM_FAILURE, "Unable to store the cookie", FormatHResult(added));
            if (callback) {
                callback(error);
            }
            return error;
        }
        if (callback) {
            callback(Result<void>::Ok());
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::DeleteCookie(
        const std::string& url, const std::string& name, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        ComPtr<ICoreWebView2_2> webview2;
        ComPtr<ICoreWebView2CookieManager> cookieManager;
        if (FAILED(webview.As(IID_ICoreWebView2_2, webview2))
            || FAILED(webview2->get_CookieManager(cookieManager.Put())) || !cookieManager) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support cookie access");
        }
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new GetCookiesCompletedHandler(
            [weakAlive, cookieManager, name = Utf8ToWide(name), callback = std::move(callback)](
                HRESULT result, ICoreWebView2CookieList* list) mutable {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                if (FAILED(result) || !list) {
                    if (callback) {
                        callback(Error(ErrorCode::PLATFORM_FAILURE, "Unable to read cookies", FormatHResult(result)));
                    }
                    return;
                }
                UINT count = 0;
                list->get_Count(&count);
                for (UINT index = 0; index < count; ++index) {
                    ComPtr<ICoreWebView2Cookie> native;
                    if (FAILED(list->GetValueAtIndex(index, native.Put())) || !native) {
                        continue;
                    }
                    LPWSTR cookieName = nullptr;
                    native->get_Name(&cookieName);
                    const bool matches = cookieName && name == cookieName;
                    CoTaskMemFree(cookieName);
                    if (matches) {
                        cookieManager->DeleteCookie(native.Get());
                        break;
                    }
                }
                if (callback) {
                    callback(Result<void>::Ok());
                }
            });
        const HRESULT result = cookieManager->GetCookies(Utf8ToWide(url).c_str(), handler);
        handler->Release();
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        ComPtr<ICoreWebView2_13> webview13;
        ComPtr<ICoreWebView2Profile2> profile2;
        if (FAILED(webview.As(IID_ICoreWebView2_13, webview13))) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support clearing browsing data");
        }
        ComPtr<ICoreWebView2Profile> profile;
        if (FAILED(webview13->get_Profile(profile.Put())) || FAILED(profile.As(IID_ICoreWebView2Profile2, profile2))) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support clearing browsing data");
        }
        COREWEBVIEW2_BROWSING_DATA_KINDS nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(0);
        if (HasClearDataKind(kinds, ClearDataKind::COOKIES)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(
                nativeKinds | COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES);
        }
        if (HasClearDataKind(kinds, ClearDataKind::CACHE)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(nativeKinds
                | COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE | COREWEBVIEW2_BROWSING_DATA_KINDS_CACHE_STORAGE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::LOCAL_STORAGE)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(
                nativeKinds | COREWEBVIEW2_BROWSING_DATA_KINDS_LOCAL_STORAGE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::INDEXED_DB)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(
                nativeKinds | COREWEBVIEW2_BROWSING_DATA_KINDS_INDEXED_DB);
        }
        if (HasClearDataKind(kinds, ClearDataKind::SERVICE_WORKERS)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(
                nativeKinds | COREWEBVIEW2_BROWSING_DATA_KINDS_SERVICE_WORKERS);
        }
        if (HasClearDataKind(kinds, ClearDataKind::SESSION_STORAGE) || HasClearDataKind(kinds, ClearDataKind::ALL)) {
            nativeKinds = static_cast<COREWEBVIEW2_BROWSING_DATA_KINDS>(
                nativeKinds | COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_DOM_STORAGE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::ALL)) {
            nativeKinds = COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE;
        }
        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        auto* handler = new ClearBrowsingDataCompletedHandler([weakAlive, callback = std::move(callback)](
                                                                  HRESULT result) mutable {
            if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                return;
            }
            if (!callback) {
                return;
            }
            if (FAILED(result)) {
                callback(Error(ErrorCode::PLATFORM_FAILURE, "Clearing browsing data failed", FormatHResult(result)));
                return;
            }
            callback(Result<void>::Ok());
        });
        const HRESULT result = profile2->ClearBrowsingData(nativeKinds, handler);
        handler->Release();
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetZoom(double factor) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        const HRESULT result = controller->put_ZoomFactor(factor);
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        zoomFactor = factor;
        return Result<void>::Ok();
    }

    Result<double> WindowsWindowBackend::GetZoom() const {
        if (!controller) {
            return zoomFactor;
        }
        double factor = zoomFactor;
        controller->get_ZoomFactor(&factor);
        return factor;
    }

    Result<void> WindowsWindowBackend::FindInPage(
        const std::string& text, const FindOptions& options, std::function<void(Result<FindResult>)> callback) {
        const std::string script =
        "(function(){var needle=" + JSON(text).dump() + ";var caseSensitive=" + (options.matchCase ? "true" : "false") +
        ";var backwards=" + (options.forward ? "false" : "true") +
        ";var count=0;try{var walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);var hay=caseSensitive?needle:needle.toLowerCase();"
        "while(walker.nextNode()){var node=walker.currentNode;var data=node.nodeValue||'';if(!caseSensitive){data=data.toLowerCase();}var index=0;"
        "while((index=data.indexOf(hay,index))!==-1){count++;index+=hay.length||1;}}"
        "if(count>0){var selection=window.getSelection();if(selection){selection.removeAllRanges();}window.find(needle,caseSensitive,backwards,true,false,false,false);}"
        "return {occurrences:count,activeMatch:count>0?1:0};}catch(error){return {occurrences:0,activeMatch:0};}})()";
        return ExecuteScript(script, [callback = std::move(callback)](Result<JSON> result) mutable {
            if (!callback) {
                return;
            }
            if (!result.IsOk()) {
                callback(result.GetError());
                return;
            }
            const JSON& value = result.Value();
            FindResult found;
            if (value.is_object()) {
                const auto occurrences = value.find("occurrences");
                const auto active = value.find("activeMatch");
                if (occurrences != value.end() && occurrences->is_number_integer()) {
                    found.occurrences = occurrences->get<int>();
                }
                if (active != value.end() && active->is_number_integer()) {
                    found.activeMatch = active->get<int>();
                }
            }
            found.finished = true;
            callback(found);
        });
    }

    Result<void> WindowsWindowBackend::StopFindInPage() {
        return ExecuteScript("if(window.getSelection){window.getSelection().removeAllRanges();}", {});
    }

    Result<std::string> WindowsWindowBackend::GetUrl() const {
        if (!webview) {
            return std::string();
        }
        LPWSTR source = nullptr;
        webview->get_Source(&source);
        const std::string url = source ? WideToUtf8(source) : std::string();
        CoTaskMemFree(source);
        return url;
    }

    Result<std::string> WindowsWindowBackend::GetBrowserTitle() const {
        if (!webview) {
            return std::string();
        }
        LPWSTR title = nullptr;
        webview->get_DocumentTitle(&title);
        const std::string value = title ? WideToUtf8(title) : std::string();
        CoTaskMemFree(title);
        return value;
    }

    Result<void> WindowsWindowBackend::SendWebMessage(const JSON& message) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        const std::string serialized = SafeJsonDump(message);
        const HRESULT result = webview->PostWebMessageAsJson(Utf8ToWide(serialized).c_str());
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    Result<void> WindowsWindowBackend::SetUserAgent(const std::string& agent) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings2> settings2;
        if (FAILED(webview->get_Settings(settings.Put()))
            || FAILED(settings.As(IID_ICoreWebView2Settings2, settings2))) {
            return Error(ErrorCode::NOT_SUPPORTED, "This WebView2 runtime does not support changing the user agent");
        }
        const HRESULT result = settings2->put_UserAgent(Utf8ToWide(agent).c_str());
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        userAgent = Utf8ToWide(agent);
        return Result<void>::Ok();
    }

    Result<std::string> WindowsWindowBackend::GetUserAgent() const {
        if (!webview) {
            return WideToUtf8(userAgent);
        }
        ComPtr<ICoreWebView2Settings> settings;
        ComPtr<ICoreWebView2Settings2> settings2;
        if (SUCCEEDED(webview->get_Settings(settings.Put()))
            && SUCCEEDED(settings.As(IID_ICoreWebView2Settings2, settings2))) {
            LPWSTR agent = nullptr;
            settings2->get_UserAgent(&agent);
            const std::string value = agent ? WideToUtf8(agent) : std::string();
            CoTaskMemFree(agent);
            if (!value.empty()) {
                return value;
            }
        }
        return WideToUtf8(userAgent);
    }

    void WindowsWindowBackend::PostWebMessage(const std::string& message) {
        if (webview) {
            webview->PostWebMessageAsJson(Utf8ToWide(message).c_str());
        }
    }

    void WindowsWindowBackend::PostWebMessageToFrame(const std::string& message, const std::string& frameId) {
        const auto iterator = frames.find(frameId);
        if (iterator == frames.end()) {
            return;
        }
        iterator->second->PostWebMessageAsJson(Utf8ToWide(message).c_str());
    }

    std::wstring WindowsWindowBackend::ComposeHeaders(const HttpHeaderMap& headers) const {
        std::wstring result;
        for (const auto& entry : headers) {
            result += Utf8ToWide(entry.first);
            result += L": ";
            result += Utf8ToWide(entry.second);
            result += L"\r\n";
        }
        return result;
    }

    Result<void> WindowsWindowBackend::NavigateCore(const std::wstring& url, bool applyExtraHeaders) {
        if (!webview) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The WebView2 browser is not ready");
        }
        if (applyExtraHeaders && !browserOptions.extraHeaders.empty()) {
            ComPtr<ICoreWebView2_2> webview2;
            ComPtr<ICoreWebView2Environment2> environment2;
            ComPtr<ICoreWebView2WebResourceRequest> request;
            if (SUCCEEDED(webview.As(IID_ICoreWebView2_2, webview2))
                && SUCCEEDED(environment.As(IID_ICoreWebView2Environment2, environment2))) {
                const HRESULT created = environment2->CreateWebResourceRequest(
                    url.c_str(), L"GET", nullptr, ComposeHeaders(browserOptions.extraHeaders).c_str(), request.Put());
                if (SUCCEEDED(created) && request) {
                    const HRESULT navigated = webview2->NavigateWithWebResourceRequest(request.Get());
                    if (FAILED(navigated)) {
                        return Error(ErrorCode::PLATFORM_FAILURE, HResultText(navigated));
                    }
                    return Result<void>::Ok();
                }
            }
        }
        const HRESULT result = webview->Navigate(url.c_str());
        if (FAILED(result)) {
            return Error(ErrorCode::PLATFORM_FAILURE, HResultText(result));
        }
        return Result<void>::Ok();
    }

    void WindowsWindowBackend::HandleTopLevelMessage(ICoreWebView2WebMessageReceivedEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        LPWSTR source = nullptr;
        LPWSTR json = nullptr;
        args->get_Source(&source);
        args->get_WebMessageAsJson(&json);
        const std::string sourceUrl = source ? WideToUtf8(source) : std::string();
        const std::string message = json ? WideToUtf8(json) : std::string("null");
        CoTaskMemFree(source);
        CoTaskMemFree(json);
        delegate->OnBackendWebMessage(message, sourceUrl, true, {});
    }

    void WindowsWindowBackend::HandleFrameMessage(
        ICoreWebView2Frame* frame, ICoreWebView2WebMessageReceivedEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        LPWSTR source = nullptr;
        LPWSTR json = nullptr;
        args->get_Source(&source);
        args->get_WebMessageAsJson(&json);
        const std::string sourceUrl = source ? WideToUtf8(source) : std::string();
        const std::string message = json ? WideToUtf8(json) : std::string("null");
        CoTaskMemFree(source);
        CoTaskMemFree(json);
        delegate->OnBackendWebMessage(message, sourceUrl, false, FrameId(frame));
    }

    void WindowsWindowBackend::HandleWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args) {
        if (!args || !webview || !environment) {
            return;
        }
        ComPtr<ICoreWebView2WebResourceRequest> request;
        if (FAILED(args->get_Request(request.Put())) || !request) {
            return;
        }
        LPWSTR rawUri = nullptr;
        LPWSTR rawMethod = nullptr;
        request->get_Uri(&rawUri);
        request->get_Method(&rawMethod);
        const std::string url = rawUri ? WideToUtf8(rawUri) : std::string();
        const std::string method = rawMethod ? WideToUtf8(rawMethod) : std::string("GET");
        CoTaskMemFree(rawUri);
        CoTaskMemFree(rawMethod);

        HttpHeaderMap headers;
        ComPtr<ICoreWebView2HttpRequestHeaders> requestHeaders;
        if (SUCCEEDED(request->get_Headers(requestHeaders.Put())) && requestHeaders) {
            ComPtr<ICoreWebView2HttpHeadersCollectionIterator> iterator;
            if (SUCCEEDED(requestHeaders->GetIterator(iterator.Put())) && iterator) {
                BOOL hasHeader = FALSE;
                iterator->get_HasCurrentHeader(&hasHeader);
                while (hasHeader) {
                    LPWSTR name = nullptr;
                    LPWSTR value = nullptr;
                    iterator->GetCurrentHeader(&name, &value);
                    if (name) {
                        headers[WideToUtf8(name)] = value ? WideToUtf8(value) : std::string();
                    }
                    CoTaskMemFree(name);
                    CoTaskMemFree(value);
                    BOOL more = FALSE;
                    iterator->MoveNext(&more);
                    hasHeader = more;
                }
            }
        }

        const std::string scheme = LowerScheme(url);
        CustomSchemeHandler customHandler;
        bool hasCustomHandler = false;
        for (const auto& entry : browserOptions.customSchemes) {
            if (EqualsIgnoreCase(entry.first, scheme)) {
                customHandler = entry.second;
                hasCustomHandler = true;
                break;
            }
        }

        const bool filteredRequest = browserOptions.interceptRequests
            && (browserOptions.requestFilters.empty()
                || std::any_of(browserOptions.requestFilters.begin(), browserOptions.requestFilters.end(),
                    [&url](const std::string& pattern) { return MatchesUrlPattern(url, pattern); }));

        auto sendResponse = [&args, this](int status, const std::string& reason, const std::string& body,
                                const std::string& mime, const HttpHeaderMap& extraHeaders) {
            IStream* rawStream = SHCreateMemStream(
                reinterpret_cast<const BYTE*>(body.data()), static_cast<UINT>(body.size()));
            if (!rawStream) {
                return;
            }
            ComPtr<IStream> stream;
            stream.Attach(rawStream);
            std::string headerBlock;
            for (const auto& entry : extraHeaders) {
                headerBlock += entry.first + ": " + entry.second + "\r\n";
            }
            if (!mime.empty()) {
                headerBlock += "Content-Type: " + mime + "\r\n";
            }
            ComPtr<ICoreWebView2WebResourceResponse> response;
            if (SUCCEEDED(environment->CreateWebResourceResponse(stream.Get(), status, Utf8ToWide(reason).c_str(),
                    Utf8ToWide(headerBlock).c_str(), response.Put()))) {
                args->put_Response(response.Get());
            }
        };

        if (!browserOptions.embeddedBundle.empty() && IsEmbeddedWebUrl(url)) {
            Result<CustomSchemeResponse> response = LoadEmbeddedWebResource(browserOptions.embeddedBundle, url);
            if (!response.IsOk()) {
                sendResponse(500, StatusReason(500), response.GetError().message, "text/plain", {});
                return;
            }
            sendResponse(response.Value().statusCode, StatusReason(response.Value().statusCode), response.Value().body,
                response.Value().mimeType, response.Value().headers);
            return;
        }

        if (hasCustomHandler) {
            CustomSchemeRequest customRequest{url, scheme, method, headers};
            Result<CustomSchemeResponse> response = Error(
                ErrorCode::HANDLER_FAILED, "Custom scheme handler was not invoked");
            try {
                response = customHandler(customRequest);
            } catch (const std::exception& exception) {
                response = Error(
                    ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an exception", exception.what());
            } catch (...) {
                response = Error(ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an unknown exception");
            }
            if (!response.IsOk()) {
                sendResponse(500, StatusReason(500), response.GetError().message, "text/plain", {});
                return;
            }
            sendResponse(response.Value().statusCode, StatusReason(response.Value().statusCode), response.Value().body,
                response.Value().mimeType, response.Value().headers);
            return;
        }

        if (!filteredRequest || !delegate) {
            return;
        }

        COREWEBVIEW2_WEB_RESOURCE_CONTEXT context = COREWEBVIEW2_WEB_RESOURCE_CONTEXT_OTHER;
        args->get_ResourceContext(&context);
        const ResourceRequestDecision decision = delegate->OnBackendResourceRequested(url, method,
            ResourceTypeFromContext(context), context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT, headers);
        if (decision.cancel) {
            sendResponse(403, StatusReason(403), "Request blocked", "text/plain", {});
            return;
        }
        if (!decision.redirectUrl.empty()) {
            HttpHeaderMap redirectHeaders{{"Location", decision.redirectUrl}};
            sendResponse(302, StatusReason(302), std::string(), "text/plain", redirectHeaders);
        }
    }

    void WindowsWindowBackend::HandleWebResourceResponse(ICoreWebView2WebResourceResponseReceivedEventArgs* args) {
        if (!args || !delegate || !browserOptions.interceptResponses) {
            return;
        }
        ComPtr<ICoreWebView2WebResourceRequest> request;
        ComPtr<ICoreWebView2WebResourceResponseView> response;
        if (FAILED(args->get_Request(request.Put())) || FAILED(args->get_Response(response.Put())) || !request
            || !response) {
            return;
        }
        LPWSTR rawUri = nullptr;
        request->get_Uri(&rawUri);
        const std::string url = rawUri ? WideToUtf8(rawUri) : std::string();
        CoTaskMemFree(rawUri);
        int status = 0;
        response->get_StatusCode(&status);
        std::string mime;
        HttpHeaderMap headers;
        ComPtr<ICoreWebView2HttpResponseHeaders> responseHeaders;
        if (SUCCEEDED(response->get_Headers(responseHeaders.Put())) && responseHeaders) {
            LPWSTR contentType = nullptr;
            if (SUCCEEDED(responseHeaders->GetHeader(L"Content-Type", &contentType)) && contentType) {
                mime = WideToUtf8(contentType);
            }
            CoTaskMemFree(contentType);
            ComPtr<ICoreWebView2HttpHeadersCollectionIterator> iterator;
            if (SUCCEEDED(responseHeaders->GetIterator(iterator.Put())) && iterator) {
                BOOL hasHeader = FALSE;
                iterator->get_HasCurrentHeader(&hasHeader);
                while (hasHeader) {
                    LPWSTR name = nullptr;
                    LPWSTR value = nullptr;
                    iterator->GetCurrentHeader(&name, &value);
                    if (name) {
                        headers[WideToUtf8(name)] = value ? WideToUtf8(value) : std::string();
                    }
                    CoTaskMemFree(name);
                    CoTaskMemFree(value);
                    BOOL more = FALSE;
                    iterator->MoveNext(&more);
                    hasHeader = more;
                }
            }
        }
        delegate->OnBackendResourceResponse(url, status, mime, headers);
    }

    void WindowsWindowBackend::HandleDownloadStarting(ICoreWebView2DownloadStartingEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        ComPtr<ICoreWebView2DownloadOperation> operation;
        if (FAILED(args->get_DownloadOperation(operation.Put())) || !operation) {
            return;
        }
        DownloadInfo info;
        info.id = PointerToHex(operation.Get());
        LPWSTR uri = nullptr;
        operation->get_Uri(&uri);
        info.url = uri ? WideToUtf8(uri) : std::string();
        CoTaskMemFree(uri);
        LPWSTR mime = nullptr;
        operation->get_MimeType(&mime);
        info.mimeType = mime ? WideToUtf8(mime) : std::string();
        CoTaskMemFree(mime);
        operation->get_TotalBytesToReceive(&info.totalBytes);
        operation->get_BytesReceived(&info.receivedBytes);
        LPWSTR resultPath = nullptr;
        args->get_ResultFilePath(&resultPath);
        const std::filesystem::path suggestedPath = resultPath ? std::filesystem::path(resultPath)
                                                               : std::filesystem::path();
        CoTaskMemFree(resultPath);
        info.suggestedFilename = suggestedPath.filename().empty() ? std::string()
                                                                  : PathToUtf8(suggestedPath.filename());
        if (info.suggestedFilename.empty()) {
            info.suggestedFilename = FileNameFromUrl(info.url);
        }
        info.path = suggestedPath;
        info.state = DownloadState::PENDING;

        DownloadStartDecision decision = delegate->OnBackendDownloadStarting(info);
        if (browserOptions.download.blocked) {
            decision.cancel = true;
        }
        if (!decision.cancel && browserOptions.download.askWhereToSave) {
            std::filesystem::path initial = decision.path;
            if (initial.empty()) {
                initial = browserOptions.download.directory / info.suggestedFilename;
            }
            const std::wstring defaultDirectory = initial.parent_path().empty()
                ? std::wstring()
                : Utf8ToWide(PathToUtf8(initial.parent_path()));
            const std::wstring suggestedName = initial.filename().empty() ? Utf8ToWide(info.suggestedFilename)
                                                                          : Utf8ToWide(PathToUtf8(initial.filename()));
            const std::filesystem::path selected = SelectDownloadPath(suggestedName, defaultDirectory);
            if (selected.empty()) {
                decision.cancel = true;
            } else {
                decision.path = selected;
            }
        }
        if (decision.cancel) {
            args->put_Cancel(TRUE);
            args->put_Handled(TRUE);
            return;
        }
        if (!decision.path.empty()) {
            std::error_code error;
            const std::filesystem::path parent = decision.path.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent, error);
            }
            info.path = decision.path;
            args->put_ResultFilePath(Utf8ToWide(PathToUtf8(decision.path)).c_str());
        }
        args->put_Handled(TRUE);
        info.state = DownloadState::IN_PROGRESS;

        auto record = std::make_shared<DownloadRecord>();
        record->operation = operation;
        record->info = info;
        downloads[info.id] = record;

        std::weak_ptr<std::atomic<bool>> weakAlive = alive;
        std::string identifier = info.id;
        EventRegistrationToken token{};
        auto* bytesHandler = new BytesReceivedChangedHandler(
            [weakAlive, this, identifier](ICoreWebView2DownloadOperation*, IUnknown*) {
                if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                    return;
                }
                const auto iterator = downloads.find(identifier);
                if (iterator == downloads.end()) {
                    return;
                }
                iterator->second->operation->get_BytesReceived(&iterator->second->info.receivedBytes);
                RaiseDownloadProgress(iterator->second);
            });
        operation->add_BytesReceivedChanged(bytesHandler, &token);
        bytesHandler->Release();

        auto* stateHandler = new DownloadStateChangedHandler([weakAlive, this, identifier](
                                                                 ICoreWebView2DownloadOperation*, IUnknown*) {
            if (!weakAlive.lock() || !weakAlive.lock()->load()) {
                return;
            }
            const auto iterator = downloads.find(identifier);
            if (iterator == downloads.end()) {
                return;
            }
            COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
            iterator->second->operation->get_State(&state);
            iterator->second->info.state = DownloadStateFromNative(state);
            iterator->second->operation->get_BytesReceived(&iterator->second->info.receivedBytes);
            if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED || state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
                CompleteDownload(iterator->second);
            } else {
                RaiseDownloadProgress(iterator->second);
            }
        });
        operation->add_StateChanged(stateHandler, &token);
        stateHandler->Release();
    }

    void WindowsWindowBackend::RaiseDownloadProgress(const std::shared_ptr<DownloadRecord>& record) {
        if (!delegate || !record) {
            return;
        }
        delegate->OnBackendDownloadProgress(record->info);
    }

    void WindowsWindowBackend::CompleteDownload(const std::shared_ptr<DownloadRecord>& record) {
        if (!delegate || !record) {
            return;
        }
        LPWSTR resultPath = nullptr;
        record->operation->get_ResultFilePath(&resultPath);
        if (resultPath) {
            record->info.path = std::filesystem::path(resultPath);
        }
        CoTaskMemFree(resultPath);
        Error error;
        if (record->info.state == DownloadState::INTERRUPTED) {
            COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason = COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
            record->operation->get_InterruptReason(&reason);
            error = Error(
                ErrorCode::PLATFORM_FAILURE, "Download was interrupted", std::to_string(static_cast<int>(reason)));
        }
        delegate->OnBackendDownloadCompleted(record->info, error);
    }

    std::filesystem::path WindowsWindowBackend::SelectDownloadPath(
        const std::wstring& suggested, const std::wstring& defaultDirectory) const {
        ComPtr<IFileSaveDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog,
                reinterpret_cast<void**>(dialog.Put())))
            || !dialog) {
            return {};
        }
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetFileName(suggested.c_str());
        if (!defaultDirectory.empty()) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                    defaultDirectory.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(folder.Put())))) {
                dialog->SetDefaultFolder(folder.Get());
            }
        }
        if (FAILED(dialog->Show(window))) {
            return {};
        }
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(item.Put())) || !item) {
            return {};
        }
        LPWSTR path = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
            return {};
        }
        const std::filesystem::path result(path);
        CoTaskMemFree(path);
        return result;
    }

    void WindowsWindowBackend::HandleScriptDialog(ICoreWebView2ScriptDialogOpeningEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        COREWEBVIEW2_SCRIPT_DIALOG_KIND kind = COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT;
        args->get_Kind(&kind);
        ScriptDialogType type = ScriptDialogType::ALERT;
        switch (kind) {
            case COREWEBVIEW2_SCRIPT_DIALOG_KIND_ALERT:
                type = ScriptDialogType::ALERT;
                break;
            case COREWEBVIEW2_SCRIPT_DIALOG_KIND_CONFIRM:
                type = ScriptDialogType::CONFIRM;
                break;
            case COREWEBVIEW2_SCRIPT_DIALOG_KIND_PROMPT:
                type = ScriptDialogType::PROMPT;
                break;
            case COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD:
                type = ScriptDialogType::BEFORE_UNLOAD;
                break;
        }
        LPWSTR message = nullptr;
        LPWSTR defaultText = nullptr;
        args->get_Message(&message);
        args->get_DefaultText(&defaultText);
        const std::string messageText = message ? WideToUtf8(message) : std::string();
        const std::string defaultTextValue = defaultText ? WideToUtf8(defaultText) : std::string();
        CoTaskMemFree(message);
        CoTaskMemFree(defaultText);
        const ScriptDialogDecision decision = delegate->OnBackendScriptDialog(type, messageText, defaultTextValue);
        if (!decision.handled) {
            return;
        }
        if (!decision.text.empty()) {
            args->put_ResultText(Utf8ToWide(decision.text).c_str());
        }
        if (decision.accepted) {
            args->Accept();
        }
    }

    void WindowsWindowBackend::HandleContextMenu(ICoreWebView2ContextMenuRequestedEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        ContextMenuDetails details;
        POINT location{};
        args->get_Location(&location);
        details.x = location.x;
        details.y = location.y;
        ComPtr<ICoreWebView2ContextMenuTarget> target;
        if (SUCCEEDED(args->get_ContextMenuTarget(target.Put())) && target) {
            BOOL editable = FALSE;
            target->get_IsEditable(&editable);
            details.isEditable = editable != FALSE;
            LPWSTR pageUri = nullptr;
            target->get_PageUri(&pageUri);
            details.sourceUrl = pageUri ? WideToUtf8(pageUri) : std::string();
            CoTaskMemFree(pageUri);
            BOOL hasLink = FALSE;
            target->get_HasLinkUri(&hasLink);
            if (hasLink) {
                LPWSTR link = nullptr;
                target->get_LinkUri(&link);
                details.linkUrl = link ? WideToUtf8(link) : std::string();
                CoTaskMemFree(link);
            }
            BOOL hasSource = FALSE;
            target->get_HasSourceUri(&hasSource);
            if (hasSource) {
                LPWSTR source = nullptr;
                target->get_SourceUri(&source);
                details.sourceUrl = source ? WideToUtf8(source) : details.sourceUrl;
                CoTaskMemFree(source);
            }
            BOOL hasSelection = FALSE;
            target->get_HasSelection(&hasSelection);
            if (hasSelection) {
                LPWSTR selection = nullptr;
                target->get_SelectionText(&selection);
                details.selectionText = selection ? WideToUtf8(selection) : std::string();
                CoTaskMemFree(selection);
            }
            COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND kind = COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_PAGE;
            target->get_Kind(&kind);
            details.isImage = kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_IMAGE;
            details.isVideo = kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_VIDEO;
        }
        if (delegate->OnBackendContextMenu(details)) {
            args->put_Handled(TRUE);
        }
    }

    void WindowsWindowBackend::HandleAuthentication(ICoreWebView2BasicAuthenticationRequestedEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        LPWSTR uri = nullptr;
        LPWSTR challenge = nullptr;
        args->get_Uri(&uri);
        args->get_Challenge(&challenge);
        AuthenticationInfo info;
        info.url = uri ? WideToUtf8(uri) : std::string();
        info.realm = challenge ? WideToUtf8(challenge) : std::string();
        info.host = ExtractOrigin(info.url);
        info.scheme = "basic";
        CoTaskMemFree(uri);
        CoTaskMemFree(challenge);
        const AuthenticationDecision decision = delegate->OnBackendAuthenticationRequested(info);
        if (decision.cancel) {
            args->put_Cancel(TRUE);
            return;
        }
        if (!decision.useDefault) {
            ComPtr<ICoreWebView2BasicAuthenticationResponse> response;
            if (SUCCEEDED(args->get_Response(response.Put())) && response) {
                response->put_UserName(Utf8ToWide(decision.username).c_str());
                response->put_Password(Utf8ToWide(decision.password).c_str());
            }
        }
    }

    void WindowsWindowBackend::HandleCertificateError(ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) {
        if (!args || !delegate) {
            return;
        }
        LPWSTR uri = nullptr;
        args->get_RequestUri(&uri);
        COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        args->get_ErrorStatus(&status);
        const std::string url = uri ? WideToUtf8(uri) : std::string();
        CoTaskMemFree(uri);
        const bool allow = delegate->OnBackendCertificateError(url, std::to_string(static_cast<int>(status)));
        args->put_Action(allow ? COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW
                               : COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);
    }
}
