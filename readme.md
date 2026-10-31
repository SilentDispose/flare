# Flare

Flare is a cross-platform C++20 library for desktop applications with a web-based user interface. It hosts native windows backed by the platform web engine, while C++ owns the application lifecycle, the UI thread, every window, and all privileged operations.

Supported engines:

| Platform | Engine |
| --- | --- |
| Windows 10+ | WebView2 |
| macOS 12+ | WKWebView |
| Linux | WebKitGTK 4.1 or 4.0, version 2.40+ with GTK 3 |

## Design

Flare is built around three principles.

1. **C++ owns the application.** C++ creates and destroys windows, runs the UI loop, manages parent/child and modal relationships, and decides when a window becomes visible.
2. **JavaScript is isolated by construction.** The page can use a narrow bridge for custom events, RPC handlers, and a whitelisted set of actions on the window that hosts it. It cannot create windows, address other windows, enumerate the application, or receive native events.
3. **The native side is the security boundary.** The bridge script is readable and inspectable. Every privileged request is authenticated and authorized in C++.

## Features

- Multiple native windows on a single UI thread.
- Parent/child and modal window relationships.
- Hidden native window until the browser is ready, with configurable show triggers and no empty flash.
- Typed C++ events for lifecycle, input, navigation, permissions, downloads, dialogs, resource requests, cookies, printing, and capture.
- A versioned JSON bridge with per-navigation credentials, HMAC-SHA256 authentication, replay protection, rate limiting, origin validation, handler permissions, and per-window action controls.
- Local files, HTML strings, normal URLs, virtual hosts, custom URL schemes, and embedded web bundles.
- Shared or isolated browsing profiles, incognito windows, proxy configuration, custom headers, request/response observation where the engine exposes it, and downloads.
- Window state persistence, single-instance forwarding, DPI and theme events, content protection, transparency, opacity, and frameless windows with custom title bars.
- Capability queries that report engine limitations before an unsupported option is used.

## Contents

