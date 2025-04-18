#include "Amathia.h"
#include "mpr121_daisy.h"
#include <cmath>
#include <algorithm>

const float MASTER_VOLUME = 0.7f; // Master output level scaler

void ProcessControls();
void ReadKnobValues();
int  DetermineEngineSettings();
void HandleTouchInput(int engineIndex, bool poly_mode, int effective_num_voices);
void ConfigureDelaySettings();
void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx);
void ProcessVoiceEnvelopes(bool poly_mode);
void ProcessAudioOutput(AudioHandle::InterleavingOutputBuffer out, size_t size, float dry_level);
void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out);
void ResetVoiceStates();

// Global variables for data sharing between decomposed functions
float pitch_val, harm_knob_val, timbre_knob_val, decay_knob_val, morph_knob_val;
float delay_feedback_val, delay_time_val, delay_lag_val, delay_mix_val;
float env_attack_val, env_release_val;
float attack_time, release_time;
float mix_buffer_out[BLOCK_SIZE];
float mix_buffer_aux[BLOCK_SIZE];

extern VoiceEnvelope voice_envelopes[NUM_VOICES];
extern float voice_values[NUM_VOICES];
extern bool voice_active[NUM_VOICES];
extern Mpr121 touch_sensor;
extern AnalogControl env_attack_knob;

// Define the CpuLoadMeter instance
CpuLoadMeter cpu_meter;

void ProcessVoices() {
    // Apply cubic response for better control at short settings
    // For punchier attack, use a stronger non-linear curve at lower attack values
    float attack_raw = env_attack_val;
    float attack_value;
    if (attack_raw < 0.2f) {
        // More exaggerated curve for very short attacks (extra punchy)
        attack_value = attack_raw * (attack_raw * 0.5f);
    } else {
        // Regular cubic response for longer attacks
        attack_value = attack_raw * attack_raw * attack_raw;
    }
    
    // Normal cubic curve for release
    float release_value = env_release_val * env_release_val * env_release_val;
    
    for(int i = 0; i < NUM_VOICES; i++) {
        // Update envelope parameters
        voice_envelopes[i].SetAttackTime(attack_value);
        voice_envelopes[i].SetReleaseTime(release_value);
        
        // Process envelope
        voice_values[i] = voice_envelopes[i].Process();
        
        // Update voice activity based on envelope state
        if(!voice_active[i] && !voice_envelopes[i].IsActive()) {
            voice_values[i] = 0.0f; // Ensure silence when inactive
        }
    }
}

