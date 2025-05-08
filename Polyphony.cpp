#include "Thaumazein.h"
#include "Polyphony.h"
#include "stmlib/utils/buffer_allocator.h"

DSY_SDRAM_BSS char shared_buffer[262144];

const int MAX_ENGINE_INDEX = 12;

// --- PolyphonyEngine Class Implementation ---

// Global instance of the PolyphonyEngine
PolyphonyEngine poly_engine;

// Definition of the static member
const float PolyphonyEngine::kTouchMidiNotes_[12] = {
    40.0f, 41.0f, 43.0f, 45.0f, 47.0f, 48.0f, // E2, F2, G2, A2, B2, C3
    50.0f, 52.0f, 53.0f, 55.0f, 57.0f, 59.0f  // D3, E3, F3, G3, A3, B3
};

PolyphonyEngine::PolyphonyEngine() : allocator_(nullptr), hw_ptr_(nullptr), engine_changed_flag_(false) {
    // Constructor: Initialize arrays to zero or default states if necessary.
    // Most of the detailed initialization will happen in Init().
    memset(voice_active_, 0, sizeof(voice_active_));
    memset(voice_note_, 0, sizeof(voice_note_));
    memset(mix_buffer_out_, 0, sizeof(mix_buffer_out_));
    memset(mix_buffer_aux_, 0, sizeof(mix_buffer_aux_));
    // output_buffers_ will be filled during rendering.
    // patches_ and modulations_ have default constructors or will be set in Init.
}

PolyphonyEngine::~PolyphonyEngine() {
    // Destructor: Clean up resources if any were dynamically allocated by this class instance.
    if (allocator_) {
        delete allocator_;
        allocator_ = nullptr;
    }
}

void PolyphonyEngine::Init(daisy::DaisySeed* hw) {
    hw_ptr_ = hw;
    AllocateVoices(); 
    InitVoiceParameters(); 
}

void PolyphonyEngine::HandleTouchInput(uint16_t current_touch_state_param, uint16_t last_touch_state_param, int engine_index, bool poly_mode, int effective_num_voices) {
    // Ported from global HandleTouchInput in Polyphony.cpp
    // Uses current_touch_state_param directly, and updates last_touch_state_member_ at the end.
    
    bool percussive_engine = (engine_index > 7);

    // kTouchMidiNotes is now PolyphonyEngine::kTouchMidiNotes_

    for (int i = 0; i < 12; ++i) {
        bool pad_currently_pressed = (current_touch_state_param >> i) & 1;
        // Use the passed last_touch_state_param for this iteration's comparison
        bool pad_was_pressed = (last_touch_state_param >> i) & 1; 
        float note_for_pad = PolyphonyEngine::kTouchMidiNotes_[i]; // Use static class member

        if (pad_currently_pressed && !pad_was_pressed) { // Note ON
            if (poly_mode) {
                int voice_idx = FindAvailableVoiceInternal(effective_num_voices); 
                if (voice_idx != -1) {
                    voice_note_[voice_idx] = note_for_pad;
                    voice_active_[voice_idx] = true;
                    modulations_[voice_idx].trigger = 1.0f; 
                    if (percussive_engine) {
                        modulations_[voice_idx].trigger_patched = true;
                    } else {
                        modulations_[voice_idx].trigger_patched = false;
                    }
                    voice_envelopes_[voice_idx].Trigger(); 
                }
                // TODO: Implement voice stealing if voice_idx == -1 (all voices busy)
                // else { StealVoice(...) }
            } else { // Mono mode
                AssignMonoNoteInternal(note_for_pad, engine_index, percussive_engine);
            }
        } else if (!pad_currently_pressed && pad_was_pressed) { // Note OFF
            if (poly_mode) {
                 int voice_idx = FindVoiceForNote(note_for_pad, engine_index, poly_mode, effective_num_voices);
                 if (voice_idx != -1) {
                     voice_active_[voice_idx] = false; 
                     voice_envelopes_[voice_idx].Release(); 
                     // Ensure trigger_patched is false on note off for poly voices if it was set true by percussive engine
                     modulations_[voice_idx].trigger_patched = false; 
                 }
            } else { // Mono mode
                if (voice_active_[0] && fabsf(voice_note_[0] - note_for_pad) < 0.1f) {
                    voice_active_[0] = false; 
                    voice_envelopes_[0].Release();
                    modulations_[0].trigger_patched = false; // Also for mono mode
                }
            }
        }
    }
    // The caller (AudioProcessor::UpdateArpState) will be responsible for updating poly_engine.last_touch_state_member_ or passing the correct last_touch_state.
    // For consistency, if this function needs the *previous* state, it should be passed in.
    // The parameter `last_touch_state_param` is used for this block's logic.
    // `last_touch_state_member_` can be updated by the caller after this function returns.
}

