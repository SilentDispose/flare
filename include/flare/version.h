#pragma once

#include <string_view>

#include "flare/export.h"

namespace Flare {
    extern FLARE_API const int VersionMajor;
    extern FLARE_API const int VersionMinor;
    extern FLARE_API const int VersionPatch;
    extern FLARE_API const std::string_view LibraryVersion;
}
