#include "flare/event.h"

namespace Flare {
    SubscriptionState::~SubscriptionState() = default;

    Subscription::Subscription() = default;

    Subscription::Subscription(std::weak_ptr<SubscriptionState> subscriptionState, std::uint64_t subscriptionId)
        : state(std::move(subscriptionState)),
          id(subscriptionId) {}

    bool Subscription::IsActive() const {
        const std::shared_ptr<SubscriptionState> locked = state.lock();
        return locked ? locked->IsActive(id) : false;
    }

    void Subscription::Unsubscribe() {
        const std::shared_ptr<SubscriptionState> locked = state.lock();
        if (locked) {
            locked->Unsubscribe(id);
        }
    }

    Subscription::operator bool() const {
        return IsActive();
    }
}
