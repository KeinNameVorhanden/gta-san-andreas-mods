#include "save_service.h"

#include "adapters.h"
#include "events.h"
#include "saves/save_file.h"

#include <windows.h>

#include <toml++/toml.hpp>

#include <cstring>
#include <format>
#include <tuple>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace sacore {
namespace {

using MakeValidSaveNameFn = void(__cdecl*)(int);
// C_PcSave::SaveSlot is a member function ending in "ret 4" (callee cleans the argument) on both builds; its body
// never uses "this", so __stdcall matches it (calling it as __cdecl shifted the caller's stack by 4 bytes).
using SaveSlotFn = char(__stdcall*)(char);
using SetSaveDirectoryFn = void(__cdecl*)(const char*);
using GetUserFilesFolderFn = char*(__cdecl*)();
GetUserFilesFolderFn g_origGetUserFilesFolder = nullptr;
constexpr size_t kUserFilesBufferSize = 0x100;  // static buffer in both builds

SaveService* g_saves = nullptr;
MakeValidSaveNameFn g_origMakeValidSaveName = nullptr;
constexpr int kSlots = 8;

std::string Stamp() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                       now.wSecond);
}

std::vector<uint8_t> ReadAll(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// The game reads "My Documents" from HKCU\...\Explorer\Shell Folders as an ANSI string.
std::string PersonalFolderFromRegistry() {
    HKEY key = nullptr;
    char buffer[MAX_PATH] = {};
    DWORD size = sizeof(buffer);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0,
                      KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "Personal", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(key);
    }
    return buffer;
}

}  // namespace

SaveService::SaveService(Logger& logger, PatchManager& patches, EventBus& bus, const GameAdapter& adapter,
                         std::filesystem::path dataDirectory)
    : logger_(logger), patches_(patches), bus_(bus), adapter_(adapter), dataDirectory_(std::move(dataDirectory)) {
    g_saves = this;
}

SaveService::~SaveService() {
    g_saves = nullptr;
}

char* __cdecl SaveService::HookedGetUserFilesFolder() {
    char* path = g_origGetUserFilesFolder();
    if (g_saves && path && !g_saves->userFilesFolder_.empty() && g_saves->userFilesFolder_ != path) {
        std::memcpy(path, g_saves->userFilesFolder_.c_str(), g_saves->userFilesFolder_.size() + 1);
    }
    return path;
}

