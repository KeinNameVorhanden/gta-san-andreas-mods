/* SACore mod API, C ABI, version 1.
 *
 * Stability rules (docs/API.md):
 * - Within SACORE_API_VERSION 1, structs only grow at the end and carry struct_size; check struct_size before
 *   using a field added after the first release (the C++ wrapper in sacore.hpp does this).
 * - Everything is called on the game thread unless stated otherwise. Do not call the host from other threads.
 * - Functions marked BUILD-SPECIFIC expose game internals whose meaning can change between game builds.
 * - A mod that crashes inside a callback is disabled for the session; other mods keep running.
 */
#ifndef SACORE_API_H
#define SACORE_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SACORE_API_VERSION 1
#define SACORE_MOD_MAIN_NAME "sacore_mod_main"

typedef enum SacoreLogLevel {
    SACORE_LOG_TRACE = 0,
    SACORE_LOG_DEBUG = 1,
    SACORE_LOG_INFO = 2,
    SACORE_LOG_WARN = 3,
    SACORE_LOG_ERROR = 4
} SacoreLogLevel;

typedef enum SacoreCapabilityState {
    SACORE_CAPABILITY_SUPPORTED = 0,       /* verified at runtime on this build */
    SACORE_CAPABILITY_EXPERIMENTAL = 1,    /* addresses validated, behaviour not yet verified on this build */
    SACORE_CAPABILITY_UNSUPPORTED = 2,     /* not available on this build (reason in the core log) */
    SACORE_CAPABILITY_NOT_INVESTIGATED = 3 /* unknown capability id */
} SacoreCapabilityState;

typedef enum SacoreEventType {
    SACORE_EVENT_FRAME = 1,               /* every presented frame; frame, device */
    SACORE_EVENT_GAME_STATE_CHANGED = 2,  /* state_from, state_to (0 start-up ... 7 menu, 8 loading, 9 playing) */
    SACORE_EVENT_SAVE_STARTED = 3,
    SACORE_EVENT_SAVE_COMPLETED = 4,      /* success */
    SACORE_EVENT_LOAD_STARTED = 5,
    SACORE_EVENT_LOAD_COMPLETED = 6,      /* success (0 also when a new game starts and nothing was loaded) */
    SACORE_EVENT_DEVICE_CREATED = 7,      /* device, window */
    SACORE_EVENT_DEVICE_RESET = 8,        /* device, before_reset (release D3DPOOL_DEFAULT resources when 1), success */
    SACORE_EVENT_SHUTDOWN = 9,            /* the game window is closing; last event a mod receives */
    SACORE_EVENT_CONFIG_CHANGED = 10,     /* one of this mod's [config] settings changed at runtime; text = key */
    SACORE_EVENT_GAME_TICK = 11           /* after every game logic step (CGame::Process), only while playing; the
                                             place to change the world (teleport, stats, time), not FRAME */
} SacoreEventType;

typedef struct SacoreEvent {
    uint32_t struct_size;
    uint32_t type; /* SacoreEventType */
    uint64_t frame;
    int32_t state_from;
    int32_t state_to;
    int32_t success;
    int32_t before_reset;
    void* device; /* IDirect3DDevice9* */
    void* window; /* HWND */
    const char* text; /* added in core 1.0.0 (check struct_size): SACORE_EVENT_CONFIG_CHANGED key */
} SacoreEvent;

typedef struct SacorePlayer {
    uint32_t struct_size;
    float health;
    float armour;
    int32_t money;
    int32_t wanted_level;
    float position[3];
    int32_t area; /* interior id of the player ped, 0 = outdoors */
} SacorePlayer;

typedef struct SacorePatchInfo {
    uint32_t struct_size;
    const char* id;          /* unique; prefixed with the mod id by the core; switchable in core.toml [patches] */
    const char* target;      /* human readable target name */
    const char* reason;
    const char* expected;
    const char* on_failure;
} SacorePatchInfo;

typedef void (*SacoreEventCallback)(const SacoreEvent* event, void* user);
/* Return non-zero to consume the message; *result is then returned from the window procedure. */
typedef int (*SacoreMessageFilter)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam, intptr_t* result,
                                   void* user);

