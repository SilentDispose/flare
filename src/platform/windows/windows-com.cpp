#include "platform/windows/windows-com.h"

#include <cstdio>
#include <string>

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string FormatHResult(HRESULT result) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(result));
    return buffer;
}

std::string PointerToHex(const void* pointer) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%p", pointer);
    return buffer;
}
