#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

// ---- settings --------------------------------------------------------
static constexpr char const* STAGE_NAMES[] = {"2 ticks", "4 ticks", "8 ticks", "16 ticks", "32 ticks", "Off"};
static constexpr int STAGES[] = {2, 4, 8, 16, 32, 0};
static constexpr int STAGE_COUNT = 6;
static int g_stage = 1;

static constexpr int WAVE_MAX_TICKS = 8;

// After a spawn, wave Grid / Min Gap are skipped for this many ticks.
static constexpr int SPAWN_GRACE_TICKS = 16;

// Index: 0 cube, 1 ball, 2 robot, 3 spider, 4 ship, 5 ufo, 6 swing, 7 wave.
// Value: -1 = same as main window, 0 = off, >0 = custom ticks.
static constexpr int MODE_COUNT = 8;
static constexpr int MODE_WAVE = 7;
static constexpr char const* MODE_KEYS[] = {
    "cube-ticks", "ball-ticks", "robot-ticks", "spider-ticks",
    "ship-ticks", "ufo-ticks", "swing-ticks", "wave-ticks"
};
static int g_modeTicks[MODE_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1};

static constexpr char const* WAVE_NAMES[] = {"Min Hold", "Min Gap", "Grid"};
enum WaveMode { WAVE_HOLD = 0, WAVE_GAP = 1, WAVE_GRID = 2 };
static constexpr int WAVE_MODE_COUNT = 3;
static int g_waveMode = WAVE_GRID;

// Index 0 is "Auto": the TPS is read from the game's own physics step.
static constexpr char const* TPS_NAMES[] = {"Auto", "60", "120", "240", "360", "480", "720", "960", "1000"};
static constexpr int TPS_PRESETS[] = {0, 60, 120, 240, 360, 480, 720, 960, 1000};
static constexpr int TPS_COUNT = 9;
static int g_tpsIndex = 0;
static int g_detectedTps = 240;

// Late orb rescue: a click this many ticks after leaving an unused orb
// still fires that orb. 0 = off.
static int g_lateOrbTicks = 4;

static bool g_practiceOnly = true;
static bool g_showOverlay = true;
static bool g_debug = false;

// Hotkeys. Order: 0 stage, 1 tps up, 2 tps down, 3 debug, 4 wave mode.
static constexpr int KEY_COUNT = 5;
static constexpr char const* KEY_SETTINGS[] = {"key-stage", "key-tps-up", "key-tps-down", "key-debug", "key-wave"};
static int g_keyVK[KEY_COUNT] = {0, 0, 0, 0, 0}; // 0 = none

// Raised whenever the level resets (death, restart, checkpoint).
static int g_resetEpoch = 0;
// ----------------------------------------------------------------------

#define DBG(...) do { if (g_debug) log::info(__VA_ARGS__); } while (0)

static int indexOf(char const* const* names, int n, std::string const& v, int def) {
    for (int i = 0; i < n; i++) if (v == names[i]) return i;
    return def;
}

static int vkFromName(std::string const& n) {
#ifdef GEODE_IS_WINDOWS
    if (n == "None") return 0;
    if (n.size() >= 2 && n[0] == 'F') {
        int num = std::atoi(n.c_str() + 1);
        if (num >= 1 && num <= 12) return VK_F1 + (num - 1);
    }
    if (n == "Insert") return VK_INSERT;
    if (n == "Delete") return VK_DELETE;
    if (n == "Home") return VK_HOME;
    if (n == "End") return VK_END;
    if (n == "Page Up") return VK_PRIOR;
    if (n == "Page Down") return VK_NEXT;
#endif
    return 0;
}

