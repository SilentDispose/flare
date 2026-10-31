#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <WebKit/WebKit.h>
#import <dispatch/dispatch.h>
#import <uuid/uuid.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/backend.h"
#include "bridge/crypto.h"
#include "core/json-utils.h"
#include "core/path-utils.h"
#include "core/text-utils.h"
#include "core/url-utils.h"

@class FlareWindowDelegate;
@class FlareWebView;
@class FlareSchemeHandler;

namespace Flare {
    struct MacDownloadRecord {
        WKDownload* download = nil;
        NSProgress* progress = nil;
        DownloadInfo info;
        bool observing = false;
    };

    class MacWindowBackend final : public WindowBackend {
    public:
        MacWindowBackend();
        ~MacWindowBackend() override;

        Result<void> Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
            WindowBackendDelegate& delegate, void* parentHandle) override;
        void Close() override;
        void* GetNativeHandle() const override;
        const PlatformCapabilities& Capabilities() const override;

        Result<void> Show() override;
        Result<void> Hide() override;
        Result<void> Minimize() override;
        Result<void> Maximize() override;
        Result<void> Restore() override;
        Result<void> Focus() override;
        Result<void> StartDrag() override;
        Result<void> SetTitle(const std::string& title) override;
        Result<std::string> GetTitle() const override;
        Result<void> SetSize(int width, int height) override;
        Result<void> SetPosition(int x, int y) override;
        Result<void> SetMinSize(int width, int height) override;
        Result<void> SetMaxSize(int width, int height) override;
        Result<void> SetFullscreen(bool fullscreen) override;
        Result<void> SetAlwaysOnTop(bool alwaysOnTop) override;
        Result<void> SetOpacity(double opacity) override;
        Result<double> GetOpacity() const override;
        Result<void> Center() override;
        Result<void> SetBackgroundColor(Color color) override;
        Result<void> SetTheme(Theme theme) override;
        Result<void> SetContentProtection(bool enabled) override;
        Result<void> SetResizable(bool resizable) override;
        Result<void> SetMovable(bool movable) override;
        Result<void> SetMinimizable(bool minimizable) override;
        Result<void> SetMaximizable(bool maximizable) override;
        Result<void> SetClosable(bool closable) override;
        Result<void> SetShowInTaskbar(bool show) override;
        Result<void> SetIcon(const Icon& icon) override;
        Result<void> SetDraggableRegions(const std::vector<Rect>& regions) override;
        Result<Rect> GetBounds() const override;
        bool IsVisible() const override;
        bool IsMaximized() const override;
        bool IsMinimized() const override;
        bool IsFocused() const override;
        bool IsFullscreen() const override;
        double GetDpiScale() const override;

        Result<void> Navigate(const std::string& url) override;
        Result<void> NavigateToString(const std::string& html, const std::string& baseUrl) override;
        Result<void> LoadFile(const std::filesystem::path& file) override;
        Result<void> Reload() override;
        Result<void> GoBack() override;
        Result<void> GoForward() override;
        Result<void> Stop() override;
        Result<void> ExecuteScript(const std::string& script, ResultCallback<JSON> callback) override;
        Result<void> OpenDevTools() override;
        Result<void> CloseDevTools() override;
        Result<void> PrintToPdf(
            const PdfOptions& options, const std::filesystem::path& outputPath, CompletionCallback callback) override;
        Result<void> Print() override;
        Result<void> CapturePreview(
            const CaptureOptions& options, std::function<void(Result<std::vector<std::uint8_t>>)> callback) override;
        Result<void> GetCookies(
            const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) override;
        Result<void> SetCookie(const Cookie& cookie, CompletionCallback callback) override;
        Result<void> DeleteCookie(
            const std::string& url, const std::string& name, CompletionCallback callback) override;
        Result<void> ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) override;
        Result<void> SetZoom(double factor) override;
        Result<double> GetZoom() const override;
        Result<void> FindInPage(const std::string& text, const FindOptions& options,
            std::function<void(Result<FindResult>)> callback) override;
        Result<void> StopFindInPage() override;
        Result<std::string> GetUrl() const override;
        Result<std::string> GetBrowserTitle() const override;
        Result<void> SendWebMessage(const JSON& message) override;
        Result<void> SetUserAgent(const std::string& userAgent) override;
        Result<std::string> GetUserAgent() const override;
        void PostWebMessage(const std::string& message) override;
        void PostWebMessageToFrame(const std::string& message, const std::string& frameId) override;

        void CreateWebView();
        void RaiseReady();
        void RaiseFailure(const Error& error);
        void ReleaseWebView();
        void ApplyStyle();
        void ApplyBackground();
        void UpdateAppearance();
        void HandleAppearanceChanged();
        void HandleTitleChanged();
        void HandleMessage(WKScriptMessage* message, bool lifecycle);
        bool HandleKeyEvent(NSEvent* event, bool down);
        bool HandleMouseEvent(NSEvent* event, MouseEventType type);
        bool HandleScrollEvent(NSEvent* event);
        bool PointInDraggableRegion(NSPoint point) const;
        void HandleFileDrop(NSArray<NSURL*>* urls, NSPoint point);
        void HandleNewWindow(NSString* url);
        void HandleDownloadDestination(
            WKDownload* download, NSURLResponse* response, NSString* suggestedFilename, void (^completion)(NSURL*));
        void UpdateDownloadProgress(NSProgress* progress);
        void FinishDownload(WKDownload* download, NSError* error);
        Result<CustomSchemeResponse> HandleScheme(
            const std::string& scheme, const std::string& url, const std::string& method, const HttpHeaderMap& headers);
        bool ShouldClose();
        void HandleWindowWillClose();
        void HandleNativeFullscreen(bool entered);
        void HandleFocus(bool focused);
        void HandleWindowFrameChanged();
        void HandleDpiChanged();
        Rect ScreenRectFromFrame(NSRect frame) const;
        NSRect FrameFromScreenRect(const Rect& rect) const;
        void AttachParent(bool attach);
        void SetParentInteractive(bool interactive);
        Result<void> RequireWebView() const;

        NSWindow* window = nil;
        NSView* contentView = nil;
        FlareWebView* webView = nil;
        FlareWindowDelegate* delegate = nil;
        NSMutableArray<FlareSchemeHandler*>* schemeHandlers = nil;
        WindowBackendDelegate* listener = nullptr;
        NSWindow* parentWindow = nil;
        std::shared_ptr<std::atomic<bool>> alive;
        WindowOptions options;
        BrowserOptions browserOptions;
        PlatformCapabilities capabilities;
        bool created = false;
        bool destroyed = false;
        bool closing = false;
        bool suppressEvents = false;
        bool visible = false;
        bool focused = false;
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        bool readyRaised = false;
        bool failureRaised = false;
        bool parentDisabled = false;
        bool parentAttached = false;
        bool observersInstalled = false;
        bool appearanceObserverInstalled = false;
        bool titleObserverInstalled = false;
        Theme effectiveTheme = Theme::SYSTEM;
        bool darkMode = false;
        Color currentBackground{30, 30, 30, 255};
        double windowOpacity = 1.0;
        std::string userAgent;
        double zoomFactor = 1.0;
        std::string currentUrl;
        std::string currentTitle;
        std::vector<Rect> draggableRegions;
        NSRect normalFrame{};
        NSUInteger normalStyle = NSWindowStyleMaskTitled;
        bool hasNormalFrame = false;
        NSInteger lastStatusCode = 0;
        std::unordered_map<void*, std::shared_ptr<MacDownloadRecord>> downloads;
        std::unordered_map<void*, std::shared_ptr<MacDownloadRecord>> progressRecords;
    };
}

using namespace Flare;

static char AppearanceContext;
static char TitleContext;
static char DownloadProgressContext;

namespace {
    std::string StringFromObject(id object) {
        if (!object || object == [NSNull null]) {
            return "null";
        }
        if ([object isKindOfClass:[NSString class]]) {
            return std::string([static_cast<NSString*>(object) UTF8String]);
        }
        if ([object isKindOfClass:[NSNumber class]]) {
            NSNumber* number = static_cast<NSNumber*>(object);
            if (std::strcmp(number.objCType, @encode(BOOL)) == 0) {
                return number.boolValue ? "true" : "false";
            }
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%.17g", number.doubleValue);
            return buffer;
        }
        if ([object isKindOfClass:[NSArray class]] || [object isKindOfClass:[NSDictionary class]]) {
            NSData* data = [NSJSONSerialization dataWithJSONObject:object options:0 error:nil];
            if (data) {
                NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                return text ? std::string([text UTF8String]) : std::string("null");
            }
        }
        return "null";
    }

    JSON JsonFromObject(id object) {
        const std::string text = StringFromObject(object);
        Flare::Result<JSON> parsed = Flare::ParseJsonValue(text, 16 * 1024 * 1024, 64, 500000);
        if (parsed.IsOk()) {
            return parsed.Value();
        }
        return JSON(text);
    }

    NSString* ToNSString(const std::string& text) {
        return [NSString stringWithUTF8String:text.c_str()];
    }

    NSColor* ToNSColor(Flare::Color color) {
        return
            [NSColor colorWithSRGBRed:color.r / 255.0 green:color.g / 255.0 blue:color.b / 255.0 alpha:color.a / 255.0];
    }

    std::string ErrorText(NSError* error) {
        return error ? std::string(error.localizedDescription.UTF8String) : std::string("Unknown error");
    }

    std::string MimeTypeForExtension(NSString* path) {
        NSString* extension = path.pathExtension.lowercaseString;
        if ([extension isEqualToString:@"html"] || [extension isEqualToString:@"htm"]) {
            return "text/html";
        }
        if ([extension isEqualToString:@"css"]) {
            return "text/css";
        }
        if ([extension isEqualToString:@"js"] || [extension isEqualToString:@"mjs"]) {
            return "text/javascript";
        }
        if ([extension isEqualToString:@"json"]) {
            return "application/json";
        }
        if ([extension isEqualToString:@"svg"]) {
            return "image/svg+xml";
        }
        if ([extension isEqualToString:@"png"]) {
            return "image/png";
        }
        if ([extension isEqualToString:@"jpg"] || [extension isEqualToString:@"jpeg"]) {
            return "image/jpeg";
        }
        if ([extension isEqualToString:@"gif"]) {
            return "image/gif";
        }
        if ([extension isEqualToString:@"wasm"]) {
            return "application/wasm";
        }
        return "application/octet-stream";
    }

    std::string LifecycleScript() {
        return "(function(){document.addEventListener('DOMContentLoaded',function(){if(window.top!==window){return;}"
               "try{window.webkit.messageHandlers.flareLifecycle.postMessage({type:'dom'});}catch(error){}function "
               "reportIcon(){try{var "
               "link=document.querySelector('link[rel~=\"icon\"]');window.webkit.messageHandlers.flareLifecycle."
               "postMessage({type:'favicon',url:link?link.href:''});}catch(error){}}reportIcon();try{var observer=new "
               "MutationObserver(reportIcon);observer.observe(document.head||document.documentElement,{childList:true,"
               "subtree:true,attributes:true,attributeFilter:['href','rel']});}catch(error){}});})();";
    }
}

