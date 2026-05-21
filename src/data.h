#pragma once
#include <cstdint>

// Parsed view of the Claude desktop's heartbeat snapshot.
// Subset of upstream's TamaState — fields we don't surface yet (RTC sync,
// folder push, stats persistence) are stripped until their tasks land.
struct TamaState {
    uint8_t  sessionsTotal;
    uint8_t  sessionsRunning;
    uint8_t  sessionsWaiting;
    bool     recentlyCompleted;
    uint32_t tokens;             // cumulative output tokens
    uint32_t tokensToday;
    uint32_t lastUpdated;        // ms-since-boot of last successful parse

    char     msg[64];            // one-line summary
    char     promptId[40];       // empty when no pending permission
    char     promptTool[24];
    char     promptHint[64];

    // Recent transcript lines (newest first)
    static constexpr uint8_t MAX_LINES = 6;
    static constexpr uint8_t LINE_W    = 80;
    char     lines[MAX_LINES][LINE_W];
    uint8_t  nLines;

    bool     connected;          // updated by dataPoll based on heartbeat freshness
};

void dataInit(TamaState* out);

// Drain available BLE bytes, accumulate into a line, parse heartbeat JSON
// and merge into `out`. Also updates `out->connected` based on whether
// we've heard from the desktop in the last 30 seconds.
void dataPoll(TamaState* out);

// Diagnostic counters — for telling "heartbeats not arriving" from
// "heartbeats arriving but tracker reports 0/0/0".
uint32_t dataHeartbeatCount();      // successful JSON parses since boot
uint32_t dataBytesReceived();       // total raw bytes from BLE since boot