void ProcessVoice(int voice_idx, float envelope_value) {
    // Store voice value for LED display
    voice_values[voice_idx] = envelope_value;
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                 AudioHandle::InterleavingOutputBuffer out,
                 size_t size) {
    cpu_meter.OnBlockStart(); // Mark the beginning of the audio block
    
    // Process controls & read values
    ProcessControls();
    ReadKnobValues();
    
    // Get touch control value from the shared volatile variable
    float touch_control = touch_cv_value; 
    
    // --- Apply Touch Control Modulation (Averaging knob + touch) ---
    // Apply touch modulation to selected parameters by averaging knob and touch values
    float intensity_factor = 0.5f; // 0.0 = knob only, 1.0 = touch only, 0.5 = average

    // harm_knob_val    = harm_knob_val    * (1.0f - intensity_factor) + touch_control * intensity_factor; // Commented out
    morph_knob_val   = morph_knob_val   * (1.0f - intensity_factor) + touch_control * intensity_factor;
    // decay_knob_val   = decay_knob_val   * (1.0f - intensity_factor) + touch_control * intensity_factor; // Commented out
    delay_feedback_val = delay_feedback_val * (1.0f - intensity_factor) + touch_control * intensity_factor;
    // delay_time_val   = delay_time_val   * (1.0f - intensity_factor) + touch_control * intensity_factor; // Commented out

    // --- End Touch Control Modulation ---

    // Add panic button check (long press but not bootloader long)
    static uint32_t button_press_time = 0;
    if (button.RisingEdge()) {
        button_press_time = System::GetNow();
    }
    else if (button.Pressed() && button_press_time > 0) {
        uint32_t held_time = System::GetNow() - button_press_time;
        // If button held more than 1 second but less than bootloader time (3s)
        if (held_time > 1000 && held_time < 3000) {
            ResetVoiceStates();
            button_press_time = 0; // Reset so it only triggers once
        }
    }
    
    // Determine engine settings
    int engineIndex = DetermineEngineSettings();
    bool poly_mode = (engineIndex <= 3); // First 4 engines are poly
    int effective_num_voices = poly_mode ? NUM_VOICES : 1;
    
    // Process touch input for note handling
    HandleTouchInput(engineIndex, poly_mode, effective_num_voices);
    
    // Configure delay settings
    ConfigureDelaySettings();
    
    // Process voice parameters and render audio
    // Apply the touch control to modify a parameter (for example, morph)
    // morph_knob_val = morph_knob_val * 0.6f + touch_control * 0.4f; // OLD METHOD - REMOVED

    // Alternate options for applying touch control to different parameters:
    // Uncomment one of these to change which parameter the touch control affects
    // harm_knob_val = harm_knob_val * 0.6f + touch_control * 0.4f;   // Touch controls harmonics
    // decay_knob_val = decay_knob_val * 0.6f + touch_control * 0.4f; // Touch controls decay
    // delay_feedback_val = delay_feedback_val * 0.6f + touch_control * 0.4f; // Touch controls delay feedback
    // delay_time_val = delay_time_val * 0.6f + touch_control * 0.4f; // Touch controls delay time

    PrepareVoiceParameters(engineIndex, poly_mode, effective_num_voices - 1);
    
    // Process envelopes and mix audio
    ProcessVoiceEnvelopes(poly_mode);
    
    // Mix dry/wet and apply delay
    float dry_level = 1.0f - delay_mix_val;
    ProcessAudioOutput(out, size, dry_level);
    
    // Mark the end of the audio block
    cpu_meter.OnBlockEnd();
    
    // Update other performance monitors (like output level)
    UpdatePerformanceMonitors(size, out);
}

void ProcessControls() {
    button.Debounce();
    pitch_knob.Process();         // ADC 0
    harmonics_knob.Process();     // ADC 1
    timbre_knob.Process();        // ADC 2
    decay_knob.Process();         // ADC 3
    morph_knob.Process();         // ADC 4
    delay_feedback_knob.Process(); // ADC 5
    delay_time_knob.Process();    // ADC 6
    delay_lag_knob.Process();     // ADC 7
    delay_mix_knob.Process();     // ADC 8
    env_attack_knob.Process();    // ADC 9
    env_release_knob.Process();   // ADC 10
}

void ReadKnobValues() {
    pitch_val = pitch_knob.Value();           // ADC 0
    harm_knob_val = harmonics_knob.Value();     // ADC 1
    timbre_knob_val = timbre_knob.Value();      // ADC 2
    decay_knob_val = decay_knob.Value();        // ADC 3
    morph_knob_val = morph_knob.Value();        // ADC 4
    delay_feedback_val = delay_feedback_knob.Value(); // ADC 5
    delay_time_val = delay_time_knob.Value();     // ADC 6
    delay_lag_val = delay_lag_knob.Value();       // ADC 7
    delay_mix_val = delay_mix_knob.Value();       // ADC 8
    env_attack_val = env_attack_knob.Value();     // ADC 9
    env_release_val = env_release_knob.Value();   // ADC 10
}

void ResetVoiceStates() {
    // Force reset all voice states to prevent stuck notes
    for (int v = 0; v < NUM_VOICES; v++) {
        // Reset the envelope to IDLE state
        voice_envelopes[v].Reset();
        
        // Clear active flag
        voice_active[v] = false;
        
        // Make sure triggers are off
        modulations[v].trigger = 0.0f;
    }
}