@interface FlareWebView : WKWebView
- (instancetype)initWithFrame:(NSRect)frame
                configuration:(WKWebViewConfiguration*)configuration
                      backend:(Flare::MacWindowBackend*)backend;
@end

@implementation FlareWebView {
    Flare::MacWindowBackend* flareBackend;
}

- (instancetype)initWithFrame:(NSRect)frame
                configuration:(WKWebViewConfiguration*)configuration
                      backend:(Flare::MacWindowBackend*)backend {
    self = [super initWithFrame:frame configuration:configuration];
    if (self) {
        flareBackend = backend;
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleKeyEvent(event, true)) {
        return;
    }
    [super keyDown:event];
}

- (void)keyUp:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleKeyEvent(event, false)) {
        return;
    }
    [super keyUp:event];
}

- (void)mouseDown:(NSEvent*)event {
    if (flareBackend && flareBackend->PointInDraggableRegion([self convertPoint:event.locationInWindow fromView:nil])
        && flareBackend->options.movable) {
        (void)flareBackend->StartDrag();
        return;
    }
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::DOWN)) {
        return;
    }
    [super mouseDown:event];
}

- (void)mouseUp:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::UP)) {
        return;
    }
    [super mouseUp:event];
}

- (void)mouseMoved:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::MOVE)) {
        return;
    }
    [super mouseMoved:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::MOVE)) {
        return;
    }
    [super mouseDragged:event];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::DOWN)) {
        return;
    }
    [super rightMouseDown:event];
}

- (void)rightMouseUp:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::UP)) {
        return;
    }
    [super rightMouseUp:event];
}

- (void)otherMouseDown:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::DOWN)) {
        return;
    }
    [super otherMouseDown:event];
}

- (void)otherMouseUp:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleMouseEvent(event, MouseEventType::UP)) {
        return;
    }
    [super otherMouseUp:event];
}

- (void)scrollWheel:(NSEvent*)event {
    if (flareBackend && flareBackend->HandleScrollEvent(event)) {
        return;
    }
    [super scrollWheel:event];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if (flareBackend) {
        NSPasteboard* pasteboard = [sender draggingPasteboard];
        NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                          options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
        if (urls.count > 0) {
            NSPoint point = [self convertPoint:[sender draggingLocation] fromView:nil];
            flareBackend->HandleFileDrop(urls, point);
        }
    }
    return [super performDragOperation:sender] || YES;
}

@end

@interface FlareWindowDelegate
    : NSObject <NSWindowDelegate, WKNavigationDelegate, WKUIDelegate, WKScriptMessageHandler, WKDownloadDelegate>
- (instancetype)initWithBackend:(Flare::MacWindowBackend*)backend;
@end

@implementation FlareWindowDelegate {
    Flare::MacWindowBackend* backend;
}

- (instancetype)initWithBackend:(Flare::MacWindowBackend*)windowBackend {
    self = [super init];
    if (self) {
        backend = windowBackend;
    }
    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    return backend ? backend->ShouldClose() : YES;
}

- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleWindowWillClose();
    }
}

- (void)windowDidMove:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleWindowFrameChanged();
    }
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleWindowFrameChanged();
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    (void)notification;
    if (backend && !backend->minimized) {
        backend->minimized = true;
        if (!backend->suppressEvents && backend->listener) {
            backend->listener->OnBackendMinimized();
        }
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    (void)notification;
    if (backend && backend->minimized) {
        backend->minimized = false;
        if (!backend->suppressEvents && backend->listener) {
            backend->listener->OnBackendRestored();
        }
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleFocus(true);
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleFocus(false);
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleDpiChanged();
    }
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleNativeFullscreen(true);
    }
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
    (void)notification;
    if (backend) {
        backend->HandleNativeFullscreen(false);
    }
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
    (void)keyPath;
    (void)change;
    if (!backend) {
        return;
    }
    if (context == &AppearanceContext) {
        backend->HandleAppearanceChanged();
    } else if (context == &TitleContext) {
        backend->HandleTitleChanged();
    } else if (context == &DownloadProgressContext) {
        backend->UpdateDownloadProgress((NSProgress*)object);
    }
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    if (!backend) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    NSURL* target = navigationAction.request.URL;
    if (!target) {
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    const bool isMainFrame = navigationAction.targetFrame ? navigationAction.targetFrame.isMainFrame : true;
    const std::string url = target.absoluteString.UTF8String ? target.absoluteString.UTF8String : "";
    const std::string method = navigationAction.request.HTTPMethod ? navigationAction.request.HTTPMethod.UTF8String
                                                                   : "GET";
    const NavigationDecision decision = backend->listener
        ? backend->listener->OnBackendNavigationStarting(
              url, method, false, isMainFrame, false, navigationAction.navigationType == WKNavigationTypeLinkActivated)
        : NavigationDecision{true, {}};
    if (decision.allow) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    decisionHandler(WKNavigationActionPolicyCancel);
    if (!decision.redirectUrl.empty() && backend->webView) {
        NSURL* redirect = [NSURL URLWithString:ToNSString(decision.redirectUrl)];
        if (redirect) {
            [backend->webView loadRequest:[NSURLRequest requestWithURL:redirect]];
        }
    }
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)navigationResponse
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
    (void)webView;
    if (navigationResponse.isForMainFrame) {
        if ([navigationResponse.response isKindOfClass:[NSHTTPURLResponse class]]) {
            backend->lastStatusCode = static_cast<NSInteger>(
                [static_cast<NSHTTPURLResponse*>(navigationResponse.response) statusCode]);
        }
    }
    decisionHandler(WKNavigationResponsePolicyAllow);
}

- (void)webView:(WKWebView*)webView
    navigationResponse:(WKNavigationResponse*)navigationResponse
     didBecomeDownload:(WKDownload*)download {
    (void)webView;
    (void)navigationResponse;
    download.delegate = self;
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation {
    (void)webView;
    (void)navigation;
    if (backend && backend->listener) {
        backend->listener->OnBackendContentLoading(false, true);
    }
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation {
    (void)navigation;
    if (backend) {
        NSURL* url = webView.URL;
        backend->currentUrl = url.absoluteString.UTF8String ? url.absoluteString.UTF8String : "";
        if (backend->listener) {
            backend->listener->OnBackendSourceChanged(backend->currentUrl, true, true);
        }
    }
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)navigation;
    if (backend) {
        NSURL* url = webView.URL;
        backend->currentUrl = url.absoluteString.UTF8String ? url.absoluteString.UTF8String : "";
        if (backend->listener) {
            backend->listener->OnBackendNavigationCompleted(
                backend->currentUrl, true, static_cast<int>(backend->lastStatusCode), Error());
        }
        backend->lastStatusCode = 0;
    }
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView;
    (void)navigation;
    if (backend && backend->listener) {
        backend->listener->OnBackendNavigationCompleted(
            backend->currentUrl, false, 0, Error(ErrorCode::NAVIGATION_BLOCKED, ErrorText(error)));
    }
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView;
    (void)navigation;
    if (backend && backend->listener) {
        backend->listener->OnBackendNavigationCompleted(
            backend->currentUrl, false, 0, Error(ErrorCode::NAVIGATION_BLOCKED, ErrorText(error)));
    }
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
                    completionHandler:
                        (void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))completionHandler {
    (void)webView;
    if (!backend || !backend->listener) {
        completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
        return;
    }
    const std::string host = challenge.protectionSpace.host.UTF8String ? challenge.protectionSpace.host.UTF8String : "";
    if ([challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust]) {
        const bool allow = backend->listener->OnBackendCertificateError(
            backend->currentUrl, "Server trust evaluation failed");
        if (allow && challenge.protectionSpace.serverTrust) {
            NSURLCredential* credential = [NSURLCredential credentialForTrust:challenge.protectionSpace.serverTrust];
            completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
        } else {
            completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
        }
        return;
    }
    AuthenticationInfo info;
    info.host = host;
    info.realm = challenge.protectionSpace.realm ? challenge.protectionSpace.realm.UTF8String : "";
    info.scheme = challenge.protectionSpace.authenticationMethod.UTF8String
        ? challenge.protectionSpace.authenticationMethod.UTF8String
        : "";
    info.isProxy = challenge.protectionSpace.isProxy;
    info.port = static_cast<int>(challenge.protectionSpace.port);
    const AuthenticationDecision decision = backend->listener->OnBackendAuthenticationRequested(info);
    if (decision.cancel) {
        completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    } else if (decision.useDefault) {
        completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    } else {
        NSURLCredential* credential = [NSURLCredential credentialWithUser:ToNSString(decision.username)
                                                                 password:ToNSString(decision.password)
                                                              persistence:NSURLCredentialPersistenceNone];
        completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
    }
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures {
    (void)webView;
    (void)configuration;
    (void)windowFeatures;
    if (backend && navigationAction.request.URL) {
        backend->HandleNewWindow(navigationAction.request.URL.absoluteString);
    }
    return nil;
}

- (void)userContentController:(WKUserContentController*)userContentController
      didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)userContentController;
    if (!backend) {
        return;
    }
    backend->HandleMessage(message, [message.name isEqualToString:@"flareLifecycle"]);
}

- (void)webView:(WKWebView*)webView
    runJavaScriptAlertPanelWithMessage:(NSString*)message
                      initiatedByFrame:(WKFrameInfo*)frame
                     completionHandler:(void (^)(void))completionHandler {
    (void)webView;
    (void)frame;
    if (!backend || !backend->listener) {
        completionHandler();
        return;
    }
    ScriptDialogDecision decision = backend->listener->OnBackendScriptDialog(
        ScriptDialogType::ALERT, message.UTF8String ? message.UTF8String : "", "");
    if (!decision.handled) {
        if (backend->browserOptions.defaultScriptDialogsEnabled) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = message;
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
        }
    }
    completionHandler();
}

- (void)webView:(WKWebView*)webView
    runJavaScriptConfirmPanelWithMessage:(NSString*)message
                        initiatedByFrame:(WKFrameInfo*)frame
                       completionHandler:(void (^)(BOOL))completionHandler {
    (void)webView;
    (void)frame;
    if (!backend || !backend->listener) {
        completionHandler(NO);
        return;
    }
    ScriptDialogDecision decision = backend->listener->OnBackendScriptDialog(
        ScriptDialogType::CONFIRM, message.UTF8String ? message.UTF8String : "", "");
    if (!decision.handled) {
        if (backend->browserOptions.defaultScriptDialogsEnabled) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = message;
            [alert addButtonWithTitle:@"OK"];
            [alert addButtonWithTitle:@"Cancel"];
            decision.accepted = [alert runModal] == NSAlertFirstButtonReturn;
        } else {
            decision.accepted = false;
        }
    }
    completionHandler(decision.accepted ? YES : NO);
}

