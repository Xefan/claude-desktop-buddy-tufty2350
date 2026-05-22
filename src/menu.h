#pragma once

namespace pimoroni { class PicoGraphics; }

// Hold-A → menu. Up/Down navigate, A cycles the selected value, B closes.
// Items: Brightness, Species, LED-on-prompt, Close.
//
// Settings written by the menu (brightness/species/led_on) are persisted
// to flash on exit so a power cycle preserves them.

void menuInit();

// Call once per frame, after buttonsUpdate(). Drives open/close logic and
// in-menu button handling. While the menu is open, main.cpp should skip its
// own button handlers (gated by menuIsOpen()).
void menuUpdate();

bool menuIsOpen();

// Overlay the menu panel on top of the main UI. No-op when closed.
void menuDraw(pimoroni::PicoGraphics& g, int screen_w, int screen_h);
