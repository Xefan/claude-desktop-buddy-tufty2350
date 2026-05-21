#include "power.h"

#include "pico/stdlib.h"
#include "pico/sleep.h"
#include "hardware/watchdog.h"

namespace {

// Pin map from pimoroni/tufty2350 board/pins.csv.
constexpr uint RESET_PIN     = 14;   // BW_RESET_SW — pull-up, active low
constexpr uint POWER_EN_PIN  = 41;   // master rail enable (LCD + I2C peripherals)
constexpr uint SWITCH_INT_PIN = 15;  // OR of A/B/C/Up/Down — pull-up, active low
constexpr uint LCD_BL_PIN    = 26;
constexpr uint CASE_LEDS[]   = {0, 1, 2, 3};

constexpr uint32_t LONG_PRESS_MS = 2000;

uint32_t press_start_ms = 0;   // 0 = not currently pressed

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

void hard_off_visible_hardware() {
    // LCD backlight off (driver normally owns this pin via PWM; we re-grab
    // it as plain GPIO output low so the backlight is unambiguously dark).
    gpio_set_function(LCD_BL_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(LCD_BL_PIN, GPIO_OUT);
    gpio_put(LCD_BL_PIN, 0);

    // Case LEDs off.
    for (uint pin : CASE_LEDS) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }

    // Drop the master rail. LCD, RTC, and other POWER_EN-gated peripherals
    // lose power. The CYW43/RM2 wireless stays alive on its own rail but
    // we'll reset the whole chip on wake anyway.
    gpio_put(POWER_EN_PIN, 0);
}

void enter_dormant_and_wait_for_wake() {
    // Prime the wake pin so dormant entry doesn't immediately fire on
    // whatever pull-up state already exists.
    gpio_init(SWITCH_INT_PIN);
    gpio_set_dir(SWITCH_INT_PIN, GPIO_IN);
    gpio_pull_up(SWITCH_INT_PIN);

    // Switch sys clock to xosc (low-power dormant-capable source), then
    // halt the chip until BW_SWITCH_INT goes low (any front button press).
    sleep_run_from_xosc();
    sleep_goto_dormant_until_pin(SWITCH_INT_PIN, /*edge*/ true, /*high*/ false);
}

void shutdown_and_sleep() {
    hard_off_visible_hardware();

    // Wait for the user to let go of RESET so the post-wake reboot doesn't
    // immediately re-trigger this code path.
    while (gpio_get(RESET_PIN) == 0) sleep_ms(10);
    sleep_ms(50);   // debounce

    enter_dormant_and_wait_for_wake();

    // Wake-up landed here. We don't try to restore clocks / peripherals
    // piecemeal — just reset and let main() reinit everything cleanly.
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

} // namespace

void powerInit() {
    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_IN);
    gpio_pull_up(RESET_PIN);
}

void powerUpdate() {
    bool pressed = (gpio_get(RESET_PIN) == 0);
    if (pressed) {
        if (press_start_ms == 0) press_start_ms = now_ms();
        else if ((now_ms() - press_start_ms) >= LONG_PRESS_MS) {
            shutdown_and_sleep();   // does not return
        }
    } else {
        press_start_ms = 0;
    }
}
