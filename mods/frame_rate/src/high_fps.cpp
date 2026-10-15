// High frame rate fixes. The game was tuned for 30 FPS: many physics and timing steps
// add a fixed amount per frame instead of per time. With k = ts / (50/30) (k = 1 at 30 FPS, 0.5 at 60 FPS) every fix
// below scales such a step back to its per-time value; all of them are the identity at 30 FPS.
// Shapes: "operand swap" points one instruction's constant operand at a variable recomputed right after
// CTimer::Update; "cave" jumps to a short routine and back; "call swap" redirects one call. Every site is a checked
// adapter symbol; everything goes through the core patch manager and is restored when switched off.
#include "high_fps.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Read by the naked routines (plain C names for the inline assembler).
extern "C" {
float g_hfK = 1.0f;          // k for this frame
float g_hfK06 = 0.6f;        // 1 / (50/30), for the live k in the ped push cave
float* g_hfTimeStep = nullptr;
uint32_t g_hfTick = 1;       // 1 on frames where a 30 Hz tick happened (water cannon)
uint32_t g_hfTick30 = 0;     // 30 Hz counter (water cannon ped push rate)
uint32_t g_hfRetDoors = 0, g_hfRetSwim = 0, g_hfRetPush = 0, g_hfRetCannon = 0, g_hfSkipCannon = 0;
uint32_t g_hfSirenHorn = 0, g_hfSirenToggle = 0, g_hfSirenNone = 0, g_hfSirenResult = 0;
}

