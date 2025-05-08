#include "DelayEffect.h"
#include "Thaumazein.h" // For global variables and Daisy hardware access

// Removed multi-line comment about assumed global variables

namespace DelayEffect {

void UpdateDelay() {
    delay.SetFeedback(delay_feedback_val * 0.98f); 
    float delay_time_s = 0.01f + delay_time_val * 0.99f; 
    delay.SetDelayTime(delay_time_s);
}

void ApplyDelayToOutput(const float* mixed_signal_input, // This will be mix_buffer_out
                        daisy::AudioHandle::InterleavingOutputBuffer out,
                        size_t size,
                        float dry_level) {
    float wet_level = 1.0f - dry_level; 
    
    float dry_level_clamped = fmaxf(0.0f, fminf(dry_level, 1.0f));

    float norm_factor = static_cast<float>(NUM_VOICES) * MASTER_VOLUME;
    if (norm_factor == 0.0f) { // Prevent division by zero
        // Handle the case where norm_factor is zero
        // This is a placeholder and should be replaced with an appropriate handling logic
        norm_factor = 1.0f; // Using a default value to avoid division by zero
    }
    
    for (size_t i = 0; i < size; i += 2) {
        // mixed_signal_input is effectively mix_buffer_out[i/2]
        float raw_sample_from_buffer = mixed_signal_input[i/2];
        
        float sample_to_delay = raw_sample_from_buffer / 32768.f / norm_factor;

        float wet_sample = delay.Process(sample_to_delay); // delay object from Thaumazein.h
        
        // MASTER_VOLUME is assumed to be from Thaumazein.h
        float output_val = ((sample_to_delay * dry_level_clamped) + (wet_sample * wet_level)) * MASTER_VOLUME;
        
        out[i]     = output_val;
        out[i+1]   = output_val;
    }
}

} // namespace DelayEffect 