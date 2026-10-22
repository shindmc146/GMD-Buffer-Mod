#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Keys.hpp"
#include "Logic.hpp"
#include "Sim.hpp"

using namespace geode::prelude;
using namespace bm;

#define DBG(...) do { if (g_cfg.debug) log::info(__VA_ARGS__); } while (0)

// Raised whenever the level resets (death, restart, checkpoint).
static int g_resetEpoch = 0;
// GD may send player 2 its own button calls in dual mode; once that is seen,
// player 1's inputs are no longer copied to player 2 by hand.
static bool g_p2Separate = false;

// Vertical speed above which leaving the ground counts as a jump or a pad.
static constexpr double JUMP_VELOCITY = 4.0;

// ---- small helpers ----------------------------------------------------------------

static int currentTps() {
    return g_cfg.tpsIndex == 0 ? sim::detectedTps() : TPS_PRESETS[g_cfg.tpsIndex];
}

static int toSteps(int ticks) {
    return logic::ticksToSteps(ticks, currentTps());
}

static bool isFlyMode(PlayerObject* p) {
    return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing;
}

static bool isGroundMode(PlayerObject* p) {
    return !isFlyMode(p);
}

static int modeIndex(PlayerObject* p) {
    if (p->m_isShip) return M_SHIP;
    if (p->m_isBird) return M_UFO;
    if (p->m_isDart) return M_WAVE;
    if (p->m_isSwing) return M_SWING;
    if (p->m_isBall) return M_BALL;
    if (p->m_isRobot) return M_ROBOT;
    if (p->m_isSpider) return M_SPIDER;
    return M_CUBE;
}

static char const* modeName(PlayerObject* p) {
    return MODE_NAMES[modeIndex(p)];
}

// Modes where how long you hold changes the path.
static bool holdSensitive(PlayerObject* p) {
    return p->m_isShip || p->m_isDart || p->m_isRobot;
}

// Modes that don't react to a button that is already held when they land,
// so a held early click has to be pressed again to count.
static bool needsRefire(PlayerObject* p) {
    return p->m_isBall || p->m_isSpider;
}

static int modeLimitSteps(PlayerObject* p) {
    return toSteps(ticksForMode(modeIndex(p)));
}

static int waveGridSteps() {
    return toSteps(ticksForMode(M_WAVE));
}

static int ringsTouching(PlayerObject* p) {
    return p->m_touchingRings ? static_cast<int>(p->m_touchingRings->count()) : 0;
}

static RingObject* firstRing(PlayerObject* p) {
    if (ringsTouching(p) == 0) return nullptr;
    return typeinfo_cast<RingObject*>(p->m_touchingRings->objectAtIndex(0));
}

// What the game itself thinks: is the jump button held for this icon?
static bool gameHeld(PlayerObject* p) {
    auto it = p->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
    return it != p->m_holdingButtons.end() && it->second;
}

static std::string tpsLabel() {
    if (g_cfg.tpsIndex == 0) return fmt::format("{} auto", sim::detectedTps());
    return TPS_NAMES[g_cfg.tpsIndex];
}

static std::string ticksLabel(int ticks) {
    return ticks > 0 ? fmt::format("{}t", ticks) : std::string("off");
}

static void notify(std::string const& text) {
    Notification::create(text, NotificationIcon::Info, 1.0f)->show();
}

// ---- hotkeys --------------------------------------------------------------------

static void doHotkey(int action) {
    switch (action) {
        case K_TOGGLE:
            saveBool("enabled", !g_cfg.enabled);
            notify(g_cfg.enabled ? "Input Buffer ON" : "Input Buffer OFF");
            break;
        case K_STAGE:
            saveStage((g_cfg.stage + 1) % STAGE_COUNT);
            log::info("[InputBuffer] Main window -> {}", STAGE_NAMES[g_cfg.stage]);
            break;
        case K_TPS_UP:
            saveTps(std::min(g_cfg.tpsIndex + 1, TPS_COUNT - 1));
            log::info("[InputBuffer] TPS -> {}", tpsLabel());
            break;
        case K_TPS_DOWN:
            saveTps(std::max(g_cfg.tpsIndex - 1, 0));
            log::info("[InputBuffer] TPS -> {}", tpsLabel());
            break;
        case K_DEBUG:
            saveBool("debug", !g_cfg.debug);
            log::info("[InputBuffer] Debug logging -> {}", g_cfg.debug ? "ON" : "OFF");
            break;
        case K_WAVE:
            saveWaveMode((g_cfg.waveMode + 1) % WAVE_MODE_COUNT);
            log::info("[InputBuffer] Wave mode -> {}", WAVE_NAMES[g_cfg.waveMode]);
            break;
        case K_LOOKAHEAD:
            saveBool("lookahead", !g_cfg.lookahead);
            notify(g_cfg.lookahead ? "Lookahead ON" : "Lookahead OFF");
            break;
        default:
            break;
    }
}

