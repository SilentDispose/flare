#pragma once

#include <filesystem>
#include <string>
#include <utility>

#include "flare/export.h"

namespace Flare {
    enum class IconSource { PATH, ASSET, RESOURCE };

    struct FLARE_API Icon {
        IconSource source = IconSource::PATH;
        std::filesystem::path path;
        std::string resource;

        Icon() = default;

        [[nodiscard]] static Icon FromPath(std::filesystem::path value);
        [[nodiscard]] static Icon FromAsset(std::filesystem::path value);
        [[nodiscard]] static Icon FromResource(std::string value);
        [[nodiscard]] bool IsEmpty() const noexcept;
    };
}
