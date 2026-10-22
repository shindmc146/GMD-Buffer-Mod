#include "Config.hpp"
#include "Keys.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace bm {

Config g_cfg;

static int indexOf(char const* const* names, int n, std::string const& v, int def) {
    for (int i = 0; i < n; i++) {
        if (v == names[i]) return i;
    }
    return def;
}

static int intSetting(char const* key) {
    return static_cast<int>(Mod::get()->getSettingValue<int64_t>(key));
}

static bool boolSetting(char const* key) {
    return Mod::get()->getSettingValue<bool>(key);
}

static std::string strSetting(char const* key) {
    return Mod::get()->getSettingValue<std::string>(key);
}

void loadConfig() {
    Config c;
    c.enabled = boolSetting("enabled");
    c.practiceOnly = boolSetting("practice-only");
    c.showOverlay = boolSetting("show-overlay");
    c.debug = boolSetting("debug");
    c.tpsIndex = indexOf(TPS_NAMES, TPS_COUNT, strSetting("tps"), 0);

    c.stage = indexOf(STAGE_NAMES, STAGE_COUNT, strSetting("stage"), 1);
    for (int i = 0; i < MODE_COUNT; i++) c.modeTicks[i] = intSetting(MODE_KEYS[i]);
    c.waveMode = indexOf(WAVE_NAMES, WAVE_MODE_COUNT, strSetting("wave-mode"), WAVE_GRID);
    c.holdRefire = boolSetting("hold-refire");

    c.lateOrbTicks = std::max(0, intSetting("late-orb-ticks"));
    c.coyoteTicks = std::max(0, intSetting("coyote-ticks"));

    c.lookahead = boolSetting("lookahead");
    c.smartShift = boolSetting("smart-shift");
    c.shiftTicks = std::max(0, intSetting("shift-ticks"));
    c.deathGuard = boolSetting("death-guard");
    c.autoSave = boolSetting("auto-save");
    c.lookaheadTicks = std::max(4, intSetting("lookahead-ticks"));
    c.holdLookaheadTicks = std::max(2, intSetting("hold-lookahead-ticks"));

    for (int i = 0; i < KEY_COUNT; i++) c.keys[i] = keys::vkFromName(strSetting(KEY_SETTINGS[i]));

    g_cfg = c;
}

void saveStage(int stage) {
    Mod::get()->setSettingValue<std::string>("stage", STAGE_NAMES[stage]);
}

void saveTps(int index) {
    Mod::get()->setSettingValue<std::string>("tps", TPS_NAMES[index]);
}

void saveWaveMode(int mode) {
    Mod::get()->setSettingValue<std::string>("wave-mode", WAVE_NAMES[mode]);
}

void saveBool(char const* key, bool value) {
    Mod::get()->setSettingValue<bool>(key, value);
}

int ticksForMode(int modeIndex) {
    int c = g_cfg.modeTicks[modeIndex];
    if (c < 0) {
        int t = STAGES[g_cfg.stage];
        if (modeIndex == M_WAVE && t > 0) t = std::min(t, WAVE_MAX_TICKS);
        return t;
    }
    return c;
}

} // namespace bm

$on_mod(Loaded) {
    bm::loadConfig();
    auto reload = [] { bm::loadConfig(); };

    for (auto key : {"enabled", "practice-only", "show-overlay", "debug", "hold-refire",
                     "lookahead", "smart-shift", "death-guard", "auto-save"}) {
        listenForSettingChanges<bool>(key, [reload](bool) { reload(); });
    }
    for (auto key : {"stage", "tps", "wave-mode"}) {
        listenForSettingChanges<std::string>(key, [reload](std::string) { reload(); });
    }
    for (auto key : bm::KEY_SETTINGS) {
        listenForSettingChanges<std::string>(key, [reload](std::string) { reload(); });
    }
    for (auto key : bm::MODE_KEYS) {
        listenForSettingChanges<int64_t>(key, [reload](int64_t) { reload(); });
    }
    for (auto key : {"late-orb-ticks", "coyote-ticks", "shift-ticks", "lookahead-ticks", "hold-lookahead-ticks"}) {
        listenForSettingChanges<int64_t>(key, [reload](int64_t) { reload(); });
    }
}
