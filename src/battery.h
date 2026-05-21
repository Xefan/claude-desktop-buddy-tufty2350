#pragma once

// Battery + USB telemetry. The Tufty 2350 has no PMIC: VBAT_SENSE is a 2:1
// divider on the LiPo into ADC0, SENSE_1V1 is the chip's 1.1V reference on
// ADC2 (lets us correct for VDDIO drift), VBUS_DETECT is a plain GPIO.

void batteryInit();

// Battery voltage in millivolts. Returns 0 if ADC read failed (e.g. before
// batteryInit). Range ~3000mV (empty) to ~4200mV (full) for a LiPo cell.
int batteryMillivolts();

// Estimated state of charge, 0..100. Uses Pimoroni's logistic LiPo curve.
int batteryPercent();

// True when USB power is detected on the USB-C connector.
bool batteryOnUsb();
