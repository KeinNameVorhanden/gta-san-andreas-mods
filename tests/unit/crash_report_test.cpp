#include "stability/crash_report.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using sacore::CrashInfo;
using sacore::DescribeAddress;
using sacore::FormatCrashReport;
using sacore::ModuleRange;

namespace {
std::vector<ModuleRange> Modules() {
    return {{"gta-sa.exe", 0x009F0000, 0x985000}, {"SACore.asi", 0x6A000000, 0x40000}, {"mod.dll", 0x6A040000, 0x1000}};
}
}  // namespace

TEST_CASE("addresses are attributed to the module containing them, with exact boundaries") {
    const auto modules = Modules();
    CHECK(DescribeAddress(0x009F0000, modules) == "gta-sa.exe+0x0");
    CHECK(DescribeAddress(0x009F0000 + 0x984FFF, modules) == "gta-sa.exe+0x984fff");
    CHECK(DescribeAddress(0x009F0000 + 0x985000, modules) == "0x01375000 (no module)");
    // Adjacent modules: the first byte of the next module belongs to it, not to the previous one.
    CHECK(DescribeAddress(0x6A03FFFF, modules) == "SACore.asi+0x3ffff");
    CHECK(DescribeAddress(0x6A040000, modules) == "mod.dll+0x0");
}

TEST_CASE("a null-pointer write in a mod produces a report that names the mod and the access") {
    CrashInfo info;
    info.exceptionCode = 0xC0000005;
    info.exceptionAddress = 0x6A040010;
    info.accessViolation = true;
    info.writeAccess = true;
    info.accessAddress = 0;
    info.stack = {0x6A040010, 0x6A001234, 0x00A01000};
    info.modules = Modules();
    info.recentLog = {"last line"};
    const std::string report = FormatCrashReport(info);
    CHECK(report.find("EXCEPTION_ACCESS_VIOLATION") != std::string::npos);
    CHECK(report.find("Faulting address: mod.dll+0x10") != std::string::npos);
    CHECK(report.find("Access: write at 0x00000000 (null or near-null pointer)") != std::string::npos);
    CHECK(report.find("#1  SACore.asi+0x1234") != std::string::npos);
    CHECK(report.find("#2  gta-sa.exe+0x11000") != std::string::npos);
    CHECK(report.find("No minidump was written.") != std::string::npos);
}

TEST_CASE("return addresses are recognised by the call in front of them, taken from real game code") {
    using sacore::PrecededByCall;
    // 1.0 US 0x7C4800: push eax; call dword ptr [ecx+0x148] -> return address 0x7C4806.
    const uint8_t callRegDisp32[7] = {0x50, 0xFF, 0x91, 0x48, 0x01, 0x00, 0x00};
    // 1.0 US 0x7C47EE: push edx; call 0x7CD590 (E8 rel32) -> return address 0x7C47F3.
    const uint8_t callRel32[7] = {0x5E, 0x10, 0xE8, 0x9D, 0x8D, 0x00, 0x00};
    // call eax / call dword ptr [eax+8] / call dword ptr ds:[abs32]
    const uint8_t callReg[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0};
    const uint8_t callRegDisp8[7] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x50, 0x08};
    const uint8_t callAbs[7] = {0x00, 0xFF, 0x15, 0x24, 0x7B, 0xC9, 0x00};
    CHECK(PrecededByCall(callRegDisp32));
    CHECK(PrecededByCall(callRel32));
    CHECK(PrecededByCall(callReg));
    CHECK(PrecededByCall(callRegDisp8));
    CHECK(PrecededByCall(callAbs));
    // Not calls: jmp eax (FF /4), push dword ptr [eax+8] (FF /6), the float 1e6 seen in the crashing routine,
    // and a mov with an E8 byte in the wrong place.
    const uint8_t jmpReg[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0};
    const uint8_t pushMem[7] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0x70, 0x08};
    const uint8_t floatData[7] = {0x00, 0x00, 0x00, 0x00, 0x24, 0x74, 0x49};
    const uint8_t misplacedE8[7] = {0x00, 0x00, 0x00, 0xE8, 0x89, 0x5E, 0x10};
    CHECK_FALSE(PrecededByCall(jmpReg));
    CHECK_FALSE(PrecededByCall(pushMem));
    CHECK_FALSE(PrecededByCall(floatData));
    CHECK_FALSE(PrecededByCall(misplacedE8));
}