static void pollKeys() {
    static bool prev[KEY_COUNT] = {};
    bool focused = keys::gameFocused();
    bool now[KEY_COUNT];
    for (int i = 0; i < KEY_COUNT; i++) {
        now[i] = focused && keys::isDown(g_cfg.keys[i]);
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        if (now[i] && !prev[i]) doHotkey(i);
        prev[i] = now[i];
    }
}

// ---- pause menu button -------------------------------------------------------------

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

// ---- level resets ---------------------------------------------------------------

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
        // Lookahead ghosts "die" here too; that must never reach the game.
        if (sim::onDestroyPlayer(player)) return;
        PlayLayer::destroyPlayer(player, object);
        g_resetEpoch++;
    }
};

// ---- per-icon state ---------------------------------------------------------------

struct QueuedEvent {
    bool down;
    int step;
    bool fromWave;
};

struct PState {
    bool phys = false;               // what the player is physically holding
    std::vector<QueuedEvent> queue;  // presses/releases waiting for their step
    int lastEventStep = -1000000;
    int lastPressStep = -1000000;

    // early buffer
    bool pending = false;
    int pendingStart = 0;
    bool releaseNext = false;

    // flying modes: minimum hold
    int pressStep = -1;
    bool delayedRelease = false;
    int releaseAt = 0;

    // wave
    int lastReleaseStep = -1000000;

    // late orb
    Ref<RingObject> lastRing;
    bool touchingRing = false;
    int ringLeftStep = -1;
    Ref<RingObject> usedRing;
    Ref<RingObject> lateRing;

    // coyote time
    logic::GroundTrack ground;
    bool coyoteCheck = false;
    int coyoteStep = 0;         // step of the coyote press; checked after the game's next physics step
    double coyoteVel = 0.0;
    bool coyoteUpside = false;

    // lookahead
    bool dropRelease = false;   // a swallowed press: swallow its release too
    bool lookDiverged = false;  // lookahead keeps the game in a state the player didn't choose
    int autoLockUntil = 0;      // an auto-save switch is kept at least until this step
    int autoTapRelease = -1;    // step to release an auto-save tap
    int autoCooldownUntil = 0;  // no new auto-save tap before this step
};

enum FlashKind { F_EARLY, F_LATE, F_COYOTE, F_SHIFT, F_BLOCK, F_AUTO };

// ---- the engine ---------------------------------------------------------------------

class $modify(BufferLayer, GJBaseGameLayer) {
    struct Fields {
        int step = 0;
        int epoch = 0;
        int graceUntil = 0;
        bool wasActive = false;
        PState ps[2];

        // counters ("attempt" ones reset on death or restart)
        int early = 0;
        int lateOrb = 0;
        int coyote = 0;
        int shifted = 0;
        int blocked = 0;
        int autoSaves = 0;
        int adjusted = 0;
        int total = 0;

        // overlay
        CCLabelBMFont* label = nullptr;
        std::string labelText;
        int flashUntil = 0;
        ccColor3B flashColor = {255, 255, 255};

        // lookahead cost readout
        uint64_t simMark = 0;
        int simMarkStep = 0;
        int simRate = 0;
    };

    // ---- state queries ----

    bool sharedDualInput() {
        return m_gameState.m_isDualMode && m_levelSettings && !m_levelSettings->m_twoPlayerMode;
    }

    // Player 1's inputs must be copied to player 2 by hand.
    bool mirrorToP2() {
        return sharedDualInput() && !g_p2Separate && m_player2;
    }

    // Player 2 gets its own presses (2-player levels, or GD sends them separately).
    bool p2OwnInput() {
        return m_gameState.m_isDualMode && (!sharedDualInput() || g_p2Separate);
    }

