#include "flare/window-group.h"

#include <algorithm>
#include <utility>

#include "flare/window.h"

namespace Flare {
    WindowGroup::WindowGroup(std::string name) : groupName(std::move(name)) {}

    WindowGroup::~WindowGroup() = default;

    std::string WindowGroup::Name() const {
        std::lock_guard<std::mutex> guard(mutex);
        return groupName;
    }

    std::size_t WindowGroup::Size() const {
        std::lock_guard<std::mutex> guard(mutex);
        std::size_t count = 0;
        for (const std::weak_ptr<Window>& entry : members) {
            if (!entry.expired()) {
                ++count;
            }
        }
        return count;
    }

    std::vector<std::shared_ptr<Window>> WindowGroup::Windows() const {
        std::lock_guard<std::mutex> guard(mutex);
        std::vector<std::shared_ptr<Window>> result;
        result.reserve(members.size());
        for (const std::weak_ptr<Window>& entry : members) {
            if (std::shared_ptr<Window> window = entry.lock()) {
                result.push_back(std::move(window));
            }
        }
        return result;
    }

    Result<void> WindowGroup::Emit(std::string_view eventName, const JSON& payload) const {
        Result<void> outcome = Result<void>::Ok();
        for (const std::shared_ptr<Window>& window : Windows()) {
            Result<void> result = window->Emit(eventName, payload);
            if (!result.IsOk() && outcome.IsOk()) {
                outcome = result;
            }
        }
        return outcome;
    }

    Result<void> WindowGroup::Add(const std::shared_ptr<Window>& window) {
        if (!window) {
            return Error(ErrorCode::INVALID_ARGUMENT, "Window group cannot add an empty window");
        }
        std::lock_guard<std::mutex> guard(mutex);
        members.erase(std::remove_if(members.begin(), members.end(),
                          [](const std::weak_ptr<Window>& entry) { return entry.expired(); }),
            members.end());
        for (const std::weak_ptr<Window>& entry : members) {
            if (entry.lock().get() == window.get()) {
                return Result<void>::Ok();
            }
        }
        members.push_back(window);
        return Result<void>::Ok();
    }

    void WindowGroup::Remove(Window* window) {
        std::lock_guard<std::mutex> guard(mutex);
        members.erase(std::remove_if(members.begin(), members.end(),
                          [window](const std::weak_ptr<Window>& entry) {
                              const std::shared_ptr<Window> locked = entry.lock();
                              return !locked || locked.get() == window;
                          }),
            members.end());
    }
}
