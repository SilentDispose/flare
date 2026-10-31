#pragma once

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <WebView2.h>

#include <filesystem>
#include <string>

class WebView2Loader {
public:
    static WebView2Loader& Instance();
    static void SetCacheDirectory(const std::filesystem::path& directory);

    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] const std::string& ErrorMessage() const noexcept;
    HRESULT CreateEnvironment(PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
        ICoreWebView2EnvironmentOptions* options,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) const;

private:
    WebView2Loader();
    WebView2Loader(const WebView2Loader&) = delete;
    WebView2Loader& operator=(const WebView2Loader&) = delete;

    using CreateEnvironmentFunction = HRESULT(STDMETHODCALLTYPE*)(
        PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

    HMODULE module = nullptr;
    CreateEnvironmentFunction createEnvironment = nullptr;
    std::string errorMessage;
};
