// Synchronous, type-keyed event bus. All calls happen on the game thread.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <typeindex>
#include <vector>

namespace sacore {

using SubscriptionId = uint64_t;

class EventBus {
public:
    // Called when a handler throws; receives owner and error text.
    using ErrorReporter = std::function<void(const std::string& owner, const std::string& error)>;
    // Called after a handler that ran longer than the threshold; receives owner, event type name and duration.
    using SlowHandlerReporter =
        std::function<void(const std::string& owner, const char* eventType, double microseconds)>;

    explicit EventBus(ErrorReporter reporter = {}, int maxFailuresPerHandler = 3)
        : reporter_(std::move(reporter)), maxFailures_(maxFailuresPerHandler) {}

    template <class Event>
    SubscriptionId Subscribe(std::string owner, std::function<void(const Event&)> handler) {
        return Add(typeid(Event), std::move(owner), [h = std::move(handler)](const void* event) {
            h(*static_cast<const Event*>(event));
        });
    }

    // Handlers run in subscription order. Subscribing or unsubscribing inside a handler is allowed:
    // removed handlers are skipped immediately, new handlers run from the next Publish on.
    template <class Event>
    void Publish(const Event& event) {
        Dispatch(typeid(Event), &event);
    }

    // Times every handler and reports the ones slower than thresholdUs (frame spike attribution).
    void SetSlowHandlerReporter(SlowHandlerReporter reporter, double thresholdUs) {
        slowReporter_ = std::move(reporter);
        slowThresholdUs_ = thresholdUs;
    }

    void Unsubscribe(SubscriptionId id);
    void UnsubscribeOwner(const std::string& owner);
    size_t HandlerCount() const;

private:
    struct Handler {
        SubscriptionId id;
        std::string owner;
        std::function<void(const void*)> call;
        int failures = 0;
        bool removed = false;
    };

    SubscriptionId Add(std::type_index type, std::string owner, std::function<void(const void*)> call);
    void Dispatch(std::type_index type, const void* event);
    void Compact();

    std::map<std::type_index, std::vector<Handler>> handlers_;
    ErrorReporter reporter_;
    SlowHandlerReporter slowReporter_;
    double slowThresholdUs_ = 0;
    int maxFailures_;
    SubscriptionId nextId_ = 1;
    int dispatchDepth_ = 0;
};

}  // namespace sacore
