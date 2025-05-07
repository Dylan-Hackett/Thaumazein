#include "Thaumazein.h"
#include "mpr121_daisy.h"
#include "DelayEffect.h"
#include <cmath>
#include <algorithm>
#include <vector> // Add vector for dynamic list

// const float MASTER_VOLUME = 0.7f; // Master output level scaler // REMOVED - Defined in Thaumazein.h

int  DetermineEngineSettings();
// void ConfigureDelaySettings(); // Ensure this is removed or commented
void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx, bool arp_on);
void ProcessVoiceEnvelopes(bool poly_mode);
// void ProcessAudioOutput(AudioHandle::InterleavingOutputBuffer out, size_t size, float dry_level); // Ensure this is removed or commented
void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out);
void ResetVoiceStates();

// Global variables for data sharing between decomposed functions
float attack_time, release_time; // attack_time and release_time are used locally in ProcessVoices
float mix_buffer_out[BLOCK_SIZE];
float mix_buffer_aux[BLOCK_SIZE];

extern VoiceEnvelope voice_envelopes[NUM_VOICES];
// extern float voice_values[NUM_VOICES]; // REMOVED
extern bool voice_active[NUM_VOICES];
// extern Mpr121 touch_sensor; // REMOVED - Handled by Thaumazein.h
// extern AnalogControl env_attack_knob; // REMOVED - Handled by Thaumazein.h

// Define the CpuLoadMeter instance
CpuLoadMeter cpu_meter;

volatile int current_engine_index = 0; // Global engine index controlled by touch pads

// ADD: flag to indicate engine change so we can retrigger voices even when notes are held
volatile bool engine_changed_flag = false;
// 0 = inactive, 2 = send trigger low this block, 1 = send trigger high next block
volatile int engine_retrigger_phase = 0;

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                 AudioHandle::InterleavingOutputBuffer out,
                 size_t size) {
    static bool audio_cb_started = false;
    if(!audio_cb_started) {
        hw.PrintLine("[AudioCallback] Enter");
        audio_cb_started = true;
    }
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

    // Arp state is now managed by Interface.cpp, read the global flag
    bool arp_on = arp_enabled;
    static bool was_arp_on = false;
    if (!arp_on && was_arp_on) {
        // ARP just turned off: clear all voices and modulation gates
        ResetVoiceStates();
        for (int v = 0; v < NUM_VOICES; ++v) {
            modulations[v].trigger = 0.0f;
            modulations[v].trigger_patched = false;
        }
        // Reset touch history so that any keys still held will register as new touches
        // when we exit ARP mode. This allows immediate playback in non-ARP mode while
        // pads are kept pressed.
        last_touch_state = 0;
    }
    was_arp_on = arp_on;

    if (arp_on) {
        // --- Update Arp Notes based on Touch Changes ---
        uint16_t st = current_touch_state;
        arp.UpdateHeldNotes(st, last_touch_state);

        // Map ADC0 (delay_time_val) to tempo and ADC11 (mod_wheel) to polyrhythm ratio
        arp.SetMainTempoFromKnob(delay_time_val); // delay_time_val is global
        arp.SetPolyrhythmRatioFromKnob(mod_wheel.Value()); // mod_wheel is global AnalogControl
        
        // Process arpeggiator for this block (frames = size/2)
        arp.Process(size / 2);
    } else {
        // Arp is off, clear the ordered notes list
        arp.SetNotes(nullptr, 0);
        // Default touch-driven note handling
        HandleTouchInput(engineIndex, poly_mode, effective_num_voices);
    }
    
    // Update last_touch_state *after* using it to detect changes for ARP
    last_touch_state = current_touch_state;

    // Configure delay settings
    DelayEffect::UpdateDelay();
    
    // Process voice parameters and render audio
    // Prepare voice rendering: skip trigger reset in ARP mode
    if (arp_on) {
        // Mono ARP: only voice 0, preserve trigger_patched set by callback
        PrepareVoiceParameters(engineIndex, false, 0, true);
    } else {
        // Normal mode: reset triggers per engine
        PrepareVoiceParameters(engineIndex, poly_mode, effective_num_voices - 1, false);
    }
    
    // Process envelopes and mix audio
    ProcessVoiceEnvelopes(poly_mode);
    
    // Mix dry/wet and apply delay
    float dry_level = 1.0f - delay_mix_val;
    DelayEffect::ApplyDelayToOutput(mix_buffer_out, out, size, dry_level);
    
    // After rendering the ARP pulse, clear triggers so next block can retrigger
    if (arp_on) {
        modulations[0].trigger = 0.0f;
        modulations[0].trigger_patched = 0.0f;
    }

    // Mark the end of the audio block
    cpu_meter.OnBlockEnd();
    
    // Update other performance monitors (like output level)
    UpdatePerformanceMonitors(size, out);
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

// void ConfigureDelaySettings() { // Ensure this is removed or commented
//     // Use delay_feedback_val derived from ADC 1
//     delay.SetFeedback(delay_feedback_val * 0.98f); 
//     // Use delay_time_val from ADC 0
//     float delay_time_s = 0.01f + delay_time_val * 0.99f; 
//     delay.SetDelayTime(delay_time_s);
//     // Remove delay lag setting (ADC 7 is now Pitch)
//     // float lag_time_s = delay_lag_val * 0.2f; 
//     // delay.SetLagTime(lag_time_s);
// }

void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx, bool arp_on) {
    bool percussiveEngine = (engineIndex > 7);

    float attack_value = 0.0f;
    float release_value = 0.0f;
    if (!percussiveEngine) {
        float attack_raw = env_attack_val;
        if (attack_raw < 0.2f) {
            attack_value = attack_raw * (attack_raw * 0.5f);
        } else {
            attack_value = attack_raw * attack_raw * attack_raw;
        }
        release_value = env_release_val * env_release_val * env_release_val;
    }

    float global_pitch_offset = pitch_val * 24.f - 12.f;
    float current_global_harmonics = harm_knob_val;
    float current_global_morph = morph_knob_val;

    if (arp_on && percussiveEngine) {
        RenderAndProcessPercussiveArpVoice(
            0, 
            engineIndex,
            global_pitch_offset,
            current_global_harmonics,
            current_global_morph,
            timbre_knob_val, 
            env_release_val  
        );
        return; 
    }

    for (int v = 0; v <= max_voice_idx; ++v) { 
        UpdateVoicePatchParams(
            patches[v],
            engineIndex,
            voice_note[v],
            global_pitch_offset,
            current_global_harmonics,
            timbre_knob_val, 
            current_global_morph,
            arp_on,
            env_release_val 
        );

        UpdateVoiceModulationAndEnvelope(
            modulations[v],
            voice_envelopes[v],
            percussiveEngine,
            attack_value,
            release_value
        );

        if (!poly_mode && !arp_on) {
            UpdateMonoNonArpVoiceTrigger(
                modulations[v],
                voice_active[v], 
                engine_changed_flag 
            );
        }
        
        voices[v].Render(patches[v], modulations[v], output_buffers[v], BLOCK_SIZE);
    }
    
    int effective_voices = max_voice_idx + 1; 
    for (int v = effective_voices; v < NUM_VOICES; ++v) {
         SilenceVoiceOutput(v);
    }

    if(engine_changed_flag) {
        for(int v = 0; v <= max_voice_idx; ++v) {
            RetriggerActiveVoiceEnvelope(v); 
        }
        engine_changed_flag = false; 
    }
}