static void loadSettings() {
    auto m = Mod::get();
    g_stage = indexOf(STAGE_NAMES, STAGE_COUNT, m->getSettingValue<std::string>("stage"), 1);
    g_tpsIndex = indexOf(TPS_NAMES, TPS_COUNT, m->getSettingValue<std::string>("tps"), 0);
    g_waveMode = indexOf(WAVE_NAMES, WAVE_MODE_COUNT, m->getSettingValue<std::string>("wave-mode"), WAVE_GRID);
    for (int i = 0; i < MODE_COUNT; i++) {
        g_modeTicks[i] = static_cast<int>(m->getSettingValue<int64_t>(MODE_KEYS[i]));
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        g_keyVK[i] = vkFromName(m->getSettingValue<std::string>(KEY_SETTINGS[i]));
    }
    g_lateOrbTicks = static_cast<int>(m->getSettingValue<int64_t>("late-orb-ticks"));
    g_practiceOnly = m->getSettingValue<bool>("practice-only");
    g_showOverlay = m->getSettingValue<bool>("show-overlay");
    g_debug = m->getSettingValue<bool>("debug");
}

static char const* waveModeName() { return WAVE_NAMES[g_waveMode]; }

static int currentTps() {
    return g_tpsIndex == 0 ? g_detectedTps : TPS_PRESETS[g_tpsIndex];
}

static float stepsPerTick() {
    return currentTps() / 240.0f;
}

static int ticksToSteps(int ticks) {
    if (ticks <= 0) return 0;
    return std::max(1, static_cast<int>(std::lround(ticks * stepsPerTick())));
}

static int mainLimitSteps() {
    return static_cast<int>(std::lround(STAGES[g_stage] * stepsPerTick()));
}

// Reads the TPS from the per-step delta the game hands to processCommands.
// GD passes it either in seconds or in 1/60 s units depending on the path,
// so both are accepted. A new value is only taken after it has been the
// same for 30 steps in a row, so one odd step does not change anything.
static void observeStepDt(float dt) {
    if (!(dt > 0.f)) return;
    float tps = dt < 0.025f ? 1.0f / dt : 60.0f / dt;
    if (tps < 30.f || tps > 2400.f) return;
    int rounded = static_cast<int>(std::lround(tps));

    static int candidate = 0;
    static int streak = 0;
    if (rounded == candidate) {
        streak++;
    } else {
        candidate = rounded;
        streak = 1;
    }
    if (streak >= 30 && candidate != g_detectedTps) {
        DBG("[InputBuffer] Auto TPS {} -> {} (dt {})", g_detectedTps, candidate, dt);
        g_detectedTps = candidate;
    }
}

static bool isFlyMode(PlayerObject* p) {
    return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing;
}

static bool isGroundMode(PlayerObject* p) {
    return !isFlyMode(p);
}

static int modeIndex(PlayerObject* p) {
    if (p->m_isShip) return 4;
    if (p->m_isBird) return 5;
    if (p->m_isDart) return 7;
    if (p->m_isSwing) return 6;
    if (p->m_isBall) return 1;
    if (p->m_isRobot) return 2;
    if (p->m_isSpider) return 3;
    return 0;
}

static int ticksForMode(int idx) {
    int c = g_modeTicks[idx];
    if (c < 0) {
        int t = STAGES[g_stage];
        if (idx == MODE_WAVE && t > 0) t = std::min(t, WAVE_MAX_TICKS);
        return t;
    }
    return c;
}

static int modeLimitSteps(PlayerObject* p) {
    return ticksToSteps(ticksForMode(modeIndex(p)));
}

static int waveGridSteps() {
    return ticksToSteps(ticksForMode(MODE_WAVE));
}

static int spawnGraceSteps() {
    return ticksToSteps(SPAWN_GRACE_TICKS);
}

static int lateOrbSteps() {
    return ticksToSteps(g_lateOrbTicks);
}

static int ringsTouching(PlayerObject* p) {
    return p->m_touchingRings ? static_cast<int>(p->m_touchingRings->count()) : 0;
}

static RingObject* firstRing(PlayerObject* p) {
    if (ringsTouching(p) == 0) return nullptr;
    return typeinfo_cast<RingObject*>(p->m_touchingRings->objectAtIndex(0));
}

static char const* modeName(PlayerObject* p) {
    if (p->m_isShip) return "ship";
    if (p->m_isBird) return "ufo";
    if (p->m_isDart) return "wave";
    if (p->m_isSwing) return "swing";
    if (p->m_isBall) return "ball";
    if (p->m_isRobot) return "robot";
    if (p->m_isSpider) return "spider";
    return "cube";
}

