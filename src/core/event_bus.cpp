#include "event_bus.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>

namespace sacore {

SubscriptionId EventBus::Add(std::type_index type, std::string owner, std::function<void(const void*)> call) {
    const SubscriptionId id = nextId_++;
    handlers_[type].push_back(Handler{id, std::move(owner), std::move(call)});
    return id;
}

void EventBus::Unsubscribe(SubscriptionId id) {
    for (auto& [type, list] : handlers_) {
        for (Handler& handler : list) {
            if (handler.id == id) {
                handler.removed = true;
            }
        }
    }
    Compact();
}

void EventBus::UnsubscribeOwner(const std::string& owner) {
    for (auto& [type, list] : handlers_) {
        for (Handler& handler : list) {
            if (handler.owner == owner) {
                handler.removed = true;
            }
        }
    }
    Compact();
}

size_t EventBus::HandlerCount() const {
    size_t count = 0;
    for (const auto& [type, list] : handlers_) {
        count += std::count_if(list.begin(), list.end(), [](const Handler& h) { return !h.removed; });
    }
    return count;
}

void EventBus::Dispatch(std::type_index type, const void* event) {
    const auto found = handlers_.find(type);
    if (found == handlers_.end()) {
        return;
    }
    ++dispatchDepth_;
    // Index loop: the vector may grow while handlers run; only the handlers present at the start run.
    const size_t count = found->second.size();
    for (size_t i = 0; i < count; ++i) {
        Handler& handler = found->second[i];
        if (handler.removed) {
            continue;
        }
        std::string error;
        const auto begin = slowReporter_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        try {
            auto call = handler.call;  // keep the callable alive even if the vector reallocates
            call(event);
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }
        if (slowReporter_) {
            const double us =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
            if (us > slowThresholdUs_) {
                slowReporter_(found->second[i].owner, type.name(), us);
            }
        }
        if (!error.empty()) {
            Handler& failed = found->second[i];
            ++failed.failures;
            const bool disable = failed.failures >= maxFailures_;
            if (reporter_) {
                reporter_(failed.owner, disable ? std::format("{} (failed {} times, handler removed)", error,
                                                              failed.failures)
                                                : error);
            }
            if (disable) {
                failed.removed = true;
            }
        }
    }
    --dispatchDepth_;
    Compact();
}

void EventBus::Compact() {
    if (dispatchDepth_ > 0) {
        return;
    }
    for (auto& [type, list] : handlers_) {
        std::erase_if(list, [](const Handler& h) { return h.removed; });
    }
}

}  // namespace sacore
