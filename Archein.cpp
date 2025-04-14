#include "Archein.h"

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


int main(void) {
    
    InitializeSynth();
    
    // Main Loop 
    while (1) {

        HandleButtonInput();
        
        UpdateLED();
        
        UpdateDisplay();
        
        // Yield for system tasks
        System::Delay(10);
    }
    
    return 0;
} 