int DetermineEngineSettings() {
    // --- Determine Dynamic Polyphony & Engine ---
    static int previous_engine_index = -1;
    
    // Calculate engine index from knob
    int engineIndex = static_cast<int>(timbre_knob_val * (MAX_ENGINE_INDEX + 1));
    if (engineIndex > MAX_ENGINE_INDEX) engineIndex = MAX_ENGINE_INDEX;
    
    // Only check when engine index changes
    if (engineIndex != previous_engine_index) {
        // Reset stuck voices only when changing engines
        for (int v = 0; v < NUM_VOICES; v++) {
            // If changing engine types (poly to mono or mono to poly)
            if ((previous_engine_index <= 3 && engineIndex > 3) || 
                (previous_engine_index > 3 && engineIndex <= 3)) {
                // Only reset active flags, don't touch envelopes
                voice_active[v] = false;
            }
        }
        
        previous_engine_index = engineIndex;
    }
    
    return engineIndex;
}

void HandleTouchInput(int engineIndex, bool poly_mode, int effective_num_voices) {
    // Use the shared volatile variable for current touch state
    uint16_t local_current_touch_state = current_touch_state;

    for (int i = 0; i < 12; ++i) { 
        bool pad_currently_pressed = (local_current_touch_state >> i) & 1;
        bool pad_was_pressed = (last_touch_state >> i) & 1;
        float note_for_pad = kTouchMidiNotes[i];

        if (pad_currently_pressed && !pad_was_pressed) { 
            if (poly_mode) {
                int voice_idx = FindAvailableVoice(effective_num_voices); 
                if (voice_idx != -1) {
                    voice_note[voice_idx] = note_for_pad;
                    voice_active[voice_idx] = true;
                    modulations[voice_idx].trigger = 1.0f; // For initial transient
                    voice_envelopes[voice_idx].Trigger(); 
                }
            } else { 
                AssignMonoNote(note_for_pad);
            }
        } else if (!pad_currently_pressed && pad_was_pressed) { 
            if (poly_mode) {
                 int voice_idx = FindVoice(note_for_pad, effective_num_voices);
                 if (voice_idx != -1) {
                     voice_active[voice_idx] = false; // Mark inactive for release
                     voice_envelopes[voice_idx].Release(); // Start release phase
                 }
            } else { 
                if (voice_active[0] && fabsf(voice_note[0] - note_for_pad) < 0.1f) {
                    voice_active[0] = false; // Mark voice 0 inactive
                }
            }
        }
    }
    last_touch_state = local_current_touch_state; // Update last state with the value used in this callback
}

void ConfigureDelaySettings() {
    delay.SetFeedback(delay_feedback_val * 0.98f); // ADC 5
    float delay_time_s = 0.01f + delay_time_val * 0.99f; // ADC 6
    delay.SetDelayTime(delay_time_s);
    float lag_time_s = delay_lag_val * 0.2f; // ADC 7
    delay.SetLagTime(lag_time_s);
}

