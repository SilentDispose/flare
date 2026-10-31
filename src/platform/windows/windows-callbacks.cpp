#include "platform/windows/windows-callbacks.h"

#include <utility>

EnvironmentCompletedHandler::EnvironmentCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE EnvironmentCompletedHandler::Invoke(HRESULT result, ICoreWebView2Environment* environment) {
    if (callback) {
        callback(result, environment);
    }
    return S_OK;
}

const IID& EnvironmentCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
}

ControllerCompletedHandler::ControllerCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ControllerCompletedHandler::Invoke(HRESULT result, ICoreWebView2Controller* controller) {
    if (callback) {
        callback(result, controller);
    }
    return S_OK;
}

const IID& ControllerCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
}

NavigationStartingHandler::NavigationStartingHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE NavigationStartingHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& NavigationStartingHandler::InterfaceId() const {
    return IID_ICoreWebView2NavigationStartingEventHandler;
}

NavigationCompletedHandler::NavigationCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE NavigationCompletedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& NavigationCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2NavigationCompletedEventHandler;
}

SourceChangedHandler::SourceChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE SourceChangedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& SourceChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2SourceChangedEventHandler;
}

ContentLoadingHandler::ContentLoadingHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ContentLoadingHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ContentLoadingHandler::InterfaceId() const {
    return IID_ICoreWebView2ContentLoadingEventHandler;
}

DomContentLoadedHandler::DomContentLoadedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE DomContentLoadedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2DOMContentLoadedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& DomContentLoadedHandler::InterfaceId() const {
    return IID_ICoreWebView2DOMContentLoadedEventHandler;
}

DocumentTitleChangedHandler::DocumentTitleChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE DocumentTitleChangedHandler::Invoke(ICoreWebView2* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& DocumentTitleChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2DocumentTitleChangedEventHandler;
}

FaviconChangedHandler::FaviconChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE FaviconChangedHandler::Invoke(ICoreWebView2* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& FaviconChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2FaviconChangedEventHandler;
}

NewWindowRequestedHandler::NewWindowRequestedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE NewWindowRequestedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& NewWindowRequestedHandler::InterfaceId() const {
    return IID_ICoreWebView2NewWindowRequestedEventHandler;
}

PermissionRequestedHandler::PermissionRequestedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE PermissionRequestedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& PermissionRequestedHandler::InterfaceId() const {
    return IID_ICoreWebView2PermissionRequestedEventHandler;
}

ProcessFailedHandler::ProcessFailedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ProcessFailedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ProcessFailedHandler::InterfaceId() const {
    return IID_ICoreWebView2ProcessFailedEventHandler;
}

WebMessageReceivedHandler::WebMessageReceivedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE WebMessageReceivedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& WebMessageReceivedHandler::InterfaceId() const {
    return IID_ICoreWebView2WebMessageReceivedEventHandler;
}

WebResourceRequestedHandler::WebResourceRequestedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE WebResourceRequestedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& WebResourceRequestedHandler::InterfaceId() const {
    return IID_ICoreWebView2WebResourceRequestedEventHandler;
}

WebResourceResponseReceivedHandler::WebResourceResponseReceivedHandler(Callback handler)
    : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE WebResourceResponseReceivedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2WebResourceResponseReceivedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& WebResourceResponseReceivedHandler::InterfaceId() const {
    return IID_ICoreWebView2WebResourceResponseReceivedEventHandler;
}

DownloadStartingHandler::DownloadStartingHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE DownloadStartingHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& DownloadStartingHandler::InterfaceId() const {
    return IID_ICoreWebView2DownloadStartingEventHandler;
}

BytesReceivedChangedHandler::BytesReceivedChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE BytesReceivedChangedHandler::Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& BytesReceivedChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2BytesReceivedChangedEventHandler;
}

DownloadStateChangedHandler::DownloadStateChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE DownloadStateChangedHandler::Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& DownloadStateChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2StateChangedEventHandler;
}

ScriptDialogOpeningHandler::ScriptDialogOpeningHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ScriptDialogOpeningHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2ScriptDialogOpeningEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ScriptDialogOpeningHandler::InterfaceId() const {
    return IID_ICoreWebView2ScriptDialogOpeningEventHandler;
}

ContainsFullScreenElementChangedHandler::ContainsFullScreenElementChangedHandler(Callback handler)
    : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ContainsFullScreenElementChangedHandler::Invoke(ICoreWebView2* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ContainsFullScreenElementChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2ContainsFullScreenElementChangedEventHandler;
}

