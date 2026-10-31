#pragma once

#include <filesystem>

#include "flare/error.h"
#include "flare/icon.h"

namespace Flare {
    [[nodiscard]] Result<Icon> ResolveIcon(const Icon& icon, const std::filesystem::path& assetsFolder);
}
