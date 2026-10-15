// Story runner: plays the story up to a chosen mission. For every mission before the target, in story order, it puts
// CJ into the contact marker so the game's own trigger thread launches the mission, then passes it (pass code, a
// researched entry, Rockstar's race skip, or it lets a cutscene-only mission play) and waits until the main script has
// reacted (new contacts, phone calls, which it answers). It stops next to the target's marker. Launch conditions come
// from the trigger threads in main.scm: strand counter, marker globals, interior, time of day, phone calls, a vehicle,
// property purchases, respect, territory and Millie's keycard.
#include "pass.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>
#include <string>
#include <vector>

namespace mission_pass {
namespace {

// A main script flag the trigger needs (set by a phone call, which the runner answers, or by a prep action).
struct Need {
    int global = 0;  // 0: none
    int atLeast = 0;
    const char* what = nullptr;
};

enum class Means : uint8_t {
    Foot,  // on foot in the marker
    Car,   // in a lowrider (also a normal car class) in the marker: the runner spawns a Voodoo
    Any,   // on foot or in a vehicle
    Auto,  // launched by the trigger thread right after the previous step, no marker
};

enum class Prep : uint8_t {
    None,
    SaveTutorial,  // after Big Smoke: the main script wants CJ to visit the house before Ryder's marker appears
    Respect,       // FIND_MAX_NUMBER_OF_GROUP_MEMBERS > 1 needs respect >= 10
    Keycard,       // Millie's keycard ($37) for the heist finale
    Territory,     // more than 34 % of the gang territory for End of the Line
};

struct Step {
    int mission;            // index in the mission table
    int counter;            // strand counter global: the step is done when it is above `value`
    int value;
    int marker = 0;         // first of the x, y, z marker globals; 0: `fixed`
    float fixed[3] = {};
    int hour = -1;          // an hour inside the trigger's time window
    Need need{};            // set by the main script before the trigger fires
    Need need2{};
    Means means = Means::Foot;
    int area = 0;           // interior of the marker
    int money = 0;          // property price: CJ needs this much before he steps on the for-sale pickup
    Prep prep = Prep::None;
    const char* doneThread = nullptr;  // the step is also done while this script thread runs
    const char* label = nullptr;       // name when the mission has several steps; nullptr: the mission title
};

constexpr int kBuy = 134;  // BUYPRO1, launched when a for-sale pickup is collected
constexpr const char* kComplete = "Story complete";  // the last target: End of the Line played through, free roam after
// Catalina's marker: first visit, later visits (globals $511/$517/$523 and $506/$512/$518 from mission 0).
constexpr float kCatalinaFirst[3] = {681.60f, -478.79f, 15.33f};
constexpr float kCatalina[3] = {868.34f, -29.55f, 62.33f};

// The whole story in order. Los Santos: trigger threads INT, SWEET, SMOKE, RYDER, STRAP, CRASH, CESAR and LA1FIN.
// After Los Santos: the 18 trigger threads and the phone-call dispatchers MOB_CAT, MOB_SF, MOB_VEG and MOB_LA2
// (optional San Fierro steps placed where their conditions are met).
constexpr Step kSteps[] = {
    {.mission = 11, .counter = 448, .value = 0, .marker = 459},  // Big Smoke
    {.mission = 12, .counter = 448, .value = 1, .marker = 465, .prep = Prep::SaveTutorial},
    {.mission = 13, .counter = 452, .value = 0, .marker = 462},
    {.mission = 14, .counter = 452, .value = 1, .marker = 462},
    {.mission = 15, .counter = 452, .value = 2, .marker = 462},
    {.mission = 16, .counter = 452, .value = 3, .marker = 462},
    {.mission = 17, .counter = 452, .value = 4, .marker = 462},
    {.mission = 18, .counter = 452, .value = 5, .marker = 462},
    {.mission = 19, .counter = 452, .value = 6, .marker = 462},
    {.mission = 27, .counter = 454, .value = 0, .marker = 468},
    {.mission = 28, .counter = 454, .value = 1, .marker = 468},
    {.mission = 29, .counter = 454, .value = 2, .marker = 468},
    {.mission = 30, .counter = 454, .value = 3, .marker = 468},
    {.mission = 24, .counter = 453, .value = 0, .marker = 465, .hour = 13},  // 12:00 to 20:00
    {.mission = 25, .counter = 453, .value = 1, .marker = 465},
    {.mission = 26, .counter = 453, .value = 2, .marker = 465},
    {.mission = 31, .counter = 455, .value = 0, .marker = 471, .hour = 23},  // 22:00 to 6:00
    {.mission = 32, .counter = 455, .value = 1, .marker = 471},
    {.mission = 33, .counter = 455, .value = 2, .marker = 471, .hour = 13},  // from 12:00
    {.mission = 34, .counter = 455, .value = 3, .marker = 471, .label = "House Party (first visit)"},
    {.mission = 34, .counter = 455, .value = 4, .marker = 474, .hour = 21, .label = "House Party (party)"},
    {.mission = 22, .counter = 456, .value = 0, .marker = 477, .need = {1343, 1, "Tenpenny's phone call"}},
    {.mission = 23, .counter = 456, .value = 1, .marker = 477},
    {.mission = 21, .counter = 452, .value = 7, .fixed = {1365.251f, -1280.120f, 12.547f},
     .need = {1346, 1, "Sweet's phone call about the Ammu-Nation"}},
    // Respect, not recruits: $47 is FIND_MAX_NUMBER_OF_GROUP_MEMBERS (story_runner.md 6.1). 9:00 to 17:00.
    {.mission = 20, .counter = 452, .value = 8, .marker = 462, .hour = 10, .prep = Prep::Respect},
    // High Stakes, Low-rider: lowrider into the marker; the meeting's normal end ($2336 = 1) makes CESAR launch the race.
    {.mission = 36, .counter = 2336, .value = 0, .marker = 480, .need = {1344, 1, "Cesar's phone call"},
     .means = Means::Car, .label = "High Stakes, Low-rider"},
    {.mission = 35, .counter = 457, .value = 0, .means = Means::Auto, .label = "High Stakes, Low-rider (race)"},
    {.mission = 37, .counter = 458, .value = 0, .marker = 462},
    {.mission = 38, .counter = 458, .value = 1, .marker = 462},
    // Badlands. Catalina's job markers are not consecutive globals: fixed points from mission 0.
    {.mission = 39, .counter = 493, .value = 0, .marker = 494},
    {.mission = 40, .counter = 64, .value = 0, .fixed = {kCatalinaFirst[0], kCatalinaFirst[1], kCatalinaFirst[2]},
     .need = {1356, 1, "Catalina's phone call"}, .label = "First Date"},
    {.mission = 41, .counter = 64, .value = 0, .means = Means::Auto},
    {.mission = 46, .counter = 491, .value = 0, .marker = 500, .need = {1357, 1, "the Truth's phone call"}},
    {.mission = 45, .counter = 2163, .value = 0, .marker = 494, .label = "King in Exile"},
    {.mission = 40, .counter = 64, .value = 1, .fixed = {kCatalina[0], kCatalina[1], kCatalina[2]}, .label = "First Base"},
    {.mission = 42, .counter = 64, .value = 1, .means = Means::Auto},
    {.mission = 48, .counter = 492, .value = 0, .marker = 503, .need = {1362, 1, "Cesar's phone call"},
     .means = Means::Car, .label = "Wu Zi Mu"},
    {.mission = 35, .counter = 492, .value = 2, .means = Means::Auto, .label = "Wu Zi Mu (race)"},
    {.mission = 48, .counter = 492, .value = 4, .means = Means::Auto, .label = "Wu Zi Mu (after the race)"},
    {.mission = 40, .counter = 64, .value = 2, .fixed = {kCatalina[0], kCatalina[1], kCatalina[2]}, .label = "Gone Courting"},
    {.mission = 43, .counter = 64, .value = 2, .means = Means::Auto},
    {.mission = 40, .counter = 64, .value = 3, .fixed = {kCatalina[0], kCatalina[1], kCatalina[2]}, .label = "Made in Heaven"},
    {.mission = 44, .counter = 64, .value = 3, .means = Means::Auto},
    {.mission = 48, .counter = 492, .value = 5, .fixed = {-513.936f, -188.314f, 77.460f}, .means = Means::Any,
     .label = "Farewell, My Love"},
    {.mission = 35, .counter = 492, .value = 7, .means = Means::Auto, .label = "Farewell, My Love (race)"},
    {.mission = 48, .counter = 492, .value = 9, .means = Means::Auto, .label = "Farewell, My Love (after the race)"},
    {.mission = 47, .counter = 491, .value = 1, .marker = 497},
    // San Fierro
    {.mission = 49, .counter = 541, .value = 0, .marker = 547},
    {.mission = 51, .counter = 546, .value = 0, .marker = 574},
    {.mission = 50, .counter = 541, .value = 1, .marker = 547},
    {.mission = kBuy, .counter = 1620, .value = 0, .fixed = {-2243.620f, 133.200f, 34.800f},
     .need = {1395, 1, "the phone call about Zero's shop"}, .money = 30000, .label = "Buy Zero's RC shop"},
    {.mission = 71, .counter = 86, .value = 0, .fixed = {-2031.400f, -116.500f, 1034.100f},
     .need = {1397, 1, "the phone call about the driving school"}, .area = 3},
    {.mission = 72, .counter = 542, .value = 0, .marker = 550, .need = {1396, 1, "Zero's phone call"}},
    {.mission = 73, .counter = 542, .value = 1, .marker = 550},
    {.mission = 74, .counter = 542, .value = 2, .marker = 550},
    {.mission = 58, .counter = 545, .value = 0, .marker = 547},
    {.mission = 59, .counter = 545, .value = 1, .marker = 547},
    {.mission = 59, .counter = 545, .value = 2, .marker = 559, .label = "Jizzy (club)"},
    {.mission = 65, .counter = 545, .value = 3, .marker = 559},
    {.mission = 66, .counter = 545, .value = 4, .marker = 559},
    {.mission = 60, .counter = 545, .value = 5, .marker = 547},
    {.mission = 52, .counter = 546, .value = 1, .marker = 574},
    {.mission = 61, .counter = 545, .value = 6, .marker = 547, .hour = 22},  // 20:00 to 6:00
    {.mission = 64, .counter = 545, .value = 7, .fixed = {-1717.050f, 1280.910f, 6.230f}},
    {.mission = 62, .counter = 545, .value = 8, .marker = 547},
    {.mission = 53, .counter = 543, .value = 0, .marker = 553, .need = {1401, 1, "Woozie's phone call"}},
    {.mission = 54, .counter = 543, .value = 1, .marker = 553},
    {.mission = 55, .counter = 543, .value = 2, .marker = 553},
    {.mission = 56, .counter = 543, .value = 3, .marker = 553, .hour = 22},  // 20:00 to 6:00 with lung capacity
    {.mission = 57, .counter = 543, .value = 4, .marker = 553},
    {.mission = 63, .counter = 545, .value = 9, .marker = 547},
    // Desert, with Wang Cars once the driving school is passed
    {.mission = 75, .counter = 593, .value = 0, .marker = 601, .need = {1404, 1, "Toreno's phone call"}},
    {.mission = kBuy, .counter = 544, .value = 0, .fixed = {-1969.270f, 282.470f, 34.600f},
     .need = {1399, 1, "the phone call about Wang Cars"}, .money = 50000, .doneThread = "STEAL",
     .label = "Buy Wang Cars"},
    {.mission = 67, .counter = 544, .value = 0, .marker = 556},
    {.mission = 68, .counter = 544, .value = 1, .marker = 556},
    {.mission = 69, .counter = 544, .value = 2, .marker = 556},
    {.mission = 70, .counter = 544, .value = 3, .marker = 556},
    {.mission = 76, .counter = 593, .value = 1, .marker = 601, .need = {1400, 1, "Toreno's phone call"}},
    {.mission = 77, .counter = 593, .value = 2, .marker = 601, .need = {1402, 1, "Toreno's phone call"}},
    {.mission = 78, .counter = 593, .value = 3, .marker = 601},
    {.mission = kBuy, .counter = 593, .value = 4, .fixed = {426.497f, 2530.689f, 16.100f}, .money = 80000,
     .doneThread = "PSCH", .label = "Buy the airstrip"},
    {.mission = 83, .counter = 593, .value = 4, .marker = 666},
    {.mission = 79, .counter = 593, .value = 5, .marker = 604},
    {.mission = 80, .counter = 593, .value = 6, .marker = 604},
    {.mission = 81, .counter = 593, .value = 7, .marker = 604, .hour = 22},  // 20:00 to 6:00
    {.mission = 82, .counter = 593, .value = 8, .marker = 604, .need = {1389, 1, "Toreno's phone call"}},
    // Las Venturas (Woozie's office and the heist room: interior 10, Caligula's: interior 1)
    {.mission = 84, .counter = 597, .value = 0, .marker = 607, .need = {1403, 1, "Woozie's phone call"}, .area = 10},
    {.mission = 85, .counter = 597, .value = 1, .marker = 607, .area = 10},
    {.mission = 86, .counter = 597, .value = 2, .marker = 607, .area = 10},
    {.mission = 88, .counter = 597, .value = 3, .fixed = {2026.603f, 1007.735f, 9.813f}},
    {.mission = 89, .counter = 597, .value = 4, .marker = 619, .need = {1380, 1, "Salvatore's phone call"}, .area = 1},
    {.mission = 90, .counter = 597, .value = 5, .marker = 619, .area = 1},
    {.mission = 87, .counter = 597, .value = 6, .marker = 607, .area = 10},
    {.mission = 91, .counter = 597, .value = 7, .marker = 619, .need = {1381, 1, "Salvatore's phone call"}, .area = 1},
    {.mission = 93, .counter = 598, .value = 0, .marker = 613},
    {.mission = 94, .counter = 598, .value = 1, .fixed = {-378.750f, 2235.850f, 41.420f},
     .need = {1378, 1, "Tenpenny's phone call"}},
    {.mission = 95, .counter = 599, .value = 0, .marker = 616},
    {.mission = 92, .counter = 597, .value = 8, .marker = 619, .need = {1382, 1, "Salvatore's phone call"}, .area = 1},
    {.mission = 96, .counter = 600, .value = 0, .marker = 610, .area = 10},
    {.mission = 97, .counter = 600, .value = 1, .marker = 610, .area = 10},
    {.mission = 98, .counter = 600, .value = 2, .marker = 610, .area = 10},
    {.mission = 99, .counter = 600, .value = 3, .marker = 610, .area = 10},
    {.mission = 100, .counter = 600, .value = 4, .marker = 610, .hour = 22, .area = 10},  // 20:00 to 6:00
    {.mission = 101, .counter = 600, .value = 5, .marker = 610, .need = {37, 1, "Millie's keycard"},
     .need2 = {1384, 1, "Woozie's phone call about the heist"}, .area = 10, .prep = Prep::Keycard},
    // Back to Los Santos
    {.mission = 102, .counter = 626, .value = 0, .marker = 607, .area = 10},
    {.mission = 103, .counter = 626, .value = 1, .marker = 633},
    {.mission = 104, .counter = 626, .value = 2, .marker = 633},
    {.mission = 105, .counter = 626, .value = 3, .marker = 633},
    {.mission = 106, .counter = 627, .value = 0, .marker = 459},
    {.mission = 107, .counter = 627, .value = 1, .marker = 462},
    {.mission = 108, .counter = 629, .value = 0, .marker = 633},
    {.mission = 109, .counter = 629, .value = 1, .marker = 462, .prep = Prep::Respect},
    {.mission = 110, .counter = 629, .value = 2, .marker = 462, .need = {1355, 1, "Sweet's phone call"},
     .need2 = {1354, 1, "Sweet's phone call about the territory"}, .prep = Prep::Territory,
     .label = "End of the Line"},
    {.mission = 111, .counter = 629, .value = 3, .means = Means::Auto, .label = "End of the Line (part 2)"},
    {.mission = 112, .counter = 629, .value = 4, .means = Means::Auto, .label = "End of the Line (part 3)"},
};

// Where CJ goes when he has to leave an interior (outside its entry-exit, from the IPL enex records).
struct Exterior {
    int area;
    float point[3];
};
constexpr Exterior kExteriors[] = {
    {1, {2193.92f, 1676.42f, 11.37f}},     // Caligula's (MAFCAS)
    {3, {-2029.72f, -122.41f, 34.17f}},    // driving school (DRIVES)
    {6, {2037.54f, 2727.69f, 9.83f}},      // Millie's house (KEYCARD thread)
    {10, {2022.49f, 1004.00f, 9.82f}},     // Four Dragons (TRICAS)
};

enum class Phase { Off, Settle, Prep, Travel, WaitStart, Pass, Play, Save, Load };

Phase g_phase = Phase::Off;
const Step* g_step = nullptr;
std::string g_target;
ULONGLONG g_since = 0;
ULONGLONG g_lastAction = 0;  // last script started or teleport made while waiting
int g_tries = 0;
bool g_waitLogged = false;
bool g_prepStarted = false;
uint8_t* g_playing = nullptr;      // mission thread left to play by itself
std::string g_cutsceneAutoBefore;  // cutscene_skip.auto_skip before the run ("" = not changed)
bool g_resetPending = false;       // a run_to left over from the last session, cleared on the first game tick
bool g_saveRequested = false;      // Phase::Save: SaveGame called, waiting for SAVE_COMPLETED
std::vector<std::string> g_saveQueue;  // Phase::Save: saves still to make here (story point, then save_to)
bool g_saveContinues = false;      // after the saves: on to the next step (a story point on the way)
std::string g_slotSaved;           // the save_to slot written at the target
const Step* g_savedStep = nullptr; // the story point saved last in this run
int g_lastArmed = -1;              // loop guard: the same mission armed again and again
int g_armedRepeats = 0;
ULONGLONG g_armedRepeatSince = 0;
ULONGLONG g_watchThreadsUntil = 0;  // diagnostics: script threads watched after an interior switch
int g_threadPeak = 0;
int g_keyHeld = 0;                 // a key posted to the game window; released after a few game ticks
int g_keyTicks = 0;

// Script byte code for RunScript (main.scm encoding: opcode, then typed parameters).
class Code {
public:
    Code& Op(uint16_t op) { return U16(op); }
    Code& Int8(int value) { return Byte(0x04).Byte(static_cast<uint8_t>(value)); }
    Code& Int16(int value) { return Byte(0x05).U16(static_cast<uint16_t>(value)); }
    Code& Int32(int32_t value) { return Byte(0x01).Raw(&value, 4); }
    Code& Float(float value) { return Byte(0x06).Raw(&value, 4); }
    Code& Global(int n) { return Byte(0x02).U16(static_cast<uint16_t>(n * 4)); }
    Code& Local(int n) { return Byte(0x03).U16(static_cast<uint16_t>(n)); }
    Code& Text8(const char* text) {
        char buffer[8] = {};
        std::memcpy(buffer, text, std::min<size_t>(std::strlen(text), 8));
        return Byte(0x09).Raw(buffer, 8);
    }
    Code& Point(const float p[3]) { return Float(p[0]).Float(p[1]).Float(p[2]); }
    bool Run() {
        Op(0x004E);  // TERMINATE_THIS_SCRIPT
        return RunScript(bytes_.data(), bytes_.size());
    }

private:
    Code& Byte(uint8_t value) {
        bytes_.push_back(value);
        return *this;
    }
    Code& U16(uint16_t value) { return Byte(value & 0xFF).Byte(value >> 8); }
    Code& Raw(const void* data, size_t size) {
        const auto* p = static_cast<const uint8_t*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
        return *this;
    }
    std::vector<uint8_t> bytes_;
};

constexpr int kPlayer = 2;  // $2: the player, $3: CJ
constexpr int kChar = 3;
constexpr int kThreadsSettled = 60;  // script threads in use before the runner moves on (the game has 96)
constexpr int kVoodoo = 412;  // a lowrider (handling flag LOW_RIDER) of the poor family class

const char* TitleOf(const Step& step) {
    const MissionInfo* mission = ByIndex(step.mission);
    return mission ? mission->title : "?";
}

std::string StepName(const Step& step) {
    return step.label ? step.label : TitleOf(step);
}

// A step the player can choose to stop before: CJ walks into its marker himself.
bool IsTarget(const Step& step) {
    return step.means != Means::Auto && step.mission != kBuy;
}

// Number of a target in the run_to list (1 = Big Smoke, last = Story complete): its story save is story<N>.
int TargetNumber(const std::string& name) {
    int number = 0;
    for (const Step& step : kSteps) {
        if (IsTarget(step)) {
            ++number;
            if (StepName(step) == name) {
                return number;
            }
        }
    }
    return name == kComplete ? number + 1 : 0;
}

const Step* TargetStep(const std::string& name) {
    for (const Step& step : kSteps) {
        if (IsTarget(step) && StepName(step) == name) {
            return &step;
        }
    }
    return nullptr;
}

std::string StorySave(const std::string& name) {
    return "story" + std::to_string(TargetNumber(name));
}

bool Done(const Step& step) {
    return GlobalInt(step.counter) > step.value || (step.doneThread && ScriptRunning(step.doneThread));
}

const Step* NextStep() {
    for (const Step& step : kSteps) {
        if (!Done(step)) {
            return &step;
        }
    }
    return nullptr;
}

bool IsStepMission(int index) {
    return std::any_of(std::begin(kSteps), std::end(kSteps), [index](const Step& s) { return s.mission == index; });
}

void Marker(const Step& step, float out[3]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = step.marker ? GlobalFloat(step.marker + i) : step.fixed[i];
    }
}

int PlayerArea() {
    const uint8_t* ped = Game().PlayerPed();
    return ped ? ped[0x2F] : 0;  // CEntity::m_nAreaCode
}

bool Teleport(const float p[3]) {
    using SetCharCoordinates = void(__thiscall*)(void*, void*, float, float, float, bool, bool);
    const auto set = Host().GameFunction<SetCharCoordinates>("CRunningScript::SetCharCoordinates");
    uint8_t* ped = Game().PlayerPed();
    if (!set || !ped) {
        return false;
    }
    set(nullptr, ped, p[0], p[1], p[2], true, true);  // what SET_CHAR_COORDINATES does: ground, space, group
    return true;
}

// Into or out of an interior: the visible area, the scene around the point, then CJ (what the KEYCARD thread does).
bool AreaTeleport(int area, const float p[3]) {
    g_watchThreadsUntil = GetTickCount64() + 4000;
    g_threadPeak = 0;
    return Code()
        .Op(0x04BB).Int8(area)                    // SET_AREA_VISIBLE
        .Op(0x0860).Global(kChar).Int8(area)      // SET_CHAR_AREA_VISIBLE $3
        .Op(0x03CB).Point(p)                      // LOAD_SCENE
        .Op(0x00A1).Global(kChar).Point(p)        // SET_CHAR_COORDINATES $3
        .Run();
}

// Moves CJ to a point, switching the interior when needed.
// Inside an interior's door (the entry-exit's target point, where walking in puts CJ).
struct Entry {
    int area;
    float point[3];
};
constexpr Entry kEntries[] = {
    {1, {2233.91f, 1710.73f, 1010.18f}},   // Caligula's (MAFCAS)
    {3, {-2029.72f, -118.07f, 1034.17f}},  // driving school (DRIVES)
    {10, {2015.45f, 1017.09f, 995.88f}},   // Four Dragons (TRICAS)
};

// Moves CJ to a point. Into another interior through its door first: every interior switch restarts the object brains
// around CJ, and in the middle of a casino floor that is some 55 script threads at once (the game has 96 and crashes
// when none is free); at the door far fewer. The caller moves CJ on once the threads have ended.
bool MoveTo(int area, const float p[3]) {
    if (PlayerArea() == area) {
        return Teleport(p);
    }
    for (const Entry& e : kEntries) {
        if (e.area == area) {
            return AreaTeleport(area, e.point);
        }
    }
    return AreaTeleport(area, p);
}

void SetHour(int hour) {
    auto* hours = Host().GameVariable<uint8_t>("CClock::ms_nGameClockHours");
    auto* minutes = Host().GameVariable<uint8_t>("CClock::ms_nGameClockMinutes");
    if (hours && minutes && *hours != hour) {
        Host().Info("story: clock {:02}:{:02} -> {:02}:00 for the mission's time window", *hours, *minutes, hour);
        *hours = static_cast<uint8_t>(hour);
        *minutes = 0;
    }
}

// cutscene_skip.auto_skip, read from the live settings list ("" when that mod does not run).
std::string CutsceneAuto() {
    std::istringstream lines(Host().SettingsList());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with("cutscene_skip\tauto_skip\t")) {
            std::istringstream fields(line);
            std::string mod, key, type, value;
            std::getline(fields, mod, '\t');
            std::getline(fields, key, '\t');
            std::getline(fields, type, '\t');
            std::getline(fields, value, '\t');
            return value;
        }
    }
    return {};
}