void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx) {
    
    float global_pitch_offset = pitch_val * 24.f - 12.f;     // ADC 0
    float current_global_harmonics = harm_knob_val;          // ADC 1
    float current_global_decay = decay_knob_val;            // ADC 3
    float current_global_morph = morph_knob_val;            // ADC 4

    // --- Process Effective Voices --- 
    for (int v = 0; v <= max_voice_idx; ++v) { 
        // Update Patch using dedicated knob values
        patches[v].note = voice_note[v] + global_pitch_offset;
        patches[v].engine = engineIndex;                      // ADC 2
        patches[v].harmonics = current_global_harmonics;    // ADC 1
        patches[v].timbre = 0.5f; 
        patches[v].morph = current_global_morph;            // ADC 4
        patches[v].lpg_colour = 0.0f;
        patches[v].decay = current_global_decay;            // ADC 3
        
        patches[v].frequency_modulation_amount = 0.f;
        patches[v].timbre_modulation_amount = 0.f;
        patches[v].morph_modulation_amount = 0.f;

        // Update Modulations & Handle Trigger 
        modulations[v].engine = 0; 
        modulations[v].note = 0.0f; 
        modulations[v].frequency = 0.0f;
        modulations[v].harmonics = 0.0f; 
        modulations[v].timbre = 0.0f;
        modulations[v].morph = 0.0f; 
        modulations[v].level = 1.0f; 
        
        // MODIFIED: Handle all engines consistently with trigger_patched
        // This ensures envelopes are properly applied
        modulations[v].trigger_patched = !poly_mode;  // Only patch trigger for non-poly engines
        
        if (poly_mode) {
            // For poly engines, we'll handle triggering through voice envelopes
            // Only set trigger on initial note-on
            // Don't modify value - already set in HandleTouchInput
        } else {
            // For non-poly engines, use direct trigger patching
            modulations[v].trigger = voice_active[v] ? 1.0f : 0.0f;
        }
        
        voices[v].Render(patches[v], modulations[v], output_buffers[v], BLOCK_SIZE);
    }
    
    // Silence unused voices (when switching from poly to mono)
    int effective_voices = max_voice_idx + 1; // Calculate from max_voice_idx
    for (int v = effective_voices; v < NUM_VOICES; ++v) {
         memset(output_buffers[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
         // Keep voice_active[v] = false and modulations[v].trigger = 0.0f
    }
}

void ProcessVoiceEnvelopes(bool poly_mode) {
    memset(mix_buffer_out, 0, sizeof(mix_buffer_out));
    memset(mix_buffer_aux, 0, sizeof(mix_buffer_aux));
    
    // Apply the same punchier attack response
    float attack_raw = env_attack_val;
    float attack_value;
    if (attack_raw < 0.2f) {
        // More exaggerated curve for very short attacks (extra punchy)
        attack_value = attack_raw * (attack_raw * 0.5f);
    } else {
        // Regular cubic response for longer attacks
        attack_value = attack_raw * attack_raw * attack_raw;
    }
    
    // Normal cubic curve for release
    float release_value = env_release_val * env_release_val * env_release_val;
    
    // Determine how many voices to process based on mode
    int voices_to_process = poly_mode ? NUM_VOICES : 1;
    
    for (int v = 0; v < voices_to_process; ++v) {
        // Set separate attack and release times for all voice envelopes
        voice_envelopes[v].SetAttackTime(attack_value);
        voice_envelopes[v].SetReleaseTime(release_value);
        
        // Process the envelope
        float env_value = voice_envelopes[v].Process();
        
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            mix_buffer_out[i] += output_buffers[v][i].out * env_value;
            mix_buffer_aux[i] += output_buffers[v][i].aux * env_value;
        }
    }
}

void ProcessAudioOutput(AudioHandle::InterleavingOutputBuffer out, size_t size, float dry_level) {
    // --- Process Echo Delay & Write Output ---
    float wet_level = 1.0f - dry_level; 
    float norm_factor = (float)NUM_VOICES * 1.0f; // Was 1.5f
    
    for (size_t i = 0; i < size; i += 2) {
        float dry_left  = mix_buffer_out[i/2] / 32768.f / norm_factor;
        float dry_right = mix_buffer_aux[i/2] / 32768.f / norm_factor;
        
        float wet_left = delay.Process(dry_left); 
        float wet_right = delay.Process(dry_right); 
                
        // Apply master volume scaling
        out[i]   = ((dry_left * dry_level) + (wet_left * wet_level)) * MASTER_VOLUME;
        out[i+1] = ((dry_right * dry_level) + (wet_right * wet_level)) * MASTER_VOLUME;
    }
}

void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out) {
    // --- Update Output Level Monitoring --- 
    if (size > 0) { // Ensure block is not empty
        float current_level = fabsf(out[0]); // Absolute level of first sample
        // Apply smoothing (adjust 0.99f/0.01f factor for more/less smoothing)
        smoothed_output_level = smoothed_output_level * 0.99f + current_level * 0.01f; 
    }

    // Signal display update periodically
    static uint32_t display_counter = 0;
    if (++display_counter >= 100) { 
        display_counter = 0;
        update_display = true;
    }
} 