void SaveService::Install(const SaveSettings& settings) {
    settings_ = settings;
    redirectBase_ = settings_.redirectBase;
    if (!redirectBase_.empty()) {
        // The game computed its save path prefix before plugins loaded; point it at the separate folder.
        if (adapter_.Available(adapters::kCapSeparateUserFiles)) {
            const std::string userFiles = redirectBase_ + "\\GTA San Andreas User Files";
            CreateDirectoryA(userFiles.c_str(), nullptr);
            reinterpret_cast<SetSaveDirectoryFn>(adapter_.Address(adapters::kSetSaveDirectory))(userFiles.c_str());
            logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "save path prefix set to {}", userFiles);
            // Settings (gta_sa.set) and the gallery follow GetUserFilesFolder, which 1.0 US fills without passing
            // through the import table SACore redirects (R21). Its result is corrected on every call.
            userFilesFolder_ = userFiles;
            if (userFilesFolder_.size() < kUserFilesBufferSize &&
                patches_.InstallHook({"saves.user_files_folder", "core", "GetUserFilesFolder",
                                      "settings and gallery follow the separate User Files folder",
                                      "the game's User Files path is this install's sacore\\userdata folder",
                                      "gta_sa.set stays in the shared Documents folder"},
                                     reinterpret_cast<void*>(adapter_.Address(adapters::kGetUserFilesFolder)),
                                     reinterpret_cast<void*>(&HookedGetUserFilesFolder),
                                     reinterpret_cast<void**>(&g_origGetUserFilesFolder))) {
                HookedGetUserFilesFolder();  // correct the buffer the game already filled before plugins loaded
            }
        } else {
            logger_.Log(LogLevel::Error, LogCategory::GameAdapter, "saves",
                        "separate_user_files: the save path setter is unavailable on this build; saves would still go "
                        "to the shared folder, so the redirect is switched off");
            redirectBase_.clear();
        }
    }
    if (settings_.separateUserFiles && redirectBase_.empty()) {
        logger_.Logf(LogLevel::Error, LogCategory::Configuration, "saves",
                     "separate_user_files is on but inactive: {}; the shared Documents folder is used",
                     settings_.redirectProblem.empty() ? "early hook not installed" : settings_.redirectProblem);
    }
    if (adapter_.Available(adapters::kCapSaveSlotTracking)) {
        patches_.InstallHook({"saves.track_slot", "core", "CGenericGameStorage::MakeValidSaveName",
                              "know which slot a save or load uses (backups, side-car data)",
                              "slot number recorded, original runs unchanged", "no slot-specific side-car data"},
                             reinterpret_cast<void*>(adapter_.Address(adapters::kMakeValidSaveName)),
                             reinterpret_cast<void*>(&HookedMakeValidSaveName),
                             reinterpret_cast<void**>(&g_origMakeValidSaveName));
    }
    bus_.Subscribe<events::SaveStarted>("core.saves", [this](const events::SaveStarted&) {
        if (!currentSave_.empty()) {
            BackupFile(FileFor(currentSave_), currentSave_, "before save");
        }
    });
    bus_.Subscribe<events::SaveCompleted>("core.saves", [this](const events::SaveCompleted& e) {
        if (!e.success || currentSave_.empty()) {
            return;
        }
        const SaveCheck check = CheckSave(ReadAll(FileFor(currentSave_)));
        logger_.Logf(check.valid ? LogLevel::Info : LogLevel::Error, LogCategory::GameAdapter, "saves",
                     "{} saved: {}", currentSave_ == std::format("slot{}", currentSlot_) ? std::format("slot {}", currentSlot_)
                                                                                         : currentSave_,
                     check.valid ? std::format("valid (version id {:#010x})", check.versionId) : check.problem);
        WriteSideCar(currentSave_);
    });
    bus_.Subscribe<events::LoadCompleted>("core.saves", [this](const events::LoadCompleted& e) {
        sideCar_.clear();
        if (e.success && !currentSave_.empty()) {
            ReadSideCar(currentSave_);
        }
    });

    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "User Files folder: {} ({})",
                 UserFilesDirectory().string(), settings_.separateUserFiles ? "separate for this install" : "shared");
    for (int slot = 1; slot <= kSlots; ++slot) {
        BackupSlot(slot, "session start");
    }
    ValidateAll();
}

std::filesystem::path SaveService::UserFilesDirectory() const {
    const std::string base = redirectBase_.empty() ? PersonalFolderFromRegistry() : redirectBase_;
    return std::filesystem::path(base) / "GTA San Andreas User Files";
}

std::filesystem::path SaveService::SlotFile(int slot) const {
    return UserFilesDirectory() / std::format("GTASAsf{}.b", slot);
}

int SaveService::ValidateAll() const {
    int invalid = 0;
    for (int slot = 1; slot <= kSlots; ++slot) {
        const std::filesystem::path file = SlotFile(slot);
        std::error_code error;
        if (!std::filesystem::exists(file, error)) {
            continue;
        }
        const SaveCheck check = CheckSave(ReadAll(file));
        if (!check.valid) {
            ++invalid;
            logger_.Logf(LogLevel::Error, LogCategory::GameAdapter, "saves",
                         "slot {} ({}) is damaged: {}. Backups: {}", slot, file.filename().string(), check.problem,
                         (dataDirectory_ / "save-backups" / file.filename()).string());
        } else {
            logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "slot {} valid (version id {:#010x})", slot,
                         check.versionId);
        }
    }
    return invalid;
}


static std::string SideCarLabel(const std::string& name) {
    return name.starts_with("slot") ? "slot " + name.substr(4) : name;
}

