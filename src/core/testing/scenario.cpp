#include "scenario.h"

#include <toml++/toml.hpp>

#include <array>
#include <format>
#include <utility>

namespace sacore {
namespace {

constexpr std::array<std::pair<std::string_view, StepAction>, 29> kActions = {{
    {"wait_state", StepAction::WaitState},
    {"wait_seconds", StepAction::WaitSeconds},
    {"expect_capability", StepAction::ExpectCapability},
    {"expect_player", StepAction::ExpectPlayer},
    {"wait_player", StepAction::WaitPlayer},
    {"set_money", StepAction::SetMoney},
    {"simulate_focus_loss", StepAction::SimulateFocusLoss},
    {"simulate_focus_return", StepAction::SimulateFocusReturn},
    {"simulate_menu_key", StepAction::SimulateMenuKey},
    {"start_new_game", StepAction::StartNewGame},
    {"load_save", StepAction::LoadSave},
    {"press_key", StepAction::PressKey},
    {"teleport", StepAction::Teleport},
    {"hold_key", StepAction::HoldKey},
    {"mark_position", StepAction::MarkPosition},
    {"expect_distance", StepAction::ExpectDistance},
    {"expect_ratio", StepAction::ExpectRatio},
    {"set_waypoint", StepAction::SetWaypoint},
    {"set_health", StepAction::SetHealth},
    {"import_save", StepAction::ImportSave},
    {"set_setting", StepAction::SetSetting},
    {"expect_frames_advance", StepAction::ExpectFramesAdvance},
    {"expect_log", StepAction::ExpectLog},
    {"expect_window_caption", StepAction::ExpectWindowCaption},
    {"expect_patch", StepAction::ExpectPatch},
    {"save_slot", StepAction::SaveSlot},
    {"expect_save_valid", StepAction::ExpectSaveValid},
    {"crash_now", StepAction::CrashNow},
    {"quit", StepAction::Quit},
}};

constexpr std::array<std::string_view, 8> kPlayerFields = {"health", "armour", "money", "wanted", "area", "cutscene",
                                                             "mission", "menu"};
constexpr std::array<std::string_view, 4> kCapabilityStates = {"supported", "experimental", "unsupported",
                                                                 "not investigated"};

std::optional<double> Number(const toml::table& table, std::string_view key) {
    if (auto i = table[key].value<int64_t>()) {
        return static_cast<double>(*i);
    }
    return table[key].value<double>();
}

}  // namespace

std::string_view ToString(StepAction action) {
    for (const auto& [name, value] : kActions) {
        if (value == action) {
            return name;
        }
    }
    return "?";
}

ScenarioParseResult ParseScenario(std::string_view tomlText) {
    ScenarioParseResult result;
    toml::table table;
    try {
        table = toml::parse(tomlText);
    } catch (const toml::parse_error& error) {
        result.errors.push_back(std::format("syntax error at line {}: {}", error.source().begin.line,
                                            error.description()));
        return result;
    }
    Scenario scenario;
    scenario.name = table["name"].value_or(std::string("unnamed"));
    scenario.timeoutSeconds = Number(table, "timeout_seconds").value_or(180.0);
    scenario.expectCrash = table["expect_crash"].value_or(false);
    const toml::array* steps = table["step"].as_array();
    if (!steps || steps->empty()) {
        result.errors.push_back("scenario has no [[step]] entries");
        return result;
    }
    int index = 0;
    for (const toml::node& node : *steps) {
        ++index;
        const toml::table* step = node.as_table();
        if (!step) {
            result.errors.push_back(std::format("step {}: not a table", index));
            continue;
        }
        const std::string actionName = (*step)["action"].value_or(std::string{});
        const auto action = std::find_if(kActions.begin(), kActions.end(),
                                         [&](const auto& a) { return a.first == actionName; });
        if (action == kActions.end()) {
            result.errors.push_back(std::format("step {}: unknown action '{}'", index, actionName));
            continue;
        }
        ScenarioStep s{action->second};
        s.value = Number(*step, "value").value_or(0);
        s.timeout = Number(*step, "timeout").value_or(30);
        s.min = Number(*step, "min").value_or(0);
        s.max = Number(*step, "max").value_or(0);
        auto need = [&](std::string_view key) {
            if (!step->contains(key)) {
                result.errors.push_back(std::format("step {} ({}): missing '{}'", index, actionName, key));
                return false;
            }
            return true;
        };
        switch (s.action) {
            case StepAction::WaitState:
            case StepAction::WaitSeconds:
            case StepAction::SetMoney:
            case StepAction::PressKey:
                need("value");
                break;
            case StepAction::HoldKey:
                if (need("value") && need("seconds")) {
                    s.seconds = Number(*step, "seconds").value_or(0);
                }
                break;
            case StepAction::ExpectDistance:
            case StepAction::ExpectRatio:
                need("min");
                need("max");
                break;
            case StepAction::SetHealth:
                need("value");
                break;
            case StepAction::ImportSave:
                if (need("name") && need("id")) {
                    s.text = (*step)["name"].value_or(std::string{});
                    s.state = (*step)["id"].value_or(std::string{});
                }
                break;
            case StepAction::SetWaypoint:
                if (need("x") && need("y")) {
                    s.x = Number(*step, "x").value_or(0);
                    s.y = Number(*step, "y").value_or(0);
                }
                break;
            case StepAction::Teleport:
                if (need("x") && need("y") && need("z")) {
                    s.x = Number(*step, "x").value_or(0);
                    s.y = Number(*step, "y").value_or(0);
                    s.z = Number(*step, "z").value_or(0);
                }
                break;
            case StepAction::SetSetting:
                if (need("name") && need("id") && need("contains")) {
                    s.mod = (*step)["name"].value_or(std::string{});
                    s.text = (*step)["id"].value_or(std::string{});
                    s.settingValue = (*step)["contains"].value_or(std::string{});
                }
                break;
            case StepAction::LoadSave:
                if (need("name")) {
                    s.text = (*step)["name"].value_or(std::string{});
                }
                break;
            case StepAction::SaveSlot:
            case StepAction::ExpectSaveValid:
                // value = menu slot 1..8, or name = "slot3" / "quick1" / "auto2"
                if (step->contains("name")) {
                    s.text = (*step)["name"].value_or(std::string{});
                } else {
                    need("value");
                }
                break;
            case StepAction::ExpectCapability:
                if (need("id") && need("state")) {
                    s.text = (*step)["id"].value_or(std::string{});
                    s.state = (*step)["state"].value_or(std::string{});
                    if (std::find(kCapabilityStates.begin(), kCapabilityStates.end(), s.state) ==
                        kCapabilityStates.end()) {
                        result.errors.push_back(std::format("step {}: unknown capability state '{}'", index, s.state));
                    }
                }
                break;
            case StepAction::ExpectPlayer:
            case StepAction::WaitPlayer:
                if (need("field") && need("min") && need("max")) {
                    s.text = (*step)["field"].value_or(std::string{});
                    if (std::find(kPlayerFields.begin(), kPlayerFields.end(), s.text) == kPlayerFields.end()) {
                        result.errors.push_back(std::format("step {}: unknown player field '{}'", index, s.text));
                    }
                    if (s.min > s.max) {
                        result.errors.push_back(std::format("step {}: min > max", index));
                    }
                }
                break;
            case StepAction::ExpectFramesAdvance:
                need("value");
                need("min");
                break;
            case StepAction::ExpectPatch:
                if (need("id") && need("state")) {
                    s.text = (*step)["id"].value_or(std::string{});
                    s.state = (*step)["state"].value_or(std::string{});
                }
                break;
            case StepAction::ExpectLog:
                if (need("contains")) {
                    s.text = (*step)["contains"].value_or(std::string{});
                    s.failText = (*step)["fail_contains"].value_or(std::string{});
                }
                break;
            default:
                break;
        }
        scenario.steps.push_back(std::move(s));
    }
    if (result.errors.empty()) {
        result.scenario = std::move(scenario);
    }
    return result;
}

}  // namespace sacore