void PolyphonyEngine::RenderBlock(int engine_index, bool poly_mode, int effective_num_voices, bool arp_on, 
                                 float pitch_val, float harm_knob_val, float morph_knob_val, 
                                 float timbre_knob_val, float env_attack_val, float env_release_val, 
                                 float delay_mix_val, float touch_cv_value) {
    // This function will orchestrate the voice rendering for a block.
    
    // 1. Prepare voice parameters (similar to the old PrepareVoiceParameters)
    PrepareVoiceParametersInternal(engine_index, poly_mode, effective_num_voices -1, arp_on, pitch_val, harm_knob_val, morph_knob_val, timbre_knob_val, env_attack_val, env_release_val);

    // 2. Process voice envelopes and mix audio (similar to the old ProcessVoiceEnvelopes)
    ProcessVoiceEnvelopesInternal(poly_mode);

    // After rendering the ARP pulse, clear triggers so next block can retrigger
    if (arp_on) {
        modulations_[0].trigger = 0.0f;
        modulations_[0].trigger_patched = false; 
    }
}

void PolyphonyEngine::ResetVoices() {
    // Logic ported from global ResetVoiceStates() and enhanced.
    for (int v = 0; v < NUM_VOICES; v++) {
        voice_envelopes_[v].Reset();
        voice_active_[v] = false;
        modulations_[v].trigger = 0.0f;
        modulations_[v].trigger_patched = false; // Ensure modulation triggers are reset
        modulations_[v].level_patched = false;   // Ensure level patching is reset
    }
}

void PolyphonyEngine::TriggerArpNote(int voice_index, float note, float strength, int engine_index, bool percussive_engine) {
    if (voice_index < 0 || voice_index >= NUM_VOICES) return;

    voice_note_[voice_index] = note; // Store the note being triggered
    voice_active_[voice_index] = true; // Mark as active

    // Set up basic patch parameters for the ARP note
    // These might need to be more dynamic based on current global/knob settings
    patches_[voice_index].note = note; // Use the exact note from ARP
    patches_[voice_index].engine = engine_index; 
    // Set other patch params (harmonics, timbre, morph) based on current settings if needed
    // For now, assume they are set by RenderBlock or a general update.

    modulations_[voice_index].trigger = strength; // Strength could be velocity (0.0 to 1.0)
    modulations_[voice_index].trigger_patched = true; // ARP directly controls the trigger

    if (!percussive_engine) {
        voice_envelopes_[voice_index].Trigger(); // Trigger the ADSR/AR envelope
    }
    // For percussive engines, the trigger_patched on modulations should be enough.
}

// --- Private Helper Implementations ---

void PolyphonyEngine::AllocateVoices() {
    if (!allocator_) {
        allocator_ = new stmlib::BufferAllocator(shared_buffer, sizeof(shared_buffer));
    }

    for (int i = 0; i < NUM_VOICES; ++i) {
        voices_[i].Init(allocator_);
    }
}