void Stop(const std::string& text) {
    g_phase = Phase::Off;
    g_step = nullptr;
    g_playing = nullptr;
    if (!g_cutsceneAutoBefore.empty()) {
        Host().SetSetting("cutscene_skip", "auto_skip", g_cutsceneAutoBefore);
        g_cutsceneAutoBefore.clear();
    }
    Message(text);
    if (Host().ConfigString("run_to") != "off") {
        Host().SetSetting(std::string(Host().Id()), "run_to", "off");
    }
}

bool Done(const Step& step);
const Step* NextStep();

void Begin(const std::string& target) {
    g_target = target;
    g_saveRequested = false;
    g_saveQueue.clear();
    g_slotSaved.clear();
    g_savedStep = nullptr;
    g_phase = Phase::Settle;
    g_since = GetTickCount64();
    // A game already past the target can only get there from its story save; a fresh one (main menu, or no mission
    // passed yet) loses nothing by loading it. Otherwise the runner plays on from this game and keeps its progress.
    const int state = Host().GameState();
    const Step* step = TargetStep(target);
    const bool past = state == 9 && (step ? Done(*step) : NextStep() == nullptr);
    const bool fresh = state == 7 || (state == 9 && Game().StatInt(147) == 0);
    if ((past || fresh) && Host().ConfigBool("load_story_saves", true) && TargetNumber(target) > 0 &&
        Host().LoadGame(StorySave(target))) {
        g_phase = Phase::Load;
        Host().Info("story: loading {} for '{}' ({})", StorySave(target), target, past ? "this game is past it" : "fresh game");
        Message("Story runner: loading the story save for " + target + ".");
        return;
    }
    if (past) {
        g_phase = Phase::Off;
        Message("Story runner stopped: this game is already past " + target + ", and there is no story save for it yet.");
        Host().SetSetting(std::string(Host().Id()), "run_to", "off");
        return;
    }
    const std::string automatic = CutsceneAuto();
    if (!automatic.empty() && automatic != "true" && Host().SetSetting("cutscene_skip", "auto_skip", "true")) {
        g_cutsceneAutoBefore = automatic;  // restored when the run ends
    }
    Host().Info("story: run to '{}' started (cutscenes skipped and phone calls answered automatically)", target);
    Message("Story runner: playing through to " + target + ". Press nothing, this takes a while.");
}

