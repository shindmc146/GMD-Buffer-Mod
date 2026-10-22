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