- [Installation and Building](#installation-and-building)
- [Quick Start](#quick-start)
- [Window Lifecycle](#window-lifecycle)
- [Window API](#window-api)
- [Browser API](#browser-api)
- [Events](#events)
- [JavaScript Bridge](#javascript-bridge)
- [Security](#security)
- [Embedded Web Resources](#embedded-web-resources)
- [Multiple Windows and Threading](#multiple-windows-and-threading)
- [Platform Support](#platform-support)
- [Examples](#examples)
- [Releases](#releases)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Installation and Building

### Requirements

#### All platforms

- CMake 3.20 or newer.
- A C++20 compiler.
- `nlohmann/json`. CMake uses a system package when available and downloads it otherwise.

#### Windows

- Windows 10 or newer.
- Microsoft Edge WebView2 Runtime.
- No separate WebView2 loader library has to be shipped with the application. MSVC builds link `WebView2LoaderStatic.lib` when it is available. Other toolchains embed the loader and extract it to `<application state>/loader-cache` when needed. An executable-local loader still takes precedence.
- The UI thread uses a single-threaded COM apartment. `App::Create` initializes it.

#### macOS

- macOS 12 or newer.
- Xcode command-line tools.
- Cocoa, WebKit, Foundation, QuartzCore, and CoreGraphics, linked automatically.

#### Linux

- GTK 3.
- WebKitGTK 4.1, or WebKitGTK 4.0 with version 2.40 or newer.
- A running Wayland or X11 display.
- `pkg-config` entries for the selected WebKitGTK version and GTK 3.

On Debian-based distributions:

```sh
sudo apt install cmake ninja-build libgtk-3-dev libwebkit2gtk-4.1-dev
```

If WebKitGTK 4.1 is unavailable, install the 4.0 development package instead.

### Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Build options

| Option | Default | Meaning |
| --- | --- | --- |
| `FLARE_BUILD_EXAMPLES` | `ON` | Build the example applications |
| `FLARE_BUILD_TESTS` | `ON` | Build and register the unit tests |
| `FLARE_SHARED_LIBS` | `OFF` | Build a shared library instead of a static one |
| `FLARE_WEBVIEW2_VERSION` | pinned SDK version | Select the WebView2 NuGet SDK version on Windows |
| `FLARE_WEBVIEW2_STATIC_LOADER` | `ON` | Link the static WebView2 loader on MSVC when the SDK provides it |

The library target is named `flare`. A namespaced alias is available as `Flare::flare`.

### Install

```sh
cmake --install build --prefix /opt/flare
```

The installed package exports the `Flare::flare` target and a package configuration.

### Use from another CMake project

```cmake
find_package(Flare REQUIRED)

add_executable(my_app ${MY_SOURCES})
target_link_libraries(my_app PRIVATE Flare::flare)
```

The `FlareApp` helper is also available after `find_package`. It links the target and can optionally embed a web directory:

```cmake
add_executable(my_app ${MY_SOURCES})
FlareApp(my_app)
```

Without a directory argument, `FlareApp` only links the application target. With a directory argument, every file below that directory is embedded into the executable:

```cmake
FlareApp(my_app "web")
```

The embedded bundle is described in [Embedded Web Resources](#embedded-web-resources).

#### Use as a subdirectory

```cmake
add_subdirectory(flare)
add_executable(my_app ${MY_SOURCES})
FlareApp(my_app "web")
```

In the build tree, the shorter `flare` target is also available.

### Runtime prerequisites

Applications still require the platform web engine at runtime:

- Windows: Microsoft Edge WebView2 Runtime. MSVC links the static loader; other toolchains extract the embedded loader to `%APPDATA%\<applicationId>\loader-cache\WebView2Loader.dll`.
- macOS: the system WebKit framework.
- Linux: GTK 3 and WebKitGTK runtime packages.

Prebuilt example and test archives from the release workflow are described in [Releases](#releases).

### Next steps

- [Quick Start](#quick-start)
- [Window Lifecycle](#window-lifecycle)
- [Platform Support](#platform-support)

## Quick Start

The snippets assume that the Flare API is available. All types live in `namespace Flare`.

### 1. Create the application

```cpp
Flare::AppOptions appOptions;
appOptions.applicationName = "Hello Flare";
appOptions.applicationId = "com.example.hello";
appOptions.applicationVersion = "1.0.0";

auto createdApp = Flare::App::Create(appOptions);
if (!createdApp.IsOk()) {
    return 1;
}
auto app = std::move(createdApp.Value());
```

`App::Create` must run on the UI thread. It initializes the platform application object and, on Windows, the COM apartment.

### 2. Create a window

```cpp
Flare::WindowOptions windowOptions;
windowOptions.title = "Hello Flare";
windowOptions.width = 960;
windowOptions.height = 640;
windowOptions.centered = true;

Flare::BrowserOptions browserOptions;
browserOptions.html = "<html><body><h1>Hello from Flare</h1></body></html>";

auto createdWindow = app->NewWindow(windowOptions, browserOptions);
if (!createdWindow.IsOk()) {
    return 1;
}
auto window = createdWindow.Value();
```

The native window is created hidden. It becomes visible only after the browser is ready according to `WindowOptions::showTrigger`.

### 3. Observe events

```cpp
window->OnBrowserReady([](Flare::Window& readyWindow) {
});

window->OnClosed([](Flare::Window&) {
});

window->OnNavigationCompleted([](const Flare::NavigationCompletedEvent& event) {
    if (!event.isSuccess) {
    }
});
```

Events are delivered on the UI thread. A `Flare::Subscription` returned by an `On...` method can be copied, queried with `IsActive()`, and unsubscribed from any thread.

### 4. Run the loop

```cpp
auto runResult = app->Run();
return runResult.IsOk() ? 0 : 1;
```

The application keeps running until `App::Quit` is called or the last window closes when `quitOnAllWindowsClosed` is enabled.

### Initial content

`BrowserOptions` accepts one primary content source:

```cpp
browserOptions.startUrl = "https://example.com/";
```

```cpp
browserOptions.html = "<html><body>Inline page</body></html>";
browserOptions.htmlBaseUrl = "https://app.local/";
```

```cpp
browserOptions.localFile = "C:/path/to/page.html";
```

```cpp
browserOptions.embeddedBundle = "my_app";
browserOptions.embeddedStartPage = "index.html";
```

```cpp
browserOptions.virtualHost = Flare::VirtualHostMapping {"app.local", "C:/path/to/web"};
```

`startUrl`, `html`, `localFile`, `embeddedBundle`, and `virtualHost` are mutually exclusive. See [Browser API](#browser-api).

### Bridge in one page

JavaScript:

```js
flare.on('server-message', payload => {
    console.log(payload.text);
});

const answer = await flare.invoke('add', { left: 2, right: 3 });
await flare.window.setTitle('Updated');
```

C++:

```cpp
window->On("button-clicked", [](const Flare::JSON& payload) {
});

window->Handle("add", [](const Flare::JSON& payload) -> Flare::Result<Flare::JSON> {
    Flare::JSON result;
    result["sum"] = payload.value("left", 0.0) + payload.value("right", 0.0);
    return result;
});

window->Emit("server-message", {{"text", "Hello from C++"}});
window->Emit("ready");
```

`Flare::JSON` is an alias for `nlohmann::json`. Payload arguments are optional and default to an empty object.

With default bridge options, every registered handler and all window actions are available. Use `BridgeOptions` to restrict handler names, permissions, origins, and window actions. See [JavaScript Bridge](#javascript-bridge) and [Security](#security).

### Threading

- `App::Create`, `App::Run`, `App::Quit`, window creation, and all window methods run on the UI thread.
- `App::Dispatch` and `App::DispatchSync` move work from other threads to the UI thread.
- `Window::Emit`, `App::Emit`, and `Flare::Subscription` operations are safe from other threads.
- Bridge handlers, native event handlers, and asynchronous callbacks run on the UI thread.

### Next steps

- [Window Lifecycle](#window-lifecycle)
- [Window API](#window-api)
- [Browser API](#browser-api)
- [Events](#events)

## Window Lifecycle

Flare guarantees that a window is never shown while it is empty.

### Creation sequence

1. The native window is created hidden.
2. The window background color is applied before any browser work starts.
3. The browser environment and controller are created asynchronously while the window remains hidden.
4. User scripts and the bridge script are registered.
5. `OnBrowserReady` is raised.
6. The window is shown according to `WindowOptions::showTrigger`.

No show happens before `OnBrowserReady`, regardless of the trigger.

### Show triggers

| `ShowTrigger` | The window becomes visible when |
| --- | --- |
| `BROWSER_CREATED` | The browser controller is ready |
| `FIRST_NAVIGATION_COMPLETED` | The first navigation finishes, successfully or not |
| `DOM_CONTENT_LOADED` | The page reports that DOM content has loaded |
| `MANUAL` | C++ calls `Window::Show()` |

The default is `FIRST_NAVIGATION_COMPLETED`.

### Visibility controls

- `visibleOnStart = false` suppresses all automatic showing. The window remains hidden until C++ calls `Show()`.
- `focusOnStart` controls whether the window is focused after its first show.
- `Window::Show()` called before the browser is ready is remembered and executed when readiness is reached.
- `Window::IsBrowserReady()` reports whether the browser is ready.
- `OnBrowserReady` runs once per window when readiness is reached.
- `OnBrowserCreationFailed` carries an `Error` if the engine cannot be created.

### Applying state before the first show

The following window state is applied while the window is still hidden, immediately before its first show:

- `maximizeOnStart`
- `fullscreen`
- restored size, position, and maximized state from state persistence

This prevents a visible resize, reposition, or maximize flash.

### Browser creation failure

If browser creation fails:

- the window remains hidden;
- `OnBrowserCreationFailed` is raised;
- the window can still be queried and closed;
- operations that require the browser return `ErrorCode::BROWSER_NOT_READY`.

On Windows, a missing or blocked WebView2 loader is a common cause. MSVC builds link the static loader. Other toolchains extract the embedded loader to the application state folder, and extraction can fail if that directory is not writable or if security software blocks the file. A missing WebView2 Runtime is another common cause.

### Manual showing

```cpp
windowOptions.visibleOnStart = false;
windowOptions.showTrigger = Flare::ShowTrigger::MANUAL;
```

C++ then decides when to reveal the window:

```cpp
window->OnBrowserReady([](Flare::Window& readyWindow) {
    readyWindow.Show();
});
```

`Show()` is also remembered when the browser is not ready yet, so an early call is safe.

### State persistence

Enable `persistState` and set a stable `stateKey`:

```cpp
windowOptions.persistState = true;
windowOptions.stateKey = "main-window";
```

Flare stores size, position, and maximized state under `App::StateFolder()`. The state is restored before the first show. Use distinct state keys for different windows.

The default state folder is the application identifier directly under the platform data directory:

- Windows: `%APPDATA%\<applicationId>`
- macOS: `~/Library/Application Support/<applicationId>`
- Linux: `$XDG_CONFIG_HOME/<applicationId>` or `~/.config/<applicationId>`

No shared `Flare` directory is added.

### Closing

`Window::Close()` and the native title-bar close both raise the cancellable `OnClose` event. A handler can keep the window open:

```cpp
window->OnClose([](Flare::CloseEvent& event) {
    event.cancel = true;
});
```

The close request then returns `ErrorCode::CANCELLED`. If the close proceeds, `OnClosed` is raised and the window cannot be used again. Every later operation returns `ErrorCode::WINDOW_CLOSED`.

## Window API

### Error and Result types

Public operations return `Flare::Result<T>` or `Flare::Result<void>`. A result holds either a value or an `Flare::Error`.

```cpp
Result<void> Show();
Result<Rect> GetBounds() const;
```

Use:

- `IsOk()`, `IsError()`
- `Value()`, `ValueOr(fallback)`, `GetError()`
- `operator bool`
- `Result<T>::Ok(value)`, `Result<T>::Fail(...)`, `Result<void>::Ok()`

`Result` is not `[[nodiscard]]`. Calls whose errors are not interesting may be written directly:

```cpp
window->Show();
window->Emit("ready");
```

Use the returned value when failure handling matters.

`Flare::Error` contains:

| Field | Meaning |
| --- | --- |
| `code` | Stable `ErrorCode` |
| `message` | Short human-readable description |
| `detail` | Optional platform or engine detail |

`ErrorCode` values:

`NONE`, `INVALID_ARGUMENT`, `NOT_FOUND`, `ALREADY_EXISTS`, `NOT_SUPPORTED`, `PERMISSION_DENIED`, `TIMEOUT`, `CANCELLED`, `BROWSER_NOT_READY`, `WINDOW_CLOSED`, `WRONG_THREAD`, `PLATFORM_FAILURE`, `IO_FAILURE`, `BRIDGE_REJECTED`, `BRIDGE_RATE_LIMITED`, `NAVIGATION_BLOCKED`, `SINGLE_INSTANCE`, `HANDLER_FAILED`, `UNKNOWN`.

`Error::ToString()` produces a stable text representation. `Error::CodeName()` returns the lower-snake name used by bridge errors.

No exception crosses the public API boundary. Handler exceptions are caught and reported through `App::OnUnhandledError` or stderr.

### AppOptions

| Field | Default | Meaning |
| --- | --- | --- |
| `applicationName` | `"Flare Application"` | Display name used by bridge metadata and platform integration |
| `applicationId` | `"com.example.flare"` | Stable identifier used for the default state folder and single-instance lock |
| `applicationVersion` | `"1.0.0"` | Version exposed through `flare.app` |
| `icon` | empty | Default application icon; used by windows without their own icon and by the macOS Dock |
| `assetsFolder` | empty | Base folder for `Icon::FromAsset` values; relative paths resolve against it |
| `stateFolder` | empty | State root; defaults to the platform configuration directory |
| `singleInstance` | `false` | Enables the single-instance guard |
| `singleInstanceId` | empty | Overrides the single-instance identifier; defaults to `applicationId` |
| `dpiAwareness` | `PER_MONITOR_V2` | Windows DPI awareness; other platforms are automatic |
| `theme` | `SYSTEM` | Application appearance preference |
| `quitOnAllWindowsClosed` | `true` | Stops the loop when the last window closes |
| `hardwareAcceleration` | `true` | Disables GPU acceleration on Windows and Linux; macOS has no host toggle |
| `launchArguments` | empty | Arguments forwarded on second-instance activation |

`applicationId` is not registered with the operating system as an application identifier. It names the default state namespace and is used as the default single-instance key.

### App

#### Creation and loop

| Method | Behavior |
| --- | --- |
| `App::Create(AppOptions)` | Creates the application runtime. UI thread only. |
| `Run()` | Runs the platform loop. Call once. |
| `Quit()` | Requests a quit. Open windows go through their cancellable close path. |
| `IsRunning()` | Reports whether the loop is active. |
| `IsOnUiThread()` | Reports whether the caller is on the UI thread. |
| `StateFolder()` | Returns the absolute state root. |
| `Options()` | Returns the effective application options. |
| `Capabilities()` | Returns the platform capability snapshot. |

#### Windows and groups

| Method | Behavior |
| --- | --- |
| `NewWindow(WindowOptions, BrowserOptions)` | Creates a window with default bridge options. |
| `NewWindow(WindowOptions, BrowserOptions, BridgeOptions)` | Creates a window with explicit bridge configuration. |
| `Windows()` | Snapshot of open windows. |
| `FindWindow(WindowId)` | Looks up an open window. |
| `CreateWindowGroup(name)` | Creates a group or returns the existing group. |
| `FindWindowGroup(name)` | Looks up a group. |

`NewWindow` returns `Result<std::shared_ptr<Window>>`. The application keeps a strong reference to every open window until it closes.

#### Application events

| Event | Payload |
| --- | --- |
| `OnReady` | no payload |
| `OnQuit` | no payload |
| `OnAllWindowsClosed` | no payload |
| `OnSecondInstance` | `const std::vector<std::string>&` |
| `OnUnhandledError` | `const Error&` |

`OnReady` replays only the newly registered handler if the loop is already running. Pending second-instance arguments are delivered when a handler is registered.

#### Application-level bridge handlers

| Method | Behavior |
| --- | --- |
| `Emit(name, payload = {})` | Broadcasts a custom event to every ready window. |
| `Handle(name, handler, options = {})` | Registers a global RPC handler. Window handlers take precedence. |
| `Unhandle(name)` | Removes a global handler. |

### WindowOptions

#### Identity and geometry

| Field | Default | Meaning |
| --- | --- | --- |
| `title` | `"Flare"` | Native title |
| `icon` | empty | Per-window icon from a path, asset, or native resource; Windows accepts ICO, PNG, JPEG, GIF, BMP, and native resources, Linux uses GTK formats or icon-theme names, macOS does not support per-window icons |
| `width`, `height` | 1024, 768 | Initial client size |
| `minWidth`, `minHeight`, `maxWidth`, `maxHeight` | 0 | 0 means unconstrained |
| `x`, `y` | 0 | Used when `hasPosition` is true |
| `hasPosition` | `false` | Uses an absolute position |
| `centered` | `true` | Centers when no position is set |

#### Chrome and state

| Field | Default | Meaning |
| --- | --- | --- |
| `resizable`, `movable`, `minimizable`, `maximizable`, `closable` | `true` | Platform window affordances |
| `fullscreen` | `false` | Enters fullscreen while hidden |
| `alwaysOnTop` | `false` | Floating or topmost level |
| `frameless` | `false` | Removes native chrome |
| `transparent` | `false` | Transparent background where supported |
| `backgroundColor` | opaque dark gray | Painted before the browser exists |
| `showInTaskbar` | `true` | Per-window taskbar or Dock visibility where supported |
| `opacity` | `1.0` | Window opacity |
| `borderlessShadow` | `true` | Native or layer shadow where supported |
| `roundedCorners` | `true` | Native or layer rounding where supported |
| `titleBarStyle` | `NATIVE` | `NATIVE`, `CUSTOM`, or `HIDDEN` |
| `draggableRegions` | empty | Client-coordinate regions that start a native window drag |
| `contentProtection` | `false` | Excludes the window from screen capture where supported |

#### Visibility and lifecycle

| Field | Default | Meaning |
| --- | --- | --- |
| `visibleOnStart` | `true` | When false, suppresses automatic showing |
| `showTrigger` | `FIRST_NAVIGATION_COMPLETED` | `BROWSER_CREATED`, `FIRST_NAVIGATION_COMPLETED`, `DOM_CONTENT_LOADED`, `MANUAL` |
| `focusOnStart` | `true` | Focuses after the first show |
| `maximizeOnStart` | `false` | Maximizes before the first show |
| `theme` | `SYSTEM` | Per-window appearance: `SYSTEM`, `LIGHT`, `DARK` |
| `useDocumentTitle` | `true` | Lets page titles update the native title until `SetTitle` is called |

#### Relationships and persistence

| Field | Default | Meaning |
| --- | --- | --- |
| `parent` | `nullptr` | Non-owning parent window pointer |
| `modal` | `false` | Disables the parent while the child is shown |
| `persistState` | `false` | Saves bounds and maximized state |
| `stateKey` | empty | State key; defaults to the title |

`ValidateWindowOptions` checks dimensions, constraints, opacity, title, and draggable regions.

### Window methods

#### Identity and configuration

`GetId`, `GetApp`, `Options`, `BrowserSettings`, `BridgeSettings`, `Capabilities`.

#### Visibility and state

`Show`, `Hide`, `Minimize`, `Maximize`, `Restore`, `Close`, `Focus`, `StartDrag`.

#### Title, size, and decoration

`SetTitle`, `GetTitle`, `SetSize`, `SetPosition`, `SetMinSize`, `SetMaxSize`, `SetFullscreen`, `SetAlwaysOnTop`, `SetOpacity`, `GetOpacity`, `Center`, `SetBackgroundColor`, `SetTheme`, `SetContentProtection`, `SetResizable`, `SetMovable`, `SetMinimizable`, `SetMaximizable`, `SetClosable`, `SetShowInTaskbar`, `SetIcon`, `SetDraggableRegions`.

#### Queries

`GetBounds`, `IsVisible`, `IsWindowMaximized`, `IsWindowMinimized`, `IsFocused`, `IsFullscreen`, `IsBrowserReady`, `IsClosed`, `GetDpiScale`, `GetState`.

#### Browser control

`Navigate`, `NavigateToString`, `LoadFile`, `Reload`, `GoBack`, `GoForward`, `Stop`, `ExecuteScript`, `OpenDevTools`, `CloseDevTools`, `PrintToPdf`, `Print`, `CapturePreview`, `GetCookies`, `SetCookie`, `DeleteCookie`, `ClearBrowsingData`, `SetZoom`, `GetZoom`, `FindInPage`, `StopFindInPage`, `GetUrl`, `GetBrowserTitle`, `SendWebMessage`, `SetUserAgent`, `GetUserAgent`.

#### Bridge

`Emit`, `On`, `Off`, `Handle`, `Unhandle`.

Custom bridge names use `[A-Za-z0-9_][A-Za-z0-9_.:-]*` and are limited to 128 characters.

#### Icons

An icon can come from a file path, an asset below `AppOptions::assetsFolder`, or a native resource:

```cpp
windowOptions.icon = Flare::Icon::FromPath("C:/path/to/icon.png");
```

```cpp
appOptions.assetsFolder = "C:/path/to/assets";
windowOptions.icon = Flare::Icon::FromAsset("icons/app.png");
```

```cpp
windowOptions.icon = Flare::Icon::FromResource("IDI_APPICON");
```

The app-level icon is used as a default for windows without their own icon:

```cpp
appOptions.icon = Flare::Icon::FromAsset("icons/app.png");
```

```cpp
window->SetIcon(Flare::Icon::FromPath("C:/path/to/icon.png"));
window->SetIcon(Flare::Icon::FromResource("IDI_APPICON"));
```

Icons are assigned as `Flare::Icon` values; implicit path or string assignment is not accepted.

Windows accepts ICO, BMP, PNG, JPEG, and GIF files and native icon resources. Linux accepts GTK image formats and icon-theme names through `Icon::FromResource`. macOS supports the application icon through `AppOptions::icon`; per-window icons return `ErrorCode::NOT_SUPPORTED` there.

`assetsFolder` may be absolute or relative. Relative values are resolved to an absolute directory when the application is created.

### Groups

`JoinGroup`, `LeaveGroup`, `Groups`.

### Ownership and lifetime

- `App` owns every open window.
- The returned `std::shared_ptr<Window>` may be retained.
- After `OnClosed`, the window is unusable and every operation returns `ErrorCode::WINDOW_CLOSED`.
- `App` must outlive its windows.
- `WindowGroup` holds weak references and never extends window lifetime.

### Native versus JavaScript

The Window API is native-only. JavaScript has no access to these methods except the explicitly whitelisted `flare.window` actions documented in [JavaScript Bridge](#javascript-bridge). Native events are never forwarded to JavaScript.

## Browser API

`BrowserOptions` configures the embedded engine for one window. Most browser operations are methods on `Window`.

### Initial content

Set exactly one primary content source:

#### URL

```cpp
browserOptions.startUrl = "https://example.com/";
```

#### HTML string

```cpp
browserOptions.html = "<html><body>Inline page</body></html>";
browserOptions.htmlBaseUrl = "https://app.local/";
```

`htmlBaseUrl` provides the base URL used for relative resources and origin checks. Without it, the page has an opaque origin.

#### Local file

```cpp
browserOptions.localFile = "C:/path/to/page.html";
```

Relative resources load from the containing folder. `allowFileAccessFromFileUrls` controls whether local pages may read other local files.

#### Embedded bundle

```cpp
browserOptions.embeddedBundle = "my_app";
browserOptions.embeddedStartPage = "index.html";
```

The bundle is produced by the `FlareApp` CMake helper. See [Embedded Web Resources](#embedded-web-resources).

#### Virtual host

```cpp
browserOptions.virtualHost = Flare::VirtualHostMapping {"app.local", "C:/path/to/web"};
```

Windows maps the folder to a real `https://app.local/` origin. macOS and Linux serve the folder through the custom scheme stored in `VirtualHostMapping::scheme`, which defaults to `flare-app`. Start URLs beginning with `https://app.local/` are rewritten to the custom scheme on those platforms. Absolute subresource URLs written as `https://app.local/...` are not rewritten, so use relative URLs in pages served outside Windows.

### BrowserOptions

#### Content and profile

| Field | Default | Meaning |
| --- | --- | --- |
| `startUrl`, `html`, `htmlBaseUrl`, `localFile`, `embeddedBundle` | empty | Initial content; mutually exclusive except `htmlBaseUrl` accompanies `html` |
| `embeddedStartPage` | `index.html` | Entry file inside an embedded bundle |
| `virtualHost` | empty | Host, folder, and custom scheme |
| `userDataFolder` | empty | Empty uses the application state folder; relative paths resolve under it |
| `incognito` | `false` | Ephemeral browsing data |
| `clearCacheOnStart` | `false` | Clears browsing data before the first navigation |

#### Appearance and behavior

| Field | Default | Meaning |
| --- | --- | --- |
| `backgroundColor` | transparent | WebView background; the window color shows through when alpha is zero |
| `zoomFactor` | `1.0` | Between 0.25 and 5.0 |
| `zoomControlEnabled` | `true` | Zoom UI or gesture where available |
| `hideScrollbars` | `false` | Hides page scrollbars with injected CSS |
| `contextMenuEnabled` | `true` | Default context menu |
| `defaultScriptDialogsEnabled` | `true` | Native alert, confirm, and prompt dialogs |
| `javascriptEnabled` | `true` | Page JavaScript |
| `webSecurityEnabled` | `true` | Engine security toggle where exposed |
| `allowFileAccessFromFileUrls` | `true` | Local pages may read local files |

#### Engine integration

| Field | Default | Meaning |
| --- | --- | --- |
| `userAgent` | empty | Engine user agent |
| `language` | empty | Locale |
| `devToolsEnabled` | `false` | Engine developer tools |
| `statusBarEnabled` | `false` | Windows only |
| `browserAcceleratorKeysEnabled` | `true` | Windows only |
| `autofillEnabled`, `passwordSaveEnabled` | `true` | Windows settings where available |
| `pinchZoomEnabled` | `true` | Windows and macOS |
| `swipeNavigationEnabled` | `true` | Windows and Linux |
| `browserArguments` | empty | Windows engine arguments |
| `proxy` | disabled | Explicit proxy settings; platform support varies |
| `extraHeaders` | empty | Main-frame header injection where supported |

#### Navigation and resources

| Field | Default | Meaning |
| --- | --- | --- |
| `allowedUrlPatterns`, `blockedUrlPatterns` | empty | Main-frame navigation filters; resource filters on Windows interception |
| `interceptRequests`, `requestFilters` | false, empty | Resource request interception |
| `interceptResponses`, `responseFilters` | false, empty | Resource response observation |
| `customSchemes` | empty | Custom scheme to handler map |

#### Permissions, dialogs, and scripts

| Field | Default | Meaning |
| --- | --- | --- |
| `permissionPolicy` | all `ASK` | Camera, microphone, geolocation, notifications, clipboard read, clipboard write |
| `download` | not blocked | Download directory, ask-on-save, block-all |
| `documentStartScripts` | empty | Scripts injected before page scripts |
| `documentEndScripts` | empty | Scripts injected at document end |

`ValidateBrowserOptions` checks content-source exclusivity, embedded bundle names and start-page paths, zoom bounds, permissions, proxy consistency, custom scheme names, virtual host folder existence, scripts, headers, patterns, and arguments.

### Navigation

```cpp
window->Navigate("https://example.com/");
window->NavigateToString("<html><body>Page</body></html>", "https://app.local/");
window->LoadFile("C:/path/to/page.html");
window->Reload();
window->GoBack();
window->GoForward();
window->Stop();
```

`Navigate`, `LoadFile`, and other browser operations require a ready browser. Use `IsBrowserReady()` or wait for `OnBrowserReady`.

### Scripts

```cpp
window->ExecuteScript("document.title", [](Flare::Result<Flare::JSON> result) {
    if (result.IsOk()) {
        const Flare::JSON value = result.Value();
    }
});
```

The callback receives a JSON representation of the JavaScript result. A JavaScript exception or engine failure becomes an `Error`.

Injected scripts:

- `documentStartScripts` run before page scripts.
- `documentEndScripts` run at document end.

On Windows, document-end scripts are wrapped in a DOM-content-loaded callback. A strict page CSP can block scripts injected that way.

### Raw web messages

```cpp
window->SendWebMessage({{"type", "native-notification"}, {"value", 42}});
```

`OnWebMessageReceived` delivers raw messages from the page. Bridge messages are recognized by the `flare` marker and do not appear as raw messages.

Raw messages have no bridge authentication or replay protection. Do not use them for privileged commands.

### Page capture

```cpp
Flare::CaptureOptions options;
options.format = Flare::ImageFormat::PNG;

window->CapturePreview(options, [](Flare::Result<std::vector<std::uint8_t>> result) {
    if (result.IsOk()) {
        const std::vector<std::uint8_t>& image = result.Value();
    }
});
```

`ImageFormat` values are `PNG` and `JPEG`. `quality` applies to JPEG.

### Printing and PDF

```cpp
window->Print();
```

`Print()` opens the platform print flow where supported.

```cpp
Flare::PdfOptions pdf;
pdf.printBackground = true;

window->PrintToPdf(pdf, "C:/path/to/output.pdf", [](Flare::Result<void> result) {
});
```

Windows supports the full PDF option set. macOS supports the default layout; unsupported layout settings return `ErrorCode::NOT_SUPPORTED`. WebKitGTK does not expose PDF export through the host API.

### Cookies

`Cookie` contains `name`, `value`, `domain`, `path`, `expiresUnixSeconds`, `secure`, `httpOnly`, and `sameSite`.

```cpp
Flare::Cookie cookie;
cookie.name = "session";
cookie.value = "value";
cookie.domain = "app.local";
cookie.path = "/";

window->SetCookie(cookie, [](Flare::Result<void> result) {
});

window->GetCookies("https://app.local/", [](Flare::Result<std::vector<Flare::Cookie>> result) {
});

window->DeleteCookie("https://app.local/", "session", [](Flare::Result<void> result) {
});
```

`CookieSameSite` values are `UNSPECIFIED`, `NONE`, `LAX`, and `STRICT_MODE`. macOS reads cookies without a SameSite value as `UNSPECIFIED`; setting SameSite uses the platform cookie properties.

### Browsing data

```cpp
Flare::ClearDataKind kinds = Flare::ClearDataKind::COOKIES | Flare::ClearDataKind::CACHE;
window->ClearBrowsingData(kinds, [](Flare::Result<void> result) {
});
```

`ClearDataKind` values: `NONE`, `COOKIES`, `CACHE`, `LOCAL_STORAGE`, `SESSION_STORAGE`, `INDEXED_DB`, `SERVICE_WORKERS`, `ALL`. The type supports bitwise combination.

### Downloads

Configuration:

```cpp
browserOptions.download.directory = "C:/Downloads";
browserOptions.download.askWhereToSave = true;
browserOptions.download.blocked = false;
```

Events:

- `OnDownloadStarting` may cancel or choose a save path.
- `OnDownloadProgress` reports received and total bytes.
- `OnDownloadCompleted` reports the final state and path.

`DownloadInfo` contains an id, URL, suggested filename, MIME type, path, byte counts, and a `DownloadState` value: `PENDING`, `IN_PROGRESS`, `COMPLETED`, `CANCELLED`, or `INTERRUPTED`.

### Permissions

`BrowserOptions::permissionPolicy` controls camera, microphone, geolocation, notifications, clipboard read, and clipboard write.

`PermissionState` values:

- `ASK`: raise `OnPermissionRequested`.
- `ALLOW`: grant directly.
- `DENY`: deny directly.

With `ASK` and no C++ handler, the request is denied. A handler sets `allow` and optionally `remember`. Engine support varies by platform; see [Platform Support](#platform-support).

### Script dialogs and context menus

`OnScriptDialog` lets C++ handle alert, confirm, prompt, and before-unload dialogs. Set `handled` to suppress the engine dialog and provide `accepted` or `resultText`.

`OnContextMenuRequested` carries the request context and may cancel the default menu. macOS supports disabling the menu but does not expose the same interception event.

### Authentication and certificates

`OnAuthenticationRequested` supports default handling, cancellation, or explicit credentials. Credentials are passed to the engine and are not stored by Flare.

`OnCertificateError` is a security decision. With no handler, Flare does not allow an invalid certificate. A handler that allows the connection must make an informed trust decision.

### Resources and interception

`OnNavigationStarting` may cancel a main-frame navigation or supply a redirect URL.

With `interceptRequests` and `requestFilters`, `OnWebResourceRequested` may cancel or redirect a matching resource. Windows provides full request interception.

With `interceptResponses` and `responseFilters`, `OnWebResourceResponseReceived` reports status, MIME type, headers, and response details. Windows provides the broadest support; Linux exposes partial response metadata; macOS does not expose generic interception.

### Custom schemes

```cpp
browserOptions.customSchemes["my-app"] = [](const Flare::CustomSchemeRequest& request) {
    Flare::CustomSchemeResponse response;
    response.statusCode = 200;
    response.mimeType = "text/html";
    response.body = "<html><body>Custom scheme</body></html>";
    return Flare::Result<Flare::CustomSchemeResponse>::Ok(response);
};
```

A handler receives the URL, scheme, method, and headers and returns a status code, MIME type, body, and headers.

### Zoom and find

```cpp
window->SetZoom(1.25);
const Flare::Result<double> zoom = window->GetZoom();
```

`FindInPage(text, options, callback)` returns occurrence information where the engine exposes it. `FindOptions` controls direction, case matching, selection scope, and a maximum match count. `StopFindInPage()` stops the search.

Windows implements find with page scripting. macOS reports whether a match exists but cannot enumerate match counts or stop a search. WebKitGTK provides native find support.

### DevTools

`OpenDevTools()` and `CloseDevTools()` are available where the engine exposes them. WebView2 cannot close DevTools from the host. macOS uses a private inspector property to close it. Keep DevTools disabled in production.

## Events

Flare has two completely separate event channels:

1. **Native events** are delivered to C++ only. They are never forwarded to JavaScript.
2. **Bridge events** are developer-defined custom events sent explicitly with `Window::Emit`, `App::Emit`, `WindowGroup::Emit`, or `flare.emit`.

This page describes native events. Bridge events are documented in [JavaScript Bridge](#javascript-bridge).

### Subscriptions

Every `On...` registration returns a `Flare::Subscription`.

```cpp
Flare::Subscription subscription = window->OnNavigationCompleted([](const Flare::NavigationCompletedEvent& event) {
});

subscription.Unsubscribe();
```

A subscription can be copied, queried with `IsActive()`, and unsubscribed from any thread. Registration and event delivery happen on the UI thread.

Bridge custom events use a separate API:

```cpp
Flare::Subscription subscription = window->On("server-message", [](const Flare::JSON& payload) {
});
window->Off("server-message");
```

### Exceptions in handlers

Exceptions thrown by native event handlers are caught and reported through `App::OnUnhandledError` or stderr. They do not cross the native boundary. A throwing handler does not terminate the application loop.

### Application events

| Event | Payload | Meaning |
| --- | --- | --- |
| `OnReady` | none | The application loop is ready |
| `OnQuit` | none | The application is quitting |
| `OnAllWindowsClosed` | none | The last window closed |
| `OnSecondInstance` | `const std::vector<std::string>&` | Another instance forwarded arguments |
| `OnUnhandledError` | `const Error&` | An internal or handler error was reported |

Registering `OnReady` after the loop is already running invokes only the newly registered handler. Pending second-instance arguments are delivered when a handler is registered.

### Window lifecycle events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnCreated` | `Window&` | none |
| `OnBrowserReady` | `Window&` | none |
| `OnBrowserCreationFailed` | `const BrowserCreationFailedEvent&` | carries `Error` |
| `OnShow` | `Window&` | none |
| `OnHide` | `Window&` | none |
| `OnClose` | `CloseEvent&` | `cancel` |
| `OnClosed` | `Window&` | none |
| `OnMove` | `const BoundsEvent&` | none |
| `OnResize` | `const ResizeEvent&` | none |
| `OnMinimize` | `Window&` | none |
| `OnMaximize` | `Window&` | none |
| `OnRestore` | `Window&` | none |
| `OnFocus` | `Window&` | none |
| `OnBlur` | `Window&` | none |
| `OnFullscreenEnter` | `Window&` | none |
| `OnFullscreenLeave` | `Window&` | none |
| `OnDpiChanged` | `const DpiChangedEvent&` | none |
| `OnThemeChanged` | `const ThemeChangedEvent&` | none |
| `OnFileDrop` | `const FileDropEvent&` | none |

`OnClose` is cancellable. Setting `event.cancel = true` keeps the window open and makes the close request return `ErrorCode::CANCELLED`.

### Input events

| Event | Payload |
| --- | --- |
| `OnKeyDown`, `OnKeyUp` | `const KeyEvent&` |
| `OnMouseDown`, `OnMouseUp`, `OnMouseMove`, `OnDoubleClick` | `const MouseEvent&` |
| `OnMouseWheel` | `const WheelEvent&` |

Input payloads include key values, DOM-style key codes, virtual key identifiers, modifiers, button, coordinates, wheel deltas, and a cancellation field where the platform supports it.

Platform support differs. All platforms expose key events to the host. macOS and Linux also expose mouse and wheel events, while WebView2 does not expose mouse, wheel, or double-click events. Flare never synthesizes missing native events.

`Modifier` values are `NONE`, `ALT`, `CONTROL`, `SHIFT`, `META`, `CAPS_LOCK`, and `NUM_LOCK`. `MouseButton` values are `NONE`, `LEFT`, `MIDDLE`, `RIGHT`, `EXTRA_1`, and `EXTRA_2`. `MouseEventType` values are `DOWN`, `UP`, `MOVE`, and `DOUBLE_CLICK_EVENT`.

### Navigation and page events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnNavigationStarting` | `NavigationStartingEvent&` | `cancel`, `redirectUrl` |
| `OnNavigationCompleted` | `const NavigationCompletedEvent&` | none |
| `OnSourceChanged` | `const SourceChangedEvent&` | none |
| `OnDocumentTitleChanged` | `const DocumentTitleEvent&` | none |
| `OnFaviconChanged` | `const FaviconEvent&` | none |
| `OnContentLoading` | `const ContentLoadingEvent&` | none |
| `OnDomContentLoaded` | `Window&` | none |

`NavigationCompletedEvent` reports success, HTTP status code, URL, and engine error details. Document-title and favicon events are available where the engine exposes them.

### Permission and dialog events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnPermissionRequested` | `PermissionRequestEvent&` | `allow`, `remember` |
| `OnScriptDialog` | `ScriptDialogEvent&` | `handled`, `accepted`, `resultText` |
| `OnContextMenuRequested` | `ContextMenuEvent&` | `cancel` |
| `OnAuthenticationRequested` | `AuthenticationRequestEvent&` | `useDefault`, `cancel`, credentials |
| `OnCertificateError` | `CertificateErrorEvent&` | `allow` |

`PermissionKind` values are `CAMERA`, `MICROPHONE`, `GEOLOCATION`, `NOTIFICATIONS`, `CLIPBOARD_READ`, `CLIPBOARD_WRITE`, `AUTOPLAY`, and `OTHER`.

`ScriptDialogType` values are `ALERT`, `CONFIRM`, `PROMPT`, and `BEFORE_UNLOAD`.

With no permission handler, an `ASK` policy denies the request. With no certificate handler, invalid certificates are not allowed.

### Download events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnDownloadStarting` | `DownloadStartingEvent&` | `cancel`, `savePath` |
| `OnDownloadProgress` | `const DownloadProgressEvent&` | none |
| `OnDownloadCompleted` | `const DownloadCompletedEvent&` | none |

`DownloadInfo` contains an id, URL, suggested filename, MIME type, path, byte counts, and a `DownloadState` value: `PENDING`, `IN_PROGRESS`, `COMPLETED`, `CANCELLED`, or `INTERRUPTED`.

### New windows

`OnNewWindowRequested` receives `NewWindowRequestedEvent&`. The engine's own popup window is always denied. If the navigation should continue, C++ creates a Flare window and uses the requested URL.

JavaScript cannot create a window.

### Resource events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnWebResourceRequested` | `WebResourceRequestEvent&` | `cancel`, `redirectUrl` |
| `OnWebResourceResponseReceived` | `const WebResourceResponseEvent&` | none |
| `OnWebMessageReceived` | `const WebMessageEvent&` | none |

Resource interception and response observation depend on the engine. Windows provides the broadest support, Linux exposes partial response metadata, and macOS does not expose generic request or response interception.

`OnWebMessageReceived` delivers raw web messages. Bridge messages are handled by the bridge and are not delivered as raw messages.

### Other browser events

| Event | Payload | Decision fields |
| --- | --- | --- |
| `OnProcessFailed` | `const ProcessFailedEvent&` | none |
| `OnFullscreenRequested` | `FullscreenRequestEvent&` | `allow` |
| `OnZoomChanged` | `const ZoomChangedEvent&` | none |

`ResourceType` values are `OTHER`, `DOCUMENT`, `STYLESHEET`, `IMAGE`, `MEDIA`, `SCRIPT`, `FONT`, `XHR`, `FETCH`, and `WEB_SOCKET`.

### Native events are C++ only

The bridge script does not register native engine event listeners other than its own message channel. Resize, move, focus, input, navigation, download, permission, dialog, certificate, and resource events exist only as C++ registrations.

To inform JavaScript, a C++ handler explicitly emits a custom event:

```cpp
window->OnResize([window](const Flare::ResizeEvent& event) {
    window->Emit("layout-changed", {
        {"width", event.bounds.width},
        {"height", event.bounds.height}
    });
});
```

## JavaScript Bridge

Flare has two event channels:

1. **Native events** are delivered only to C++ handlers.
2. **Bridge events** are custom events and RPC calls explicitly sent by C++ or JavaScript.

The bridge never forwards native events to JavaScript. It also never lets JavaScript create a window, list windows, address another window, or control anything except the window that hosts the page.

### JavaScript API

The bridge defines a normal, readable global object named `flare`. It is available in the top-level page. On Windows, iframe sessions can be enabled explicitly. On macOS and Linux, the bridge is top-frame-only.

#### Events

```js
const unsubscribe = flare.on('server-message', payload => {
});

flare.once('ready', payload => {
});

flare.off('server-message', handler);
flare.off('server-message');
unsubscribe();
```

`flare.on` subscribes to custom events emitted by C++ through `Window::Emit`, `App::Emit`, or `WindowGroup::Emit` for this window.

- `once` removes the registration before invoking the handler.
- `off(name, handler)` removes one registration.
- `off(name)` removes every registration for the event name.
- The returned function unsubscribes that registration.

Event names are validated on both sides. An exception in one JavaScript handler is reported asynchronously through `console.error` and does not prevent other handlers from running.

#### Emit

```js
flare.emit('button-clicked', { id: 7, label: 'Save' });
```

`flare.emit` is fire-and-forget. The payload argument is optional and defaults to an empty object. The call is queued until the bridge handshake completes. C++ receives it through `Window::On` or `App`-level registration. It never reaches another window unless C++ relays it.

#### Invoke

```js
try {
    const result = await flare.invoke('add', { left: 2, right: 3 });
} catch (error) {
    console.error(error.code, error.message);
}
```

`flare.invoke` sends an RPC request to a handler registered with `Window::Handle` or `App::Handle`. The argument is optional and defaults to an empty object. It returns a Promise that resolves to the handler's JSON result or rejects with an `Error` carrying a stable `code`.

C++ handlers are synchronous and run on the UI thread. Requests are queued until the handshake completes. They time out after `BridgeOptions::invocationTimeoutMs`, 15000 ms by default. Request ids correlate responses; late responses are ignored.

#### Window actions

Every action returns a Promise.

```js
await flare.window.show();
await flare.window.hide();
await flare.window.minimize();
await flare.window.maximize();
await flare.window.restore();
await flare.window.close();
await flare.window.focus();
await flare.window.setTitle('Title');
await flare.window.setSize(1200, 800);
await flare.window.setPosition(120, 80);
await flare.window.setOpacity(0.95);
await flare.window.setFullscreen(true);
await flare.window.setAlwaysOnTop(true);
await flare.window.center();
await flare.window.getBounds();
await flare.window.isMaximized();
await flare.window.isMinimized();
await flare.window.isVisible();
await flare.window.isFocused();
await flare.window.startDrag();
```

`getBounds` resolves to `{x, y, width, height}`. Boolean getters resolve to booleans. Mutating actions resolve to `null`.

`close` runs the normal cancellable close path in C++. If a C++ handler cancels the close, the Promise rejects with `cancelled`.

Every action is validated natively against `BridgeOptions::allowedWindowActions`. The JavaScript-side precheck is only a convenience. Window actions are accepted only from the top-level session.

`startDrag` asks the native window to begin a move drag. `WindowOptions::draggableRegions` and elements styled with `-webkit-app-region: drag` both call `startDrag` on primary-button mousedown. Interactive elements, `[data-flare-no-drag]`, and `-webkit-app-region: no-drag` subtrees are excluded. CSS regions require an enabled bridge with `START_DRAG` in `allowedWindowActions`; native regions also participate in Windows hit testing.

#### Application information

```js
flare.app.name;
flare.app.version;
flare.app.flareVersion;
flare.app.platform;
```

These values are read-only and are supplied by C++. There is no `flare.app.quit`, no window creation helper, and no window registry.

#### Channel detection

The bridge detects WebView2 or WebKit automatically. If neither native message channel is available, the script does not define `flare`.

The bridge is intentionally readable and can be inspected in DevTools. Security comes from native validation, not from hiding the script.

### C++ API

#### Custom events

```cpp
window->On("button-clicked", [](const Flare::JSON& payload) {
});

Flare::Subscription subscription = window->On("user-updated", handler);
subscription.Unsubscribe();

window->Off("user-updated");

window->Emit("server-message", {{"text", "hello"}});
window->Emit("ready");
```

`On` receives only `flare.emit` payloads from this window. `Emit` sends a custom event to `flare.on` in this window.

Emitting before the handshake is queued up to `BridgeOptions::maxPendingEvents`. The queue is flushed after the handshake. A failed navigation resets the session but keeps events intended for the next document.

`Window::Emit` is thread-safe. Calls from other threads are queued to the UI thread. `Window::On` and `Handle` may be called before the browser exists.

#### RPC handlers

```cpp
window->Handle("add", [](const Flare::JSON& payload) -> Flare::Result<Flare::JSON> {
    Flare::JSON result;
    result["sum"] = payload.value("left", 0.0) + payload.value("right", 0.0);
    return result;
});

app->Handle("version", [](const Flare::JSON&) {
    return Flare::JSON {{"version", "1.4.0"}};
});
```

Window handlers are checked before application handlers. `Unhandle` removes a registration.

With default bridge options, every registered handler name is callable. Setting `BridgeOptions::allowedHandlerNames` to a non-empty set turns it into a native whitelist and rejects every other name with `permission_denied`.

`HandlerOptions::permission` is enforced when `grantedPermissions` is non-empty or `hardenedMode` is enabled. A non-empty grant list must contain the permission. `HandlerOptions::allowedOrigins` restricts the calling session origin.

Handlers run on the UI thread. A thrown exception becomes `handler_failed` and is reported through `App::OnUnhandledError`.

#### Broadcasting

```cpp
app->Emit("theme-changed", {{"dark", true}});
group->Emit("document-saved", {{"id", 5}});
```

`App::Emit` sends to every ready window. `WindowGroup::Emit` sends to the group's live members. These are the only cross-window messaging paths. JavaScript cannot address another window or join a group.

### Protocol

All bridge traffic is JSON. Every object carries `flare: 1` and `v: 1`.

#### Handshake

JavaScript sends:

```json
{"flare":1,"v":1,"type":"hello","nonce":"8f14e45fceea167a5a36dedd4bea2543"}
```

Native code validates the source origin and frame, creates a session id and a 32-byte secret token, and replies to the top frame:

```json
{
  "flare": 1,
  "v": 1,
  "type": "handshake",
  "sid": "session-id",
  "token": "secret-token",
  "limits": {
    "maxMessageBytes": 1048576,
    "maxPayloadBytes": 524288,
    "rateLimitPerSecond": 120,
    "invokeTimeoutMs": 15000,
    "clockSkewSeconds": 300
  },
  "actions": ["show", "hide", "getBounds"],
  "app": {
    "name": "My App",
    "version": "1.4.0",
    "flareVersion": "1.0.0",
    "platform": "windows"
  },
  "config": {
    "allowIframes": false,
    "iframeSession": false,
    "draggableRegions": []
  }
}
```

The token is delivered only to the top frame. It is not broadcast to child frames.

The JavaScript bridge queues outbound operations until this message arrives. Subsequent native-to-JavaScript messages use `event`, `result`, `error`, or `config`.

#### Signed JavaScript-to-native messages

```json
{
  "flare": 1,
  "v": 1,
  "type": "invoke",
  "sid": "session-id",
  "seq": 3,
  "ts": 1760000000000,
  "nonce": "2b7e151628aed2a6abf7158809cf4f3c",
  "name": "add",
  "id": "req-3-91af2c",
  "payload": "{\"left\":2,\"right\":3}",
  "mac": "base64url-hmac-sha256"
}
```

Message types:

- `emit`: `name`, optional empty `id`, JSON-string `payload`, no response.
- `invoke`: `name`, `id`, JSON-string `payload`, response with the same `id`.
- `windowAction`: whitelisted action name, `id`, JSON-string `payload`, response with the same `id`.

The payload is a JSON-encoded string so the MAC covers an identical byte sequence on both sides. Native code parses the payload only after verifying the MAC.

The canonical MAC input is:

```text
v1|<sid>|<seq>|<ts>|<nonce>|<type>|<name>|<id>|<payload>
```

The MAC is HMAC-SHA256 with the session token as the key, encoded as unpadded base64url. `seq` starts at 1 and increases for every signed message. `nonce` is 16 random bytes in hex. `ts` is Unix milliseconds.

Nonce and sequence are checked for replay. Timestamps outside `clockSkewSeconds` are rejected. Native state is updated only after a valid MAC. Rate limits apply after authentication, so forged messages cannot usefully consume a session's budget.

#### Native-to-JavaScript messages

```json
{"flare":1,"v":1,"type":"event","name":"server-message","payload":{"text":"hello"}}
{"flare":1,"v":1,"type":"result","id":"req-3-91af2c","payload":{"sum":5}}
{"flare":1,"v":1,"type":"error","id":"req-3-91af2c","code":"permission_denied","message":"..."}
{"flare":1,"v":1,"type":"config","config":{"draggableRegions":[]}}
```

Native-to-JavaScript messages are trusted and are not signed. Only `event`, `result`, `error`, `config`, and the handshake are recognized. Unknown types are ignored.

#### Validation limits

Native validation checks:

- raw message size;
- JSON depth and node count;
- payload size and JSON validity;
- session existence;
- frame kind;
- source origin;
- timestamp;
- sequence and nonce;
- MAC;
- rate limit;
- handler name, handler origin, permission, and window action authorization.

Stable JavaScript error codes:

| Code | Meaning |
| --- | --- |
| `invalid_argument` | Malformed name, payload, argument type, or window action arguments |
| `not_found` | No handler with that name |
| `permission_denied` | Origin, frame, whitelist, permission, or window action denied |
| `rate_limited` | Session token bucket exhausted |
| `rejected` | Session, MAC, timestamp, or replay rejection |
| `handler_failed` | C++ handler threw |
| `timeout` | No reply within the invocation or handshake timeout |
| `cancelled` | Close was cancelled or an operation was superseded |
| `browser_not_ready` | Browser is not available |
| `window_closed` | Window closed |
| `internal` | Platform or unknown failure |

### BridgeOptions

| Field | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Injects and accepts the bridge |
| `hardenedMode` | `false` | Requires explicit origins and disables inspection helpers |
| `allowIframes` | `false` | Enables iframe sessions where the engine supports frame targeting |
| `allowedOrigins` | empty | Empty accepts only the current document origin; supports `*` and `?` patterns |
| `allowedWindowActions` | all actions | Native whitelist for `flare.window` |
| `allowedHandlerNames` | empty | Empty allows all registered handlers |
| `grantedPermissions` | empty | Empty grants handler permissions unless hardened mode is enabled |
| `maxMessageBytes` | 1 MiB | Raw bridge message limit |
| `maxPayloadBytes` | 512 KiB | Decoded JSON payload limit |
| `maxMessagesPerSecond`, `maxBurst` | 120, 240 | Token bucket |
| `invocationTimeoutMs` | 15000 | JavaScript-side RPC timeout |
| `clockSkewSeconds` | 300 | Timestamp window; 0 disables the check |
| `maxPendingEvents` | 256 | Native-to-JavaScript queue before handshake |
| `maxJsonDepth`, `maxJsonNodes` | 32, 100000 | Payload complexity limits |
| `maxHandshakeAttemptsPerDocument` | 16 | Hello flood limit |

`HandlerOptions` contains `permission` and `allowedOrigins`. A non-empty permission is enforced when `grantedPermissions` is non-empty or hardened mode is enabled. Non-empty handler origins must match the session origin.

`WindowAction` values:

`SHOW`, `HIDE`, `MINIMIZE`, `MAXIMIZE`, `RESTORE`, `CLOSE`, `FOCUS`, `SET_TITLE`, `SET_SIZE`, `SET_POSITION`, `SET_OPACITY`, `SET_FULLSCREEN`, `SET_ALWAYS_ON_TOP`, `CENTER`, `GET_BOUNDS`, `IS_MAXIMIZED`, `IS_MINIMIZED`, `IS_VISIBLE`, `IS_FOCUSED`, `START_DRAG`.

Helpers: `WindowActionName`, `ParseWindowAction`, `AllWindowActions`.

### Iframe sessions

`BridgeOptions::allowIframes` depends on the engine:

- Windows WebView2 supports frame-targeted messages and script execution. Iframe sessions work when enabled.
- macOS WKWebView reports frame information, but Flare keeps a single top-frame token model. Requesting `allowIframes` returns `ErrorCode::NOT_SUPPORTED` at window creation.
- WebKitGTK does not expose a frame-targeted native reply path. Requesting `allowIframes` returns `ErrorCode::NOT_SUPPORTED`.

When iframe sessions are enabled, native code validates the frame source origin. With an empty `allowedOrigins` list, frames must be same-origin with the main document. Iframe window actions are always rejected. Frames may use custom events and RPC handlers, but `Window::Emit` is not broadcast into frames.

### Raw web messages

`Window::SendWebMessage(json)` and `OnWebMessageReceived` provide a raw, non-bridge channel for compatibility.

- Bridge messages are recognized by the `flare` marker.
- Anything else is delivered as a raw message.
- Raw messages have no bridge authentication and should not carry privileged commands.

On WebKit engines, native-to-page raw messages use `window.postMessage`; bridge messages use a separate entry point. On WebView2 both use the native web message channel.

### Guarantees

- JavaScript cannot create a window.
- JavaScript cannot enumerate, discover, focus, close, or message another window.
- JavaScript receives no native events.
- `flare.on` receives only custom events sent by native code.
- `Window::On` receives only custom events sent by `flare.emit`.
- C++ can broadcast to all windows or a group; JavaScript cannot.
- Every `flare.window` action is validated natively.
- Every `flare.invoke` call is authenticated and authorized natively.

## Security

### The boundary

The native process is the security boundary. The injected bridge script is readable, inspectable, and not protected from DevTools. Any security property that depended on JavaScript being unable to inspect the bridge would be invalid.

Trust decisions are made only in C++.

### Assets

- The user's machine, files, clipboard, camera, microphone, geolocation, and downloaded content.
- Native window control: showing, hiding, moving, resizing, maximizing, entering fullscreen, staying on top, and closing.
- Native application data exposed through RPC handlers.
- The per-window bridge session token.

### Adversaries

- Hostile web content loaded in a window.
- A cross-origin iframe embedded by a trusted page.
- An XSS vulnerability in a trusted page. XSS runs with the page's origin and therefore with the bridge privileges granted to that origin.
- A malicious local process trying to connect to the single-instance socket.
- A network attacker. TLS, certificate decisions, and web-engine protections remain relevant.

Flare does not protect a trusted origin from its own scripts. Any script running in the top-level page can call the bridge within the permissions native code granted to that window.

### What JavaScript can do

- Send custom events to C++ handlers of its own window.
- Invoke C++ RPC handlers exposed to that window.
- Perform a whitelisted subset of actions on its own window.
- Read harmless application metadata.

### What JavaScript cannot do

- Create a window.
- Enumerate, discover, address, focus, or close another window.
- Receive native events.
- Access native window lists or application-level control unless C++ explicitly exposes an RPC handler for that purpose.
- Read another window's bridge queue or group membership.

These restrictions are structural:

- The bridge contains no window-creation implementation.
- The protocol contains no window registry or window-targeting field.
- The protocol contains no native event type.
- `Window::Emit` targets one window. `App::Emit` and `WindowGroup::Emit` exist only in C++.
- Native events are raised in C++ and never routed through the bridge server.

### Session establishment

For each top-level document, Flare creates a fresh random session id and a 256-bit secret key. The injected script sends an unauthenticated `hello`. Native code checks the sender origin and frame, then delivers the session credentials to the top frame.

An iframe cannot read a cross-origin top frame's closure or message channel and therefore cannot acquire the top-frame session.

On WebKitGTK, native-to-page delivery targets the top frame. On WebView2 and WKWebView, main-session delivery is likewise top-frame. When iframe sessions are supported and enabled, frames receive their own credentials through frame-targeted delivery.

The token is not secret from the page itself. It is secret from other frames and origins. Page scripts can inspect their own global object; the model does not depend on hiding the token from the trusted top-level origin.

### Message authentication

Every JavaScript-to-native message after the handshake carries:

- `sid`: session id;
- `seq`: strictly increasing sequence;
- `ts`: Unix milliseconds;
- `nonce`: 16 random bytes in hex;
- `type`, `name`, `id`, and a JSON-string `payload`;
- `mac`: HMAC-SHA256 over a length-stable canonical string.

The canonical string:

```text
v1|<sid>|<seq>|<ts>|<nonce>|<type>|<name>|<id>|<payload>
```

Native validation order:

1. Raw message size limit.
2. Envelope structure and name/id validation.
3. Session lookup.
4. Frame kind match.
5. Source origin match against the session origin.
6. Timestamp within the allowed clock skew.
7. Sequence strictly greater than the last accepted sequence.
8. Nonce not seen before.
9. Constant-time MAC comparison.
10. Rate-limit token bucket.
11. Payload size, JSON validity, depth, and node count.
12. Handler, origin, permission, and window-action authorization.

State changes for sequence, nonce, and the token bucket happen only after a valid MAC. An attacker without the token cannot advance a session's sequence or consume its budget through forged frames.

Replay protection combines the strictly increasing sequence with nonce tracking. The timestamp window limits capture and replay opportunities. Rate limiting bounds CPU and handler work.

### Origin validation

`BridgeOptions::allowedOrigins` accepts exact origins and glob patterns such as `https://app.flare` or `https://*.example.com`.

When the list is empty, the bridge accepts only the current document origin. A hardened configuration should always set an explicit list.

Origin checks:

- Messages must match the session origin exactly.
- The main session must also match the current top-level document origin.
- Iframe sessions must match the frame source origin. With an empty allowlist, iframes must be same-origin with the main document.

Special origins:

- `file://` is one origin shared by all local files. The engine does not isolate local files from each other. Use an HTTPS virtual host or a controlled custom scheme for production, or keep the bridge on a single trusted local page.
- `about:blank` and `data:` produce an opaque `null` origin. A page loaded from an HTML string without a base URL runs on such an origin.
- Custom schemes produce `scheme://host` origins.
- `blob:` inherits the embedded origin.

Windows virtual-host mapping gives pages a normal `https://host/` origin. macOS and Linux use a custom scheme; absolute HTTPS subresource URLs are not rewritten there.

### Authorization layers

1. `BridgeOptions::enabled` disables the bridge entirely.
2. `allowedOrigins` restricts document and caller origins.
3. Session authentication and rate limiting reject unauthenticated or abusive traffic.
4. `allowedHandlerNames` is a native allowlist when non-empty. Empty allows every registered handler.
5. `HandlerOptions::allowedOrigins` narrows a handler to specific origins.
6. `HandlerOptions::permission` requires a permission in `grantedPermissions` when that list is non-empty or hardened mode is enabled.
7. `allowedWindowActions` is the native whitelist for `flare.window`.
8. Payload size and complexity limits bound memory and recursion.
9. Engine-level permission policy handles camera, microphone, geolocation, notifications, and clipboard decisions.

No JavaScript-side check is authoritative. The client-side action list is informational.

### Default behavior and least privilege

Default bridge options are convenient:

- every registered handler is callable;
- every window action is available;
- handler permissions are not enforced while `grantedPermissions` is empty.

For production, restrict these layers explicitly.

Recommendations:

- Grant only the window actions the UI needs. If the page does not need `close`, remove it.
- Set `allowedHandlerNames` even when only a few handlers exist.
- Split handlers by permission. Do not put every privileged operation behind one broad permission.
- Do not expose OS-level actions such as quitting the app or creating windows unless intentional. JavaScript cannot do these by default, but an RPC handler could add them.
- Use `file://` only for trusted local applications. Prefer HTTPS virtual hosts.
- Set explicit origins and enable `hardenedMode` for production pages served over HTTPS.
- Keep `allowIframes` off unless frame sessions are required and supported.
- Do not disable web security in production.
- Treat every payload as untrusted. Validate types, ranges, lengths, and state transitions in C++.

### Hardened mode

`BridgeOptions::hardenedMode = true`:

- requires a non-empty `allowedOrigins` list;
- forces DevTools off;
- forces the default context menu off;
- forces browser accelerator keys off;
- forces autofill and password saving off where supported;
- disables the status bar;
- rejects an empty origin allowlist instead of falling back to implicit same-origin;
- keeps handler permission checks active, so an empty grant list grants no permissions;
- still requires explicit iframe enablement and an allowed origin.

Hardened mode is a configuration guardrail, not a sandbox. The engine still runs web content, and XSS still inherits the permissions granted to that window. Keep handlers and window actions minimal.

### DevTools and inspection

- `devToolsEnabled` controls engine developer tools.
- Hardened mode disables inspection helpers.
- The bridge is not hidden in DevTools. This is intentional.
- Production builds should not expose DevTools or debug handlers. A handler that returns filesystem paths, environment variables, or tokens is a serious risk.

### Permissions

`BrowserOptions::permissionPolicy` provides `ASK`, `ALLOW`, or `DENY` for camera, microphone, geolocation, notifications, and clipboard access.

- `ASK` raises `OnPermissionRequested`. With no handler, the request is denied.
- `ALLOW` and `DENY` answer directly.

Engine support varies:

- macOS surfaces media capture permission to the host.
- Linux WebKitGTK supports media, geolocation, notifications, and clipboard requests depending on the runtime version and installed headers.
- Windows WebView2 supports camera, microphone, geolocation, notifications, clipboard read, autoplay, and other kinds. Flare maps the main kinds and sends the rest as `OTHER`.

Clipboard write may be handled by the engine or operating system without a host prompt. Capability queries report what the engine can enforce; unsupported policy entries are not silently treated as enforced.

### Downloads and file access

Downloads are controlled through `OnDownloadStarting`. Set `BrowserOptions::download.blocked` to reject all downloads, or cancel individual downloads in C++. Use `askWhereToSave` for a native save panel.

`allowFileAccessFromFileUrls` grants local pages access to local files. Disable it when pages do not need local resources. The file origin is broad; do not run untrusted local files with the bridge enabled.

### Certificates and authentication

`OnCertificateError` is a security decision. With no handler, invalid certificates are not allowed. A handler that sets `allow` must make an informed trust decision.

`OnAuthenticationRequested` supports default browser handling, cancellation, or explicit credentials. Credentials are passed to the engine and are not stored by Flare.

### Single instance

The single-instance guard uses a per-user lock plus a named pipe or Unix socket.

- On POSIX, the socket and lock file are created with mode 0600.
- Arguments are forwarded as NUL-separated UTF-8 strings. The IPC layer does not encrypt or authenticate them.
- It also does not parse or validate forwarded arguments.
- A local process running as the same user can connect to the socket or send the window message.
- Do not expose privileged operations solely through forwarded launch arguments.

### Network and local resources

- The bridge is not a network transport. It uses the engine's native message channel.
- `BridgeOptions::clockSkewSeconds` exists because JavaScript timestamps come from the page clock. A large skew widens the replay window.
- Rate limits and size limits reduce CPU and memory abuse from hostile pages.
- Engine TLS, HSTS, cookie, and origin protections remain active unless explicitly weakened.
- Disabling web security is a development-only setting.

### Threat summary

| Threat | Mitigation |
| --- | --- |
| Cross-origin iframe forges bridge messages | Top-frame token delivery, source-origin validation, frame checks; iframe support disabled on macOS and Linux |
| Page replays a captured message | Strictly increasing sequence, nonce tracking, timestamp window, MAC |
| Page floods native handlers | Token bucket, raw size, payload size, JSON depth and node limits, handshake attempt limit |
| Page discovers or addresses another window | No window APIs in the bridge, no window ids in the protocol, per-window queues |
| Page forges native input or navigation | Native events are C++-only and never cross the bridge |
| XSS in a trusted page | Least-privilege handler names, permissions, and window actions |
| Untrusted local file access | Explicit capability, `file://` caveat, HTTPS virtual host recommendation |
| Invalid TLS certificate accepted silently | Certificate decisions are explicit in C++, default deny |
| Malicious second instance | Per-user lock and socket; IPC arguments are not authenticated, so do not trust them for privileged actions |
| Debug handlers leak authority | Hardened mode disables inspection helpers; do not ship debug handlers |

The core of the model is that native code validates every bridge request. The bridge script is not a secret.

## Embedded Web Resources

Flare can embed a web directory directly into an executable. Embedding is optional and is controlled by the `FlareApp` CMake helper.

### Embed a directory

```cmake
add_executable(my_app ${MY_SOURCES})
FlareApp(my_app "web")
```

Every file below `web` is embedded recursively as a named byte array. The generated bundle is registered with the Flare runtime before `main` begins.

### Use the bundle

When no other initial content is configured and exactly one bundle is registered, Flare selects it automatically:

```cpp
Flare::BrowserOptions browserOptions;
browserOptions.embeddedStartPage = "index.html";
```

If `embeddedStartPage` is empty, Flare uses `index.html`. No generated header is needed.

If several bundles are registered, select one explicitly by target name:

```cpp
browserOptions.embeddedBundle = "my_app";
```

At window creation Flare serves the bundle directly from the executable. Nothing is written to the state folder, a temporary directory, or local application data.

Serving strategy:

- Windows uses the reserved origin `https://flare-embedded.invalid/` and answers requests through WebView2 request interception. No DNS lookup or network request is made.
- macOS and Linux use the reserved custom scheme `flare-embedded://embedded/`.

Relative scripts, stylesheets, images, and fonts work normally.

### No embedding

Omit the directory argument:

```cmake
FlareApp(my_app)
```

The target is linked to Flare but no resources are embedded. `embeddedBundle` stays empty and the application chooses one of the other initial content sources.

Embedding is also absent when the application links Flare without using the helper.

### Rebuild behavior

The build tracks the embedded directory. Adding, removing, or changing a file causes the generated bundle to be rebuilt on the next build.

### Validation and conflicts

`embeddedBundle` is mutually exclusive with:

- `startUrl`
- `html`
- `localFile`
- `virtualHost`

`embeddedStartPage` must be a safe relative path. Absolute paths and paths containing traversal outside the bundle are rejected with `ErrorCode::INVALID_ARGUMENT`.

Bundle names are validated at window creation. At embedding time, every source path is relative to the selected directory.

### In-memory serving

Embedded files are served from process memory:

- no files are extracted to the state folder or any other directory;
- each response receives a MIME type based on the file extension;
- missing files return a 404 response;
- URL paths are percent-decoded before lookup;
- requests for another bundle, traversal paths, and malformed URLs are rejected.

The bundle is registered before `main` and remains available for the process lifetime.

### Packaging

Because resources are part of the executable, a release package only needs the executable and the platform web-engine runtime. No separate web-asset folder is required.

### CMake helper availability

`FlareApp` works when Flare is consumed through:

- `find_package(Flare)`;
- `add_subdirectory(...)`.

The helper links the target automatically, so an additional link step is not required.

## Multiple Windows and Threading

### Windows are created only by C++

```cpp
Flare::WindowOptions childOptions;
childOptions.title = "Child";
childOptions.parent = mainWindow.get();
childOptions.modal = true;

Flare::BrowserOptions childBrowser;
childBrowser.startUrl = "https://app.local/child";

auto child = app->NewWindow(childOptions, childBrowser);
```

`NewWindow` returns `Result<std::shared_ptr<Window>>`. The application keeps a strong reference to every open window until it closes.

JavaScript cannot create, enumerate, address, focus, or close another window.

### Parent and modal windows

`WindowOptions::parent` is a non-owning pointer to another Flare window. `modal = true` disables the parent while the child is shown.

Behavior by platform:

- Windows and GTK disable the parent while the modal child is open.
- macOS makes the parent ignore mouse events while the modal child is open.

Modal behavior is therefore strongest on Windows and Linux.

### Window registry

```cpp
const std::vector<std::shared_ptr<Flare::Window>> windows = app->Windows();
const Flare::Result<std::shared_ptr<Flare::Window>> found = app->FindWindow(id);
```

The registry is native-only. Closed windows are removed automatically.

### Window groups

```cpp
auto group = app->CreateWindowGroup("tools").Value();
windowA->JoinGroup("tools");
windowB->JoinGroup("tools");

group->Emit("change", {{"value", 42}});
```

A `WindowGroup` holds weak references to its members. It never extends window lifetime. Closed windows are skipped.

A group emits only to live members. JavaScript cannot enumerate, join, leave, or address groups.

### Application-wide broadcast

```cpp
app->Emit("theme-changed", {{"dark", true}});
```

`App::Emit` sends a custom bridge event to every ready window. `WindowGroup::Emit` targets a named group.

These are the only native cross-window messaging paths. A window receiving a bridge event cannot tell which other windows exist.

### Threading model

The UI thread owns:

- `App::Create`, `App::Run`, and `App::Quit`;
- window creation and all window methods;
- native event delivery;
- bridge handlers;
- asynchronous engine callbacks.

Work from other threads:

```cpp
app->Dispatch([window]() {
});
```

```cpp
Flare::Result<void> result = app->DispatchSync([&]() {
});
```

`Dispatch` queues work on the UI thread. `DispatchSync` runs immediately when called on the UI thread or blocks another thread until the task completes.

`Window::Emit`, `App::Emit`, `WindowGroup::Emit`, and `Flare::Subscription` operations are safe from other threads. `Window::Emit` queues calls made outside the UI thread.

### Ownership rules

- `App` owns all open windows.
- `App` must remain alive while windows exist.
- `App::~App` closes remaining windows without raising close events.
- Destroying `App` releases the platform loop.
- Do not use a `Window` after the owning `App` is destroyed.
- Do not use a `Window` after `OnClosed`; operations return `ErrorCode::WINDOW_CLOSED`.

### Window state persistence

Each window can persist its size, position, and maximized state:

```cpp
windowOptions.persistState = true;
windowOptions.stateKey = "editor-main";
```

State is stored under `App::StateFolder()` and restored before the first show. Use a distinct key for each window.

### Single instance

```cpp
appOptions.singleInstance = true;
appOptions.singleInstanceId = "com.example.editor";
```

The default identifier is `applicationId`.

The second process forwards its command line to the first process, raises `App::OnSecondInstance`, and `App::Create` returns `ErrorCode::SINGLE_INSTANCE`.

Arguments can be supplied with `launchArguments`; otherwise the process command line is used.

## Platform Support

Flare exposes one native C++ API across three engines. A capability can be fully implemented, implemented with an engine restriction, or unavailable. Unsupported options return `ErrorCode::NOT_SUPPORTED` instead of being silently ignored.

### Capability matrix

`Yes` means implemented through the engine. `Partial` means implemented with a documented restriction. `No` means the engine does not expose the capability.

| Capability | Windows | macOS | Linux |
| --- | --- | --- | --- |
| Multiple windows, groups, thread dispatch | Yes | Yes | Yes |
| Hidden-until-ready lifecycle | Yes | Yes | Yes |
| Native key events | Yes | Yes | Yes |
| Native mouse and wheel events | No | Yes | Yes |
| File drop | Yes | Yes | Yes |
| Transparent window | Partial | Yes | Yes |
| Opacity | Yes | Yes | Yes |
| Rounded corners and shadow | Yes | Yes | No (system theme) |
| Per-window icon | Yes | No | Yes |
| Per-window taskbar or Dock visibility | Yes | No | Yes |
| Content protection | Yes | Yes | No |
| Custom or frameless title bar | Yes | Yes | Yes |
| Draggable regions | Yes | Yes | Partial (bridge-driven) |
| Virtual host over HTTPS | Yes | No (custom scheme) | No (custom scheme) |
| Custom URL schemes | Yes | Yes | Yes |
| Request interception | Yes | No | No |
| Response observation or interception | Yes | No | Partial (status, MIME type, headers) |
| HTTP header injection | Main frame | Main frame | No |
| Explicit proxy | Yes | No (system proxy) | Server and bypass; no PAC |
| Browser command-line arguments | Yes | No | No |
| Downloads with save dialog | Yes | Yes | Yes |
| Print to PDF | Yes | Yes (limited options) | No |
| Print dialog | Yes | Yes | Yes |
| Page capture | Yes | Yes | Yes |
| Find in page | Yes (script-assisted) | Partial (match found only) | Yes |
| Cookie management | Yes | Yes | Yes |
| Clear browsing data | Yes | Yes | Yes |
| Incognito | Yes | Yes | Yes |
| Per-window data store | Yes | Yes (macOS 14+ profiles) | Yes |
| Window fullscreen | Yes | Yes | Yes |
| HTML5 fullscreen events | Yes | No | Yes |
| Permission requests | Yes | Partial (media) | Yes |
| Script dialogs | Yes | Yes | Yes |
| Context menu control and event | Yes | Partial (disable only) | Yes |
| HTTP authentication | Yes | Yes | Yes |
| Certificate error decision | Yes | Yes | Yes |
| Web messages | Yes | Yes | Yes |
| Iframe bridge | Yes | No | No |
| DevTools | Yes | Yes | Yes |
| Close DevTools | No | Yes (private inspector property) | Yes |
| Accelerator key toggle | Yes | No | No |
| Autofill and password toggles | Yes | No | No |
| Swipe navigation | Yes | No | Yes |
| Pinch zoom | Yes | Yes | No |
| Status bar toggle | Yes | No | No |
| Zoom control | Yes | Yes | Yes |
| Disable web security | Yes | No | WebKitGTK 2.42+ |
| DPI and theme events | Yes | Yes | Yes |

Use `App::Capabilities()` and `Window::Capabilities()` to inspect the current runtime.

### Windows

Runtime:

- Windows 10 or newer.
- Microsoft Edge WebView2 Runtime.
- No loader deployment is required. MSVC links the static loader when available. MinGW and other toolchains embed and extract it to `%APPDATA%\<applicationId>\loader-cache\WebView2Loader.dll`. An executable-local loader still takes precedence.
- Single-threaded COM apartment on the UI thread, initialized by `App::Create`.

Limitations:

- WebView2 exposes key events but not mouse, wheel, or double-click events to the host. `nativeMouseEvents` and `nativeWheelEvents` are false. Missing events are never synthesized.
- WebView2 has no host API for closing DevTools.
- WebView2 has no host find controller. Find-in-page uses page scripting to count matches and scroll to the first match.
- Document-end scripts are wrapped in a DOM-content-loaded callback. A strict page CSP can block them. Document-start scripts are native.
- Transparent windows use layered-window color keying. Pages should avoid painting the exact background key color over regions that must be transparent.
- Request interception and response observation are available through WebView2.
- Accelerator key, autofill, password, and status-bar toggles are available.
- Public API names do not collide with Win32 macros, so `Windows.h` and `WindowsX.h` can be included before or after Flare.

### macOS

Runtime:

- macOS 12 or newer.
- System WebKit, Cocoa, QuartzCore, and CoreGraphics.
- ARC for the Objective-C++ engine integration.

Limitations:

- Per-window icons and per-window Dock visibility are unavailable. The application icon can be set through `AppOptions::icon`.
- WKWebView does not expose generic request or response interception or an explicit network proxy. The system proxy is used.
- Iframe bridge sessions are disabled.
- Context menus can be disabled with injected JavaScript but are not intercepted as a native event.
- HTML5 fullscreen entry is not reported to the host. Native window fullscreen is supported.
- Find-in-page reports whether a match exists but cannot enumerate counts or stop a search.
- PDF export supports the default layout. Non-default orientation, margins, scale, page size, header/footer, or CSS page-size requests return `ErrorCode::NOT_SUPPORTED`.
- Closing DevTools relies on a private inspector property.
- Cookies read from the system store report `CookieSameSite::UNSPECIFIED`. Setting SameSite uses the platform cookie properties.
- Per-window data stores require macOS 14 or newer.

### Linux

Runtime:

- GTK 3.
- WebKitGTK 4.1 or 4.0 version 2.40 or newer.
- A running Wayland or X11 display.
- `pkg-config` entries for GTK 3 and the selected WebKitGTK version.

Limitations:

- WebKitGTK does not expose HTTPS virtual-host mapping. Virtual hosts use a custom scheme. Only main URLs are rewritten from `https://host/` to that scheme.
- Arbitrary request interception and header injection are unavailable. Main-frame navigation decisions are available.
- Context menu selection text is not exposed by the hit-test result. The event carries link, source, coordinates, and editable/image/video flags.
- PAC proxy selection is unavailable. Configure an explicit proxy server.
- PDF export is not exposed through the host API.
- Rounded corners and borderless shadows follow the GTK theme and compositor.
- Iframe bridge sessions are unavailable.
- The web-security toggle is compiled only against WebKitGTK 2.42 or newer.
- Browser command-line arguments, autofill toggles, accelerator key toggles, status bar, and pinch zoom are unavailable.
- Draggable regions use the bridge's mousedown handler and native window drag. They require the bridge to be enabled.

### Capability fields

`PlatformCapabilities` includes:

`platformName`, `notes`, `multiWindow`, `nativeKeyEvents`, `nativeMouseEvents`, `nativeWheelEvents`, `fileDrop`, `transparentWindows`, `windowOpacity`, `roundedCorners`, `borderlessShadow`, `perWindowIcon`, `taskbarVisibility`, `contentProtection`, `customTitleBar`, `draggableRegions`, `virtualHostHttps`, `virtualHostScheme`, `customSchemes`, `requestInterception`, `responseInterception`, `headerInjection`, `proxy`, `browserArguments`, `downloads`, `printToPdf`, `print`, `capturePreview`, `findInPage`, `cookies`, `clearBrowsingData`, `incognito`, `perWindowDataStore`, `windowFullscreen`, `html5FullscreenEvents`, `permissionRequests`, `scriptDialogs`, `contextMenuControl`, `httpAuthentication`, `certificateErrors`, `webMessages`, `iframeBridge`, `devTools`, `devToolsControl`, `browserAcceleratorKeysToggle`, `autofillToggle`, `swipeNavigation`, `pinchZoom`, `statusBar`, `zoomControl`, `webSecurityToggle`, `dpiEvents`, `themeEvents`, `systemThemeChange`, `documentTitleEvents`, `faviconEvents`, `domContentLoadedEvents`, `singleInstance`, `windowStatePersistence`.

`platformName` is `windows`, `linux`, or `macos`. `notes` contains human-readable engine limitations.

## Examples

The project includes four complete example applications. They are built when `FLARE_BUILD_EXAMPLES` is enabled.

### Basic Window

Demonstrates:

- application creation and the UI loop;
- one native window with an inline HTML page;
- hidden-until-ready lifecycle;
- window state persistence;
- native navigation and close events;
- a global RPC handler.

Use it as the smallest complete reference for the application and window lifecycle.

### Multi-Window

Demonstrates:

- creating several windows from C++;
- shared window configuration;
- a `WindowGroup` with several members;
- broadcasting a custom bridge event from C++;
- receiving the same event in every member window.

It is the reference for native-controlled cross-window communication. JavaScript still cannot enumerate or address windows.

### Bridge Demo

Demonstrates:

- `flare.on`, `flare.once`, `flare.off`, and `flare.emit`;
- `flare.invoke` with JSON arguments and results;
- window handlers and application handlers;
- custom events from C++ to JavaScript;
- window actions through `flare.window`;
- a permission-tagged handler;
- native event registration;
- an embedded web bundle.

With default bridge options, all registered handlers and window actions are available. The demo also shows how a page can call native code without exposing window creation or application-wide control.

### Frameless Custom Title Bar

Demonstrates:

- a frameless native window;
- a custom title-bar appearance;
- draggable client regions;
- native window controls through the whitelisted `flare.window` API;
- fullscreen toggling;
- window bounds and state queries;
- an embedded web bundle.

The top strip is a draggable region declared with `-webkit-app-region: drag` and also exposed through `WindowOptions::draggableRegions`. The controls opt out with `-webkit-app-region: no-drag`; interactive elements are excluded from dragging. On Windows the native regions participate in hit testing.

### Build and run

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLARE_BUILD_EXAMPLES=ON
cmake --build build
```

Executables are placed in the build's `bin` directory. The exact executable names are emitted by CMake and included in the release archives described in [Releases](#releases).

### Tests

The unit test suite is built when `FLARE_BUILD_TESTS` is enabled and runs with:

```sh
ctest --test-dir build --output-on-failure
```

The tests cover:

- `Result` and error behavior;
- the event/subscription system;
- cryptography and hashing;
- URL origin and pattern handling;
- JSON validation;
- window, browser, and bridge option validation;
- bridge protocol parsing;
- bridge session authentication, replay protection, rate limits, handler whitelists, frame checks, origin checks, and window action checks;
- embedded web bundle serving and path validation.

The tests do not create GUI windows. They are headless and run on CI for Windows, Linux, and macOS.

### Release archives

Release archives contain the example executables and the test executable for each platform. See [Releases](#releases).

## Releases

Flare publishes prebuilt example and test archives through GitHub Releases.

### Version source

The release version is read from the CMake project version.

```cmake
project(Flare VERSION 1.0.0 LANGUAGES CXX)
```

The release tag is `v<version>`, for example `v1.0.0`.

A tag build fails when the pushed tag does not match the version declared by CMake.

### Triggers

The release workflow runs when:

- a tag matching `v*` is pushed;
- the workflow is started manually from the Actions page.

A manual run creates or updates the release for the version declared by CMake.

### Build matrix

For every release, the workflow:

1. resolves the version from CMake;
2. builds and tests on Windows, Linux, and macOS;
3. signs macOS executables ad hoc so they can run on Apple Silicon;
4. packages examples and the test executable;
5. uploads one archive per platform;
6. creates or updates the GitHub Release;
7. attaches every archive to the release.

### Release archives

Archive names include the version, operating system, and architecture:

```text
flare-<version>-windows-<arch>.zip
flare-<version>-linux-<arch>.tar.gz
flare-<version>-macos-<arch>.tar.gz
```

Each archive contains:

- the four example applications;
- the unit test executable;
- the project readme;
- the license.

### Release notes

The release notes include a full changelog line:

```text
Full changelog: <previous_commit> - <current_commit>
```

They also include:

- a compare link between the previous release and the current commit;
- a list of non-merge commits;
- runtime requirements for each platform.

### Runtime requirements

The archives contain application binaries, not the platform web engine.

- Windows requires Microsoft Edge WebView2 Runtime. MSVC uses the static loader; other toolchains extract the embedded loader when needed.
- Linux requires GTK 3 and WebKitGTK 4.1 or 4.0.
- macOS requires macOS 12 or newer.

### Running the tests

Extract the archive and run the test executable from a terminal. The tests are headless and do not create GUI windows.

On Linux and macOS, preserve executable permissions when extracting from an archive:

```sh
tar -xzf flare-<version>-<os>-<arch>.tar.gz
```

On Windows, extract the zip and run the executables directly.

## Troubleshooting

### Common error codes

| Error | Typical cause |
| --- | --- |
| `INVALID_ARGUMENT` | Invalid option, name, path, size, URL pattern, or JSON value |
| `WRONG_THREAD` | A UI operation was called outside the UI thread |
| `BROWSER_NOT_READY` | A browser operation ran before readiness, or browser creation failed |
| `NOT_SUPPORTED` | The option or method is not available on the current engine |
| `WINDOW_CLOSED` | The window is already closed or the application is shutting down |
| `PERMISSION_DENIED` | A bridge whitelist, permission, origin, or window action rejected the call |
| `TIMEOUT` | A synchronous dispatch or engine operation did not finish in time |
| `IO_FAILURE` | State, embedded resources, cookies, or a downloaded file could not be written |
| `PLATFORM_FAILURE` | The underlying native engine or platform operation failed |
| `SINGLE_INSTANCE` | Another instance owns the single-instance lock |
| `HANDLER_FAILED` | A C++ bridge handler threw an exception |

### Window never appears

Check:

- `WindowOptions::visibleOnStart` is true, or `Show()` is called for manual mode.
- `WindowOptions::showTrigger` matches the expected readiness point.
- `Window::IsBrowserReady()` returns true.
- `OnBrowserCreationFailed` has not fired.
- The browser engine is installed and available.

A window is intentionally hidden until the browser is ready. An empty window is never shown.

### Browser creation fails on Windows

Common causes:

- Microsoft Edge WebView2 Runtime is not installed.
- On non-MSVC Windows builds, local application data is not writable or security software blocks the extracted embedded loader.
- The process is not using a single-threaded COM apartment.

`App::Create` initializes the apartment. If the failure persists, inspect the error detail from `OnBrowserCreationFailed`.

### Navigation or script methods return `BROWSER_NOT_READY`

Wait for `OnBrowserReady`, or check `IsBrowserReady()` before calling navigation, script, capture, cookie, print, or find operations. Window controls such as `Show`, `Hide`, `SetTitle`, and `SetSize` do not require browser readiness.

### Window operations return `WRONG_THREAD`

Call all window methods on the thread that created the application. From another thread, use:

```cpp
app->Dispatch([window]() {
});
```

or:

```cpp
app->DispatchSync([&]() {
});
```

`Window::Emit`, `App::Emit`, and `WindowGroup::Emit` are safe from other threads.

### Bridge error `not_found`

No handler with that name is registered on the window or application.

Check:

- the handler name matches exactly;
- the handler was registered with `Window::Handle` or `App::Handle`;
- the registration happened before the invocation;
- a window handler and an application handler do not shadow each other unexpectedly. Window handlers take precedence.

### Bridge error `permission_denied`

Possible causes:

- `allowedHandlerNames` is non-empty and does not contain the handler;
- the handler requires a permission and the grant list is enforced but does not contain it;
- the handler restricts origins and the session origin does not match;
- `allowedWindowActions` does not contain the action;
- the request came from an iframe for a window action;
- hardened mode is enabled and the origin or permission is not explicitly allowed.

Default bridge options allow all registered handlers, but explicit lists tighten access.

### Bridge error `rejected`

The session, MAC, timestamp, sequence, nonce, or frame check failed.

Possible causes:

- the document was replaced and an old session is still being used;
- clock skew is too small for the page clock;
- a message was replayed;
- the bridge was used from an unsupported frame;
- a native message was mutated in transit.

Reload the page to establish a fresh session. Check `clockSkewSeconds` if system and page clocks differ significantly.

### Bridge error `rate_limited`

The per-session token bucket is exhausted. Reduce event or invocation frequency, or raise `maxMessagesPerSecond` and `maxBurst` if the workload is legitimate.

### Bridge error `handler_failed`

A C++ handler threw an exception. The error is reported through `App::OnUnhandledError`. Validate handler inputs and avoid throwing across the handler boundary.

### JavaScript `flare` is missing

Check:

- the bridge is enabled;
- the page is loaded in a supported engine;
- the page is the top frame, or iframe sessions are enabled and supported;
- a strict content security policy has not blocked the injected script;
- the document has completed enough of the handshake for the bridge to initialize.

### Embedded resources are missing

Check:

- the target uses the `FlareApp` helper with a web directory;
- `BrowserOptions::embeddedBundle` is set to the generated bundle name;
- `embeddedStartPage` names a file that exists in the bundle;
- `embeddedBundle` is not combined with `startUrl`, `html`, `localFile`, or `virtualHost`.

### Window is visible but page content is blank

Check:

- initial content is set;
- the browser is ready;
- navigation succeeded;
- any virtual host or local file path exists;
- a blocked URL pattern is not rejecting the page;
- CSP or engine security settings are not blocking inline scripts or resources.

### PDF export fails

- Windows supports the full PDF option set.
- macOS supports the default layout; unsupported layout settings return `ErrorCode::NOT_SUPPORTED`.
- WebKitGTK does not expose PDF export through the host API.

### Find-in-page behaves differently

- Windows uses page scripting to count matches.
- macOS reports whether a match exists, but cannot enumerate counts or stop a search.
- Linux uses native WebKitGTK find support.

Check `App::Capabilities()` before relying on match counts or stop behavior.

### Tests fail

Run:

```sh
ctest --test-dir build --output-on-failure
```

The test suite is headless. If a test fails, inspect the reported check. Tests do not require a display server.

## License

Flare is provided under the MIT License.

Copyright (c) 2026 SilentDispose

WebView2, WebKitGTK, and WKWebView remain subject to their own licenses.