// The phone rings ($15 call running, $1409 rings so far): answer it the way the call handler does when the answer
// button is pressed (main+0x2F38B: $1414 = 1). Not during missions (the handler itself sets $ONMISSION).
void AnswerPhone() {
    if (GlobalInt(15) == 1 && GlobalInt(1409) > 0 && GlobalInt(1414) == 0 && GlobalInt(1417) == 0 &&
        MissionScript() == nullptr) {
        GlobalInt(1414) = 1;
        Host().Info("story: answered phone call {}", GlobalInt(1412));
    }
}

bool NeedMet(const Need& need) {
    return need.global == 0 || GlobalInt(need.global) >= need.atLeast;
}

const Need* OpenNeed(const Step& step) {
    return !NeedMet(step.need) ? &step.need : !NeedMet(step.need2) ? &step.need2 : nullptr;
}

// Takes CJ off a vehicle next to the given point with the game's own WARP_CHAR_FROM_CAR_TO_COORD (some missions end
// with CJ on a bike or in a car; triggers and phone calls want him on foot).
void WarpOffVehicle(const float p[3]) {
    const bool started = Code().Op(0x0362).Global(kChar).Point(p).Run();
    Host().Info("story: CJ is in a vehicle: {}", started ? "warping him off" : "cannot warp him off");
    if (!started) {
        Stop("Story runner stopped: leave the vehicle and start the runner again.");
    }
}

