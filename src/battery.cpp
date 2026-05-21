#include "battery.h"

#include <cmath>

#include "pico/stdlib.h"
#include "hardware/adc.h"

namespace {

// Pin map from pimoroni/tufty2350 board/pins.csv.
constexpr uint VBAT_PIN     = 40;   // ADC0 — battery voltage / 2 via divider
constexpr uint VREF_PIN     = 42;   // ADC2 — chip's 1.1V reference
constexpr uint VBUS_PIN     = 12;   // VBUS_DETECT — active high

// RP2350 ADC channel for GPIO N is N-40.
constexpr uint VBAT_ADC_CH  = 0;
constexpr uint VREF_ADC_CH  = 2;

// Average N samples on a given ADC channel to smooth noise.
uint32_t sample_adc(uint ch, int n) {
    adc_select_input(ch);
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += adc_read();
    return sum / n;
}

} // namespace

void batteryInit() {
    adc_init();
    adc_gpio_init(VBAT_PIN);
    adc_gpio_init(VREF_PIN);

    gpio_init(VBUS_PIN);
    gpio_set_dir(VBUS_PIN, GPIO_IN);
    gpio_set_pulls(VBUS_PIN, false, false);
}

int batteryMillivolts() {
    // Ratio method (from Pimoroni's badge.py): the 3.3V step cancels out
    // when we read both VBAT and the internal 1.1V reference, so VDDIO
    // drift doesn't skew the result.
    //   battery_volts = (vbat / vref) * 2 * 1.1
    uint32_t vbat = sample_adc(VBAT_ADC_CH, 10);
    uint32_t vref = sample_adc(VREF_ADC_CH, 10);
    if (vref == 0) return 0;
    return (int)((float)vbat / (float)vref * 2.2f * 1000.0f);
}

int batteryPercent() {
    int mv = batteryMillivolts();
    if (mv <= 0) return 0;
    // Pimoroni's logistic LiPo curve. Calibrated against real cell discharge.
    float v = mv / 1000.0f;
    float k = powf(v / 3.2f, 80.0f);
    float pct = 123.0f - (123.0f / powf(1.0f + k, 0.165f));
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (int)(pct + 0.5f);
}

bool batteryOnUsb() {
    return gpio_get(VBUS_PIN) != 0;
}
