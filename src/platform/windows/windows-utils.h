#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

#include "flare/geometry.h"

namespace Flare {
    COLORREF ToColorRef(Color color);
    Color FromColorRef(COLORREF color);
    std::filesystem::path ExecutableDirectory();
    bool SystemUsesDarkMode();
    void ApplyDarkTitleBar(HWND window, bool dark);
    void ApplyRoundedCorners(HWND window, bool rounded);
    void ApplyBorderlessShadow(HWND window, bool shadow);
    UINT WindowDpi(HWND window);
    int ScaleForDpi(int value, UINT dpi);
    RECT RectToNative(const Rect& rect);
    Rect NativeToRect(const RECT& rect);
    std::string WideToUtf8Path(const std::wstring& text);
    std::wstring Utf8ToWidePath(const std::string& text);
}
