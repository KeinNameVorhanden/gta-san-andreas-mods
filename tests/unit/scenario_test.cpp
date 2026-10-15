#include "testing/scenario.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using sacore::ParseScenario;
using sacore::StepAction;

namespace {
bool HasError(const sacore::ScenarioParseResult& r, const std::string& fragment) {
    return std::any_of(r.errors.begin(), r.errors.end(),
                       [&](const std::string& e) { return e.find(fragment) != std::string::npos; });
}
}  // namespace

TEST_CASE("a valid scenario keeps step order and numeric values given as integers or floats") {
    const auto r = ParseScenario(R"(
name = "smoke"
timeout_seconds = 90
[[step]]
action = "wait_state"
value = 9
timeout = 60
[[step]]
action = "expect_player"
field = "health"
min = 1
max = 200.5
[[step]]
action = "quit"
)");
    REQUIRE(r.errors.empty());
    REQUIRE(r.scenario->steps.size() == 3);
    CHECK(r.scenario->timeoutSeconds == 90.0);
    CHECK(r.scenario->steps[0].action == StepAction::WaitState);
    CHECK(r.scenario->steps[0].timeout == 60.0);
    CHECK(r.scenario->steps[1].max == 200.5);
    CHECK(r.scenario->steps[2].action == StepAction::Quit);
}

TEST_CASE("every malformed step is reported with its index, so a broken scenario never runs half-way") {
    const auto r = ParseScenario(R"(
[[step]]
action = "fly_to_moon"
[[step]]
action = "expect_player"
field = "stamina"
min = 5
max = 1
[[step]]
action = "expect_capability"
id = "player.read"
state = "maybe"
[[step]]
action = "wait_state"
[[step]]
action = "teleport"
x = 2495.2
y = -1687.0
)");
    CHECK_FALSE(r.scenario);
    CHECK(HasError(r, "step 1: unknown action 'fly_to_moon'"));
    CHECK(HasError(r, "step 2: unknown player field 'stamina'"));
    CHECK(HasError(r, "step 2: min > max"));
    CHECK(HasError(r, "step 3: unknown capability state 'maybe'"));
    CHECK(HasError(r, "step 4 (wait_state): missing 'value'"));
    CHECK(HasError(r, "step 5 (teleport): missing 'z'"));
}

TEST_CASE("a scenario without steps is rejected") {
    CHECK(HasError(ParseScenario("name = \"empty\"\n"), "no [[step]] entries"));
}
