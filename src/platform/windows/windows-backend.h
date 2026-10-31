#pragma once

#include "flare/window-options.h"

namespace Flare {
    struct WindowsAppSettings {
        Theme theme = Theme::SYSTEM;
        bool hardwareAcceleration = true;
        DpiAwareness dpiAwareness = DpiAwareness::PER_MONITOR_V2;
    };

    const WindowsAppSettings& CurrentWindowsAppSettings();
    WindowsAppSettings& MutableWindowsAppSettings();
}
