#pragma once
#include <cstdint>

// Persistent settings stored in the last flash sector. Loaded into RAM at
// boot; modified in RAM via setters; settingsSave() flushes back to flash.
//
// Adding fields: bump VERSION in settings.cpp and add a migration path
// (or just accept reset to defaults on version mismatch — that's fine for
// the small set of values we hold).

struct Settings {
    char    owner[32];     // user's first name (sent by desktop once per connect)
    uint8_t species_idx;   // ASCII buddy index (0..N-1); 0 default
    // future: brightness, sound, etc.
};

void settingsInit();
const Settings& settings();

void settingsSetOwner(const char* name);
void settingsSetSpeciesIdx(uint8_t idx);

// Flush in-RAM settings to flash. Cheap-ish but blocks the core for a few
// ms (sector erase). Call from main-loop context, not from an IRQ.
void settingsSave();