ZoomFactorChangedHandler::ZoomFactorChangedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ZoomFactorChangedHandler::Invoke(ICoreWebView2Controller* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ZoomFactorChangedHandler::InterfaceId() const {
    return IID_ICoreWebView2ZoomFactorChangedEventHandler;
}

GotFocusHandler::GotFocusHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE GotFocusHandler::Invoke(ICoreWebView2Controller* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& GotFocusHandler::InterfaceId() const {
    return IID_ICoreWebView2FocusChangedEventHandler;
}

LostFocusHandler::LostFocusHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE LostFocusHandler::Invoke(ICoreWebView2Controller* sender, IUnknown* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& LostFocusHandler::InterfaceId() const {
    return IID_ICoreWebView2FocusChangedEventHandler;
}

AcceleratorKeyPressedHandler::AcceleratorKeyPressedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE AcceleratorKeyPressedHandler::Invoke(
    ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& AcceleratorKeyPressedHandler::InterfaceId() const {
    return IID_ICoreWebView2AcceleratorKeyPressedEventHandler;
}

ContextMenuRequestedHandler::ContextMenuRequestedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ContextMenuRequestedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2ContextMenuRequestedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ContextMenuRequestedHandler::InterfaceId() const {
    return IID_ICoreWebView2ContextMenuRequestedEventHandler;
}

BasicAuthenticationRequestedHandler::BasicAuthenticationRequestedHandler(Callback handler)
    : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE BasicAuthenticationRequestedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2BasicAuthenticationRequestedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& BasicAuthenticationRequestedHandler::InterfaceId() const {
    return IID_ICoreWebView2BasicAuthenticationRequestedEventHandler;
}

ServerCertificateErrorDetectedHandler::ServerCertificateErrorDetectedHandler(Callback handler)
    : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ServerCertificateErrorDetectedHandler::Invoke(
    ICoreWebView2* sender, ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& ServerCertificateErrorDetectedHandler::InterfaceId() const {
    return IID_ICoreWebView2ServerCertificateErrorDetectedEventHandler;
}

ExecuteScriptCompletedHandler::ExecuteScriptCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ExecuteScriptCompletedHandler::Invoke(HRESULT result, LPCWSTR value) {
    if (callback) {
        callback(result, value);
    }
    return S_OK;
}

const IID& ExecuteScriptCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2ExecuteScriptCompletedHandler;
}

AddScriptCompletedHandler::AddScriptCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE AddScriptCompletedHandler::Invoke(HRESULT result, LPCWSTR value) {
    if (callback) {
        callback(result, value);
    }
    return S_OK;
}

const IID& AddScriptCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler;
}

CapturePreviewCompletedHandler::CapturePreviewCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE CapturePreviewCompletedHandler::Invoke(HRESULT result) {
    if (callback) {
        callback(result);
    }
    return S_OK;
}

const IID& CapturePreviewCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2CapturePreviewCompletedHandler;
}

PrintToPdfCompletedHandler::PrintToPdfCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE PrintToPdfCompletedHandler::Invoke(HRESULT result, BOOL success) {
    if (callback) {
        callback(result, success);
    }
    return S_OK;
}

const IID& PrintToPdfCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2PrintToPdfCompletedHandler;
}

GetCookiesCompletedHandler::GetCookiesCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE GetCookiesCompletedHandler::Invoke(HRESULT result, ICoreWebView2CookieList* cookies) {
    if (callback) {
        callback(result, cookies);
    }
    return S_OK;
}

const IID& GetCookiesCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2GetCookiesCompletedHandler;
}

ClearBrowsingDataCompletedHandler::ClearBrowsingDataCompletedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE ClearBrowsingDataCompletedHandler::Invoke(HRESULT result) {
    if (callback) {
        callback(result);
    }
    return S_OK;
}

const IID& ClearBrowsingDataCompletedHandler::InterfaceId() const {
    return IID_ICoreWebView2ClearBrowsingDataCompletedHandler;
}

FrameCreatedHandler::FrameCreatedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE FrameCreatedHandler::Invoke(ICoreWebView2* sender, ICoreWebView2FrameCreatedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& FrameCreatedHandler::InterfaceId() const {
    return IID_ICoreWebView2FrameCreatedEventHandler;
}

FrameDestroyedHandler::FrameDestroyedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE FrameDestroyedHandler::Invoke(ICoreWebView2Frame* sender, IUnknown*) {
    if (callback) {
        callback(sender);
    }
    return S_OK;
}

const IID& FrameDestroyedHandler::InterfaceId() const {
    return IID_ICoreWebView2FrameDestroyedEventHandler;
}

FrameWebMessageReceivedHandler::FrameWebMessageReceivedHandler(Callback handler) : callback(std::move(handler)) {}

HRESULT STDMETHODCALLTYPE FrameWebMessageReceivedHandler::Invoke(
    ICoreWebView2Frame* sender, ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (callback) {
        callback(sender, args);
    }
    return S_OK;
}

const IID& FrameWebMessageReceivedHandler::InterfaceId() const {
    return IID_ICoreWebView2FrameWebMessageReceivedEventHandler;
}
