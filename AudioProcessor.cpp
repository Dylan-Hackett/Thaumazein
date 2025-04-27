#include "Thaumazein.h"
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
float pitch_val, harm_knob_val, timbre_knob_val, morph_knob_val;
float delay_time_val, delay_mix_feedback_val, delay_mix_val, delay_feedback_val;
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

volatile int current_engine_index = 0; // Global engine index controlled by touch pads

volatile float adc_raw_values[12] = {0.0f}; // Initialize the array for 12 ADCs
// ADD: flag to indicate engine change so we can retrigger voices even when notes are held
volatile bool engine_changed_flag = false;
// 0 = inactive, 2 = send trigger low this block, 1 = send trigger high next block
volatile int engine_retrigger_phase = 0;

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
    delay_time_knob.Process();        // ADC 0
    delay_mix_feedback_knob.Process(); // ADC 1
    env_release_knob.Process();       // ADC 2
    env_attack_knob.Process();        // ADC 3
    timbre_knob.Process();            // ADC 4
    harmonics_knob.Process();         // ADC 5
    morph_knob.Process();             // ADC 6
    pitch_knob.Process();             // ADC 7
    // Process the remaining ADC-based controls
    arp_pad.Process();            // Process ADC 8: Arpeggiator Toggle Pad
    model_prev_pad.Process();     // Process ADC 9: Model Select Previous Pad
    model_next_pad.Process();     // Process ADC 10: Model Select Next Pad
    mod_wheel.Process();          // Process ADC 11: Mod Wheel Control

    // Read raw values for ALL 12 ADC channels
    for(int i = 0; i < 12; ++i) {
        adc_raw_values[i] = hw.adc.GetFloat(i);
    }

    // --- Model selection logic via touch pads ---
    const float threshold = 0.5f;
    static bool prev_model_prev = false;
    static bool prev_model_next = false;
    bool current_model_prev = model_prev_pad.Value() > threshold;
    bool current_model_next = model_next_pad.Value() > threshold;

    // Implement cyclic selection and reverse direction to match physical pad layout
    // `model_prev_pad` now moves **forward** through the engine list, wrapping at the end.
    // `model_next_pad` moves **backward**, wrapping at the beginning.
    const int kNumEngines = MAX_ENGINE_INDEX + 1; // inclusive range 0..MAX_ENGINE_INDEX

    if(current_model_prev && !prev_model_prev) {
        current_engine_index = (current_engine_index + 1) % kNumEngines;
        engine_changed_flag = true;
    }

    if(current_model_next && !prev_model_next) {
        current_engine_index = (current_engine_index - 1 + kNumEngines) % kNumEngines;
        engine_changed_flag = true;
    }
    prev_model_prev = current_model_prev;
    prev_model_next = current_model_next;

    static int prev_engine_for_reset = 0;
    if(current_engine_index != prev_engine_for_reset) {
        bool prev_was_poly = (prev_engine_for_reset <= 3);
        bool now_poly = (current_engine_index <=3);
        if(prev_was_poly != now_poly) {
            if(prev_was_poly && !now_poly) {
                // Transitioning from poly to mono while notes may be held.
                // Find the first active poly voice and transfer it to voice 0 so sound continues.
                int source_voice = -1;
                for(int v=0; v<NUM_VOICES; ++v) {
                    if(voice_active[v]) { source_voice = v; break; }
                }

                // Clear all voices first
                for(int v=0; v<NUM_VOICES; ++v) {
                    voice_active[v] = false;
                }

                if(source_voice != -1) {
                    voice_active[0] = true;
                    voice_note[0] = voice_note[source_voice];
                    // Transfer envelope state to voice 0 so level stays consistent.
                    voice_envelopes[0] = voice_envelopes[source_voice];
                    voice_envelopes[source_voice].Reset();
                }
            } else {
                // Mono to poly or other transition: simply clear all voices to avoid stuck notes.
                for(int v=0; v<NUM_VOICES; ++v) {
                    voice_active[v] = false;
                }
            }
        }
        prev_engine_for_reset = current_engine_index;
    }
}

