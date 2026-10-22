// Unit tests for src/Logic.hpp. Needs no game or Geode SDK:
//   g++ -std=c++20 -I../src logic_test.cpp -o logic_test && ./logic_test

#include "Logic.hpp"
#include <cstdio>
#include <vector>

using namespace bm::logic;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond) do { if (cond) { g_passed++; } else { g_failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testTicks() {
    CHECK(ticksToSteps(0, 240) == 0);
    CHECK(ticksToSteps(-1, 240) == 0);
    CHECK(ticksToSteps(4, 240) == 4);
    CHECK(ticksToSteps(4, 480) == 8);
    CHECK(ticksToSteps(1, 60) == 1);   // never rounds a real window down to 0
    CHECK(ticksToSteps(16, 60) == 4);
    CHECK(ticksToSteps(4, 0) == 0);
}

static void testTps() {
    CHECK(tpsFromStepDt(0.25, 1.0) == 240);
    CHECK(tpsFromStepDt(0.125, 1.0) == 480);
    CHECK(tpsFromStepDt(0.125, 0.5) == 240);  // slow time warp, still 240 steps per second
    CHECK(tpsFromStepDt(0.25, 2.0) == 240);   // fast warps are clamped to 1
    CHECK(tpsFromStepDt(0.0, 1.0) == 0);
    CHECK(tpsFromStepDt(-1.0, 1.0) == 0);
    CHECK(tpsFromStepDt(100.0, 1.0) == 0);    // nonsense rejected

    StableValue v(240, 3);
    CHECK(!v.feed(480));
    CHECK(!v.feed(480));
    CHECK(v.feed(480));
    CHECK(v.value() == 480);
    CHECK(!v.feed(240));
    CHECK(!v.feed(480));   // streak broken, back to the accepted value
    CHECK(v.value() == 480);
    CHECK(!v.feed(0));     // ignored
}

static void testOrdering() {
    CHECK(orderAfter(10, false, 0, false, false) == 10);
    CHECK(orderAfter(10, true, 12, true, false) == 12);  // never overtakes
    CHECK(orderAfter(10, true, 12, true, true) == 13);   // release after a queued press
    CHECK(orderAfter(20, true, 12, true, true) == 20);
    CHECK(orderAfter(12, true, 12, false, false) == 12);
}

static void testWave() {
    CHECK(waveGridTarget(10, 4, false, 0, false, false) == 12);
    CHECK(waveGridTarget(12, 4, false, 0, false, false) == 12);
    CHECK(waveGridTarget(13, 4, true, 16, true, true) == 20);   // tap holds one full cell
    CHECK(waveGridTarget(13, 0, false, 0, false, false) == 13);
    CHECK(waveGapTarget(10, 4, false, 0, 8, true) == 12);
    CHECK(waveGapTarget(10, 4, false, 0, 2, true) == 10);
    CHECK(waveGapTarget(10, 4, true, 14, 2, false) == 14);
}

static void testCoyote() {
    GroundTrack g;
    trackGround(g, true, 100, false, -1000, 0.0, 4.0);
    CHECK(g.wasOnGround);
    // runs off the edge
    trackGround(g, false, 101, false, -1000, 0.2, 4.0);
    CHECK(!g.leftByJump);
    CHECK(coyoteEligible(g, 101, 4));
    CHECK(coyoteEligible(g, 105, 4));
    CHECK(!coyoteEligible(g, 106, 4));
    CHECK(!coyoteEligible(g, 103, 0));

    // jumped off: the press came while grounded
    GroundTrack j;
    trackGround(j, true, 200, false, -1000, 0.0, 4.0);
    trackGround(j, false, 201, false, 200, 11.18, 4.0);
    CHECK(j.leftByJump);
    CHECK(!coyoteEligible(j, 202, 8));

    // launched by a pad: fast vertical speed, no press
    GroundTrack p;
    trackGround(p, true, 300, false, -1000, 0.0, 4.0);
    trackGround(p, false, 301, false, -1000, 16.0, 4.0);
    CHECK(p.leftByJump);

    // still holding (cube hold-jump)
    GroundTrack h;
    trackGround(h, true, 400, true, 350, 0.0, 4.0);
    trackGround(h, false, 401, true, 350, 0.1, 4.0);
    CHECK(h.leftByJump);

    // back on the ground clears eligibility
    GroundTrack r;
    trackGround(r, true, 500, false, -1000, 0.0, 4.0);
    trackGround(r, false, 501, false, -1000, 0.0, 4.0);
    trackGround(r, true, 502, false, -1000, 0.0, 4.0);
    CHECK(!coyoteEligible(r, 503, 8));
}

static void testLateOrb() {
    CHECK(lateOrbEligible(4, false, true, false, 100, 103, false));
    CHECK(!lateOrbEligible(4, false, true, false, 100, 105, false));  // too late
    CHECK(!lateOrbEligible(4, true, true, false, 100, 101, false));   // still on an orb
    CHECK(!lateOrbEligible(4, false, true, true, 100, 101, false));   // orb already used
    CHECK(!lateOrbEligible(4, false, false, false, 100, 101, false)); // no orb
    CHECK(!lateOrbEligible(0, false, true, false, 100, 101, false));  // off
    CHECK(!lateOrbEligible(4, false, true, false, 100, 101, true));   // on the ground
    CHECK(!lateOrbEligible(4, false, true, false, -1, 101, false));
}

static void testDecideEvent() {
    int calls = 0;
    // safe now: nothing else is simulated
    auto d0 = decideEvent(8, true, false, [&](int) { calls++; return true; }, [&] { calls += 100; return true; });
    CHECK(d0.action == EventAction::PassNow);
    CHECK(calls == 1);

    // first safe moment is 3 steps later
    auto d1 = decideEvent(8, true, false, [](int k) { return k >= 3; }, [] { return true; });
    CHECK(d1.action == EventAction::Delay);
    CHECK(d1.delay == 3);

    // nothing within the window, not clicking is safe
    auto d2 = decideEvent(4, true, false, [](int k) { return k >= 10; }, [] { return true; });
    CHECK(d2.action == EventAction::Drop);
    auto d3 = decideEvent(4, true, true, [](int k) { return k >= 10; }, [] { return true; });
    CHECK(d3.action == EventAction::Hold);

    // guard off: pass through
    auto d4 = decideEvent(4, false, true, [](int k) { return k >= 10; }, [] { return true; });
    CHECK(d4.action == EventAction::PassNow);

    // everything dies: the player's own choice stands
    auto d5 = decideEvent(4, true, false, [](int) { return false; }, [] { return false; });
    CHECK(d5.action == EventAction::PassNow);

    // window 0 still checks "now" and the guard
    auto d6 = decideEvent(0, true, false, [](int) { return false; }, [] { return true; });
    CHECK(d6.action == EventAction::Drop);
}

static void testHoldStep() {
    // diverged: go back to the player when safe, or when waiting doesn't help
    CHECK(holdStepSwitch(true, true, true, false));
    CHECK(!holdStepSwitch(true, true, false, false));
    CHECK(holdStepSwitch(true, false, false, false));
    // in sync: only auto save switches, and only to survive
    CHECK(!holdStepSwitch(false, false, true, false));
    CHECK(holdStepSwitch(false, false, true, true));
    CHECK(!holdStepSwitch(false, true, true, true));
    CHECK(!holdStepSwitch(false, false, false, true));
}

static void testAutoTap() {
    CHECK(impulseAutoTap(false, true, false));
    CHECK(!impulseAutoTap(true, true, false));   // not needed
    CHECK(!impulseAutoTap(false, false, false)); // doesn't help
    CHECK(!impulseAutoTap(false, true, true));   // can still wait
}

int main() {
    testTicks();
    testTps();
    testOrdering();
    testWave();
    testCoyote();
    testLateOrb();
    testDecideEvent();
    testHoldStep();
    testAutoTap();
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