namespace frame_rate {
namespace {

constexpr float kT30 = 50.0f / 30.0f;
const float kT30Const = kT30;  // operand target for "use the 30 FPS step" sites

sacore::mod::Host* g_host = nullptr;
bool g_us10 = false;
bool g_applied = false;
std::vector<std::string> g_patchIds;
void(__cdecl* g_origTimerUpdate)() = nullptr;

enum class Value {
    PowK,          // pow(base, k)
    TimesK,        // base * k
    DivK,          // base / k
    T30Const,      // pointer to the constant 50/30
    PerTs,         // base * T30 / ts (newsteam sites where the instruction is followed by "* ts")
    BuoyancyNs,    // 4 * T30 / max(ts, 0.7)
    FrictionK,     // the game's wheel friction global * k
    Tick30Counter, // pointer to the 30 Hz counter
};

struct OperandSwap {
    const char* symbol;
    Value value;
    double base;
};

// float operand on 1.0 (dword), double on newsteam where its instruction reads a qword; decided per site from the
// opcode (DC / DD = qword).
constexpr OperandSwap kSwaps[] = {
    {"FPS doors damping ladder", Value::PowK, 0.92},
    {"FPS doors damping 0.97", Value::PowK, 0.97},
    {"FPS doors damping 0.97 other", Value::PowK, 0.97},
    {"FPS doors damping boat", Value::PowK, 0.98},
    {"FPS aim walk step", Value::DivK, 0.07},
    {"FPS dive speed", Value::TimesK, -0.1},
    {"FPS dive surface", Value::TimesK, 0.01},
    {"FPS swim surface clamp", Value::T30Const, 0.1},  // 1.0: fld [ts] -> T30; newsteam: see Resolve
    {"FPS swim pitch damping 1", Value::PowK, 0.95},
    {"FPS swim pitch damping 2", Value::PowK, 0.95},
    {"FPS swim pitch damping 3", Value::PowK, 0.95},
    {"FPS buoyancy cutoff", Value::DivK, 4.0},          // newsteam: BuoyancyNs
    {"FPS skimmer resistance", Value::TimesK, 30.0},
    {"FPS burnout torque", Value::TimesK, -0.002},
    {"FPS wheel friction 1", Value::FrictionK, 0},
    {"FPS wheel friction 2", Value::FrictionK, 0},
    {"FPS wheel friction 3", Value::FrictionK, 0},
    {"FPS wheel friction 4", Value::FrictionK, 0},
    {"FPS wheel friction 5", Value::FrictionK, 0},
    {"FPS heli rotor normal", Value::TimesK, 0.001},
    {"FPS heli rotor rc", Value::TimesK, 0.003},
    {"FPS boat aquaplane", Value::T30Const, 0.5},       // newsteam: PerTs 0.5
    {"FPS climb settle", Value::T30Const, 0},
    {"FPS climb coarse", Value::T30Const, 0},
    {"FPS turn damping x", Value::PowK, 0.99},
    {"FPS turn damping y", Value::PowK, 0.99},
    {"FPS turn damping z", Value::PowK, 0.99},
    {"FPS water cannon push counter", Value::Tick30Counter, 0},
};

struct Slot {
    Value value;
    double base;
    bool isDouble;
    float f = 0;
    double d = 0;
    const float* friction = nullptr;  // FrictionK: the game's global (the original operand)
};
std::vector<Slot*> g_slots;  // stable addresses (operands point at f / d)

void Update() {
    if (!g_hfTimeStep) {
        return;
    }
    const float ts = *g_hfTimeStep;
    const double k = std::clamp(static_cast<double>(ts) / kT30, 0.01, 2.0);
    g_hfK = static_cast<float>(k);
    for (Slot* slot : g_slots) {
        double v = 0;
        switch (slot->value) {
            case Value::PowK: v = std::pow(slot->base, k); break;
            case Value::TimesK: v = slot->base * k; break;
            case Value::DivK: v = slot->base / k; break;
            case Value::PerTs: v = slot->base * kT30 / std::max(0.01, static_cast<double>(ts)); break;
            case Value::BuoyancyNs: v = 4.0 * kT30 / std::max(0.7, static_cast<double>(ts)); break;
            case Value::FrictionK: v = (slot->friction ? *slot->friction : 0.9f) * k; break;
            default: continue;
        }
        slot->f = static_cast<float>(v);
        slot->d = v;
    }
    // 30 Hz tick for the water cannon (sections recycled and peds pushed at the 30 FPS rate).
    static double acc = 0;
    acc = std::min(acc + ts, 2.0 * kT30);
    if (acc >= kT30) {
        acc -= kT30;
        g_hfTick = 1;
        ++g_hfTick30;
    } else {
        g_hfTick = 0;
    }
}

void __cdecl HookedTimerUpdate() {
    g_origTimerUpdate();
    Update();
}

// ---- caves ------------------------------------------------------------------------------------------------------

// Doors: m_fAngle += m_fAngVel * k (1.0: the angular velocity is loaded and added next; newsteam: st0 = angle).
__declspec(naked) void DoorsUs() {
    __asm {
        fld dword ptr [esi + 14h]
        fmul dword ptr [g_hfK]
        mov ecx, ebx
        jmp dword ptr [g_hfRetDoors]
    }
}
__declspec(naked) void DoorsNs() {
    __asm {
        fld dword ptr [esi + 14h]
        fmul dword ptr [g_hfK]
        faddp st(1), st
        mov ecx, edx
        jmp dword ptr [g_hfRetDoors]
    }
}

// Swimming: the target velocity is a per-frame distance; per time it is distance / k.
__declspec(naked) void SwimUs() {
    __asm {
        fld dword ptr [esp + 1Ch]
        fdiv dword ptr [g_hfK]
        fstp dword ptr [esp + 1Ch]
        fld dword ptr [esp + 20h]
        fdiv dword ptr [g_hfK]
        fstp dword ptr [esp + 20h]
        fdiv dword ptr [g_hfK]
        fld st(1)
        fmul dword ptr [eax]
        fld st(2)
        jmp dword ptr [g_hfRetSwim]
    }
}
__declspec(naked) void SwimNs() {
    __asm {
        fld dword ptr [ebp - 14h]
        fdiv dword ptr [g_hfK]
        fstp dword ptr [ebp - 14h]
        fld dword ptr [ebp - 10h]
        fdiv dword ptr [g_hfK]
        fstp dword ptr [ebp - 10h]
        fld dword ptr [ebp - 0Ch]
        fdiv dword ptr [g_hfK]
        fstp dword ptr [ebp - 0Ch]
        fld dword ptr [ebp + 8]
        fld st(0)
        jmp dword ptr [g_hfRetSwim]
    }
}

// Running into a car: the push impulse is per collision step; scale by the live k (collision sub-steps change ts).
__declspec(naked) void PushUs() {
    __asm {
        mov eax, dword ptr [g_hfTimeStep]
        fld dword ptr [eax]
        fmul dword ptr [g_hfK06]
        fld dword ptr [esp + 20h]
        fmul st, st(1)
        fstp dword ptr [esp + 20h]
        fld dword ptr [esp + 24h]
        fmul st, st(1)
        fstp dword ptr [esp + 24h]
        fld dword ptr [esp + 28h]
        fmul st, st(1)
        fstp dword ptr [esp + 28h]
        fstp st(0)
        mov edx, dword ptr [esp + 20h]
        mov eax, dword ptr [esp + 24h]
        jmp dword ptr [g_hfRetPush]
    }
}
__declspec(naked) void PushNs() {
    __asm {
        mov ecx, dword ptr [g_hfTimeStep]
        fld dword ptr [ecx]
        fmul dword ptr [g_hfK06]
        fld dword ptr [ebp - 0Ch]
        fmul st, st(1)
        fstp dword ptr [ebp - 0Ch]
        fld dword ptr [ebp - 8]
        fmul st, st(1)
        fstp dword ptr [ebp - 8]
        fld dword ptr [ebp - 4]
        fmul st, st(1)
        fstp dword ptr [ebp - 4]
        fstp st(0)
        mov ecx, dword ptr [ebp - 54h]
        mov edx, dword ptr [ebp - 50h]
        jmp dword ptr [g_hfRetPush]
    }
}

// Water cannon: recycle a jet section only on 30 Hz ticks.
__declspec(naked) void CannonUs() {
    __asm {
        cmp dword ptr [g_hfTick], 0
        je skip
        movsx eax, word ptr [edi + 4]
        inc eax
        jmp dword ptr [g_hfRetCannon]
    skip:
        jmp dword ptr [g_hfSkipCannon]
    }
}
__declspec(naked) void CannonNs() {
    __asm {
        cmp dword ptr [g_hfTick], 0
        je skip
        movsx eax, word ptr [esi + 4]
        inc eax
        jmp dword ptr [g_hfRetCannon]
    skip:
        jmp dword ptr [g_hfSkipCannon]
    }
}

// Siren: tap the horn within 150 ms toggles the siren, holding honks; time based instead of the 5-frame history.
enum SirenResult : uint32_t { kNone = 0, kHorn = 1, kToggle = 2 };
uint32_t __cdecl SirenDecide() {
    static ULONGLONG pressedAt = 0;
    static bool pressed = false;
    auto* pad = g_host->GameFunction<void*(__cdecl*)(int)>("CPad::GetPad")(0);
    if (!pad) {
        return kNone;
    }
    using PadFn = int16_t(__thiscall*)(void*);
    const bool justDown = (g_host->GameFunction<PadFn>("CPad::HornJustDown")(pad) & 0xFF) != 0;
    const bool held = g_host->GameFunction<PadFn>("CPad::GetHorn")(pad) != 0;
    const ULONGLONG now = GetTickCount64();
    if (justDown) {
        pressedAt = now;
        pressed = true;
    }
    if (held) {
        return now - pressedAt >= 150 ? kHorn : kNone;
    }
    if (pressed) {
        pressed = false;
        if (now - pressedAt < 150) {
            return kToggle;
        }
    }
    return kNone;
}
__declspec(naked) void SirenUs() {
    __asm {
        pushad
        call SirenDecide
        mov dword ptr [g_hfSirenResult], eax
        popad
        cmp dword ptr [g_hfSirenResult], 1
        je horn
        cmp dword ptr [g_hfSirenResult], 2
        je toggle
        jmp dword ptr [g_hfSirenNone]
    horn:
        jmp dword ptr [g_hfSirenHorn]
    toggle:
        jmp dword ptr [g_hfSirenToggle]
    }
}
__declspec(naked) void SirenNs() {
    __asm {
        pushad
        call SirenDecide
        mov dword ptr [g_hfSirenResult], eax
        popad
        cmp dword ptr [g_hfSirenResult], 1
        je horn
        cmp dword ptr [g_hfSirenResult], 2
        je toggle
        jmp dword ptr [g_hfSirenNone]
    horn:
        jmp dword ptr [g_hfSirenHorn]
    toggle:
        push edi  // the toggle path's exits pop it (newsteam pushes it only after this site)
        jmp dword ptr [g_hfSirenToggle]
    }
}

// ---- call swaps ---------------------------------------------------------------------------------------------------

uint32_t g_rng = 0x9E3779B9u;
bool EmitThisFrame() {  // probability min(1, k), private xorshift (keeps the game's rand() sequence)
    if (g_hfK >= 1.0f) {
        return true;
    }
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (g_rng & 0xFFFFFF) < static_cast<uint32_t>(g_hfK * 16777216.0f);
}

using AddParticleFn = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                        uint32_t);
using CreateFxFn = void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
AddParticleFn g_addParticle = nullptr;
CreateFxFn g_createFx = nullptr;

void __fastcall AddParticleWrap(void* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                                uint32_t f, uint32_t g, uint32_t h) {
    if (EmitThisFrame()) {
        g_addParticle(self, a, b, c, d, e, f, g, h);
    }
}

void* __fastcall CreateFxWrap(void* self, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return EmitThisFrame() ? g_createFx(self, a, b, c, d) : nullptr;
}

// AI autopilot: round to nearest instead of truncating (the truncation error added up twice as fast at 60 FPS).
__declspec(naked) void RoundToInt() {
    __asm {
        sub esp, 4
        fistp dword ptr [esp]
        pop eax
        cdq
        ret
    }
}

// ---- applying -----------------------------------------------------------------------------------------------------

bool Write(const std::string& id, const char* target, uintptr_t address, const void* bytes, size_t size) {
    if (!g_host->WriteMemory(id.c_str(), target, "high frame rate fix", address, bytes, size)) {
        return false;
    }
    g_patchIds.push_back(id);
    return true;
}

int g_applied_count = 0, g_skipped = 0;

void Swap(const OperandSwap& swap) {
    const uintptr_t site = g_host->Symbol(swap.symbol);
    if (!site) {
        // newsteam merges the second 0.97 door damping and the y/z turn damping into the x site (one constant).
        const bool merged = !g_us10 && (std::strcmp(swap.symbol, "FPS doors damping 0.97 other") == 0 ||
                                        std::strcmp(swap.symbol, "FPS turn damping y") == 0 ||
                                        std::strcmp(swap.symbol, "FPS turn damping z") == 0);
        g_skipped += merged ? 0 : 1;
        return;
    }
    const auto* code = reinterpret_cast<const uint8_t*>(site);
    const size_t operandAt = code[0] == 0xA1 ? 1 : 2;
    const bool isDouble = code[0] == 0xDC || code[0] == 0xDD;
    auto* slot = new Slot{swap.value, swap.base, isDouble};
    if (swap.value == Value::FrictionK) {
        slot->friction = *reinterpret_cast<const float* const*>(site + operandAt);
    }
    // newsteam folds some expressions differently (swim surface clamp, buoyancy cutoff, boat aquaplane).
    if (!g_us10) {
        if (std::strcmp(swap.symbol, "FPS swim surface clamp") == 0) {
            slot->value = Value::PerTs;
        } else if (std::strcmp(swap.symbol, "FPS buoyancy cutoff") == 0) {
            slot->value = Value::BuoyancyNs;
        } else if (std::strcmp(swap.symbol, "FPS boat aquaplane") == 0) {
            slot->value = Value::PerTs;
        }
    }
    const void* target = nullptr;
    if (slot->value == Value::T30Const) {
        target = &kT30Const;
    } else if (slot->value == Value::Tick30Counter) {
        target = &g_hfTick30;
    } else {
        target = isDouble ? static_cast<const void*>(&slot->d) : static_cast<const void*>(&slot->f);
    }
    g_slots.push_back(slot);
    const auto pointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    if (Write(std::string("hf_") + swap.symbol, swap.symbol, site + operandAt, &pointer, 4)) {
        ++g_applied_count;
    }
}

bool Jump(const char* symbol, void (*cave)(), size_t covered, uint32_t* returnTo) {
    const uintptr_t site = g_host->Symbol(symbol);
    if (!site) {
        ++g_skipped;
        return false;
    }
    if (returnTo) {
        *returnTo = static_cast<uint32_t>(site + covered);
    }
    uint8_t code[16];
    std::memset(code, 0x90, sizeof(code));
    code[0] = 0xE9;
    const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(cave) - (site + 5));
    std::memcpy(code + 1, &rel, 4);
    const bool ok = Write(std::string("hf_") + symbol, symbol, site, code, std::max<size_t>(covered, 5));
    g_applied_count += ok ? 1 : 0;
    return ok;
}

