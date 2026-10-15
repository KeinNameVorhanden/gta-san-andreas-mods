// Real hooks inside the test executable (x86): MinHook, import table and memory writes.
#include "patch_manager.h"

#include <windows.h>

#include <MinHook.h>
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

using sacore::Logger;
using sacore::LogLevel;
using sacore::PatchInfo;
using sacore::PatchManager;
using sacore::PatchState;

namespace {

struct NullSink final : sacore::LogSink {
    void Write(std::string_view) override {}
};

struct MinHookScope {
    MinHookScope() { MH_Initialize(); }
    ~MinHookScope() { MH_Uninitialize(); }
};

__declspec(noinline) int Target(int value) {
    volatile int result = value * 2;  // keep the function body large enough to hook
    return result + 1;
}

int (*g_originalTarget)(int) = nullptr;
int (*g_secondOriginal)(int) = nullptr;
__declspec(noinline) int Detour(int value) {
    return g_originalTarget(value) + 1000;
}
__declspec(noinline) int SecondDetour(int value) {
    return g_secondOriginal(value) + 5000;
}

DWORD(WINAPI* g_originalPid)() = nullptr;
DWORD WINAPI FakePid() {
    return 4242;
}

PatchInfo Info(const char* id) {
    return PatchInfo{id, "test", "target", "reason", "expected", "on failure"};
}

}  // namespace

TEST_CASE("a second hook on the same target is refused and the first keeps working; removal restores the target") {
    MinHookScope minhook;
    Logger logger(std::make_unique<NullSink>(), LogLevel::Trace);
    PatchManager patches(logger);
    REQUIRE(patches.InstallHook(Info("first"), reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour),
                                reinterpret_cast<void**>(&g_originalTarget)));
    CHECK(Target(1) == 1003);
    CHECK_FALSE(patches.InstallHook(Info("second"), reinterpret_cast<void*>(&Target),
                                    reinterpret_cast<void*>(&SecondDetour), reinterpret_cast<void**>(&g_secondOriginal)));
    CHECK(patches.Find("second")->detail.find("already hooked by patch 'first' of 'test'") != std::string::npos);
    CHECK(Target(1) == 1003);
    CHECK(patches.Remove("first"));
    CHECK(Target(1) == 3);
    CHECK(patches.Find("first")->state == PatchState::Removed);
}

TEST_CASE("a patch switched off by configuration is recorded but never installed") {
    MinHookScope minhook;
    Logger logger(std::make_unique<NullSink>(), LogLevel::Trace);
    PatchManager patches(logger);
    patches.SetDisabled({"first"});
    CHECK_FALSE(patches.InstallHook(Info("first"), reinterpret_cast<void*>(&Target), reinterpret_cast<void*>(&Detour),
                                    reinterpret_cast<void**>(&g_originalTarget)));
    CHECK(Target(1) == 3);
    CHECK(patches.Find("first")->state == PatchState::Disabled);
}

TEST_CASE("the import table hook redirects this executable's own calls and is undone on removal") {
    Logger logger(std::make_unique<NullSink>(), LogLevel::Trace);
    PatchManager patches(logger);
    const DWORD real = GetCurrentProcessId();
    REQUIRE(patches.InstallImportHook(Info("pid"), "KERNEL32.dll", "GetCurrentProcessId",
                                      reinterpret_cast<void*>(&FakePid), reinterpret_cast<void**>(&g_originalPid)));
    CHECK(GetCurrentProcessId() == 4242);
    CHECK(g_originalPid() == real);
    patches.RemoveAll();
    CHECK(GetCurrentProcessId() == real);
    CHECK_FALSE(patches.InstallImportHook(Info("missing"), "KERNEL32.dll", "NoSuchFunctionExport",
                                          reinterpret_cast<void*>(&FakePid), reinterpret_cast<void**>(&g_originalPid)));
}

TEST_CASE("memory writes are reverted on removal unless marked as game state") {
    Logger logger(std::make_unique<NullSink>(), LogLevel::Trace);
    PatchManager patches(logger);
    static int code = 1;
    static int state = 0;
    const int two = 2;
    const int five = 5;
    REQUIRE(patches.WriteMemory(Info("code"), &code, &two, sizeof(two)));
    PatchInfo stateInfo = Info("state");
    stateInfo.revertOnRemove = false;
    REQUIRE(patches.WriteMemory(stateInfo, &state, &five, sizeof(five)));
    CHECK(code == 2);
    CHECK(state == 5);
    patches.RemoveAll();
    CHECK(code == 1);
    CHECK(state == 5);
    CHECK(patches.StatusReport().find("removed") != std::string::npos);
}
