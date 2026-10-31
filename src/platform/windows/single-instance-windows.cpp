#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/single-instance.h"
#include "core/text-utils.h"

namespace Flare {
    namespace {
        constexpr ULONG_PTR InstanceMessageTag = 0x464C4152u;

        std::wstring Utf8ToWide(const std::string& text) {
            if (text.empty()) {
                return {};
            }
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0) {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
            return result;
        }

        std::string InstanceKey(const std::string& identifier) {
            return SanitizeFileName(identifier);
        }

        std::string EncodeArguments(const std::vector<std::string>& arguments) {
            std::string payload;
            for (const std::string& argument : arguments) {
                payload.append(argument);
                payload.push_back('\0');
            }
            return payload;
        }

        std::vector<std::string> DecodeArguments(const char* data, std::size_t size) {
            std::vector<std::string> arguments;
            if (!data) {
                return arguments;
            }
            std::size_t start = 0;
            while (start < size) {
                const void* terminator = std::memchr(data + start, '\0', size - start);
                if (!terminator) {
                    break;
                }
                const auto* end = static_cast<const char*>(terminator);
                arguments.emplace_back(data + start, static_cast<std::size_t>(end - (data + start)));
                start = static_cast<std::size_t>(end - data) + 1;
            }
            return arguments;
        }
    }

    struct SingleInstanceGuard::Impl {
        std::wstring className;
        HANDLE mutexHandle = nullptr;
        HWND window = nullptr;
        HINSTANCE instance = nullptr;
        SingleInstanceGuard::Callback callback;

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
            Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                self = static_cast<Impl*>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (message == WM_COPYDATA && self) {
                const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
                if (copy && copy->dwData == InstanceMessageTag) {
                    std::vector<std::string> arguments = DecodeArguments(
                        static_cast<const char*>(copy->lpData), copy->cbData);
                    if (self->callback) {
                        self->callback(std::move(arguments));
                    }
                    return TRUE;
                }
                return FALSE;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    };

    Result<std::unique_ptr<SingleInstanceGuard>> SingleInstanceGuard::Create(
        std::string identifier, const std::vector<std::string>& arguments, Callback callback) {
        const std::string key = InstanceKey(identifier);
        const std::wstring className = L"FlareInstanceWindow_" + Utf8ToWide(key);
        const std::wstring mutexName = L"Local\\FlareInstanceMutex_" + Utf8ToWide(key);

        HANDLE mutexHandle = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutexHandle) {
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the single-instance mutex");
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutexHandle);
            const std::string payload = EncodeArguments(arguments);
            HWND target = nullptr;
            for (int attempt = 0; attempt < 20 && !target; ++attempt) {
                target = FindWindowW(className.c_str(), nullptr);
                if (!target) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            if (!target) {
                return Error(ErrorCode::SINGLE_INSTANCE, "Another instance is running and could not be reached");
            }
            COPYDATASTRUCT copy{};
            copy.dwData = InstanceMessageTag;
            copy.cbData = static_cast<DWORD>(payload.size());
            copy.lpData = const_cast<char*>(payload.data());
            DWORD_PTR result = 0;
            const LRESULT sent = SendMessageTimeoutW(
                target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy), SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result);
            if (sent == 0) {
                return Error(
                    ErrorCode::SINGLE_INSTANCE, "Another instance is running but did not accept the launch arguments");
            }
            return Error(ErrorCode::SINGLE_INSTANCE, "Another instance is already running; arguments were forwarded");
        }

        auto impl = std::make_unique<Impl>();
        impl->className = className;
        impl->mutexHandle = mutexHandle;
        impl->instance = GetModuleHandleW(nullptr);
        impl->callback = std::move(callback);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &Impl::WindowProc;
        windowClass.hInstance = impl->instance;
        windowClass.lpszClassName = className.c_str();
        const ATOM classAtom = RegisterClassExW(&windowClass);
        if (!classAtom) {
            CloseHandle(mutexHandle);
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to register the single-instance window class");
        }

        impl->window = CreateWindowExW(WS_EX_TOOLWINDOW, className.c_str(), L"FlareInstance", WS_POPUP, 0, 0, 0, 0,
            nullptr, nullptr, impl->instance, impl.get());
        if (!impl->window) {
            UnregisterClassW(className.c_str(), impl->instance);
            CloseHandle(mutexHandle);
            return Error(ErrorCode::PLATFORM_FAILURE, "Unable to create the single-instance window");
        }

        auto guard = std::unique_ptr<SingleInstanceGuard>(new SingleInstanceGuard(std::move(impl)));
        return guard;
    }

    SingleInstanceGuard::SingleInstanceGuard(std::unique_ptr<Impl> guardImpl) : impl(std::move(guardImpl)) {}

    SingleInstanceGuard::~SingleInstanceGuard() {
        if (!impl) {
            return;
        }
        if (impl->window) {
            DestroyWindow(impl->window);
            impl->window = nullptr;
        }
        if (!impl->className.empty()) {
            UnregisterClassW(impl->className.c_str(), impl->instance);
        }
        if (impl->mutexHandle) {
            CloseHandle(impl->mutexHandle);
            impl->mutexHandle = nullptr;
        }
    }
}
