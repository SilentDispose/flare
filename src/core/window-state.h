#pragma once

#include <filesystem>
#include <string>

#include "flare/error.h"

namespace Flare {
    struct SavedWindowState {
        bool valid = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool maximized = false;
    };

    Result<SavedWindowState> LoadWindowState(const std::filesystem::path& folder, const std::string& key);
    Result<void> SaveWindowState(
        const std::filesystem::path& folder, const std::string& key, const SavedWindowState& state);
}