void PolyphonyEngine::InitVoiceParameters() {
    float sample_rate_val = SAMPLE_RATE;
    if (hw_ptr_) {
        // Consider hw_ptr_->AudioSampleRate()
    }

    for (int i = 0; i < NUM_VOICES; ++i) {
        patches_[i].engine = 0;      
        modulations_[i].engine = 0; 
        modulations_[i].trigger = 0.0f;
        modulations_[i].level_patched = false;
        voice_active_[i] = false;
        voice_note_[i] = 0.0f;
        
        voice_envelopes_[i].Init(sample_rate_val); 
        voice_envelopes_[i].SetMode(VoiceEnvelope::MODE_AR);
        voice_envelopes_[i].SetShape(0.5f);

        memset(output_buffers_[i], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void PolyphonyEngine::PrepareVoiceParametersInternal(int engine_index, bool poly_mode, int max_voice_idx, bool arp_on, 
                                                   float pitch_val, float harm_knob_val, float morph_knob_val, 
                                                   float timbre_knob_val, float env_attack_val, float env_release_val) {
    // TODO: Adapt logic from AudioProcessor.cpp's PrepareVoiceParameters function.

    bool percussive_engine = (engine_index > 7);

    float attack_value = 0.0f;
    float release_value = 0.0f;
    if (!percussive_engine) {
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
    float current_global_timbre = timbre_knob_val;


    if (arp_on && percussive_engine) {
        RenderAndProcessPercussiveArpVoiceInternal(
            0, 
            engine_index,
            global_pitch_offset,
            current_global_harmonics,
            current_global_morph,
            current_global_timbre, 
            env_release_val
        );
        return; 
    }

    for (int v = 0; v <= max_voice_idx; ++v) { 
        UpdateVoicePatchParamsInternal(
            patches_[v],
            engine_index,
            voice_note_[v],
            global_pitch_offset,
            current_global_harmonics,
            current_global_timbre, 
            current_global_morph,
            arp_on,
            env_release_val
        );

        UpdateVoiceModulationAndEnvelopeInternal(
            modulations_[v],
            voice_envelopes_[v],
            percussive_engine,
            attack_value,
            release_value
        );

        if (!poly_mode && !arp_on) {
            UpdateMonoNonArpVoiceTriggerInternal(
                modulations_[v],
                voice_active_[v],
                engine_changed_flag_
            );
        }
        
        voices_[v].Render(patches_[v], modulations_[v], output_buffers_[v], BLOCK_SIZE);

        if (!poly_mode && !arp_on && (patches_[v].engine > 7) && v == 0) {
            modulations_[v].trigger = 0.0f;
        }
    }
    
    int effective_voices = max_voice_idx + 1; 
    for (int v = effective_voices; v < NUM_VOICES; ++v) {
         SilenceVoiceOutputInternal(v);
    }

    if(engine_changed_flag_) {
        for(int v = 0; v <= max_voice_idx; ++v) {
            RetriggerActiveVoiceEnvelopeInternal(v);
        }
        engine_changed_flag_ = false;
    }
}

void PolyphonyEngine::ProcessVoiceEnvelopesInternal(bool poly_mode) {
    // TODO: Adapt logic from AudioProcessor.cpp's ProcessVoiceEnvelopes function.
    memset(mix_buffer_out_, 0, sizeof(mix_buffer_out_));
    memset(mix_buffer_aux_, 0, sizeof(mix_buffer_aux_));

    int voices_to_process = poly_mode ? NUM_VOICES : 1;
    for (int v = 0; v < voices_to_process; ++v) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            mix_buffer_out_[i] += output_buffers_[v][i].out;
            mix_buffer_aux_[i] += output_buffers_[v][i].aux;
        }
    }
}

// --- Implementations for other private helpers ---

void PolyphonyEngine::UpdateVoicePatchParamsInternal(plaits::Patch& patch, int engine_idx, float note, float global_pitch_offset, float current_global_harmonics, float current_global_timbre, float current_global_morph, bool arp_on, float current_decay) {
    // TODO: Move logic from global UpdateVoicePatchParams
    patch.note = note + global_pitch_offset;
    patch.engine = engine_idx;
    patch.harmonics = current_global_harmonics;
    patch.timbre = current_global_timbre;
    patch.morph = current_global_morph;
    patch.lpg_colour = 0.0f;
    patch.decay = arp_on ? current_decay : 0.5f;
    patch.frequency_modulation_amount = 0.f;
    patch.timbre_modulation_amount = 0.f;
    patch.morph_modulation_amount = 0.f;
}

void PolyphonyEngine::UpdateVoiceModulationAndEnvelopeInternal(plaits::Modulations& mod, VoiceEnvelope& env, bool percussive_engine, float attack_value, float release_value) {
    // TODO: Move logic from global UpdateVoiceModulationAndEnvelope
    mod.engine = 0;
    mod.note = 0.0f; 
    mod.frequency = 0.0f;
    mod.harmonics = 0.0f; 
    mod.timbre = 0.0f;
    mod.morph = 0.0f; 

    if (!percussive_engine) {
        env.SetAttackTime(attack_value);
        env.SetReleaseTime(release_value);
        float env_value = env.Process();
        mod.level = env_value;
        mod.level_patched = true;
    } else {
        mod.level = 1.0f;
        mod.level_patched = false;
        mod.trigger_patched = true;
    }
}

void PolyphonyEngine::UpdateMonoNonArpVoiceTriggerInternal(plaits::Modulations& mod, bool& active_flag, bool engine_changed_flag_param) {
    if ((engine_changed_flag_param && active_flag) || !active_flag) {
        mod.trigger = 0.0f; 
    } 
}

void PolyphonyEngine::RenderAndProcessPercussiveArpVoiceInternal(int voice_idx, int engine_idx, float global_pitch_offset, float current_global_harmonics, float current_global_morph, float current_global_timbre, float current_env_release_val_param) {
    // Ported from global RenderAndProcessPercussiveArpVoice
    if (voice_idx < 0 || voice_idx >= NUM_VOICES) return;

    UpdateVoicePatchParamsInternal(
        patches_[voice_idx],
        engine_idx,
        voice_note_[voice_idx], 
        global_pitch_offset,
        current_global_harmonics,
        current_global_timbre,
        current_global_morph,
        true, // arp_on is true here
        current_env_release_val_param
    );
    


    voices_[voice_idx].Render(patches_[voice_idx], modulations_[voice_idx], output_buffers_[voice_idx], BLOCK_SIZE);
    
    // Clear trigger after rendering for one-shot behavior if plaits engine expects it


    // Silence other voices if this is the primary voice (voice 0) in a mono-like ARP setup
    if (voice_idx == 0) { 
        for (int v = 1; v < NUM_VOICES; ++v) {
            SilenceVoiceOutputInternal(v);
        }
    }
}

void PolyphonyEngine::SilenceVoiceOutputInternal(int voice_idx) {
    // TODO: Move logic from global SilenceVoiceOutput
    if (voice_idx >= 0 && voice_idx < NUM_VOICES) {
        memset(output_buffers_[voice_idx], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void PolyphonyEngine::RetriggerActiveVoiceEnvelopeInternal(int voice_idx) {
    
    if (voice_idx >= 0 && voice_idx < NUM_VOICES && voice_active_[voice_idx]) {
        // For non-percussive engines, re-trigger the envelope
        bool percussive_engine = (patches_[voice_idx].engine > 7); // Get engine from patch
        if (!percussive_engine) {
            voice_envelopes_[voice_idx].Reset();
            voice_envelopes_[voice_idx].Trigger();
        }

        modulations_[voice_idx].trigger = 1.0f; // Send a new trigger
        if(percussive_engine) {
            modulations_[voice_idx].trigger_patched = true;
        }
        
    }
}

void PolyphonyEngine::ClearAllVoicesForEngineSwitch() {
    for (int v = 0; v < NUM_VOICES; ++v) {
        voice_envelopes_[v].Reset();
        voice_active_[v] = false;
        modulations_[v].trigger = 0.0f;
        modulations_[v].trigger_patched = false;
        modulations_[v].level_patched = false; 
        memset(output_buffers_[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void PolyphonyEngine::TransferPolyToMonoVoice(int source_voice_idx) {
    if (source_voice_idx < 0 || source_voice_idx >= NUM_VOICES) return;

    for (int v = 0; v < NUM_VOICES; ++v) {
        if (v != source_voice_idx) {
            voice_active_[v] = false;
            voice_envelopes_[v].Reset();
            modulations_[v].trigger = 0.0f;
            modulations_[v].trigger_patched = false;
        }
    }

    if (voice_active_[source_voice_idx]) {
        voice_active_[0] = true;
        voice_note_[0] = voice_note_[source_voice_idx];
        
        voice_envelopes_[0] = voice_envelopes_[source_voice_idx]; // This needs VoiceEnvelope to be copyable

        modulations_[0].trigger = modulations_[source_voice_idx].trigger; 
        modulations_[0].trigger_patched = modulations_[source_voice_idx].trigger_patched;
        modulations_[0].level = modulations_[source_voice_idx].level;
        modulations_[0].level_patched = modulations_[source_voice_idx].level_patched;

        voice_active_[source_voice_idx] = false;
        voice_envelopes_[source_voice_idx].Reset();
        modulations_[source_voice_idx].trigger = 0.0f;
        modulations_[source_voice_idx].trigger_patched = false;
    } else {
        voice_active_[0] = false;
        voice_envelopes_[0].Reset();
        modulations_[0].trigger = 0.0f;
        modulations_[0].trigger_patched = false;
    }
}

void PolyphonyEngine::OnEngineChange(int old_engine_idx, int new_engine_idx) {
    bool prev_was_poly = (old_engine_idx <= 3);
    bool now_poly      = (new_engine_idx <= 3);

    if(old_engine_idx == new_engine_idx) {
        return;
    }

    engine_changed_flag_ = true;

    if(prev_was_poly == now_poly) {
        return;
    }

    if(prev_was_poly && !now_poly) {
        int source_voice = -1;
        for(int v = 0; v < NUM_VOICES; ++v) {
            if(voice_active_[v]) { source_voice = v; break; }
        }

        if(source_voice != -1) {
            TransferPolyToMonoVoice(source_voice);
        } else {
            ClearAllVoicesForEngineSwitch();
        }
    }
    else {
        ClearAllVoicesForEngineSwitch();
    }
}

uint16_t PolyphonyEngine::GetLastTouchState() const {
    return last_touch_state_member_;
}

void PolyphonyEngine::UpdateLastTouchState(uint16_t current_state) {
    last_touch_state_member_ = current_state;
}


// --- Definitions for Private Helper Methods for Touch Input ---

int PolyphonyEngine::FindAvailableVoiceInternal(int max_voices) {
    for (int i = 0; i < max_voices; ++i) {
        if (!voice_active_[i]) {
            return i;
        }
    }
    // TODO: Implement voice stealing (e.g. return FindOldestVoice(true, max_voices);)
    return -1; 
}

void PolyphonyEngine::AssignMonoNoteInternal(float note, int engine_index, bool percussive_engine) {
    voice_note_[0] = note;
    voice_active_[0] = true;
    modulations_[0].trigger = 1.0f;

    if (percussive_engine) {
        modulations_[0].trigger_patched = true; 
    } else {
        modulations_[0].trigger_patched = false;
        voice_envelopes_[0].Trigger(); 
    }
}

int PolyphonyEngine::FindVoiceForNote(float note, int engine_index, bool poly_mode, int max_voices) {
    if (poly_mode) {
        for (int i = 0; i < max_voices; ++i) {
            if (voice_active_[i] && fabsf(voice_note_[i] - note) < 0.1f) {
                return i;
            }
        }
    } else { 
        if (voice_active_[0] && fabsf(voice_note_[0] - note) < 0.1f) {
            return 0;
        }
    }
    return -1;
}



void PolyphonyEngine::TriggerArpCallbackVoice(int pad_idx, int current_engine_index_val) {
    if (pad_idx < 0 || pad_idx >= 12) return;

    float note_to_play = kTouchMidiNotes_[pad_idx];
    bool percussive = (current_engine_index_val > 7);

    voice_note_[0] = note_to_play;
    voice_active_[0] = true;

    patches_[0].engine = current_engine_index_val;
    patches_[0].note = note_to_play; 

    modulations_[0].trigger = 1.0f;
    modulations_[0].trigger_patched = true;

    if (!percussive) {
        voice_envelopes_[0].SetMode(VoiceEnvelope::MODE_AR);
        voice_envelopes_[0].Trigger();
    }
}

bool PolyphonyEngine::IsAnyVoiceActive() const {
    for (int i = 0; i < NUM_VOICES; ++i) {
        if (voice_active_[i]) {
            return true;
        }
    }
    return false;
}

bool PolyphonyEngine::IsVoiceActive(int voice_index) const {
    if (voice_index < 0 || voice_index >= NUM_VOICES) {
        return false;
    }
    return voice_active_[voice_index];
}


// --- Touch handling logic (to be moved from global HandleTouchInput and its helpers) ---