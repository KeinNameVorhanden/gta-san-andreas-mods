#include "event_bus.h"

#include <doctest/doctest.h>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

using sacore::EventBus;

namespace {
struct Tick {
    int frame;
};
struct Other {};
}  // namespace

TEST_CASE("a handler can unsubscribe a later handler during dispatch; it does not run") {
    EventBus bus;
    std::vector<std::string> calls;
    sacore::SubscriptionId second = 0;
    bus.Subscribe<Tick>("a", [&](const Tick&) {
        calls.push_back("a");
        bus.Unsubscribe(second);
    });
    second = bus.Subscribe<Tick>("b", [&](const Tick&) { calls.push_back("b"); });
    bus.Publish(Tick{1});
    CHECK(calls == std::vector<std::string>{"a"});
    CHECK(bus.HandlerCount() == 1);
}

TEST_CASE("a handler subscribed during dispatch runs from the next publish on") {
    EventBus bus;
    int lateCalls = 0;
    bool added = false;
    bus.Subscribe<Tick>("a", [&](const Tick&) {
        if (!added) {
            added = true;
            bus.Subscribe<Tick>("late", [&](const Tick&) { ++lateCalls; });
        }
    });
    bus.Publish(Tick{1});
    CHECK(lateCalls == 0);
    bus.Publish(Tick{2});
    CHECK(lateCalls == 1);
}

TEST_CASE("a throwing handler is reported, removed after repeated failures, and never blocks others") {
    std::vector<std::string> reports;
    EventBus bus([&](const std::string& owner, const std::string& error) { reports.push_back(owner + ": " + error); },
                 2);
    int healthy = 0;
    bus.Subscribe<Tick>("broken", [](const Tick&) { throw std::runtime_error("boom"); });
    bus.Subscribe<Tick>("healthy", [&](const Tick&) { ++healthy; });
    for (int i = 0; i < 4; ++i) {
        bus.Publish(Tick{i});
    }
    CHECK(healthy == 4);
    REQUIRE(reports.size() == 2);
    CHECK(reports[0] == "broken: boom");
    CHECK(reports[1] == "broken: boom (failed 2 times, handler removed)");
    CHECK(bus.HandlerCount() == 1);
}

TEST_CASE("unsubscribing an owner removes all its handlers across event types") {
    EventBus bus;
    int calls = 0;
    bus.Subscribe<Tick>("mod", [&](const Tick&) { ++calls; });
    bus.Subscribe<Other>("mod", [&](const Other&) { ++calls; });
    bus.Subscribe<Tick>("keep", [&](const Tick&) { ++calls; });
    bus.UnsubscribeOwner("mod");
    bus.Publish(Tick{0});
    bus.Publish(Other{});
    CHECK(calls == 1);
}

TEST_CASE("slow handlers are attributed to their owner, fast ones and throwing slow ones are told apart") {
    EventBus bus;
    struct Report {
        std::string owner;
        double us;
    };
    std::vector<Report> reports;
    bus.SetSlowHandlerReporter([&](const std::string& owner, const char*, double us) { reports.push_back({owner, us}); },
                               5000.0);
    bus.Subscribe<Tick>("fast", [](const Tick&) {});
    bus.Subscribe<Tick>("slow", [](const Tick&) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
    bus.Subscribe<Tick>("slow_throw", [](const Tick&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        throw std::runtime_error("boom");
    });
    bus.Publish(Tick{1});
    REQUIRE(reports.size() == 2);
    CHECK(reports[0].owner == "slow");
    CHECK(reports[0].us >= 15000.0);
    CHECK(reports[1].owner == "slow_throw");
}