static std::string tpsLabel() {
    if (g_tpsIndex == 0) return fmt::format("{} auto", g_detectedTps);
    return TPS_NAMES[g_tpsIndex];
}

static void doHotkey(int action) {
    auto m = Mod::get();
    switch (action) {
        case 0:
            g_stage = (g_stage + 1) % STAGE_COUNT;
            m->setSettingValue<std::string>("stage", STAGE_NAMES[g_stage]);
            log::info("[InputBuffer] Main window -> {} ({} steps, wave {} steps @ {} TPS)",
                      STAGE_NAMES[g_stage], mainLimitSteps(), waveGridSteps(), currentTps());
            break;
        case 1:
            g_tpsIndex = std::min(g_tpsIndex + 1, TPS_COUNT - 1);
            m->setSettingValue<std::string>("tps", TPS_NAMES[g_tpsIndex]);
            log::info("[InputBuffer] TPS -> {} ({:.2f} steps/tick)", tpsLabel(), stepsPerTick());
            break;
        case 2:
            g_tpsIndex = std::max(g_tpsIndex - 1, 0);
            m->setSettingValue<std::string>("tps", TPS_NAMES[g_tpsIndex]);
            log::info("[InputBuffer] TPS -> {} ({:.2f} steps/tick)", tpsLabel(), stepsPerTick());
            break;
        case 3:
            g_debug = !g_debug;
            m->setSettingValue<bool>("debug", g_debug);
            log::info("[InputBuffer] Debug logging -> {}", g_debug ? "ON" : "OFF");
            break;
        case 4:
            g_waveMode = (g_waveMode + 1) % WAVE_MODE_COUNT;
            m->setSettingValue<std::string>("wave-mode", WAVE_NAMES[g_waveMode]);
            log::info("[InputBuffer] Wave mode -> {} ({} steps)", waveModeName(), waveGridSteps());
            break;
    }
}

#ifdef GEODE_IS_WINDOWS
// True only when the focused window belongs to Geometry Dash, so hotkeys
// pressed in Discord, OBS, a browser and so on are ignored.
static bool gameHasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
#endif

static void pollKeys() {
#ifdef GEODE_IS_WINDOWS
    static bool prev[KEY_COUNT] = {false, false, false, false, false};
    bool focused = gameHasFocus();
    bool now[KEY_COUNT];
    for (int i = 0; i < KEY_COUNT; i++) {
        now[i] = focused && g_keyVK[i] != 0 && (GetAsyncKeyState(g_keyVK[i]) & 0x8000) != 0;
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        if (now[i] && !prev[i]) doHotkey(i);
    }
    for (int i = 0; i < KEY_COUNT; i++) prev[i] = now[i];
#endif
}

$on_mod(Loaded) {
    loadSettings();
    listenForSettingChanges<std::string>("stage", [](std::string) { loadSettings(); });
    listenForSettingChanges<std::string>("tps", [](std::string) { loadSettings(); });
    listenForSettingChanges<std::string>("wave-mode", [](std::string) { loadSettings(); });
    for (auto key : MODE_KEYS) {
        listenForSettingChanges<int64_t>(key, [](int64_t) { loadSettings(); });
    }
    for (auto key : KEY_SETTINGS) {
        listenForSettingChanges<std::string>(key, [](std::string) { loadSettings(); });
    }
    listenForSettingChanges<int64_t>("late-orb-ticks", [](int64_t) { loadSettings(); });
    listenForSettingChanges<bool>("practice-only", [](bool) { loadSettings(); });
    listenForSettingChanges<bool>("show-overlay", [](bool) { loadSettings(); });
    listenForSettingChanges<bool>("debug", [](bool) { loadSettings(); });

    log::info("[InputBuffer] BufferMod loaded (v1.1.0). Main: {}, TPS: {}, wave: {}, late orb: {} ticks, practice only: {}",
              STAGE_NAMES[g_stage], tpsLabel(), waveModeName(), g_lateOrbTicks, g_practiceOnly);
}

