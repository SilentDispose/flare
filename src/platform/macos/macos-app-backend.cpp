#import <Cocoa/Cocoa.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "backend/backend.h"
#include "core/path-utils.h"

namespace {
    struct MacDispatchTask {
        std::function<void()> function;
    };

    NSString* FlareApplicationName() {
        NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
        return name ? name : @"Flare Application";
    }

    void BuildMainMenu() {
        NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@""];
        NSMenuItem* applicationItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        [mainMenu addItem:applicationItem];

        NSMenu* applicationMenu = [[NSMenu alloc] initWithTitle:FlareApplicationName()];
        [applicationMenu addItemWithTitle:[@"About " stringByAppendingString:FlareApplicationName()]
                                   action:@selector(orderFrontStandardAboutPanel:)
                            keyEquivalent:@""];
        [applicationMenu addItem:[NSMenuItem separatorItem]];
        [applicationMenu addItemWithTitle:[@"Hide " stringByAppendingString:FlareApplicationName()]
                                   action:@selector(hide:)
                            keyEquivalent:@"h"];
        NSMenuItem* hideOthers = [applicationMenu addItemWithTitle:@"Hide Others"
                                                            action:@selector(hideOtherApplications:)
                                                     keyEquivalent:@"h"];
        hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [applicationMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
        [applicationMenu addItem:[NSMenuItem separatorItem]];
        [applicationMenu addItemWithTitle:[@"Quit " stringByAppendingString:FlareApplicationName()]
                                   action:@selector(terminate:)
                            keyEquivalent:@"q"];
        applicationItem.submenu = applicationMenu;

        NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
        [mainMenu addItem:editItem];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
        NSMenuItem* redo = [editMenu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"];
        redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        [editMenu addItem:[NSMenuItem separatorItem]];
        [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
        [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
        [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
        [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        editItem.submenu = editMenu;

        NSMenuItem* windowItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
        [mainMenu addItem:windowItem];
        NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
        [windowMenu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
        windowItem.submenu = windowMenu;
        [NSApp setWindowsMenu:windowMenu];

        [NSApp setMainMenu:mainMenu];
    }
}

@interface FlareApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation FlareApplicationDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    [NSApp stop:nil];
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:YES];
    return NSTerminateCancel;
}
@end

namespace Flare {
    class MacAppBackend final : public AppBackend {
    public:
        Result<void> Initialize(const AppOptions& options) override {
            bool iconLoaded = true;
            @autoreleasepool {
                [NSApplication sharedApplication];
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                applicationDelegate = [[FlareApplicationDelegate alloc] init];
                [NSApp setDelegate:applicationDelegate];
                BuildMainMenu();
                ApplyTheme(options.theme);
                if (!options.icon.IsEmpty()) {
                    NSImage* image = nil;
                    if (options.icon.source == IconSource::RESOURCE) {
                        NSString* name = [NSString stringWithUTF8String:options.icon.resource.c_str()];
                        image = name ? [NSImage imageNamed:name] : nil;
                    } else {
                        NSString* path = [NSString stringWithUTF8String:PathToUtf8(options.icon.path).c_str()];
                        image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
                    }
                    if (image) {
                        [NSApp setApplicationIconImage:image];
                    } else {
                        iconLoaded = false;
                    }
                }
                [NSApp activateIgnoringOtherApps:YES];
            }
            if (!iconLoaded) {
                return Error(ErrorCode::IO_FAILURE, "Unable to load the application icon");
            }

            platformCapabilities.platformName = "macos";
            platformCapabilities.multiWindow = true;
            platformCapabilities.nativeKeyEvents = true;
            platformCapabilities.nativeMouseEvents = true;
            platformCapabilities.nativeWheelEvents = true;
            platformCapabilities.fileDrop = true;
            platformCapabilities.transparentWindows = true;
            platformCapabilities.windowOpacity = true;
            platformCapabilities.roundedCorners = true;
            platformCapabilities.borderlessShadow = true;
            platformCapabilities.contentProtection = true;
            platformCapabilities.customTitleBar = true;
            platformCapabilities.draggableRegions = true;
            platformCapabilities.virtualHostScheme = true;
            platformCapabilities.customSchemes = true;
            platformCapabilities.headerInjection = true;
            platformCapabilities.downloads = true;
            platformCapabilities.printToPdf = true;
            platformCapabilities.print = true;
            platformCapabilities.capturePreview = true;
            platformCapabilities.findInPage = true;
            platformCapabilities.cookies = true;
            platformCapabilities.clearBrowsingData = true;
            platformCapabilities.incognito = true;
            platformCapabilities.perWindowDataStore = true;
            platformCapabilities.windowFullscreen = true;
            platformCapabilities.permissionRequests = true;
            platformCapabilities.scriptDialogs = true;
            platformCapabilities.httpAuthentication = true;
            platformCapabilities.certificateErrors = true;
            platformCapabilities.webMessages = true;
            platformCapabilities.devTools = true;
            platformCapabilities.devToolsControl = true;
            platformCapabilities.pinchZoom = true;
            platformCapabilities.zoomControl = true;
            platformCapabilities.dpiEvents = true;
            platformCapabilities.themeEvents = true;
            platformCapabilities.systemThemeChange = true;
            platformCapabilities.documentTitleEvents = true;
            platformCapabilities.faviconEvents = true;
            platformCapabilities.domContentLoadedEvents = true;
            platformCapabilities.notes.emplace_back(
                "HTTPS virtual hosts are served through a custom URL scheme on WKWebView");
            platformCapabilities.notes.emplace_back(
                "Request interception and browser command-line arguments are not exposed by WKWebView");
            platformCapabilities.notes.emplace_back(
                "The system proxy is used on macOS; explicit proxy configuration is not available");
            platformCapabilities.notes.emplace_back(
                "Iframe bridging, programmatic context-menu control, and HTML5 fullscreen notifications are limited on "
                "macOS");
            platformCapabilities.notes.emplace_back(
                "Per-window icons and per-window taskbar visibility are not available on macOS");
            return Result<void>::Ok();
        }

        Result<void> Run() override {
            if (!IsOnUiThread()) {
                return Error(ErrorCode::WRONG_THREAD, "App::Run must be called on the UI thread");
            }
            if (stopRequested.load()) {
                return Result<void>::Ok();
            }
            running.store(true);
            @autoreleasepool {
                [NSApp run];
            }
            running.store(false);
            return Result<void>::Ok();
        }

        void Stop() override {
            stopRequested.store(true);
            dispatch_async(dispatch_get_main_queue(), ^{
              [NSApp stop:nil];
              NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                  location:NSZeroPoint
                                             modifierFlags:0
                                                 timestamp:0
                                              windowNumber:0
                                                   context:nil
                                                   subtype:0
                                                     data1:0
                                                     data2:0];
              [NSApp postEvent:event atStart:YES];
            });
        }

        void Dispatch(std::function<void()> task) override {
            if (!task) {
                return;
            }
            auto shared = std::make_shared<std::function<void()>>(std::move(task));
            dispatch_async(dispatch_get_main_queue(), ^{
              @autoreleasepool {
                  try {
                      (*shared)();
                  } catch (const std::exception&) {
                  } catch (...) {
                  }
              }
            });
        }

        bool IsOnUiThread() const override { return [NSThread isMainThread]; }

        const PlatformCapabilities& Capabilities() const override { return platformCapabilities; }

    private:
        static void ApplyTheme(Theme theme) {
            if (theme == Theme::DARK) {
                NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            } else if (theme == Theme::LIGHT) {
                NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
            } else {
                NSApp.appearance = nil;
            }
        }

        FlareApplicationDelegate* applicationDelegate = nil;
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> running{false};
        PlatformCapabilities platformCapabilities;
    };

    std::unique_ptr<AppBackend> CreatePlatformAppBackend() {
        return std::make_unique<MacAppBackend>();
    }
}
