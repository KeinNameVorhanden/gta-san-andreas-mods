#include "harness.h"

#include "adapters.h"
#include "events.h"
#include "runtime/early_hooks.h"
#include "saves/save_file.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <fstream>
#include <sstream>

namespace sacore {
namespace {

std::string JsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Private message: run a requested save between frames, like the game's own menu save.
constexpr UINT kSaveMessage = WM_APP + 0x5A1;

__declspec(noinline) void HarnessCrash() {
    *static_cast<volatile int*>(nullptr) = 0x7E57;
}

}  // namespace

Harness::Harness(Logger& logger, EventBus& bus, Engine& engine, Game& game, const PatchManager& patches,
                 SaveService* saves,
                 std::filesystem::path testingDirectory,
                 std::filesystem::path logFile, std::string coreVersion, std::string buildId)
    : logger_(logger),
      bus_(bus),
      engine_(engine),
      game_(game),
      patches_(patches),
      saves_(saves),
      directory_(std::move(testingDirectory)),
      logFile_(std::move(logFile)),
      coreVersion_(std::move(coreVersion)),
      buildId_(std::move(buildId)) {}

bool Harness::Start() {
    const std::filesystem::path file = directory_ / L"scenario.toml";
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    std::ostringstream text;
    text << stream.rdbuf();
    const ScenarioParseResult parsed = ParseScenario(text.str());
    std::error_code error;
    std::filesystem::remove(directory_ / L"results.json", error);
    if (!parsed.scenario) {
        for (const std::string& e : parsed.errors) {
            logger_.Log(LogLevel::Error, LogCategory::Diagnostics, "harness", "scenario.toml: " + e);
            results_.push_back({0, "parse", false, e, 0});
        }
        WriteResults(false);
        return false;
    }
    scenario_ = *parsed.scenario;
    logger_.Logf(LogLevel::Warn, LogCategory::Diagnostics, "harness",
                 "TEST HARNESS ACTIVE: scenario '{}' ({} steps; second game instance {}, {} object name(s) made "
                 "unique: {})",
                 scenario_.name, scenario_.steps.size(),
                 early::Instance().hooksInstalled ? "allowed" : "not allowed", early::Instance().renamed,
                 early::Instance().names.empty() ? "-" : early::Instance().names);
    // Teleports change the world lists: run them between game logic steps, not from the Present hook.
    bus_.Subscribe<events::GameTick>("core.harness", [this](const events::GameTick&) {
        if (teleportState_ == 1) {
            teleportOk_ = game_.Teleport(teleportTo_[0], teleportTo_[1], teleportTo_[2]);
            teleportState_ = 2;
        }
        if (tickAction_ == 1 && tickStep_) {
            tickOk_ = tickStep_->action == StepAction::SetWaypoint
                          ? game_.SetWaypoint(static_cast<float>(tickStep_->x), static_cast<float>(tickStep_->y))
                          : game_.SetHealth(static_cast<float>(tickStep_->value));
            tickAction_ = 2;
        }
    });
    bus_.Subscribe<events::FramePresented>("core.harness", [this](const events::FramePresented&) {
        if (inFrame_) {
            return;
        }
        inFrame_ = true;
        OnFrame();
        inFrame_ = false;
    });
    engine_.AddMessageFilter("core.harness", [this](HWND, UINT message, WPARAM, LPARAM, LRESULT& result) {
        if (message == kSaveMessage && !pendingLoad_.empty()) {
            const std::string load = pendingLoad_;
            pendingLoad_.clear();
            if (saves_) {
                saves_->LoadFrom(load);
            }
            result = 0;
            return true;
        }
        if (message != kSaveMessage || pendingSave_.empty()) {
            return false;
        }
        const std::string name = pendingSave_;
        pendingSave_.clear();
        saveResult_ = saves_ && saves_->SaveTo(name) ? 1 : 0;
        result = 0;
        return true;
    });
    running_ = true;
    return true;
}

void Harness::OnFrame() {
    if (pendingKeyUp_ != 0 && engine_.FrameCount() >= keyUpFrame_) {
        PostMessageW(engine_.GameWindow(), WM_KEYUP, static_cast<WPARAM>(pendingKeyUp_), 0xC0000001);
        pendingKeyUp_ = 0;
    }
    if (!running_ || finished_) {
        return;
    }
    const uint64_t now = GetTickCount64();
    if (scenarioStart_ == 0) {
        scenarioStart_ = now;
        stepStart_ = now;
        stepStartFrames_ = engine_.FrameCount();
    }
    if (static_cast<double>(now - scenarioStart_) / 1000.0 > scenario_.timeoutSeconds) {
        results_.push_back({static_cast<int>(current_) + 1, "scenario", false,
                            std::format("scenario timeout after {} s", scenario_.timeoutSeconds), now - scenarioStart_});
        Finish(true);
        return;
    }
    while (current_ < scenario_.steps.size()) {
        const ScenarioStep& step = scenario_.steps[current_];
        std::string message;
        bool passed = false;
        if (!RunStep(step, static_cast<double>(GetTickCount64() - stepStart_) / 1000.0, message, passed)) {
            return;  // step still waiting; continue next frame
        }
        results_.push_back({static_cast<int>(current_) + 1, std::string(ToString(step.action)), passed, message,
                            GetTickCount64() - stepStart_});
        logger_.Logf(passed ? LogLevel::Info : LogLevel::Error, LogCategory::Diagnostics, "harness", "step {} {}: {} {}",
                     current_ + 1, ToString(step.action), passed ? "PASS" : "FAIL", message);
        ++current_;
        stepStart_ = GetTickCount64();
        stepStartFrames_ = engine_.FrameCount();
        if (!passed || step.action == StepAction::Quit) {
            Finish(false);
            return;
        }
    }
    Finish(false);
}

bool Harness::RunStep(const ScenarioStep& step, double seconds, std::string& message, bool& passed) {
    switch (step.action) {
        case StepAction::WaitState: {
            const int state = game_.State();
            if (state == static_cast<int>(step.value)) {
                passed = true;
                message = std::format("state {} reached after {:.1f} s", state, seconds);
                return true;
            }
            if (seconds > step.timeout) {
                message = std::format("state {} not reached within {} s (current {})", step.value, step.timeout, state);
                return true;
            }
            return false;
        }
        case StepAction::WaitSeconds:
            if (seconds < step.value) {
                return false;
            }
            passed = true;
            message = std::format("waited {:.1f} s", seconds);
            return true;
        case StepAction::ExpectCapability: {
            const std::string_view actual = ToString(game_.Adapter().State(step.text));
            passed = actual == step.state;
            message = std::format("{} is {} (expected {})", step.text, actual, step.state);
            return true;
        }
        case StepAction::ExpectPlayer:
        case StepAction::WaitPlayer: {
            const auto player = game_.Player();
            const bool waiting = step.action == StepAction::WaitPlayer && seconds <= step.timeout;
            if (!player) {
                message = "no player available";
                return !waiting;
            }
            double value = 0;
            if (step.text == "health") value = player->health;
            else if (step.text == "armour") value = player->armour;
            else if (step.text == "money") value = player->money;
            else if (step.text == "wanted") value = player->wantedLevel;
            else if (step.text == "cutscene") value = game_.CutsceneRunning() ? 1 : 0;
            else if (step.text == "mission") value = game_.OnMissionOrMiniGame() ? 1 : 0;
            else if (step.text == "menu") value = game_.PauseMenuActive() ? 1 : 0;
            else value = player->pedArea;
            passed = value >= step.min && value <= step.max;
            message = std::format("{} = {} (expected {}..{})", step.text, value, step.min, step.max);
            return passed || !waiting;
        }
        case StepAction::SetMoney:
            passed = game_.SetMoney(static_cast<int>(step.value));
            message = passed ? std::format("money set to {}", step.value) : "money write unavailable";
            return true;
        case StepAction::SimulateFocusLoss: {
            const HWND window = engine_.GameWindow();
            if (!window) {
                message = "no game window";
                return true;
            }
            SendMessageW(window, WM_ACTIVATE, WA_INACTIVE, 0);
            SendMessageW(window, WM_KILLFOCUS, 0, 0);
            SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
            passed = true;
            message = "deactivation messages sent";
            return true;
        }
        case StepAction::SimulateFocusReturn: {
            const HWND window = engine_.GameWindow();
            if (!window) {
                message = "no game window";
                return true;
            }
            SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
            SendMessageW(window, WM_ACTIVATE, WA_ACTIVE, 0);
            SendMessageW(window, WM_SETFOCUS, 0, 0);
            passed = true;
            message = "activation messages sent";
            return true;
        }
        case StepAction::SimulateMenuKey:
            passed = engine_.GameWindow() && PostMessageW(engine_.GameWindow(), WM_SYSCOMMAND, SC_KEYMENU, 0);
            message = passed ? "keyboard menu request posted (what F10 produces)" : "no game window";
            return true;
        case StepAction::SetSetting:
            passed = settingsSetter_ && settingsSetter_(step.mod, step.text, step.settingValue);
            message = std::format("setting {}.{} = {}: {}", step.mod, step.text, step.settingValue, passed ? "applied" : "refused");
            return true;
        case StepAction::SetWaypoint:
        case StepAction::SetHealth:
            if (tickAction_ == 0) {
                tickStep_ = &step;
                tickAction_ = 1;
                return false;
            }
            if (tickAction_ == 1) {
                if (seconds < 10) {
                    return false;
                }
                tickAction_ = 0;
                message = "no game tick within 10 s (not playing?)";
                return true;
            }
            tickAction_ = 0;
            passed = tickOk_;
            message = step.action == StepAction::SetWaypoint ? std::format("waypoint at ({:.1f}, {:.1f})", step.x, step.y)
                                                             : std::format("health {}", step.value);
            return true;
        case StepAction::ImportSave: {
            const std::string problem = saves_ ? saves_->Import(step.state, step.text) : "no save service";
            passed = problem.empty();
            message = passed ? std::format("{} imported as {}", step.state, step.text) : problem;
            return true;
        }
        case StepAction::Teleport:
            if (teleportState_ == 0) {
                teleportTo_[0] = static_cast<float>(step.x);
                teleportTo_[1] = static_cast<float>(step.y);
                teleportTo_[2] = static_cast<float>(step.z);
                teleportState_ = 1;
                return false;
            }
            if (teleportState_ == 1) {
                if (seconds < 10) {
                    return false;
                }
                teleportState_ = 0;
                message = "no game tick within 10 s (not playing?)";
                return true;
            }
            teleportState_ = 0;
            passed = teleportOk_;
            message = std::format("teleport to ({:.1f}, {:.1f}, {:.1f}): {}", step.x, step.y, step.z,
                                  passed ? "done" : "unavailable");
            return true;
        case StepAction::HoldKey: {
            const HWND window = engine_.GameWindow();
            const auto key = static_cast<WPARAM>(step.value);
            if (!holding_) {
                holding_ = window && PostMessageW(window, WM_KEYDOWN, key, 1);
                if (!holding_) {
                    message = "no game window";
                    return true;
                }
                return false;
            }
            if (seconds < step.seconds) {
                return false;
            }
            PostMessageW(window, WM_KEYUP, key, 0xC0000001);
            holding_ = false;
            passed = true;
            message = std::format("key {} held for {:.1f} s", static_cast<int>(step.value), seconds);
            return true;
        }
        case StepAction::MarkPosition: {
            const auto player = game_.Player();
            markedValid_ = player.has_value();
            if (player) {
                std::copy(std::begin(player->position), std::end(player->position), marked_);
            }
            passed = markedValid_;
            message = player ? std::format("position ({:.1f}, {:.1f}, {:.1f}) marked", marked_[0], marked_[1], marked_[2])
                             : "no player";
            return true;
        }
        case StepAction::ExpectDistance: {
            const auto player = game_.Player();
            if (!player || !markedValid_) {
                message = "no player or no marked position";
                return true;
            }
            const double d = std::hypot(player->position[0] - marked_[0], player->position[1] - marked_[1]);
            distances_.push_back(d);
            passed = d >= step.min && d <= step.max;
            message = std::format("distance {:.2f} m (expected {} .. {})", d, step.min, step.max);
            return true;
        }
        case StepAction::ExpectRatio: {
            if (distances_.size() < 2 || distances_[distances_.size() - 2] <= 0.0) {
                message = "needs two expect_distance results";
                return true;
            }
            const double ratio = distances_.back() / distances_[distances_.size() - 2];
            passed = ratio >= step.min && ratio <= step.max;
            message = std::format("ratio {:.3f} ({:.2f} m / {:.2f} m, expected {} .. {})", ratio, distances_.back(),
                                  distances_[distances_.size() - 2], step.min, step.max);
            return true;
        }
        case StepAction::PressKey: {
            const HWND window = engine_.GameWindow();
            const auto key = static_cast<WPARAM>(step.value);
            // Held for a few frames: the game samples its keyboard state once per frame, a key that goes down and up
            // between two samples is never seen as pressed (mods react to the key-down message either way).
            passed = window && PostMessageW(window, WM_KEYDOWN, key, 1);
            pendingKeyUp_ = passed ? static_cast<int>(key) : 0;
            keyUpFrame_ = engine_.FrameCount() + 4;
            message = std::format("key {} pressed", static_cast<int>(step.value));
            return true;
        }
        case StepAction::LoadSave:
            pendingLoad_ = step.text;
            passed = PostMessageW(engine_.GameWindow(), kSaveMessage, 0, 0) != 0;
            message = passed ? "load of " + step.text + " requested" : "no game window";
            return true;
        case StepAction::StartNewGame:
            passed = game_.RequestNewGame(true);
            message = passed ? "new game requested from the pause menu routine" : "no game window";
            return true;
        case StepAction::ExpectFramesAdvance: {
            if (seconds < step.value) {
                return false;
            }
            const uint64_t frames = engine_.FrameCount() - stepStartFrames_;
            passed = static_cast<double>(frames) >= step.min;
            message = std::format("{} frames in {:.1f} s (need {})", frames, seconds, step.min);
            return true;
        }
        case StepAction::ExpectLog:
            if (const uint64_t now = GetTickCount64(); now - logCheckedAt_ < 1000 && seconds <= step.timeout) {
                return false;
            } else {
                logCheckedAt_ = now;
            }
            if (LogContains(step.text)) {
                passed = true;
                message = std::format("log contains \"{}\"", step.text);
                return true;
            }
            if (!step.failText.empty() && LogContains(step.failText)) {
                message = std::format("log contains \"{}\" before \"{}\"", step.failText, step.text);
                return true;
            }
            if (seconds > step.timeout) {
                message = std::format("log does not contain \"{}\" after {} s", step.text, step.timeout);
                return true;
            }
            return false;
        case StepAction::ExpectWindowCaption: {
            const HWND window = engine_.GameWindow();
            const auto style = window ? static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)) : 0;
            passed = (style & WS_CAPTION) == WS_CAPTION && engine_.WindowedActive();
            message = std::format("window style {:#010x}, windowed {}", style, engine_.WindowedActive());
            return true;
        }
        case StepAction::ExpectPatch: {
            const PatchRecord* patch = patches_.Find(step.text);
            const std::string actual = patch ? std::string(ToString(patch->state)) : "not registered";
            passed = actual == step.state;
            message = std::format("patch {} is {} (expected {})", step.text, actual, step.state);
            return true;
        }
        case StepAction::SaveSlot:
            // The game only saves in free roam; while a cutscene or interior (world area != 0) is active the step
            // waits, up to its timeout (default 30 s).
            if (pendingSave_.empty() && saveResult_ == -1 && game_.CurrentArea() != 0) {
                if (seconds > step.timeout) {
                    message = std::format("refused: world area is still {} after {} s (cutscene or interior), save "
                                          "only in free roam", game_.CurrentArea(), step.timeout);
                    return true;
                }
                return false;
            }
            if (saveResult_ == -1 && pendingSave_.empty()) {
                pendingSave_ = step.text.empty() ? std::format("slot{}", static_cast<int>(step.value)) : step.text;
                PostMessageW(engine_.GameWindow(), kSaveMessage, 0, 0);
                return false;
            }
            if (saveResult_ == -1) {
                if (seconds > step.timeout) {
                    pendingSave_.clear();
                    message = "save request was not processed by the message loop";
                    return true;
                }
                return false;
            }
            passed = saveResult_ == 1;
            saveResult_ = -1;
            message = passed ? (step.text.empty() ? std::format("saved to slot {}", step.value) : "saved to " + step.text)
                             : "save failed or unavailable";
            return true;
        case StepAction::ExpectSaveValid: {
            if (!saves_) {
                message = "save service unavailable";
                return true;
            }
            const std::filesystem::path file = step.text.empty() ? saves_->SlotFile(static_cast<int>(step.value))
                                                                 : saves_->FileFor(step.text);
            std::ifstream stream(file, std::ios::binary);
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            const SaveCheck check = CheckSave(bytes);
            passed = check.valid;
            message = std::format("{}: {}", file.string(), check.valid ? "valid" : check.problem);
            return true;
        }
        case StepAction::CrashNow:
            WriteResults(false);  // the crash ends the process; the runner checks the crash report
            logger_.Log(LogLevel::Warn, LogCategory::Diagnostics, "harness", "crash_now: crashing deliberately");
            logger_.Flush();
            HarnessCrash();
            return true;
        case StepAction::Quit:
            passed = true;
            message = "quit requested";
            return true;
    }
    return true;
}