typedef struct SacoreHost SacoreHost;

/* Passed to each mod; valid until its SACORE_EVENT_SHUTDOWN handler (or shutdown callback) returns. UTF-8 strings. */
struct SacoreHost {
    uint32_t struct_size;
    uint32_t api_version;
    const char* core_version; /* "0.5.0" */
    const char* game_build;   /* "newsteam_r2_lv", "us10_hoodlum" */
    const char* mod_id;
    const char* mod_directory;
    void* core_internal;      /* opaque, do not touch */

    /* Logging: core log, [Mods] category, tagged with the mod id. */
    void (*log)(const SacoreHost* host, SacoreLogLevel level, const char* message);

    /* Capabilities: ids as listed in sacore\diagnostics\capabilities.txt (e.g. "player.read"). */
    SacoreCapabilityState (*capability_state)(const SacoreHost* host, const char* capability_id);

    /* Events. Returns a subscription id (0 on failure). Subscriptions end automatically when the mod stops. */
    uint32_t (*subscribe)(const SacoreHost* host, SacoreEventType type, SacoreEventCallback callback, void* user);
    void (*unsubscribe)(const SacoreHost* host, uint32_t subscription);

    /* Game state (capability "game.state.read"); -1 when unavailable. */
    int32_t (*game_state)(const SacoreHost* host);
    /* Player (capability "player.read"); returns 0 when there is no player (menu, loading) or it is unavailable. */
    int32_t (*get_player)(const SacoreHost* host, SacorePlayer* out);
    /* Money (capability "player.money.write"); returns 0 if unavailable. */
    int32_t (*set_money)(const SacoreHost* host, int32_t money);

    /* Mod configuration declared in mod.toml [config]; stored in sacore\config\mods\<mod id>.toml.
     * Unknown keys return the given fallback and log an error. */
    int32_t (*config_bool)(const SacoreHost* host, const char* key, int32_t fallback);
    int64_t (*config_int)(const SacoreHost* host, const char* key, int64_t fallback);
    double (*config_double)(const SacoreHost* host, const char* key, double fallback);
    /* Copies the value into buffer (always NUL-terminated); returns the full length. */
    size_t (*config_string)(const SacoreHost* host, const char* key, char* buffer, size_t size);

    /* Engine: IDirect3DDevice9* and HWND of the game (NULL before device creation). */
    void* (*d3d_device)(const SacoreHost* host);
    void* (*game_window)(const SacoreHost* host);
    /* Window message filter, called before the game's window procedure. Removed when the mod stops. */
    int32_t (*add_message_filter)(const SacoreHost* host, SacoreMessageFilter filter, void* user);

    /* Patching through the core patch manager (registered, switchable, removed at shutdown).
     * install_hook: function hook (MinHook); returns 1 on success and stores the trampoline in *original. */
    int32_t (*install_hook)(const SacoreHost* host, const SacorePatchInfo* info, void* target, void* detour,
                            void** original);
    /* BUILD-SPECIFIC: runtime address of an adapter symbol (docs/ADDRESSES.md), 0 if unknown or failed its check. */
    uintptr_t (*symbol_address)(const SacoreHost* host, const char* symbol);

    /* EXPERIMENTAL diagnostics text for developer tools: kind is "mods", "capabilities", "patches" or "log".
     * Copies into buffer (NUL-terminated); returns the full length. */
    size_t (*diagnostics_text)(const SacoreHost* host, const char* kind, char* buffer, size_t size);

    /* Save side-car data (added in core 0.8.0; check struct_size): string values per mod, written next to the save
     * of the slot the player saves to (sacore\savedata\slot<N>\<mod id>.toml) and restored when that slot is
     * loaded. Values set before a save belong to that save. */
    int32_t (*savedata_set)(const SacoreHost* host, const char* key, const char* value);
    /* Copies the value (NUL-terminated); returns its length, or (size_t)-1 when the key is not set. */
    size_t (*savedata_get)(const SacoreHost* host, const char* key, char* buffer, size_t size);
    /* Slot (1..8) of the last save or load in this session, 0 if none. */
    int32_t (*save_slot)(const SacoreHost* host);

