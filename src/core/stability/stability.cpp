#include "stability.h"

#include "events.h"
#include "stability/crash_report.h"
#include "stability/zip_writer.h"

#include <dbghelp.h>
#include <psapi.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>

namespace sacore {
namespace {

using MessageBoxAFn = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
using StackWalk64Fn = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                                    PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
                                    PTRANSLATE_ADDRESS_ROUTINE64);
using SymInitializeFn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);

Stability* g_stability = nullptr;
HHOOK g_dialogWatcher = nullptr;
MessageBoxAFn g_origMessageBoxA = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_chainedFilter = nullptr;  // filter installed by someone else, called after ours
volatile LONG g_crashing = 0;

MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;
StackWalk64Fn g_stackWalk64 = nullptr;
PFUNCTION_TABLE_ACCESS_ROUTINE64 g_symFunctionTableAccess64 = nullptr;
PGET_MODULE_BASE_ROUTINE64 g_symGetModuleBase64 = nullptr;

constexpr wchar_t kDeviceSelectionTitle[] = L"Device Selection";
constexpr int kMaxStackFrames = 32;
constexpr ULONGLONG kFilterReassertMs = 30'000;
constexpr size_t kMaxModules = 512;
constexpr size_t kMaxScanFrames = 40;
constexpr uintptr_t kMaxScanBytes = 64 * 1024;
using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);

std::string Timestamp(char dateTimeSeparator, char timeSeparator) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return std::format("{:04}-{:02}-{:02}{}{:02}{}{:02}{}{:02}", now.wYear, now.wMonth, now.wDay, dateTimeSeparator,
                       now.wHour, timeSeparator, now.wMinute, timeSeparator, now.wSecond);
}

std::vector<ModuleRange> LoadedModules() {
    std::vector<ModuleRange> ranges;
    HMODULE modules[kMaxModules];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        return ranges;
    }
    const size_t count = std::min<size_t>(needed / sizeof(HMODULE), kMaxModules);
    for (size_t i = 0; i < count; ++i) {
        MODULEINFO info{};
        char name[MAX_PATH] = {};
        if (GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)) &&
            GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH)) {
            ranges.push_back({name, reinterpret_cast<uintptr_t>(info.lpBaseOfDll), info.SizeOfImage});
        }
    }
    return ranges;
}

std::vector<uintptr_t> WalkStack(const CONTEXT& crashContext) {
    std::vector<uintptr_t> frames;
    if (!g_stackWalk64) {
        frames.push_back(crashContext.Eip);
        return frames;
    }
    CONTEXT context = crashContext;  // StackWalk64 modifies it
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Eip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Ebp, 0, AddrModeFlat};
    frame.AddrStack = {context.Esp, 0, AddrModeFlat};
    for (int i = 0; i < kMaxStackFrames; ++i) {
        if (!g_stackWalk64(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr,
                           g_symFunctionTableAccess64, g_symGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        frames.push_back(static_cast<uintptr_t>(frame.AddrPC.Offset));
    }
    return frames;
}

bool SafeCopy(uintptr_t address, void* out, size_t size) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Return addresses on the crashing thread's stack whose preceding bytes are a CALL. Complements StackWalk64, which
// stops at the first function without a frame pointer (most of the game).
std::vector<uintptr_t> ScanStack(const CONTEXT& context, const std::vector<ModuleRange>& modules) {
    std::vector<uintptr_t> found;
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t top = reinterpret_cast<uintptr_t>(tib->StackBase);
    const uintptr_t begin = context.Esp;
    const uintptr_t end = std::min<uintptr_t>(top, begin + kMaxScanBytes);
    for (uintptr_t slot = begin; slot + 4 <= end && found.size() < kMaxScanFrames; slot += 4) {
        uintptr_t value = 0;
        if (!SafeCopy(slot, &value, 4) || value < 0x10000) {
            continue;
        }
        const bool inModule = std::any_of(modules.begin(), modules.end(), [&](const ModuleRange& m) {
            return value >= m.base + 7 && value - m.base < m.size;
        });
        uint8_t before[7];
        if (inModule && SafeCopy(value - 7, before, 7) && PrecededByCall(before)) {
            found.push_back(value);
        }
    }
    return found;
}

std::string SystemDescription() {
    std::string text;
    OSVERSIONINFOW version{sizeof(version)};
    if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        rtlGetVersion && rtlGetVersion(&version) == 0) {
        text += std::format("  Windows {}.{} build {}\n", version.dwMajorVersion, version.dwMinorVersion,
                            version.dwBuildNumber);
    }
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) {
        text += std::format("  memory: {} MB physical, {} MB free; process address space {} MB free of {} MB\n",
                            memory.ullTotalPhys >> 20, memory.ullAvailPhys >> 20, memory.ullAvailVirtual >> 20,
                            memory.ullTotalVirtual >> 20);
    }
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        text += std::format("  process: {} MB private, {} MB working set\n", counters.PrivateUsage >> 20,
                            counters.WorkingSetSize >> 20);
    }
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    text += std::format("  processors: {}\n", system.dwNumberOfProcessors);
    return text;
}