// Spawns a Voodoo at the point and puts CJ in.
bool SpawnLowrider(const float p[3]) {
    return Code()
        .Op(0x0247).Int16(kVoodoo)                             // REQUEST_MODEL
        .Op(0x038B)                                            // LOAD_ALL_MODELS_NOW
        .Op(0x00A5).Int16(kVoodoo).Point(p).Local(0)           // CREATE_CAR 0@
        .Op(0x036A).Global(kChar).Local(0)                     // WARP_CHAR_INTO_CAR $3 0@
        .Op(0x0249).Int16(kVoodoo)                             // MARK_MODEL_AS_NO_LONGER_NEEDED
        .Op(0x01C3).Local(0)                                   // MARK_CAR_AS_NO_LONGER_NEEDED 0@
        .Run();
}

// Presses a key in the game window for a few frames, as the keyboard does (the game samples keys once per frame).
void PressGameKey(int vk) {
    auto* raw = Host().Raw();
    const auto window = static_cast<HWND>(raw->game_window(raw));
    if (window && g_keyHeld == 0 && PostMessageW(window, WM_KEYDOWN, static_cast<WPARAM>(vk), 1)) {
        g_keyHeld = vk;
        g_keyTicks = 0;
    }
}

void ReleaseGameKey() {
    if (g_keyHeld != 0 && ++g_keyTicks >= 4) {
        auto* raw = Host().Raw();
        PostMessageW(static_cast<HWND>(raw->game_window(raw)), WM_KEYUP, static_cast<WPARAM>(g_keyHeld), 0xC0000001);
        g_keyHeld = 0;
    }
}

