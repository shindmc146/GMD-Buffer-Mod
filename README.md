# Input Buffer

A Geode mod for Geometry Dash 2.2081 (Windows) that makes tight timings easier.

It fixes clicks that are slightly too early, slightly too late, or that would crash, and shows on screen every click it changed.

> **Note:** this is an input assist. Use it for practice and testing, not for leaderboard or submitted runs. By default it only works in practice mode.

## What it does

| Feature | Behavior |
|---|---|
| Early buffer (cube, ball, robot, spider) | Tap a little before landing or touching an orb and the click fires when you touch the surface or ring. Ball and spider also count a click you're still holding. |
| Late orb | Click just after passing an orb you didn't use and the orb still fires, from where you are now. |
| Late jump (coyote time) | Click just after running off a ledge and you still jump (cube, robot), flip (ball) or teleport (spider). |
| Ship, UFO, Swing | A very short tap is stretched to a minimum length, so fast taps are all the same length. |
| Wave | Three styles: **Min Hold**, **Min Gap** and **Grid**. |
| Lookahead (experimental) | Simulates the next moments with the game's own physics and shifts, blocks or (optionally) makes clicks so you don't crash. |
| 2 Player | Both icons are buffered with the same rules, each by its own mode. |

### Lookahead

Lookahead runs invisible copies of your icon a fraction of a second ahead through the game's physics and collision code. While a copy runs, everything it could change in the level (orbs, pads, portals, triggers, coins, effects, stats) is intercepted.

- **Smart Shift:** a click (or a ship, wave or robot release) that would crash is delayed to the first moment within the shift window that survives.
- **Death Guard:** if no shift helps but not clicking survives, the click is ignored. In ship, wave and robot your input is held back until it becomes safe.
- **Auto Save:** if what you are doing now would crash and the opposite input survives, the mod clicks or lets go for you. Off by default.

It stops predicting at orbs, pads and portals, because the path after them depends on what you do there. Near those it doesn't interfere. When Lookahead is on, it replaces the flying-mode styles (minimum hold and wave styles).

It costs CPU; the overlay shows the simulated steps per second. Lower **Jump Lookahead** or **Flying Lookahead** if the game stutters.

### Overlay colors

| Color | Meaning |
|---|---|
| Green | Early click saved |
| Orange | Late orb rescued |
| Cyan | Late jump off a ledge |
| Yellow | Click shifted by Lookahead |
| Red | Click blocked by Lookahead |
| Purple | Auto Save |

## Install

1. You need Geometry Dash 2.2081 on Windows with the [Geode](https://geode-sdk.org) loader installed.
2. Build the mod with the included GitHub Actions workflow (Actions tab, **Build Geode Mod**, download **Build Output**), or download a release.
3. Put `lyophi.buffermod.geode` in the `geode/mods` folder inside your Geometry Dash folder, then restart the game.

## Settings

Open them from the Geode mod list, or press the **Buffer** button on the pause menu. Every setting has an explanation in the game.

### About ticks

A tick is 1/240 of a second (about 4 ms):

| Ticks | About |
|---|---|
| 2 | 8 ms, half a frame at 60 FPS |
| 4 | 17 ms, 1 frame at 60 FPS |
| 8 | 33 ms, 2 frames |
| 16 | 67 ms, 4 frames |
| 32 | 133 ms, 8 frames |

### Default hotkeys

Hotkeys only react while the Geometry Dash window is focused.

| Key | Action |
|---|---|
| F5 | Whole mod on/off |
| F6 | Cycle the main window |
| F7 / F8 | TPS up / down |
| F9 | Toggle debug logging |
| F10 | Cycle wave style |
| F11 | Lookahead on/off |

## Known limitations

- Windows only.
- Lookahead and the late assists use game internals that were checked against the game's bindings but could not be play-tested before release. Each one reports whether it worked in the overlay counters and in the debug log.
- Delayed inputs are injected by the mod, so effects that depend on the original input event may not see them at the exact moment.
- Other input mods (for example Click Between Frames) can interact with this one. If something feels off, test with the other mod disabled.

## Building from source

You need Geode CLI and SDK, CMake, and Visual Studio 2022 with the **Desktop development with C++** workload.

```
geode sdk install-binaries
geode build
```

The packaged `.geode` file is produced in the `build` folder.

The timing rules in `src/Logic.hpp` have unit tests that need no game or SDK:

```
cd tests
g++ -std=c++20 -I../src logic_test.cpp -o logic_test && ./logic_test
```

## License

MIT, see [LICENSE](LICENSE).