__declspec(noinline) void DeliberateTestCrash() {
    *static_cast<volatile int*>(nullptr) = 0x5ACE;
}

}  // namespace

Stability::Stability(Logger& logger, PatchManager& patches, EventBus& bus, const RingBufferSink* recentLog)
    : logger_(logger), patches_(patches), bus_(bus), recentLog_(recentLog) {
    g_stability = this;
}

Stability::~Stability() {
    g_stability = nullptr;
}

void Stability::Install(const StabilitySettings& settings, const std::filesystem::path& crashDirectory,
                        ContextProvider context) {
    settings_ = settings;
    crashDirectory_ = crashDirectory;
    context_ = std::move(context);

    if (settings_.crashDumps) {
        if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
            g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
            g_stackWalk64 = reinterpret_cast<StackWalk64Fn>(GetProcAddress(dbghelp, "StackWalk64"));
            g_symFunctionTableAccess64 = reinterpret_cast<PFUNCTION_TABLE_ACCESS_ROUTINE64>(
                GetProcAddress(dbghelp, "SymFunctionTableAccess64"));
            g_symGetModuleBase64 =
                reinterpret_cast<PGET_MODULE_BASE_ROUTINE64>(GetProcAddress(dbghelp, "SymGetModuleBase64"));
            auto symSetOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(dbghelp, "SymSetOptions"));
            auto symInitialize = reinterpret_cast<SymInitializeFn>(GetProcAddress(dbghelp, "SymInitialize"));
            if (symSetOptions && symInitialize) {
                symSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
                symInitialize(GetCurrentProcess(), nullptr, TRUE);
            }
        }
        // Install our filter and re-assert it on the first frame and every 30 s: a filter set later by the game or
        // another plugin is then chained after ours. (Hooking SetUnhandledExceptionFilter instead made newsteam end
        // with 0xC0000409 on every normal exit; with the hook off the exit code was 0 again.)
        g_chainedFilter = SetUnhandledExceptionFilter(&CrashFilter);
        bus_.Subscribe<events::FramePresented>("core.stability.filter", [this](const events::FramePresented& frame) {
            static ULONGLONG last = 0;
            const ULONGLONG now = GetTickCount64();
            if (frame.frame != 1 && now - last < kFilterReassertMs) {
                return;
            }
            last = now;
            const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(&CrashFilter);
            if (current != &CrashFilter) {
                g_chainedFilter = current;
                logger_.Logf(LogLevel::Debug, LogCategory::Diagnostics, "crash",
                             "another module had set an exception filter ({:#010x}); SACore's is first again, theirs "
                             "is chained", reinterpret_cast<uintptr_t>(current));
            }
        });
        logger_.Logf(LogLevel::Info, LogCategory::Diagnostics, "crash", "crash handler installed, reports go to {}",
                     crashDirectory_.string());
        PackLeftoverReports();
    }
    if (settings_.skipDeviceSelection) {
        // A thread-local CBT hook on the game thread sees the dialog however it is created (the game, SilentPatch
        // or a compatibility shim may each route the DialogBoxParamA call differently).
        g_dialogWatcher = SetWindowsHookExW(WH_CBT, &DialogWatcher, nullptr, GetCurrentThreadId());
        logger_.Logf(g_dialogWatcher ? LogLevel::Info : LogLevel::Error, LogCategory::Runtime, "stability",
                     "Device Selection auto-confirm {}", g_dialogWatcher
                         ? "armed (thread CBT hook)"
                         : std::format("NOT armed (SetWindowsHookEx error {})", GetLastError()));
    }
    patches_.InstallImportHook({"stability.log_message_boxes", "core", "user32!MessageBoxA (game imports)",
                          "record the game's error message boxes in the log",
                          "message box text logged under [Diagnostics], box shown as usual",
                          "error boxes are not logged"},
                         "user32.dll", "MessageBoxA", reinterpret_cast<void*>(&HookedMessageBoxA),
                         reinterpret_cast<void**>(&g_origMessageBoxA));

    if (settings_.testCrashAfterSeconds > 0) {
        logger_.Logf(LogLevel::Warn, LogCategory::Diagnostics, "crash",
                     "TEST ONLY: deliberate crash scheduled {} s after the first frame", settings_.testCrashAfterSeconds);
        bus_.Subscribe<events::FramePresented>("core.stability", [this](const events::FramePresented&) {
            static ULONGLONG start = GetTickCount64();
            if (GetTickCount64() - start >= static_cast<ULONGLONG>(settings_.testCrashAfterSeconds) * 1000) {
                logger_.Log(LogLevel::Warn, LogCategory::Diagnostics, "crash", "TEST ONLY: crashing now");
                DeliberateTestCrash();
            }
        });
    }
}

LONG WINAPI Stability::CrashFilter(EXCEPTION_POINTERS* pointers) {
    if (g_stability && InterlockedExchange(&g_crashing, 1) == 0) {
        g_stability->WriteCrashReport(pointers);
    }
    // With nobody to chain to, end the process here: our report and dump are written, and the Windows
    // "has stopped working" dialog would only block the desktop.
    return g_chainedFilter ? g_chainedFilter(pointers) : EXCEPTION_EXECUTE_HANDLER;
}