class $modify(BufferPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        auto spr = ButtonSprite::create("Buffer");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BufferPause::onBufferSettings));
        menu->addChild(btn);

        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 60.f, 30.f});
        this->addChild(menu);
    }

    void onBufferSettings(CCObject*) {
        openSettingsPopup(Mod::get(), false);
    }
};

// Raises the reset flag on death, restart and checkpoint loads. The buffer
// layer clears its state on the next step.
class $modify(BufferReset, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g_resetEpoch++;
    }

    void resetLevelFromStart() {
        PlayLayer::resetLevelFromStart();
        g_resetEpoch++;
    }

    void loadFromCheckpoint(CheckpointObject* object) {
        PlayLayer::loadFromCheckpoint(object);
        g_resetEpoch++;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        g_resetEpoch++;
    }
};

struct PState {
    bool held = false;
    bool pending = false;
    int pendingStart = 0;
    int lastRingCount = 0;
    bool releaseNext = false;

    int pressStep = -1;
    bool delayedRelease = false;
    int releaseAt = 0;

    std::vector<std::pair<bool, int>> waveQueue;
    int lastReleaseStep = -1000000;

    // Late orb rescue.
    Ref<RingObject> lastRing;   // last orb the icon overlapped
    bool touchingRing = false;  // overlapping an orb right now
    int ringLeftStep = -1;      // step the icon stopped overlapping it
    Ref<RingObject> usedRing;   // last orb a click (or hold) was spent on
    Ref<RingObject> lateRing;   // orb to fire on the next step
};

class $modify(BufferLayer, GJBaseGameLayer) {
    struct Fields {
        int step = 0;
        int epoch = 0;
        int graceUntil = 0;
        PState ps[2];

        // Counters for the overlay. "Attempt" ones reset on death/restart.
        int savedEarly = 0;
        int savedLate = 0;
        int adjusted = 0;
        int totalSaved = 0;

        CCLabelBMFont* label = nullptr;
        std::string labelText;
        int flashUntil = 0;
        ccColor3B flashColor = {255, 255, 255};
    };

    // True in a one-player level with a dual portal: a single click is sent
    // as player 1 and the game applies it to both icons.
    bool sharedDualInput() {
        return m_gameState.m_isDualMode && m_levelSettings && !m_levelSettings->m_twoPlayerMode;
    }

    // The buffer only runs in a real level, and with "Practice Mode Only"
    // turned on, only while practice mode is on.
    bool assistActive() {
        if (!PlayLayer::get()) return false;
        if (g_practiceOnly && !m_isPracticeMode) return false;
        return true;
    }

    void flash(ccColor3B color) {
        auto f = m_fields.operator->();
        f->flashColor = color;
        f->flashUntil = f->step + ticksToSteps(48);
    }

    // Press or release for the icon at idx. In shared dual input, player 1's
    // events also go to player 2 (rescued clicks only if player 2 is in a
    // landing mode, so a cube rescue does not tap a wave).
    void sendInput(PlayerObject* p, int idx, bool down, bool groundOnly) {
        auto one = [&](PlayerObject* q) {
            if (down) q->pushButton(PlayerButton::Jump);
            else q->releaseButton(PlayerButton::Jump);
        };
        one(p);
        if (idx == 0 && m_player2 && sharedDualInput()) {
            if (!groundOnly || isGroundMode(m_player2)) one(m_player2);
        }
    }

    // Clears our state after a death / restart / checkpoint load.
    void syncState() {
        auto f = m_fields.operator->();
        if (f->epoch == g_resetEpoch) return;
        f->epoch = g_resetEpoch;

        bool inconsistent = false;
        for (auto& s : f->ps) {
            if (s.delayedRelease || s.releaseNext || !s.waveQueue.empty()) {
                inconsistent = true;
            }
            s = PState();
        }

        f->savedEarly = 0;
        f->savedLate = 0;
        f->adjusted = 0;

        // Wave Grid / Min Gap are skipped briefly after a spawn so the first
        // clicks go straight through.
        f->graceUntil = f->step + spawnGraceSteps();

        // We may have held back a release the game never saw. Releasing a
        // button that is not held is harmless, so make sure nothing is stuck.
        if (inconsistent) {
            if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            if (m_player2) m_player2->releaseButton(PlayerButton::Jump);
        }
        DBG("[InputBuffer] state reset (death/restart), spawn grace until step {}{}",
            f->graceUntil, inconsistent ? ", released stuck button" : "");
    }

