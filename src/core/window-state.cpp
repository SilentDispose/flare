#include "core/window-state.h"

#include <fstream>
#include <string>

#include "flare/json.h"

#include "core/json-utils.h"
#include "core/text-utils.h"

namespace Flare {
    namespace {
        std::filesystem::path StateFilePath(const std::filesystem::path& folder, const std::string& key) {
            return folder / (SanitizeFileName(key) + ".json");
        }
    }

    Result<SavedWindowState> LoadWindowState(const std::filesystem::path& folder, const std::string& key) {
        SavedWindowState state;
        if (key.empty()) {
            return state;
        }
        const std::filesystem::path path = StateFilePath(folder, key);
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return state;
        }
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to inspect window state file", error.message());
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return Error(ErrorCode::IO_FAILURE, "Unable to open window state file");
        }
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.size() > 65536) {
            return state;
        }
        const Result<JSON> parsed = ParseJsonValue(text, 65536, 8, 512);
        if (!parsed.IsOk() || !parsed.Value().is_object()) {
            return state;
        }
        const JSON& value = parsed.Value();
        const auto number = [&value](const char* name, int fallback) {
            const auto iterator = value.find(name);
            if (iterator == value.end() || !iterator->is_number_integer()) {
                return fallback;
            }
            return iterator->get<int>();
        };
        state.x = number("x", 0);
        state.y = number("y", 0);
        state.width = number("width", 0);
        state.height = number("height", 0);
        const auto maximized = value.find("maximized");
        state.maximized = maximized != value.end() && maximized->is_boolean() && maximized->get<bool>();
        state.valid = state.width >= 64 && state.height >= 64;
        return state;
    }

    Result<void> SaveWindowState(
        const std::filesystem::path& folder, const std::string& key, const SavedWindowState& state) {
        if (key.empty() || !state.valid) {
            return Result<void>::Ok();
        }
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to create the state directory", error.message());
        }
        JSON value;
        value["x"] = state.x;
        value["y"] = state.y;
        value["width"] = state.width;
        value["height"] = state.height;
        value["maximized"] = state.maximized;

        const std::filesystem::path finalPath = StateFilePath(folder, key);
        const std::filesystem::path temporaryPath = finalPath.string() + ".tmp";
        {
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                return Error(ErrorCode::IO_FAILURE, "Unable to write window state file");
            }
            output << value.dump(2);
            if (!output.good()) {
                return Error(ErrorCode::IO_FAILURE, "Unable to write window state file");
            }
        }
        std::filesystem::rename(temporaryPath, finalPath, error);
        if (error) {
            std::filesystem::remove(finalPath, error);
            error.clear();
            std::filesystem::rename(temporaryPath, finalPath, error);
        }
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to replace window state file", error.message());
        }
        return Result<void>::Ok();
    }
}
