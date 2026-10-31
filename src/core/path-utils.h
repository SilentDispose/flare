#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Flare {
    std::filesystem::path PathFromUtf8(std::string_view text);
    std::string PathToUtf8(const std::filesystem::path& path);
}
