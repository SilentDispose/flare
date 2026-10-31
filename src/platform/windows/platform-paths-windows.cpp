#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/path-utils.h"
#include "core/platform-paths.h"
#include "core/text-utils.h"

namespace Flare {
    namespace {
        std::string WideToUtf8(const std::wstring& text) {
            if (text.empty()) {
                return {};
            }
            const int size = WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) {
                return {};
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring KnownFolder(REFKNOWNFOLDERID folderId) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &raw)) && raw) {
                std::wstring value(raw);
                CoTaskMemFree(raw);
                return value;
            }
            return {};
        }
    }

    std::filesystem::path DefaultStateFolder(std::string_view applicationId) {
        std::wstring base = KnownFolder(FOLDERID_RoamingAppData);
        if (base.empty()) {
            wchar_t buffer[MAX_PATH];
            const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
            if (length > 0 && length < MAX_PATH) {
                base.assign(buffer, length);
            }
        }
        if (base.empty()) {
            base = L".";
        }
        const std::string identifier = SanitizeFileName(applicationId.empty() ? "flare-application" : applicationId);
        return std::filesystem::path(base) / PathFromUtf8(identifier);
    }

    std::filesystem::path DefaultDownloadsFolder() {
        const std::wstring known = KnownFolder(FOLDERID_Downloads);
        if (!known.empty()) {
            return std::filesystem::path(known);
        }
        const std::wstring state = KnownFolder(FOLDERID_Profile);
        return std::filesystem::path(state) / L"Downloads";
    }

    std::string CurrentPlatformName() {
        return "windows";
    }

    std::vector<std::string> CurrentProcessArguments() {
        std::vector<std::string> arguments;
        int count = 0;
        LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!raw) {
            return arguments;
        }
        for (int index = 0; index < count; ++index) {
            arguments.push_back(WideToUtf8(raw[index]));
        }
        LocalFree(raw);
        return arguments;
    }
}
