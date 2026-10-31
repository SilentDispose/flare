#pragma once

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <WebView2.h>

#include <functional>
#include <utility>

#include "platform/windows/windows-com.h"

class EnvironmentCompletedHandler final
    : public ComCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, ICoreWebView2Environment*)>;
    explicit EnvironmentCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* environment) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ControllerCompletedHandler final : public ComCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, ICoreWebView2Controller*)>;
    explicit ControllerCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class NavigationStartingHandler final : public ComCallback<ICoreWebView2NavigationStartingEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)>;
    explicit NavigationStartingHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class NavigationCompletedHandler final : public ComCallback<ICoreWebView2NavigationCompletedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*)>;
    explicit NavigationCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class SourceChangedHandler final : public ComCallback<ICoreWebView2SourceChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*)>;
    explicit SourceChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ContentLoadingHandler final : public ComCallback<ICoreWebView2ContentLoadingEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs*)>;
    explicit ContentLoadingHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class DomContentLoadedHandler final : public ComCallback<ICoreWebView2DOMContentLoadedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2DOMContentLoadedEventArgs*)>;
    explicit DomContentLoadedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2DOMContentLoadedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class DocumentTitleChangedHandler final : public ComCallback<ICoreWebView2DocumentTitleChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, IUnknown*)>;
    explicit DocumentTitleChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class FaviconChangedHandler final : public ComCallback<ICoreWebView2FaviconChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, IUnknown*)>;
    explicit FaviconChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class NewWindowRequestedHandler final : public ComCallback<ICoreWebView2NewWindowRequestedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*)>;
    explicit NewWindowRequestedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class PermissionRequestedHandler final : public ComCallback<ICoreWebView2PermissionRequestedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*)>;
    explicit PermissionRequestedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ProcessFailedHandler final : public ComCallback<ICoreWebView2ProcessFailedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*)>;
    explicit ProcessFailedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class WebMessageReceivedHandler final : public ComCallback<ICoreWebView2WebMessageReceivedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)>;
    explicit WebMessageReceivedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class WebResourceRequestedHandler final : public ComCallback<ICoreWebView2WebResourceRequestedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*)>;
    explicit WebResourceRequestedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class WebResourceResponseReceivedHandler final
    : public ComCallback<ICoreWebView2WebResourceResponseReceivedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs*)>;
    explicit WebResourceResponseReceivedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, ICoreWebView2WebResourceResponseReceivedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class DownloadStartingHandler final : public ComCallback<ICoreWebView2DownloadStartingEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs*)>;
    explicit DownloadStartingHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class BytesReceivedChangedHandler final : public ComCallback<ICoreWebView2BytesReceivedChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2DownloadOperation*, IUnknown*)>;
    explicit BytesReceivedChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class DownloadStateChangedHandler final : public ComCallback<ICoreWebView2StateChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2DownloadOperation*, IUnknown*)>;
    explicit DownloadStateChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ScriptDialogOpeningHandler final : public ComCallback<ICoreWebView2ScriptDialogOpeningEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2ScriptDialogOpeningEventArgs*)>;
    explicit ScriptDialogOpeningHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2ScriptDialogOpeningEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ContainsFullScreenElementChangedHandler final
    : public ComCallback<ICoreWebView2ContainsFullScreenElementChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, IUnknown*)>;
    explicit ContainsFullScreenElementChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ZoomFactorChangedHandler final : public ComCallback<ICoreWebView2ZoomFactorChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Controller*, IUnknown*)>;
    explicit ZoomFactorChangedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class GotFocusHandler final : public ComCallback<ICoreWebView2FocusChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Controller*, IUnknown*)>;
    explicit GotFocusHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class LostFocusHandler final : public ComCallback<ICoreWebView2FocusChangedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Controller*, IUnknown*)>;
    explicit LostFocusHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender, IUnknown* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class AcceleratorKeyPressedHandler final : public ComCallback<ICoreWebView2AcceleratorKeyPressedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*)>;
    explicit AcceleratorKeyPressedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ContextMenuRequestedHandler final : public ComCallback<ICoreWebView2ContextMenuRequestedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs*)>;
    explicit ContextMenuRequestedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2ContextMenuRequestedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class BasicAuthenticationRequestedHandler final
    : public ComCallback<ICoreWebView2BasicAuthenticationRequestedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2BasicAuthenticationRequestedEventArgs*)>;
    explicit BasicAuthenticationRequestedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ServerCertificateErrorDetectedHandler final
    : public ComCallback<ICoreWebView2ServerCertificateErrorDetectedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2ServerCertificateErrorDetectedEventArgs*)>;
    explicit ServerCertificateErrorDetectedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ExecuteScriptCompletedHandler final : public ComCallback<ICoreWebView2ExecuteScriptCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, LPCWSTR)>;
    explicit ExecuteScriptCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, LPCWSTR value) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class AddScriptCompletedHandler final
    : public ComCallback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, LPCWSTR)>;
    explicit AddScriptCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, LPCWSTR value) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class CapturePreviewCompletedHandler final : public ComCallback<ICoreWebView2CapturePreviewCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT)>;
    explicit CapturePreviewCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class PrintToPdfCompletedHandler final : public ComCallback<ICoreWebView2PrintToPdfCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, BOOL)>;
    explicit PrintToPdfCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, BOOL success) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class GetCookiesCompletedHandler final : public ComCallback<ICoreWebView2GetCookiesCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT, ICoreWebView2CookieList*)>;
    explicit GetCookiesCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2CookieList* cookies) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class ClearBrowsingDataCompletedHandler final : public ComCallback<ICoreWebView2ClearBrowsingDataCompletedHandler> {
public:
    using Callback = std::function<void(HRESULT)>;
    explicit ClearBrowsingDataCompletedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class FrameCreatedHandler final : public ComCallback<ICoreWebView2FrameCreatedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2*, ICoreWebView2FrameCreatedEventArgs*)>;
    explicit FrameCreatedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2FrameCreatedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class FrameDestroyedHandler final : public ComCallback<ICoreWebView2FrameDestroyedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Frame*)>;
    explicit FrameDestroyedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Frame* sender, IUnknown*) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};

class FrameWebMessageReceivedHandler final : public ComCallback<ICoreWebView2FrameWebMessageReceivedEventHandler> {
public:
    using Callback = std::function<void(ICoreWebView2Frame*, ICoreWebView2WebMessageReceivedEventArgs*)>;
    explicit FrameWebMessageReceivedHandler(Callback handler);
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Frame* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override;

private:
    const IID& InterfaceId() const override;
    Callback callback;
};
