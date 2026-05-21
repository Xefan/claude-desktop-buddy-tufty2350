#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#include "st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "ble_bridge.h"
#include "buttons.h"

// Pimoroni's tufty2350 board header pins PICO_PANIC_FUNCTION at mp_pico_panic
// because the upstream copy is the MicroPython board definition. We're not
// MicroPython — provide a stub that traps so the link succeeds.
extern "C" __attribute__((noreturn))
void mp_pico_panic(const char* /*fmt*/, ...) {
    __builtin_trap();
}

// Tufty 2350 pin map (authoritative source: pimoroni/tufty2350 board/pins.csv).
// LCD pins are hardcoded inside the vendored ST7789 driver to match these.
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

using namespace pimoroni;

// PicoGraphics_PenRGB888 packs pixels as 0x00RRGGBB; the vendored ST7789
// driver reads pixels as 0x00BBGGRR (see conversion in st7789.cpp update()).
// We compensate at color-creation time so the rest of the rendering code
// stays natural — pass real RGB, R and B get swapped exactly once here.
static int rgb_pen(PicoGraphics_PenRGB888& g, uint8_t r, uint8_t g_, uint8_t b) {
    return g.create_pen(b, g_, r);
}

int main() {
    stdio_init_all();
    power_en_high();
    init_case_leds();
    buttonsInit();

    // Bring BLE up before the LCD so first-frame draw can already show state.
    bleInit("Claude");

    ST7789 lcd;
    lcd.set_mode(true);           // fullres 320x240 landscape (software-rotated)
    lcd.set_vsync(false);         // Tufty 2350 panel TE line isn't wired out
    lcd.set_backlight(200);

    constexpr int W = 320, H = 240;
    PicoGraphics_PenRGB888 g(W, H, lcd.get_framebuffer());

    int BG     = rgb_pen(g, 0x10, 0x10, 0x18);
    int ACCENT = rgb_pen(g, 0xFA, 0x70, 0x20);
    int TEXT   = rgb_pen(g, 0xFF, 0xFF, 0xFF);
    int DIM    = rgb_pen(g, 0x60, 0x60, 0x70);

    uint32_t tick = 0;
    while (true) {
        buttonsUpdate();

        g.set_pen(BG);
        g.clear();

        g.set_font("bitmap8");
        g.set_pen(ACCENT);
        g.text("Claude Buddy", Point(12, 40), W, 5);

        g.set_pen(TEXT);
        g.text("Tufty 2350", Point(12, 110), W, 2);

        g.set_pen(DIM);
        g.text("pico-sdk port", Point(12, 140), W, 1);

        // BLE state — show actual btstack HCI state so we can tell init
        // success from advertising-but-no-client from full connection.
        char ble_label[40];
        const char* hci = bleHciState();
        bool linked    = bleConnected();
        bool working   = hci[0] == 'w';   // "work" = HCI_STATE_WORKING
        snprintf(ble_label, sizeof(ble_label),
                 "BLE: %s", linked ? "connected" : working ? "advertising" : hci);
        int col = linked  ? rgb_pen(g, 0x40, 0xE0, 0x60)   // green
                : working ? rgb_pen(g, 0xFA, 0xC0, 0x40)   // amber
                          : rgb_pen(g, 0xE0, 0x40, 0x40);  // red — not up
        g.set_pen(col);
        g.text(ble_label, Point(12, 175), W, 2);

        // Button row: A B C ^ v. Held → accent, idle → dim.
        const char* labels[] = {"A", "B", "C", "^", "v"};
        const Btn   btns[]   = {Btn::A, Btn::B, Btn::C, Btn::Up, Btn::Down};
        for (int i = 0; i < 5; i++) {
            g.set_pen(btnHeld(btns[i]) ? ACCENT : DIM);
            g.text(labels[i], Point(W - 110 + i * 22, H - 24), W, 3);
        }

        g.set_pen(DIM);
        char buf[32];
        snprintf(buf, sizeof(buf), "frame %lu", (unsigned long)tick);
        g.text(buf, Point(12, H - 16), W, 1);

        lcd.update();

        // 4-zone case-LED chase heartbeat.
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
