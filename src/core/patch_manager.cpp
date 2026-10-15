#include "patch_manager.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace sacore {

std::string_view ToString(PatchState state) {
    switch (state) {
        case PatchState::Disabled: return "disabled";
        case PatchState::Installed: return "installed";
        case PatchState::Failed: return "failed";
        case PatchState::Removed: return "removed";
    }
    return "?";
}

PatchManager::PatchManager(Logger& logger) : logger_(logger) {}

PatchRecord& PatchManager::Record(const PatchInfo& info) {
    patches_.push_back(PatchRecord{info});
    return patches_.back();
}

bool PatchManager::InstallHook(const PatchInfo& info, void* target, void* detour, void** original) {
    if (IsDisabled(info.id)) {
        PatchRecord& record = Record(info);
        record.state = PatchState::Disabled;
        record.detail = "switched off in core.toml [patches]";
        logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} not installed: disabled by configuration",
                     info.id);
        return false;
    }
    const auto conflict = std::find_if(patches_.begin(), patches_.end(), [&](const PatchRecord& p) {
        return p.hook && p.target == target && p.state == PatchState::Installed;
    });
    if (conflict != patches_.end()) {
        // Format before Record(): push_back may reallocate and invalidate `conflict`.
        std::string detail = std::format("conflict: {} is already hooked by patch '{}' of '{}'", info.target,
                                         conflict->info.id, conflict->info.owner);
        PatchRecord& record = Record(info);
        record.detail = std::move(detail);
        logger_.Logf(LogLevel::Error, LogCategory::Patching, info.owner, "{} refused, {}. {}", info.id, record.detail,
                     info.onFailure);
        return false;
    }
    const bool alreadyJump = target && *static_cast<const uint8_t*>(target) == 0xE9;
    MH_STATUS status = target ? MH_CreateHook(target, detour, original) : MH_ERROR_NOT_EXECUTABLE;
    if (status == MH_OK) {
        status = MH_EnableHook(target);
    }
    PatchRecord& record = Record(info);
    record.target = target;
    record.hook = true;
    if (status == MH_OK) {
        record.state = PatchState::Installed;
        record.detail = alreadyJump ? "chained after another plugin's hook" : "installed";
        logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} on {} at {:#010x}: {}", info.id,
                     info.target, reinterpret_cast<uintptr_t>(target), record.detail);
        return true;
    }
    record.detail = std::format("MinHook error {}", MH_StatusToString(status));
    logger_.Logf(LogLevel::Error, LogCategory::Patching, info.owner, "{} on {} failed: {}. {}", info.id, info.target,
                 record.detail, info.onFailure);
    return false;
}

namespace {

// Finds the main module's import address table slot for dll!function, by name.
void** FindImportSlot(const char* dll, const char* function) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) {
        return nullptr;
    }
    for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
         descriptor->Name != 0; ++descriptor) {
        if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), dll) != 0 ||
            descriptor->OriginalFirstThunk == 0) {
            continue;
        }
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                continue;
            }
            const auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0) {
                return reinterpret_cast<void**>(&slots->u1.Function);
            }
        }
    }
    return nullptr;
}

}  // namespace

bool PatchManager::GameImports(const char* dll, const char* function) {
    return FindImportSlot(dll, function) != nullptr;
}

bool PatchManager::InstallImportHook(const PatchInfo& info, const char* dll, const char* function, void* detour,
                                     void** original) {
    if (IsDisabled(info.id)) {
        PatchRecord& record = Record(info);
        record.state = PatchState::Disabled;
        record.detail = "switched off in core.toml [patches]";
        logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} not installed: disabled by configuration",
                     info.id);
        return false;
    }
    void** slot = FindImportSlot(dll, function);
    PatchRecord& record = Record(info);
    if (!slot) {
        record.detail = std::format("the game does not import {}!{} by name", dll, function);
        logger_.Logf(LogLevel::Error, LogCategory::Patching, info.owner, "{} failed: {}. {}", info.id, record.detail,
                     info.onFailure);
        return false;
    }
    *original = *slot;
    record.target = slot;
    record.original.resize(sizeof(void*));
    std::memcpy(record.original.data(), slot, sizeof(void*));
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        record.detail = std::format("cannot change page protection (error {})", GetLastError());
        logger_.Logf(LogLevel::Error, LogCategory::Patching, info.owner, "{} failed: {}", info.id, record.detail);
        return false;
    }
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    record.state = PatchState::Installed;
    record.detail = std::format("import entry replaced (previous target {:#010x})", reinterpret_cast<uintptr_t>(*original));
    logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} on {}!{}: {}", info.id, dll, function,
                 record.detail);
    return true;
}

bool PatchManager::WriteMemory(const PatchInfo& info, void* target, const void* bytes, size_t size) {
    if (IsDisabled(info.id)) {
        PatchRecord& record = Record(info);
        record.state = PatchState::Disabled;
        record.detail = "switched off in core.toml [patches]";
        logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} not applied: disabled by configuration",
                     info.id);
        return false;
    }
    PatchRecord& record = Record(info);
    record.target = target;
    DWORD oldProtect = 0;
    if (!target || !VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        record.detail = std::format("cannot change page protection (error {})", GetLastError());
        logger_.Logf(LogLevel::Error, LogCategory::Patching, info.owner, "{} failed: {}. {}", info.id, record.detail,
                     info.onFailure);
        return false;
    }
    record.original.assign(static_cast<const uint8_t*>(target), static_cast<const uint8_t*>(target) + size);
    std::memcpy(target, bytes, size);
    VirtualProtect(target, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    record.state = PatchState::Installed;
    record.detail = std::format("{} byte(s) written", size);
    logger_.Logf(LogLevel::Info, LogCategory::Patching, info.owner, "{} on {} at {:#010x}: {}", info.id, info.target,
                 reinterpret_cast<uintptr_t>(target), record.detail);
    return true;
}

void PatchManager::Revert(PatchRecord& record) {
    if (record.state != PatchState::Installed) {
        return;
    }
    if (record.hook) {
        MH_DisableHook(record.target);
        MH_RemoveHook(record.target);
    } else if (!record.original.empty() && record.info.revertOnRemove) {
        DWORD oldProtect = 0;
        if (VirtualProtect(record.target, record.original.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(record.target, record.original.data(), record.original.size());
            VirtualProtect(record.target, record.original.size(), oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), record.target, record.original.size());
        }
    }
    record.state = PatchState::Removed;
}

bool PatchManager::Remove(const std::string& id) {
    for (auto it = patches_.rbegin(); it != patches_.rend(); ++it) {
        if (it->info.id == id && it->state == PatchState::Installed) {
            Revert(*it);
            logger_.Logf(LogLevel::Info, LogCategory::Patching, it->info.owner, "{} removed", id);
            return true;
        }
    }
    return false;
}

void PatchManager::RemoveAll() {
    for (auto it = patches_.rbegin(); it != patches_.rend(); ++it) {
        Revert(*it);
    }
}

const PatchRecord* PatchManager::Find(const std::string& id) const {
    for (auto it = patches_.rbegin(); it != patches_.rend(); ++it) {
        if (it->info.id == id) {
            return &*it;
        }
    }
    return nullptr;
}

std::string PatchManager::StatusReport() const {
    std::string text;
    for (const PatchRecord& p : patches_) {
        text += std::format("{:<34} {:<10} {:<10} {} | {} | reason: {} | if missing: {}\n", p.info.id, p.info.owner,
                            ToString(p.state), p.info.target, p.detail, p.info.reason, p.info.onFailure);
    }
    return text;
}

}  // namespace sacore