    PlayLayer* playLayer() {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this) ? pl : nullptr;
    }

    bool assistActive() {
        if (!g_cfg.enabled || !playLayer()) return false;
        if (g_cfg.practiceOnly && !m_isPracticeMode) return false;
        return true;
    }

    bool lookaheadActive() {
        return assistActive() && g_cfg.lookahead && sim::usable(playLayer());
    }

    // Lookahead decides the timing of clicks (and replaces the wave and
    // minimum-hold styles while it does).
    bool smartTiming() {
        return lookaheadActive() && (g_cfg.smartShift || g_cfg.deathGuard);
    }

    bool simOk(PlayerObject* p, int idx) {
        if (!p || p->m_isDead || p->m_isDashing) return false;
        if (idx == 1 && !p2OwnInput()) return false;
        return ringsTouching(p) == 0;
    }

    // ---- feedback ----

    void flash(FlashKind kind) {
        static ccColor3B const colors[] = {
            {80, 255, 80},   // early: green
            {255, 160, 40},  // late orb: orange
            {80, 220, 255},  // coyote: cyan
            {255, 235, 60},  // shifted: yellow
            {255, 70, 70},   // blocked: red
            {230, 90, 255},  // auto save: purple
        };
        auto f = m_fields.self();
        f->flashColor = colors[kind];
        f->flashUntil = f->step + toSteps(48);
        f->total++;
    }

    // ---- sending input ----

    // Press or release for the icon at idx (and player 2 when GD wouldn't).
    // `groundOnly`: only copy to player 2 if it's in a landing mode, so a
    // rescued cube click doesn't tap a wave.
    void sendInput(PlayerObject* p, int idx, bool down, bool groundOnly) {
        auto one = [down](PlayerObject* q) {
            if (down) q->pushButton(PlayerButton::Jump);
            else q->releaseButton(PlayerButton::Jump);
        };
        one(p);
        if (idx == 0 && mirrorToP2() && (!groundOnly || isGroundMode(m_player2))) one(m_player2);
    }

    // ---- resets ----

    void syncState() {
        auto f = m_fields.self();
        if (f->epoch == g_resetEpoch) return;
        f->epoch = g_resetEpoch;

        bool stuck = false;
        for (auto& s : f->ps) {
            if (s.delayedRelease || s.releaseNext || !s.queue.empty() || s.lookDiverged || s.autoTapRelease >= 0) {
                stuck = true;
            }
            s = PState();
        }
        f->early = f->lateOrb = f->coyote = f->shifted = f->blocked = f->autoSaves = f->adjusted = 0;
        f->graceUntil = f->step + toSteps(16);

        // We may have held back a release the game never saw. Releasing a
        // button that isn't held is harmless, so make sure nothing is stuck.
        if (stuck) {
            if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            if (m_player2) m_player2->releaseButton(PlayerButton::Jump);
        }
        DBG("[InputBuffer] reset (death/restart){}", stuck ? ", released held button" : "");
    }

    // The buffer was switched off mid-level: hand everything back to the player.
    void drainAll() {
        auto f = m_fields.self();
        for (int i = 0; i < 2; i++) {
            auto& s = f->ps[i];
            auto p = i == 0 ? m_player1 : m_player2;
            if (p && (i == 0 || p2OwnInput())) {
                for (auto const& ev : s.queue) sendInput(p, i, ev.down, false);
                bool hadQueue = !s.queue.empty();
                s.queue.clear();
                // only touch the button where the mod itself held it or held it back
                bool ours = hadQueue || s.lookDiverged || s.delayedRelease || s.releaseNext || s.autoTapRelease >= 0;
                if (ours && gameHeld(p) != s.phys) sendInput(p, i, s.phys, false);
            }
            s = PState();
        }
    }

    // ---- lookahead requests ----

    // Jump-type modes look ahead until the next landing; flying modes only a
    // short way, because the player will keep steering.
    int horizonFor(PlayerObject* p, bool& stopOnLand) {
        if (isGroundMode(p)) {
            stopOnLand = true;
            return toSteps(g_cfg.lookaheadTicks);
        }
        stopOnLand = false;
        return toSteps(g_cfg.holdLookaheadTicks);
    }

    sim::Request baseRequest(PlayerObject* p, int idx) {
        sim::Request r;
        r.real[0] = p;
        r.count = 1;
        if (idx == 0 && mirrorToP2() && m_player2 && !m_player2->m_isDead) {
            r.real[1] = m_player2;
            r.count = 2;
        }
        bool stop = false;
        r.horizon = horizonFor(p, stop);
        r.stopOnLand = stop;
        return r;
    }

    bool simSafe(sim::Request const& r) {
        return sim::safe(sim::run(playLayer(), r));
    }

    // An event `k` steps from now (a tap if `tap`), then nothing.
    bool eventSafe(PlayerObject* p, int idx, bool down, bool tap, int k) {
        auto r = baseRequest(p, idx);
        r.plan.add(k, down);
        if (tap && down) r.plan.add(k + 1, false);
        r.horizon += k;
        return simSafe(r);
    }

    bool idleSafe(PlayerObject* p, int idx) {
        return simSafe(baseRequest(p, idx));
    }

    logic::EventDecision decideSmart(PlayerObject* p, int idx, bool down, bool tap) {
        int maxDelay = g_cfg.smartShift ? toSteps(g_cfg.shiftTicks) : 0;
        bool holdMode = holdSensitive(p) && !tap;
        return logic::decideEvent(
            maxDelay, g_cfg.deathGuard, holdMode,
            [&](int k) { return eventSafe(p, idx, down, tap, k); },
            [&] { return idleSafe(p, idx); }
        );
    }

    // ---- queue ----

    void enqueue(PState& s, bool down, int target, bool fromWave) {
        if (!s.queue.empty()) {
            auto const& last = s.queue.back();
            target = logic::orderAfter(target, true, last.step, last.down, !down);
        }
        s.queue.push_back({down, target, fromWave});
    }

    void processQueue(PlayerObject* p, PState& s, int idx) {
        if (s.queue.empty()) return;
        int step = m_fields->step;
        size_t applied = 0;
        while (applied < s.queue.size()) {
            auto const& ev = s.queue[applied];
            bool due = ev.step <= step || (ev.fromWave && !p->m_isDart);
            if (!due) break;
            sendInput(p, idx, ev.down, false);
            DBG("[InputBuffer] P{} queued {} applied at step {}", idx + 1, ev.down ? "PRESS" : "RELEASE", step);
            applied++;
        }
        s.queue.erase(s.queue.begin(), s.queue.begin() + applied);
    }

    // ---- once per step, after the game read this step's inputs ----

    void fireLateRing(PlayerObject* p, PState& s, int idx) {
        Ref<RingObject> ring = s.lateRing;
        s.lateRing = nullptr;
        if (!ring) return;

        auto yVel = p->m_yVelocity;
        auto upside = p->m_isUpsideDown;
        auto dashing = p->m_isDashing;
        p->ringJump(ring, true);
        bool worked = p->m_yVelocity != yVel || p->m_isUpsideDown != upside || p->m_isDashing != dashing;
        if (worked) {
            m_fields->lateOrb++;
            flash(F_LATE);
            DBG("[InputBuffer] P{} LATE ORB fired [{}]", idx + 1, modeName(p));
        } else {
            DBG("[InputBuffer] P{} late orb had no effect", idx + 1);
        }
    }

    void verifyCoyote(PlayerObject* p, PState& s, int idx) {
        s.coyoteCheck = false;
        bool worked = std::abs(p->m_yVelocity - s.coyoteVel) > 3.0 || p->m_isUpsideDown != s.coyoteUpside;
        if (worked) {
            m_fields->coyote++;
            flash(F_COYOTE);
            DBG("[InputBuffer] P{} COYOTE jump [{}]", idx + 1, modeName(p));
        } else {
            DBG("[InputBuffer] P{} coyote jump had no effect [{}]", idx + 1, modeName(p));
        }
    }

    // Hold-type modes with lookahead: go back to the player's input when that
    // is safe, or (auto save) switch the button when staying would crash.
    void lookaheadHold(PlayerObject* p, PState& s, int idx) {
        int step = m_fields->step;
        bool held = gameHeld(p);
        if (s.lookDiverged && held == s.phys) s.lookDiverged = false;
        bool diverged = s.lookDiverged;
        if (!diverged && !g_cfg.autoSave) return;
        if (diverged && step < s.autoLockUntil) return;

        bool sw;
        if (diverged) {
            // back to the player as soon as that survives, or when waiting doesn't help
            bool switchSafe = eventSafe(p, idx, !held, false, 0);
            sw = logic::holdStepSwitch(true, switchSafe || idleSafe(p, idx), switchSafe, false);
        } else {
            bool keepSafe = idleSafe(p, idx);
            sw = !keepSafe && logic::holdStepSwitch(false, false, eventSafe(p, idx, !held, false, 0), true);
        }
        if (!sw) return;

        sendInput(p, idx, !held, false);
        if (diverged) {
            s.lookDiverged = false;
            DBG("[InputBuffer] P{} back to the player's input [{}]", idx + 1, modeName(p));
        } else {
            s.lookDiverged = true;
            s.autoLockUntil = step + std::max(1, toSteps(4));
            m_fields->autoSaves++;
            flash(F_AUTO);
            DBG("[InputBuffer] P{} AUTO SAVE {} [{}]", idx + 1, held ? "release" : "hold", modeName(p));
        }
    }

    // Tap-type modes with auto save: tap at the last safe moment.
    void lookaheadTap(PlayerObject* p, PState& s, int idx) {
        if (!g_cfg.autoSave) return;
        int step = m_fields->step;
        if (s.phys || gameHeld(p)) return;
        if (s.pending || s.releaseNext || s.autoTapRelease >= 0) return;
        if (step - s.lastEventStep < 2 || step < s.autoCooldownUntil) return;
        if (isGroundMode(p) && !p->m_isOnGround) return; // jumps start from the ground
        if ((step & 1) != 0) return;                       // every other step is plenty

        if (idleSafe(p, idx)) return;
        if (!eventSafe(p, idx, true, true, 0)) return;
        int lead = std::max(1, toSteps(2));
        if (!logic::impulseAutoTap(false, true, eventSafe(p, idx, true, true, lead))) return;

        sendInput(p, idx, true, false);
        s.autoTapRelease = step + 1;
        s.autoCooldownUntil = step + std::max(2, toSteps(8));
        m_fields->autoSaves++;
        flash(F_AUTO);
        DBG("[InputBuffer] P{} AUTO SAVE tap [{}]", idx + 1, modeName(p));
    }

    void stepPlayer(PlayerObject* p, PState& s, int idx) {
        auto f = m_fields.self();
        int step = f->step;

        if (s.lateRing) fireLateRing(p, s, idx);
        if (s.coyoteCheck && step > s.coyoteStep) verifyCoyote(p, s, idx);

        processQueue(p, s, idx);

        if (s.delayedRelease && (!isFlyMode(p) || step >= s.releaseAt)) {
            s.delayedRelease = false;
            if (!s.phys) sendInput(p, idx, false, false);
            DBG("[InputBuffer] P{} min-hold release at step {} [{}]", idx + 1, step, modeName(p));
        }

        if (s.releaseNext) {
            s.releaseNext = false;
            if (!s.phys) sendInput(p, idx, false, true);
        }

        if (s.autoTapRelease >= 0 && step >= s.autoTapRelease) {
            s.autoTapRelease = -1;
            if (!s.phys) sendInput(p, idx, false, false);
        }

        // lookahead
        bool look = lookaheadActive() && simOk(p, idx) && s.queue.empty() && !s.delayedRelease;
        if (look) {
            if (holdSensitive(p)) lookaheadHold(p, s, idx);
            else lookaheadTap(p, s, idx);
        } else if (s.lookDiverged && s.queue.empty()) {
            // lookahead switched off (or can't run here): give control back
            if (gameHeld(p) != s.phys) sendInput(p, idx, s.phys, false);
            s.lookDiverged = false;
        }

        // early buffer
        if (!s.pending) return;
        int waited = step - s.pendingStart;
        int limit = modeLimitSteps(p);
        if (limit <= 0 || waited > limit || !isGroundMode(p)) {
            s.pending = false;
            return;
        }

        bool onGround = p->m_isOnGround;
        bool onRing = ringsTouching(p) > 0;
        if (!onGround && !onRing) return;

        s.pending = false;
        if (s.phys) {
            // Cube and robot jump by themselves when landing held; ball and
            // spider need a fresh press. Orbs take a held button by themselves.
            if (onRing || !needsRefire(p) || !g_cfg.holdRefire) return;
            sendInput(p, idx, false, true);
            sendInput(p, idx, true, true);
            f->early++;
            flash(F_EARLY);
            DBG("[InputBuffer] P{} SAVED held click (pressed again) after {} steps [{}]", idx + 1, waited, modeName(p));
            return;
        }

        // lookahead can still move or drop the buffered click
        if (onGround && smartTiming() && simOk(p, idx)) {
            auto d = decideSmart(p, idx, true, true);
            if (d.action == logic::EventAction::Delay) {
                enqueue(s, true, step + d.delay, false);
                enqueue(s, false, step + d.delay + 1, false);
                f->shifted++;
                flash(F_SHIFT);
                DBG("[InputBuffer] P{} buffered click shifted {} steps [{}]", idx + 1, d.delay, modeName(p));
                return;
            }
            if (d.action == logic::EventAction::Drop || d.action == logic::EventAction::Hold) {
                f->blocked++;
                flash(F_BLOCK);
                DBG("[InputBuffer] P{} buffered click dropped (would crash) [{}]", idx + 1, modeName(p));
                return;
            }
        }

        sendInput(p, idx, true, true);
        s.releaseNext = true;
        if (onRing) s.usedRing = firstRing(p);
        f->early++;
        flash(F_EARLY);
        DBG("[InputBuffer] P{} SAVED +{} steps [{} {}]", idx + 1, waited, modeName(p), onRing ? "ring" : "ground");
    }

    // Remember orbs and ground contact after the step's input was handled.
    void trackPlayer(PlayerObject* p, PState& s) {
        int step = m_fields->step;
        if (auto ring = firstRing(p)) {
            s.lastRing = ring;
            if (gameHeld(p)) s.usedRing = ring; // holding into an orb uses it
            s.touchingRing = true;
        } else if (s.touchingRing) {
            s.touchingRing = false;
            s.ringLeftStep = step;
        }
        logic::trackGround(s.ground, p->m_isOnGround, step, gameHeld(p), s.lastPressStep,
                           std::abs(p->m_yVelocity), JUMP_VELOCITY);
    }

    // ---- a press or release from the player ----

    void notePress(PlayerObject* p, PState& s, int step) {
        s.pressStep = isFlyMode(p) ? step : -1;
        if (auto ring = firstRing(p)) s.usedRing = ring; // a press on an orb is spent on it
    }

    // Wave Grid / Min Gap target for an event. Returns false when the wave
    // styles don't apply right now.
    bool waveTarget(PlayerObject* p, PState& s, bool down, int step, int& target) {
        int n = waveGridSteps();
        if (!p->m_isDart || n <= 0 || g_cfg.waveMode == WAVE_HOLD) return false;
        bool hasLast = !s.queue.empty();
        if (!hasLast && step < m_fields->graceUntil) return false; // right after a spawn, clicks go straight through
        int lastStep = hasLast ? s.queue.back().step : 0;
        bool lastDown = hasLast && s.queue.back().down;
        if (g_cfg.waveMode == WAVE_GRID) {
            target = logic::waveGridTarget(step, n, hasLast, lastStep, lastDown, !down);
        } else {
            target = logic::waveGapTarget(step, n, hasLast, lastStep, s.lastReleaseStep, down);
            if (!down) s.lastReleaseStep = target;
        }
        return true;
    }

    // Returns true when the event is consumed here (sent later, or never).
    bool inputPlayer(PlayerObject* p, PState& s, int idx, bool down) {
        auto f = m_fields.self();
        int step = f->step;
        s.lastEventStep = step;
        if (down) s.lastPressStep = step;

        // the release of a click lookahead swallowed
        if (!down && s.dropRelease) {
            s.dropRelease = false;
            return true;
        }
        if (down) s.dropRelease = false;

        // wave-styled events are sent at once when the icon isn't a wave anymore
        if (!p->m_isDart && !s.queue.empty()) processQueue(p, s, idx);

        // never overtake queued events
        if (!s.queue.empty()) {
            int target = step;
            bool wave = s.queue.back().fromWave && !smartTiming() && waveTarget(p, s, down, step, target);
            if (wave) s.queue.push_back({down, target, true});
            else enqueue(s, down, step, false);
            DBG("[InputBuffer] P{} {} queued behind earlier input", idx + 1, down ? "press" : "release");
            return true;
        }

        // a quick re-press during a minimum hold: release first so the press is fresh
        if (down && s.delayedRelease) {
            s.delayedRelease = false;
            sendInput(p, idx, false, false);
        }

        // lookahead kept the game in a state: the player may have caught up with it
        if (s.lookDiverged) {
            s.lookDiverged = false;
            if (gameHeld(p) == down) return true;
        }

        // an auto-save tap is still held: release it so this press counts
        if (down && s.autoTapRelease >= 0) {
            s.autoTapRelease = -1;
            if (gameHeld(p)) sendInput(p, idx, false, false);
        }

        bool touchingOrb = ringsTouching(p) > 0;
        bool airborneLander = isGroundMode(p) && !p->m_isOnGround && !touchingOrb;

        // ---- late orb: just passed an orb that wasn't used ----
        if (down && !touchingOrb && !p->m_isDart) {
            int lateLimit = toSteps(g_cfg.lateOrbTicks);
            bool lastUsed = s.lastRing && s.lastRing.data() == s.usedRing.data();
            bool lastActivated = s.lastRing && s.lastRing->hasBeenActivatedByPlayer(p);
            if (logic::lateOrbEligible(lateLimit, s.touchingRing, s.lastRing != nullptr,
                                       lastUsed || lastActivated, s.ringLeftStep, step, p->m_isOnGround)) {
                DBG("[InputBuffer] P{} late click {} steps after orb [{}]", idx + 1, step - s.ringLeftStep, modeName(p));
                notePress(p, s, step);
                s.lateRing = s.lastRing.data();
                s.lastRing = nullptr;
                s.pending = false;
                return false;
            }
        }

        if (smartTiming() && simOk(p, idx)) {
            // ---- lookahead timing ----
            bool tapMode = !holdSensitive(p);
            bool consider = down ? !airborneLander : !tapMode;
            if (consider) {
                auto d = decideSmart(p, idx, down, tapMode && down);
                switch (d.action) {
                    case logic::EventAction::Delay:
                        enqueue(s, down, step + d.delay, false);
                        f->shifted++;
                        flash(F_SHIFT);
                        DBG("[InputBuffer] P{} {} shifted {} steps [{}]", idx + 1, down ? "press" : "release", d.delay, modeName(p));
                        return true;
                    case logic::EventAction::Hold:
                        s.lookDiverged = true;
                        s.autoLockUntil = 0;
                        f->blocked++;
                        flash(F_BLOCK);
                        DBG("[InputBuffer] P{} {} held back (would crash) [{}]", idx + 1, down ? "press" : "release", modeName(p));
                        return true;
                    case logic::EventAction::Drop:
                        s.dropRelease = true;
                        f->blocked++;
                        flash(F_BLOCK);
                        DBG("[InputBuffer] P{} press dropped (would crash) [{}]", idx + 1, modeName(p));
                        return true;
                    case logic::EventAction::PassNow:
                        break;
                }
                if (down) notePress(p, s, step);
                return false;
            }
            if (isFlyMode(p)) {
                // lookahead replaces the wave and minimum-hold styles
                return false;
            }
        } else {
            // ---- wave styles ----
            int target = step;
            if (waveTarget(p, s, down, step, target)) {
                if (target == step) return false;
                s.queue.push_back({down, target, true});
                f->adjusted++;
                return true;
            }

            // ---- minimum hold (ship, UFO, swing, wave "Min Hold") ----
            if (!down && isFlyMode(p) && s.pressStep >= 0) {
                int minHold = modeLimitSteps(p);
                int heldFor = step - s.pressStep;
                if (minHold > 0 && heldFor < minHold) {
                    s.delayedRelease = true;
                    s.releaseAt = s.pressStep + minHold;
                    f->adjusted++;
                    return true;
                }
            }
        }

        if (!down) return false;
        notePress(p, s, step);
        if (touchingOrb) return false;

        // ---- coyote time: just ran off a ledge ----
        if (airborneLander && logic::coyoteEligible(s.ground, step, toSteps(g_cfg.coyoteTicks))) {
            DBG("[InputBuffer] P{} coyote: {} steps after the ledge [{}]", idx + 1, step - s.ground.leftStep, modeName(p));
            s.coyoteCheck = true;
            s.coyoteStep = step;
            s.coyoteVel = p->m_yVelocity;
            s.coyoteUpside = p->m_isUpsideDown;
            s.ground.leftStep = -1; // one coyote jump per ledge
            s.pending = false;
            p->m_isOnGround = true; // the press is read as if it came on the ledge
            return false;
        }

        // ---- early buffer: remember a click that came before landing ----
        if (airborneLander && modeLimitSteps(p) > 0) {
            s.pending = true;
            s.pendingStart = step;
        } else {
            s.pending = false;
        }
        return false;
    }

    // ---- overlay ----

    void updateOverlay() {
        auto f = m_fields.self();
        auto pl = playLayer();
        if (!pl) return;

        if (!g_cfg.showOverlay) {
            if (f->label) f->label->setVisible(false);
            return;
        }
        if (!f->label) {
            auto label = CCLabelBMFont::create("", "bigFont.fnt");
            label->setAnchorPoint({0.f, 0.f});
            label->setScale(0.25f);
            label->setOpacity(210);
            label->setPosition({5.f, 5.f});
            label->setID("buffer-overlay"_spr);
            pl->addChild(label, 10000);
            f->label = label;
        }
        f->label->setVisible(true);

        // lookahead cost, sampled about once a second
        if (f->step - f->simMarkStep >= 240) {
            uint64_t total = sim::totalSteps();
            f->simRate = static_cast<int>((total - f->simMark) * 240 / static_cast<uint64_t>(std::max(1, f->step - f->simMarkStep)));
            f->simMark = total;
            f->simMarkStep = f->step;
        }

        std::string text;
        if (!assistActive()) {
            text = g_cfg.enabled ? "Buffer OFF (practice mode only)" : "Buffer OFF";
        } else {
            std::string wave = smartTiming() ? "lookahead" : WAVE_NAMES[g_cfg.waveMode];
            text = fmt::format("Buffer {}  Late orb {}  Coyote {}  Wave {}  TPS {}",
                               STAGE_NAMES[g_cfg.stage], ticksLabel(g_cfg.lateOrbTicks),
                               ticksLabel(g_cfg.coyoteTicks), wave, tpsLabel());
            if (g_cfg.lookahead) {
                std::string parts;
                if (g_cfg.smartShift) parts += fmt::format(" shift {}t", g_cfg.shiftTicks);
                if (g_cfg.deathGuard) parts += " guard";
                if (g_cfg.autoSave) parts += " auto-save";
                text += fmt::format("\nLookahead{}{}  {:.1f}k steps/s", lookaheadActive() ? "" : " (paused)",
                                    parts, f->simRate / 1000.0);
            }
            text += fmt::format("\nEarly {}  Orb {}  Coyote {}  Shift {}  Block {}  Auto {}  Adj {}  |  Total {}",
                                f->early, f->lateOrb, f->coyote, f->shifted, f->blocked, f->autoSaves,
                                f->adjusted, f->total);
        }
        if (text != f->labelText) {
            f->label->setString(text.c_str());
            f->labelText = text;
        }
        f->label->setColor(f->step < f->flashUntil ? f->flashColor : ccColor3B{255, 255, 255});
    }

    // ---- hooks ----

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto f = m_fields.self();
        f->step++;
        sim::beginStep(this);
        if (playLayer()) pollKeys();
        syncState();

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (!assistActive()) {
            if (f->wasActive) drainAll();
            f->wasActive = false;
            updateOverlay();
            return;
        }
        f->wasActive = true;
        if (g_cfg.lookahead) sim::prepare(playLayer());

        if (m_player1) stepPlayer(m_player1, f->ps[0], 0);
        if (m_player2 && p2OwnInput()) stepPlayer(m_player2, f->ps[1], 1);
        if (m_player1) trackPlayer(m_player1, f->ps[0]);
        if (m_player2) trackPlayer(m_player2, f->ps[1]);

        updateOverlay();
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto f = m_fields.self();
        syncState();

        if (button == static_cast<int>(PlayerButton::Jump) && assistActive() && !sim::simulating()) {
            if (!isPlayer1 && sharedDualInput()) g_p2Separate = true;
            int idx = isPlayer1 ? 0 : 1;
            auto p = isPlayer1 ? m_player1 : m_player2;
            if (p) {
                f->ps[idx].phys = down;
                if (inputPlayer(p, f->ps[idx], idx, down)) return;
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

$on_mod(Loaded) {
    log::info("[InputBuffer] v2.0.0 loaded");
}