- (void)webView:(WKWebView*)webView
    runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
                              defaultText:(NSString*)defaultText
                         initiatedByFrame:(WKFrameInfo*)frame
                        completionHandler:(void (^)(NSString*))completionHandler {
    (void)webView;
    (void)frame;
    if (!backend || !backend->listener) {
        completionHandler(nil);
        return;
    }
    const std::string defaultString = defaultText ? defaultText.UTF8String : "";
    ScriptDialogDecision decision = backend->listener->OnBackendScriptDialog(
        ScriptDialogType::PROMPT, prompt.UTF8String ? prompt.UTF8String : "", defaultString);
    if (!decision.handled) {
        if (backend->browserOptions.defaultScriptDialogsEnabled) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = prompt;
            [alert addButtonWithTitle:@"OK"];
            [alert addButtonWithTitle:@"Cancel"];
            NSTextField* input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
            input.stringValue = defaultText ? defaultText : @"";
            alert.accessoryView = input;
            if ([alert runModal] == NSAlertFirstButtonReturn) {
                completionHandler(input.stringValue);
            } else {
                completionHandler(nil);
            }
            return;
        }
        completionHandler(nil);
        return;
    }
    completionHandler(decision.accepted ? ToNSString(decision.text) : nil);
}

- (void)webView:(WKWebView*)webView
    runBeforeUnloadConfirmPanelWithMessage:(NSString*)message
                          initiatedByFrame:(WKFrameInfo*)frame
                         completionHandler:(void (^)(BOOL))completionHandler {
    (void)webView;
    (void)frame;
    if (!backend || !backend->listener) {
        completionHandler(YES);
        return;
    }
    ScriptDialogDecision decision = backend->listener->OnBackendScriptDialog(
        ScriptDialogType::BEFORE_UNLOAD, message.UTF8String ? message.UTF8String : "", "");
    if (!decision.handled) {
        if (backend->browserOptions.defaultScriptDialogsEnabled) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = message;
            [alert addButtonWithTitle:@"Leave"];
            [alert addButtonWithTitle:@"Stay"];
            decision.accepted = [alert runModal] == NSAlertFirstButtonReturn;
        } else {
            decision.accepted = true;
        }
    }
    completionHandler(decision.accepted ? YES : NO);
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                      type:(WKMediaCaptureType)type
                           decisionHandler:(void (^)(WKPermissionDecision))decisionHandler {
    (void)webView;
    (void)frame;
    if (!backend || !backend->listener) {
        decisionHandler(WKPermissionDecisionDeny);
        return;
    }
    PermissionKind kind = PermissionKind::OTHER;
    if (type == WKMediaCaptureTypeCamera) {
        kind = PermissionKind::CAMERA;
    } else if (type == WKMediaCaptureTypeMicrophone) {
        kind = PermissionKind::MICROPHONE;
    } else {
        kind = PermissionKind::CAMERA;
    }
    const std::string url = [NSString
        stringWithFormat:@"%@://%@:%ld", origin.protocol, origin.host, static_cast<long>(origin.port)]
                                .UTF8String;
    const PermissionDecision decision = backend->listener->OnBackendPermissionRequested(kind, url, url);
    decisionHandler(decision.allow ? WKPermissionDecisionGrant : WKPermissionDecisionDeny);
}

- (void)webView:(WKWebView*)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
              initiatedByFrame:(WKFrameInfo*)frame
             completionHandler:(void (^)(NSArray<NSURL*>*))completionHandler {
    (void)webView;
    (void)frame;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.canChooseDirectories = parameters.allowsDirectories;
    if ([panel runModal] == NSModalResponseOK) {
        completionHandler(panel.URLs);
    } else {
        completionHandler(nil);
    }
}

- (void)download:(WKDownload*)download
    decideDestinationUsingResponse:(NSURLResponse*)response
                 suggestedFilename:(NSString*)suggestedFilename
                 completionHandler:(void (^)(NSURL*))completionHandler {
    if (backend) {
        backend->HandleDownloadDestination(download, response, suggestedFilename, completionHandler);
    } else {
        completionHandler(nil);
    }
}

- (void)downloadDidFinish:(WKDownload*)download {
    if (backend) {
        backend->FinishDownload(download, nil);
    }
}

- (void)download:(WKDownload*)download didFailWithError:(NSError*)error resumeData:(NSData*)resumeData {
    (void)resumeData;
    if (backend) {
        backend->FinishDownload(download, error);
    }
}

@end

@interface FlareSchemeHandler : NSObject <WKURLSchemeHandler>
- (instancetype)initWithBackend:(Flare::MacWindowBackend*)backend scheme:(NSString*)scheme;
@end

@implementation FlareSchemeHandler {
    Flare::MacWindowBackend* flareBackend;
    NSString* schemeName;
}

- (instancetype)initWithBackend:(Flare::MacWindowBackend*)backend scheme:(NSString*)scheme {
    self = [super init];
    if (self) {
        flareBackend = backend;
        schemeName = [scheme copy];
    }
    return self;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    NSURLRequest* request = urlSchemeTask.request;
    if (!flareBackend || !request.URL) {
        NSError* error = [NSError errorWithDomain:@"FlareScheme"
                                             code:1
                                         userInfo:@{NSLocalizedDescriptionKey : @"Invalid scheme request"}];
        [urlSchemeTask didFailWithError:error];
        return;
    }
    Flare::HttpHeaderMap headers;
    for (NSString* key in request.allHTTPHeaderFields) {
        headers[std::string(key.UTF8String)] = std::string(request.allHTTPHeaderFields[key].UTF8String);
    }
    const std::string url = request.URL.absoluteString.UTF8String ? request.URL.absoluteString.UTF8String : "";
    const std::string method = request.HTTPMethod ? request.HTTPMethod.UTF8String : "GET";
    Flare::Result<Flare::CustomSchemeResponse> result = flareBackend->HandleScheme(
        std::string(schemeName.UTF8String), url, method, headers);
    if (!result.IsOk()) {
        NSError* error = [NSError errorWithDomain:@"FlareScheme"
                                             code:2
                                         userInfo:@{NSLocalizedDescriptionKey : ToNSString(result.GetError().message)}];
        [urlSchemeTask didFailWithError:error];
        return;
    }
    const Flare::CustomSchemeResponse& response = result.Value();
    NSMutableDictionary* headerFields = [NSMutableDictionary dictionary];
    for (const auto& entry : response.headers) {
        headerFields[ToNSString(entry.first)] = ToNSString(entry.second);
    }
    if (!response.mimeType.empty()) {
        headerFields[@"Content-Type"] = ToNSString(response.mimeType);
    }
    NSHTTPURLResponse* httpResponse = [[NSHTTPURLResponse alloc] initWithURL:request.URL
                                                                  statusCode:response.statusCode
                                                                 HTTPVersion:@"HTTP/1.1"
                                                                headerFields:headerFields];
    NSData* data = [NSData dataWithBytes:response.body.data() length:response.body.size()];
    [urlSchemeTask didReceiveResponse:httpResponse];
    [urlSchemeTask didReceiveData:data];
    [urlSchemeTask didFinish];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    (void)urlSchemeTask;
}

@end

namespace Flare {
    MacWindowBackend::MacWindowBackend() : alive(std::make_shared<std::atomic<bool>>(true)) {
        capabilities.platformName = "macos";
        capabilities.multiWindow = true;
        capabilities.nativeKeyEvents = true;
        capabilities.nativeMouseEvents = true;
        capabilities.nativeWheelEvents = true;
        capabilities.fileDrop = true;
        capabilities.transparentWindows = true;
        capabilities.windowOpacity = true;
        capabilities.roundedCorners = true;
        capabilities.borderlessShadow = true;
        capabilities.contentProtection = true;
        capabilities.customTitleBar = true;
        capabilities.draggableRegions = true;
        capabilities.virtualHostScheme = true;
        capabilities.customSchemes = true;
        capabilities.headerInjection = true;
        capabilities.downloads = true;
        capabilities.printToPdf = true;
        capabilities.print = true;
        capabilities.capturePreview = true;
        capabilities.findInPage = true;
        capabilities.cookies = true;
        capabilities.clearBrowsingData = true;
        capabilities.incognito = true;
        capabilities.perWindowDataStore = true;
        capabilities.windowFullscreen = true;
        capabilities.permissionRequests = true;
        capabilities.scriptDialogs = true;
        capabilities.httpAuthentication = true;
        capabilities.certificateErrors = true;
        capabilities.webMessages = true;
        capabilities.devTools = true;
        capabilities.devToolsControl = true;
        capabilities.pinchZoom = true;
        capabilities.zoomControl = true;
        capabilities.dpiEvents = true;
        capabilities.themeEvents = true;
        capabilities.systemThemeChange = true;
        capabilities.documentTitleEvents = true;
        capabilities.faviconEvents = true;
        capabilities.domContentLoadedEvents = true;
    }

    MacWindowBackend::~MacWindowBackend() {
        alive->store(false);
        suppressEvents = true;
        if (window) {
            [window setDelegate:nil];
            if (appearanceObserverInstalled) {
                [window removeObserver:delegate forKeyPath:@"effectiveAppearance" context:&AppearanceContext];
                appearanceObserverInstalled = false;
            }
            ReleaseWebView();
            [window close];
            window = nil;
        }
        SetParentInteractive(true);
        delegate = nil;
        schemeHandlers = nil;
        downloads.clear();
        progressRecords.clear();
    }

    Result<void> MacWindowBackend::Create(const WindowOptions& windowOptions, const BrowserOptions& browserOptions,
        WindowBackendDelegate& backendDelegate, void* parentHandle) {
        options = windowOptions;
        this->browserOptions = browserOptions;
        listener = &backendDelegate;
        parentWindow = (__bridge NSWindow*)parentHandle;
        effectiveTheme = options.theme;
        if (effectiveTheme == Theme::SYSTEM) {
            darkMode = [[NSApp.effectiveAppearance
                bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]]
                isEqualToString:NSAppearanceNameDarkAqua];
        } else {
            darkMode = effectiveTheme == Theme::DARK;
        }
        currentBackground = options.backgroundColor;
        windowOpacity = options.opacity;
        userAgent = browserOptions.userAgent;
        zoomFactor = browserOptions.zoomFactor;
        draggableRegions = options.draggableRegions;

