#include "stats.h"

#include <cstring>
#include <cstdint>

#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"

namespace {

constexpr uint32_t MAGIC   = 0x53544243;   // 'CBTS' little-endian
constexpr uint32_t VERSION = 1;

// Stats sector lives one sector before the settings sector. The two never
// alias, and stats writes don't wear the settings sector.
constexpr uint32_t SAVE_OFFSET = PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE;

struct __attribute__((packed)) Persisted {
    uint32_t magic;
    uint32_t version;
    Stats    s;
};

Stats g_stats{};

// Bridge token sync — see upstream's stats.h for the rationale:
// bridge total resets on its own restart; we add deltas to our cumulative
// tokens. _lastBridgeTokens starts unsynced so a device reboot doesn't
// re-credit the bridge's session.
uint32_t lastBridgeTokens = 0;
bool     tokensSynced = false;
bool     levelUpPending = false;

uint32_t energy_base_ms = 0;     // ms-since-boot anchor for energy decay

void do_save_unsafe(void* /*param*/) {
    Persisted p{};
    p.magic   = MAGIC;
    p.version = VERSION;
    p.s       = g_stats;

    flash_range_erase(SAVE_OFFSET, FLASH_SECTOR_SIZE);
    static_assert(sizeof(Persisted) <= FLASH_PAGE_SIZE,
                  "Stats outgrew one flash page");
    uint8_t page[FLASH_PAGE_SIZE]{};
    std::memcpy(page, &p, sizeof(p));
    flash_range_program(SAVE_OFFSET, page, FLASH_PAGE_SIZE);
}

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

} // namespace

void statsInit() {
    const Persisted* p = (const Persisted*)(XIP_BASE + SAVE_OFFSET);
    if (p->magic == MAGIC && p->version == VERSION) {
        g_stats = p->s;
    } else {
        std::memset(&g_stats, 0, sizeof(g_stats));
    }
    energy_base_ms = now_ms();
}

const Stats& stats() { return g_stats; }

void statsSave() {
    flash_safe_execute(do_save_unsafe, nullptr, UINT32_MAX);
}

void statsReset() {
    std::memset(&g_stats, 0, sizeof(g_stats));
    // Drop the bridge sync state so the next inbound tokens report becomes a
    // fresh baseline (otherwise lastBridgeTokens would still hold the old
    // value and we'd immediately credit the difference, undoing the reset).
    lastBridgeTokens = 0;
    tokensSynced     = false;
    levelUpPending   = false;
    energy_base_ms   = now_ms();
    statsSave();
}

void statsOnApproval(uint32_t seconds_to_respond) {
    g_stats.approvals++;
    if (seconds_to_respond > 65535) seconds_to_respond = 65535;
    g_stats.velocity[g_stats.velIdx] = (uint16_t)seconds_to_respond;
    g_stats.velIdx = (g_stats.velIdx + 1) % 8;
    if (g_stats.velCount < 8) g_stats.velCount++;
    statsSave();
}

void statsOnDenial() {
    g_stats.denials++;
    statsSave();
}

void statsOnBridgeTokens(uint32_t bridge_total) {
    if (!tokensSynced) {
        lastBridgeTokens = bridge_total;
        tokensSynced = true;
        return;
    }
    if (bridge_total < lastBridgeTokens) {
        // bridge restart — resync without crediting
        lastBridgeTokens = bridge_total;
        return;
    }
    uint32_t delta = bridge_total - lastBridgeTokens;
    lastBridgeTokens = bridge_total;
    if (delta == 0) return;

    uint8_t lvlBefore = (uint8_t)(g_stats.tokens / TOKENS_PER_LEVEL);
    g_stats.tokens += delta;
    uint8_t lvlAfter  = (uint8_t)(g_stats.tokens / TOKENS_PER_LEVEL);
    if (lvlAfter > lvlBefore) {
        g_stats.level = lvlAfter;
        levelUpPending = true;
        statsSave();   // persist only at the level milestone, not every delta
    }
}

bool statsPollLevelUp() {
    bool r = levelUpPending;
    levelUpPending = false;
    return r;
}

uint16_t statsMedianVelocity() {
    if (g_stats.velCount == 0) return 0;
    uint16_t tmp[8];
    std::memcpy(tmp, g_stats.velocity, sizeof(tmp));
    uint8_t n = g_stats.velCount;
    for (uint8_t i = 1; i < n; i++) {
        uint16_t k = tmp[i]; int8_t j = i - 1;
        while (j >= 0 && tmp[j] > k) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = k;
    }
    return tmp[n/2];
}

uint8_t statsMoodTier() {
    uint16_t vel = statsMedianVelocity();
    int8_t tier;
    if (vel == 0)        tier = 2;        // no data → neutral
    else if (vel < 15)   tier = 4;
    else if (vel < 30)   tier = 3;
    else if (vel < 60)   tier = 2;
    else if (vel < 120)  tier = 1;
    else                 tier = 0;
    uint32_t a = g_stats.approvals, d = g_stats.denials;
    if (a + d >= 3) {
        if      (d > a)       tier -= 2;
        else if (d * 2 > a)   tier -= 1;
    }
    if (tier < 0) tier = 0;
    if (tier > 4) tier = 4;
    return (uint8_t)tier;
}

uint8_t statsFedProgress() {
    return (uint8_t)((g_stats.tokens % TOKENS_PER_LEVEL) / (TOKENS_PER_LEVEL / 10));
}

uint8_t statsEnergyTier() {
    // No IMU = no face-down nap, so energy just drains over uptime.
    uint32_t hours = (now_ms() - energy_base_ms) / 3600000;
    int8_t e = 5 - (int8_t)(hours / 2);
    if (e < 0) e = 0;
    if (e > 5) e = 5;
    return (uint8_t)e;
}
