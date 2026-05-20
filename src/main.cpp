#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#include "st7789.hpp"

// Pimoroni's tufty2350 board header pins PICO_PANIC_FUNCTION at mp_pico_panic
// because the upstream copy is the MicroPython board definition. We're not
// MicroPython — provide a stub that traps so the link succeeds.
extern "C" __attribute__((noreturn))
void mp_pico_panic(const char* /*fmt*/, ...) {
    __builtin_trap();
}

// Tufty 2350 pin map (authoritative source: pimoroni/tufty2350 board/pins.csv).
// The vendored ST7789 driver hardcodes its own LCD/backlight pins to match
// these, so we don't pass them in here.
namespace tufty {
    constexpr uint CASE_LEDS[] = {0, 1, 2, 3};   // 4-zone mono case LEDs
    constexpr uint POWER_EN    = 41;             // master hardware enable
}

// Pimoroni's powman.c drives POWER_EN high to wake the hardware rails;
// without it the LCD (among other things) stays dark.
static void power_en_high() {
    gpio_init(tufty::POWER_EN);
    gpio_set_dir(tufty::POWER_EN, GPIO_OUT);
    gpio_put(tufty::POWER_EN, 1);
}

static void init_case_leds() {
    for (uint pin : tufty::CASE_LEDS) {
        gpio_set_function(pin, GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(pin);
        pwm_config cfg = pwm_get_default_config();
        pwm_set_wrap(slice, 65535);
        pwm_init(slice, &cfg, true);
        pwm_set_gpio_level(pin, 0);
    }
}

// The vendored driver's framebuffer is 0x00BBGGRR (R in byte 0, B in byte 2);
// see the conversion math in st7789.cpp's update().
static constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

int main() {
    stdio_init_all();
    power_en_high();
    init_case_leds();

    pimoroni::ST7789 lcd;
    lcd.set_mode(true);          // fullres 320x240 landscape (software-rotated)
    lcd.set_vsync(false);        // Tufty 2350 panel TE line isn't wired out
    lcd.set_backlight(200);

    constexpr int W = 320, H = 240;
    uint32_t* fb = lcd.get_framebuffer();

    const uint32_t BG     = rgb(0x10, 0x10, 0x18);
    const uint32_t ACCENT = rgb(0xFA, 0x70, 0x20);
    const uint32_t TEXT   = rgb(0xFF, 0xFF, 0xFF);
    const uint32_t DIM    = rgb(0x60, 0x60, 0x70);
    const uint32_t RED    = rgb(0xE0, 0x20, 0x20);
    const uint32_t GREEN  = rgb(0x20, 0xC0, 0x40);
    const uint32_t BLUE   = rgb(0x30, 0x60, 0xE0);

    auto fill_rect = [&](int x, int y, int w, int h, uint32_t c) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
        for (int dy = 0; dy < h; dy++) {
            uint32_t* row = fb + (y + dy) * W + x;
            for (int dx = 0; dx < w; dx++) row[dx] = c;
        }
    };

    uint32_t tick = 0;
    while (true) {
        fill_rect(0, 0, W, H, BG);

        // Sanity-check orientation: small color squares in each corner so a
        // glance tells us which way is up. Top-left=red, top-right=green,
        // bottom-left=blue, bottom-right=white.
        fill_rect(0,       0,       16, 16, RED);
        fill_rect(W - 16,  0,       16, 16, GREEN);
        fill_rect(0,       H - 16,  16, 16, BLUE);
        fill_rect(W - 16,  H - 16,  16, 16, TEXT);

        // "Claude Buddy" placeholder block — orange band where the title goes.
        fill_rect(20, 70, W - 40, 50, ACCENT);

        // "Tufty 2350" placeholder block — white band below.
        fill_rect(20, 140, W - 40, 24, TEXT);

        // Frame counter progress bar at bottom — wraps around continuously.
        fill_rect(20, 200, W - 40, 10, DIM);
        int progress = (tick * 2) % (W - 40);
        fill_rect(20, 200, progress, 10, ACCENT);

        lcd.update();

        // LED chase as before — known-good heartbeat.
        uint active = (tick / 30) % 4;
        for (uint i = 0; i < 4; i++) {
            uint dist = (i + 4 - active) % 4;
            uint16_t level = dist == 0 ? 0xC000
                           : dist == 1 ? 0x3000
                           : dist == 2 ? 0x0800
                                       : 0x0200;
            pwm_set_gpio_level(tufty::CASE_LEDS[i], level);
        }

        tick++;
        sleep_ms(16);
    }
}
