#include "adapters.h"

namespace sacore::adapters {
namespace {

constexpr std::string_view kNeedsGameState[] = {kGameState};
constexpr std::string_view kNeedsMenu[] = {kGameState, kMenuManager, kStartNewGameRoutine};
constexpr std::string_view kNeedsPlayers[] = {kPlayers};
constexpr std::string_view kNeedsArea[] = {kCurrArea};
constexpr std::string_view kNeedsSave[] = {kGenericSave, kGenericLoad};
constexpr std::string_view kNeedsSlotName[] = {kMakeValidSaveName};
constexpr std::string_view kNeedsSaveSlot[] = {kMakeValidSaveName, kSaveSlot, kGenericSave};
constexpr std::string_view kNeedsExtraSlots[] = {kMakeValidSaveName, kSaveSlot, kGenericSave, kValidSaveName};
constexpr std::string_view kNeedsSaveDirectory[] = {kSetSaveDirectory, kGetUserFilesFolder};
constexpr std::string_view kNeedsExitTeardown[] = {kRsEventHandler, kExitRwTerminateCall};
constexpr std::string_view kNeedsScriptThreadGuard[] = {kStartBrain, kIdleScripts};
constexpr std::string_view kNeedsParallelCopies[] = {kCdStreamThread, kCdStreamSemaphore, kCdStreamThreadHandle,
                                                     kCdStreamThreadId, kCdStreamQueueHead, kCdStreamQueueTail};
constexpr std::string_view kNeedsCdStreamSync[] = {kCdStreamSync, kCdStreamChannels, kCdStreamThreaded};

// Verification status per build comes from the runtime tests recorded in docs/COMPATIBILITY.md.
constexpr std::string_view kNeedsQol0[] = {"CGame::Process"};
constexpr std::string_view kNeedsQol1[] = {"CTheScripts::OnAMissionFlag", "CTheScripts::ScriptSpace", "CTheScripts::bMiniGameInProgress", "CCutsceneMgr::ms_running", "CPad::GetPad"};
constexpr std::string_view kNeedsQol2[] = {"CHud::SetHelpMessage"};
constexpr std::string_view kNeedsQol3[] = {"CGenericGameStorage::MakeValidSaveName", "CGenericGameStorage::GenericSave", "CGenericGameStorage::ms_ValidSaveName", "PcSaveHelper", "GetUserFilesFolder"};
constexpr std::string_view kNeedsQol4[] = {"FrontEndMenuManager", "CGenericGameStorage::CheckSlotDataValid", "CGame::bMissionPackGame", "CGenericGameStorage::ms_LoadFileName", "CMenuManager::DoSettingsBeforeStartingAGame"};
constexpr std::string_view kNeedsQol5[] = {"CStats::StatTypesInt", "CStats::StatTypesFloat", "CStats::GetStatValue"};
constexpr std::string_view kNeedsQol6[] = {"CStats::IncrementStat", "CStats::DecrementStat"};
constexpr std::string_view kNeedsQol7[] = {"FrontEndMenuManager", "CRadar::ms_RadarTrace", "CStreaming::LoadScene", "CStreaming::LoadSceneCollision", "CWorld::FindGroundZFor3DCoord", "CRunningScript::SetCharCoordinates", "CRadar::ClearBlip", "FindPlayerPed"};
constexpr std::string_view kNeedsQol8[] = {"CGameLogic::SetUpSkip", "CGameLogic::SkipState", "CRadar::ms_RadarTrace"};
constexpr std::string_view kNeedsQol9[] = {"CClock::ms_nGameClockMinutes", "CClock::ms_nGameClockHours", "CClock::ms_nLastClockTick", "CClock::ms_nMillisecondsPerGameMinute", "CClock::SetGameClock", "CTimer::m_snTimeInMilliseconds"};
constexpr std::string_view kNeedsQol10[] = {"CWeather::ForceWeatherNow", "CWeather::ReleaseWeather", "CWeather::ForcedWeatherType"};
constexpr std::string_view kNeedsQol11[] = {"CWanted::SetWantedLevel", "CWanted::WorkOutPolicePresence", "CCheat::m_aCheatsActive"};
constexpr std::string_view kNeedsQol12[] = {"CHeli::bPoliceHelisAllowed", "CHeli::ProcessControl police shot interval"};
constexpr std::string_view kNeedsQol13[] = {"CAutomobile::ProcessControlInputs", "CPad::GetPad", "CPad::GetSteeringLeftRight"};
constexpr std::string_view kNeedsQol14[] = {"CAutomobile::VehicleDamage upside-down gate"};
constexpr std::string_view kNeedsQol15[] = {"CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment", "CPickups::CollectPickupWindow"};

constexpr std::string_view kNeedsHud[] = {"CHud::Draw", "CHud::DrawAfterFade"};
constexpr std::string_view kNeedsFreeCamera[] = {"TheCamera", "CCamera::SetCamPositionForFixedMode", "CCamera::TakeControlNoEntity", "CCamera::Restore", "CTimer::ms_fTimeScale"};
constexpr std::string_view kNeedsAim[] = {"CCamera::m_fMouseAccelHorzntl", "CCamera::m_fMouseAccelVertical", "CCam::Process_AimWeapon", "CCam::Process_1rstPersonPedOnPC", "CCam::Process_M16_1stPerson", "CCam::Process_Rocket", "CPad::UpdateMouse", "CPad::NewMouseControllerState"};
constexpr std::string_view kNeedsDensity[] = {"CPopulation::PedDensityMultiplier", "CCarCtrl::CarDensityMultiplier", "CRenderer::ms_lodDistScale"};
constexpr std::string_view kNeedsRadarZoom[] = {"CRadar::m_radarRange", "CRadar::DrawRadarMap"};
constexpr std::string_view kNeedsGps[] = {"CRadar::TransformRealWorldPointToRadarSpace", "CRadar::TransformRadarPointToScreenSpace", "CRadar::ms_RadarTrace", "FrontEndMenuManager"};
constexpr std::string_view kNeedsWeaponSelect[] = {"CPed::SetCurrentWeapon", "CTimer::ms_fTimeScale"};
constexpr std::string_view kNeedsRadio[] = {"AudioEngine", "CAudioEngine::RetuneRadio"};
constexpr std::string_view kNeedsFrameLimit[] = {"RsGlobal.frameLimit", "FrontEndMenuManager"};
constexpr std::string_view kNeedsFov[] = {"CDraw::SetFOV", "CDraw::ms_fFOV", "CDraw::ms_fAspectRatio"};
constexpr std::string_view kNeedsPads[] = {"CPad::UpdatePads", "CPad::Pads"};
constexpr std::string_view kNeedsStreamingMemory[] = {"CStreaming::ms_memoryAvailable", "CStreaming::desiredNumVehiclesLoaded"};
constexpr std::string_view kNeedsPreload[] = {"CStreaming::ms_memoryAvailable", "CStreaming::RequestModel",
                                              "CStreaming::SetModelIsDeletable", "CStreaming::ms_aInfoForModel",
                                              "CModelInfo::ms_modelInfoPtrs", "CStreaming::ms_numModelsRequested",
                                              "CStreaming::ms_memoryUsed"};
constexpr std::string_view kNeedsHighFps[] = {"CTimer::Update", "CTimer::ms_fTimeStep"};
constexpr std::string_view kNeedsCutsceneSkip[] = {"CCutsceneMgr::IsCutsceneSkipButtonBeingPressed", "CCutsceneMgr::ms_running"};
constexpr std::string_view kNeedsCallSkip[] = {"CTheScripts::pActiveScripts", "CTheScripts::ScriptSpace"};
constexpr std::string_view kNeedsMissionPass[] = {"ScriptDebugKeyPressed", "ScriptDebugKeyJustPressed", "CTheScripts::pActiveScripts",
                                                  "CTheScripts::OnAMissionFlag", "CTheScripts::ScriptSpace"};

constexpr CapabilitySpec kCapabilities[] = {
    {kCapWindowed, "Run in a window (Direct3D 9 + window style, no game addresses)", {}, true},
    {kCapBackground, "Keep running when the window loses focus", {}, true},
    {kCapMute, "Mute the game's audio session at start", {}, false},
    {kCapGameState, "Read the main loop state (menu, loading, playing)", kNeedsGameState, true},
    {kCapIntroSkip, "Skip the logo and intro movies", kNeedsGameState, true},
    {kCapStartNewGame, "Start a new game from the main menu (test automation)", kNeedsMenu, true},
    {kCapPlayerRead, "Read player health, armour, money, wanted level, position, area", kNeedsPlayers, true},
    {kCapMoneyWrite, "Change the player's money", kNeedsPlayers, true},
    {kCapAreaRead, "Read the current interior/area id", kNeedsArea, true},
    {kCapSaveEvents, "SaveStarted/SaveCompleted/LoadStarted/LoadCompleted events", kNeedsSave, true},
    {kCapSaveSlotTracking, "Know the slot of the current save or load (backups, side-car data)", kNeedsSlotName, true},
    {kCapSaveWriteSlot, "Save into a slot through the game's own routine (test harness)", kNeedsSaveSlot, true},
    {kCapSaveExtraSlots, "Quick and auto saves in their own files next to the 8 menu slots", kNeedsExtraSlots, true},
    {kCapSeparateUserFiles, "Separate User Files folder per install (registry read redirected, save path reset)",
     kNeedsSaveDirectory, true},
    {kCapExitCrashGuard, "Avoid the crash on exit: RenderWare teardown is left to the process exit",
     kNeedsExitTeardown, true},
    {kCapScriptThreadGuard, "Skip an object's script brain instead of crashing when all 96 script threads are in use",
     kNeedsScriptThreadGuard, true},
    {kCapCdStreamSync, "No freeze when a stream read ends while the game starts waiting for it (CdStreamSync race)",
     kNeedsCdStreamSync, true},
    {kCapParallelTestCopies, "Test runs: several copies of one build at once (private CdStream semaphore)",
     kNeedsParallelCopies, true},
    {"game.tick", "Game logic tick event (after CGame::Process)", kNeedsQol0, true},
    {"game.mission.read", "Know whether a mission, mini game or cutscene runs", kNeedsQol1, true},
    {"hud.help_message", "Show a message in the game's help box", kNeedsQol2, false},
    {"save.quick", "Save without the safe-house side effects (no time skip, no health refill, wanted level kept)", kNeedsQol3, true},
    {"save.load_slot", "Load a save from the main menu without input", kNeedsQol4, true},
    {"stats.read", "Read player stats (missions passed, skills)", kNeedsQol5, true},
    {"stats.scale", "Scale stat gains", kNeedsQol6, false},
    {"world.teleport", "Teleport the player (waypoint, fast travel, retry)", kNeedsQol7, true},
    {"world.trip_skip", "Offer the game's own trip skip during missions", kNeedsQol8, false},
    {"world.time", "Set, freeze and speed up the game clock", kNeedsQol9, true},
    {"world.weather", "Force or release the weather", kNeedsQol10, true},
    {"police.wanted", "Wanted level switches (never wanted, faster decay)", kNeedsQol11, false},
    {"police.heli", "Police helicopter switches", kNeedsQol12, false},
    {"vehicle.steering", "Steering return-to-centre control", kNeedsQol13, false},
    {"vehicle.upside_down", "No damage when a car lies on its roof", kNeedsQol14, false},
    {"pickups.weapon_key", "Weapons are picked up only on key press", kNeedsQol15, false},
    {"hud.toggle", "Hide the HUD (photos, video)", kNeedsHud, false},
    {"camera.free", "Free camera with slowed world (photo mode)", kNeedsFreeCamera, false},
    {"input.aim", "Aim sensitivity and per-axis invert", kNeedsAim, false},
    {"world.density", "Ped and traffic density, draw distance", kNeedsDensity, false},
    {"radar.zoom", "Radar zoom", kNeedsRadarZoom, false},
    {"radar.gps", "Route line to the map waypoint on the radar", kNeedsGps, false},
    {"player.weapon_select", "Select a weapon slot directly (weapon wheel)", kNeedsWeaponSelect, false},
    {"audio.radio", "Tune the vehicle radio (radio wheel)", kNeedsRadio, false},
    {"engine.frame_limit", "Choose the frame limit", kNeedsFrameLimit, false},
    {"camera.fov", "Field of view (Hor+ for wide screens)", kNeedsFov, false},
    {"input.pads", "Controller state (XInput layer)", kNeedsPads, false},
    {"streaming.memory", "Streaming memory budget (less pop-in)", kNeedsStreamingMemory, true},
    {"streaming.preload", "Preload models and textures and keep them loaded", kNeedsPreload, true},
    {"engine.high_fps", "Physics and timing fixes for frame rates above 30", kNeedsHighFps, false},
    {"cutscene.skip", "Skip cutscenes and skippable scripted scenes by key or automatically", kNeedsCutsceneSkip, true},
    {"phone.call_skip", "Answer and end phone calls the way the call script does", kNeedsCallSkip, true},
    {"mission.pass", "Pass the current mission through the game's debug skip or its pass code", kNeedsMissionPass, true},
};

// newsteam r2 German low-violence. Sources: own Ghidra analysis of gta-sa.exe.
constexpr SymbolSpec kNewsteamSymbols[] = {
    {kGameState, 0x919020, SymbolCheck::Instruction, 0, "C7 05", "02 00 00 00"},
    {kMenuManager, 0x833938, SymbolCheck::CodeReferences, 20, "", ""},
    // __thiscall, no stack arguments: clears pad state, closes the menu, sets m_bWantToRestart (+0x5D).
    {kStartNewGameRoutine, 0x186E90, SymbolCheck::Prologue, 0, "33 C0 56 8B F1 A3", ""},
    {kPlayers, 0x8100D0, SymbolCheck::CodeReferences, 10, "", ""},
    {kCurrArea, 0x7FFBE4, SymbolCheck::CodeReferences, 3, "", ""},
    {kGenericSave, 0x1ECD50, SymbolCheck::Prologue, 0, "55 8B EC 81 EC 38 01 00 00", ""},
    {kGenericLoad, 0x1ED120, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 81 EC 7C 02 00 00", ""},
    {kMakeValidSaveName, 0x1EC7D0, SymbolCheck::Prologue, 0, "55 8B EC 81 EC 04 01 00 00", ""},
    {kSaveSlot, 0x236D40, SymbolCheck::Prologue, 0, "55 8B EC 0F BE 45 08 50 E8", ""},
    {kSetSaveDirectory, 0x236D20, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 68", ""},
    {kRsEventHandler, 0x237900, SymbolCheck::Prologue, 0, "55 8B EC 56 8B 75 0C 57 8B 7D 08 56", ""},
    {kStartBrain, 0x706B0, SymbolCheck::Prologue, 0, "55 8B EC 0F B6 45 08 53 56 8D 04 80 8D 34 81", ""},
    {kIdleScripts, 0x6C02CC, SymbolCheck::CodeReferences, 8, "", ""},
    {kCdStreamThread, 0x65F0, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 08 53 8B 1D ?? ?? ?? ??", ""},
    {kCdStreamSemaphore, 0x55BBFC, SymbolCheck::CodeReferences, 4, "", ""},
    {kCdStreamThreadHandle, 0x55BC00, SymbolCheck::CodeReferences, 4, "", ""},
    {kCdStreamThreadId, 0x55BBF8, SymbolCheck::CodeReferences, 1, "", ""},
    {kCdStreamQueueHead, 0x55BBE8, SymbolCheck::CodeReferences, 4, "", ""},
    {kCdStreamQueueTail, 0x55BBEC, SymbolCheck::CodeReferences, 6, "", ""},
    {kCdStreamSync, 0x64E0, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 8B 0D ?? ?? ?? ?? 8D 04 40 03 C0 83 3D", ""},
    {kCdStreamChannels, 0x55BBF4, SymbolCheck::CodeReferences, 12, "", ""},
    {kCdStreamThreaded, 0x55BBDC, SymbolCheck::CodeReferences, 6, "", ""},
    // Builds "<Personal>\\GTA San Andreas User Files" into a static buffer and returns it (own decompile).
    // 256-byte buffer MakeValidSaveName fills with "<dir>\\GTASAsfN.b" (own disassembly; same code as 1.0 US).
    {kValidSaveName, 0x8A3B50, SymbolCheck::Instruction, 0, "B8", ""},
    {kGetUserFilesFolder, 0x378E90, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 0C 8D 45 FC 50 68 19 00 02 00", ""},
    // push 0x16 (rsRWTERMINATE); call RsEventHandler, after the main loop, before DestroyWindow (WinMain decompile).
    {kExitRwTerminateCall, 0x37D557, SymbolCheck::Prologue, 0, "6A 16 E8", ""},
    // QoL features
    {"CTimer::m_snTimeInMilliseconds", 0x80FD74, SymbolCheck::CodeReferences, 600, "", ""},
    {"CTimer::ms_fTimeStep", 0x80FD50, SymbolCheck::CodeReferences, 300, "", ""},
    {"CGame::Process", 0x14BBB0, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 10 53 56 57 E8 ?? ?? ?? ??", ""},
    {"CCheat::m_aCheatsActive", 0x5E0C58, SymbolCheck::CodeReferences, 6, "", ""},
    {"CTheScripts::OnAMissionFlag", 0x6BF01C, SymbolCheck::CodeReferences, 5, "", ""},
    {"CTheScripts::ScriptSpace", 0x6C12D0, SymbolCheck::CodeReferences, 40, "", ""},
    {"CTheScripts::pActiveScripts", 0x702D98, SymbolCheck::CodeReferences, 12, "", ""},
    {"CTheScripts::bMiniGameInProgress", 0x6BBE28, SymbolCheck::CodeReferences, 1, "", ""},
    {"CCutsceneMgr::ms_running", 0x7D6F3D, SymbolCheck::CodeReferences, 20, "", ""},
    {"CCutsceneMgr::IsCutsceneSkipButtonBeingPressed", 0xDFE40, SymbolCheck::Prologue, 0, "6A 00 E8 ?? ?? ?? ?? 83 C4 04 66 83 78 20 00", ""},
    {"ScriptDebugKeyPressed", 0x167B30, SymbolCheck::Prologue, 0, "32 C0 C2 0C 00", ""},
    {"ScriptDebugKeyJustPressed", 0x167B20, SymbolCheck::Prologue, 0, "32 C0 C2 0C 00", ""},
    // QoL batch 2
    {"CHud::Draw", 0x19C380, SymbolCheck::Prologue, 0, "80 3D ?? ?? ?? ?? 01 0F 84 3E 02 00 00", ""},
    {"CHud::DrawAfterFade", 0x199AC0, SymbolCheck::Prologue, 0, "A1 ?? ?? ?? ?? 8B 48 20 6A 01 6A 09 FF D1 8B 15 ?? ?? ?? ??", ""},
    {"TheCamera", 0x7FC448, SymbolCheck::CodeReferences, 150, "", ""},
    {"CCamera::SetCamPositionForFixedMode", 0x118590, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 8B 10 89 91 6C 08 00 00", ""},
    {"CCamera::TakeControlNoEntity", 0x118FB0, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 10 BA 01 00 00 00 83 F8 02", ""},
    {"CCamera::Restore", 0x117FF0, SymbolCheck::Prologue, 0, "D9 EE 53 56 8B F1 33 DB D9 9E 40 01 00 00", ""},
    {"CTimer::ms_fTimeScale", 0x80FD58, SymbolCheck::CodeReferences, 12, "", ""},
    {"CCamera::m_fMouseAccelHorzntl", 0x7FC05C, SymbolCheck::CodeReferences, 9, "", ""},
    {"CCamera::m_fMouseAccelVertical", 0x7FC058, SymbolCheck::CodeReferences, 3, "", ""},
    {"CCam::Process_AimWeapon", 0x12EC50, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 60 F6 05 ?? ?? ?? ?? 01 56 8B F1 75 1B", ""},
    {"CCam::Process_1rstPersonPedOnPC", 0x11B4C0, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 50 F6 05 ?? ?? ?? ?? 01", ""},
    {"CCam::Process_M16_1stPerson", 0x11D050, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 4C 56 8B F1 8B 8E 1C 02 00 00", ""},
    {"CCam::Process_Rocket", 0x11E790, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 30 53 56 8B F1 8B 96 1C 02 00 00", ""},
    {"CPad::UpdateMouse", 0x14FDA0, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 18 56 8B F1 E8 ?? ?? ?? ??", ""},
    {"CPad::NewMouseControllerState", 0x8007B8, SymbolCheck::CodeReferences, 12, "", ""},
    {"CPad::Pads", 0x8007E8, SymbolCheck::CodeReferences, 8, "", ""},
    {"CPad::UpdatePads", 0x152CA0, SymbolCheck::Prologue, 0, "56 57 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 6A 00 E8", ""},
    {"CPopulation::PedDensityMultiplier", 0x545CB0, SymbolCheck::CodeReferences, 5, "", ""},
    {"CCarCtrl::CarDensityMultiplier", 0x5141A4, SymbolCheck::CodeReferences, 5, "", ""},
    {"CRenderer::ms_lodDistScale", 0x540130, SymbolCheck::CodeReferences, 3, "", ""},
    {"CRadar::m_radarRange", 0x836494, SymbolCheck::CodeReferences, 7, "", ""},
    {"CRadar::DrawRadarMap", 0x1A3C10, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 34 53 56 57 E8", ""},
    {"CRadar::TransformRealWorldPointToRadarSpace", 0x1A0600, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 08 8B 45 0C D9 00 D8 25", ""},
    {"CRadar::TransformRadarPointToScreenSpace", 0x1A0560, SymbolCheck::Prologue, 0, "55 8B EC 80 3D ?? ?? ?? ?? 00 8B 45 0C", ""},
    {"CPed::SetCurrentWeapon", 0x2024F0, SymbolCheck::Prologue, 0, "55 8B EC 53 8B 5D 08 56 8B F1 83 FB FF", ""},
    {"AudioEngine", 0x53C998, SymbolCheck::CodeReferences, 150, "", ""},
    {"CAudioEngine::RetuneRadio", 0x114060, SymbolCheck::Prologue, 0, "55 8B EC B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 75 0B B9 ?? ?? ?? ?? 5D E9", ""},
    {"RsGlobal.frameLimit", 0x8A3DB8, SymbolCheck::CodeReferences, 2, "", ""},
    {"CDraw::SetFOV", 0x334130, SymbolCheck::Prologue, 0, "55 8B EC D9 45 08 D9 1D ?? ?? ?? ?? 5D C3", ""},
    {"CDraw::ms_fFOV", 0x54CC04, SymbolCheck::CodeReferences, 6, "", ""},
    {"CDraw::ms_fAspectRatio", 0x8CEF30, SymbolCheck::CodeReferences, 8, "", ""},
    {"CStreaming::ms_memoryAvailable", 0x514074, SymbolCheck::CodeReferences, 3, "", ""},
    {"CStreaming::desiredNumVehiclesLoaded", 0x514078, SymbolCheck::CodeReferences, 2, "", ""},
    {"CStreaming::RequestModel", 0x8B00, SymbolCheck::Prologue, 0, "55 8B EC 53 8B 5D 08 56 57 8D 3C 9B", ""},
    {"CStreaming::SetModelIsDeletable", 0xA060, SymbolCheck::Prologue, 0,
     "55 8B EC 56 8B 75 08 8D 04 B6 03 C0 03 C0 80 A0 ?? ?? ?? ?? FD", ""},
    {"CStreaming::ms_aInfoForModel", 0x55C8A0, SymbolCheck::CodeReferences, 90, "", ""},
    {"CModelInfo::ms_modelInfoPtrs", 0x712818, SymbolCheck::CodeReferences, 600, "", ""},
    {"CStreaming::ms_numModelsRequested", 0x55C89C, SymbolCheck::CodeReferences, 10, "", ""},
    {"CStreaming::ms_memoryUsed", 0x55C898, SymbolCheck::CodeReferences, 15, "", ""},
    {"CWanted::SetMaximumWantedLevel", 0x17AEF0, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 83 F8 06 0F 87 97 00 00 00", ""},
    {"CVehicle::m_bEnableMouseSteering", 0x8B92D2, SymbolCheck::CodeReferences, 5, "", ""},
    {"CWorld::FindGroundZForCoord", 0x182920, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 38 D9 45 08 6A 00 6A 00", ""},
    {"Idle frame time cap variable", 0x53FD78, SymbolCheck::CodeReferences, 1, "", ""},
    {"CRadar::SetCoordBlip", 0x1A0930, SymbolCheck::Prologue, 0, "55 8B EC 56 B1 02 33 D2 33 F6 84 0D", ""},
    {"CCutsceneMgr::ms_cutsceneLoadStatus", 0x7D6F38, SymbolCheck::CodeReferences, 8, "", ""},
    {"CTheScripts::StartNewScript", 0x69CB0, SymbolCheck::Prologue, 0, "55 8B EC 8B 0D ?? ?? ?? ?? 8B 41 04", ""},
    // High frame rate fixes, bytes read from the executable
    {"CTimer::Update", 0x17A850, SymbolCheck::Prologue, 0, "55 8B EC 8B 0D ??", ""},
    {"FPS doors add velocity", 0x2F5D4D, SymbolCheck::Prologue, 0, "D8 46 14 8B CA", ""},
    {"FPS doors damping ladder", 0x2F5CBE, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS doors damping 0.97", 0x2F5CE0, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS doors damping boat", 0x2F5CAB, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS aim walk step", 0x23DACD, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS swim speed", 0x2B73CB, SymbolCheck::Prologue, 0, "D9 45 08 D9 C0", ""},
    {"FPS dive speed", 0x2B72BD, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS dive surface", 0x2B7398, SymbolCheck::Prologue, 0, "DC 05 ?? ?? ?? ??", ""},
    {"FPS swim surface clamp", 0x2B76C0, SymbolCheck::Prologue, 0, "DD 05 ?? ?? ?? ??", ""},
    {"FPS swim pitch damping 1", 0x2B758D, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS swim pitch damping 2", 0x2B75E5, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS swim pitch damping 3", 0x2B7676, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS buoyancy cutoff", 0x2F6A91, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS swim bubbles call", 0x2B7CC9, SymbolCheck::Prologue, 0, "E8 A2 9D DF FF", ""},
    {"FPS swim wake call", 0x2B7DAB, SymbolCheck::Prologue, 0, "E8 C0 9C DF FF", ""},
    {"FPS swim splash call 1", 0x2B7E9C, SymbolCheck::Prologue, 0, "E8 3F 93 DF FF", ""},
    {"FPS swim splash call 2", 0x2B7F06, SymbolCheck::Prologue, 0, "E8 D5 92 DF FF", ""},
    {"FPS swim splash call 3", 0x2B7F68, SymbolCheck::Prologue, 0, "E8 73 92 DF FF", ""},
    {"FPS swim splash call 4", 0x2B7FC7, SymbolCheck::Prologue, 0, "E8 14 92 DF FF", ""},
    {"FPS skimmer resistance", 0x30C7CD, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS burnout torque", 0x2D2C26, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS wheel friction 1", 0x310DFD, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS wheel friction 2", 0x310E76, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS wheel friction 3", 0x3115DB, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS wheel friction 4", 0x311606, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS wheel friction 5", 0x311636, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS siren site", 0x31AAA1, SymbolCheck::Prologue, 0, "0F B6 0D ?? ??", ""},
    {"FPS siren exit horn", 0x31AB22, SymbolCheck::Prologue, 0, "C7 86 14 05 00 00", ""},
    {"FPS siren exit toggle", 0x31AAD7, SymbolCheck::Prologue, 0, "8A 86 2D 04 00 00", ""},
    {"FPS siren exit none", 0x31AB32, SymbolCheck::Prologue, 0, "C7 86 14 05 00 00", ""},
    {"CPad::GetHorn", 0x150800, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00", ""},
    {"CPad::HornJustDown", 0x150850, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00", ""},
    {"FPS heli rotor normal", 0x2F966E, SymbolCheck::Prologue, 0, "DC 05 ?? ?? ?? ??", ""},
    {"FPS heli rotor rc", 0x2F967C, SymbolCheck::Prologue, 0, "DC 05 ?? ?? ?? ??", ""},
    {"FPS ped push force", 0x15A549, SymbolCheck::Prologue, 0, "8B 4D AC 8B 55 B0", ""},
    {"FPS boat aquaplane", 0x31627E, SymbolCheck::Prologue, 0, "DC 0D ?? ?? ?? ??", ""},
    {"FPS climb settle", 0x2AD204, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS climb coarse", 0x2AD2F1, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS turn damping x", 0x155BCA, SymbolCheck::Prologue, 0, "DD 05 ?? ?? ?? ??", ""},
    {"FPS water cannon recycle", 0x35C814, SymbolCheck::Prologue, 0, "0F BF 46 04 40", ""},
    {"FPS water cannon recycle skip", 0x35C834, SymbolCheck::Prologue, 0, "D9 05 ?? ?? ?? ??", ""},
    {"FPS water cannon push counter", 0x35C904, SymbolCheck::Prologue, 0, "03 0D ?? ?? ?? ??", ""},
    {"FPS autopilot segment time", 0x1CA77, SymbolCheck::Prologue, 0, "E8 B4 72 43 00", ""},
    {"FPS autopilot entered time", 0x1CAA0, SymbolCheck::Prologue, 0, "E8 8B 72 43 00", ""},
    // on-rails wheel spin: fadd [esi+0x828..0x834] (FramerateVigilante CarWheelOnRailsSpinFix sites)
    {"Wheel spin add 0", 0x2E381F, SymbolCheck::Prologue, 0, "D8 86 28 08 00 00", ""},
    {"Wheel spin add 1", 0x2E382D, SymbolCheck::Prologue, 0, "D8 86 2C 08 00 00", ""},
    {"Wheel spin add 2", 0x2E383B, SymbolCheck::Prologue, 0, "D8 86 30 08 00 00", ""},
    {"Wheel spin add 3", 0x2E3847, SymbolCheck::Prologue, 0, "D8 86 34 08 00 00", ""},
    {"CPad::GetPad", 0x150470, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 69 C0 34 01 00 00", ""},
    {"CHud::SetHelpMessage", 0x194BE0, SymbolCheck::Prologue, 0, "55 8B EC 80 3D ?? ?? ?? ?? 00", ""},
    {"FindPlayerPed", 0x160E10, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 85 C0 79 07 0F B6 05 ?? ?? ?? ??", ""},
    {"PcSaveHelper", 0x8A3DA0, SymbolCheck::CodeReferences, 15, "", ""},
    {"CGenericGameStorage::ms_LoadFileName", 0x8A2D60, SymbolCheck::CodeReferences, 2, "", ""},
    {"CGenericGameStorage::CheckSlotDataValid", 0x1ECCF0, SymbolCheck::Prologue, 0, "55 8B EC 81 EC 04 01 00 00 56", ""},
    {"CGame::bMissionPackGame", 0x7FFBE0, SymbolCheck::CodeReferences, 25, "", ""},
    {"CStats::StatTypesInt", 0x80C228, SymbolCheck::CodeReferences, 2, "", ""},
    {"CStats::StatTypesFloat", 0x80C5A8, SymbolCheck::CodeReferences, 20, "", ""},
    {"CStats::LastMissionPassedName", 0x80BC28, SymbolCheck::CodeReferences, 6, "", ""},
    {"CRadar::ms_RadarTrace", 0x836858, SymbolCheck::CodeReferences, 5, "", ""},
    {"CStreaming::LoadScene", 0xFD60, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 1C 53 56 57 8B 7D 08", ""},
    {"CStreaming::LoadSceneCollision", 0xFF50, SymbolCheck::Prologue, 0, "55 8B EC 56 8B 75 08 8B 0E 8B 56 04", ""},
    {"CWorld::FindGroundZFor3DCoord", 0x182980, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 38 D9 45 08 6A 00 6A 00", ""},
    {"CRunningScript::SetCharCoordinates", 0x69E60, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 0C 56 57 8B 7D 08 F7 87 6C 04 00 00 00 01 00 00", ""},
    {"CRadar::ClearBlip", 0x1A5460, SymbolCheck::Prologue, 0, "55 8B EC 8B 55 08 83 FA FF 74 66 8B CA", ""},
    {"CGameLogic::SetUpSkip", 0x45170, SymbolCheck::Prologue, 0, "55 8B EC 83 3D ?? ?? ?? ?? 02", ""},
    {"CGameLogic::SkipState", 0x5E2358, SymbolCheck::CodeReferences, 20, "", ""},
    {"CStats::IncrementStat", 0x172C00, SymbolCheck::Prologue, 0, "55 8B EC D9 EE 83 EC 08 D9 45 0C D8 D1", ""},
    {"CStats::DecrementStat", 0x170750, SymbolCheck::Prologue, 0, "55 8B EC D9 EE D9 45 0C D8 D1 DF E0", ""},
    {"CStats::GetStatValue", 0x16F480, SymbolCheck::Prologue, 0, "55 8B EC 66 8B 45 08 66 83 F8 52 73 0C", ""},
    {"CClock::ms_nGameClockMinutes", 0x7FD4C6, SymbolCheck::CodeReferences, 25, "", ""},
    {"CClock::ms_nGameClockHours", 0x7FD4C7, SymbolCheck::CodeReferences, 60, "", ""},
    {"CClock::ms_nLastClockTick", 0x7FD4CC, SymbolCheck::CodeReferences, 7, "", ""},
    {"CClock::ms_nMillisecondsPerGameMinute", 0x7FD4D0, SymbolCheck::CodeReferences, 4, "", ""},
    {"CClock::SetGameClock", 0x13B1B0, SymbolCheck::Prologue, 0, "55 8B EC A1 ?? ?? ?? ?? 8A 4D 08 8A 55 0C", ""},
    {"CWeather::ForceWeatherNow", 0x35CA60, SymbolCheck::Prologue, 0, "55 8B EC 66 8B 45 08 66 A3 ?? ?? ?? ?? 66 A3", ""},
    {"CWeather::ReleaseWeather", 0x35CA80, SymbolCheck::Prologue, 0, "83 C8 FF 66 A3 ?? ?? ?? ?? C3", ""},
    {"CWeather::ForcedWeatherType", 0x90D068, SymbolCheck::CodeReferences, 6, "", ""},
    {"CWanted::SetWantedLevel", 0x17B4B0, SymbolCheck::Prologue, 0, "55 8B EC 80 3D ?? ?? ?? ?? 00 0F 85 A4 00 00 00", ""},
    {"CWanted::WorkOutPolicePresence", 0x17B640, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 18 53 8B 1D ?? ?? ?? ??", ""},
    {"CHeli::bPoliceHelisAllowed", 0x54B360, SymbolCheck::CodeReferences, 3, "", ""},
    {"CHeli::ProcessControl police shot interval", 0x2FBCA2, SymbolCheck::Prologue, 0, "83 F8 06 77 ?? FF 24 85 ?? ?? ?? ?? BB 3F 42 0F 00", ""},
    {"CAutomobile::ProcessControlInputs", 0x2DBBC0, SymbolCheck::Prologue, 0, "55 8B EC 83 EC 18 53 8A 5D 08 56 8B F1", ""},
    {"CPad::GetSteeringLeftRight", 0x150490, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00 75 23 0F B7 81 0A 01 00 00", ""},
    {"CAutomobile::VehicleDamage upside-down gate", 0x2D5537, SymbolCheck::Prologue, 0, "F6 86 68 08 00 00 08 0F 85", ""},
    {"CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment", 0x598E0, SymbolCheck::Prologue, 0, "55 8B EC 8B 45 08 6A 01 50 E8 ?? ?? ?? ??", ""},
    {"CPickups::CollectPickupWindow", 0x5F505C, SymbolCheck::CodeReferences, 8, "", ""},
};

// 1.0 US Hoodlum. Sources: plugin-sdk (Zlib) addresses, confirmed by own Ghidra analysis.
constexpr SymbolSpec kUs10Symbols[] = {
    {kGameState, 0x88D4C0, SymbolCheck::Instruction, 0, "C7 05", "02 00 00 00"},
    {kMenuManager, 0x7A6748, SymbolCheck::CodeReferences, 20, "", ""},
    {kStartNewGameRoutine, 0x173330, SymbolCheck::Prologue, 0, "56 8B F1 57 33 C0 B9 0E 00 00 00", ""},
    {kPlayers, 0x77CD98, SymbolCheck::CodeReferences, 10, "", ""},
    {kCurrArea, 0x772914, SymbolCheck::CodeReferences, 3, "", ""},
    {kGenericSave, 0x1D13E0, SymbolCheck::Prologue, 0, "81 EC 38 01 00 00", ""},
    {kGenericLoad, 0x1D17B0, SymbolCheck::Prologue, 0, "8B 44 24 04 81 EC 7C 02 00 00", ""},
    {kMakeValidSaveName, 0x1D0E90, SymbolCheck::Prologue, 0, "81 EC 04 01 00 00", ""},
    {kSaveSlot, 0x219060, SymbolCheck::Prologue, 0, "0F BE 44 24 04 50 E8", ""},
    {kSetSaveDirectory, 0x219040, SymbolCheck::Prologue, 0, "8B 44 24 04 68", ""},
    {kRsEventHandler, 0x219B60, SymbolCheck::Prologue, 0, "56 8B 74 24 0C 57 8B 7C 24 0C 56 57", ""},
    {kStartBrain, 0x6B270, SymbolCheck::Prologue, 0, "0F B6 44 24 04 53 56 8D 04 80 8D 34 81", ""},
    {kIdleScripts, 0x68B428, SymbolCheck::CodeReferences, 5, "", ""},
    {kCdStreamThread, 0x6560, SymbolCheck::Prologue, 0, "83 EC 08 53 55 8B 2D 98 80 85 00 56", ""},
    {kCdStreamSemaphore, 0x4E4004, SymbolCheck::CodeReferences, 3, "", ""},
    {kCdStreamThreadHandle, 0x4E4008, SymbolCheck::CodeReferences, 4, "", ""},
    {kCdStreamThreadId, 0x4E4000, SymbolCheck::CodeReferences, 1, "", ""},
    {kCdStreamQueueHead, 0x4E3FF0, SymbolCheck::CodeReferences, 4, "", ""},
    {kCdStreamQueueTail, 0x4E3FF4, SymbolCheck::CodeReferences, 3, "", ""},
    {kCdStreamSync, 0x6460, SymbolCheck::Prologue, 0, "E9 1B 69 16 01", ""},  // jump to the body at 0x156CD80
    {kCdStreamChannels, 0x4E3FFC, SymbolCheck::CodeReferences, 5, "", ""},
    {kCdStreamThreaded, 0x4E3FE4, SymbolCheck::CodeReferences, 3, "", ""},
    // Entry before the Hoodlum stub; the body (0x744FBD) fills the static buffer at 0xC92368.
    {kValidSaveName, 0x816DB8, SymbolCheck::Instruction, 0, "B8", ""},  // plugin-sdk ms_ValidSaveName
    {kGetUserFilesFolder, 0x344FB0, SymbolCheck::Prologue, 0, "83 EC 0C 8D 44 24 00 50 E9", ""},
    {kExitRwTerminateCall, 0x348E80, SymbolCheck::Prologue, 0, "6A 16 E8", ""},
    // QoL features
    {"CTimer::m_snTimeInMilliseconds", 0x77CB84, SymbolCheck::CodeReferences, 600, "", ""},
    {"CTimer::ms_fTimeStep", 0x77CB5C, SymbolCheck::CodeReferences, 300, "", ""},
    {"CGame::Process", 0x13BEE0, SymbolCheck::Prologue, 0, "83 EC 0C 53 56 57 E8 ?? ?? ?? ??", ""},
    {"CCheat::m_aCheatsActive", 0x569130, SymbolCheck::CodeReferences, 6, "", ""},
    {"CTheScripts::OnAMissionFlag", 0x6476AC, SymbolCheck::CodeReferences, 4, "", ""},
    {"CTheScripts::ScriptSpace", 0x649960, SymbolCheck::CodeReferences, 40, "", ""},
    {"CTheScripts::pActiveScripts", 0x68B42C, SymbolCheck::CodeReferences, 8, "", ""},
    {"CTheScripts::bMiniGameInProgress", 0x6444A8, SymbolCheck::CodeReferences, 1, "", ""},
    {"CCutsceneMgr::ms_running", 0x75F851, SymbolCheck::CodeReferences, 20, "", ""},
    {"CCutsceneMgr::IsCutsceneSkipButtonBeingPressed", 0x0D5D10, SymbolCheck::Prologue, 0, "6A 00 E8 ?? ?? ?? ?? 83 C4 04 E9", ""},
    {"ScriptDebugKeyPressed", 0x171980, SymbolCheck::Prologue, 0, "32 C0 C2 0C 00", ""},
    {"ScriptDebugKeyJustPressed", 0x171970, SymbolCheck::Prologue, 0, "32 C0 C2 0C 00", ""},
    // QoL batch 2
    {"CHud::Draw", 0x18FAE0, SymbolCheck::Prologue, 0, "80 3D ?? ?? ?? ?? 01 0F 84 26 02 00 00", ""},
    {"CHud::DrawAfterFade", 0x18D490, SymbolCheck::Prologue, 0, "A1 ?? ?? ?? ?? 6A 01 6A 09 FF 50 20 8B 0D ?? ?? ?? ?? 6A 03", ""},
    {"TheCamera", 0x76F028, SymbolCheck::CodeReferences, 150, "", ""},
    {"CCamera::SetCamPositionForFixedMode", 0x10BEC0, SymbolCheck::Prologue, 0, "8B 54 24 04 56 8B 32 90 E9", ""},
    {"CCamera::TakeControlNoEntity", 0x10C8B0, SymbolCheck::Prologue, 0, "8B 44 24 0C 83 F8 02 56 BA 01 00 00 00", ""},
    {"CCamera::Restore", 0x10B930, SymbolCheck::Prologue, 0, "53 33 DB 56 8B F1 57 53 6A FF C6 46 2B 01", ""},
    {"CTimer::ms_fTimeScale", 0x77CB64, SymbolCheck::CodeReferences, 12, "", ""},
    {"CCamera::m_fMouseAccelHorzntl", 0x76EC1C, SymbolCheck::CodeReferences, 9, "", ""},
    {"CCamera::m_fMouseAccelVertical", 0x76EC18, SymbolCheck::CodeReferences, 1, "", ""},
    {"CCam::Process_AimWeapon", 0x121500, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 83 EC 58 A8 01 56 8B F1 75 25", ""},
    {"CCam::Process_1rstPersonPedOnPC", 0x10EB70, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 83 EC 50 A8 01 53 56", ""},
    {"CCam::Process_M16_1stPerson", 0x1105C0, SymbolCheck::Prologue, 0, "83 EC 4C 56 8B F1 8B 8E 1C 02 00 00", ""},
    {"CCam::Process_Rocket", 0x111B50, SymbolCheck::Prologue, 0, "83 EC 28 53 55 56 8B F1 8B 96 1C 02 00 00", ""},
    {"CPad::UpdateMouse", 0x13F3C0, SymbolCheck::Prologue, 0, "83 EC 18 55 8B E9 E8 ?? ?? ?? ?? 84 C0", ""},
    {"CPad::NewMouseControllerState", 0x773418, SymbolCheck::CodeReferences, 12, "", ""},
    {"CPad::Pads", 0x773458, SymbolCheck::CodeReferences, 8, "", ""},
    {"CPad::UpdatePads", 0x141DD0, SymbolCheck::Prologue, 0, "56 57 E9", ""},
    {"CPopulation::PedDensityMultiplier", 0x4D2530, SymbolCheck::CodeReferences, 5, "", ""},
    {"CCarCtrl::CarDensityMultiplier", 0x4A5B20, SymbolCheck::CodeReferences, 5, "", ""},
    {"CRenderer::ms_lodDistScale", 0x4CD800, SymbolCheck::CodeReferences, 3, "", ""},
    {"CRadar::m_radarRange", 0x7A8314, SymbolCheck::CodeReferences, 7, "", ""},
    {"CRadar::DrawRadarMap", 0x186880, SymbolCheck::Prologue, 0, "83 EC 2C 53 55 56 57 E8", ""},
    {"CRadar::TransformRealWorldPointToRadarSpace", 0x183530, SymbolCheck::Prologue, 0, "83 EC 08 8B 44 24 10 D9 00 D8 25", ""},
    {"CRadar::TransformRadarPointToScreenSpace", 0x183480, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 8B 4C 24 04 84 C0 8B 44 24 08", ""},
    {"CPed::SetCurrentWeapon", 0x1E61F0, SymbolCheck::Prologue, 0, "53 8B 5C 24 08 83 FB FF 56 8B F1 74 73", ""},
    {"AudioEngine", 0x76BC90, SymbolCheck::CodeReferences, 150, "", ""},
    {"CAudioEngine::RetuneRadio", 0x107E10, SymbolCheck::Prologue, 0, "B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 75 0A B9 ?? ?? ?? ?? E9", ""},
    {"RsGlobal.frameLimit", 0x81704C, SymbolCheck::CodeReferences, 2, "", ""},
    {"CDraw::SetFOV", 0x2FF410, SymbolCheck::Prologue, 0, "8B 44 24 04 E9", ""},
    {"CDraw::ms_fFOV", 0x4D5038, SymbolCheck::CodeReferences, 6, "", ""},
    {"CDraw::ms_fAspectRatio", 0x83EFA4, SymbolCheck::CodeReferences, 8, "", ""},
    {"CStreaming::ms_memoryAvailable", 0x4A5A80, SymbolCheck::CodeReferences, 3, "", ""},
    {"CStreaming::desiredNumVehiclesLoaded", 0x4A5A84, SymbolCheck::CodeReferences, 2, "", ""},
    {"CStreaming::RequestModel", 0x87E0, SymbolCheck::Prologue, 0, "53 8B 5C 24 0C 55 8B 6C 24 0C 56 57 8D 7C AD 00",
     ""},
    {"CStreaming::SetModelIsDeletable", 0x9C10, SymbolCheck::Prologue, 0, "56 8B 74 24 08 8D 04 B6", ""},
    {"CStreaming::ms_aInfoForModel", 0x4E4CC0, SymbolCheck::CodeReferences, 30, "", ""},
    {"CModelInfo::ms_modelInfoPtrs", 0x69B0C8, SymbolCheck::CodeReferences, 500, "", ""},
    {"CStreaming::ms_numModelsRequested", 0x4E4CB8, SymbolCheck::CodeReferences, 10, "", ""},
    {"CStreaming::ms_memoryUsed", 0x4E4CB4, SymbolCheck::CodeReferences, 15, "", ""},
    {"CWanted::SetMaximumWantedLevel", 0x161E70, SymbolCheck::Prologue, 0, "8B 44 24 04 83 F8 06 0F 87 91 00 00 00", ""},
    {"CVehicle::m_bEnableMouseSteering", 0x81CC02, SymbolCheck::CodeReferences, 5, "", ""},
    {"CWorld::FindGroundZForCoord", 0x169660, SymbolCheck::Prologue, 0, "83 EC 38 8B 44 24 3C 8B 4C 24 40 6A 00", ""},
    {"Idle frame time cap compare", 0x13E94A, SymbolCheck::Prologue, 0, "83 F8 0E 72 E1", ""},
    {"CRadar::SetCoordBlip", 0x183820, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 53 56 B1 02 33 D2 33 F6", ""},
    {"CCutsceneMgr::ms_cutsceneLoadStatus", 0x75F84C, SymbolCheck::CodeReferences, 8, "", ""},
    {"CTheScripts::StartNewScript", 0x64C20, SymbolCheck::Prologue, 0, "E9", ""},
    // High frame rate fixes, bytes read from the executable
    {"CTimer::Update", 0x161B10, SymbolCheck::Prologue, 0, "8B 0D 28 CB B7 00", ""},
    {"FPS doors add velocity", 0x2F4422, SymbolCheck::Prologue, 0, "D9 46 14 8B CB", ""},
    {"FPS doors damping ladder", 0x2F43A1, SymbolCheck::Prologue, 0, "D9 05 14 23 87 00", ""},
    {"FPS doors damping 0.97", 0x2F43CA, SymbolCheck::Prologue, 0, "D9 05 18 23 87 00", ""},
    {"FPS doors damping 0.97 other", 0x2F43D2, SymbolCheck::Prologue, 0, "D9 05 0C 23 87 00", ""},
    {"FPS doors damping boat", 0x2F4391, SymbolCheck::Prologue, 0, "D9 05 10 23 87 00", ""},
    {"FPS aim walk step", 0x21E0CA, SymbolCheck::Prologue, 0, "D8 0D A8 8C 85 00", ""},
    {"FPS swim speed", 0x28A50E, SymbolCheck::Prologue, 0, "D9 C1 D8 08 D9 C2", ""},
    {"FPS dive speed", 0x28A42B, SymbolCheck::Prologue, 0, "D8 0D F4 8E 85 00", ""},
    {"FPS dive surface", 0x28A4CA, SymbolCheck::Prologue, 0, "D8 05 CC 08 87 00", ""},
    {"FPS swim surface clamp", 0x28A7E6, SymbolCheck::Prologue, 0, "D9 05 5C CB B7 00", ""},
    {"FPS swim pitch damping 1", 0x28A6BD, SymbolCheck::Prologue, 0, "D8 0D F0 8E 85 00", ""},
    {"FPS swim pitch damping 2", 0x28A735, SymbolCheck::Prologue, 0, "D8 0D F0 8E 85 00", ""},
    {"FPS swim pitch damping 3", 0x28A7C0, SymbolCheck::Prologue, 0, "D8 0D F0 8E 85 00", ""},
    {"FPS buoyancy cutoff", 0x2C27C2, SymbolCheck::Prologue, 0, "D8 0D 90 8B 85 00", ""},
    {"FPS swim bubbles call", 0x28AD31, SymbolCheck::Prologue, 0, "E8 0A F7 E1 FF", ""},
    {"FPS swim wake call", 0x28ADE2, SymbolCheck::Prologue, 0, "E8 59 F6 E1 FF", ""},
    {"FPS swim splash call 1", 0x28AEBA, SymbolCheck::Prologue, 0, "E8 21 ED E1 FF", ""},
    {"FPS swim splash call 2", 0x28AF15, SymbolCheck::Prologue, 0, "E8 C6 EC E1 FF", ""},
    {"FPS swim splash call 3", 0x28AF66, SymbolCheck::Prologue, 0, "E8 75 EC E1 FF", ""},
    {"FPS swim splash call 4", 0x28AFB3, SymbolCheck::Prologue, 0, "E8 28 EC E1 FF", ""},
    {"FPS skimmer resistance", 0x2D2771, SymbolCheck::Prologue, 0, "D8 0D DC 1D 87 00", ""},
    {"FPS burnout torque", 0x2A4FCE, SymbolCheck::Prologue, 0, "D9 05 24 10 87 00", ""},
    {"FPS wheel friction 1", 0x2D6E69, SymbolCheck::Prologue, 0, "D9 05 CC B9 C2 00", ""},
    {"FPS wheel friction 2", 0x2D6EA8, SymbolCheck::Prologue, 0, "D9 05 CC B9 C2 00", ""},
    {"FPS wheel friction 3", 0x2D767F, SymbolCheck::Prologue, 0, "D9 05 CC B9 C2 00", ""},
    {"FPS wheel friction 4", 0x2D76AB, SymbolCheck::Prologue, 0, "D9 05 CC B9 C2 00", ""},
    {"FPS wheel friction 5", 0x2D76CD, SymbolCheck::Prologue, 0, "D9 05 CC B9 C2 00", ""},
    {"FPS siren site", 0x2E0961, SymbolCheck::Prologue, 0, "90 90 E9 D8 2F", ""},
    {"FPS siren exit horn", 0x2E09E8, SymbolCheck::Prologue, 0, "5F C7 86 14 05 00", ""},
    {"FPS siren exit toggle", 0x2E0999, SymbolCheck::Prologue, 0, "8A 86 2D 04 00 00", ""},
    {"FPS siren exit none", 0x2E09F7, SymbolCheck::Prologue, 0, "5F C7 86 14 05 00", ""},
    {"CPad::GetHorn", 0x13FEE0, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00", ""},
    {"CPad::HornJustDown", 0x13FF30, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00", ""},
    {"FPS heli rotor normal", 0x2C4F29, SymbolCheck::Prologue, 0, "D8 05 DC 8C 85 00", ""},
    {"FPS heli rotor rc", 0x2C4F37, SymbolCheck::Prologue, 0, "D8 05 D8 9C 85 00", ""},
    {"FPS ped push force", 0x149652, SymbolCheck::Prologue, 0, "8B 54 24 20 8B 44 24 24", ""},
    {"FPS boat aquaplane", 0x2DC23D, SymbolCheck::Prologue, 0, "D8 0D 5C CB B7 00", ""},
    {"FPS climb settle", 0x2811E8, SymbolCheck::Prologue, 0, "A1 5C CB B7 00", ""},
    {"FPS climb coarse", 0x281288, SymbolCheck::Prologue, 0, "8B 15 5C CB B7 00", ""},
    {"FPS turn damping x", 0x144D2C, SymbolCheck::Prologue, 0, "D8 0D D0 2C 86 00", ""},
    {"FPS turn damping y", 0x144D38, SymbolCheck::Prologue, 0, "D8 0D D0 2C 86 00", ""},
    {"FPS turn damping z", 0x144D44, SymbolCheck::Prologue, 0, "D8 0D D0 2C 86 00", ""},
    {"FPS water cannon recycle", 0x32A29B, SymbolCheck::Prologue, 0, "0F BF 47 04 40", ""},
    {"FPS water cannon recycle skip", 0x32A2BB, SymbolCheck::Prologue, 0, "8D 9F 0C 03 00 00", ""},
    {"FPS water cannon push counter", 0x32A37B, SymbolCheck::Prologue, 0, "8B 0D 4C CB B7 00", ""},
    {"FPS autopilot segment time", 0x1BC33, SymbolCheck::Prologue, 0, "E8 08 5F 40 00", ""},
    {"FPS autopilot entered time", 0x1BC5F, SymbolCheck::Prologue, 0, "E8 DC 5E 40 00", ""},
    // on-rails wheel spin: fadd [esi+0x828..0x834] (FramerateVigilante CarWheelOnRailsSpinFix sites)
    {"Wheel spin add 0", 0x2B523F, SymbolCheck::Prologue, 0, "D8 86 28 08 00 00", ""},
    {"Wheel spin add 1", 0x2B524F, SymbolCheck::Prologue, 0, "D8 86 2C 08 00 00", ""},
    {"Wheel spin add 2", 0x2B525D, SymbolCheck::Prologue, 0, "D8 86 30 08 00 00", ""},
    {"Wheel spin add 3", 0x2B5269, SymbolCheck::Prologue, 0, "D8 86 34 08 00 00", ""},
    {"CPad::GetPad", 0x13FB70, SymbolCheck::Prologue, 0, "8B 44 24 04 69 C0 34 01 00 00", ""},
    {"CHud::SetHelpMessage", 0x188BE0, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ??", ""},
    {"FindPlayerPed", 0x16E210, SymbolCheck::Prologue, 0, "8B 44 24 04 85 C0 7D 07 0F B6 05 ?? ?? ?? ??", ""},
    {"PcSaveHelper", 0x817034, SymbolCheck::CodeReferences, 15, "", ""},
    {"CGenericGameStorage::ms_LoadFileName", 0x815FC8, SymbolCheck::CodeReferences, 2, "", ""},
    {"CGenericGameStorage::CheckSlotDataValid", 0x1D1380, SymbolCheck::Prologue, 0, "81 EC 04 01 00 00 56 8B B4 24 0C 01 00 00", ""},
    {"CGame::bMissionPackGame", 0x772910, SymbolCheck::CodeReferences, 25, "", ""},
    {"CStats::StatTypesInt", 0x779000, SymbolCheck::CodeReferences, 2, "", ""},
    {"CStats::StatTypesFloat", 0x779380, SymbolCheck::CodeReferences, 20, "", ""},
    {"CStats::LastMissionPassedName", 0x778A00, SymbolCheck::CodeReferences, 6, "", ""},
    {"CRadar::ms_RadarTrace", 0x7A86F0, SymbolCheck::CodeReferences, 5, "", ""},
    {"CStreaming::LoadScene", 0xEB70, SymbolCheck::Prologue, 0, "83 EC 1C 55 8B 6C 24 24 56 57 55 E8 ?? ?? ?? ??", ""},
    {"CStreaming::LoadSceneCollision", 0xED80, SymbolCheck::Prologue, 0, "56 8B 74 24 08 6A 01 83 EC 0C 8B C6", ""},
    {"CWorld::FindGroundZFor3DCoord", 0x1696C0, SymbolCheck::Prologue, 0, "83 EC 38 8B 44 24 3C 8B 4C 24 40 8B 54 24 44", ""},
    {"CRunningScript::SetCharCoordinates", 0x64DC0, SymbolCheck::Prologue, 0, "83 EC 0C 56 57 8B 7C 24 18 90 E9", ""},
    {"CRadar::ClearBlip", 0x187CE0, SymbolCheck::Prologue, 0, "8B 4C 24 04 83 F9 FF 74 30 8B C1 25 FF FF 00 00", ""},
    {"CGameLogic::SetUpSkip", 0x423C0, SymbolCheck::Prologue, 0, "E9", ""},
    {"CGameLogic::SkipState", 0x56A8A0, SymbolCheck::CodeReferences, 12, "", ""},
    {"CStats::IncrementStat", 0x15C180, SymbolCheck::Prologue, 0, "51 D9 44 24 0C D8 1D ?? ?? ?? ?? DF E0", ""},
    {"CStats::DecrementStat", 0x159FA0, SymbolCheck::Prologue, 0, "D9 44 24 08 D8 1D ?? ?? ?? ?? DF E0", ""},
    {"CStats::GetStatValue", 0x158E40, SymbolCheck::Prologue, 0, "66 8B 44 24 04 66 3D 52 00 73 0B 0F B7 C0", ""},
    {"CClock::ms_nGameClockMinutes", 0x770152, SymbolCheck::CodeReferences, 25, "", ""},
    {"CClock::ms_nGameClockHours", 0x770153, SymbolCheck::CodeReferences, 60, "", ""},
    {"CClock::ms_nLastClockTick", 0x770158, SymbolCheck::CodeReferences, 7, "", ""},
    {"CClock::ms_nMillisecondsPerGameMinute", 0x77015C, SymbolCheck::CodeReferences, 4, "", ""},
    {"CClock::SetGameClock", 0x12D150, SymbolCheck::Prologue, 0, "A1 ?? ?? ?? ?? 8A 4C 24 04 8A 54 24 08", ""},
    {"CWeather::ForceWeatherNow", 0x32A4F0, SymbolCheck::Prologue, 0, "E9 ?? ?? ?? ?? 66 A3 ?? ?? ?? ?? 66 A3", ""},
    {"CWeather::ReleaseWeather", 0x32A510, SymbolCheck::Prologue, 0, "66 C7 05 ?? ?? ?? ?? FF FF C3", ""},
    {"CWeather::ForcedWeatherType", 0x881318, SymbolCheck::CodeReferences, 6, "", ""},
    {"CWanted::SetWantedLevel", 0x162470, SymbolCheck::Prologue, 0, "A0 ?? ?? ?? ?? 84 C0 0F 85 96 00 00 00", ""},
    {"CWanted::WorkOutPolicePresence", 0x1625F0, SymbolCheck::Prologue, 0, "51 53 8B 1D ?? ?? ?? ?? 55 56 57 8B 7B 08", ""},
    {"CHeli::bPoliceHelisAllowed", 0x4D338C, SymbolCheck::CodeReferences, 3, "", ""},
    {"CHeli::ProcessControl police shot interval", 0x2C74E0, SymbolCheck::Prologue, 0, "83 F8 06 77 ?? FF 24 85 ?? ?? ?? ?? BF 3F 42 0F 00", ""},
    {"CAutomobile::ProcessControlInputs", 0x2AD690, SymbolCheck::Prologue, 0, "83 EC 18 53 8A 5C 24 20 55 56 8B F1", ""},
    {"CPad::GetSteeringLeftRight", 0x13FB80, SymbolCheck::Prologue, 0, "66 83 B9 0E 01 00 00 00 75 23 0F B7 81 0A 01 00 00", ""},
    {"CAutomobile::VehicleDamage upside-down gate", 0x2A7717, SymbolCheck::Prologue, 0, "F6 86 68 08 00 00 08 0F 85", ""},
    {"CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment", 0x554C0, SymbolCheck::Prologue, 0, "E9", ""},
    {"CPickups::CollectPickupWindow", 0x57D644, SymbolCheck::CodeReferences, 8, "", ""},
};

const BuildAdapterData kNewsteam{"newsteam_r2_lv", kNewsteamSymbols, kCapabilities};
const BuildAdapterData kUs10{"us10_hoodlum", kUs10Symbols, kCapabilities};

}  // namespace

const BuildAdapterData& NewsteamR2Lv() {
    return kNewsteam;
}

const BuildAdapterData& Us10Hoodlum() {
    return kUs10;
}

const BuildAdapterData* FindAdapterData(std::string_view buildId) {
    for (const BuildAdapterData* data : {&kNewsteam, &kUs10}) {
        if (data->buildId == buildId) {
            return data;
        }
    }
    return nullptr;
}

}  // namespace sacore::adapters
