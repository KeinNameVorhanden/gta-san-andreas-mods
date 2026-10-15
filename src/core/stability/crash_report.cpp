#include "crash_report.h"

#include <format>

namespace sacore {

std::string DescribeAddress(uintptr_t address, const std::vector<ModuleRange>& modules) {
    for (const ModuleRange& module : modules) {
        if (address >= module.base && address - module.base < module.size) {
            return std::format("{}+{:#x}", module.name, address - module.base);
        }
    }
    return std::format("{:#010x} (no module)", address);
}

std::string_view ExceptionName(uint32_t code) {
    switch (code) {
        case 0xC0000005: return "EXCEPTION_ACCESS_VIOLATION";
        case 0xC000001D: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case 0xC0000094: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case 0xC00000FD: return "EXCEPTION_STACK_OVERFLOW";
        case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
        case 0x80000003: return "EXCEPTION_BREAKPOINT";
        case 0xC000008E: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case 0xC0000096: return "EXCEPTION_PRIV_INSTRUCTION";
        case 0xE06D7363: return "C++ exception (unhandled)";
        default: return "unknown exception";
    }
}

bool PrecededByCall(const uint8_t (&b)[7]) {
    // b[6] is the byte right before the return address.
    if (b[2] == 0xE8) {
        return true;  // call rel32
    }
    // call r/m32 = FF /2: ModRM reg field 010. Check each possible instruction length.
    const auto isCallModRm = [](uint8_t modrm) { return ((modrm >> 3) & 7) == 2; };
    if (b[5] == 0xFF && isCallModRm(b[6]) && (b[6] >> 6) == 3) {
        return true;  // call reg (2 bytes)
    }
    if (b[4] == 0xFF && isCallModRm(b[5]) && (b[5] >> 6) == 1 && (b[5] & 7) != 4) {
        return true;  // call [reg+disp8] (3 bytes)
    }
    if (b[4] == 0xFF && isCallModRm(b[5]) && (b[5] >> 6) == 0 && (b[5] & 7) == 4) {
        return true;  // call [sib] (3 bytes)
    }
    if (b[3] == 0xFF && isCallModRm(b[4]) && (b[4] >> 6) == 1 && (b[4] & 7) == 4) {
        return true;  // call [sib+disp8] (4 bytes)
    }
    if (b[1] == 0xFF && isCallModRm(b[2]) && (((b[2] >> 6) == 2 && (b[2] & 7) != 4) || b[2] == 0x15)) {
        return true;  // call [reg+disp32] or call [abs32] (6 bytes)
    }
    if (b[0] == 0xFF && isCallModRm(b[1]) && (b[1] >> 6) == 2 && (b[1] & 7) == 4) {
        return true;  // call [sib+disp32] (7 bytes)
    }
    return false;
}

std::string FormatCrashReport(const CrashInfo& info) {
    std::string text = std::format("SACore crash report {}\n\n", info.timestamp);
    text += std::format("Exception: {:#010x} {}\n", info.exceptionCode, ExceptionName(info.exceptionCode));
    text += std::format("Faulting address: {}\n", DescribeAddress(info.exceptionAddress, info.modules));
    if (info.accessViolation) {
        text += std::format("Access: {} at {:#010x}{}\n", info.writeAccess ? "write" : "read", info.accessAddress,
                            info.accessAddress < 0x10000 ? " (null or near-null pointer)" : "");
    }
    text += "\nStack (innermost first):\n";
    for (size_t i = 0; i < info.stack.size(); ++i) {
        text += std::format("  #{:<2} {}\n", i, DescribeAddress(info.stack[i], info.modules));
    }
    if (!info.stackScan.empty()) {
        text += "\nStack scan (call-verified return addresses on the stack, innermost first):\n";
        for (size_t i = 0; i < info.stackScan.size(); ++i) {
            text += std::format("  ~{:<2} {}\n", i, DescribeAddress(info.stackScan[i], info.modules));
        }
    }
    if (!info.registers.empty()) {
        text += "\nRegisters:\n" + info.registers + "\n";
    }
    text += "\nContext:\n" + info.context + "\n";
    if (!info.system.empty()) {
        text += "\nSystem:\n" + info.system + "\n";
    }
    text += "\nLast log lines:\n";
    for (const std::string& line : info.recentLog) {
        text += "  " + line + "\n";
    }
    text += info.dumpFile.empty() ? "\nNo minidump was written.\n" : "\nMinidump: " + info.dumpFile + "\n";
    text += "\nSend the .zip with the same name (report, dump, log, settings) for analysis.\n";
    return text;
}

}  // namespace sacore
