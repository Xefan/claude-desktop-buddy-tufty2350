#pragma once
#include <cstdint>

// Tufty 2350 user buttons. All 5 are wired active-low with pull-ups, per
// Pimoroni's powman.c (`user_button_state = ~gpio_get_all()`). Polled each
// frame from the main loop; held/pressed/released are derived from edges.

enum class Btn : uint8_t {
    A    = 0,
    B    = 1,
    C    = 2,
    Up   = 3,
    Down = 4,
    Count
};

void buttonsInit();

// Sample all buttons. Call once per frame BEFORE checking any of the
// pressed/released/held queries.
void buttonsUpdate();

// True for every frame the button is physically pressed.
bool btnHeld(Btn b);

// True only on the first frame after a transition from up to down (tap).
bool btnPressed(Btn b);

// True only on the first frame after a transition from down to up.
bool btnReleased(Btn b);
