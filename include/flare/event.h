#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "flare/export.h"

namespace Flare {
    class FLARE_API SubscriptionState {
    public:
        virtual ~SubscriptionState();

        [[nodiscard]] virtual bool IsActive(std::uint64_t id) const = 0;
        virtual void Unsubscribe(std::uint64_t id) = 0;
    };

    class FLARE_API Subscription {
    public:
        Subscription();

        [[nodiscard]] bool IsActive() const;
        void Unsubscribe();
        explicit operator bool() const;

    private:
        template <typename... Args>
        friend class Event;

        Subscription(std::weak_ptr<SubscriptionState> subscriptionState, std::uint64_t subscriptionId);

        std::weak_ptr<SubscriptionState> state;
        std::uint64_t id = 0;
    };

    template <typename... Args>
    class Event {
    private:
        using Handler = std::function<void(Args...)>;

        class State final : public SubscriptionState {
        public:
            [[nodiscard]] bool IsActive(std::uint64_t id) const override {
                std::lock_guard<std::mutex> guard(mutex);
                return handlers.find(id) != handlers.end();
            }

            void Unsubscribe(std::uint64_t id) override {
                std::lock_guard<std::mutex> guard(mutex);
                handlers.erase(id);
            }

            mutable std::mutex mutex;
            std::unordered_map<std::uint64_t, Handler> handlers;
            std::uint64_t nextId = 1;
        };

    public:
        Event() = default;
        Event(const Event&) = delete;
        Event(Event&&) = delete;
        Event& operator=(const Event&) = delete;
        Event& operator=(Event&&) = delete;
        ~Event() = default;

        Subscription Subscribe(Handler handler) {
            if (!handler) {
                return Subscription();
            }
            std::lock_guard<std::mutex> guard(state->mutex);
            const std::uint64_t id = state->nextId++;
            state->handlers.emplace(id, std::move(handler));
            return Subscription(state, id);
        }

        void Emit(Args... args) const {
            std::vector<Handler> snapshot;
            {
                std::lock_guard<std::mutex> guard(state->mutex);
                snapshot.reserve(state->handlers.size());
                for (const auto& entry : state->handlers) {
                    snapshot.push_back(entry.second);
                }
            }
            for (const Handler& handler : snapshot) {
                handler(args...);
            }
        }

        void Clear() {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->handlers.clear();
        }

        [[nodiscard]] bool HasSubscribers() const {
            std::lock_guard<std::mutex> guard(state->mutex);
            return !state->handlers.empty();
        }

        [[nodiscard]] std::size_t Size() const {
            std::lock_guard<std::mutex> guard(state->mutex);
            return state->handlers.size();
        }

        [[nodiscard]] bool Empty() const { return Size() == 0; }

    private:
        std::shared_ptr<State> state = std::make_shared<State>();
    };
}
