#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#include "st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "battery.h"
#include "ble_bridge.h"
#include "buddy.h"
#include "buttons.h"
#include "data.h"
#include "menu.h"
#include "power.h"
#include "rtc.h"
#include "settings.h"
#include "stats.h"

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
    sleep_ms(50);          // give the I2C peripherals' rail a moment to stabilize
    init_case_leds();
    buttonsInit();
    powerInit();
    batteryInit();
    rtcInit();             // depends on POWER_EN; must come after power_en_high()
    settingsInit();        // load owner name etc. from flash

    // Bring BLE up before the LCD so first-frame draw can already show state.
    bleInit("Claude");

    ST7789 lcd;
    lcd.set_mode(true);           // fullres 320x240 landscape (software-rotated)
    lcd.set_vsync(false);         // Tufty 2350 panel TE line isn't wired out
    lcd.set_backlight(200);

    constexpr int W = 320, H = 240;
    PicoGraphics_PenRGB888 g(W, H, lcd.get_framebuffer());

    // ASCII buddy lives in a 180×200 stage on the left half of the screen,
    // below the title bar. The info column gets the right ~130px.
    buddyInit();
    buddyAttach(&g, 0, 40);
    buddySetSpeciesIdx(settings().species_idx);
    menuInit();
    statsInit();

    // Brightness levels map 0..4 → backlight PWM. Level 0 stays visible
    // so a settings mishap doesn't leave the screen black.
    static const uint8_t BRIGHT_LEVELS[5] = {30, 80, 130, 180, 240};

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
    uint32_t prompt_arrived_ms = 0;
    uint32_t heart_until_ms = 0;       // brief P_HEART override after fast approval

    // Multi-view UI. A button cycles between them when no prompt/menu is
    // active. Upstream's text/transcript HUD view doesn't fit landscape, so
    // we drop it — session activity is visible through the buddy's persona.
    enum View : uint8_t { V_PET = 0, V_INFO, V_COUNT };
    View view = V_PET;
    uint32_t celebrate_until_ms = 0;   // brief P_CELEBRATE after a level-up

    // Tracks the last time we saw *anything* happen on the link — a running
    // session, a prompt, a completion. Used to keep the buddy looking idle
    // (not asleep) through brief heartbeat gaps. Desktop's keepalive can lull
    // during long generations between tool calls, and we don't want the pet
    // to flip to sleep persona mid-think.
    uint32_t last_activity_ms = 0;
    constexpr uint32_t ACTIVITY_GRACE_MS = 120000;   // 2 minutes

    // Map session state + transient overrides to PersonaState.
    auto derive_persona = [&heart_until_ms, &celebrate_until_ms, &last_activity_ms]
                          (const TamaState& s, uint32_t now) -> uint8_t {
        if ((int32_t)(heart_until_ms - now) > 0)     return 6;   // heart
        if ((int32_t)(celebrate_until_ms - now) > 0) return 4;   // celebrate
        if (s.promptId[0])          return 3;   // attention
        if (s.sessionsRunning >= 1) return 2;   // busy
        if (s.recentlyCompleted)    return 4;   // celebrate
        // Recent activity holds the pet out of sleep persona even if the
        // heartbeat link has gone quiet — Desktop's keepalive isn't reliable
        // during long thinking gaps between tool calls.
        bool recent = last_activity_ms != 0
                      && (now - last_activity_ms) < ACTIVITY_GRACE_MS;
        if (recent)                 return 1;   // idle (recently active)
        if (!s.connected)           return 0;   // sleep
        return 1;                                // idle
    };

    constexpr uint32_t SCREEN_AUTO_OFF_MS = 30000;
    bool     screen_off          = false;
    uint32_t last_interaction_ms = to_ms_since_boot(get_absolute_time());

    uint32_t tick = 0;
    while (true) {
        buttonsUpdate();
        powerUpdate();        // long-press RESET (note: hardware-tied to chip reset)
        dataPoll(&tama);
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool any_btn = btnPressed(Btn::A) || btnPressed(Btn::B) || btnPressed(Btn::C)
                    || btnPressed(Btn::Up) || btnPressed(Btn::Down);
        // C is the screen-toggle button. RESET can't be used — Pimoroni wires
        // it to the RP2350's hardware reset line (that's how their BOOTSEL
        // gesture works), so a tap of RESET reboots the chip unconditionally.
        bool screen_toggle = btnPressed(Btn::C);

        // ── Activity detection ──
        // Compare against previous-frame state to spot edges: a new running
        // session, a fresh completion, a new prompt id, the link coming back.
        // Edges count as activity (and optionally as user interactions, gated
        // by the wake-on-activity setting).
        static uint8_t prev_running   = 0;
        static uint8_t prev_waiting   = 0;
        static bool    prev_completed = false;
        static bool    prev_connected = false;
        static char    prev_prompt[40] = "";
        bool activity = false;
        if (tama.sessionsRunning > prev_running)             activity = true;
        if (tama.sessionsWaiting > prev_waiting)             activity = true;
        if (tama.recentlyCompleted && !prev_completed)       activity = true;
        if (tama.connected && !prev_connected)               activity = true;
        if (tama.promptId[0] && std::strcmp(tama.promptId, prev_prompt) != 0)
                                                              activity = true;
        prev_running   = tama.sessionsRunning;
        prev_waiting   = tama.sessionsWaiting;
        prev_completed = tama.recentlyCompleted;
        prev_connected = tama.connected;
        std::strncpy(prev_prompt, tama.promptId, sizeof(prev_prompt) - 1);
        prev_prompt[sizeof(prev_prompt) - 1] = 0;

        if (activity) {
            last_activity_ms = now;
            // Optionally treat as a user interaction → wakes the screen and
            // resets the idle-auto-off timer. Pending prompts always wake
            // regardless (handled below) since they need a response.
            if (settings().wake_on_activity) {
                last_interaction_ms = now;
                if (screen_off) {
                    screen_off = false;
                    lcd.set_backlight(BRIGHT_LEVELS[settings().brightness]);
                }
            }
        }

        // ── Screen on/off (manual via C tap + auto-off on battery idle) ──
        if (screen_off) {
            // Wake on any input or when a permission prompt arrives.
            if (any_btn || tama.promptId[0]) {
                screen_off = false;
                lcd.set_backlight(BRIGHT_LEVELS[settings().brightness]);
                last_interaction_ms = now;
            }
            sleep_ms(16);
            tick++;
            continue;       // skip menu/render entirely while dark
        }
        if (screen_toggle) {
            // User asked to blank the LCD.
            screen_off = true;
            lcd.set_backlight(0);
            sleep_ms(16);
            tick++;
            continue;
        }
        if (any_btn || tama.promptId[0]) last_interaction_ms = now;
        // Auto-off: only on battery, and only when nothing's pending. USB
        // keeps the screen lit so the clock face / status stays visible.
        if (!batteryOnUsb() && !tama.promptId[0]
                && (now - last_interaction_ms) > SCREEN_AUTO_OFF_MS) {
            screen_off = true;
            lcd.set_backlight(0);
            sleep_ms(16);
            tick++;
            continue;
        }

        menuUpdate();         // hold-A opens menu; consumes Up/Down/A/B while open

        // Apply user-set brightness each frame — cheap PWM register write.
        lcd.set_backlight(BRIGHT_LEVELS[settings().brightness]);

        // Reset response latch when the prompt id changes (new prompt arrived,
        // or the old one was cleared by a heartbeat without `prompt`).
        if (strcmp(tama.promptId, prompt_id_seen) != 0) {
            std::strncpy(prompt_id_seen, tama.promptId, sizeof(prompt_id_seen) - 1);
            prompt_id_seen[sizeof(prompt_id_seen) - 1] = 0;
            responded = false;
            response_label = "";
            if (tama.promptId[0]) prompt_arrived_ms = now;
        }

        // A button behavior is context-sensitive:
        //   prompt pending → approve
        //   no prompt      → cycle home/pet/info views
        // B is similarly: prompt → deny; otherwise no-op (reserved for sub-page nav).
        // Up/Down cycle the buddy species when there's no prompt to keep them
        // from deflecting an approval. Species swap is cosmetic only — stats
        // are global, the buddy is just a skin change.
        static uint32_t species_save_at_ms = 0;
        if (!menuIsOpen()) {
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
                    uint32_t took_s = (now - prompt_arrived_ms) / 1000;
                    if (decision[0] == 'o') {                  // approved
                        statsOnApproval(took_s);
                        if (took_s < 5) heart_until_ms = now + 2000;
                    } else {
                        statsOnDenial();
                    }
                }
            } else {
                if (btnPressed(Btn::A)) {
                    view = (View)((view + 1) % V_COUNT);
                }
                bool species_changed = false;
                uint8_t n = buddySpeciesCount();
                if (n > 0 && btnPressed(Btn::Up)) {
                    buddySetSpeciesIdx((buddySpeciesIdx() + n - 1) % n);
                    species_changed = true;
                }
                if (n > 0 && btnPressed(Btn::Down)) {
                    buddySetSpeciesIdx((buddySpeciesIdx() + 1) % n);
                    species_changed = true;
                }
                if (species_changed) {
                    settingsSetSpeciesIdx(buddySpeciesIdx());
                    // Debounce flash writes — only persist 1s after the last
                    // press so paging through species in quick succession
                    // doesn't churn the settings sector.
                    species_save_at_ms = now + 1000;
                    // Clear any persona override so the new buddy doesn't
                    // start mid-celebrate / mid-heart from the previous one.
                    heart_until_ms = 0;
                    celebrate_until_ms = 0;
                }
            }
        }
        if (species_save_at_ms != 0 && (int32_t)(species_save_at_ms - now) <= 0) {
            settingsSave();
            species_save_at_ms = 0;
        }

        g.set_pen(BG);
        g.clear();
        g.set_font("bitmap8");

        // ────────────── Top bar: title (left) + compact status (right) ──────────────
        // Title personalises to "<owner>'s Buddy" once the desktop has sent
        // {"cmd":"owner","name":"..."}; otherwise falls back to "Claude Buddy".
        g.set_pen(ACCENT);
        char title_buf[40];
        const char* owner = settings().owner;
        if (owner[0]) snprintf(title_buf, sizeof(title_buf), "%s's Buddy", owner);
        else          snprintf(title_buf, sizeof(title_buf), "Claude Buddy");
        g.text(title_buf, Point(6, 4), 200, 2);

        // Right-aligned status: link state · power · clock. Approximate
        // bitmap8 char width = 6px at scale 1; we right-align manually.
        const char* hci = bleHciState();
        bool linked  = bleConnected();
        bool working = hci[0] == 'w';
        const char* link_label = linked ? "linked" : working ? "scanning" : "off";
        int link_pen = linked ? OK : working ? WARN : HOT;

        bool on_usb = batteryOnUsb();
        int  pct = batteryPercent();
        char pwr_buf[8];
        const char* pwr_str;
        if (on_usb) pwr_str = "USB";
        else { snprintf(pwr_buf, sizeof(pwr_buf), "%d%%", pct); pwr_str = pwr_buf; }
        int pwr_pen = on_usb ? OK : pct < 15 ? HOT : pct < 30 ? WARN : DIM;

        RtcNow t;
        bool has_time = rtcRead(&t);
        char clock_buf[8] = "";
        if (has_time) snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d", t.hour, t.min);

        // Lay out right-to-left so absolute widths don't have to match exactly.
        int rx = W - 6;
        if (has_time) {
            int w = (int)strlen(clock_buf) * 6;
            rx -= w;
            g.set_pen(DIM);
            g.text(clock_buf, Point(rx, 8), 999, 1);
            rx -= 8;
        }
        {
            int w = (int)strlen(pwr_str) * 6;
            rx -= w;
            g.set_pen(pwr_pen);
            g.text(pwr_str, Point(rx, 8), 999, 1);
            rx -= 8;
        }
        {
            int w = (int)strlen(link_label) * 6;
            rx -= w;
            g.set_pen(link_pen);
            g.text(link_label, Point(rx, 8), 999, 1);
        }

        // Thin divider under the top bar.
        g.set_pen(DIM);
        g.rectangle(Rect(0, 22, W, 1));

        // ────────────── Buddy on the left half ──────────────
        // buddyTick paints into its 180×200 stage at (0, 40). The screen-wide
        // clear above keeps the bg consistent; the buddy redraws every frame.
        buddyTick(derive_persona(tama, now));

        // Optional vertical divider between buddy and info column.
        g.set_pen(DIM);
        g.rectangle(Rect(184, 28, 1, H - 32));

        // ────────────── Info column on the right ──────────────
        constexpr int IX = 192;
        constexpr int IW = W - IX - 4;
        int iy = 30;

        if (tama.promptId[0]) {
            // Approval prompt always wins — even if user was browsing PET/INFO.
            g.set_pen(responded ? OK : HOT);
            char head[64];
            snprintf(head, sizeof(head), "%s%s",
                     responded ? "sent: " : "approve?",
                     responded ? response_label : "");
            g.text(head, Point(IX, iy), IW, 2);
            iy += 22;

            if (!responded) {
                g.set_pen(TEXT);
                g.text(tama.promptTool, Point(IX, iy), IW, 3);
                iy += 30;
                if (tama.promptHint[0]) {
                    g.set_pen(DIM);
                    g.text(tama.promptHint, Point(IX, iy), IW, 1);
                }
                g.set_pen(OK);
                g.text("A approve", Point(IX, H - 38), IW, 2);
                g.set_pen(HOT);
                g.text("B deny",    Point(IX, H - 18), IW, 2);
            }
        } else if (view == V_PET) {
            // Pet stats — mood, fed, energy, level, lifetime counts.
            uint8_t mood = statsMoodTier();
            uint8_t fed  = statsFedProgress();
            uint8_t ene  = statsEnergyTier();
            const Stats& st = stats();

            auto pips = [&](int y, int filled, int total, int pip_w, int gap, int on_pen) {
                for (int i = 0; i < total; i++) {
                    int px = IX + i * (pip_w + gap);
                    if (i < filled) {
                        g.set_pen(on_pen);
                        g.rectangle(Rect(px, y, pip_w, 8));
                    } else {
                        g.set_pen(DIM);
                        g.rectangle(Rect(px, y, pip_w, 8));
                        g.set_pen(BG);
                        g.rectangle(Rect(px + 1, y + 1, pip_w - 2, 6));
                    }
                }
            };

            g.set_pen(DIM); g.text("mood",   Point(IX, iy), IW, 1);
            int mood_pen = mood >= 3 ? OK : mood >= 2 ? WARN : HOT;
            pips(iy + 12, mood, 4, 12, 4, mood_pen);
            iy += 28;

            g.set_pen(DIM); g.text("fed",    Point(IX, iy), IW, 1);
            pips(iy + 12, fed, 10, 8, 2, ACCENT);
            iy += 28;

            g.set_pen(DIM); g.text("energy", Point(IX, iy), IW, 1);
            int en_pen = ene >= 4 ? OK : ene >= 2 ? WARN : HOT;
            pips(iy + 12, ene, 5, 14, 4, en_pen);
            iy += 28;

            // Level badge + lifetime counters
            g.set_pen(ACCENT);
            char lvl_buf[12]; snprintf(lvl_buf, sizeof(lvl_buf), "Lv %u", st.level);
            g.text(lvl_buf, Point(IX, iy), IW, 2);
            iy += 24;

            g.set_pen(DIM);
            char cnt[40];
            snprintf(cnt, sizeof(cnt), "approved  %lu", (unsigned long)st.approvals);
            g.text(cnt, Point(IX, iy), IW, 1); iy += 11;
            snprintf(cnt, sizeof(cnt), "denied    %lu", (unsigned long)st.denials);
            g.text(cnt, Point(IX, iy), IW, 1); iy += 11;
            if (st.tokens >= 1000)
                snprintf(cnt, sizeof(cnt), "tokens    %luK", (unsigned long)(st.tokens / 1000));
            else
                snprintf(cnt, sizeof(cnt), "tokens    %lu",  (unsigned long)st.tokens);
            g.text(cnt, Point(IX, iy), IW, 1); iy += 11;
        } else if (view == V_INFO) {
            // Info — owner/device/uptime/MAC/firmware. Single combined page
            // (upstream paginates this; landscape gives us room for one card).
            g.set_pen(ACCENT); g.text("INFO", Point(IX, iy), IW, 2);
            iy += 22;

            char ln[40];
            g.set_pen(TEXT);
            snprintf(ln, sizeof(ln), "owner   %s", owner[0] ? owner : "<Unknown>");
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            int mv = batteryMillivolts();
            snprintf(ln, sizeof(ln), "battery %d%%  %d.%02dV",
                     pct, mv / 1000, (mv % 1000) / 10);
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            snprintf(ln, sizeof(ln), "power   %s", on_usb ? "USB" : "battery");
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            uint32_t up = (uint32_t)(now / 1000);
            snprintf(ln, sizeof(ln), "uptime  %luh %02lum", up / 3600, (up / 60) % 60);
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            snprintf(ln, sizeof(ln), "ble     %s", linked ? "linked" : working ? "scan" : "off");
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            snprintf(ln, sizeof(ln), "species %s", buddySpeciesName());
            g.text(ln, Point(IX, iy), IW, 1); iy += 12;

            iy += 6;
            g.set_pen(DIM);
            g.text("A: next view",      Point(IX, iy), IW, 1); iy += 11;
            g.text("Up/Dn: change buddy", Point(IX, iy), IW, 1); iy += 11;
            g.text("hold A: menu",      Point(IX, iy), IW, 1); iy += 11;
        }

        // Settings menu draws last so it overlays everything underneath.
        menuDraw(g, W, H);

        lcd.update();

        // Case LEDs only light up on an unanswered prompt (and only when the
        // user hasn't turned the attention LED off in the settings menu).
        // No idle animation — keeps the device quiet when nothing's pending.
        bool attention = tama.promptId[0] && !responded && settings().led_on;
        if (attention) {
            // Triangle wave 0..0xFFFF..0 over 30 frames (~2Hz at 60fps).
            int phase = tick % 30;
            int amp = phase < 15 ? phase : 30 - phase;
            uint16_t level = (uint16_t)((amp * 0xFFFF) / 15);
            for (uint i = 0; i < 4; i++) pwm_set_gpio_level(tufty::CASE_LEDS[i], level);
        } else {
            for (uint i = 0; i < 4; i++) pwm_set_gpio_level(tufty::CASE_LEDS[i], 0);
        }

        tick++;
        sleep_ms(16);
    }
}