        const bool custom = options.frameless || options.titleBarStyle != TitleBarStyle::NATIVE;
        NSUInteger style = 0;
        if (custom) {
            if (options.frameless || options.titleBarStyle == TitleBarStyle::HIDDEN) {
                style = NSWindowStyleMaskBorderless;
                if (options.resizable) {
                    style |= NSWindowStyleMaskResizable;
                }
            } else {
                style = NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView | NSWindowStyleMaskClosable
                    | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
            }
        } else {
            style = NSWindowStyleMaskTitled;
            if (options.closable) {
                style |= NSWindowStyleMaskClosable;
            }
            if (options.minimizable) {
                style |= NSWindowStyleMaskMiniaturizable;
            }
            if (options.resizable) {
                style |= NSWindowStyleMaskResizable;
            }
        }

        const NSRect contentRect = NSMakeRect(0, 0, options.width, options.height);
        window = [[NSWindow alloc] initWithContentRect:contentRect
                                             styleMask:style
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the NSWindow");
        }
        window.acceptsMouseMovedEvents = YES;
        window.title = ToNSString(options.title);
        window.opaque = !options.transparent;
        window.hasShadow = options.borderlessShadow;
        window.alphaValue = windowOpacity;
        window.movable = options.movable;
        window.movableByWindowBackground = NO;
        window.contentMinSize = NSMakeSize(options.minWidth, options.minHeight);
        window.contentMaxSize = NSMakeSize(options.maxWidth > 0 ? options.maxWidth : CGFLOAT_MAX,
            options.maxHeight > 0 ? options.maxHeight : CGFLOAT_MAX);
        window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        contentView = window.contentView;
        contentView.wantsLayer = YES;
        contentView.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        window.backgroundColor = ToNSColor(currentBackground);
        if (options.alwaysOnTop) {
            window.level = NSFloatingWindowLevel;
        }
        if (options.theme == Theme::DARK) {
            window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        } else if (options.theme == Theme::LIGHT) {
            window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        }
        if (parentWindow) {
            window.level = parentWindow.level;
        }
        ApplyStyle();
        ApplyBackground();

        delegate = [[FlareWindowDelegate alloc] initWithBackend:this];
        window.delegate = delegate;
        [window addObserver:delegate forKeyPath:@"effectiveAppearance" options:0 context:&AppearanceContext];
        appearanceObserverInstalled = true;
        observersInstalled = true;

        if (options.hasPosition) {
            const NSRect frame = FrameFromScreenRect(
                Rect{static_cast<double>(options.x), static_cast<double>(options.y), static_cast<double>(options.width),
                    static_cast<double>(options.height)});
            [window setFrame:frame display:NO];
        } else if (options.centered) {
            [window center];
        }
        if (options.contentProtection) {
            window.sharingType = NSWindowSharingNone;
        }
        if (options.fullscreen) {
            (void)SetFullscreen(true);
        } else if (options.maximizeOnStart) {
            (void)Maximize();
        }