float Respect() {
    const float* stats = Host().GameVariable<float>("CStats::StatTypesFloat");
    return stats ? stats[64] : 0.0f;
}

bool Throttle(ULONGLONG intervalMs) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastAction < intervalMs) {
        return false;
    }
    g_lastAction = now;
    return true;
}

// Phone calls need CJ outside (area 0), on foot and without a wanted level (main+0x1849F).
void ReadyForCalls(const char* what, const float point[3]) {
    if (Game().InVehicle()) {
        if (Throttle(3000)) {
            WarpOffVehicle(point);
        }
        return;
    }
    if (const int area = PlayerArea(); area != 0 && Throttle(5000)) {
        for (const Exterior& e : kExteriors) {
            if (e.area == area) {
                Host().Info("story: CJ leaves interior {} to wait for {}", area, what);
                AreaTeleport(0, e.point);
            }
        }
    }
    if (Game().WantedLevel() > 0 && Throttle(5000)) {
        Code().Op(0x0110).Global(kPlayer).Run();  // CLEAR_WANTED_LEVEL $2
    }
}

// Prep actions before the step's travel. True when done.
bool Prepare(const Step& step) {
    switch (step.prep) {
        case Prep::None:
            return true;
        case Prep::SaveTutorial: {
            // After Big Smoke the main script asks CJ to enter the house ($65 = 1), notes it ($65 = 2) and puts up
            // Ryder's marker once CJ is outside near the house again ($65 = 3).
            const int state = GlobalInt(65);
            if (state >= 3) {
                return true;
            }
            if (state == 1) {
                GlobalInt(65) = 2;
                Host().Info("story: save tutorial marked as seen ($65 = 2)");
            }
            if (state >= 1 && !g_prepStarted) {
                g_prepStarted = true;
                const float house[3] = {2492.0f, -1677.0f, 13.3f};  // within the main script's 20 m box
                Teleport(house);
            }
            return false;
        }
        case Prep::Respect:
            // FIND_MAX_NUMBER_OF_GROUP_MEMBERS returns 0 below respect 10; in story order it is far above that.
            if (!g_prepStarted) {
                g_prepStarted = true;
                if (Respect() < 10.0f) {
                    Host().Info("story: respect {:.1f} < 10: AWARD_PLAYER_MISSION_RESPECT 40", Respect());
                    Code().Op(0x0998).Int8(40).Run();
                    return false;
                }
            }
            return GetTickCount64() - g_since > 2000 || Respect() >= 10.0f;
        case Prep::Territory: {
            // GET_TERRITORY_UNDER_CONTROL_PERCENTAGE > 34: Grove owns a zone when its density is at least Ballas plus
            // Vagos. 21 of the 53 gang zones of Los Santos (story_runner.md 5.4), what winning their gang wars gives.
            if (!g_prepStarted) {
                g_prepStarted = true;
                static constexpr const char* kZones[] = {"GAN1",  "GAN2",  "IWD1", "IWD2", "IWD3A", "IWD3B", "IWD4",
                                                         "IWD5",  "GLN1",  "GLN2A", "JEF1A", "JEF1B", "JEF2", "JEF3B",
                                                         "JEF3C", "ELS1A", "ELS1B", "ELS2", "ELS3A", "ELS3B", "ELS4"};
                Code code;
                for (const char* zone : kZones) {
                    code.Op(0x076C).Text8(zone).Int8(0).Int8(0);   // SET_ZONE_GANG_STRENGTH Ballas 0
                    code.Op(0x076C).Text8(zone).Int8(1).Int8(40);  // Grove 40
                    code.Op(0x076C).Text8(zone).Int8(2).Int8(0);   // Vagos 0
                }
                Host().Info("story: territory: Grove Street takes {} zones: {}", std::size(kZones),
                            code.Run() ? "done" : "FAILED");
            }
            return true;
        }
        case Prep::Keycard: {
            // Millie's keycard: MOB_VEG call 53 (Millie progress, stat 257, >= 35) creates the pickup in her house
            // and starts KEYCARD, which takes CJ in and out through the door markers and sets $37 on pickup.
            static constexpr float kDoor[3] = {2037.349f, 2722.071f, 9.828f};
            static constexpr float kPickup[3] = {348.786f, 306.105f, 998.656f};
            static constexpr float kExit[3] = {343.997f, 305.104f, 998.156f};
            const int area = PlayerArea();
            if (GlobalInt(37) >= 1) {
                if (area == 0) {
                    return true;
                }
                if (Throttle(5000)) {
                    Teleport(kExit);
                }
                return false;
            }
            if (!g_prepStarted) {
                g_prepStarted = true;
                if (Game().StatInt(257) < 35) {
                    Host().Info("story: Millie's progress {} -> 35 for the keycard call", Game().StatInt(257));
                    Code().Op(0x0629).Int16(257).Int8(35).Run();  // SET_INT_STAT
                }
            }
            if (GlobalInt(1379) < 1) {
                ReadyForCalls("the keycard call", kDoor);
                return false;  // waiting for the call (answered automatically)
            }
            if (Game().FreeRoam() && Throttle(5000)) {
                Teleport(area == 6 ? kPickup : kDoor);
            }
            return false;
        }
    }
    return true;
}

