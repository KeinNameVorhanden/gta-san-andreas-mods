// Runtime test harness: runs a scenario on the game thread, frame by frame, and writes results.json.
// Active only when <game>\sacore\testing\scenario.toml exists (dev installs only, never packaged).
#pragma once

#include "engine/engine.h"
#include "event_bus.h"
#include "game/game.h"
#include "logger.h"
#include "patch_manager.h"
#include "saves/save_service.h"
#include "testing/scenario.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sacore {

class Harness {
public:
    Harness(Logger& logger, EventBus& bus, Engine& engine, Game& game, const PatchManager& patches, SaveService* saves,
            std::filesystem::path testingDirectory,
            std::filesystem::path logFile, std::string coreVersion, std::string buildId);

    // set_setting steps go through the same path as the F11 console (set by the core runtime).
    void SetSettingsSetter(std::function<bool(const std::string&, const std::string&, const std::string&)> setter) {
        settingsSetter_ = std::move(setter);
    }

    // Loads scenario.toml if present; returns false when the harness stays inactive.
    bool Start();

private:
    struct StepResult {
        int index;
        std::string action;
        bool passed;
        std::string message;
        uint64_t elapsedMs;
    };

    void OnFrame();
    // Returns true when the current step finished (passed or failed).
    bool RunStep(const ScenarioStep& step, double stepSeconds, std::string& message, bool& passed);
    void Finish(bool timedOut);
    void WriteResults(bool timedOut) const;
    bool LogContains(const std::string& text) const;

    Logger& logger_;
    EventBus& bus_;
    Engine& engine_;
    Game& game_;
    const PatchManager& patches_;
    SaveService* saves_;
    std::filesystem::path directory_;
    std::filesystem::path logFile_;
    std::string coreVersion_;
    std::string buildId_;
    Scenario scenario_;
    std::vector<StepResult> results_;
    size_t current_ = 0;
    uint64_t scenarioStart_ = 0;
    uint64_t stepStart_ = 0;
    uint64_t stepStartFrames_ = 0;
    bool running_ = false;
    bool finished_ = false;
    bool inFrame_ = false;          // the game may present frames while saving: no re-entry
    std::function<bool(const std::string&, const std::string&, const std::string&)> settingsSetter_;
    std::string pendingLoad_;
    bool holding_ = false;          // hold_key: key is down
    float marked_[3] = {};
    bool markedValid_ = false;
    std::vector<double> distances_;  // expect_distance results
    int pendingKeyUp_ = 0;          // press_key: key released a few frames later
    uint64_t keyUpFrame_ = 0;
    int teleportState_ = 0;         // 0 idle, 1 waiting for a game tick, 2 done
    int tickAction_ = 0;            // set_waypoint / set_health: 0 idle, 1 waiting for a game tick, 2 done
    bool tickOk_ = false;
    const ScenarioStep* tickStep_ = nullptr;
    bool teleportOk_ = false;
    float teleportTo_[3] = {};
    std::string pendingSave_;       // save requested ("slot3", "quick1"), executed from the message loop
    int saveResult_ = -1;           // -1 pending, 0 failed, 1 saved
    uint64_t logCheckedAt_ = 0;     // expect_log: the log file is read at most once a second
};

}  // namespace sacore