template <class Fn>
bool CallSwap(const char* symbol, void* wrapper, Fn* original) {
    const uintptr_t site = g_host->Symbol(symbol);
    if (!site || *reinterpret_cast<const uint8_t*>(site) != 0xE8) {
        ++g_skipped;
        return false;
    }
    const auto oldRel = *reinterpret_cast<const int32_t*>(site + 1);
    if (original) {
        *original = reinterpret_cast<Fn>(site + 5 + oldRel);
    }
    const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(wrapper) - (site + 5));
    uint8_t code[5] = {0xE8};
    std::memcpy(code + 1, &rel, 4);
    const bool ok = Write(std::string("hf_") + symbol, symbol, site, code, 5);
    g_applied_count += ok ? 1 : 0;
    return ok;
}

void ApplyAll() {
    g_applied_count = g_skipped = 0;
    for (const OperandSwap& swap : kSwaps) {
        Swap(swap);
    }
    Jump("FPS doors add velocity", g_us10 ? &DoorsUs : &DoorsNs, 5, &g_hfRetDoors);
    Jump("FPS swim speed", g_us10 ? &SwimUs : &SwimNs, g_us10 ? 6 : 5, &g_hfRetSwim);
    Jump("FPS ped push force", g_us10 ? &PushUs : &PushNs, g_us10 ? 8 : 6, &g_hfRetPush);
    if (const uintptr_t skip = g_host->Symbol("FPS water cannon recycle skip")) {
        g_hfSkipCannon = static_cast<uint32_t>(skip);
        Jump("FPS water cannon recycle", g_us10 ? &CannonUs : &CannonNs, 5, &g_hfRetCannon);
    }
    const uintptr_t horn = g_host->Symbol("FPS siren exit horn"), toggle = g_host->Symbol("FPS siren exit toggle"),
                    none = g_host->Symbol("FPS siren exit none");
    if (horn && toggle && none && g_host->Symbol("CPad::GetHorn") && g_host->Symbol("CPad::HornJustDown")) {
        g_hfSirenHorn = static_cast<uint32_t>(horn);
        g_hfSirenToggle = static_cast<uint32_t>(toggle);
        g_hfSirenNone = static_cast<uint32_t>(none);
        Jump("FPS siren site", g_us10 ? &SirenUs : &SirenNs, g_us10 ? 5 : 7, nullptr);
    } else {
        ++g_skipped;
    }
    for (const char* site : {"FPS swim bubbles call", "FPS swim wake call"}) {
        CallSwap(site, reinterpret_cast<void*>(&AddParticleWrap), &g_addParticle);
    }
    for (const char* site : {"FPS swim splash call 1", "FPS swim splash call 2", "FPS swim splash call 3",
                             "FPS swim splash call 4"}) {
        CallSwap(site, reinterpret_cast<void*>(&CreateFxWrap), &g_createFx);
    }
    for (const char* site : {"FPS autopilot segment time", "FPS autopilot entered time"}) {
        CallSwap<void*>(site, reinterpret_cast<void*>(&RoundToInt), nullptr);
    }
    Update();
    g_applied = true;
    g_host->Info("high frame rate fixes: {} sites patched, {} not available on this build", g_applied_count, g_skipped);
}