void Travel() {
    const Step& step = *g_step;
    float marker[3];
    Marker(step, marker);
    const Need* open = OpenNeed(step);
    // The lowrider spawned on the first try stays; any other vehicle is left first.
    const bool keepVehicle =
        step.means == Means::Any || step.means == Means::Auto || (step.means == Means::Car && g_tries > 0);
    if (open) {
        if (!g_waitLogged) {
            g_waitLogged = true;
            Host().Info("story: '{}' waits for {} (${} = {}, needs {})", StepName(step), open->what, open->global,
                        GlobalInt(open->global), open->atLeast);
        }
        ReadyForCalls(open->what, marker);
        if (GetTickCount64() - g_since > 300000) {
            Stop(std::string("Story runner stopped before ") + StepName(step) + ": waiting for " + open->what + ".");
        }
        return;
    }
    const bool target = StepName(step) == g_target;
    const bool storyPoint = Host().ConfigBool("save_each", true) && IsTarget(step) && g_savedStep != &step;
    if (target || storyPoint) {
        // The target, or a story point on the way to it (save_each): its preparation and phone calls are done, so
        // its marker is up. CJ stops beside it; the story point is saved as story<N>, the target also to save_to.
        // Beside it, outside the trigger's radius (on foot 1.2 m, in a car 4 m).
        const float beside[3] = {marker[0] + (step.means == Means::Car ? 8.0f : 3.0f), marker[1], marker[2]};
        if (step.hour >= 0) {
            SetHour(step.hour);
        }
        const Exterior* door = nullptr;
        for (const Exterior& e : kExteriors) {
            door = e.area == step.area ? &e : door;
        }
        if (door) {
            // Saves are refused inside interiors: CJ waits outside the building's door and walks in himself.
            if (Game().InVehicle()) {
                WarpOffVehicle(door->point);
            } else {
                MoveTo(0, door->point);
            }
        } else if (step.means == Means::Car && !Game().InVehicle() && PlayerArea() == 0) {
            SpawnLowrider(beside);  // the trigger wants a lowrider: CJ waits in one
        } else if (Game().InVehicle() && step.means != Means::Car) {
            WarpOffVehicle(beside);
        } else {
            MoveTo(step.area, beside);
        }
        Host().Info("story: stopped next to the {} of '{}'", door ? "building" : "marker", StepName(step));
        g_savedStep = &step;
        g_saveQueue.clear();
        if (storyPoint) {
            g_saveQueue.push_back(StorySave(StepName(step)));
        }
        if (target && Host().ConfigString("save_to") != "off") {
            g_saveQueue.push_back(Host().ConfigString("save_to"));
        }
        g_saveContinues = !target;
        if (!g_saveQueue.empty()) {
            g_phase = Phase::Save;  // the saves are made once the game has settled after the teleport
            g_since = GetTickCount64();
            g_saveRequested = false;
            return;
        }
        Stop("Ready for " + g_target + ": walk into the marker. Save first if you like.");
        return;
    }
    if (Game().InVehicle() && !keepVehicle) {
        if (Throttle(3000)) {
            WarpOffVehicle(marker);
        }
        return;
    }
    if (int threads = 0, ours = 0; ScriptCounts(threads, ours), threads > kThreadsSettled) {
        return;  // object brains of the last interior switch still running
    }
    const bool viaDoor = step.area != 0 && PlayerArea() != step.area;
    g_phase = Phase::WaitStart;
    g_since = GetTickCount64();
    if (viaDoor) {
        g_since -= 6000;  // only through the door: the next try (2 s) moves CJ to the marker inside
        --g_tries;
    }
    if (step.means == Means::Auto) {
        Host().Info("story: waiting for '{}' to start by itself", StepName(step));
        return;
    }
    if (step.hour >= 0) {
        SetHour(step.hour);
    }
    if (step.money > 0) {
        if (const auto player = Host().Player(); player && player->money < step.money) {
            Host().Info("story: money {} -> {} for the purchase", player->money, step.money);
            Host().SetMoney(step.money);
        }
    }
    bool moved;
    if (step.means == Means::Car) {
        moved = Game().InVehicle() ? Teleport(marker) : PlayerArea() == 0 && SpawnLowrider(marker);
        if (PlayerArea() != 0) {
            AreaTeleport(0, marker);  // the car is spawned on the next try, once CJ is outside
        }
    } else {
        moved = MoveTo(step.area, marker);
    }
    if (!moved && PlayerArea() == step.area) {
        Stop("Story runner stopped: teleport is not available on this build.");
        return;
    }
    Host().Info("story: at the marker of '{}' ({:.1f}, {:.1f}, {:.1f}) area {}, counter ${} = {}", StepName(step),
                marker[0], marker[1], marker[2], step.area, step.counter, GlobalInt(step.counter));
}

