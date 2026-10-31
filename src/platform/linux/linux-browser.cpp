#include "platform/linux/linux-window.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json-utils.h"
#include "core/path-utils.h"
#include "core/url-utils.h"

namespace Flare {
    namespace {
        struct EvaluatePayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            ResultCallback<JSON> callback;
        };

        struct SnapshotPayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            CaptureOptions options;
            std::function<void(Result<std::vector<std::uint8_t>>)> callback;
        };

        struct CookiesPayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            std::function<void(Result<std::vector<Cookie>>)> callback;
        };

        struct AddCookiePayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            CompletionCallback callback;
            SoupCookie* cookie = nullptr;
        };

        struct DeleteLookupPayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            CompletionCallback callback;
            std::string name;
        };

        struct DeleteCookiePayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            CompletionCallback callback;
            SoupCookie* cookie = nullptr;
        };

        struct ClearDataPayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
            CompletionCallback callback;
        };

        struct StartupPayload {
            std::weak_ptr<std::atomic<bool>> alive;
            LinuxWindowBackend* self;
        };

        struct PngWriter {
            std::vector<std::uint8_t>* bytes;
        };

        cairo_status_t WritePngChunk(void* closure, const unsigned char* data, unsigned int length) {
            auto* writer = static_cast<PngWriter*>(closure);
            writer->bytes->insert(writer->bytes->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        }

        CookieSameSite TranslateSameSite(SoupSameSitePolicy policy) {
            switch (policy) {
                case SOUP_SAME_SITE_POLICY_NONE:
                    return CookieSameSite::NONE;
                case SOUP_SAME_SITE_POLICY_LAX:
                    return CookieSameSite::LAX;
                case SOUP_SAME_SITE_POLICY_STRICT:
                    return CookieSameSite::STRICT_MODE;
                default:
                    return CookieSameSite::UNSPECIFIED;
            }
        }

        SoupSameSitePolicy TranslateSameSite(CookieSameSite policy) {
            switch (policy) {
                case CookieSameSite::NONE:
                    return SOUP_SAME_SITE_POLICY_NONE;
                case CookieSameSite::LAX:
                    return SOUP_SAME_SITE_POLICY_LAX;
                case CookieSameSite::STRICT_MODE:
                    return SOUP_SAME_SITE_POLICY_STRICT;
                case CookieSameSite::UNSPECIFIED:
                    break;
            }
            return SOUP_SAME_SITE_POLICY_LAX;
        }
    }

    void LinuxWindowBackend::StartCacheClear() {
        WebKitWebsiteDataManager* manager = DataManager();
        if (!manager) {
            RaiseReady();
            return;
        }
        auto* payload = new StartupPayload{alive, this};
        webkit_website_data_manager_clear(
            manager, WEBKIT_WEBSITE_DATA_ALL, 0, cancellable, &LinuxWindowBackend::OnStartupCacheCleared, payload);
    }

    void LinuxWindowBackend::OnStartupCacheCleared(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<StartupPayload*>(data);
        GError* error = nullptr;
        webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);
        if (error) {
            g_error_free(error);
        }
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            payload->self->RaiseReady();
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::Navigate(const std::string& url) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_load_uri(webView, url.c_str());
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::NavigateToString(const std::string& html, const std::string& baseUrl) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_load_html(webView, html.c_str(), baseUrl.empty() ? nullptr : baseUrl.c_str());
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::LoadFile(const std::filesystem::path& file) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_load_uri(webView, FileUrlFromPath(file).c_str());
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Reload() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_reload(webView);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::GoBack() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_go_back(webView);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::GoForward() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_go_forward(webView);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::Stop() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_stop_loading(webView);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::ExecuteScript(const std::string& script, ResultCallback<JSON> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        auto* payload = new EvaluatePayload{alive, this, std::move(callback)};
        webkit_web_view_evaluate_javascript(webView, script.c_str(), static_cast<gssize>(script.size()), nullptr,
            nullptr, cancellable, &LinuxWindowBackend::OnEvaluateFinished, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnEvaluateFinished(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<EvaluatePayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
            if (payload->callback) {
                if (!value) {
                    const std::string message = error ? error->message : "JavaScript evaluation failed";
                    payload->callback(Error(ErrorCode::PLATFORM_FAILURE, message));
                } else {
                    gchar* json = jsc_value_to_json(value, 0);
                    const std::string text = json ? json : std::string("null");
                    g_free(json);
                    g_object_unref(value);
                    Result<JSON> parsed = ParseJsonValue(text, 16 * 1024 * 1024, 64, 500000);
                    if (parsed.IsOk()) {
                        payload->callback(std::move(parsed.Value()));
                    } else {
                        payload->callback(JSON(text));
                    }
                }
            }
            if (error) {
                g_error_free(error);
            }
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::OpenDevTools() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        WebKitWebInspector* inspector = webkit_web_view_get_inspector(webView);
        if (!inspector) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK inspector is unavailable");
        }
        webkit_web_inspector_show(inspector);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::CloseDevTools() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        WebKitWebInspector* inspector = webkit_web_view_get_inspector(webView);
        if (!inspector) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK inspector is unavailable");
        }
        webkit_web_inspector_close(inspector);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::PrintToPdf(
        const PdfOptions&, const std::filesystem::path&, CompletionCallback callback) {
        const Error error(ErrorCode::NOT_SUPPORTED, "PDF export is not exposed by the WebKitGTK host API");
        if (callback) {
            callback(error);
        }
        return error;
    }

    Result<void> LinuxWindowBackend::Print() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        WebKitPrintOperation* operation = webkit_print_operation_new(webView);
        if (!operation) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create a print operation");
        }
        webkit_print_operation_run_dialog(operation, GTK_WINDOW(window));
        g_object_unref(operation);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::CapturePreview(
        const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        auto* payload = new SnapshotPayload{alive, this, options, std::move(callback)};
        const WebKitSnapshotOptions snapshotOptions = options.transparentBackground
            ? WEBKIT_SNAPSHOT_OPTIONS_TRANSPARENT_BACKGROUND
            : WEBKIT_SNAPSHOT_OPTIONS_NONE;
        webkit_web_view_get_snapshot(webView, WEBKIT_SNAPSHOT_REGION_VISIBLE, snapshotOptions, cancellable,
            &LinuxWindowBackend::OnSnapshotFinished, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnSnapshotFinished(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<SnapshotPayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            cairo_surface_t* surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), result, &error);
            if (!surface) {
                if (payload->callback) {
                    payload->callback(
                        Error(ErrorCode::PLATFORM_FAILURE, error ? error->message : "Page capture failed"));
                }
                if (error) {
                    g_error_free(error);
                }
                delete payload;
                return;
            }
            std::vector<std::uint8_t> bytes;
            if (payload->options.format == ImageFormat::PNG) {
                PngWriter writer{&bytes};
                cairo_surface_write_to_png_stream(surface, &WritePngChunk, &writer);
            } else {
                const int width = cairo_image_surface_get_width(surface);
                const int height = cairo_image_surface_get_height(surface);
                GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
                if (pixbuf) {
                    gchar quality[16]{};
                    std::snprintf(quality, sizeof(quality), "%d", payload->options.quality);
                    gchar* buffer = nullptr;
                    gsize size = 0;
                    GError* saveError = nullptr;
                    if (gdk_pixbuf_save_to_buffer(
                            pixbuf, &buffer, &size, "jpeg", &saveError, "quality", quality, nullptr)) {
                        bytes.assign(
                            reinterpret_cast<std::uint8_t*>(buffer), reinterpret_cast<std::uint8_t*>(buffer) + size);
                    }
                    if (saveError) {
                        g_error_free(saveError);
                    }
                    g_free(buffer);
                    g_object_unref(pixbuf);
                }
            }
            cairo_surface_destroy(surface);
            if (payload->callback) {
                if (bytes.empty()) {
                    payload->callback(Error(ErrorCode::PLATFORM_FAILURE, "Unable to encode the captured image"));
                } else {
                    payload->callback(std::move(bytes));
                }
            }
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::GetCookies(
        const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(context);
        if (!manager) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK cookie manager is unavailable");
        }
        auto* payload = new CookiesPayload{alive, this, std::move(callback)};
        webkit_cookie_manager_get_cookies(
            manager, url.c_str(), cancellable, &LinuxWindowBackend::OnCookiesReceived, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnCookiesReceived(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<CookiesPayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            GList* list = webkit_cookie_manager_get_cookies_finish(WEBKIT_COOKIE_MANAGER(source), result, &error);
            if (error) {
                if (payload->callback) {
                    payload->callback(Error(ErrorCode::PLATFORM_FAILURE, error->message));
                }
                g_error_free(error);
            } else {
                std::vector<Cookie> cookies;
                for (GList* entry = list; entry; entry = entry->next) {
                    auto* native = static_cast<SoupCookie*>(entry->data);
                    Cookie cookie;
                    const gchar* name = soup_cookie_get_name(native);
                    const gchar* value = soup_cookie_get_value(native);
                    const gchar* domain = soup_cookie_get_domain(native);
                    const gchar* path = soup_cookie_get_path(native);
                    cookie.name = name ? name : std::string();
                    cookie.value = value ? value : std::string();
                    cookie.domain = domain ? domain : std::string();
                    cookie.path = path ? path : std::string("/");
                    cookie.secure = soup_cookie_get_secure(native) != FALSE;
                    cookie.httpOnly = soup_cookie_get_http_only(native) != FALSE;
                    GDateTime* expires = soup_cookie_get_expires(native);
                    cookie.expiresUnixSeconds = expires ? static_cast<std::int64_t>(g_date_time_to_unix(expires)) : -1;
                    cookie.sameSite = TranslateSameSite(soup_cookie_get_same_site_policy(native));
                    cookies.push_back(std::move(cookie));
                }
                g_list_free_full(list, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
                if (payload->callback) {
                    payload->callback(std::move(cookies));
                }
            }
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::SetCookie(const Cookie& cookie, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(context);
        if (!manager) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK cookie manager is unavailable");
        }
        const std::string path = cookie.path.empty() ? std::string("/") : cookie.path;
        SoupCookie* native = soup_cookie_new(
            cookie.name.c_str(), cookie.value.c_str(), cookie.domain.c_str(), path.c_str(), -1);
        if (!native) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Unable to create the cookie");
        }
        soup_cookie_set_secure(native, cookie.secure ? TRUE : FALSE);
        soup_cookie_set_http_only(native, cookie.httpOnly ? TRUE : FALSE);
        if (cookie.expiresUnixSeconds >= 0) {
            GDateTime* expires = g_date_time_new_from_unix_utc(static_cast<gint64>(cookie.expiresUnixSeconds));
            if (expires) {
                soup_cookie_set_expires(native, expires);
                g_date_time_unref(expires);
            }
        }
        if (cookie.sameSite != CookieSameSite::UNSPECIFIED) {
            soup_cookie_set_same_site_policy(native, TranslateSameSite(cookie.sameSite));
        }
        auto* payload = new AddCookiePayload{alive, this, std::move(callback), native};
        webkit_cookie_manager_add_cookie(manager, native, cancellable, &LinuxWindowBackend::OnCookieAdded, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnCookieAdded(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<AddCookiePayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            const gboolean added = webkit_cookie_manager_add_cookie_finish(
                WEBKIT_COOKIE_MANAGER(source), result, &error);
            if (payload->callback) {
                if (!added) {
                    payload->callback(
                        Error(ErrorCode::PLATFORM_FAILURE, error ? error->message : "Unable to add the cookie"));
                } else {
                    payload->callback(Result<void>::Ok());
                }
            }
            if (error) {
                g_error_free(error);
            }
        }
        if (payload->cookie) {
            soup_cookie_free(payload->cookie);
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::DeleteCookie(
        const std::string& url, const std::string& name, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(context);
        if (!manager) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK cookie manager is unavailable");
        }
        auto* payload = new DeleteLookupPayload{alive, this, std::move(callback), name};
        webkit_cookie_manager_get_cookies(
            manager, url.c_str(), cancellable, &LinuxWindowBackend::OnDeleteCookieLookup, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnDeleteCookieLookup(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<DeleteLookupPayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            GList* list = webkit_cookie_manager_get_cookies_finish(WEBKIT_COOKIE_MANAGER(source), result, &error);
            if (error) {
                if (payload->callback) {
                    payload->callback(Error(ErrorCode::PLATFORM_FAILURE, error->message));
                }
                g_error_free(error);
                delete payload;
                return;
            }
            SoupCookie* target = nullptr;
            for (GList* entry = list; entry; entry = entry->next) {
                auto* native = static_cast<SoupCookie*>(entry->data);
                const gchar* cookieName = soup_cookie_get_name(native);
                if (cookieName && payload->name == cookieName) {
                    target = soup_cookie_copy(native);
                    break;
                }
            }
            g_list_free_full(list, reinterpret_cast<GDestroyNotify>(soup_cookie_free));
            if (!target) {
                if (payload->callback) {
                    payload->callback(Result<void>::Ok());
                }
                delete payload;
                return;
            }
            auto* deletePayload = new DeleteCookiePayload{
                payload->alive, payload->self, std::move(payload->callback), target};
            WebKitCookieManager* manager = webkit_web_context_get_cookie_manager(payload->self->context);
            webkit_cookie_manager_delete_cookie(
                manager, target, payload->self->cancellable, &LinuxWindowBackend::OnCookieDeleted, deletePayload);
        }
        delete payload;
    }

    void LinuxWindowBackend::OnCookieDeleted(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<DeleteCookiePayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            const gboolean deleted = webkit_cookie_manager_delete_cookie_finish(
                WEBKIT_COOKIE_MANAGER(source), result, &error);
            if (payload->callback) {
                if (!deleted) {
                    payload->callback(
                        Error(ErrorCode::PLATFORM_FAILURE, error ? error->message : "Unable to delete the cookie"));
                } else {
                    payload->callback(Result<void>::Ok());
                }
            }
            if (error) {
                g_error_free(error);
            }
        }
        if (payload->cookie) {
            soup_cookie_free(payload->cookie);
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WebKitWebsiteDataManager* manager = DataManager();
        if (!manager) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The WebKitGTK data manager is unavailable");
        }
        WebKitWebsiteDataTypes types = static_cast<WebKitWebsiteDataTypes>(0);
        if (HasClearDataKind(kinds, ClearDataKind::COOKIES)) {
            types = static_cast<WebKitWebsiteDataTypes>(types | WEBKIT_WEBSITE_DATA_COOKIES);
        }
        if (HasClearDataKind(kinds, ClearDataKind::CACHE)) {
            types = static_cast<WebKitWebsiteDataTypes>(
                types | WEBKIT_WEBSITE_DATA_MEMORY_CACHE | WEBKIT_WEBSITE_DATA_DISK_CACHE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::LOCAL_STORAGE)) {
            types = static_cast<WebKitWebsiteDataTypes>(types | WEBKIT_WEBSITE_DATA_LOCAL_STORAGE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::SESSION_STORAGE)) {
            types = static_cast<WebKitWebsiteDataTypes>(types | WEBKIT_WEBSITE_DATA_SESSION_STORAGE);
        }
        if (HasClearDataKind(kinds, ClearDataKind::INDEXED_DB)) {
            types = static_cast<WebKitWebsiteDataTypes>(types | WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES);
        }
        if (HasClearDataKind(kinds, ClearDataKind::SERVICE_WORKERS)) {
            types = static_cast<WebKitWebsiteDataTypes>(types | WEBKIT_WEBSITE_DATA_SERVICE_WORKER_REGISTRATIONS);
        }
        if (HasClearDataKind(kinds, ClearDataKind::ALL)) {
            types = WEBKIT_WEBSITE_DATA_ALL;
        }
        auto* payload = new ClearDataPayload{alive, this, std::move(callback)};
        webkit_website_data_manager_clear(
            manager, types, 0, cancellable, &LinuxWindowBackend::OnClearDataFinished, payload);
        return Result<void>::Ok();
    }

    void LinuxWindowBackend::OnClearDataFinished(GObject* source, GAsyncResult* result, gpointer data) {
        auto* payload = static_cast<ClearDataPayload*>(data);
        if (const std::shared_ptr<std::atomic<bool>> state = payload->alive.lock(); state && state->load()) {
            GError* error = nullptr;
            const gboolean cleared = webkit_website_data_manager_clear_finish(
                WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);
            if (payload->callback) {
                if (!cleared) {
                    payload->callback(
                        Error(ErrorCode::PLATFORM_FAILURE, error ? error->message : "Unable to clear browsing data"));
                } else {
                    payload->callback(Result<void>::Ok());
                }
            }
            if (error) {
                g_error_free(error);
            }
        }
        delete payload;
    }

    Result<void> LinuxWindowBackend::SetZoom(double factor) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webkit_web_view_set_zoom_level(webView, factor);
        zoomFactor = factor;
        if (delegate) {
            delegate->OnBackendZoomChanged(factor);
        }
        return Result<void>::Ok();
    }

    Result<double> LinuxWindowBackend::GetZoom() const {
        if (!webView) {
            return zoomFactor;
        }
        return webkit_web_view_get_zoom_level(webView);
    }

    Result<void> LinuxWindowBackend::FindInPage(
        const std::string& text, const FindOptions& options, std::function<void(Result<FindResult>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WebKitFindController* controller = webkit_web_view_get_find_controller(webView);
        if (!controller) {
            return Error(ErrorCode::NOT_SUPPORTED, "The WebKitGTK find controller is unavailable");
        }
        if (pendingFind) {
            auto previous = std::move(pendingFind);
            pendingFind = {};
            previous(Error(ErrorCode::CANCELLED, "A new search replaced the previous search"));
        }
        guint32 flags = WEBKIT_FIND_OPTIONS_WRAP_AROUND;
        if (!options.matchCase) {
            flags |= WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE;
        }
        if (!options.forward) {
            flags |= WEBKIT_FIND_OPTIONS_BACKWARDS;
        }
        const guint maxMatches = options.maxMatches > 0 ? static_cast<guint>(options.maxMatches) : G_MAXUINT;
        pendingFind = std::move(callback);
        webkit_find_controller_search(controller, text.c_str(), flags, maxMatches);
        webkit_find_controller_count_matches(controller, text.c_str(), flags, maxMatches);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::StopFindInPage() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        WebKitFindController* controller = webkit_web_view_get_find_controller(webView);
        if (!controller) {
            return Result<void>::Ok();
        }
        if (pendingFind) {
            auto previous = std::move(pendingFind);
            pendingFind = {};
            previous(Error(ErrorCode::CANCELLED, "Find-in-page was stopped"));
        }
        webkit_find_controller_search_finish(controller);
        return Result<void>::Ok();
    }

    Result<std::string> LinuxWindowBackend::GetUrl() const {
        return CurrentUri();
    }

    Result<std::string> LinuxWindowBackend::GetBrowserTitle() const {
        if (!webView) {
            return std::string();
        }
        const gchar* title = webkit_web_view_get_title(webView);
        return title ? std::string(title) : std::string();
    }

    Result<void> LinuxWindowBackend::SendWebMessage(const JSON& message) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        const std::string script = "window.postMessage(" + SafeJsonDump(message) + ",'*');";
        webkit_web_view_evaluate_javascript(
            webView, script.c_str(), static_cast<gssize>(script.size()), nullptr, nullptr, nullptr, nullptr, nullptr);
        return Result<void>::Ok();
    }

    Result<void> LinuxWindowBackend::SetUserAgent(const std::string& agent) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        WebKitSettings* settings = webkit_web_view_get_settings(webView);
        if (!settings) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The WebKitGTK settings are unavailable");
        }
        webkit_settings_set_user_agent(settings, agent.c_str());
        userAgent = agent;
        return Result<void>::Ok();
    }

    Result<std::string> LinuxWindowBackend::GetUserAgent() const {
        if (!webView) {
            return userAgent;
        }
        WebKitSettings* settings = webkit_web_view_get_settings(webView);
        if (!settings) {
            return userAgent;
        }
        const gchar* agent = webkit_settings_get_user_agent(settings);
        return agent ? std::string(agent) : userAgent;
    }

    void LinuxWindowBackend::PostWebMessage(const std::string& message) {
        if (!webView) {
            return;
        }
        const std::string script = "if(window.__flareReceive){window.__flareReceive(" + message + ");}";
        webkit_web_view_evaluate_javascript(
            webView, script.c_str(), static_cast<gssize>(script.size()), nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    void LinuxWindowBackend::PostWebMessageToFrame(const std::string&, const std::string&) {}
}
