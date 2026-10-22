#pragma once

// Pure timing and decision rules. Nothing here touches the game, so every
// rule can be unit tested on its own (see tests/logic_test.cpp).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bm::logic {

// ---- time -----------------------------------------------------------------

// A "tick" is 1/240 s of real time, the unit every setting uses.
// Physics steps per tick depend on the TPS the game actually runs at.
inline int ticksToSteps(int ticks, int tps) {
    if (ticks <= 0 || tps <= 0) return 0;
    return std::max(1, static_cast<int>(std::lround(ticks * (tps / 240.0))));
}

// GD hands every physics step a delta in 1/60 s units (0.25 at 240 TPS).
// Slow time warps (< 1) shrink that delta without changing the step rate,
// so they are divided back out. Returns 0 for values that make no sense.
inline int tpsFromStepDt(double stepDt, double timeWarp) {
    if (!(stepDt > 0.0)) return 0;
    double tw = timeWarp > 0.0 ? std::min(1.0, timeWarp) : 1.0;
    double tps = 60.0 * tw / stepDt;
    if (tps < 30.0 || tps > 4000.0) return 0;
    return static_cast<int>(std::lround(tps));
}

// Takes a new value only after it was seen `needed` times in a row, so one
// odd frame never changes anything.
class StableValue {
    int m_value;
    int m_candidate = 0;
    int m_streak = 0;
    int m_needed;

public:
    explicit StableValue(int initial, int needed = 30) : m_value(initial), m_needed(needed) {}

    // Returns true when the accepted value changed.
    bool feed(int v) {
        if (v <= 0) return false;
        if (v == m_candidate) {
            m_streak++;
        } else {
            m_candidate = v;
            m_streak = 1;
        }
        if (m_streak >= m_needed && m_candidate != m_value) {
            m_value = m_candidate;
            return true;
        }
        return false;
    }

    int value() const { return m_value; }
};

// ---- event queue ordering -------------------------------------------------

// Where a new event may go when it is appended behind the last queued one.
// Events never overtake each other, and a release can't share a step with
// the press right before it (they would cancel out).
inline int orderAfter(int target, bool hasLast, int lastStep, bool lastWasPress, bool isRelease) {
    if (!hasLast) return target;
    target = std::max(target, lastStep);
    if (isRelease && lastWasPress) target = std::max(target, lastStep + 1);
    return target;
}

// Wave "Grid": every press and release waits for the next grid line.
// A tap holds for at least one full cell.
inline int waveGridTarget(int step, int cell, bool hasLast, int lastStep, bool lastWasPress, bool isRelease) {
    if (cell <= 0) return step;
    int target = ((step + cell - 1) / cell) * cell;
    if (hasLast) {
        target = std::max(target, lastStep);
        if (isRelease && lastWasPress) target = std::max(target, lastStep + cell);
    }
    return target;
}

// Wave "Min Gap": a press waits until `gap` steps have passed since the last release.
inline int waveGapTarget(int step, int gap, bool hasLast, int lastStep, int lastReleaseStep, bool isPress) {
    int target = step;
    if (hasLast) target = std::max(target, lastStep);
    if (isPress) target = std::max(target, lastReleaseStep + gap);
    return target;
}

// ---- coyote time (late jump off a ledge) ----------------------------------

struct GroundTrack {
    bool wasOnGround = false;
    int groundStep = -1;     // last step the icon was on the ground
    int leftStep = -1;       // step it left the ground
    bool leftByJump = false; // it left because of a jump, pad or orb, not by running off
};

// Call once per step with the icon's current state.
// `absYVel` is the size of the vertical speed; running off an edge starts
// near 0, while jumps and pads start far above `jumpVel`.
inline void trackGround(GroundTrack& g, bool onGround, int step, bool buttonHeld,
                        int lastPressStep, double absYVel, double jumpVel) {
    if (onGround) {
        g.wasOnGround = true;
        g.groundStep = step;
        return;
    }
    if (g.wasOnGround) {
        g.wasOnGround = false;
        g.leftStep = step;
        g.leftByJump = buttonHeld || lastPressStep >= g.groundStep - 1 || absYVel > jumpVel;
    }
}

inline bool coyoteEligible(GroundTrack const& g, int step, int windowSteps) {
    return windowSteps > 0 && !g.wasOnGround && g.leftStep >= 0 && !g.leftByJump &&
           step - g.leftStep <= windowSteps;
}

// ---- late orb ---------------------------------------------------------------

inline bool lateOrbEligible(int windowSteps, bool touchingOrbNow, bool hasLastOrb, bool lastOrbUsed,
                            int leftStep, int step, bool onGround) {
    return windowSteps > 0 && !touchingOrbNow && hasLastOrb && !lastOrbUsed && !onGround &&
           leftStep >= 0 && step - leftStep <= windowSteps;
}

// ---- lookahead decisions ----------------------------------------------------

enum class EventAction : uint8_t {
    PassNow,  // the click is fine (or nothing helps): let it through
    Delay,    // fire it `delay` steps later, the first moment that survives
    Hold,     // hold-type modes: keep the game as it is until switching is safe
    Drop,     // tap-type modes: swallow the click, not clicking is safe
};

struct EventDecision {
    EventAction action = EventAction::PassNow;
    int delay = 0;
};

// What to do with a click (or a release) the player just made.
// `safeAt(k)` simulates the event k steps from now; `idleSafe()` simulates
// not doing it at all. Only evaluates what it needs.
template <class SafeAt, class IdleSafe>
EventDecision decideEvent(int maxDelay, bool guard, bool holdMode, SafeAt&& safeAt, IdleSafe&& idleSafe) {
    EventDecision d;
    if (safeAt(0)) return d;
    for (int k = 1; k <= maxDelay; k++) {
        if (safeAt(k)) {
            d.action = EventAction::Delay;
            d.delay = k;
            return d;
        }
    }
    if (guard && idleSafe()) {
        d.action = holdMode ? EventAction::Hold : EventAction::Drop;
    }
    return d;
}

// Hold-type modes (ship, wave), once per step.
// `diverged`: the game is not in the state the player is holding.
// `keepSafe`: staying as the game is now survives.
// `switchSafe`: switching the game to the other state now survives.
// Returns true when the game state should switch this step.
inline bool holdStepSwitch(bool diverged, bool keepSafe, bool switchSafe, bool autoSave) {
    if (diverged) return switchSafe || !keepSafe; // back to the player as soon as it's safe
    return autoSave && !keepSafe && switchSafe;   // save the player
}

// Tap-type modes, auto save: tap now if not tapping dies, a tap now survives,
// and waiting `lead` more steps would be too late. That keeps rescue taps
// close to the last safe moment instead of hopping early.
inline bool impulseAutoTap(bool idleSafe, bool tapNowSafe, bool tapLaterSafe) {
    return !idleSafe && tapNowSafe && !tapLaterSafe;
}

} // namespace bm::logic