        created = true;
        if (listener) {
            listener->OnBackendCreated();
        }
        MacWindowBackend* self = this;
        std::shared_ptr<std::atomic<bool>> guard = alive;
        dispatch_async(dispatch_get_main_queue(), ^{
          if (guard->load()) {
              self->CreateWebView();
          }
        });
        return Result<void>::Ok();
    }

    void MacWindowBackend::ApplyStyle() {
        if (!window) {
            return;
        }
        const bool custom = options.frameless || options.titleBarStyle != TitleBarStyle::NATIVE;
        NSUInteger style = static_cast<NSUInteger>(window.styleMask);
        if (custom) {
            if (options.frameless || options.titleBarStyle == TitleBarStyle::HIDDEN) {
                style = NSWindowStyleMaskBorderless;
                if (options.resizable) {
                    style |= NSWindowStyleMaskResizable;
                }
            } else {
                style = NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView;
                if (options.closable) {
                    style |= NSWindowStyleMaskClosable;
                }
                if (options.minimizable) {
                    style |= NSWindowStyleMaskMiniaturizable;
                }
                if (options.resizable) {
                    style |= NSWindowStyleMaskResizable;
                }
            }
            window.titlebarAppearsTransparent = YES;
            window.titleVisibility = NSWindowTitleHidden;
        } else {
            style = NSWindowStyleMaskTitled;
            if (options.closable) {
                style |= NSWindowStyleMaskClosable;
            }
            if (options.minimizable) {
                style |= NSWindowStyleMaskMiniaturizable;
            }
            if (options.resizable) {
                style |= NSWindowStyleMaskResizable;
            }
            window.titlebarAppearsTransparent = NO;
            window.titleVisibility = NSWindowTitleVisible;
        }
        window.styleMask = static_cast<NSWindowStyleMask>(style);
        window.movable = options.movable;
        window.hasShadow = options.borderlessShadow;
        [[window standardWindowButton:NSWindowCloseButton] setEnabled:options.closable ? YES : NO];
        [[window standardWindowButton:NSWindowMiniaturizeButton] setEnabled:options.minimizable ? YES : NO];
        [[window standardWindowButton:NSWindowZoomButton] setEnabled:options.maximizable ? YES : NO];
        if (options.titleBarStyle == TitleBarStyle::HIDDEN || options.frameless) {
            [[window standardWindowButton:NSWindowCloseButton] setHidden:YES];
            [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
            [[window standardWindowButton:NSWindowZoomButton] setHidden:YES];
        }
        if (contentView) {
            contentView.layer.cornerRadius = (custom && options.roundedCorners) ? 10.0 : 0.0;
            contentView.layer.masksToBounds = (custom && options.roundedCorners) ? YES : NO;
        }
    }

    void MacWindowBackend::ApplyBackground() {
        if (!window || !contentView) {
            return;
        }
        const CGFloat alpha = options.transparent ? 0.0 : currentBackground.a / 255.0;
        NSColor* color = [NSColor colorWithSRGBRed:currentBackground.r / 255.0
                                             green:currentBackground.g / 255.0
                                              blue:currentBackground.b / 255.0
                                             alpha:alpha];
        window.backgroundColor = color;
        window.opaque = options.transparent ? NO : YES;
        contentView.layer.backgroundColor = color.CGColor;
        if (webView) {
            if (options.transparent || browserOptions.backgroundColor.a == 0) {
                @try {
                    [webView setValue:@NO forKey:@"drawsBackground"];
                } @catch (NSException* exception) {
                    (void)exception;
                }
            }
            if (@available(macOS 12.0, *)) {
                webView.underPageBackgroundColor = color;
            }
        }
    }

    void MacWindowBackend::UpdateAppearance() {
        if (!window) {
            return;
        }
        darkMode = effectiveTheme == Theme::DARK
            || (effectiveTheme == Theme::SYSTEM && [[window.effectiveAppearance bestMatchFromAppearancesWithNames:@[
                   NSAppearanceNameAqua, NSAppearanceNameDarkAqua
               ]] isEqualToString:NSAppearanceNameDarkAqua]);
    }

    void MacWindowBackend::HandleAppearanceChanged() {
        UpdateAppearance();
        if (!suppressEvents && listener) {
            listener->OnBackendThemeChanged(effectiveTheme, darkMode ? Theme::DARK : Theme::LIGHT);
        }
    }

    void MacWindowBackend::HandleTitleChanged() {
        if (!webView || suppressEvents || !listener) {
            return;
        }
        NSString* title = webView.title;
        currentTitle = title ? title.UTF8String : "";
        listener->OnBackendDocumentTitleChanged(currentTitle);
    }

    void MacWindowBackend::HandleFocus(bool isFocused) {
        if (focused == isFocused) {
            return;
        }
        focused = isFocused;
        if (!suppressEvents && listener) {
            listener->OnBackendFocusChanged(isFocused);
        }
    }

    void MacWindowBackend::HandleWindowFrameChanged() {
        if (!window || suppressEvents || !listener) {
            return;
        }
        listener->OnBackendBoundsChanged(ScreenRectFromFrame(window.frame), maximized);
    }

    void MacWindowBackend::HandleDpiChanged() {
        if (!window || suppressEvents || !listener) {
            return;
        }
        listener->OnBackendDpiChanged(window.backingScaleFactor, ScreenRectFromFrame(window.frame));
    }

    void MacWindowBackend::HandleNativeFullscreen(bool entered) {
        if (fullscreen == entered) {
            return;
        }
        fullscreen = entered;
        if (!suppressEvents && listener) {
            listener->OnBackendFullscreenChanged(entered);
        }
    }

    Rect MacWindowBackend::ScreenRectFromFrame(NSRect frame) const {
        NSScreen* screen = [NSScreen screens].firstObject;
        const CGFloat screenHeight = screen ? screen.frame.size.height : 0.0;
        return Rect{
            frame.origin.x, screenHeight - (frame.origin.y + frame.size.height), frame.size.width, frame.size.height};
    }

    NSRect MacWindowBackend::FrameFromScreenRect(const Rect& rect) const {
        NSScreen* screen = [NSScreen screens].firstObject;
        const CGFloat screenHeight = screen ? screen.frame.size.height : 0.0;
        return NSMakeRect(rect.x, screenHeight - (rect.y + rect.height), rect.width, rect.height);
    }

    void MacWindowBackend::CreateWebView() {
        if (destroyed || !window) {
            return;
        }
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.suppressesIncrementalRendering = NO;
        configuration.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;

        if (browserOptions.incognito) {
            configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        } else if (!browserOptions.userDataFolder.empty()) {
            if (@available(macOS 14.0, *)) {
                auto digest = Crypto::Sha256(PathToUtf8(browserOptions.userDataFolder));
                uuid_t identifier;
                std::memcpy(identifier, digest.data(), 16);
                NSUUID* uuid = [[NSUUID alloc] initWithUUIDBytes:identifier];
                configuration.websiteDataStore = [WKWebsiteDataStore dataStoreForIdentifier:uuid];
            } else {
                configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
            }
        } else {
            configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
        }

        WKPreferences* preferences = [[WKPreferences alloc] init];
        @try {
            [preferences setValue:@(browserOptions.javascriptEnabled) forKey:@"javaScriptEnabled"];
            [preferences setValue:@(browserOptions.devToolsEnabled) forKey:@"developerExtrasEnabled"];
            [preferences setValue:@(browserOptions.allowFileAccessFromFileUrls) forKey:@"allowFileAccessFromFileURLs"];
            [preferences setValue:@NO forKey:@"allowUniversalAccessFromFileURLs"];
        } @catch (NSException* exception) {
            (void)exception;
        }
        configuration.preferences = preferences;

        WKUserContentController* userContentController = [[WKUserContentController alloc] init];
        for (const std::string& script : browserOptions.documentStartScripts) {
            WKUserScript* userScript = [[WKUserScript alloc] initWithSource:ToNSString(script)
                                                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                           forMainFrameOnly:NO];
            [userContentController addUserScript:userScript];
        }
        for (const std::string& script : browserOptions.documentEndScripts) {
            WKUserScript* userScript = [[WKUserScript alloc] initWithSource:ToNSString(script)
                                                              injectionTime:WKUserScriptInjectionTimeAtDocumentEnd
                                                           forMainFrameOnly:NO];
            [userContentController addUserScript:userScript];
        }
        WKUserScript* lifecycle = [[WKUserScript alloc] initWithSource:ToNSString(LifecycleScript())
                                                         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                      forMainFrameOnly:YES];
        [userContentController addUserScript:lifecycle];
        [userContentController addScriptMessageHandler:delegate name:@"flare"];
        [userContentController addScriptMessageHandler:delegate name:@"flareLifecycle"];
        configuration.userContentController = userContentController;

        schemeHandlers = [NSMutableArray array];
        if (browserOptions.virtualHost.has_value()) {
            NSString* scheme = ToNSString(browserOptions.virtualHost->scheme);
            FlareSchemeHandler* handler = [[FlareSchemeHandler alloc] initWithBackend:this scheme:scheme];
            [schemeHandlers addObject:handler];
            [configuration setURLSchemeHandler:handler forURLScheme:scheme];
        }
        for (const auto& entry : browserOptions.customSchemes) {
            NSString* scheme = ToNSString(entry.first);
            FlareSchemeHandler* handler = [[FlareSchemeHandler alloc] initWithBackend:this scheme:scheme];
            [schemeHandlers addObject:handler];
            [configuration setURLSchemeHandler:handler forURLScheme:scheme];
        }

        webView = [[FlareWebView alloc] initWithFrame:contentView.bounds configuration:configuration backend:this];
        if (!webView) {
            RaiseFailure(Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the WKWebView"));
            return;
        }
        webView.navigationDelegate = delegate;
        webView.UIDelegate = delegate;
        webView.allowsBackForwardNavigationGestures = browserOptions.swipeNavigationEnabled ? YES : NO;
        webView.allowsMagnification = browserOptions.pinchZoomEnabled ? YES : NO;
        webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        if (!browserOptions.userAgent.empty()) {
            webView.customUserAgent = ToNSString(browserOptions.userAgent);
        }
        webView.pageZoom = zoomFactor;
        if (@available(macOS 13.3, *)) {
            webView.inspectable = browserOptions.devToolsEnabled ? YES : NO;
        }
        [webView addObserver:delegate forKeyPath:@"title" options:0 context:&TitleContext];
        titleObserverInstalled = true;
        [contentView addSubview:webView];
        ApplyBackground();

        if (browserOptions.clearCacheOnStart) {
            NSSet* types = [NSSet setWithObjects:WKWebsiteDataTypeCookies, WKWebsiteDataTypeDiskCache,
                WKWebsiteDataTypeMemoryCache, WKWebsiteDataTypeLocalStorage, WKWebsiteDataTypeIndexedDBDatabases,
                WKWebsiteDataTypeServiceWorkerRegistrations, nil];
            MacWindowBackend* self = this;
            std::shared_ptr<std::atomic<bool>> guard = alive;
            [configuration.websiteDataStore removeDataOfTypes:types
                                                modifiedSince:[NSDate dateWithTimeIntervalSince1970:0]
                                            completionHandler:^{
                                              dispatch_async(dispatch_get_main_queue(), ^{
                                                if (guard->load()) {
                                                    self->RaiseReady();
                                                }
                                              });
                                            }];
            return;
        }
        RaiseReady();
    }

    void MacWindowBackend::RaiseReady() {
        if (readyRaised || failureRaised || destroyed) {
            return;
        }
        readyRaised = true;
        if (listener) {
            listener->OnBackendBrowserReady();
        }
    }

    void MacWindowBackend::RaiseFailure(const Error& error) {
        if (failureRaised || destroyed) {
            return;
        }
        failureRaised = true;
        if (listener) {
            listener->OnBackendBrowserCreationFailed(error);
        }
    }

    void MacWindowBackend::ReleaseWebView() {
        if (webView) {
            [webView stopLoading];
            [webView.configuration.userContentController removeScriptMessageHandlerForName:@"flare"];
            [webView.configuration.userContentController removeScriptMessageHandlerForName:@"flareLifecycle"];
            [webView.configuration.userContentController removeAllUserScripts];
            if (titleObserverInstalled) {
                [webView removeObserver:delegate forKeyPath:@"title" context:&TitleContext];
                titleObserverInstalled = false;
            }
            webView.navigationDelegate = nil;
            webView.UIDelegate = nil;
            [webView removeFromSuperview];
            webView = nil;
        }
        for (auto& entry : downloads) {
            if (entry.second->observing && entry.second->progress) {
                [entry.second->progress removeObserver:delegate
                                            forKeyPath:@"fractionCompleted"
                                               context:&DownloadProgressContext];
                entry.second->observing = false;
            }
            [entry.second->download cancel:^(NSData* resumeData) {
              (void)resumeData;
            }];
        }
        downloads.clear();
        progressRecords.clear();
        [schemeHandlers removeAllObjects];
        schemeHandlers = nil;
    }

    Result<void> MacWindowBackend::RequireWebView() const {
        if (!webView || destroyed) {
            return Error(ErrorCode::BROWSER_NOT_READY, "The WKWebView is not ready");
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Show() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        AttachParent(true);
        if (options.modal && parentWindow) {
            SetParentInteractive(false);
        }
        [window orderFrontRegardless];
        if (!visible) {
            visible = true;
            if (!suppressEvents && listener) {
                listener->OnBackendVisibilityChanged(true);
            }
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Hide() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        [window orderOut:nil];
        if (visible) {
            visible = false;
            if (!suppressEvents && listener) {
                listener->OnBackendVisibilityChanged(false);
            }
        }
        SetParentInteractive(true);
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Minimize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        [window miniaturize:nil];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Maximize() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        if (maximized || fullscreen) {
            return Result<void>::Ok();
        }
        if (!hasNormalFrame) {
            normalFrame = window.frame;
            normalStyle = window.styleMask;
            hasNormalFrame = true;
        }
        NSScreen* screen = window.screen ? window.screen : [NSScreen mainScreen];
        [window setFrame:screen.visibleFrame display:YES];
        maximized = true;
        if (!suppressEvents && listener) {
            listener->OnBackendMaximized();
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Restore() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        if (fullscreen) {
            (void)SetFullscreen(false);
        }
        if (minimized) {
            [window deminiaturize:nil];
        }
        if (window.isZoomed) {
            [window zoom:nil];
        }
        if (maximized && hasNormalFrame) {
            window.styleMask = static_cast<NSWindowStyleMask>(normalStyle);
            [window setFrame:normalFrame display:YES];
            maximized = false;
            hasNormalFrame = false;
            if (!suppressEvents && listener) {
                listener->OnBackendRestored();
            }
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Focus() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        [window makeKeyAndOrderFront:nil];
        if (webView) {
            [window makeFirstResponder:webView];
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::StartDrag() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        if (!options.movable) {
            return Error(ErrorCode::PERMISSION_DENIED, "The window is not movable");
        }
        NSEvent* event = [NSApp currentEvent];
        if (event && event.type != NSEventTypeLeftMouseDown) {
            event = nil;
        }
        if (!event) {
            NSPoint mouse = [NSEvent mouseLocation];
            NSPoint local = [window convertPointFromScreen:mouse];
            event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                       location:local
                                  modifierFlags:0
                                      timestamp:[NSProcessInfo processInfo].systemUptime
                                   windowNumber:window.windowNumber
                                        context:nil
                                    eventNumber:0
                                     clickCount:1
                                       pressure:1.0];
        }
        [window performWindowDragWithEvent:event];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetTitle(const std::string& title) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        window.title = ToNSString(title);
        options.title = title;
        return Result<void>::Ok();
    }

    Result<std::string> MacWindowBackend::GetTitle() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        return window.title ? std::string(window.title.UTF8String) : std::string();
    }

    Result<void> MacWindowBackend::SetSize(int width, int height) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        [window setContentSize:NSMakeSize(width, height)];
        options.width = width;
        options.height = height;
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetPosition(int x, int y) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        NSRect frame = window.frame;
        const NSRect target = FrameFromScreenRect(
            Rect{static_cast<double>(x), static_cast<double>(y), frame.size.width, frame.size.height});
        [window setFrameOrigin:target.origin];
        options.x = x;
        options.y = y;
        options.hasPosition = true;
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetMinSize(int width, int height) {
        options.minWidth = width;
        options.minHeight = height;
        if (window) {
            window.contentMinSize = NSMakeSize(width, height);
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetMaxSize(int width, int height) {
        options.maxWidth = width;
        options.maxHeight = height;
        if (window) {
            window.contentMaxSize = NSMakeSize(width > 0 ? width : CGFLOAT_MAX, height > 0 ? height : CGFLOAT_MAX);
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetFullscreen(bool value) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        if (value == fullscreen) {
            return Result<void>::Ok();
        }
        if (value) {
            if (!hasNormalFrame) {
                normalFrame = window.frame;
                normalStyle = window.styleMask;
                hasNormalFrame = true;
            }
            NSScreen* screen = window.screen ? window.screen : [NSScreen mainScreen];
            window.styleMask = NSWindowStyleMaskBorderless;
            [window setFrame:screen.frame display:YES];
            [NSApp setPresentationOptions:static_cast<NSApplicationPresentationOptions>(
                                              NSApplicationPresentationAutoHideMenuBar
                                              | NSApplicationPresentationAutoHideDock)];
            fullscreen = true;
            if (!suppressEvents && listener) {
                listener->OnBackendFullscreenChanged(true);
            }
        } else {
            [NSApp setPresentationOptions:NSApplicationPresentationDefault];
            if (hasNormalFrame) {
                window.styleMask = static_cast<NSWindowStyleMask>(normalStyle);
                [window setFrame:normalFrame display:YES];
                hasNormalFrame = false;
            }
            fullscreen = false;
            maximized = false;
            if (!suppressEvents && listener) {
                listener->OnBackendFullscreenChanged(false);
            }
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetAlwaysOnTop(bool alwaysOnTop) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        window.level = alwaysOnTop ? NSFloatingWindowLevel : NSNormalWindowLevel;
        options.alwaysOnTop = alwaysOnTop;
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetOpacity(double opacity) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        windowOpacity = opacity;
        options.opacity = opacity;
        window.alphaValue = opacity;
        return Result<void>::Ok();
    }

    Result<double> MacWindowBackend::GetOpacity() const {
        return window ? window.alphaValue : windowOpacity;
    }

    Result<void> MacWindowBackend::Center() {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        [window center];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetBackgroundColor(Color color) {
        currentBackground = color;
        options.backgroundColor = color;
        ApplyBackground();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetTheme(Theme theme) {
        effectiveTheme = theme;
        options.theme = theme;
        if (window) {
            if (theme == Theme::DARK) {
                window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            } else if (theme == Theme::LIGHT) {
                window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
            } else {
                window.appearance = nil;
            }
        }
        UpdateAppearance();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetContentProtection(bool enabled) {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        window.sharingType = enabled ? NSWindowSharingNone : NSWindowSharingReadOnly;
        options.contentProtection = enabled;
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetResizable(bool resizable) {
        options.resizable = resizable;
        ApplyStyle();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetMovable(bool movable) {
        options.movable = movable;
        if (window) {
            window.movable = movable;
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetMinimizable(bool minimizable) {
        options.minimizable = minimizable;
        ApplyStyle();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetMaximizable(bool maximizable) {
        options.maximizable = maximizable;
        ApplyStyle();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetClosable(bool closable) {
        options.closable = closable;
        ApplyStyle();
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetShowInTaskbar(bool) {
        return Error(ErrorCode::NOT_SUPPORTED, "Per-window Dock visibility is not supported on macOS");
    }

    Result<void> MacWindowBackend::SetIcon(const Icon&) {
        return Error(ErrorCode::NOT_SUPPORTED, "Per-window icons are not supported on macOS");
    }

    Result<void> MacWindowBackend::SetDraggableRegions(const std::vector<Rect>& regions) {
        draggableRegions = regions;
        options.draggableRegions = regions;
        return Result<void>::Ok();
    }

    Result<Rect> MacWindowBackend::GetBounds() const {
        if (!window) {
            return Error(ErrorCode::PLATFORM_FAILURE, "The NSWindow is unavailable");
        }
        return ScreenRectFromFrame(window.frame);
    }

    bool MacWindowBackend::IsVisible() const {
        return window && window.isVisible;
    }

    bool MacWindowBackend::IsMaximized() const {
        return maximized || (window && window.isZoomed);
    }

    bool MacWindowBackend::IsMinimized() const {
        return window && window.isMiniaturized;
    }

    bool MacWindowBackend::IsFocused() const {
        return window && window.isKeyWindow;
    }

    bool MacWindowBackend::IsFullscreen() const {
        return fullscreen;
    }

    double MacWindowBackend::GetDpiScale() const {
        return window ? window.backingScaleFactor : 1.0;
    }

    Result<void> MacWindowBackend::Navigate(const std::string& url) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        NSURL* target = [NSURL URLWithString:ToNSString(url)];
        if (!target) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Navigation URL is invalid");
        }
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:target];
        for (const auto& header : browserOptions.extraHeaders) {
            [request setValue:ToNSString(header.second) forHTTPHeaderField:ToNSString(header.first)];
        }
        [webView loadRequest:request];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::NavigateToString(const std::string& html, const std::string& baseUrl) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        NSURL* base = baseUrl.empty() ? nil : [NSURL URLWithString:ToNSString(baseUrl)];
        [webView loadHTMLString:ToNSString(html) baseURL:base];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::LoadFile(const std::filesystem::path& file) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        NSURL* url = [NSURL fileURLWithPath:ToNSString(PathToUtf8(file))];
        NSURL* directory = [url URLByDeletingLastPathComponent];
        [webView loadFileURL:url allowingReadAccessToURL:directory];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Reload() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        [webView reload];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::GoBack() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        [webView goBack];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::GoForward() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        [webView goForward];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Stop() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        [webView stopLoading];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::ExecuteScript(const std::string& script, ResultCallback<JSON> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        auto sharedCallback = std::make_shared<ResultCallback<JSON>>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        [webView evaluateJavaScript:ToNSString(script)
                  completionHandler:^(id result, NSError* error) {
                    if (!guard->load() || !(*sharedCallback)) {
                        return;
                    }
                    if (error) {
                        (*sharedCallback)(Error(ErrorCode::PLATFORM_FAILURE, ErrorText(error)));
                        return;
                    }
                    (*sharedCallback)(JsonFromObject(result));
                  }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::OpenDevTools() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        if (@available(macOS 13.3, *)) {
            webView.inspectable = YES;
        }
        @try {
            [webView setValue:@YES forKey:@"inspectorVisible"];
        } @catch (NSException* exception) {
            (void)exception;
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to show the WebKit inspector");
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::CloseDevTools() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        @try {
            [webView setValue:@NO forKey:@"inspectorVisible"];
        } @catch (NSException* exception) {
            (void)exception;
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to close the WebKit inspector");
        }
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::PrintToPdf(
        const PdfOptions& pdfOptions, const std::filesystem::path& outputPath, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        const bool defaultLayout = !pdfOptions.landscape && pdfOptions.printBackground && pdfOptions.scale == 1.0
            && pdfOptions.pageWidthInches == 8.5 && pdfOptions.pageHeightInches == 11.0
            && pdfOptions.marginTopInches == 0.4 && pdfOptions.marginBottomInches == 0.4
            && pdfOptions.marginLeftInches == 0.4 && pdfOptions.marginRightInches == 0.4 && pdfOptions.preferCssPageSize
            && !pdfOptions.displayHeaderFooter && pdfOptions.headerTemplate.empty()
            && pdfOptions.footerTemplate.empty();
        if (!defaultLayout) {
            const Error error(ErrorCode::NOT_SUPPORTED,
                "WKWebView PDF export does not expose page layout, margin, or header/footer options");
            if (callback) {
                callback(error);
            }
            return error;
        }
        std::error_code error;
        const std::filesystem::path parent = outputPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
        }
        auto sharedCallback = std::make_shared<CompletionCallback>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        WKPDFConfiguration* configuration = [[WKPDFConfiguration alloc] init];
        [webView createPDFWithConfiguration:configuration
                          completionHandler:^(NSData* data, NSError* pdfError) {
                            if (!guard->load() || !(*sharedCallback)) {
                                return;
                            }
                            if (pdfError || !data) {
                                (*sharedCallback)(Error(ErrorCode::PLATFORM_FAILURE, ErrorText(pdfError)));
                                return;
                            }
                            const bool written = [data writeToFile:ToNSString(PathToUtf8(outputPath)) atomically:YES];
                            if (!written) {
                                (*sharedCallback)(Error(ErrorCode::IO_FAILURE, "Unable to write the PDF file"));
                                return;
                            }
                            (*sharedCallback)(Result<void>::Ok());
                          }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::Print() {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        NSPrintOperation* operation = [webView printOperationWithPrintInfo:[NSPrintInfo sharedPrintInfo]];
        if (!operation) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create a print operation");
        }
        [operation runOperation];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::CapturePreview(
        const CaptureOptions& optionsCapture, std::function<void(Result<std::vector<std::uint8_t>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        auto sharedCallback = std::make_shared<std::function<void(Result<std::vector<std::uint8_t>>)>>(
            std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        WKSnapshotConfiguration* snapshot = [[WKSnapshotConfiguration alloc] init];
        [webView
            takeSnapshotWithConfiguration:snapshot
                        completionHandler:^(NSImage* image, NSError* error) {
                          if (!guard->load() || !(*sharedCallback)) {
                              return;
                          }
                          if (error || !image) {
                              (*sharedCallback)(Error(ErrorCode::PLATFORM_FAILURE, ErrorText(error)));
                              return;
                          }
                          NSData* tiff = [image TIFFRepresentation];
                          NSBitmapImageRep* representation = tiff ? [NSBitmapImageRep imageRepWithData:tiff] : nil;
                          if (!representation) {
                              (*sharedCallback)(
                                  Error(ErrorCode::PLATFORM_FAILURE, "Unable to encode the captured image"));
                              return;
                          }
                          NSData* data = nil;
                          if (optionsCapture.format == ImageFormat::JPEG) {
                              data = [representation
                                  representationUsingType:NSBitmapImageFileTypeJPEG
                                               properties:@{
                                                   NSImageCompressionFactor : @(optionsCapture.quality / 100.0)
                                               }];
                          } else {
                              data = [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                          }
                          if (!data) {
                              (*sharedCallback)(
                                  Error(ErrorCode::PLATFORM_FAILURE, "Unable to encode the captured image"));
                              return;
                          }
                          std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(data.bytes),
                              static_cast<const std::uint8_t*>(data.bytes) + data.length);
                          (*sharedCallback)(std::move(bytes));
                        }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::GetCookies(
        const std::string& url, std::function<void(Result<std::vector<Cookie>>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        auto sharedCallback = std::make_shared<std::function<void(Result<std::vector<Cookie>>)>>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        NSURL* target = [NSURL URLWithString:ToNSString(url)];
        NSString* targetHost = target.host;
        WKHTTPCookieStore* store = webView.configuration.websiteDataStore.httpCookieStore;
        [store getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
          if (!guard->load() || !(*sharedCallback)) {
              return;
          }
          std::vector<Cookie> result;
          for (NSHTTPCookie* native in cookies) {
              if (targetHost && native.domain) {
                  NSString* domain = [native.domain hasPrefix:@"."] ? [native.domain substringFromIndex:1]
                                                                    : native.domain;
                  if (![targetHost isEqualToString:domain]
                      && ![targetHost hasSuffix:[@"." stringByAppendingString:domain]]) {
                      continue;
                  }
              }
              Cookie cookie;
              cookie.name = native.name.UTF8String ? native.name.UTF8String : "";
              cookie.value = native.value.UTF8String ? native.value.UTF8String : "";
              cookie.domain = native.domain.UTF8String ? native.domain.UTF8String : "";
              cookie.path = native.path.UTF8String ? native.path.UTF8String : "/";
              cookie.secure = native.isSecure;
              cookie.httpOnly = native.isHTTPOnly;
              cookie.expiresUnixSeconds = native.expiresDate
                  ? static_cast<std::int64_t>(native.expiresDate.timeIntervalSince1970)
                  : -1;
              result.push_back(std::move(cookie));
          }
          (*sharedCallback)(std::move(result));
        }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetCookie(const Cookie& cookie, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        NSMutableDictionary* properties = [NSMutableDictionary dictionary];
        properties[NSHTTPCookieName] = ToNSString(cookie.name);
        properties[NSHTTPCookieValue] = ToNSString(cookie.value);
        properties[NSHTTPCookieDomain] = ToNSString(cookie.domain);
        properties[NSHTTPCookiePath] = ToNSString(cookie.path.empty() ? "/" : cookie.path);
        if (cookie.secure) {
            properties[NSHTTPCookieSecure] = @"TRUE";
        }
        if (cookie.httpOnly) {
            properties[@"HttpOnly"] = @"TRUE";
        }
        if (cookie.expiresUnixSeconds >= 0) {
            properties[NSHTTPCookieExpires] = [NSDate
                dateWithTimeIntervalSince1970:static_cast<NSTimeInterval>(cookie.expiresUnixSeconds)];
        }
        switch (cookie.sameSite) {
            case CookieSameSite::NONE:
                properties[@"SameSite"] = @"None";
                break;
            case CookieSameSite::LAX:
                properties[@"SameSite"] = @"Lax";
                break;
            case CookieSameSite::STRICT_MODE:
                properties[@"SameSite"] = @"Strict";
                break;
            case CookieSameSite::UNSPECIFIED:
                break;
        }
        NSHTTPCookie* native = [NSHTTPCookie cookieWithProperties:properties];
        if (!native) {
            const Error error(ErrorCode::INVALID_ARGUMENT, "Unable to create the cookie");
            if (callback) {
                callback(error);
            }
            return error;
        }
        auto sharedCallback = std::make_shared<CompletionCallback>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        [webView.configuration.websiteDataStore.httpCookieStore setCookie:native
                                                        completionHandler:^{
                                                          if (guard->load() && (*sharedCallback)) {
                                                              (*sharedCallback)(Result<void>::Ok());
                                                          }
                                                        }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::DeleteCookie(
        const std::string& url, const std::string& name, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        NSURL* target = [NSURL URLWithString:ToNSString(url)];
        NSString* targetHost = target.host;
        auto sharedCallback = std::make_shared<CompletionCallback>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        WKHTTPCookieStore* store = webView.configuration.websiteDataStore.httpCookieStore;
        [store getAllCookies:^(NSArray<NSHTTPCookie*>* cookies) {
          if (!guard->load() || !(*sharedCallback)) {
              return;
          }
          for (NSHTTPCookie* native in cookies) {
              if (targetHost && native.domain) {
                  NSString* domain = [native.domain hasPrefix:@"."] ? [native.domain substringFromIndex:1]
                                                                    : native.domain;
                  if (![targetHost isEqualToString:domain]
                      && ![targetHost hasSuffix:[@"." stringByAppendingString:domain]]) {
                      continue;
                  }
              }
              if (native.name.UTF8String && name == native.name.UTF8String) {
                  [store deleteCookie:native
                      completionHandler:^{
                        if (guard->load() && (*sharedCallback)) {
                            (*sharedCallback)(Result<void>::Ok());
                        }
                      }];
                  return;
              }
          }
          (*sharedCallback)(Result<void>::Ok());
        }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::ClearBrowsingData(ClearDataKind kinds, CompletionCallback callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        NSMutableSet* types = [NSMutableSet set];
        if (HasClearDataKind(kinds, ClearDataKind::COOKIES)) {
            [types addObject:WKWebsiteDataTypeCookies];
        }
        if (HasClearDataKind(kinds, ClearDataKind::CACHE)) {
            [types addObject:WKWebsiteDataTypeDiskCache];
            [types addObject:WKWebsiteDataTypeMemoryCache];
        }
        if (HasClearDataKind(kinds, ClearDataKind::LOCAL_STORAGE)) {
            [types addObject:WKWebsiteDataTypeLocalStorage];
        }
        if (HasClearDataKind(kinds, ClearDataKind::SESSION_STORAGE)) {
            [types addObject:WKWebsiteDataTypeSessionStorage];
        }
        if (HasClearDataKind(kinds, ClearDataKind::INDEXED_DB)) {
            [types addObject:WKWebsiteDataTypeIndexedDBDatabases];
        }
        if (HasClearDataKind(kinds, ClearDataKind::SERVICE_WORKERS)) {
            [types addObject:WKWebsiteDataTypeServiceWorkerRegistrations];
        }
        if (HasClearDataKind(kinds, ClearDataKind::ALL)) {
            [types addObject:WKWebsiteDataTypeCookies];
            [types addObject:WKWebsiteDataTypeDiskCache];
            [types addObject:WKWebsiteDataTypeMemoryCache];
            [types addObject:WKWebsiteDataTypeLocalStorage];
            [types addObject:WKWebsiteDataTypeSessionStorage];
            [types addObject:WKWebsiteDataTypeIndexedDBDatabases];
            [types addObject:WKWebsiteDataTypeServiceWorkerRegistrations];
        }
        auto sharedCallback = std::make_shared<CompletionCallback>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        [webView.configuration.websiteDataStore removeDataOfTypes:types
                                                    modifiedSince:[NSDate dateWithTimeIntervalSince1970:0]
                                                completionHandler:^{
                                                  dispatch_async(dispatch_get_main_queue(), ^{
                                                    if (guard->load() && (*sharedCallback)) {
                                                        (*sharedCallback)(Result<void>::Ok());
                                                    }
                                                  });
                                                }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetZoom(double factor) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webView.pageZoom = factor;
        zoomFactor = factor;
        if (listener) {
            listener->OnBackendZoomChanged(factor);
        }
        return Result<void>::Ok();
    }

    Result<double> MacWindowBackend::GetZoom() const {
        return webView ? webView.pageZoom : zoomFactor;
    }

    Result<void> MacWindowBackend::FindInPage(
        const std::string& text, const FindOptions& optionsFind, std::function<void(Result<FindResult>)> callback) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            if (callback) {
                callback(usable.GetError());
            }
            return usable;
        }
        WKFindConfiguration* configuration = [[WKFindConfiguration alloc] init];
        configuration.caseSensitive = optionsFind.matchCase;
        configuration.backwards = !optionsFind.forward;
        configuration.wraps = YES;
        auto sharedCallback = std::make_shared<std::function<void(Result<FindResult>)>>(std::move(callback));
        std::shared_ptr<std::atomic<bool>> guard = alive;
        [webView findString:ToNSString(text)
            withConfiguration:configuration
            completionHandler:^(WKFindResult* result) {
              if (!guard->load() || !(*sharedCallback)) {
                  return;
              }
              FindResult outcome;
              outcome.occurrences = result.matchFound ? 1 : 0;
              outcome.activeMatch = result.matchFound ? 1 : 0;
              outcome.finished = YES;
              (*sharedCallback)(outcome);
            }];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::StopFindInPage() {
        return Error(ErrorCode::NOT_SUPPORTED, "WKWebView does not expose a stop-find API");
    }

    Result<std::string> MacWindowBackend::GetUrl() const {
        if (!webView || !webView.URL) {
            return currentUrl;
        }
        NSString* absolute = webView.URL.absoluteString;
        return absolute ? std::string(absolute.UTF8String) : currentUrl;
    }

    Result<std::string> MacWindowBackend::GetBrowserTitle() const {
        if (webView && webView.title) {
            return std::string(webView.title.UTF8String);
        }
        return currentTitle;
    }

    Result<void> MacWindowBackend::SendWebMessage(const JSON& message) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        const std::string script = "window.postMessage(" + SafeJsonDump(message) + ",'*');";
        [webView evaluateJavaScript:ToNSString(script) completionHandler:nil];
        return Result<void>::Ok();
    }

    Result<void> MacWindowBackend::SetUserAgent(const std::string& agent) {
        Result<void> usable = RequireWebView();
        if (!usable.IsOk()) {
            return usable;
        }
        webView.customUserAgent = agent.empty() ? nil : ToNSString(agent);
        userAgent = agent;
        return Result<void>::Ok();
    }

    Result<std::string> MacWindowBackend::GetUserAgent() const {
        if (!webView) {
            return userAgent;
        }
        return webView.customUserAgent ? std::string(webView.customUserAgent.UTF8String) : userAgent;
    }

    void MacWindowBackend::PostWebMessage(const std::string& message) {
        if (!webView) {
            return;
        }
        const std::string script = "if(window.__flareReceive){window.__flareReceive(" + message + ");}";
        [webView evaluateJavaScript:ToNSString(script) completionHandler:nil];
    }

    void MacWindowBackend::PostWebMessageToFrame(const std::string&, const std::string&) {}

    void MacWindowBackend::HandleMessage(WKScriptMessage* message, bool lifecycle) {
        if (!listener || suppressEvents) {
            return;
        }
        if (lifecycle) {
            if (![message.body isKindOfClass:[NSDictionary class]]) {
                return;
            }
            NSDictionary* body = message.body;
            NSString* type = body[@"type"];
            if ([type isEqualToString:@"dom"]) {
                listener->OnBackendDomContentLoaded(true);
            } else if ([type isEqualToString:@"favicon"]) {
                NSString* url = body[@"url"];
                listener->OnBackendFaviconChanged(url ? url.UTF8String : "");
            }
            return;
        }
        const std::string json = StringFromObject(message.body);
        NSURL* source = message.frameInfo.request.URL;
        if (!source) {
            source = webView.URL;
        }
        std::string sourceUrl = currentUrl;
        if (source && source.absoluteString.UTF8String) {
            sourceUrl = source.absoluteString.UTF8String;
        }
        const bool isMainFrame = message.frameInfo ? message.frameInfo.isMainFrame : true;
        char frameBuffer[32]{};
        std::snprintf(frameBuffer, sizeof(frameBuffer), "%p", (__bridge void*)message.frameInfo);
        listener->OnBackendWebMessage(json, sourceUrl, isMainFrame, frameBuffer);
    }

    bool MacWindowBackend::HandleKeyEvent(NSEvent* event, bool down) {
        if (!listener || suppressEvents) {
            return false;
        }
        NSString* characters = event.charactersIgnoringModifiers;
        std::string key = characters ? characters.UTF8String : "";
        char codeBuffer[32]{};
        std::snprintf(codeBuffer, sizeof(codeBuffer), "Key%u", static_cast<unsigned>(event.keyCode));
        Modifiers modifiers = 0;
        if ((event.modifierFlags & NSEventModifierFlagOption) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::ALT);
        }
        if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::CONTROL);
        }
        if ((event.modifierFlags & NSEventModifierFlagShift) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::SHIFT);
        }
        if ((event.modifierFlags & NSEventModifierFlagCommand) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::META);
        }
        if ((event.modifierFlags & NSEventModifierFlagCapsLock) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::CAPS_LOCK);
        }
        return listener->OnBackendKey(
            down, key, codeBuffer, static_cast<int>(event.keyCode), modifiers, event.isARepeat);
    }

    bool MacWindowBackend::HandleMouseEvent(NSEvent* event, MouseEventType type) {
        if (!listener || suppressEvents) {
            return false;
        }
        NSPoint point = [webView convertPoint:event.locationInWindow fromView:nil];
        MouseButton button = MouseButton::NONE;
        switch (event.buttonNumber) {
            case 0:
                button = MouseButton::LEFT;
                break;
            case 1:
                button = MouseButton::RIGHT;
                break;
            case 2:
                button = MouseButton::MIDDLE;
                break;
            default:
                break;
        }
        MouseButtons buttons = 0;
        const NSUInteger pressedButtons = [NSEvent pressedMouseButtons];
        if ((pressedButtons & (1u << 0)) != 0) {
            buttons |= MouseButtonMask(MouseButton::LEFT);
        }
        if ((pressedButtons & (1u << 1)) != 0) {
            buttons |= MouseButtonMask(MouseButton::RIGHT);
        }
        if ((pressedButtons & (1u << 2)) != 0) {
            buttons |= MouseButtonMask(MouseButton::MIDDLE);
        }
        Modifiers modifiers = 0;
        if ((event.modifierFlags & NSEventModifierFlagOption) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::ALT);
        }
        if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::CONTROL);
        }
        if ((event.modifierFlags & NSEventModifierFlagShift) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::SHIFT);
        }
        if ((event.modifierFlags & NSEventModifierFlagCommand) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::META);
        }
        if (event.clickCount >= 2 && type == MouseEventType::DOWN) {
            type = MouseEventType::DOUBLE_CLICK_EVENT;
        }
        return listener->OnBackendMouse(type, point.x, webView.bounds.size.height - point.y, button, buttons, modifiers,
            static_cast<int>(event.clickCount));
    }

    bool MacWindowBackend::HandleScrollEvent(NSEvent* event) {
        if (!listener || suppressEvents) {
            return false;
        }
        NSPoint point = [webView convertPoint:event.locationInWindow fromView:nil];
        Modifiers modifiers = 0;
        if ((event.modifierFlags & NSEventModifierFlagOption) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::ALT);
        }
        if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::CONTROL);
        }
        if ((event.modifierFlags & NSEventModifierFlagShift) != 0) {
            modifiers |= static_cast<Modifiers>(Modifier::SHIFT);
        }
        return listener->OnBackendWheel(
            point.x, webView.bounds.size.height - point.y, event.scrollingDeltaX, event.scrollingDeltaY, modifiers);
    }

    bool MacWindowBackend::PointInDraggableRegion(NSPoint point) const {
        for (const Rect& region : draggableRegions) {
            if (region.Contains(Point{point.x, static_cast<double>(webView.bounds.size.height) - point.y})) {
                return true;
            }
        }
        return false;
    }

    void MacWindowBackend::HandleFileDrop(NSArray<NSURL*>* urls, NSPoint point) {
        if (!listener || suppressEvents) {
            return;
        }
        std::vector<std::filesystem::path> files;
        for (NSURL* url in urls) {
            if (url.isFileURL && url.path) {
                files.emplace_back(PathFromUtf8(url.path.UTF8String));
            }
        }
        listener->OnBackendFileDrop(
            std::move(files), Point{point.x, static_cast<double>(webView.bounds.size.height) - point.y});
    }

    void MacWindowBackend::HandleNewWindow(NSString* url) {
        if (listener && !suppressEvents) {
            listener->OnBackendNewWindowRequested(url ? url.UTF8String : "", true);
        }
    }

    void MacWindowBackend::HandleDownloadDestination(
        WKDownload* download, NSURLResponse* response, NSString* suggestedFilename, void (^completion)(NSURL*)) {
        if (!listener || suppressEvents) {
            completion(nil);
            return;
        }
        DownloadInfo info;
        char identifier[32]{};
        std::snprintf(identifier, sizeof(identifier), "%p", (__bridge void*)download);
        info.id = identifier;
        info.url = response.URL.absoluteString.UTF8String ? response.URL.absoluteString.UTF8String : "";
        info.suggestedFilename = suggestedFilename ? suggestedFilename.UTF8String : "download";
        info.mimeType = response.MIMEType ? response.MIMEType.UTF8String : "";
        info.totalBytes = response.expectedContentLength;
        info.state = DownloadState::PENDING;
        const DownloadStartDecision decision = listener->OnBackendDownloadStarting(info);
        if (browserOptions.download.blocked || decision.cancel) {
            [download cancel:^(NSData* resumeData) {
              (void)resumeData;
            }];
            completion(nil);
            return;
        }
        std::filesystem::path target = decision.path;
        if (browserOptions.download.askWhereToSave) {
            NSSavePanel* panel = [NSSavePanel savePanel];
            if (!target.empty()) {
                panel.nameFieldStringValue = ToNSString(PathToUtf8(target.filename()));
                panel.directoryURL = [NSURL fileURLWithPath:ToNSString(PathToUtf8(target.parent_path()))];
            } else {
                panel.nameFieldStringValue = suggestedFilename ? suggestedFilename : @"download";
                if (!browserOptions.download.directory.empty()) {
                    panel.directoryURL = [NSURL
                        fileURLWithPath:ToNSString(PathToUtf8(browserOptions.download.directory))];
                }
            }
            if ([panel runModal] == NSModalResponseOK && panel.URL) {
                target = PathFromUtf8(panel.URL.path.UTF8String);
            } else {
                [download cancel:^(NSData* resumeData) {
                  (void)resumeData;
                }];
                completion(nil);
                return;
            }
        }
        if (target.empty()) {
            NSURL* downloads = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory
                                                                      inDomain:NSUserDomainMask
                                                             appropriateForURL:nil
                                                                        create:YES
                                                                         error:nil];
            if (downloads) {
                target = PathFromUtf8(downloads.path.UTF8String)
                    / (suggestedFilename ? suggestedFilename.UTF8String : "download");
            }
        }
        info.path = target;
        info.state = DownloadState::IN_PROGRESS;
        auto record = std::make_shared<MacDownloadRecord>();
        record->download = download;
        record->info = info;
        downloads[(__bridge void*)download] = record;
        if (download.progress) {
            record->progress = download.progress;
            progressRecords[(__bridge void*)download.progress] = record;
            [download.progress addObserver:delegate
                                forKeyPath:@"fractionCompleted"
                                   options:NSKeyValueObservingOptionNew
                                   context:&DownloadProgressContext];
            record->observing = true;
        }
        listener->OnBackendDownloadProgress(info);
        completion([NSURL fileURLWithPath:ToNSString(PathToUtf8(target))]);
    }

    void MacWindowBackend::UpdateDownloadProgress(NSProgress* progress) {
        const auto iterator = progressRecords.find((__bridge void*)progress);
        if (iterator == progressRecords.end() || !iterator->second || !listener) {
            return;
        }
        MacDownloadRecord& record = *iterator->second;
        if (progress.totalUnitCount > 0) {
            record.info.totalBytes = progress.totalUnitCount;
            record.info.receivedBytes = progress.completedUnitCount;
        }
        listener->OnBackendDownloadProgress(record.info);
    }

    void MacWindowBackend::FinishDownload(WKDownload* download, NSError* error) {
        const auto iterator = downloads.find((__bridge void*)download);
        if (iterator == downloads.end()) {
            return;
        }
        std::shared_ptr<MacDownloadRecord> record = iterator->second;
        if (record->observing && record->progress) {
            [record->progress removeObserver:delegate forKeyPath:@"fractionCompleted" context:&DownloadProgressContext];
            record->observing = false;
            progressRecords.erase((__bridge void*)record->progress);
        }
        DownloadInfo info = record->info;
        info.state = error ? DownloadState::INTERRUPTED : DownloadState::COMPLETED;
        downloads.erase(iterator);
        if (listener) {
            listener->OnBackendDownloadCompleted(
                info, error ? Error(ErrorCode::PLATFORM_FAILURE, ErrorText(error)) : Error());
        }
    }

    Result<CustomSchemeResponse> MacWindowBackend::HandleScheme(
        const std::string& scheme, const std::string& url, const std::string& method, const HttpHeaderMap& headers) {
        for (const auto& entry : browserOptions.customSchemes) {
            if (!EqualsIgnoreCase(entry.first, scheme)) {
                continue;
            }
            CustomSchemeRequest request{url, scheme, method, headers};
            try {
                return entry.second(request);
            } catch (const std::exception& exception) {
                return Error(ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an exception", exception.what());
            } catch (...) {
                return Error(ErrorCode::HANDLER_FAILED, "Custom scheme handler threw an unknown exception");
            }
        }
        if (!browserOptions.virtualHost.has_value() || !EqualsIgnoreCase(browserOptions.virtualHost->scheme, scheme)) {
            return Error(ErrorCode::NOT_FOUND, "Unknown custom scheme");
        }
        NSURL* parsed = [NSURL URLWithString:ToNSString(url)];
        if (!parsed || !parsed.host || !EqualsIgnoreCase(parsed.host.UTF8String, browserOptions.virtualHost->host)) {
            return Error(ErrorCode::NOT_FOUND, "Virtual host does not match");
        }
        std::string path = parsed.path ? parsed.path.UTF8String : "/";
        if (path.empty() || path.back() == '/') {
            path += "index.html";
        }
        std::filesystem::path resolved;
        if (!ResolveWithinDirectory(browserOptions.virtualHost->folder, path, resolved)) {
            return Error(ErrorCode::NOT_FOUND, "Virtual host file was not found");
        }
        std::ifstream input(resolved, std::ios::binary);
        if (!input) {
            return Error(ErrorCode::NOT_FOUND, "Virtual host file was not found");
        }
        CustomSchemeResponse response;
        response.body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        response.mimeType = MimeTypeForExtension(ToNSString(PathToUtf8(resolved)));
        response.statusCode = 200;
        return response;
    }

    bool MacWindowBackend::ShouldClose() {
        if (suppressEvents) {
            return true;
        }
        if (!options.closable) {
            return false;
        }
        return listener ? listener->OnBackendCloseRequested() : true;
    }

    void MacWindowBackend::HandleWindowWillClose() {
        if (destroyed) {
            return;
        }
        destroyed = true;
        closing = true;
        visible = false;
        if (window && appearanceObserverInstalled) {
            [window removeObserver:delegate forKeyPath:@"effectiveAppearance" context:&AppearanceContext];
            appearanceObserverInstalled = false;
        }
        SetParentInteractive(true);
        AttachParent(false);
        ReleaseWebView();
        if (!suppressEvents && listener) {
            listener->OnBackendClosed();
        }
        window = nil;
    }

    void MacWindowBackend::AttachParent(bool attach) {
        if (!window || !parentWindow) {
            return;
        }
        if (attach && !parentAttached) {
            [parentWindow addChildWindow:window ordered:NSWindowAbove];
            parentAttached = true;
        } else if (!attach && parentAttached) {
            [parentWindow removeChildWindow:window];
            parentAttached = false;
        }
    }

    void MacWindowBackend::SetParentInteractive(bool interactive) {
        if (!parentWindow) {
            return;
        }
        if (!interactive && !parentDisabled) {
            parentWindow.ignoresMouseEvents = YES;
            parentDisabled = true;
        } else if (interactive && parentDisabled) {
            parentWindow.ignoresMouseEvents = NO;
            parentDisabled = false;
        }
    }

    void MacWindowBackend::Close() {
        if (!window || closing) {
            return;
        }
        closing = true;
        [window close];
    }

    void* MacWindowBackend::GetNativeHandle() const {
        return (__bridge void*)window;
    }

    const PlatformCapabilities& MacWindowBackend::Capabilities() const {
        return capabilities;
    }

    std::unique_ptr<WindowBackend> CreatePlatformWindowBackend() {
        return std::make_unique<MacWindowBackend>();
    }
}
