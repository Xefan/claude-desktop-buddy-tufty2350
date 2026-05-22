#pragma once
#include <cstdint>

// Pet stats — the "tamagotchi" state the device displays on its PET view.
// Persisted in its own flash sector (second-to-last), separate from settings,
// so the frequent stat writes don't churn the rare-write settings sector.
//
// Drops upstream's nap-seconds tracking — we have no IMU for face-down nap.

struct Stats {
    uint32_t approvals;
    uint32_t denials;
    uint8_t  level;
    uint32_t tokens;            // cumulative output tokens, drives level

    uint16_t velocity[8];        // ring buffer: seconds-to-respond per approval
    uint8_t  velIdx;
    uint8_t  velCount;
};

constexpr uint32_t TOKENS_PER_LEVEL = 50000;

void statsInit();
const Stats& stats();

// Persist to flash (~10ms, pauses BLE briefly via flash_safe_execute).
void statsSave();

// Event hooks — bump in-memory counters and persist immediately. Use these
// from main loop / data parser; safe to call frequently.
void statsOnApproval(uint32_t seconds_to_respond);
void statsOnDenial();
void statsOnBridgeTokens(uint32_t bridge_total);

// One-shot: true on the call following a token milestone that bumped level.
// Use to trigger a CELEBRATE buddy state.
bool statsPollLevelUp();

// Derived metrics for the PET view:
uint8_t statsMoodTier();        // 0..4 (worst..best)
uint8_t statsFedProgress();     // 0..10 (fraction of current level filled)
uint8_t statsEnergyTier();      // 0..5 (drains 1 per 2h since boot; no IMU nap)

// Median response seconds — exposed so the PET help text can quote it.
uint16_t statsMedianVelocity();
