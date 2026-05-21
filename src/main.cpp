#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#include "st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "battery.h"
#include "ble_bridge.h"
#include "buttons.h"
#include "data.h"
#include "power.h"

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
    powerInit();
    batteryInit();

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
    int OK     = rgb_pen(g, 0x40, 0xE0, 0x60);
    int WARN   = rgb_pen(g, 0xFA, 0xC0, 0x40);
    int HOT    = rgb_pen(g, 0xE0, 0x40, 0x40);

    TamaState tama;
    dataInit(&tama);

    // Permission-prompt state. promptId is what we last saw; when it changes
    // we reset the "already responded" latch so the new prompt is fresh.
    char prompt_id_seen[40] = "";
    bool responded = false;
    const char* response_label = "";   // "approved" / "denied" briefly after sending
    uint32_t responded_at_ms = 0;

    uint32_t tick = 0;
    while (true) {
        buttonsUpdate();
        powerUpdate();        // long-press RESET → dormant sleep (no return)
        dataPoll(&tama);
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Reset response latch when the prompt id changes (new prompt arrived,
        // or the old one was cleared by a heartbeat without `prompt`).
        if (strcmp(tama.promptId, prompt_id_seen) != 0) {
            std::strncpy(prompt_id_seen, tama.promptId, sizeof(prompt_id_seen) - 1);
            prompt_id_seen[sizeof(prompt_id_seen) - 1] = 0;
            responded = false;
            response_label = "";
        }

        // Pending prompt → A approves, B denies. Format per REFERENCE.md.
        if (tama.promptId[0] && !responded) {
            const char* decision = nullptr;
            if (btnPressed(Btn::A)) { decision = "once"; response_label = "approved"; }
            else if (btnPressed(Btn::B)) { decision = "deny"; response_label = "denied"; }
            if (decision) {
                char cmd[160];
                int n = snprintf(cmd, sizeof(cmd),
                    "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
                    tama.promptId, decision);
                bleWrite((const uint8_t*)cmd, (size_t)n);
                responded = true;
                responded_at_ms = now;
            }
        }

        g.set_pen(BG);
        g.clear();
        g.set_font("bitmap8");

        // Title + link/data state along the top
        g.set_pen(ACCENT);
        g.text("Claude Buddy", Point(12, 10), W, 3);

        // BLE link state (top-right)
        const char* hci = bleHciState();
        bool linked  = bleConnected();
        bool working = hci[0] == 'w';
        const char* link_label = linked ? "linked" : working ? "advertising" : hci;
        g.set_pen(linked ? OK : working ? WARN : HOT);
        g.text(link_label, Point(W - 110, 14), W, 2);

        // Power state below the BLE label: "USB" when plugged in, else "NN%".
        bool on_usb = batteryOnUsb();
        char pwr[16];
        if (on_usb) snprintf(pwr, sizeof(pwr), "USB");
        else        snprintf(pwr, sizeof(pwr), "%d%%", batteryPercent());
        int  pct = batteryPercent();
        int  pwr_pen = on_usb ? OK
                     : pct < 15 ? HOT
                     : pct < 30 ? WARN
                                : DIM;
        g.set_pen(pwr_pen);
        g.text(pwr, Point(W - 110, 32), W, 2);

        // Session counts row
        char buf[80];
        if (tama.connected) {
            snprintf(buf, sizeof(buf), "%u sessions  %u running  %u waiting",
                     tama.sessionsTotal, tama.sessionsRunning, tama.sessionsWaiting);
            g.set_pen(TEXT);
        } else {
            snprintf(buf, sizeof(buf), linked ? "waiting for heartbeat..."
                                              : "no Claude desktop connected");
            g.set_pen(DIM);
        }
        g.text(buf, Point(12, 55), W, 2);

        // Latest one-line message
        if (tama.msg[0]) {
            g.set_pen(tama.sessionsWaiting > 0 ? WARN : TEXT);
            g.text(tama.msg, Point(12, 85), W, 2);
        }

        // Pending permission prompt — the headline event
        if (tama.promptId[0]) {
            g.set_pen(responded ? OK : HOT);
            char prompt_line[100];
            snprintf(prompt_line, sizeof(prompt_line),
                     "%s%s", responded ? "sent: " : "approve? ",
                     responded ? response_label : tama.promptTool);
            g.text(prompt_line, Point(12, 115), W, 3);
            g.set_pen(DIM);
            g.text(tama.promptHint, Point(12, 150), W, 1);
            if (!responded) {
                g.set_pen(OK);
                g.text("A approve", Point(12, 175), W, 2);
                g.set_pen(HOT);
                g.text("B deny", Point(160, 175), W, 2);
            }
        } else {
            // Otherwise show transcript entries
            g.set_pen(DIM);
            int y = 115;
            for (int i = 0; i < tama.nLines && y < H - 40; i++) {
                g.text(tama.lines[i], Point(12, y), W, 1);
                y += 10;
            }
        }

        // Button row: A B C ^ v. Held → accent, idle → dim.
        const char* labels[] = {"A", "B", "C", "^", "v"};
        const Btn   btns[]   = {Btn::A, Btn::B, Btn::C, Btn::Up, Btn::Down};
        for (int i = 0; i < 5; i++) {
            g.set_pen(btnHeld(btns[i]) ? ACCENT : DIM);
            g.text(labels[i], Point(W - 110 + i * 22, H - 24), W, 3);
        }

        // Diagnostic line: heartbeats parsed, raw bytes RX, age of last
        // heartbeat. Lets us tell "no updates arriving" from "updates
        // arriving but counters stay 0".
        g.set_pen(DIM);
        char diag[64];
        uint32_t age_ms = tama.lastUpdated
            ? (to_ms_since_boot(get_absolute_time()) - tama.lastUpdated)
            : 0;
        snprintf(diag, sizeof(diag),
                 "hb %lu  rx %lu B  age %lus",
                 (unsigned long)dataHeartbeatCount(),
                 (unsigned long)dataBytesReceived(),
                 (unsigned long)(age_ms / 1000));
        g.text(diag, Point(12, H - 16), W, 1);

        lcd.update();

        // Case LEDs:
        //   Pending unanswered prompt → all four pulse in sync at ~2Hz.
        //   Otherwise                 → subtle chase as "I'm alive" heartbeat.
        bool attention = tama.promptId[0] && !responded;
        if (attention) {
            // Triangle wave 0..0xFFFF..0 over 30 frames (~2Hz at 60fps).
            int phase = tick % 30;
            int amp = phase < 15 ? phase : 30 - phase;
            uint16_t level = (uint16_t)((amp * 0xFFFF) / 15);
            for (uint i = 0; i < 4; i++) pwm_set_gpio_level(tufty::CASE_LEDS[i], level);
        } else {
            uint active = (tick / 30) % 4;
            for (uint i = 0; i < 4; i++) {
                uint dist = (i + 4 - active) % 4;
                uint16_t level = dist == 0 ? 0xC000
                               : dist == 1 ? 0x3000
                               : dist == 2 ? 0x0800
                                           : 0x0200;
                pwm_set_gpio_level(tufty::CASE_LEDS[i], level);
            }
        }

        tick++;
        sleep_ms(16);
    }
}
