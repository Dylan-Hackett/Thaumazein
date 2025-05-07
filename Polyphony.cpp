#include "Thaumazein.h"

// --- Polyphony Setup ---
plaits::Voice voices[NUM_VOICES];
plaits::Patch patches[NUM_VOICES];
plaits::Modulations modulations[NUM_VOICES];

// Place the shared buffer in SDRAM using the DSY_SDRAM_BSS attribute
// Increase buffer size for 4 voices (256KB)
DSY_SDRAM_BSS char shared_buffer[262144]; 

plaits::Voice::Frame output_buffers[NUM_VOICES][BLOCK_SIZE]; 

bool voice_active[NUM_VOICES] = {false};
float voice_note[NUM_VOICES] = {0.0f};
uint16_t last_touch_state = 0;
VoiceEnvelope voice_envelopes[NUM_VOICES]; 

// Map touch pads (0-11) to MIDI notes (E Phrygian scale starting at E2)
const float kTouchMidiNotes[12] = {
    40.0f, 41.0f, 43.0f, 45.0f, 47.0f, 48.0f, // E2, F2, G2, A2, B2, C3
    50.0f, 52.0f, 53.0f, 55.0f, 57.0f, 59.0f  // D3, E3, F3, G3, A3, B3
};

// Engine configuration
// Allow all engines, polyphony determined dynamically
// Total registered engines in plaits::Voice (see Voice::Init)
const int MAX_ENGINE_INDEX = 12; // Valid indices: 0-12

// --- Voice management functions ---
int FindVoice(float note, int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (voice_active[v] && fabsf(voice_note[v] - note) < 0.1f) {
            return v;
        }
    }
    return -1; // Not found
}

int FindAvailableVoice(int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (!voice_active[v]) {
            return v;
        }
    }
    return -1; // All allowed voices active
}

void AssignMonoNote(float note) {
    if (voice_active[0] && fabsf(voice_note[0] - note) > 0.1f) {
        voice_active[0] = false; // Mark old note for release
    }
    voice_note[0] = note;
    voice_active[0] = true;
    modulations[0].trigger = 1.0f; // Set trigger high for the new note
}

void InitializeVoices() {
    // Initialize the allocator with the SDRAM buffer
    stmlib::BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));

    // Initialize Plaits Voices 
    for (int i = 0; i < NUM_VOICES; ++i) { // Initialize all 4 voices
        voices[i].Init(&allocator);
        patches[i].engine = 0;      
        modulations[i].engine = 0; 
        modulations[i].trigger = 0.0f;
        modulations[i].level_patched = false; // Initialize level patched flag
        voice_active[i] = false;
        voice_note[i] = 0.0f;
        
        // Initialize envelopes with proper settings
        voice_envelopes[i].Init(SAMPLE_RATE);
        voice_envelopes[i].SetMode(VoiceEnvelope::MODE_AR);  // Use AR mode (no sustain) for snappier release
        voice_envelopes[i].SetShape(0.5f); // Start with middle curve
    }
} 

// --- Touch Input Handling (Moved from AudioProcessor.cpp) ---
void HandleTouchInput(int engineIndex, bool poly_mode, int effective_num_voices) {
    // Determine if the selected engine is percussive (uses internal envelope)
    bool percussiveEngine = engineIndex > 7;
    uint16_t local_current_touch_state = current_touch_state;
    // The comment "Don't update last_touch_state here anymore..." is removed as per instruction.

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
    // The comment "last_touch_state = local_current_touch_state; // Update removed from here" is removed as per instruction.
} 

// --- Polyphony Helper Function Implementations ---

void UpdateVoicePatchParams(
    plaits::Patch& patch, 
    int engine_idx, 
    float base_note, 
    float pitch_offset, 
    float harmonics_val, 
    float timbre_val,    
    float morph_val,     
    bool arp_on, 
    float env_release_val
) {
    patch.note = base_note + pitch_offset;
    patch.engine = engine_idx;
    patch.harmonics = harmonics_val;
    patch.timbre = timbre_val;
    patch.morph = morph_val;
    patch.lpg_colour = 0.0f;
    patch.decay = arp_on ? env_release_val : 0.5f; 
    patch.frequency_modulation_amount = 0.f;
    patch.timbre_modulation_amount = 0.f;
    patch.morph_modulation_amount = 0.f;
}

void UpdateVoiceModulationAndEnvelope(
    plaits::Modulations& mod, 
    VoiceEnvelope& envelope, 
    bool percussive_engine, 
    float attack_val, 
    float release_val
) {
    mod.engine = 0; 
    mod.note = 0.0f; 
    mod.frequency = 0.0f;
    mod.harmonics = 0.0f; 
    mod.timbre = 0.0f;
    mod.morph = 0.0f; 

    if (!percussive_engine) {
        envelope.SetAttackTime(attack_val);
        envelope.SetReleaseTime(release_val);
        float env_value = envelope.Process();
        mod.level = env_value;
        mod.level_patched = true;
    } else {
        mod.level = 1.0f;
        mod.level_patched = false;
    }
}

void UpdateMonoNonArpVoiceTrigger(
    plaits::Modulations& mod, 
    bool voice_active_state,
    bool engine_changed_flag
) {
    if (engine_changed_flag && voice_active_state) {
        mod.trigger = 0.0f; 
    } else {
        mod.trigger = voice_active_state ? 1.0f : 0.0f;
    }
}

void RenderAndProcessPercussiveArpVoice(
    int voice_idx,
    int engine_idx,
    float global_pitch_offset,
    float current_global_harmonics,
    float current_global_morph,
    float timbre_knob_val_param, 
    float current_env_release_val
) {
    UpdateVoicePatchParams(
        patches[voice_idx],
        engine_idx,
        voice_note[voice_idx], 
        global_pitch_offset,
        current_global_harmonics,
        timbre_knob_val_param,
        current_global_morph,
        true, 
        current_env_release_val
    );
    
    voices[voice_idx].Render(patches[voice_idx], modulations[voice_idx], output_buffers[voice_idx], BLOCK_SIZE);
    
    modulations[voice_idx].trigger = 0.0f;
    modulations[voice_idx].trigger_patched = false;

    if (voice_idx == 0) { 
        for (int v = 1; v < NUM_VOICES; ++v) {
            SilenceVoiceOutput(v);
        }
    }
}

void SilenceVoiceOutput(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < NUM_VOICES) {
        memset(output_buffers[voice_idx], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void RetriggerActiveVoiceEnvelope(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < NUM_VOICES && voice_active[voice_idx]) {
        voice_envelopes[voice_idx].Reset();
        voice_envelopes[voice_idx].Trigger();
    }
} 