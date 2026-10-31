#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Flare {
    std::filesystem::path DefaultStateFolder(std::string_view applicationId);
    std::filesystem::path DefaultDownloadsFolder();
    std::string CurrentPlatformName();
    std::vector<std::string> CurrentProcessArguments();
}
