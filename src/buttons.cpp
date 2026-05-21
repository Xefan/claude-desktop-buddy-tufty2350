#include "buttons.h"

#include "pico/stdlib.h"

namespace {

// Pin map from pimoroni/tufty2350 board/pins.csv (BW_SWITCH_*).
constexpr uint PINS[(size_t)Btn::Count] = {
    7,    // A
    9,    // B
    10,   // C
    11,   // Up
    6,    // Down
};

uint8_t curr_mask = 0;
uint8_t prev_mask = 0;

inline uint8_t bit(Btn b) { return uint8_t(1u << uint8_t(b)); }

} // namespace

void buttonsInit() {
    for (uint pin : PINS) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);            // active-low with internal pull-up
    }
    curr_mask = 0;
    prev_mask = 0;
}

void buttonsUpdate() {
    prev_mask = curr_mask;
    uint8_t m = 0;
    for (size_t i = 0; i < (size_t)Btn::Count; i++) {
        if (gpio_get(PINS[i]) == 0) m |= (1u << i);   // pulled low = pressed
    }
    curr_mask = m;
}

bool btnHeld(Btn b)     { return (curr_mask & bit(b)) != 0; }
bool btnPressed(Btn b)  { return (curr_mask & bit(b)) && !(prev_mask & bit(b)); }
bool btnReleased(Btn b) { return !(curr_mask & bit(b)) && (prev_mask & bit(b)); }
