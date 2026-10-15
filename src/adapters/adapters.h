// Adapter data for every supported build. Addresses and their evidence: docs/ADDRESSES.md.
#pragma once

#include "game/adapter.h"

#include <string_view>

namespace sacore::adapters {

// Symbol names shared by all builds.
inline constexpr std::string_view kGameState = "gGameState";
inline constexpr std::string_view kMenuManager = "FrontEndMenuManager";
inline constexpr std::string_view kStartNewGameRoutine = "CMenuManager::DoSettingsBeforeStartingAGame";
inline constexpr std::string_view kPlayers = "CWorld::Players";
inline constexpr std::string_view kCurrArea = "CGame::currArea";
inline constexpr std::string_view kGenericSave = "CGenericGameStorage::GenericSave";
inline constexpr std::string_view kGenericLoad = "CGenericGameStorage::GenericLoad";
inline constexpr std::string_view kMakeValidSaveName = "CGenericGameStorage::MakeValidSaveName";
inline constexpr std::string_view kSaveSlot = "C_PcSave::SaveSlot";
inline constexpr std::string_view kSetSaveDirectory = "C_PcSave::SetSaveDirectory";
inline constexpr std::string_view kRsEventHandler = "RsEventHandler";
inline constexpr std::string_view kGetUserFilesFolder = "GetUserFilesFolder";
inline constexpr std::string_view kValidSaveName = "CGenericGameStorage::ms_ValidSaveName";
inline constexpr std::string_view kExitRwTerminateCall = "WinMain: RsEventHandler(rsRWTERMINATE) on exit";

// Capability ids. "engine.*" capabilities need no game addresses.
inline constexpr std::string_view kCapWindowed = "engine.windowed";
inline constexpr std::string_view kCapBackground = "engine.run_in_background";
inline constexpr std::string_view kCapMute = "engine.start_muted";
inline constexpr std::string_view kCapGameState = "game.state.read";
inline constexpr std::string_view kCapIntroSkip = "game.intro.skip";
inline constexpr std::string_view kCapStartNewGame = "game.menu.start_new_game";
inline constexpr std::string_view kCapPlayerRead = "player.read";
inline constexpr std::string_view kCapMoneyWrite = "player.money.write";
inline constexpr std::string_view kCapAreaRead = "world.area.read";
inline constexpr std::string_view kCapSaveEvents = "save.events";
inline constexpr std::string_view kCapSaveSlotTracking = "save.slot_tracking";
inline constexpr std::string_view kCapSaveWriteSlot = "save.write_slot";
inline constexpr std::string_view kCapSaveExtraSlots = "save.extra_slots";
inline constexpr std::string_view kCapSeparateUserFiles = "save.separate_user_files";
inline constexpr std::string_view kCapExitCrashGuard = "stability.exit_crash_guard";
inline constexpr std::string_view kCapScriptThreadGuard = "stability.script_thread_guard";
inline constexpr std::string_view kStartBrain = "CScriptsForBrains::StartNewStreamedScriptBrain";
inline constexpr std::string_view kIdleScripts = "CTheScripts::pIdleScripts";
inline constexpr std::string_view kCapParallelTestCopies = "testing.parallel_copies";
inline constexpr std::string_view kCdStreamThread = "CdStreamThread";
inline constexpr std::string_view kCdStreamSemaphore = "CdStream::gStreamSemaphore";
inline constexpr std::string_view kCdStreamThreadHandle = "CdStream::gStreamThread";
inline constexpr std::string_view kCdStreamThreadId = "CdStream::gStreamThreadId";
inline constexpr std::string_view kCdStreamQueueHead = "CdStream::gChannelRequestQ.head";
inline constexpr std::string_view kCdStreamQueueTail = "CdStream::gChannelRequestQ.tail";
inline constexpr std::string_view kCapCdStreamSync = "stability.cdstream_sync";
inline constexpr std::string_view kCdStreamSync = "CdStreamSync";
inline constexpr std::string_view kCdStreamChannels = "CdStream::gpChannels";
inline constexpr std::string_view kCdStreamThreaded = "CdStream::gbThreaded";
inline constexpr std::string_view kCapGameTick = "game.tick";

const BuildAdapterData& NewsteamR2Lv();
const BuildAdapterData& Us10Hoodlum();

// Adapter data for a build id, or nullptr.
const BuildAdapterData* FindAdapterData(std::string_view buildId);

}  // namespace sacore::adapters
