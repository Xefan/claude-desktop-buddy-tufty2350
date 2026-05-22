#pragma once
#include <cstdint>

namespace pimoroni { class PicoGraphics; }

// Multi-species ASCII buddy renderer. Each species lives in its own
// src/buddies/<name>.cpp file and exposes 7 state functions matching
// the PersonaState enum order: sleep, idle, busy, attention, celebrate,
// dizzy, heart.

void buddyInit();

// Where the buddy gets drawn. Call once at startup after PicoGraphics is
// constructed. canvas_x/y is the top-left corner of the drawing area.
void buddyAttach(pimoroni::PicoGraphics* g, int canvas_x, int canvas_y);

// Tick + draw the current species in the given PersonaState (0..6). The
// renderer internally throttles to ~5 fps; calling more often is cheap
// (no redundant draws between frame boundaries).
void buddyTick(uint8_t personaState);

// Force a redraw on the next tick (e.g. when the species changes or
// something else dirtied our canvas region).
void buddyInvalidate();

// Species cycling — A/B/C button menus typically call buddyNextSpecies().
void  buddySetSpeciesIdx(uint8_t idx);
void  buddyNextSpecies();
uint8_t  buddySpeciesIdx();
uint8_t  buddySpeciesCount();
const char* buddySpeciesName();

// State function signature: takes a tick counter and renders into the
// attached canvas using the helpers from buddy_common.h.
typedef void (*StateFn)(uint32_t t);

struct Species {
    const char* name;
    uint16_t    bodyColor;       // RGB565 (upstream convention; converted on use)
    StateFn     states[7];       // [sleep, idle, busy, attention, celebrate, dizzy, heart]
};
