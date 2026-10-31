#include "platform/windows/webview2-loader.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#ifndef FLARE_WEBVIEW2_STATIC_LOADER
#include "webview2-loader-data.h"
#endif

#include "platform/windows/windows-utils.h"

#ifndef FLARE_WEBVIEW2_STATIC_LOADER
namespace {
    std::mutex cacheMutex;
    std::filesystem::path cacheDirectory;

    std::wstring ExecutableLocalLoaderPath() {
        const std::filesystem::path directory = Flare::ExecutableDirectory();
        return (directory / L"WebView2Loader.dll").wstring();
    }

    std::filesystem::path LoaderCachePath() {
        std::filesystem::path base;
        {
            std::lock_guard<std::mutex> guard(cacheMutex);
            base = cacheDirectory;
        }
        if (base.empty()) {
            std::vector<wchar_t> localAppData(32768, L'\0');
            const DWORD length = GetEnvironmentVariableW(
                L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
            if (length > 0 && length < localAppData.size()) {
                base = std::filesystem::path(std::wstring(localAppData.data(), length)) / L"WebView2Loader";
            } else {
                wchar_t temporary[MAX_PATH + 1]{};
                const DWORD temporaryLength = GetTempPathW(MAX_PATH + 1, temporary);
                if (temporaryLength == 0 || temporaryLength > MAX_PATH) {
                    return {};
                }
                base = std::filesystem::path(std::wstring(temporary, temporaryLength)) / L"WebView2Loader";
            }
        }
        return base / L"WebView2Loader.dll";
    }

    bool MatchesEmbeddedLoader(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            return false;
        }
        if (std::filesystem::file_size(path, error) != Flare::EmbeddedLoader::size || error) {
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return false;
        }
        constexpr std::size_t chunkSize = 65536;
        std::array<char, chunkSize> buffer{};
        std::size_t offset = 0;
        while (offset < Flare::EmbeddedLoader::size) {
            const std::size_t chunk = std::min(chunkSize, Flare::EmbeddedLoader::size - offset);
            input.read(buffer.data(), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                return false;
            }
            if (std::memcmp(buffer.data(), Flare::EmbeddedLoader::data + offset, chunk) != 0) {
                return false;
            }
            offset += chunk;
        }
        return true;
    }

    std::filesystem::path ExtractEmbeddedLoader(const std::filesystem::path& path) {
        if (path.empty()) {
            return {};
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return {};
        }
        if (MatchesEmbeddedLoader(path)) {
            return path;
        }
        std::filesystem::path temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return {};
        }
        output.write(reinterpret_cast<const char*>(Flare::EmbeddedLoader::data),
            static_cast<std::streamsize>(Flare::EmbeddedLoader::size));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return {};
        }
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (!error) {
            return path;
        }
        if (MatchesEmbeddedLoader(path)) {
            std::filesystem::remove(temporary, error);
            return path;
        }
        if (MatchesEmbeddedLoader(temporary)) {
            return temporary;
        }
        std::filesystem::remove(temporary, error);
        return {};
    }
}
#endif

WebView2Loader& WebView2Loader::Instance() {
    static WebView2Loader loader;
    return loader;
}

void WebView2Loader::SetCacheDirectory(const std::filesystem::path& directory) {
#ifdef FLARE_WEBVIEW2_STATIC_LOADER
    (void)directory;
#else
    std::lock_guard<std::mutex> guard(cacheMutex);
    cacheDirectory = directory;
#endif
}

WebView2Loader::WebView2Loader() {
#ifdef FLARE_WEBVIEW2_STATIC_LOADER
    createEnvironment = &CreateCoreWebView2EnvironmentWithOptions;
#else
    const std::wstring localPath = ExecutableLocalLoaderPath();
    module = LoadLibraryW(localPath.c_str());
    if (!module) {
        const std::filesystem::path extractedPath = ExtractEmbeddedLoader(LoaderCachePath());
        if (!extractedPath.empty()) {
            module = LoadLibraryW(extractedPath.wstring().c_str());
        }
    }
    if (!module) {
        module = LoadLibraryW(L"WebView2Loader.dll");
    }
    if (!module) {
        errorMessage =
            "WebView2Loader could not be loaded from the executable directory, the embedded copy, or the library path";
        return;
    }
    createEnvironment = reinterpret_cast<CreateEnvironmentFunction>(
        GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!createEnvironment) {
        errorMessage = "WebView2Loader.dll does not export CreateCoreWebView2EnvironmentWithOptions";
        FreeLibrary(module);
        module = nullptr;
    }
#endif
}

bool WebView2Loader::IsAvailable() const noexcept {
    return createEnvironment != nullptr;
}

const std::string& WebView2Loader::ErrorMessage() const noexcept {
    return errorMessage;
}

HRESULT WebView2Loader::CreateEnvironment(PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler) const {
    if (!createEnvironment) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return createEnvironment(browserExecutableFolder, userDataFolder, options, handler);
}