bool Harness::LogContains(const std::string& text) const {
    std::ifstream stream(logFile_, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void Harness::Finish(bool timedOut) {
    finished_ = true;
    WriteResults(timedOut);
    logger_.Logf(LogLevel::Warn, LogCategory::Diagnostics, "harness", "scenario '{}' finished, closing the game",
                 scenario_.name);
    logger_.Flush();
    if (engine_.GameWindow()) {
        PostMessageW(engine_.GameWindow(), WM_CLOSE, 0, 0);
    }
}

void Harness::WriteResults(bool timedOut) const {
    bool passed = !results_.empty() && !timedOut;
    for (const StepResult& r : results_) {
        passed = passed && r.passed;
    }
    const bool complete = results_.size() == scenario_.steps.size();
    std::string json = std::format(
        "{{\n  \"scenario\": \"{}\",\n  \"build\": \"{}\",\n  \"core_version\": \"{}\",\n  \"passed\": {},\n"
        "  \"complete\": {},\n  \"timed_out\": {},\n  \"steps\": [\n",
        JsonEscape(scenario_.name), JsonEscape(buildId_), JsonEscape(coreVersion_), passed && complete, complete,
        timedOut);
    for (size_t i = 0; i < results_.size(); ++i) {
        const StepResult& r = results_[i];
        json += std::format("    {{\"index\": {}, \"action\": \"{}\", \"passed\": {}, \"elapsed_ms\": {}, "
                            "\"message\": \"{}\"}}{}\n",
                            r.index, r.action, r.passed, r.elapsedMs, JsonEscape(r.message),
                            i + 1 < results_.size() ? "," : "");
    }
    json += "  ]\n}\n";
    std::ofstream(directory_ / L"results.json", std::ios::binary | std::ios::trunc) << json;
}

}  // namespace sacore
