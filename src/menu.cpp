#include "menu.h"

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "libraries/pico_graphics/pico_graphics.hpp"

#include "buddy.h"
#include "buttons.h"
#include "settings.h"

namespace {

enum Item : uint8_t {
    ITEM_BRIGHTNESS,
    ITEM_SPECIES,
    ITEM_LED,
    ITEM_CLOSE,
    ITEM_COUNT
};

constexpr uint32_t HOLD_OPEN_MS = 600;

bool     menu_open = false;
bool     ignore_a  = false;     // gate so the hold-to-open release isn't a click
uint32_t a_press_start = 0;
uint8_t  selection = 0;
bool     dirty = false;         // any changes to persist on close

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

void close_menu() {
    if (dirty) {
        settingsSave();
        dirty = false;
    }
    menu_open = false;
    selection = 0;
}

void apply_selected() {
    switch (selection) {
        case ITEM_BRIGHTNESS: {
            uint8_t b = (settings().brightness + 1) % 5;
            settingsSetBrightness(b);
            dirty = true;
            break;
        }
        case ITEM_SPECIES: {
            buddyNextSpecies();
            settingsSetSpeciesIdx(buddySpeciesIdx());
            dirty = true;
            break;
        }
        case ITEM_LED: {
            settingsSetLedOn(!settings().led_on);
            dirty = true;
            break;
        }
        case ITEM_CLOSE:
            close_menu();
            break;
    }
}

int rgb_pen(pimoroni::PicoGraphics& g, uint8_t r, uint8_t gr, uint8_t b) {
    // R/B swap to compensate for the vendored ST7789 driver's framebuffer
    // layout (same trick main.cpp uses for its rgb_pen helper).
    auto* gp = static_cast<pimoroni::PicoGraphics_PenRGB888*>(&g);
    return gp->create_pen(b, gr, r);
}

} // namespace

void menuInit() {
    menu_open = false;
    ignore_a = false;
    a_press_start = 0;
    selection = 0;
    dirty = false;
}

bool menuIsOpen() { return menu_open; }

void menuUpdate() {
    uint32_t now = now_ms();

    if (!menu_open) {
        // Watch for A long-press to open the menu.
        if (btnPressed(Btn::A))  a_press_start = now;
        if (btnHeld(Btn::A) && a_press_start != 0
                            && (now - a_press_start) >= HOLD_OPEN_MS) {
            menu_open = true;
            ignore_a  = true;     // swallow the eventual release of this same press
            a_press_start = 0;
        }
        if (btnReleased(Btn::A)) a_press_start = 0;
        return;
    }

    // While menu is open, ignore the trailing release of the hold that opened
    // us. Once A is fully released we re-enable normal button handling.
    if (ignore_a) {
        if (!btnHeld(Btn::A)) ignore_a = false;
        return;
    }

    if (btnPressed(Btn::Up))   selection = (selection + ITEM_COUNT - 1) % ITEM_COUNT;
    if (btnPressed(Btn::Down)) selection = (selection + 1) % ITEM_COUNT;
    if (btnPressed(Btn::A))    apply_selected();
    if (btnPressed(Btn::B))    close_menu();
}

void menuDraw(pimoroni::PicoGraphics& g, int W, int H) {
    if (!menu_open) return;

    const int PW = 288, PH = 170;
    const int px = (W - PW) / 2, py = (H - PH) / 2;

    const int BG    = rgb_pen(g, 0x14, 0x14, 0x1C);
    const int BORDER= rgb_pen(g, 0xFA, 0x70, 0x20);
    const int TITLE = rgb_pen(g, 0xFA, 0x70, 0x20);
    const int LABEL = rgb_pen(g, 0xC0, 0xC0, 0xD0);
    const int VALUE = rgb_pen(g, 0xFF, 0xFF, 0xFF);
    const int SEL   = rgb_pen(g, 0x40, 0xE0, 0x60);
    const int DIM   = rgb_pen(g, 0x60, 0x60, 0x70);

    // Panel
    g.set_pen(BG);
    g.rectangle(pimoroni::Rect(px, py, PW, PH));
    g.set_pen(BORDER);
    g.rectangle(pimoroni::Rect(px, py, PW, 2));
    g.rectangle(pimoroni::Rect(px, py + PH - 2, PW, 2));
    g.rectangle(pimoroni::Rect(px, py, 2, PH));
    g.rectangle(pimoroni::Rect(px + PW - 2, py, 2, PH));

    g.set_font("bitmap8");
    g.set_pen(TITLE);
    g.text("MENU", pimoroni::Point(px + 12, py + 8), PW, 2);

    // Items
    const char* labels[ITEM_COUNT] = { "Brightness", "Species", "LED on prompt", "Close" };
    int item_y0 = py + 38;
    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = item_y0 + i * 22;
        bool sel = (i == selection);

        g.set_pen(sel ? SEL : LABEL);
        g.text(sel ? ">" : " ", pimoroni::Point(px + 12, y), 16, 2);
        g.text(labels[i], pimoroni::Point(px + 28, y), 200, 2);

        // Value column on the right
        char value[24] = "";
        switch (i) {
            case ITEM_BRIGHTNESS:
                // Render as a small bar: N filled blocks of 5
                snprintf(value, sizeof(value), "%d/4", settings().brightness);
                break;
            case ITEM_SPECIES:
                snprintf(value, sizeof(value), "%s", buddySpeciesName());
                break;
            case ITEM_LED:
                snprintf(value, sizeof(value), "%s", settings().led_on ? "on" : "off");
                break;
            default: break;
        }
        if (value[0]) {
            g.set_pen(VALUE);
            g.text(value, pimoroni::Point(px + 170, y), PW - 170, 2);
        }
    }

    // Footer hint
    g.set_pen(DIM);
    g.text("A: change   B: close   Up/Down: nav",
           pimoroni::Point(px + 12, py + PH - 18), PW - 24, 1);
}
