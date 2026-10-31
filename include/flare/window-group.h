#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "flare/json.h"

#include "flare/error.h"
#include "flare/export.h"

namespace Flare {
    class App;
    struct AppRuntime;
    class Window;

    class FLARE_API WindowGroup {
    public:
        ~WindowGroup();
        WindowGroup(const WindowGroup&) = delete;
        WindowGroup(WindowGroup&&) = delete;
        WindowGroup& operator=(const WindowGroup&) = delete;
        WindowGroup& operator=(WindowGroup&&) = delete;

        [[nodiscard]] std::string Name() const;
        [[nodiscard]] std::size_t Size() const;
        [[nodiscard]] std::vector<std::shared_ptr<Window>> Windows() const;
        Result<void> Emit(std::string_view eventName, const JSON& payload = JSON::object()) const;

    private:
        explicit WindowGroup(std::string name);

        Result<void> Add(const std::shared_ptr<Window>& window);
        void Remove(Window* window);

        friend class App;
        friend struct AppRuntime;
        friend class Window;

        std::string groupName;
        mutable std::mutex mutex;
        std::vector<std::weak_ptr<Window>> members;
    };
}
