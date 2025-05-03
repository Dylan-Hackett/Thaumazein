#include "Thaumazein.h"
#include "mpr121_daisy.h"
#include <cmath>
#include <algorithm>
#include <vector> // Add vector for dynamic list

const float MASTER_VOLUME = 0.7f; // Master output level scaler

void ProcessControls();
void ReadKnobValues();
int  DetermineEngineSettings();
void HandleTouchInput(int engineIndex, bool poly_mode, int effective_num_voices);
void ConfigureDelaySettings();
void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx, bool arp_on);
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

// Global list to store arpeggiator notes in the order they were pressed
std::vector<int> arp_notes_ordered;
int arp_note_indices[12]; // For passing to Arpeggiator class if needed

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

    // Integrate Arpeggiator: hold ADC8 pad to run arp, release to stop
    // Tap-to-toggle behaviour on pad 8
    // --- Arpeggiator toggle pad with hysteresis to avoid double-toggles ---
    static bool arp_enabled = false;
    constexpr float kOnThreshold  = 0.30f;   // more sensitive press detection
    constexpr float kOffThreshold = 0.20f;   // lower release threshold for fast reset

    static bool pad_pressed = false;          // debounced pad pressed state
    float pad_read = arp_pad.Value();

    // Detect state transitions with hysteresis
    if(!pad_pressed && pad_read > kOnThreshold)
    {
        pad_pressed = true;
        // Rising edge detected -> toggle arp
        arp_enabled = !arp_enabled;
        // hw.PrintLine(arp_enabled ? "[DEBUG] ARP ON" : "[DEBUG] ARP OFF"); // Removed debug print
        if(arp_enabled)
        {
            arp.Init(sample_rate);          // restart timing
            arp.SetDirection(Arpeggiator::AsPlayed); // Set default direction to AsPlayed
        }
    }
    else if(pad_pressed && pad_read < kOffThreshold)
    {
        // Consider pad released only when it falls well below off threshold
        pad_pressed = false;
    }
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
        uint16_t changed_pads = st ^ last_touch_state; // Detect changed pads

        if (changed_pads != 0) {
            for (int i = 0; i < 12; ++i) {
                uint16_t mask = 1 << i;
                if (changed_pads & mask) { // If this pad changed state
                    if (st & mask) { // Pad was pressed
                        // Add to list only if not already present (safety check)
                        bool found = false;
                        for(int note_idx : arp_notes_ordered) {
                            if (note_idx == i) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                           arp_notes_ordered.push_back(i);
                        }
                    } else { // Pad was released
                        // Remove from list
                        arp_notes_ordered.erase(
                            std::remove(arp_notes_ordered.begin(), arp_notes_ordered.end(), i),
                            arp_notes_ordered.end()
                        );
                    }
                }
            }
        }

        // Copy the ordered notes to a C-style array for the Arpeggiator class
        // (Arpeggiator::SetNotes expects int*)
        int num_keys = arp_notes_ordered.size();
        for(size_t i=0; i < num_keys; ++i) {
            arp_note_indices[i] = arp_notes_ordered[i];
        }

        // If notes are present, send them to the arpeggiator
        if (num_keys > 0) {
             arp.SetNotes(arp_note_indices, num_keys);
        } else {
             // Ensure arp knows no notes are held if vector is empty
             arp.SetNotes(nullptr, 0);
        }
        // --- End Update Arp Notes ---

        // Map ADC0 (delay_time_val) to tempo range 1-15 Hz
        // Exponential tempo mapping for full knob response: 1 Hz to 30 Hz
        float min_tempo = 1.0f;
        float max_tempo = 30.0f;
        float ratio     = max_tempo / min_tempo;
        float tempo     = min_tempo * powf(ratio, delay_time_val);
        arp.SetMainTempo(tempo);
        // Map mod wheel (ADC 11) to polyrhythm ratio 0.5x to 2.0x
        {
            float min_ratio = 0.5f, max_ratio = 2.0f;
            float mw = mod_wheel.Value();
            float ratio = min_ratio + mw * (max_ratio - min_ratio);
            arp.SetPolyrhythmRatio(ratio);
        }
        // Process arpeggiator for this block (frames = size/2)
        arp.Process(size / 2);
    } else {
        // Arp is off, clear the ordered notes list
        if (!arp_notes_ordered.empty()) {
            arp_notes_ordered.clear();
            arp.SetNotes(nullptr, 0); // Tell arp no notes are held
        }
        // Default touch-driven note handling
        HandleTouchInput(engineIndex, poly_mode, effective_num_voices);
    }
    
    // Update last_touch_state *after* using it to detect changes for ARP
    last_touch_state = current_touch_state;

    // Configure delay settings
    ConfigureDelaySettings();
    
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
    ProcessAudioOutput(out, size, dry_level);
    
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
    // Determine if the selected engine is percussive (uses internal envelope)
    bool percussiveEngine = engineIndex > 7;
    uint16_t local_current_touch_state = current_touch_state;
    // Don't update last_touch_state here anymore, it's done globally after arp check
    // uint16_t local_last_touch_state = last_touch_state; // Use a snapshot

    for (int i = 0; i < 12; ++i) {
        bool pad_currently_pressed = (local_current_touch_state >> i) & 1;
        // Use the *actual* last_touch_state from the previous block for comparison
        bool pad_was_pressed = (last_touch_state >> i) & 1; 
        float note_for_pad = kTouchMidiNotes[i];

        if (pad_currently_pressed && !pad_was_pressed) { 
            if (poly_mode) {
                int voice_idx = FindAvailableVoice(effective_num_voices); 
                if (voice_idx != -1) {
                    voice_note[voice_idx] = note_for_pad;
                    voice_active[voice_idx] = true;
                    modulations[voice_idx].trigger = 1.0f; // For initial transient
                    // Patch the trigger if this is a percussive engine
                    if (percussiveEngine) modulations[voice_idx].trigger_patched = true;
                    voice_envelopes[voice_idx].Trigger(); 
                }
            } else { 
                AssignMonoNote(note_for_pad);
                // NEW: Trigger the mono voice envelope to ensure audio playback
                voice_envelopes[0].Trigger();
                // Patch the trigger for percussive engines
                if (percussiveEngine) modulations[0].trigger_patched = true;
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
    // last_touch_state = local_current_touch_state; // Update removed from here
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

void PrepareVoiceParameters(int engineIndex, bool poly_mode, int max_voice_idx, bool arp_on) {
    // Compute global parameter sources
    float global_pitch_offset = pitch_val * 24.f - 12.f;     // Pitch follows ADC 7
    float current_global_harmonics = harm_knob_val;          // Harmonics ADC 5
    float current_global_morph = morph_knob_val;             // Morph ADC 6

    // Percussive ARP mode: render voice 0 only with patched trigger
    if (arp_on && engineIndex > 7) {
        // Update patch for voice 0
        patches[0].note = voice_note[0] + global_pitch_offset;
        patches[0].engine = engineIndex;
        patches[0].harmonics = current_global_harmonics;
        patches[0].timbre = timbre_knob_val;
        patches[0].morph = current_global_morph;
        patches[0].lpg_colour = 0.0f;
        patches[0].decay = env_release_val; // use release knob as decay
        patches[0].frequency_modulation_amount = 0.0f;
        patches[0].timbre_modulation_amount = 0.0f;
        patches[0].morph_modulation_amount = 0.0f;
        // Render with ARP callback trigger
        voices[0].Render(patches[0], modulations[0], output_buffers[0], BLOCK_SIZE);
        // Silence other voices
        for (int v = 1; v < NUM_VOICES; ++v) {
            memset(output_buffers[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
        }
        // Clear triggers for next block
        modulations[0].trigger = 0.0f;
        modulations[0].trigger_patched = false;
        return;
    }

    // --- Process Effective Voices --- 
    for (int v = 0; v <= max_voice_idx; ++v) { 
        // Update Patch using dedicated knob values
        patches[v].note = voice_note[v] + global_pitch_offset;
        patches[v].engine = engineIndex;                      // Engine index from touch pads
        patches[v].harmonics = current_global_harmonics;    // ADC 5
        patches[v].timbre = timbre_knob_val;                  // ADC 4 (Reconnected)
        patches[v].morph = current_global_morph;            // ADC 6
        patches[v].lpg_colour = 0.0f;
        // Map decay: use release knob when in ARP mode, otherwise a moderate default
        if (arp_on) {
            // Map Plaits internal decay envelope to release knob for smoother release
            patches[v].decay = env_release_val;
        } else {
            patches[v].decay = 0.5f; // moderate default
        }
        
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
        
        // Only reset trigger_patched when not in ARP mode
        if (!arp_on) {
            if(engine_changed_flag && voice_active[v]) {
                modulations[v].trigger = 0.0f; // falling edge on change
            } else {
                modulations[v].trigger = voice_active[v] ? 1.0f : 0.0f;
            }
        }
        
        if (poly_mode) {
            // For poly engines, we'll handle triggering through voice envelopes
            // Only set trigger on initial note-on
            // Don't modify value - already set in HandleTouchInput
        } else {
            // In non-poly mode and ARP active, leave trigger from callback; otherwise override
            if (!arp_on) {
                if (engine_changed_flag && voice_active[v]) {
                    modulations[v].trigger = 0.0f;
                } else {
                    modulations[v].trigger = voice_active[v] ? 1.0f : 0.0f;
                }
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
    // Bypass custom envelope for percussive Plaits engines
    bool percussiveEngine = (current_engine_index > 7);

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
        // Choose envelope value: either custom AR envelope or flat for percussive modes
        float env_value;
        if (!percussiveEngine) {
            voice_envelopes[v].SetAttackTime(attack_value);
            voice_envelopes[v].SetReleaseTime(release_value);
            env_value = voice_envelopes[v].Process();
        } else {
            env_value = 1.0f;
        }
        
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