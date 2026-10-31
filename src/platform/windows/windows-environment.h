#pragma once

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <WebView2.h>

#include <functional>
#include <string>
#include <vector>

#include "flare/error.h"
#include "platform/windows/windows-com.h"

class WebView2EnvironmentOptions final : public ICoreWebView2EnvironmentOptions {
public:
    WebView2EnvironmentOptions(std::wstring arguments, std::wstring language);
    ~WebView2EnvironmentOptions();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* value) override;
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* value) override;
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* value) override;
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* value) override;
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL value) override;

private:
    std::atomic<ULONG> referenceCount{1};
    std::wstring additionalArguments;
    std::wstring language;
    std::wstring targetCompatibleBrowserVersion;
    BOOL allowSingleSignOn = FALSE;
};

using EnvironmentCallback = std::function<void(Flare::Result<ComPtr<ICoreWebView2Environment>>)>;

void RequestWebView2Environment(const std::string& key, const std::wstring& userDataFolder,
    ComPtr<ICoreWebView2EnvironmentOptions> options, EnvironmentCallback callback);
std::string WebView2LoaderErrorMessage();
