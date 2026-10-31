#include "core/icon-utils.h"

namespace Flare {
    Icon Icon::FromPath(std::filesystem::path value) {
        Icon icon;
        icon.source = IconSource::PATH;
        icon.path = std::move(value);
        return icon;
    }

    Icon Icon::FromAsset(std::filesystem::path value) {
        Icon icon;
        icon.source = IconSource::ASSET;
        icon.path = std::move(value);
        return icon;
    }

    Icon Icon::FromResource(std::string value) {
        Icon icon;
        icon.source = IconSource::RESOURCE;
        icon.resource = std::move(value);
        return icon;
    }

    bool Icon::IsEmpty() const noexcept {
        return source == IconSource::RESOURCE ? resource.empty() : path.empty();
    }

    Result<Icon> ResolveIcon(const Icon& icon, const std::filesystem::path& assetsFolder) {
        if (icon.source != IconSource::ASSET || icon.IsEmpty()) {
            return icon;
        }
        if (icon.path.is_absolute()) {
            return Icon::FromPath(icon.path);
        }
        if (assetsFolder.empty()) {
            return Error(ErrorCode::INVALID_ARGUMENT, "An assets folder is required to resolve an asset icon");
        }
        std::error_code error;
        const std::filesystem::path resolved = std::filesystem::absolute(assetsFolder / icon.path, error);
        if (error) {
            return Error(ErrorCode::IO_FAILURE, "Unable to resolve the asset icon path", error.message());
        }
        return Icon::FromPath(resolved);
    }
}
