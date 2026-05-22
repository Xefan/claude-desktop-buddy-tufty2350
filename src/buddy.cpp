#include "buddy.h"
#include "buddy_common.h"

#include <cstring>

#include "pico/stdlib.h"
#include "libraries/pico_graphics/pico_graphics.hpp"

// ──────────────── shared geometry (1× units) ────────────────
// These match upstream's M5StickC layout so all 18 species files render
// the same way. The implementation scales by BUDDY_SCALE — we run at 2×
// since we have a 320×240 screen instead of 135×240.
const int BUDDY_X_CENTER  = 67;
const int BUDDY_CANVAS_W  = 135;
const int BUDDY_Y_BASE    = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W    = 6;
const int BUDDY_CHAR_H    = 8;

// ──────────────── shared colors (RGB565) ────────────────
const uint16_t BUDDY_BG     = 0x0000;
const uint16_t BUDDY_HEART  = 0xF810;
const uint16_t BUDDY_DIM    = 0x8410;
const uint16_t BUDDY_YEL    = 0xFFE0;
const uint16_t BUDDY_WHITE  = 0xFFFF;
const uint16_t BUDDY_CYAN   = 0x07FF;
const uint16_t BUDDY_GREEN  = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED    = 0xF800;
const uint16_t BUDDY_BLUE   = 0x041F;

namespace {

constexpr int BUDDY_SCALE = 2;
constexpr uint32_t TICK_MS = 200;

// The buddy lives inside a rectangular "stage" of this size. main.cpp
// places the stage by passing its top-left corner to buddyAttach().
constexpr int BUDDY_STAGE_W = 200;
constexpr int BUDDY_STAGE_H = 140;

pimoroni::PicoGraphics* g_target = nullptr;
int canvas_x = 0;     // left edge of stage on screen
int canvas_y = 0;     // top edge of stage on screen

// Per-frame cursor + color state used by buddySetCursor/buddyPrint.
int  cursor_x = 0;
int  cursor_y = 0;
int  current_pen = 0xFFFFFFFF;

// RGB565 → PicoGraphics pen, with R/B swap so the vendored ST7789 driver
// reads them back correctly (its framebuffer is 0xBBGGRR; PicoGraphics
// writes 0xRRGGBB).
int rgb565_pen(uint16_t c) {
    if (!g_target) return 0;
    auto* gp = static_cast<pimoroni::PicoGraphics_PenRGB888*>(g_target);
    uint8_t r = ((c >> 11) & 0x1F) * 8;
    uint8_t g = ((c >> 5)  & 0x3F) * 4;
    uint8_t b = ( c        & 0x1F) * 8;
    return gp->create_pen(b, g, r);
}

// Render one char at (px, py) at our scale. We bypass PicoGraphics' text()
// auto-spacing (variable-width) by stepping in fixed BUDDY_CHAR_W cells,
// so ASCII art alignment holds.
void draw_glyph(char c, int px, int py) {
    if (!g_target) return;
    g_target->set_pen(current_pen);
    char s[2] = { c, 0 };
    g_target->set_font("bitmap8");
    g_target->text(s, pimoroni::Point(px, py), 999, BUDDY_SCALE);
}

} // namespace

void buddyAttach(pimoroni::PicoGraphics* g, int cx, int cy) {
    g_target = g;
    canvas_x = cx;
    canvas_y = cy;
}

// ──────────────── shared rendering helpers ────────────────
// All coords species pass in are in upstream 1× units; we multiply by
// BUDDY_SCALE and offset by canvas_x/y.

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff) {
    if (!g_target) return;
    int len = (int)std::strlen(line);
    // Trim trailing spaces for cleaner centering at non-1× scale.
    while (len && line[len - 1] == ' ') len--;
    while (len && *line == ' ')        { line++; len--; }
    int w = len * BUDDY_CHAR_W * BUDDY_SCALE;
    int x = canvas_x + (BUDDY_STAGE_W / 2) - (w / 2) + xOff * BUDDY_SCALE;
    int y = canvas_y + yPx;
    current_pen = rgb565_pen(color);
    for (int i = 0; i < len; i++) {
        draw_glyph(line[i], x + i * BUDDY_CHAR_W * BUDDY_SCALE, y);
    }
}

