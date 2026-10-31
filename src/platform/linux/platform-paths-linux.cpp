#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/platform-paths.h"
#include "core/text-utils.h"

namespace Flare {
    std::filesystem::path DefaultStateFolder(std::string_view applicationId) {
        const char* configHome = std::getenv("XDG_CONFIG_HOME");
        std::filesystem::path base;
        if (configHome && *configHome) {
            base = configHome;
        } else {
            const char* home = std::getenv("HOME");
            base = home && *home ? std::filesystem::path(home) / ".config" : std::filesystem::path(".");
        }
        const std::string identifier = SanitizeFileName(applicationId.empty() ? "flare-application" : applicationId);
        return base / identifier;
    }

    std::filesystem::path DefaultDownloadsFolder() {
        const char* home = std::getenv("HOME");
        return home && *home ? std::filesystem::path(home) / "Downloads" : std::filesystem::path(".");
    }

    std::string CurrentPlatformName() {
        return "linux";
    }

    std::vector<std::string> CurrentProcessArguments() {
        std::vector<std::string> arguments;
        std::ifstream input("/proc/self/cmdline", std::ios::binary);
        if (!input) {
            return arguments;
        }
        std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::size_t start = 0;
        while (start < raw.size()) {
            const std::size_t end = raw.find('\0', start);
            const std::size_t length = end == std::string::npos ? raw.size() - start : end - start;
            if (length > 0) {
                arguments.emplace_back(raw.substr(start, length));
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return arguments;
    }
}