// How the runner passes a mission: researched entries for missions without a pass chunk,
// the race skip for races, and cutscene-only stages left to play.
bool PlanFor(int index, PassPlan& plan) {
    auto entry = [&plan](int at, std::array<uint8_t, 8> bytes) {
        plan.mode = PassMode::Entry;
        plan.entry = at;
        plan.bytes = bytes;
    };
    // CJ off the contact's marker (12 m east): its trigger runs again in the step that launches the next mission.
    auto offMarker = [&plan](float x, float y) {
        plan.moveFirst = true;
        plan.moveTo[0] = x + 12.0f;
        plan.moveTo[1] = y;
    };
    switch (index) {
        case 35:  // CPRACE: Rockstar's S branch puts CJ on each checkpoint; state 13 registers the win
        case 72:  // Air Raid: the pass code's end scene turns CJ on the mounted gun the mission sets up first
            plan.mode = PassMode::DebugSkip;
            return true;
        case 36:  // the lowrider meeting's normal end: $2336 = 1, RETURN (CESAR then launches the race)
            entry(4999, {0x04, 0x00, 0x02, 0x80, 0x24, 0x04, 0x01, 0x51});
            offMarker(GlobalFloat(480), GlobalFloat(481));
            return true;
        case 40:  // CATALIN: $718 = job, RETURN; the lowest job not done yet ($714 + job == 0)
            for (int job = 0; job < 4; ++job) {
                if (GlobalInt(714 + job) == 0) {
                    entry(6439 + 141 * job, {0x04, 0x00, 0x02, 0x38, 0x0B, 0x04, static_cast<uint8_t>(job), 0x51});
                    const float* p = job == 0 ? kCatalinaFirst : kCatalina;
                    offMarker(p[0], p[1]);
                    return true;
                }
            }
            return false;
        case 45:  // CATCUT after the trailer cutscene: $2163 = 1, Catalina's blip, RETURN
            entry(813, {0x64, 0x01, 0x02, 0x90, 0x07, 0x10, 0x01, 0x02});
            return true;
        case 71:  // the driving school: its lessons can be repeated, so the school relaunches while CJ is inside
            plan.moveFirst = true;
            plan.moveTo[0] = -2029.7f;  // outside the school's door (enex DRIVES)
            plan.moveTo[1] = -127.0f;
            plan.moveArea = 0;
            return true;
        case 74:  // New Model Army: the pass entry is the cleanup, which registers the pass when 36@ == 1 (tank won)
            plan.local = 36;
            plan.localValue = 1;
            return true;
        case 59:  // SYN2: first part (garage) or the pass chunk (club), chosen by the mission itself
            entry(31204, {0xD6, 0x00, 0x04, 0x00, 0x38, 0x00, 0x02, 0xC4});
            return true;
        case 110:  // FINALEA: Rockstar's S target ($629 += 1)
            entry(32590, {0x06, 0x00, 0x03, 0xC9, 0x00, 0x04, 0x01, 0x08});
            offMarker(GlobalFloat(462), GlobalFloat(463));  // RIOT also launches the next part from Sweet's marker
            return true;
        case 111:  // FINALEB: Rockstar's S target ($629 += 1)
            entry(18535, {0x06, 0x00, 0x03, 0x9C, 0x00, 0x04, 0x01, 0x10});
            offMarker(GlobalFloat(462), GlobalFloat(463));
            return true;
        default:
            return true;
    }
}

// Cutscene-only missions without a pass check: Wu Zi Mu's stages (48) and the property purchase scene (134).
bool PlaysItself(int index) {
    return index == 48 || index == kBuy;
}

}  // namespace

namespace {

// One save of the queue is done (or refused): the next one, back to the run, or the end at the target.
void SaveFinished(bool success) {
    const std::string name = g_saveQueue.empty() ? std::string{} : g_saveQueue.front();
    if (!g_saveQueue.empty()) {
        g_saveQueue.erase(g_saveQueue.begin());
    }
    g_saveRequested = false;
    const bool story = name.starts_with("story");
    Host().Info("story: save of the story point '{}' to {}: {}", g_savedStep ? StepName(*g_savedStep) : g_target, name,
                success ? "done" : "FAILED");
    if (!success && !story) {
        Stop("Story runner stopped: ready for " + g_target + ", but saving to " + name + " FAILED (see the SACore log).");
        return;
    }
    if (success && !story) {
        g_slotSaved = name;
    }
    g_since = GetTickCount64() - 2000;  // the next save after 1 s
    if (!g_saveQueue.empty()) {
        return;
    }
    if (g_saveContinues) {
        g_phase = Phase::Travel;
        g_since = GetTickCount64();
        return;
    }
    if (!g_slotSaved.empty()) {
        Stop(g_target == kComplete ? "Ready for " + g_target + ": saved to " + g_slotSaved + "."
                                   : "Ready for " + g_target + ": saved to " + g_slotSaved + ". Walk into the marker to start it.");
    } else {
        Stop(g_target == kComplete ? "Ready for " + g_target + ": the story is done."
                                   : "Ready for " + g_target + ": walk into the marker. Save first if you like.");
    }
}

}  // namespace

void StorySaveCompleted(bool success) {
    if (g_phase == Phase::Save && g_saveRequested) {
        SaveFinished(success);
    }
}

std::string StoryTargets() {
    std::string list = "off";
    for (const Step& step : kSteps) {
        if (IsTarget(step)) {
            list += "|" + StepName(step);
        }
    }
    return list + "|" + kComplete;
}

void StoryStart() {
    // mod.toml lists the targets for the console; they must match the step table.
    std::istringstream lines(Host().SettingsList());
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with(std::string(Host().Id()) + "\trun_to\t")) {
            std::istringstream fields(line);
            std::string field;
            for (int i = 0; i < 7 && std::getline(fields, field, '\t'); ++i) {  // 7th field: allowed values
            }
            if (field != StoryTargets()) {
                Host().Warn("story: run_to choices in mod.toml differ from the step table: {}", StoryTargets());
            }
        }
    }
    // The run is a one-shot action: a value left over from a closed game does not start a new run. Settings can only
    // be changed once the mod runs, so the reset waits for the first game tick.
    g_resetPending = Host().ConfigString("run_to") != "off";
}

void StoryConfigChanged() {
    const std::string target = Host().ConfigString("run_to");
    if (target == "off") {
        if (g_phase != Phase::Off) {
            Stop("Story runner stopped.");
        }
        return;
    }
    if (g_phase != Phase::Off) {
        g_target = target;
        return;
    }
    Begin(target);
}