void Stability::WriteCrashReport(EXCEPTION_POINTERS* pointers) {
    std::error_code error;
    std::filesystem::create_directories(crashDirectory_, error);
    const std::string stamp = Timestamp('_', '-');
    CrashInfo info;
    info.timestamp = Timestamp(' ', ':');
    info.exceptionCode = pointers->ExceptionRecord->ExceptionCode;
    info.exceptionAddress = reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
    if (info.exceptionCode == EXCEPTION_ACCESS_VIOLATION && pointers->ExceptionRecord->NumberParameters >= 2) {
        info.accessViolation = true;
        info.writeAccess = pointers->ExceptionRecord->ExceptionInformation[0] == 1;
        info.accessAddress = pointers->ExceptionRecord->ExceptionInformation[1];
    }
    const CONTEXT& c = *pointers->ContextRecord;
    info.registers = std::format("  EAX {:08X} EBX {:08X} ECX {:08X} EDX {:08X}\n  ESI {:08X} EDI {:08X} EBP {:08X} "
                                 "ESP {:08X} EIP {:08X}",
                                 c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp, c.Eip);
    info.modules = LoadedModules();
    info.stack = WalkStack(c);
    info.stackScan = ScanStack(c, info.modules);
    info.system = SystemDescription();
    if (context_) {
        info.context = context_();
    }
    if (recentLog_) {
        info.recentLog = recentLog_->Lines();
    }

    if (g_miniDumpWriteDump) {
        const std::filesystem::path dumpPath = crashDirectory_ / (stamp + ".dmp");
        HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), pointers, FALSE};
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                                                         MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
            if (g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr,
                                    nullptr)) {
                info.dumpFile = dumpPath.filename().string();
            }
            CloseHandle(file);
        }
    }
    const std::string report = FormatCrashReport(info);
    std::ofstream(crashDirectory_ / (stamp + ".txt"), std::ios::binary) << report;
    logger_.Logf(LogLevel::Fatal, LogCategory::Diagnostics, "crash", "{:#010x} {} at {}; package {}.zip",
                 info.exceptionCode, ExceptionName(info.exceptionCode),
                 DescribeAddress(info.exceptionAddress, info.modules), stamp);
    logger_.Flush();

    // One file to send: report, dump, the session log and the settings that shaped the session.
    std::vector<ZipSource> sources;
    sources.push_back({stamp + ".txt", crashDirectory_ / (stamp + ".txt"), {}});
    if (!info.dumpFile.empty()) {
        sources.push_back({info.dumpFile, crashDirectory_ / info.dumpFile, {}});
    }
    if (packageFiles_) {
        for (auto& [file, name] : packageFiles_()) {
            sources.push_back({name, file, {}});
        }
    }
    WriteStoredZip(crashDirectory_ / (stamp + ".zip"), sources);
}

void Stability::PackLeftoverReports() {
    // A crash whose package could not be written (for example a crash inside the crash handler) is packed now,
    // with the report and the dump; the log of that session is sacore.1.log by now and is named as such.
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(crashDirectory_, error)) {
        const std::filesystem::path report = entry.path();
        if (report.extension() != ".txt" || std::filesystem::exists(report.parent_path() / (report.stem().string() + ".zip"))) {
            continue;
        }
        std::vector<ZipSource> sources{{report.filename().string(), report, {}}};
        const std::filesystem::path dump = report.parent_path() / (report.stem().string() + ".dmp");
        if (std::filesystem::exists(dump)) {
            sources.push_back({dump.filename().string(), dump, {}});
        }
        if (WriteStoredZip(report.parent_path() / (report.stem().string() + ".zip"), sources)) {
            logger_.Logf(LogLevel::Info, LogCategory::Diagnostics, "crash", "packed earlier crash report {}",
                         report.stem().string());
        }
    }
}

LRESULT CALLBACK Stability::DialogWatcher(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE) {
        const auto dialog = reinterpret_cast<HWND>(wParam);
        wchar_t className[16] = {};
        wchar_t title[64] = {};
        GetClassNameW(dialog, className, 16);
        GetWindowTextW(dialog, title, 64);
        if (wcscmp(className, L"#32770") == 0 && wcscmp(title, kDeviceSelectionTitle) == 0) {
            // Press OK with the default selection, exactly like a user would, and keep the dialog off screen.
            SetWindowPos(dialog, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                         reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDOK)));
            if (g_stability) {
                g_stability->logger_.Log(LogLevel::Info, LogCategory::Runtime, "stability",
                                         "Device Selection dialog confirmed automatically (default device)");
            }
        }
    }
    return CallNextHookEx(g_dialogWatcher, code, wParam, lParam);
}

int WINAPI Stability::HookedMessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
    if (g_stability) {
        g_stability->logger_.Logf(LogLevel::Error, LogCategory::Diagnostics, "messagebox", "\"{}\": {}",
                                  caption ? caption : "", text ? text : "");
        g_stability->logger_.Flush();
    }
    return g_origMessageBoxA(owner, text, caption, type);
}

}  // namespace sacore