    // Used when the buffer switches off mid-level (practice mode turned off):
    // sends anything still held back so no input is lost, then clears state.
    void drainAll() {
        auto f = m_fields.operator->();
        for (int i = 0; i < 2; i++) {
            auto& s = f->ps[i];
            auto p = i == 0 ? m_player1 : m_player2;
            if (p) {
                if (!s.waveQueue.empty()) flushWaveQueue(p, s, i);
                if (s.delayedRelease) sendInput(p, i, false, false);
                if (s.releaseNext && !s.held) sendInput(p, i, false, true);
            }
            s = PState();
        }
    }

    void flushWaveQueue(PlayerObject* p, PState& s, int idx) {
        for (auto const& ev : s.waveQueue) {
            sendInput(p, idx, ev.first, false);
        }
        s.waveQueue.clear();
    }

    // Remembers the last orb each icon overlapped and when it left it.
    void trackRings(PlayerObject* p, PState& s) {
        int step = m_fields->step;
        if (ringsTouching(p) > 0) {
            if (auto ring = firstRing(p)) {
                s.lastRing = ring;
                // Holding into an orb uses it.
                if (s.held) s.usedRing = ring;
            }
            s.touchingRing = true;
        } else if (s.touchingRing) {
            s.touchingRing = false;
            s.ringLeftStep = step;
        }
    }

    // Fires an orb the icon already left, for a click that came just too late.
    void fireLateRing(PlayerObject* p, PState& s, int idx) {
        Ref<RingObject> ring = s.lateRing;
        s.lateRing = nullptr;
        if (!ring) return;

        auto yVel = p->m_yVelocity;
        auto upside = p->m_isUpsideDown;
        auto dashing = p->m_isDashing;
        p->ringJump(ring, true);
        bool worked = p->m_yVelocity != yVel || p->m_isUpsideDown != upside || p->m_isDashing != dashing;

        auto f = m_fields.operator->();
        if (worked) {
            f->savedLate++;
            f->totalSaved++;
            flash({255, 160, 40});
            DBG("[InputBuffer] {} LATE ORB fired at step {} ({} steps after leaving it) [{}]",
                idx == 0 ? "P1" : "P2", f->step, f->step - s.ringLeftStep, modeName(p));
        } else {
            DBG("[InputBuffer] {} late orb rescue had no effect at step {}", idx == 0 ? "P1" : "P2", f->step);
        }
    }

