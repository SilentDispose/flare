#include "platform/windows/windows-utils.h"

#include <dwmapi.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "platform/windows/windows-com.h"

namespace Flare {
    namespace {
        constexpr DWORD DarkModeAttribute = 20;
        constexpr DWORD CornerPreferenceAttribute = 33;
        constexpr DWORD CornerRound = 2;
        constexpr DWORD CornerDoNotRound = 1;
    }

    COLORREF ToColorRef(Color color) {
        return RGB(color.r, color.g, color.b);
    }

    Color FromColorRef(COLORREF color) {
        return Color{GetRValue(color), GetGValue(color), GetBValue(color), 255};
    }

    std::filesystem::path ExecutableDirectory() {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return std::filesystem::current_path();
            }
            if (length < buffer.size()) {
                buffer.resize(length);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        return std::filesystem::path(buffer).parent_path();
    }

    bool SystemUsesDarkMode() {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                KEY_READ, &key)
            != ERROR_SUCCESS) {
            return false;
        }
        DWORD value = 1;
        DWORD size = sizeof(value);
        const LSTATUS status = RegQueryValueExW(
            key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
        return status == ERROR_SUCCESS && value == 0;
    }

    void ApplyDarkTitleBar(HWND window, bool dark) {
        if (!window) {
            return;
        }
        const BOOL enabled = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(window, DarkModeAttribute, &enabled, sizeof(enabled));
    }

    void ApplyRoundedCorners(HWND window, bool rounded) {
        if (!window) {
            return;
        }
        const DWORD preference = rounded ? CornerRound : CornerDoNotRound;
        DwmSetWindowAttribute(window, CornerPreferenceAttribute, &preference, sizeof(preference));
    }

    void ApplyBorderlessShadow(HWND window, bool shadow) {
        if (!window) {
            return;
        }
        const MARGINS margins = shadow ? MARGINS{1, 1, 1, 1} : MARGINS{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(window, &margins);
    }

    UINT WindowDpi(HWND window) {
        using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
        static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (getDpiForWindow && window) {
            const UINT dpi = getDpiForWindow(window);
            if (dpi > 0) {
                return dpi;
            }
        }
        HDC device = GetDC(window);
        if (!device) {
            return 96;
        }
        const int dpi = GetDeviceCaps(device, LOGPIXELSX);
        ReleaseDC(window, device);
        return dpi > 0 ? static_cast<UINT>(dpi) : 96;
    }

    int ScaleForDpi(int value, UINT dpi) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    RECT RectToNative(const Rect& rect) {
        RECT native{};
        native.left = static_cast<LONG>(rect.x);
        native.top = static_cast<LONG>(rect.y);
        native.right = static_cast<LONG>(rect.Right());
        native.bottom = static_cast<LONG>(rect.Bottom());
        return native;
    }

    Rect NativeToRect(const RECT& rect) {
        return Rect{static_cast<double>(rect.left), static_cast<double>(rect.top),
            static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
    }

    std::string WideToUtf8Path(const std::wstring& text) {
        return WideToUtf8(text);
    }

    std::wstring Utf8ToWidePath(const std::string& text) {
        return Utf8ToWide(text);
    }
}