void ProcessVoiceEnvelopes(bool poly_mode) {
    // Simply mix rendered voice buffers – amplitude now handled by modulations.level inside Plaits.
    memset(mix_buffer_out, 0, sizeof(mix_buffer_out));
    memset(mix_buffer_aux, 0, sizeof(mix_buffer_aux));

    int voices_to_process = poly_mode ? NUM_VOICES : 1;
    for (int v = 0; v < voices_to_process; ++v) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            mix_buffer_out[i] += output_buffers[v][i].out;
            mix_buffer_aux[i] += output_buffers[v][i].aux;
        }
    }
}

// void ProcessAudioOutput(AudioHandle::InterleavingOutputBuffer out, size_t size, float dry_level) { // Ensure this is removed or commented
//     // --- Process Echo Delay & Write Output ---
//     float wet_level = 1.0f - dry_level; 
//     float norm_factor = (float)NUM_VOICES * 1.0f; // Was 1.5f
//     
//     for (size_t i = 0; i < size; i += 2) {
//         // Use mono mix buffer for both channels
//         float sample = mix_buffer_out[i/2] / 32768.f / norm_factor;
//         float wet    = delay.Process(sample);
//         float output_val = ((sample * dry_level) + (wet * wet_level)) * MASTER_VOLUME;
//         out[i]     = output_val;
//         out[i+1]   = output_val;
//     }
// }

void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out) {
    if (size > 0) {
        float current_level = fabsf(out[0]);
        smoothed_output_level = smoothed_output_level * 0.99f + current_level * 0.01f;
    }

    // Signal display update every 1 second
    static uint32_t display_counter = 0;
    static const uint32_t display_interval_blocks = (uint32_t)(sample_rate / BLOCK_SIZE * 3.0f);
    if (++display_counter >= display_interval_blocks) {
        display_counter = 0;
        update_display = true;
    }
} 