void StoryTick() {
    if (g_resetPending) {
        g_resetPending = false;
        if (g_phase == Phase::Off && Host().ConfigString("run_to") != "off") {
            Host().SetSetting(std::string(Host().Id()), "run_to", "off");
            Host().Info("story: run_to from the last session reset to off");
        }
    }
    if (g_phase == Phase::Off || Host().GameState() != 9) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG s_heartbeat = 0;
    if (now - s_heartbeat > 10000) {
        s_heartbeat = now;
        int threads = 0, ours = 0;
        ScriptCounts(threads, ours);
        Host().Info("story: phase {}, step {}, passing {}, mission {}, free roam {}, area {}, script threads {} ({} runner)",
                    static_cast<int>(g_phase), g_step ? StepName(*g_step) : "-", Passing(), Game().OnMission(),
                    Game().FreeRoam(), PlayerArea(), threads, ours);
    }
    AnswerPhone();
    ReleaseGameKey();
    if (g_watchThreadsUntil != 0) {
        int threads = 0, ours = 0;
        ScriptCounts(threads, ours);
        g_threadPeak = std::max(g_threadPeak, threads);
        if (now > g_watchThreadsUntil) {
            Host().Info("story: script threads after the interior switch: peak {} of 96, area {}", g_threadPeak,
                        PlayerArea());
            g_watchThreadsUntil = 0;
        }
    }
    uint8_t* script = Game().OnMission() ? MissionScript() : nullptr;
    if (script) {
        if (Passing()) {
            return;
        }
        if (g_phase == Phase::Play && script == g_playing) {
            if (now - g_since > 180000) {
                Stop("Story runner stopped: the scene did not end within 3 minutes.");
            }
            return;
        }
        const MissionInfo* mission = Identify(script);
        if (!mission || (mission->index != 2 && !IsStepMission(mission->index))) {
            Stop("Story runner stopped: finish the current mission first, then start it again.");
            return;
        }
        if (g_step && StepName(*g_step) == g_target && mission->index == g_step->mission) {
            Stop("Ready: " + g_target + " has started.");
            return;
        }
        g_since = now;
        if (PlaysItself(mission->index)) {
            Host().Info("story: letting '{}' play (cutscenes only)", mission->title);
            g_playing = script;
            g_phase = Phase::Play;
            return;
        }
        if (mission->index != g_lastArmed || now - g_armedRepeatSince > 30000) {
            g_lastArmed = mission->index;
            g_armedRepeats = 0;
            g_armedRepeatSince = now;
        } else if (++g_armedRepeats >= 3) {
            Stop(std::string("Story runner stopped: ") + mission->title + " starts again after every pass.");
            return;
        }
        PassPlan plan;
        if (!PlanFor(mission->index, plan)) {
            Stop(std::string("Story runner stopped: no way to pass ") + mission->title + " here.");
            return;
        }
        Host().Info("story: passing '{}'{}", mission->title,
                    plan.mode == PassMode::DebugSkip ? " (debug skip)" : plan.mode == PassMode::Entry ? " (entry)" : "");
        Arm(script, mission, plan.mode == PassMode::DebugSkip ? 240000 : 120000, true, plan);
        g_phase = Phase::Pass;
        return;
    }
    if (Passing()) {
        return;
    }
    if (g_phase == Phase::WaitStart) {
        const bool automatic = g_step->means == Means::Auto;
        if (Done(*g_step)) {
            g_phase = Phase::Settle;  // a purchase or a scene finished without a mission thread the runner saw
            g_since = now;
            return;
        }
        if (g_step->money > 0 && Game().FreeRoam() && now - g_since > 1000 && Throttle(2000)) {
            // A for-sale sign sells on the game's "answer phone" control (PROP_3), not by walking in.
            PressGameKey(static_cast<int>(Host().ConfigInt("buy_key", VK_TAB)));
        }
        if (now - g_since < (automatic ? 30000u : 8000u)) {
            return;
        }
        if (!automatic && ++g_tries < 3) {
            g_phase = Phase::Travel;  // the trigger polls every few hundred ms; place CJ again
            return;
        }
        Stop("Story runner stopped: " + StepName(*g_step) + " did not start (counter $" +
             std::to_string(g_step->counter) + " = " + std::to_string(GlobalInt(g_step->counter)) + ").");
        return;
    }
    if (!Game().FreeRoam()) {
        static ULONGLONG s_reported = 0;
        if (now - g_since > 10000 && now - s_reported > 30000) {
            s_reported = now;
            const auto pad = Host().GameFunction<uint8_t*(__cdecl*)(int)>("CPad::GetPad")(0);
            Host().Info("story: waiting for free roam: mission {}, mini game {}, cutscene {}, player state {}, "
                        "controls disabled {:#06x}, phone ${} = {}", Game().OnMission(), Game().MiniGame(),
                        Game().CutsceneRunning(), Game().PlayerState(),
                        pad ? *reinterpret_cast<const uint16_t*>(pad + 0x10E) : 0, 15, GlobalInt(15));
        }
        if (g_phase != Phase::Prep && g_phase != Phase::Travel) {
            g_since = now;  // settle only counts free roam; prep and travel keep their time limits
        }
        return;
    }
    switch (g_phase) {
        case Phase::Pass:
        case Phase::Play:
            g_phase = Phase::Settle;
            g_playing = nullptr;
            g_since = now;
            return;
        case Phase::Settle: {
            if (now - g_since < 3000) {
                return;
            }
            // Object brains (the casinos' slot machines and tables) start a script thread each and end it some
            // seconds after CJ has left. Moving on at once after a mission in and out of Caligula's stacks up both
            // sets and can use up the game's 96 threads (it crashes when none is free): wait for them to end.
            if (int threads = 0, ours = 0; ScriptCounts(threads, ours), threads > kThreadsSettled) {
                static ULONGLONG s_reported = 0;
                if (now - s_reported > 10000) {
                    s_reported = now;
                    Host().Info("story: waiting for script threads to end ({} of 96 running)", threads);
                }
                if (now - g_since < 60000) {
                    return;
                }
            }
            g_step = NextStep();
            g_tries = 0;
            g_waitLogged = false;
            g_prepStarted = false;
            g_since = now;
            if (!g_step) {
                g_saveQueue.clear();
                if (g_target == kComplete && Host().ConfigBool("save_each", true)) {
                    g_saveQueue.push_back(StorySave(kComplete));
                }
                if (g_target == kComplete && Host().ConfigString("save_to") != "off") {
                    g_saveQueue.push_back(Host().ConfigString("save_to"));
                }
                if (!g_saveQueue.empty()) {
                    g_phase = Phase::Save;  // the story's end, back in free roam after the credits
                    g_saveContinues = false;
                    g_saveRequested = false;
                    return;
                }
                Stop(g_target == kComplete ? "Ready for " + g_target + ": the story is done."
                                           : "Story runner: the story is done.");
                return;
            }
            Host().Info("story: next '{}'{}", StepName(*g_step), StepName(*g_step) == g_target ? " (target)" : "");
            g_phase = g_step->prep != Prep::None ? Phase::Prep : Phase::Travel;
            return;
        }
        case Phase::Prep:
            if (Prepare(*g_step)) {
                g_phase = Phase::Travel;
                g_since = now;
            } else if (now - g_since > 300000) {
                Stop("Story runner stopped: the preparation for " + StepName(*g_step) + " did not finish.");
            }
            return;
        case Phase::Save:
            if (now - g_since > 3000 && !g_saveRequested && !g_saveQueue.empty()) {
                g_saveRequested = true;
                if (!Host().SaveGame(g_saveQueue.front())) {
                    SaveFinished(false);  // refused (see the SACore log)
                }
            }
            return;
        case Phase::Load:
            // Back in free roam after loading the story save: CJ stands beside the target's marker.
            if (now - g_since > 4000) {
                const Step* next = NextStep();
                const bool there = g_target == kComplete ? next == nullptr : next && StepName(*next) == g_target;
                Stop(there ? "Ready for " + g_target + ": story save loaded."
                           : "Story runner stopped: the story save for " + g_target + " is at another story point.");
            }
            return;
        case Phase::Travel:
            Travel();
            return;
        default:
            return;
    }
}

}  // namespace mission_pass
