#include "core/path-utils.h"

namespace Flare {
    std::filesystem::path PathFromUtf8(std::string_view text) {
        const auto* data = reinterpret_cast<const char8_t*>(text.data());
        return std::filesystem::path(std::u8string(data, text.size()));
    }

    std::string PathToUtf8(const std::filesystem::path& path) {
        const std::u8string text = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }
}
