#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Flare {
    std::string ExtractOrigin(std::string_view url);
    bool MatchesPattern(std::string_view text, std::string_view pattern);
    bool MatchesUrlPattern(std::string_view url, std::string_view pattern);
    bool OriginMatchesPattern(std::string_view origin, std::string_view pattern);
    bool IsValidOriginPattern(std::string_view pattern);
    bool IsHttpLikeUrl(std::string_view url);
    std::string FileUrlFromPath(const std::filesystem::path& path);
    std::filesystem::path PathFromFileUrl(std::string_view url);
    bool ResolveWithinDirectory(
        const std::filesystem::path& directory, std::string_view relativePath, std::filesystem::path& resolved);
}
