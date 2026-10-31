#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "flare/browser-options.h"
#include "flare/error.h"

namespace Flare {
    struct EmbeddedWebFile {
        std::string_view path;
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
    };

    struct EmbeddedWebBundle {
        std::string_view name;
        const EmbeddedWebFile* files = nullptr;
        std::size_t fileCount = 0;
    };

    FLARE_API void RegisterEmbeddedWebBundle(const EmbeddedWebBundle& bundle);
    [[nodiscard]] FLARE_API std::vector<std::string> EmbeddedWebBundleNames();
    [[nodiscard]] FLARE_API std::string BuildEmbeddedWebUrl(
        std::string_view name, std::string_view path, bool useHttps);
    [[nodiscard]] FLARE_API bool IsEmbeddedWebUrl(std::string_view url) noexcept;
    [[nodiscard]] FLARE_API Result<CustomSchemeResponse> LoadEmbeddedWebResource(
        std::string_view name, std::string_view url);
}