std::filesystem::path SaveService::FileFor(const std::string& name) const {
    const auto parsed = ParseSaveName(name);
    return parsed ? UserFilesDirectory() / std::format("{}{}.b", parsed->first, parsed->second) : std::filesystem::path{};
}

std::vector<SaveService::SaveEntry> SaveService::ListSaves() const {
    std::vector<SaveEntry> entries;
    for (const auto& [prefix, count] : {std::pair{"slot", 8}, std::pair{"quick", 9}, std::pair{"auto", 9}}) {
        for (int i = 1; i <= count; ++i) {
            SaveEntry entry;
            entry.name = std::format("{}{}", prefix, i);
            entry.file = FileFor(entry.name);
            std::error_code error;
            const auto time = std::filesystem::last_write_time(entry.file, error);
            if (error) {
                continue;
            }
            entry.modified = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::clock_cast<std::chrono::system_clock>(time).time_since_epoch())
                                 .count();
            entry.valid = CheckSave(ReadAll(entry.file)).valid;
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::vector<SaveService::ImportEntry> SaveService::ListImports() const {
    std::vector<ImportEntry> entries;
    std::error_code error;
    std::filesystem::create_directories(ImportDirectory(), error);
    for (const auto& item : std::filesystem::directory_iterator(ImportDirectory(), error)) {
        if (!item.is_regular_file()) {
            continue;
        }
        ImportEntry entry;
        const std::u8string name = item.path().filename().u8string();
        entry.file.assign(name.begin(), name.end());
        entry.size = item.file_size(error);
        const SaveCheck check = CheckSave(ReadAll(item.path()));
        entry.valid = check.valid;
        entry.problem = check.problem;
        entry.versionId = check.versionId;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string SaveService::Import(const std::string& file, const std::string& name) const {
    const std::filesystem::path source = ImportDirectory() / std::filesystem::path(std::u8string(file.begin(), file.end()));
    if (file.empty() || source.parent_path() != ImportDirectory()) {
        return "not a file in the import folder";
    }
    const std::filesystem::path target = FileFor(name);
    if (target.empty()) {
        return "unknown save name " + name;
    }
    const std::vector<uint8_t> bytes = ReadAll(source);
    const SaveCheck check = CheckSave(bytes);
    if (!check.valid) {
        return "not a valid save: " + check.problem;
    }
    std::error_code error;
    if (std::filesystem::exists(target, error)) {
        BackupFile(target, name, "before import");
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        return "could not write " + target.string();
    }
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "imported {} as {} (version id {:#010x})", file,
                 name, check.versionId);
    return {};
}

void SaveService::BackupFile(const std::filesystem::path& file, const std::string& name, const char* reason) const {
    std::string error;
    const auto backup = BackupSaveFile(file, dataDirectory_ / L"save-backups", Stamp(),
                                       static_cast<size_t>(std::max(1, settings_.backupsPerSlot)), error);
    if (backup) {
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "{} backed up ({}): {}", SideCarLabel(name),
                     reason, backup->string());
    } else if (!error.empty()) {
        logger_.Logf(LogLevel::Error, LogCategory::GameAdapter, "saves", "{} backup FAILED: {}", SideCarLabel(name),
                     error);
    }
}

bool SaveService::SaveTo(const std::string& name) {
    const auto parsed = ParseSaveName(name);
    if (!parsed) {
        return false;
    }
    if (parsed->first == "GTASAsf") {
        return SaveToSlot(parsed->second);
    }
    if (!adapter_.Available(adapters::kCapSaveExtraSlots)) {
        logger_.Logf(LogLevel::Error, LogCategory::GameAdapter, "saves", "{}: extra save files unavailable on this build",
                     name);
        return false;
    }
    // The file name the game builds for "slot 1" is redirected once (HookedMakeValidSaveName). With save.quick the
    // save skips C_PcSave::SaveSlot's safe-house steps (6 hours pass, health refill, wanted level cleared): the same
    // calls without DoGameSpecificStuffBeforeSave.
    pendingName_ = name;
    const auto clock = [this]() -> int {
        if (!adapter_.Available("world.time")) {
            return -1;
        }
        return *reinterpret_cast<const uint8_t*>(adapter_.Address("CClock::ms_nGameClockHours")) * 60 +
               *reinterpret_cast<const uint8_t*>(adapter_.Address("CClock::ms_nGameClockMinutes"));
    };
    const int before = clock();
    bool ok = false;
    if (adapter_.Available("save.quick")) {
        reinterpret_cast<void(__cdecl*)(int)>(adapter_.Address(adapters::kMakeValidSaveName))(0);
        *reinterpret_cast<int*>(adapter_.Address("PcSaveHelper")) = 0;
        reinterpret_cast<char*(__cdecl*)()>(adapter_.Address(adapters::kGetUserFilesFolder))();
        ok = reinterpret_cast<bool(__cdecl*)(int)>(adapter_.Address(adapters::kGenericSave))(0);
    } else {
        ok = reinterpret_cast<SaveSlotFn>(adapter_.Address(adapters::kSaveSlot))(0) == 0;
    }
    pendingName_.clear();
    const int after = clock();
    logger_.Logf(ok ? LogLevel::Info : LogLevel::Error, LogCategory::GameAdapter, "saves",
                 "save to {} {} (game clock {:02}:{:02} before, {:02}:{:02} after)", name, ok ? "done" : "FAILED",
                 before / 60, before % 60, after / 60, after % 60);
    if (before >= 0 && after >= 0 && std::abs(after - before) <= 5) {
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "{}: no time skip", name);
    }
    return ok;
}

bool SaveService::LoadFrom(const std::string& name) {
    const std::filesystem::path file = FileFor(name);
    const auto fail = [&](const std::string& why) {
        logger_.Logf(LogLevel::Warn, LogCategory::GameAdapter, "saves", "load {} refused: {}", name, why);
        return false;
    };
    if (file.empty()) {
        return fail("unknown save name");
    }
    if (!adapter_.Available("save.load_slot")) {
        return fail("loading is unavailable on this build");
    }
    const SaveCheck check = CheckSave(ReadAll(file));
    if (!check.valid) {
        return fail(check.problem);
    }
    const std::string path = file.string();
    if (path.size() >= 104) {  // CGenericGameStorage::ms_LoadFileName is char[104]
        return fail("path too long for the game");
    }
    const int state = *reinterpret_cast<const int*>(adapter_.Address(adapters::kGameState));
    if (state != 7 && state != 9) {
        return fail("only from the main menu or while playing");
    }
    // ProcessFileActions (load confirm) without CheckSlotDataValid, whose only job here is to put the file name into
    // ms_LoadFileName; SACore validated the file itself.
    std::memcpy(reinterpret_cast<char*>(adapter_.Address("CGenericGameStorage::ms_LoadFileName")), path.c_str(),
                path.size() + 1);
    auto* menu = reinterpret_cast<uint8_t*>(adapter_.Address(adapters::kMenuManager));
    if (state == 9) {
        reinterpret_cast<void(__thiscall*)(void*)>(adapter_.Address(adapters::kStartNewGameRoutine))(menu);
    }
    menu[0x32] = 1;  // m_bShutDownFrontEndRequested
    *reinterpret_cast<uint8_t*>(adapter_.Address("CGame::bMissionPackGame")) = 0;
    menu[0x60] = 1;  // m_bWantToLoad
    currentSave_ = name;
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "loading {} ({})", name, file.filename().string());
    return true;
}

