#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "flare/error.h"
#include "flare/geometry.h"
#include "flare/icon.h"

namespace Flare {
    class Window;

    using WindowId = std::uint64_t;

    enum class ShowTrigger { BROWSER_CREATED, FIRST_NAVIGATION_COMPLETED, DOM_CONTENT_LOADED, MANUAL };

    enum class TitleBarStyle { NATIVE, CUSTOM, HIDDEN };

    enum class Theme { SYSTEM, LIGHT, DARK };

    enum class WindowState { NORMAL, MINIMIZED, MAXIMIZED, FULLSCREEN };

    enum class DpiAwareness { UNAWARE, SYSTEM, PER_MONITOR, PER_MONITOR_V2 };

    struct WindowOptions {
        std::string title = "Flare";
        Icon icon;
        int width = 1024;
        int height = 768;
        int minWidth = 0;
        int minHeight = 0;
        int maxWidth = 0;
        int maxHeight = 0;
        int x = 0;
        int y = 0;
        bool hasPosition = false;
        bool centered = true;
        bool resizable = true;
        bool movable = true;
        bool minimizable = true;
        bool maximizable = true;
        bool closable = true;
        bool fullscreen = false;
        bool alwaysOnTop = false;
        bool frameless = false;
        bool transparent = false;
        Color backgroundColor{30, 30, 30, 255};
        bool showInTaskbar = true;
        bool visibleOnStart = true;
        ShowTrigger showTrigger = ShowTrigger::FIRST_NAVIGATION_COMPLETED;
        bool focusOnStart = true;
        double opacity = 1.0;
        bool borderlessShadow = true;
        bool roundedCorners = true;
        TitleBarStyle titleBarStyle = TitleBarStyle::NATIVE;
        std::vector<Rect> draggableRegions;
        bool maximizeOnStart = false;
        Theme theme = Theme::SYSTEM;
        Window* parent = nullptr;
        bool modal = false;
        bool contentProtection = false;
        bool persistState = false;
        std::string stateKey;
        bool useDocumentTitle = true;
    };

    [[nodiscard]] FLARE_API Result<void> ValidateWindowOptions(const WindowOptions& options);
    [[nodiscard]] FLARE_API std::string WindowStateName(WindowState state);
    [[nodiscard]] FLARE_API std::string ThemeName(Theme theme);
}
