#include <crt_externs.h>
#include <cstdlib>

#include <filesystem>
#include <string>
#include <vector>

#include "core/platform-paths.h"
#include "core/text-utils.h"

namespace Flare {
    std::filesystem::path DefaultStateFolder(std::string_view applicationId) {
        const char* home = std::getenv("HOME");
        const std::filesystem::path base = home && *home
            ? std::filesystem::path(home) / "Library" / "Application Support"
            : std::filesystem::path(".");
        const std::string identifier = SanitizeFileName(applicationId.empty() ? "flare-application" : applicationId);
        return base / identifier;
    }

    std::filesystem::path DefaultDownloadsFolder() {
        const char* home = std::getenv("HOME");
        return home && *home ? std::filesystem::path(home) / "Downloads" : std::filesystem::path(".");
    }

    std::string CurrentPlatformName() {
        return "macos";
    }

    std::vector<std::string> CurrentProcessArguments() {
        std::vector<std::string> arguments;
        int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        if (!argv) {
            return arguments;
        }
        for (int index = 0; index < argc; ++index) {
            if (argv[index]) {
                arguments.emplace_back(argv[index]);
            }
        }
        return arguments;
    }
}