    /* Live settings (added in core 1.0.0; check struct_size). settings_list copies every running mod's declared
     * [config] settings, one per line, fields separated by tabs:
     *   mod id, key, type (bool|int|double|string), current value, min, max, allowed values (|-separated),
     *   description
     * and returns the full length. settings_set validates `value` (text) against that mod's declaration, stores it in
     * sacore\config\mods\<id>.toml and sends that mod SACORE_EVENT_CONFIG_CHANGED; returns 1, or 0 with the reason
     * in the core log. */
    size_t (*settings_list)(const SacoreHost* host, char* buffer, size_t size);
    int32_t (*settings_set)(const SacoreHost* host, const char* mod_id, const char* key, const char* value);

    /* Saves the game through the game's own routine (added in core 1.0.0; check struct_size). `name` is "slot1".."slot8"
     * (the menu slots), "quick1".."quick9" / "auto1".."auto9" (own files that never touch the menu slots; capability
     * save.extra_slots) or, since core 1.3.0, "story1".."story120" (the story runner's library of story points). The
     * save runs from the game's message loop, never inside a frame; the result arrives as SACORE_EVENT_SAVE_COMPLETED.
     * Returns 1 when scheduled, 0 when not possible now (name invalid, capability missing, not playing, inside an
     * interior or cutscene, a mission or mini game running, another save pending). Whether a save is sensible
     * otherwise (wanted level) is the mod's decision.
     * save_list copies every existing save except the story saves, one per line, tab-separated: name, file path, last
     * write time (Unix seconds), valid (1/0); returns the full length. */
    int32_t (*save_game)(const SacoreHost* host, const char* name);
    size_t (*save_list)(const SacoreHost* host, char* buffer, size_t size);
    /* Loads a save by name (same names as save_game) the way the game's menu does: from the main menu or while
     * playing; the game restarts into the save (SACORE_EVENT_LOAD_STARTED / _COMPLETED follow). Returns 1 when
     * scheduled, 0 when the file is missing or damaged or loading is unavailable (capability save.load_slot). */
    int32_t (*load_game)(const SacoreHost* host, const char* name);

    /* Code and data patches through the core patch manager (added in core 1.2.0; check struct_size).
     * write_memory copies `size` bytes to `target` (page protection handled), records the original bytes and
     * restores them when the patch is removed (remove_patch with the same info->id, or at shutdown). Returns 1 on
     * success. BUILD-SPECIFIC: only write to addresses from symbol_address whose bytes the mod has checked. */
    int32_t (*write_memory)(const SacoreHost* host, const SacorePatchInfo* info, void* target, const void* bytes,
                            size_t size);
    /* Removes one of this mod's hooks or memory patches by its id (hooks are disabled, memory is restored). */
    int32_t (*remove_patch)(const SacoreHost* host, const char* id);
    /* Core setting from core.toml as text ("true", "30", "info"); copies NUL-terminated, returns the full length,
     * (size_t)-1 for an unknown key. Example: "testing.auto_start_new_game". */
    size_t (*core_setting)(const SacoreHost* host, const char* key, char* buffer, size_t size);

    /* Community save import (added in core 1.2.20; check struct_size). import_list copies one line per file in
     * sacore\import: file name, size, valid (1/0), problem, version id (hex), tab-separated. import_save copies a
     * valid file from there over the save `name` ("slot1".."slot8", "quick1".., "auto1"..), backing up what it
     * replaces; returns 1, or 0 with the reason in the core log. */
    size_t (*import_list)(const SacoreHost* host, char* buffer, size_t size);
    int32_t (*import_save)(const SacoreHost* host, const char* file, const char* name);
};

/* Filled by the mod in sacore_mod_main. Unused entries stay NULL. */
typedef struct SacoreModCallbacks {
    uint32_t struct_size;
    void (*shutdown)(void); /* called on the game thread before the game window is destroyed */
} SacoreModCallbacks;

/* Every mod DLL exports this. Return 0 on success; any other value refuses loading (logged as error code). */
typedef int (*SacoreModMainFn)(const SacoreHost* host, SacoreModCallbacks* callbacks);

#ifdef __cplusplus
}
#endif

#endif /* SACORE_API_H */
