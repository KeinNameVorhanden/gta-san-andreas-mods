// Crash report content, kept free of Windows calls so it can be unit tested.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

struct ModuleRange {
    std::string name;
    uintptr_t base = 0;
    uintptr_t size = 0;
};

struct CrashInfo {
    uint32_t exceptionCode = 0;
    uintptr_t exceptionAddress = 0;
    bool accessViolation = false;
    bool writeAccess = false;        // access violation type
    uintptr_t accessAddress = 0;     // address that was accessed
    std::vector<uintptr_t> stack;    // return addresses, innermost first
    std::vector<uintptr_t> stackScan;  // call-verified return addresses found on the stack, innermost first
    std::string system;              // OS, memory, processors (preformatted)
    std::vector<ModuleRange> modules;
    std::string registers;           // preformatted
    std::string context;             // core version, build, mods, patches
    std::vector<std::string> recentLog;
    std::string timestamp;
    std::string dumpFile;            // empty if no dump was written
};

// "module+0xRVA" or "0xADDRESS (no module)".
std::string DescribeAddress(uintptr_t address, const std::vector<ModuleRange>& modules);
std::string_view ExceptionName(uint32_t code);
std::string FormatCrashReport(const CrashInfo& info);

// True if the 7 bytes before a candidate return address end with a CALL instruction (E8 rel32, FF /2 with any
// addressing form). Game code is built without frame pointers, so the frame walk breaks early; scanning the stack
// for such addresses recovers the call chain with few false positives.
bool PrecededByCall(const uint8_t (&before)[7]);

}  // namespace sacore