void SaveService::BackupSlot(int slot, const char* reason) const {
    std::string error;
    const auto backup = BackupSaveFile(SlotFile(slot), dataDirectory_ / L"save-backups", Stamp(),
                                       static_cast<size_t>(std::max(1, settings_.backupsPerSlot)), error);
    if (backup) {
        logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "slot {} backed up ({}): {}", slot, reason,
                     backup->string());
    } else if (!error.empty()) {
        logger_.Logf(LogLevel::Error, LogCategory::GameAdapter, "saves", "slot {} backup FAILED: {}", slot, error);
    }
}

bool SaveService::SaveToSlot(int slot) {
    if (slot < 1 || slot > kSlots || !adapter_.Available(adapters::kCapSaveWriteSlot)) {
        return false;
    }
    const auto saveSlot = reinterpret_cast<SaveSlotFn>(adapter_.Address(adapters::kSaveSlot));
    const char result = saveSlot(static_cast<char>(slot - 1));
    logger_.Logf(result == 0 ? LogLevel::Info : LogLevel::Error, LogCategory::GameAdapter, "saves",
                 "save to slot {} requested by SACore: game returned {}", slot, static_cast<int>(result));
    return result == 0;
}

void SaveService::SetModValue(const std::string& mod, const std::string& key, const std::string& value) {
    sideCar_[mod][key] = value;
}

