#pragma once

// Power management — long-press of the RESET button triggers a clean shutdown
// and dormant sleep. The chip wakes when any front button is pressed and
// reboots from scratch (so BLE, LCD, etc. reinit cleanly).
//
// This matches Pimoroni's stock badgeware behavior:
//   tap RESET        -> reboot (handled by hardware, not this module)
//   double-tap RESET -> USB disk mode (handled by hardware)
//   long-press RESET -> deep sleep (this module)

void powerInit();

// Poll the RESET button. If it's been held for ~2s, drop POWER_EN,
// turn off LCD backlight + case LEDs, and enter dormant mode. On wake
// (any front button), watchdog-reboot back to a clean startup state.
// Call once per main-loop frame.
void powerUpdate();