    void stepPlayer(PlayerObject* p, PState& s, int idx) {
        int step = m_fields->step;
        char const* tag = idx == 0 ? "P1" : "P2";

        if (s.lateRing) fireLateRing(p, s, idx);

        if (!s.waveQueue.empty()) {
            if (!p->m_isDart) {
                flushWaveQueue(p, s, idx);
            } else {
                size_t applied = 0;
                while (applied < s.waveQueue.size() &&
                       step >= s.waveQueue[applied].second) {
                    auto ev = s.waveQueue[applied];
                    sendInput(p, idx, ev.first, false);
                    DBG("[InputBuffer] {} WAVE {} {} applied at step {}",
                        tag, waveModeName(), ev.first ? "PRESS" : "RELEASE", step);
                    applied++;
                }
                s.waveQueue.erase(s.waveQueue.begin(), s.waveQueue.begin() + applied);
            }
        }

        if (s.delayedRelease) {
            if (!isFlyMode(p) || step >= s.releaseAt) {
                s.delayedRelease = false;
                s.held = false;
                sendInput(p, idx, false, false);
                DBG("[InputBuffer] {} MIN-HOLD release applied at step {} [{}]",
                    tag, step, modeName(p));
            }
        }

        if (s.releaseNext) {
            s.releaseNext = false;
            if (!s.held) sendInput(p, idx, false, true);
        }

        if (!s.pending) return;

        int waited = step - s.pendingStart;
        int limit = modeLimitSteps(p);

        if (limit <= 0 || waited > limit || !isGroundMode(p)) {
            DBG("[InputBuffer] {} pending EXPIRED waited={} limit={} mode={}",
                tag, waited, limit, modeName(p));
            s.pending = false;
            return;
        }

        int rings = ringsTouching(p);
        if (rings != s.lastRingCount) {
            DBG("[InputBuffer] {} ring overlap count {} -> {} (waited {})",
                tag, s.lastRingCount, rings, waited);
            s.lastRingCount = rings;
        }

        bool onGround = p->m_isOnGround;
        bool onRing = rings > 0;

        if (onGround || onRing) {
            s.pending = false;
            float ticksSaved = waited / stepsPerTick();
            char const* what = onRing ? "RING" : "GROUND";

            if (s.held) {
                DBG("[InputBuffer] {} reached {} after {} steps but button still held, skipping ({})",
                    tag, what, waited, modeName(p));
            } else {
                sendInput(p, idx, true, true);
                s.releaseNext = true;
                if (onRing) s.usedRing = firstRing(p);
                auto f = m_fields.operator->();
                f->savedEarly++;
                f->totalSaved++;
                flash({80, 255, 80});
                DBG("[InputBuffer] *** SAVED +{:.1f} TICKS ({:.2f} frames @ 60fps) PRESS! [{} {} {}] (Step0: {} -> Delayed: {}) [Limit: {} steps] ***",
                    ticksSaved, ticksSaved / 4.0f, tag, modeName(p), what,
                    s.pendingStart, step, limit);
            }
        }
    }

    bool inputPlayer(PlayerObject* p, PState& s, int idx, bool down) {
        int step = m_fields->step;
        char const* tag = idx == 0 ? "P1" : "P2";

        if (!p->m_isDart && !s.waveQueue.empty()) {
            flushWaveQueue(p, s, idx);
        }

        int n = waveGridSteps();
        bool inGrace = step < m_fields->graceUntil;
        if (p->m_isDart && inGrace && n > 0 && g_waveMode != WAVE_HOLD) {
            DBG("[InputBuffer] {} WAVE {} {} passed through (spawn grace) at step {}",
                tag, waveModeName(), down ? "PRESS" : "RELEASE", step);
        }

        if (p->m_isDart && n > 0 && g_waveMode != WAVE_HOLD && !inGrace) {
            int target;
            if (g_waveMode == WAVE_GRID) {
                target = ((step + n - 1) / n) * n;
                if (!s.waveQueue.empty()) {
                    auto const& last = s.waveQueue.back();
                    target = std::max(target, last.second);
                    // A tap must hold for at least one grid cell, or the
                    // press and release would cancel out in the same step.
                    if (!down && last.first) {
                        target = std::max(target, last.second + n);
                    }
                }
            } else { // WAVE_GAP
                target = step;
                if (!s.waveQueue.empty()) {
                    target = std::max(target, s.waveQueue.back().second);
                }
                if (down) {
                    target = std::max(target, s.lastReleaseStep + n);
                } else {
                    s.lastReleaseStep = target;
                }
                if (target == step && s.waveQueue.empty()) {
                    s.held = down;
                    return false;
                }
            }

            s.held = down;
            s.waveQueue.emplace_back(down, target);
            if (target != step) m_fields->adjusted++;
            DBG("[InputBuffer] {} WAVE {} {} queued at step {} -> {} ({} steps)",
                tag, waveModeName(), down ? "PRESS" : "RELEASE", step, target, n);
            return true;
        }

        if (down && s.delayedRelease) {
            s.delayedRelease = false;
            s.held = false;
            sendInput(p, idx, false, false);
        }

        if (!down && isFlyMode(p) && s.pressStep >= 0) {
            int minHold = modeLimitSteps(p);
            int heldFor = step - s.pressStep;
            if (minHold > 0 && heldFor < minHold) {
                s.delayedRelease = true;
                s.releaseAt = s.pressStep + minHold;
                m_fields->adjusted++;
                DBG("[InputBuffer] {} MIN-HOLD delaying release: held {} < {} steps [{}]",
                    tag, heldFor, minHold, modeName(p));
                return true;
            }
        }

        s.held = down;

        if (down) {
            s.pressStep = isFlyMode(p) ? step : -1;

            // A press while overlapping an orb is spent on that orb.
            if (auto ring = firstRing(p)) s.usedRing = ring;

            // Late orb: the icon left an orb it never used a moment ago.
            // The press still goes through normally; the orb is fired on
            // the next step, after the game has processed the press.
            int lateLimit = lateOrbSteps();
            if (lateLimit > 0 && !p->m_isDart && !p->m_isOnGround &&
                ringsTouching(p) == 0 && !s.touchingRing && s.lastRing &&
                s.ringLeftStep >= 0 && step - s.ringLeftStep <= lateLimit &&
                s.lastRing.data() != s.usedRing.data() &&
                !s.lastRing->hasBeenActivatedByPlayer(p)) {
                s.lateRing = s.lastRing.data();
                s.lastRing = nullptr;
                s.pending = false;
                DBG("[InputBuffer] {} late click {} steps after orb, queuing rescue [{}]",
                    tag, step - s.ringLeftStep, modeName(p));
                return false;
            }

            bool early = isGroundMode(p) && !p->m_isOnGround && ringsTouching(p) == 0;
            if (early && modeLimitSteps(p) > 0) {
                s.pending = true;
                s.pendingStart = step;
                s.lastRingCount = 0;
            } else {
                s.pending = false;
            }
        }
        return false;
    }