std::optional<std::string> SaveService::GetModValue(const std::string& mod, const std::string& key) const {
    const auto m = sideCar_.find(mod);
    if (m == sideCar_.end()) {
        return std::nullopt;
    }
    const auto k = m->second.find(key);
    return k == m->second.end() ? std::nullopt : std::optional(k->second);
}

std::filesystem::path SaveService::SideCarDirectory(const std::string& name) const {
    return dataDirectory_ / L"savedata" / name;
}

void SaveService::WriteSideCar(const std::string& name) const {
    const std::filesystem::path directory = SideCarDirectory(name);
    std::error_code error;
    std::filesystem::remove_all(directory, error);  // data of a previous game in this slot must not survive
    if (sideCar_.empty()) {
        return;
    }
    std::filesystem::create_directories(directory, error);
    for (const auto& [mod, values] : sideCar_) {
        toml::table table;
        for (const auto& [key, value] : values) {
            table.insert_or_assign(key, value);
        }
        std::ofstream(directory / (mod + ".toml"), std::ios::binary) << table;
    }
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "side-car data of {} mod(s) stored for {}",
                 sideCar_.size(), SideCarLabel(name));
}

void SaveService::ReadSideCar(const std::string& name) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(SideCarDirectory(name), error)) {
        if (entry.path().extension() != ".toml") {
            continue;
        }
        try {
            const toml::table table = toml::parse_file(entry.path().string());
            auto& values = sideCar_[entry.path().stem().string()];
            for (const auto& [key, node] : table) {
                if (auto text = node.value<std::string>()) {
                    values[std::string(key.str())] = *text;
                }
            }
        } catch (const toml::parse_error& e) {
            logger_.Logf(LogLevel::Error, LogCategory::GameAdapter, "saves", "side-car {} unreadable: {}",
                         entry.path().string(), e.description());
        }
    }
    logger_.Logf(LogLevel::Info, LogCategory::GameAdapter, "saves", "side-car data of {} mod(s) loaded for {}",
                 sideCar_.size(), SideCarLabel(name));
}

void __cdecl SaveService::HookedMakeValidSaveName(int slotIndex) {
    g_origMakeValidSaveName(slotIndex);
    if (!g_saves) {
        return;
    }
    SaveService& self = *g_saves;
    if (self.pendingName_.empty()) {
        self.currentSlot_ = slotIndex + 1;
        self.currentSave_ = std::format("slot{}", slotIndex + 1);
        return;
    }
    // Quick or auto save: replace "GTASAsfN" in the file name the game just built with the save's own file.
    auto* buffer = reinterpret_cast<char*>(self.adapter_.Address(adapters::kValidSaveName));
    const std::string target = self.FileFor(self.pendingName_).filename().replace_extension().string();
    std::string text(buffer);
    const size_t at = text.rfind("GTASAsf");
    if (at != std::string::npos && at + 8 <= text.size()) {
        text.replace(at, 8, target);
        if (text.size() < 256) {
            std::memcpy(buffer, text.c_str(), text.size() + 1);
            self.currentSave_ = self.pendingName_;
        }
    }
}

}  // namespace sacore
