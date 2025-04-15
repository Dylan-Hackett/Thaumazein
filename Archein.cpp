#include "Archein.h"

// Define shared volatile variables
volatile uint16_t current_touch_state = 0;
volatile float touch_cv_value = 0.0f;

void UpdateDisplay() {
    if (update_display) {
        float block_duration_us = (float)BLOCK_SIZE / (float)sample_rate * 1000000.0f; 
        int cpu_percent_int = 0;
        if (block_duration_us > 1.0f) { 
             cpu_percent_int = static_cast<int>(( (float)avg_elapsed_us / block_duration_us ) * 100.0f);
        }
        cpu_percent_int = cpu_percent_int < 0 ? 0 : (cpu_percent_int > 100 ? 100 : cpu_percent_int);

        char buffer[32]; 
        sprintf(buffer, "CPU: %d", cpu_percent_int); 
        hw.PrintLine(buffer);
        sprintf(buffer, "Avg Time: %lu us", (unsigned long)avg_elapsed_us); 
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
    // Read current touch state
    uint16_t touched = touch_sensor.Touched();
    current_touch_state = touched; // Update shared variable

    if (touched == 0) {
        // No pads touched, apply slight decay to control value
        touch_cv_value *= 0.95f; 
        return; // Exit early
    }
    
    // Find the rightmost (highest) touched pad
    int highest_pad = -1;
    for (int i = 11; i >= 0; i--) {
        if (touched & (1 << i)) {
            highest_pad = i;
            break;
        }
    }
    
    if (highest_pad == -1) return; // Should not happen if touched != 0, but safety check
    
    // Get capacitance deviation for the touched pad
    int16_t deviation = touch_sensor.GetBaselineDeviation(highest_pad);
    
    // Normalize to 0.0-1.0 range with adjustable sensitivity
    float sensitivity = 50.0f; // Adjust this value based on your MPR121 calibration
    float normalized_value = daisysp::fmax(0.0f, daisysp::fmin(1.0f, deviation / sensitivity));
    
    // Apply curve for better control response (squared curve feels more natural)
    normalized_value = normalized_value * normalized_value;
    
    // Map pad position (0-11) to full range (0.0-1.0)
    float position_value = highest_pad / 11.0f;
    
    // Combine both position and pressure for more expressive control
    float position_weight = 0.7f;
    float combined_value = position_value * position_weight + normalized_value * (1.0f - position_weight);
    
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