void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset, uint16_t color, int xOff) {
    int yBase = BUDDY_Y_BASE * BUDDY_SCALE - (BUDDY_SCALE - 1) * 14;
    for (uint8_t i = 0; i < nLines; i++) {
        buddyPrintLine(lines[i], yBase + (yOffset + i * BUDDY_CHAR_H) * BUDDY_SCALE,
                       color, xOff);
    }
}

void buddySetCursor(int x, int y) {
    cursor_x = canvas_x + BUDDY_STAGE_W / 2 + (x - BUDDY_X_CENTER) * BUDDY_SCALE;
    cursor_y = canvas_y + y * BUDDY_SCALE;
}

void buddySetColor(uint16_t fg) {
    current_pen = rgb565_pen(fg);
}

void buddyPrint(const char* s) {
    if (!s) return;
    int n = (int)std::strlen(s);
    for (int i = 0; i < n; i++) {
        draw_glyph(s[i], cursor_x + i * BUDDY_CHAR_W * BUDDY_SCALE, cursor_y);
    }
}

// ──────────────── species registry ────────────────
extern const Species CAPYBARA_SPECIES;
extern const Species DUCK_SPECIES;
extern const Species GOOSE_SPECIES;
extern const Species BLOB_SPECIES;
extern const Species CAT_SPECIES;
extern const Species DRAGON_SPECIES;
extern const Species OCTOPUS_SPECIES;
extern const Species OWL_SPECIES;
extern const Species PENGUIN_SPECIES;
extern const Species TURTLE_SPECIES;
extern const Species SNAIL_SPECIES;
extern const Species GHOST_SPECIES;
extern const Species AXOLOTL_SPECIES;
extern const Species CACTUS_SPECIES;
extern const Species ROBOT_SPECIES;
extern const Species RABBIT_SPECIES;
extern const Species MUSHROOM_SPECIES;
extern const Species CHONK_SPECIES;

static const Species* SPECIES_TABLE[] = {
    &CAPYBARA_SPECIES, &DUCK_SPECIES, &GOOSE_SPECIES, &BLOB_SPECIES,
    &CAT_SPECIES, &DRAGON_SPECIES, &OCTOPUS_SPECIES, &OWL_SPECIES,
    &PENGUIN_SPECIES, &TURTLE_SPECIES, &SNAIL_SPECIES, &GHOST_SPECIES,
    &AXOLOTL_SPECIES, &CACTUS_SPECIES, &ROBOT_SPECIES, &RABBIT_SPECIES,
    &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
static const uint8_t N_SPECIES = sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]);
static uint8_t currentSpeciesIdx = 0;

// ──────────────── tick state ────────────────
static uint32_t tickCount  = 0;
static uint32_t nextTickAt = 0;
static uint8_t  lastDrawnState   = 0xFF;
static uint8_t  lastDrawnSpecies = 0xFF;

static uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

void buddyInit() {
    tickCount = 0;
    nextTickAt = 0;
    lastDrawnState = 0xFF;
    lastDrawnSpecies = 0xFF;
}

void buddyInvalidate() { lastDrawnState = 0xFF; }

void buddySetSpeciesIdx(uint8_t idx) {
    if (idx < N_SPECIES) currentSpeciesIdx = idx;
    buddyInvalidate();
}
void buddyNextSpecies() {
    currentSpeciesIdx = (currentSpeciesIdx + 1) % N_SPECIES;
    buddyInvalidate();
}
uint8_t buddySpeciesIdx() { return currentSpeciesIdx; }
uint8_t buddySpeciesCount() { return N_SPECIES; }
const char* buddySpeciesName() { return SPECIES_TABLE[currentSpeciesIdx]->name; }

void buddyTick(uint8_t personaState) {
    if (!g_target) return;
    // tickCount drives animation phase — only advance on TICK_MS boundaries
    // so animations run at ~5fps even though we redraw at the loop rate.
    uint32_t now = now_ms();
    if ((int32_t)(now - nextTickAt) >= 0) {
        nextTickAt = now + TICK_MS;
        tickCount++;
    }
    if (personaState >= 7) personaState = 1;   // fallback to idle

    // We deliberately don't clear the stage here. main.cpp clears the whole
    // screen to its background every frame, so the area is already clean.
    // Clearing here to BUDDY_BG (black) would stamp a black rectangle on top
    // of main's bg, defeating the screen-wide clear.
    const Species* sp = SPECIES_TABLE[currentSpeciesIdx];
    if (sp->states[personaState]) sp->states[personaState](tickCount);
}