    // Small label in the bottom-left corner of the level, so recordings
    // show whether the buffer is on and how many clicks it changed.
    void updateOverlay() {
        auto f = m_fields.operator->();
        auto pl = typeinfo_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this));
        if (!pl) return;

        if (!g_showOverlay) {
            if (f->label) f->label->setVisible(false);
            return;
        }

        if (!f->label) {
            auto label = CCLabelBMFont::create("", "bigFont.fnt");
            label->setAnchorPoint({0.f, 0.f});
            label->setScale(0.28f);
            label->setOpacity(210);
            label->setPosition({5.f, 5.f});
            label->setID("buffer-overlay"_spr);
            pl->addChild(label, 10000);
            f->label = label;
        }
        f->label->setVisible(true);

        std::string text;
        if (!assistActive()) {
            text = g_practiceOnly && !m_isPracticeMode ? "Buffer OFF (practice mode only)" : "Buffer OFF";
        } else {
            text = fmt::format(
                "Buffer {} | Late orb {}t | Wave {} | TPS {}\n"
                "Saved this attempt: {} early, {} late | Adjusted: {} | Level total: {}",
                STAGE_NAMES[g_stage], g_lateOrbTicks, waveModeName(), tpsLabel(),
                f->savedEarly, f->savedLate, f->adjusted, f->totalSaved
            );
        }
        if (text != f->labelText) {
            f->label->setString(text.c_str());
            f->labelText = text;
        }

        bool flashing = f->step < f->flashUntil;
        f->label->setColor(flashing ? f->flashColor : ccColor3B{255, 255, 255});
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto f = m_fields.operator->();
        f->step++;
        observeStepDt(dt);
        pollKeys();
        syncState();

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (!assistActive()) {
            drainAll();
            updateOverlay();
            return;
        }

        if (m_player1) stepPlayer(m_player1, f->ps[0], 0);
        if (m_player2) stepPlayer(m_player2, f->ps[1], 1);
        if (m_player1) trackRings(m_player1, f->ps[0]);
        if (m_player2) trackRings(m_player2, f->ps[1]);

        updateOverlay();
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto f = m_fields.operator->();
        syncState();

        if (button == 1 && assistActive()) {
            int idx = isPlayer1 ? 0 : 1;
            auto p = isPlayer1 ? m_player1 : m_player2;
            if (p) {
                if (inputPlayer(p, f->ps[idx], idx, down)) return;
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
