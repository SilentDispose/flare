#pragma once

#include <string>
#include <vector>

namespace Flare {
    struct PlatformCapabilities {
        std::string platformName;
        bool multiWindow = true;
        bool nativeKeyEvents = false;
        bool nativeMouseEvents = false;
        bool nativeWheelEvents = false;
        bool fileDrop = false;
        bool transparentWindows = false;
        bool windowOpacity = false;
        bool roundedCorners = false;
        bool borderlessShadow = false;
        bool perWindowIcon = false;
        bool taskbarVisibility = false;
        bool contentProtection = false;
        bool customTitleBar = false;
        bool draggableRegions = false;
        bool virtualHostHttps = false;
        bool virtualHostScheme = false;
        bool customSchemes = false;
        bool requestInterception = false;
        bool responseInterception = false;
        bool headerInjection = false;
        bool proxy = false;
        bool browserArguments = false;
        bool downloads = false;
        bool printToPdf = false;
        bool print = false;
        bool capturePreview = false;
        bool findInPage = false;
        bool cookies = false;
        bool clearBrowsingData = false;
        bool incognito = false;
        bool perWindowDataStore = false;
        bool windowFullscreen = false;
        bool html5FullscreenEvents = false;
        bool permissionRequests = false;
        bool scriptDialogs = false;
        bool contextMenuControl = false;
        bool httpAuthentication = false;
        bool certificateErrors = false;
        bool webMessages = false;
        bool iframeBridge = false;
        bool devTools = false;
        bool devToolsControl = false;
        bool browserAcceleratorKeysToggle = false;
        bool autofillToggle = false;
        bool swipeNavigation = false;
        bool pinchZoom = false;
        bool statusBar = false;
        bool zoomControl = false;
        bool webSecurityToggle = false;
        bool dpiEvents = false;
        bool themeEvents = false;
        bool systemThemeChange = false;
        bool documentTitleEvents = true;
        bool faviconEvents = false;
        bool domContentLoadedEvents = false;
        bool singleInstance = true;
        bool windowStatePersistence = true;
        std::vector<std::string> notes;
    };
}
