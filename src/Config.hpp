#pragma once

#include <string>

namespace bm {

// Main window presets cycled by the hotkey.
inline constexpr char const* STAGE_NAMES[] = {"2 ticks", "4 ticks", "8 ticks", "16 ticks", "32 ticks", "Off"};
inline constexpr int STAGES[] = {2, 4, 8, 16, 32, 0};
inline constexpr int STAGE_COUNT = 6;

// Per-mode windows. Index order is used everywhere.
enum ModeIndex { M_CUBE = 0, M_BALL, M_ROBOT, M_SPIDER, M_SHIP, M_UFO, M_SWING, M_WAVE, MODE_COUNT };
inline constexpr char const* MODE_KEYS[MODE_COUNT] = {
    "cube-ticks", "ball-ticks", "robot-ticks", "spider-ticks",
    "ship-ticks", "ufo-ticks", "swing-ticks", "wave-ticks"
};
inline constexpr char const* MODE_NAMES[MODE_COUNT] = {
    "cube", "ball", "robot", "spider", "ship", "ufo", "swing", "wave"
};
inline constexpr int WAVE_MAX_TICKS = 8; // wave on "-1" never goes above this

enum WaveMode { WAVE_HOLD = 0, WAVE_GAP = 1, WAVE_GRID = 2, WAVE_MODE_COUNT = 3 };
inline constexpr char const* WAVE_NAMES[] = {"Min Hold", "Min Gap", "Grid"};

// TPS presets. Index 0 means "read it from the game".
inline constexpr char const* TPS_NAMES[] = {"Auto", "60", "120", "240", "360", "480", "720", "960", "1000"};
inline constexpr int TPS_PRESETS[] = {0, 60, 120, 240, 360, 480, 720, 960, 1000};
inline constexpr int TPS_COUNT = 9;

// Hotkeys, in settings order.
enum HotkeyAction { K_TOGGLE = 0, K_STAGE, K_TPS_UP, K_TPS_DOWN, K_DEBUG, K_WAVE, K_LOOKAHEAD, KEY_COUNT };
inline constexpr char const* KEY_SETTINGS[KEY_COUNT] = {
    "key-toggle", "key-stage", "key-tps-up", "key-tps-down", "key-debug", "key-wave", "key-lookahead"
};

struct Config {
    // general
    bool enabled = true;
    bool practiceOnly = true;
    bool showOverlay = true;
    bool debug = false;
    int tpsIndex = 0;

    // early buffer and flying modes
    int stage = 1;
    int modeTicks[MODE_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int waveMode = WAVE_GRID;
    bool holdRefire = true;

    // late assists
    int lateOrbTicks = 4;
    int coyoteTicks = 4;

    // lookahead
    bool lookahead = false;
    bool smartShift = true;
    int shiftTicks = 8;
    bool deathGuard = true;
    bool autoSave = false;
    int lookaheadTicks = 96;
    int holdLookaheadTicks = 16;

    int keys[KEY_COUNT] = {};
};

extern Config g_cfg;

// Reads every setting into g_cfg.
void loadConfig();

// Saves one value back (used by hotkeys); the change listener reloads g_cfg.
void saveStage(int stage);
void saveTps(int index);
void saveWaveMode(int mode);
void saveBool(char const* key, bool value);

// Main window of a mode, in ticks: -1 follows the main window, 0 is off.
int ticksForMode(int modeIndex);

} // namespace bm