void ReadKnobValues() {
    delay_time_val = delay_time_knob.Value();           // ADC 0
    delay_mix_feedback_val = delay_mix_feedback_knob.Value(); // ADC 1 (Combined)
    env_release_val = env_release_knob.Value();       // ADC 2
    env_attack_val = env_attack_knob.Value();        // ADC 3
    timbre_knob_val = timbre_knob.Value();            // ADC 4
    harm_knob_val = harmonics_knob.Value();         // ADC 5
    morph_knob_val = morph_knob.Value();             // ADC 6
    pitch_val = pitch_knob.Value();                 // ADC 7

    // Derive separate mix and feedback values from the combined knob
    // Simple approach: use the same value for both
    delay_mix_val = delay_mix_feedback_val;
    delay_feedback_val = delay_mix_feedback_val;
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
    return current_engine_index;
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
                // NEW: Trigger the mono voice envelope to ensure audio playback
                voice_envelopes[0].Trigger();
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
                    // NEW: Release the mono voice envelope so it decays properly
                    voice_envelopes[0].Release();
                }
            }
        }
    }
    last_touch_state = local_current_touch_state; // Update last state with the value used in this callback
}

void ConfigureDelaySettings() {
    // Use delay_feedback_val derived from ADC 1
    delay.SetFeedback(delay_feedback_val * 0.98f); 
    // Use delay_time_val from ADC 0
    float delay_time_s = 0.01f + delay_time_val * 0.99f; 
    delay.SetDelayTime(delay_time_s);
    // Remove delay lag setting (ADC 7 is now Pitch)
    // float lag_time_s = delay_lag_val * 0.2f; 
    // delay.SetLagTime(lag_time_s);
}

void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx) {
    
    // Update global parameter sources based on new ADC mapping
    float global_pitch_offset = pitch_val * 24.f - 12.f;     // Pitch is now ADC 7
    float current_global_harmonics = harm_knob_val;          // Harmonics is now ADC 5
    // float current_global_decay = decay_knob_val;            // Removed (Decay knob removed)
    float current_global_morph = morph_knob_val;            // Morph is now ADC 6

    // --- Process Effective Voices --- 
    for (int v = 0; v <= max_voice_idx; ++v) { 
        // Update Patch using dedicated knob values
        patches[v].note = voice_note[v] + global_pitch_offset;
        patches[v].engine = engineIndex;                      // Engine index from touch pads
        patches[v].harmonics = current_global_harmonics;    // ADC 5
        patches[v].timbre = timbre_knob_val;                  // ADC 4 (Reconnected)
        patches[v].morph = current_global_morph;            // ADC 6
        patches[v].lpg_colour = 0.0f;
        // patches[v].decay = current_global_decay;            // Removed (Decay knob removed)
        // Ensure decay uses a default or is handled correctly by the engine if needed
        // Setting a default moderate decay:
        patches[v].decay = 0.5f; // Default decay value since knob is gone
        
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
            // For non-poly engines, use direct trigger patching.
            // When the engine just changed while a note is held, force a low trigger for one block
            // to generate a fresh rising edge on the next block so percussive engines retrigger.
            if(engine_changed_flag && voice_active[v]) {
                modulations[v].trigger = 0.0f; // create falling edge this block
            } else {
                modulations[v].trigger = voice_active[v] ? 1.0f : 0.0f;
            }
        }
        
        voices[v].Render(patches[v], modulations[v], output_buffers[v], BLOCK_SIZE);
    }
    
    // Silence unused voices (when switching from poly to mono)
    int effective_voices = max_voice_idx + 1; // Calculate from max_voice_idx
    for (int v = effective_voices; v < NUM_VOICES; ++v) {
         memset(output_buffers[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
         // Keep voice_active[v] = false and modulations[v].trigger = 0.0f
    }

    // If engine changed while notes are active, retrigger envelopes so that the new engine sound is audible immediately.
    if(engine_changed_flag) {
        for(int v = 0; v <= max_voice_idx; ++v) {
            if(voice_active[v]) {
                // Reset and immediately trigger the envelope to restart the note with the new engine.
                voice_envelopes[v].Reset();
                voice_envelopes[v].Trigger();
            }
        }
        engine_changed_flag = false; // Clear flag after handling.
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
        // Use mono mix buffer for both channels
        float sample = mix_buffer_out[i/2] / 32768.f / norm_factor;
        float wet    = delay.Process(sample);
        float output_val = ((sample * dry_level) + (wet * wet_level)) * MASTER_VOLUME;
        out[i]     = output_val;
        out[i+1]   = output_val;
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