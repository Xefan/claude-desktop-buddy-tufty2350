#pragma once
#include <cstdint>

// Shared constants and helpers for buddy species files.
// Each species file (src/buddies/<name>.cpp) includes this header and
// defines its 7 state functions using these helpers.

// Geometry, in upstream "1×" character units — implementation scales these
// to whatever the canvas-attach point asked for. Species code stays
// resolution-agnostic.
extern const int BUDDY_X_CENTER;
extern const int BUDDY_CANVAS_W;
extern const int BUDDY_Y_BASE;
extern const int BUDDY_Y_OVERLAY;
extern const int BUDDY_CHAR_W;
extern const int BUDDY_CHAR_H;

// Common colors species can use freely. RGB565 (upstream convention).
extern const uint16_t BUDDY_BG;
extern const uint16_t BUDDY_HEART;
extern const uint16_t BUDDY_DIM;
extern const uint16_t BUDDY_YEL;
extern const uint16_t BUDDY_WHITE;
extern const uint16_t BUDDY_CYAN;
extern const uint16_t BUDDY_GREEN;
extern const uint16_t BUDDY_PURPLE;
extern const uint16_t BUDDY_RED;
extern const uint16_t BUDDY_BLUE;

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff = 0);
void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset, uint16_t color, int xOff = 0);

// Stateful cursor + color setters for ad-hoc particle drawing.
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char* s);
