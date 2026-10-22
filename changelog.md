## v2.0.0
- **Late jumps (coyote time):** run off a ledge and click just after, and you still jump (cube, robot), flip (ball) or teleport (spider). New setting: Late Jump Window, default 4 ticks.
- **Re-press held clicks:** ball and spider now count an early click you're still holding when you land.
- **Lookahead (experimental, off by default):** simulates the next moments with the game's own physics on invisible copies of your icon.
  - **Smart Shift** delays a click (or a ship, wave or robot release) that would crash to the first moment that survives.
  - **Death Guard** ignores clicks that would crash when not clicking is safe; in ship, wave and robot it holds your input back until it's safe.
  - **Auto Save** clicks or lets go for you when your current input would crash.
  - It stops predicting at orbs, pads and portals, so it never fights them.
- **Master switch** (F5) and **Lookahead switch** (F11), each with a small popup.
- Overlay shows every kind of rescue with its own color and counter, plus how much work Lookahead is doing.
- Auto TPS now reads the real physics step (works with Click Between Frames and time warps).
- Code split into smaller files, with unit tests for the timing rules (`tests/`).

## v1.1.0
- **Late orb rescue:** if you pass an orb without using it and click within the Late Orb Window (default 4 ticks), the orb fires anyway. Set it to 0 to turn it off.
- **Overlay:** a label in the bottom-left corner shows the current settings and counts every click the mod changed this attempt (early saves, late orb rescues, adjusted flying/wave inputs) plus a level total. It flashes green on an early save and orange on a late rescue. Can be turned off in settings.
- **Practice Mode Only** (on by default): the buffer only runs in practice mode, so normal-mode completions are never assisted by accident.
- **Auto TPS** (new default): the mod reads the game's physics speed itself instead of you entering it. The detected value is shown in the overlay.
- Main Buffer Window can now go up to 32 ticks.
- Hotkeys only react while the Geometry Dash window is focused, so pressing F7/F8 in another app no longer changes your TPS.
- The "SAVED" message is now only logged when Debug Logging is on.

## v1.0.2
- Same mod as v1.0.1, now built automatically by GitHub Actions instead of on a local PC.
- No gameplay or settings changes. Your saved settings carry over.
- Builds are pinned to Geode 5.11.2 and its matching bindings, so every release is reproducible.

## v1.0.1
- Fixed first clicks being dropped after death, restart or checkpoint.
- Fixed dual-portal sections so held-back inputs reach both icons.
- Fixed wave Grid quick taps cancelling out. Taps now hold at least one cell.
- Wave Grid and Min Gap are skipped for about 16 ticks after a spawn, so start-pos clicks go straight through.