void RemoveAll() {
    for (const std::string& id : g_patchIds) {
        g_host->RemovePatch(id.c_str());
    }
    g_patchIds.clear();
    g_applied = false;
    g_host->Info("high frame rate fixes removed");
}

}  // namespace

void InitHighFps(sacore::mod::Host& host) {
    g_host = &host;
    g_us10 = host.GameBuild() == "us10_hoodlum";
    g_hfTimeStep = host.GameVariable<float>("CTimer::ms_fTimeStep");
    if (!host.Has("engine.high_fps") || !host.HasPatching() || !g_hfTimeStep) {
        host.Warn("high frame rate fixes are not available on this build");
        return;
    }
    if (GetModuleHandleA("FramerateVigilante.asi")) {
        host.Warn("FramerateVigilante is loaded: it patches the same places, SACore's high frame rate fixes stay off");
        return;
    }
    using UpdateFn = void __cdecl();
    if (!host.Hook<UpdateFn>("timer_update", "CTimer::Update", "recompute the frame rate factors each frame",
                             host.GameFunction<UpdateFn*>("CTimer::Update"), &HookedTimerUpdate, &g_origTimerUpdate)) {
        host.Error("could not hook CTimer::Update: high frame rate fixes stay off");
        return;
    }
    SetHighFps(host.ConfigBool("enabled", true) && host.ConfigBool("physics_fixes", true));
}

void SetHighFps(bool on) {
    if (!g_host || !g_origTimerUpdate || on == g_applied) {
        return;
    }
    on ? ApplyAll() : RemoveAll();
}

}  // namespace frame_rate
