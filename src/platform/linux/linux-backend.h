#pragma once

#include "flare/window-options.h"

namespace Flare {
    struct LinuxAppSettings {
        Theme theme = Theme::SYSTEM;
        bool hardwareAcceleration = true;
    };

    const LinuxAppSettings& CurrentLinuxAppSettings();
    LinuxAppSettings& MutableLinuxAppSettings();
}
