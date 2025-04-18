#include "Amathia.h"

// Define shared volatile variables
volatile uint16_t current_touch_state = 0;
volatile float touch_cv_value = 0.0f;

void UpdateDisplay() {
    if (update_display) {
        // Get CPU Load from the CpuLoadMeter
        float avg_cpu_load = cpu_meter.GetAvgCpuLoad();
        int cpu_percent_int = static_cast<int>(avg_cpu_load * 100.0f);
        cpu_percent_int = cpu_percent_int < 0 ? 0 : (cpu_percent_int > 100 ? 100 : cpu_percent_int); // Clamp 0-100

        char buffer[32]; 
        sprintf(buffer, "CPU: %d", cpu_percent_int); 
        hw.PrintLine(buffer);
        
        // Determine current effective engine index again for display
        int current_engine_idx = static_cast<int>(timbre_knob.Value() * (MAX_ENGINE_INDEX + 1));
        if (current_engine_idx > MAX_ENGINE_INDEX) current_engine_idx = MAX_ENGINE_INDEX;
        sprintf(buffer, "Engine: %d (%s)", current_engine_idx, (current_engine_idx <=3) ? "Poly-4" : "Mono");
        hw.PrintLine(buffer);
        
        // Print Smoothed Output Level
        int level_percent = static_cast<int>(smoothed_output_level * 100.0f);
        level_percent = level_percent < 0 ? 0 : (level_percent > 100 ? 100 : level_percent); // Clamp 0-100
        sprintf(buffer, "Level: %d%%", level_percent); 
        hw.PrintLine(buffer);
        
        hw.PrintLine("--------"); 
        
        update_display = false; 
    }
}

// Poll the touch sensor and update shared variables
void PollTouchSensor() {
    // Recover from any I2C errors on the touch sensor
    if(touch_sensor.HasError()) {
        touch_sensor.ClearError();
        Mpr121::Config cfg;
        cfg.Defaults();
        touch_sensor.Init(cfg);
        touch_sensor.SetThresholds(6, 3);
    }
    uint16_t touched = touch_sensor.Touched();
    current_touch_state = touched;

    if (touched == 0) {
        // Optionally decay the control value smoothly to 0 when no pads are touched
        touch_cv_value = touch_cv_value * 0.95f; 
        return;
    }

    float total_deviation = 0.0f;
    int touched_count = 0;

    // Iterate through all pads
    for (int i = 0; i < 12; i++) {
        if (touched & (1 << i)) {
            // Get capacitance deviation for this touched pad
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

        HandleButtonInput();
        
        UpdateLED();
        
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