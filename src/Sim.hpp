#pragma once

// Lookahead: runs hidden copies ("ghosts") of the players through the game's
// own physics and collision code to see whether an input would crash.
// Everything a ghost touches that could change the level (orbs, pads,
// portals, triggers, coins, effects, stats) is intercepted while simulating.

#include <Geode/Geode.hpp>
#include <cstdint>

namespace bm::sim {

enum class Outcome : uint8_t {
    Survive, // reached the horizon (or landed safely) without dying
    Die,     // a hazard killed the ghost
    Unknown, // reached an orb, pad or portal: the future depends on it, treat as safe
};

inline bool safe(Outcome o) { return o != Outcome::Die; }

// Jump-button presses and releases at step offsets from now (0 = this step).
struct Plan {
    struct Ev {
        int step;
        bool down;
    };
    Ev ev[4] = {};
    int count = 0;

    void add(int step, bool down) {
        if (count < 4) ev[count++] = {step, down};
    }
    int lastStep() const {
        int m = -1;
        for (int i = 0; i < count; i++) m = ev[i].step > m ? ev[i].step : m;
        return m;
    }
};

struct Request {
    PlayerObject* real[2] = {nullptr, nullptr}; // icons that receive the same input
    int count = 0;
    Plan plan;
    int horizon = 0;         // steps to simulate
    bool stopOnLand = false; // jump-type modes: a safe landing after the last event ends the check
};

// Called at the start of every physics step (processCommands).
void beginStep(GJBaseGameLayer* layer);

// Per-step delta in GD's units (0.25 at 240 TPS), 0 until known.
float stepDt();

// TPS worked out from the step delta. 240 until known.
int detectedTps();

bool isGhost(PlayerObject* p);
bool simulating();

// Returns true if `p` is a ghost (its death is recorded and must not reach the game).
bool onDestroyPlayer(PlayerObject* p);

// Whether lookahead can run in this level right now.
bool usable(PlayLayer* pl);

// Creates the ghosts ahead of time so the first simulation doesn't stall.
void prepare(PlayLayer* pl);

Outcome run(PlayLayer* pl, Request const& req);

// Total simulated steps so far (for the overlay's cost readout).
uint64_t totalSteps();

} // namespace bm::sim
