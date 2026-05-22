#include "settings.h"

#include <cstring>

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

namespace {

// 'CBPS' = Claude Buddy Persistent Settings — anything else means uninitialized.
constexpr uint32_t MAGIC   = 0x53504243;
constexpr uint32_t VERSION = 1;

struct __attribute__((packed)) Persisted {
    uint32_t magic;
    uint32_t version;
    Settings s;
};

// Last sector of flash. The program itself lives at the start; we're 16MB away.
constexpr uint32_t SAVE_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

Settings g_settings{};

void do_save_unsafe(void* /*param*/) {
    Persisted p{};
    p.magic   = MAGIC;
    p.version = VERSION;
    p.s       = g_settings;

    flash_range_erase(SAVE_OFFSET, FLASH_SECTOR_SIZE);
    // flash_range_program requires a multiple of FLASH_PAGE_SIZE (256).
    // Pad a small staging buffer up to one page so we satisfy that.
    static_assert(sizeof(Persisted) <= FLASH_PAGE_SIZE,
                  "Settings struct outgrew one flash page; need a paged write loop");
    uint8_t page[FLASH_PAGE_SIZE]{};
    std::memcpy(page, &p, sizeof(p));
    flash_range_program(SAVE_OFFSET, page, FLASH_PAGE_SIZE);
}

} // namespace

void settingsInit() {
    const Persisted* p = (const Persisted*)(XIP_BASE + SAVE_OFFSET);
    if (p->magic == MAGIC && p->version == VERSION) {
        g_settings = p->s;
    } else {
        std::memset(&g_settings, 0, sizeof(g_settings));
    }
}

const Settings& settings() { return g_settings; }

void settingsSetOwner(const char* name) {
    if (!name) return;
    std::strncpy(g_settings.owner, name, sizeof(g_settings.owner) - 1);
    g_settings.owner[sizeof(g_settings.owner) - 1] = 0;
}

void settingsSetSpeciesIdx(uint8_t idx) {
    g_settings.species_idx = idx;
}

void settingsSave() {
    // flash_safe_execute pauses cyw43/IRQs across the erase+program so BLE
    // doesn't race with the bus. Wait indefinitely for the lock.
    flash_safe_execute(do_save_unsafe, nullptr, UINT32_MAX);
}
