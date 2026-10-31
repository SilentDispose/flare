#pragma once

#include <string>
#include <string_view>

namespace Flare {
    std::string TrimAscii(std::string_view text);
    std::string ToLowerAscii(std::string_view text);
    bool EqualsIgnoreCase(std::string_view left, std::string_view right);
    bool IsValidName(std::string_view name);
    std::string SanitizeFileName(std::string_view name);
}
