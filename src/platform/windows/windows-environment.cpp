#include "platform/windows/windows-environment.h"

#include <windows.h>

#if defined(CreateWindow)
#undef CreateWindow
#endif

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/windows/windows-callbacks.h"
#include "platform/windows/windows-com.h"
#include "platform/windows/webview2-loader.h"

namespace {
    HRESULT CopyWideString(const std::wstring& text, LPWSTR* output) {
        if (!output) {
            return E_POINTER;
        }
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        auto* buffer = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!buffer) {
            *output = nullptr;
            return E_OUTOFMEMORY;
        }
        if (!text.empty()) {
            std::memcpy(buffer, text.data(), text.size() * sizeof(wchar_t));
        }
        buffer[text.size()] = L'\0';
        *output = buffer;
        return S_OK;
    }

    struct EnvironmentEntry {
        ComPtr<ICoreWebView2Environment> environment;
        ComPtr<ICoreWebView2EnvironmentOptions> options;
        bool completed = false;
        bool failed = false;
        Flare::Error error;
        std::vector<EnvironmentCallback> waiters;
    };

    class EnvironmentManager {
    public:
        static EnvironmentManager& Instance() {
            static auto* instance = new EnvironmentManager();
            return *instance;
        }

        void Request(const std::string& key, const std::wstring& userDataFolder,
            ComPtr<ICoreWebView2EnvironmentOptions> options, EnvironmentCallback callback) {
            if (!callback) {
                return;
            }
            std::shared_ptr<EnvironmentEntry> entry;
            bool start = false;
            {
                std::lock_guard<std::mutex> guard(mutex);
                auto iterator = entries.find(key);
                if (iterator == entries.end()) {
                    entry = std::make_shared<EnvironmentEntry>();
                    entry->options = std::move(options);
                    entries.emplace(key, entry);
                    start = true;
                } else {
                    entry = iterator->second;
                }
                if (!entry->completed) {
                    entry->waiters.push_back(std::move(callback));
                    callback = nullptr;
                }
            }
            if (callback) {
                Invoke(callback, entry);
                return;
            }
            if (start) {
                Start(entry, userDataFolder);
            }
        }

    private:
        void Invoke(const EnvironmentCallback& callback, const std::shared_ptr<EnvironmentEntry>& entry) {
            if (entry->failed) {
                callback(entry->error);
            } else {
                callback(entry->environment);
            }
        }

        void Complete(const std::shared_ptr<EnvironmentEntry>& entry, ComPtr<ICoreWebView2Environment> environment,
            Flare::Error error) {
            std::vector<EnvironmentCallback> waiters;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (entry->completed) {
                    return;
                }
                entry->completed = true;
                if (environment) {
                    entry->environment = std::move(environment);
                } else {
                    entry->failed = true;
                    entry->error = std::move(error);
                }
                entry->options.Reset();
                waiters.swap(entry->waiters);
            }
            for (const EnvironmentCallback& waiter : waiters) {
                Invoke(waiter, entry);
            }
        }

        void Start(const std::shared_ptr<EnvironmentEntry>& entry, const std::wstring& userDataFolder) {
            auto* handler = new EnvironmentCompletedHandler(
                [this, entry](HRESULT result, ICoreWebView2Environment* environment) {
                    if (FAILED(result) || !environment) {
                        Flare::Error error(Flare::ErrorCode::PLATFORM_FAILURE, "WebView2 environment creation failed");
                        error.detail = FormatHResult(result);
                        entry->options.Reset();
                        Complete(entry, ComPtr<ICoreWebView2Environment>(), std::move(error));
                        return;
                    }
                    ComPtr<ICoreWebView2Environment> value;
                    value.Attach(environment);
                    value->AddRef();
                    Complete(entry, std::move(value), Flare::Error());
                });

            if (!WebView2Loader::Instance().IsAvailable()) {
                handler->Release();
                Complete(entry, ComPtr<ICoreWebView2Environment>(),
                    Flare::Error(Flare::ErrorCode::PLATFORM_FAILURE, WebView2Loader::Instance().ErrorMessage()));
                return;
            }

            const HRESULT result = WebView2Loader::Instance().CreateEnvironment(
                nullptr, userDataFolder.c_str(), entry->options.Get(), handler);
            handler->Release();
            if (FAILED(result)) {
                Flare::Error error(Flare::ErrorCode::PLATFORM_FAILURE, "Unable to start WebView2 environment creation");
                error.detail = FormatHResult(result);
                Complete(entry, ComPtr<ICoreWebView2Environment>(), std::move(error));
            }
        }

        std::mutex mutex;
        std::unordered_map<std::string, std::shared_ptr<EnvironmentEntry>> entries;
    };
}

WebView2EnvironmentOptions::WebView2EnvironmentOptions(std::wstring arguments, std::wstring locale)
    : additionalArguments(std::move(arguments)),
      language(std::move(locale)),
      targetCompatibleBrowserVersion(L"138.0.3351.48") {}

WebView2EnvironmentOptions::~WebView2EnvironmentOptions() = default;

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::QueryInterface(REFIID riid, void** object) {
    if (!object) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_ICoreWebView2EnvironmentOptions) {
        *object = static_cast<ICoreWebView2EnvironmentOptions*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WebView2EnvironmentOptions::AddRef() {
    return referenceCount.fetch_add(1) + 1;
}

ULONG STDMETHODCALLTYPE WebView2EnvironmentOptions::Release() {
    const ULONG remaining = referenceCount.fetch_sub(1) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::get_AdditionalBrowserArguments(LPWSTR* value) {
    if (additionalArguments.empty()) {
        if (!value) {
            return E_POINTER;
        }
        *value = nullptr;
        return S_OK;
    }
    return CopyWideString(additionalArguments, value);
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::put_AdditionalBrowserArguments(LPCWSTR value) {
    additionalArguments = value ? value : L"";
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::get_Language(LPWSTR* value) {
    if (language.empty()) {
        if (!value) {
            return E_POINTER;
        }
        *value = nullptr;
        return S_OK;
    }
    return CopyWideString(language, value);
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::put_Language(LPCWSTR value) {
    language = value ? value : L"";
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::get_TargetCompatibleBrowserVersion(LPWSTR* value) {
    if (targetCompatibleBrowserVersion.empty()) {
        if (!value) {
            return E_POINTER;
        }
        *value = nullptr;
        return S_OK;
    }
    return CopyWideString(targetCompatibleBrowserVersion, value);
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::put_TargetCompatibleBrowserVersion(LPCWSTR value) {
    targetCompatibleBrowserVersion = value ? value : L"";
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* value) {
    if (!value) {
        return E_POINTER;
    }
    *value = allowSingleSignOn;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebView2EnvironmentOptions::put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL value) {
    allowSingleSignOn = value;
    return S_OK;
}

void RequestWebView2Environment(const std::string& key, const std::wstring& userDataFolder,
    ComPtr<ICoreWebView2EnvironmentOptions> options, EnvironmentCallback callback) {
    EnvironmentManager::Instance().Request(key, userDataFolder, std::move(options), std::move(callback));
}

std::string WebView2LoaderErrorMessage() {
    return WebView2Loader::Instance().ErrorMessage();
}
