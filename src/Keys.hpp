#pragma once

#include <string>

// Hotkey input. Implemented with the Windows API; other platforms get
// harmless stubs (no hotkeys).
namespace bm::keys {

// Virtual key code for a setting value like "F6" or "Page Up". 0 = none.
int vkFromName(std::string const& name);

// True only when the focused window belongs to Geometry Dash.
bool gameFocused();

// True while the key is held down.
bool isDown(int vk);

} // namespace bm::keys
