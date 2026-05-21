#include "data.h"

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "ble_bridge.h"
#include "lib/ArduinoJson.h"

namespace {

// Heartbeat lines can run a bit over 1KB with full transcript entries;
// 2KB gives us comfortable headroom.
constexpr size_t LINE_CAP = 2048;
char     line_buf[LINE_CAP];
uint16_t line_len = 0;
uint32_t hb_count = 0;
uint32_t bytes_received = 0;

uint32_t now_ms() {
    return to_ms_since_boot(get_absolute_time());
}

template <typename T, size_t N>
void copy_str(char (&dst)[N], T src) {
    if (!src) { dst[0] = 0; return; }
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = 0;
}

// Apply a single JSON line to TamaState. Returns true if the line was a
// valid object we recognized (caller uses this to refresh lastUpdated).
bool apply_json(const char* line, TamaState* out) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;

    // Heartbeat snapshot: fields all optional, only update what's present.
    // `|` is ArduinoJson's "value or default" — keeps current value if missing.
    out->sessionsTotal     = doc["total"]     | out->sessionsTotal;
    out->sessionsRunning   = doc["running"]   | out->sessionsRunning;
    out->sessionsWaiting   = doc["waiting"]   | out->sessionsWaiting;
    out->recentlyCompleted = doc["completed"] | false;
    out->tokens            = doc["tokens"]        | out->tokens;
    out->tokensToday       = doc["tokens_today"]  | out->tokensToday;

    if (const char* m = doc["msg"]) copy_str(out->msg, m);

    // Transcript entries: replace wholesale on each heartbeat, newest-first.
    if (JsonArrayConst la = doc["entries"].as<JsonArrayConst>()) {
        uint8_t n = 0;
        for (JsonVariantConst v : la) {
            if (n >= TamaState::MAX_LINES) break;
            copy_str(out->lines[n], v.as<const char*>());
            n++;
        }
        out->nLines = n;
    }

    // Pending permission prompt: present means we need to show approval UI.
    // Absence on a snapshot means any previous prompt was resolved upstream.
    if (JsonObjectConst pr = doc["prompt"].as<JsonObjectConst>()) {
        copy_str(out->promptId,   pr["id"].as<const char*>());
        copy_str(out->promptTool, pr["tool"].as<const char*>());
        copy_str(out->promptHint, pr["hint"].as<const char*>());
    } else if (doc["total"].is<unsigned>()) {
        // Heartbeat without prompt → clear stale prompt. Only do this on
        // heartbeats (which always carry `total`), not on every JSON line.
        out->promptId[0] = 0;
        out->promptTool[0] = 0;
        out->promptHint[0] = 0;
    }

    return true;
}

} // namespace

void dataInit(TamaState* out) {
    std::memset(out, 0, sizeof(*out));
}

void dataPoll(TamaState* out) {
    while (bleAvailable()) {
        int c = bleRead();
        if (c < 0) break;
        bytes_received++;
        if (c == '\n' || c == '\r') {
            if (line_len > 0) {
                line_buf[line_len] = 0;
                if (line_buf[0] == '{' && apply_json(line_buf, out)) {
                    out->lastUpdated = now_ms();
                    hb_count++;
                }
                line_len = 0;
            }
        } else if (line_len < LINE_CAP - 1) {
            line_buf[line_len++] = (char)c;
        } else {
            // Line overflowed the buffer — reset and drop. Should never
            // happen with well-formed heartbeats; the cap is generous.
            line_len = 0;
        }
    }

    // "Connected" = at least one heartbeat in the last 30 seconds, matching
    // the upstream comment in REFERENCE.md ("treat as dead after ~30s").
    out->connected = (out->lastUpdated != 0)
                  && (now_ms() - out->lastUpdated) <= 30000;
}

uint32_t dataHeartbeatCount() { return hb_count; }
uint32_t dataBytesReceived()  { return bytes_received; }
