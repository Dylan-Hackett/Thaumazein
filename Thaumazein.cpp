#include "Thaumazein.h"

// Define shared volatile variables
volatile uint16_t current_touch_state = 0;
volatile float touch_cv_value = 0.0f;

void UpdateDisplay() {
    if (update_display) {
        // Build stats message in a smaller buffer
        char msg[512];
        int pos = 0;

        // cpu load average/max
        float avg_cpu_load = cpu_meter.GetAvgCpuLoad();
        int cpu_avg = static_cast<int>(avg_cpu_load * 100.0f);
        cpu_avg = cpu_avg < 0 ? 0 : (cpu_avg > 100 ? 100 : cpu_avg);
        float max_cpu_load = cpu_meter.GetMaxCpuLoad();
        int cpu_max = static_cast<int>(max_cpu_load * 100.0f);
        cpu_max = cpu_max < 0 ? 0 : (cpu_max > 100 ? 100 : cpu_max);
        pos += snprintf(msg + pos, sizeof(msg) - pos, "cpu : %d/%d\n", cpu_avg, cpu_max);

        // Engine Info
        int current_engine_idx = current_engine_index;
        pos += snprintf(msg + pos, sizeof(msg) - pos, "Engine: %d (%s)\n", current_engine_idx, (current_engine_idx <= 3) ? "Poly-4" : "Mono");

        // Only show ADC values 8-11
        pos += snprintf(msg + pos, sizeof(msg) - pos, "ADC Values (8-11):\n");
        for (int i = 8; i < 12; ++i) {
            pos += snprintf(msg + pos, sizeof(msg) - pos, "[%d]: %d\n", i, static_cast<int>(adc_raw_values[i] * 1000));
            if(pos >= (int)sizeof(msg) - 20) break; // safety margin
        }

        // Separator
        pos += snprintf(msg + pos, sizeof(msg) - pos, "--------");

        // Print once with a single call to avoid throttling
        hw.PrintLine("%s", msg);

        update_display = false; 
    }
}

// Poll the touch sensor and update shared variables
void PollTouchSensor() {
    if(!touch_sensor_present) {
        // Touch sensor unavailable – nothing to poll
        return;
    }
    // Recover from any I2C errors on the touch sensor
    if(touch_sensor.HasError()) {
        touch_sensor.ClearError();
        thaumazein_hal::Mpr121::Config cfg;
        cfg.Defaults();
        touch_sensor.Init(cfg);
        touch_sensor.SetThresholds(6, 3);
    }
    uint16_t touched = touch_sensor.Touched();
    current_touch_state = touched;

    // Update touch pad LEDs with touch and ARP blink
    uint32_t now = hw.system.GetNow();
    bool arp_on = arp.IsActive();
    for(int i = 0; i < 12; ++i) {
        int ledIdx = 11 - i;  // pad i maps to LED[11-i]
        bool padTouched = (touched & (1 << i)) != 0;
        bool blink     = (now - arp_led_timestamps[ledIdx]) < ARP_LED_DURATION_MS;
        bool ledState  = arp_on ? blink : (padTouched || blink);
        touch_leds[ledIdx].Write(ledState);
    }

    if (touched == 0) {
       
        touch_cv_value = touch_cv_value * 0.95f; 
        return;
    }

    float total_deviation = 0.0f;
    int touched_count = 0;

    // Iterate through all pads
    for (int i = 0; i < 12; i++) {
        if (touched & (1 << i)) {
            int16_t deviation = touch_sensor.GetBaselineDeviation(i);
            total_deviation += deviation;
            touched_count++;
        }
    }

    float average_deviation = 0.0f;
    if (touched_count > 0) {
        average_deviation = total_deviation / touched_count;
    }

    // Normalize to 0.0-1.0 range with adjustable sensitivity
    // Consider recalibrating sensitivity if needed for average pressure
    float sensitivity = 150.0f;
    float normalized_value = daisysp::fmax(0.0f, daisysp::fmin(1.0f, average_deviation / sensitivity));

    // Apply curve for better control response (squared curve feels more natural)
    normalized_value = normalized_value * normalized_value;

    // Remove the position component - control is now based purely on average pressure
    // float position_value = highest_pad / 11.0f; // Removed
    // float position_weight = 0.7f;              // Removed
    // float combined_value = position_value * position_weight + normalized_value * (1.0f - position_weight); // Replaced
    float combined_value = normalized_value; // Use normalized average pressure directly

    // Apply adaptive smoothing - more smoothing for small changes, less for big changes
    float change = fabsf(combined_value - touch_cv_value);
    float smoothing = daisysp::fmax(0.5f, 0.95f - change * 2.0f); // 0.5-0.95 smoothing range

    // Update the shared volatile variable
    touch_cv_value = touch_cv_value * smoothing + combined_value * (1.0f - smoothing);
}

int main(void) {
    InitializeSynth();
    
    uint32_t lastPoll = hw.system.GetNow();  // Track last poll time
    
    // Main Loop 
    while (1) {
        UpdateLED();
        
        // Check bootloader condition via ADC touch pads
        Bootload();
        
        UpdateDisplay();
        
        // Poll touch sensor every 5 ms (200 Hz)
        if (hw.system.GetNow() - lastPoll >= 5) {
            lastPoll = hw.system.GetNow();
            PollTouchSensor();
        }
        
        // Yield for system tasks, adjusted for polling interval
        System::Delay(1); // Shorter delay to allow more frequent polling checks
    }
    
    return 0;
} 