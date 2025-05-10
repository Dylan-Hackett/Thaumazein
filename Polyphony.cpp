#include "Thaumazein.h"
#include "Polyphony.h"
#include "stmlib/utils/buffer_allocator.h"

DSY_SDRAM_BSS char shared_buffer[262144];

const int MAX_ENGINE_INDEX = 12;


PolyphonyEngine poly_engine;

const float PolyphonyEngine::kTouchMidiNotes_[12] = {
    40.0f, 41.0f, 43.0f, 45.0f, 47.0f, 48.0f, // E2, F2, G2, A2, B2, C3
    50.0f, 52.0f, 53.0f, 55.0f, 57.0f, 59.0f  // D3, E3, F3, G3, A3, B3
};

PolyphonyEngine::PolyphonyEngine() : allocator_(nullptr), hw_ptr_(nullptr), engine_changed_flag_(false) {
    memset(voice_active_, 0, sizeof(voice_active_));
    memset(voice_note_, 0, sizeof(voice_note_));
    memset(mix_buffer_out_, 0, sizeof(mix_buffer_out_));
    memset(mix_buffer_aux_, 0, sizeof(mix_buffer_aux_));
}

PolyphonyEngine::~PolyphonyEngine() {
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
    bool percussive_engine = (engine_index > 7);

    for (int i = 0; i < 12; ++i) {
        bool pad_currently_pressed = (current_touch_state_param >> i) & 1;
        bool pad_was_pressed = (last_touch_state_param >> i) & 1; 
        float note_for_pad = PolyphonyEngine::kTouchMidiNotes_[i];

        if (pad_currently_pressed && !pad_was_pressed) { // Note ON
            if (poly_mode) {
                int voice_idx = FindFreeVoice(effective_num_voices); 
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
            } else { // Mono mode
                AssignMonoNote(note_for_pad, percussive_engine);
            }
        } else if (!pad_currently_pressed && pad_was_pressed) { // Note OFF
            if (poly_mode) {
                 int voice_idx = FindVoiceForNote(note_for_pad, engine_index, poly_mode, effective_num_voices);
                 if (voice_idx != -1) {
                     voice_active_[voice_idx] = false; 
                     voice_envelopes_[voice_idx].Release(); 
                     modulations_[voice_idx].trigger_patched = false; 
                 }
            } else { // Mono mode
                if (voice_active_[0] && fabsf(voice_note_[0] - note_for_pad) < 0.1f) {
                    voice_active_[0] = false; 
                    voice_envelopes_[0].Release();
                    modulations_[0].trigger_patched = false;
                }
            }
        }
    }
}

void PolyphonyEngine::RenderBlock(const RenderParameters& params) {
    PrepVoiceParams(params);
    
    ProcessEnvelopes(params.poly_mode);

    if (params.arp_on) {
        modulations_[0].trigger = 0.0f;
        modulations_[0].trigger_patched = false; 
    }
}

void PolyphonyEngine::ResetVoices() {
    for (int v = 0; v < NUM_VOICES; v++) {
        voice_envelopes_[v].Reset();
        voice_active_[v] = false;
        modulations_[v].trigger = 0.0f;
        modulations_[v].trigger_patched = false; 
        modulations_[v].level_patched = false;  
    }
}

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

void PolyphonyEngine::PrepVoiceParams(const RenderParameters& params) {
    bool percussive_engine = (params.engine_index > 7);

    float attack_value = 0.0f;
    float release_value = 0.0f;
    if (!percussive_engine) {
        float attack_raw = params.env_attack_val; 
        if (attack_raw < 0.2f) {
            attack_value = attack_raw * (attack_raw * 0.5f);
        } else {
            attack_value = attack_raw * attack_raw * attack_raw;
        }
        release_value = params.env_release_val * params.env_release_val * params.env_release_val;
    }

    float global_pitch_offset = params.pitch_val * 24.f - 12.f;
    float current_global_harmonics = params.harm_knob_val;
    float current_global_morph = params.morph_knob_val;
    float current_global_timbre = params.timbre_knob_val;

    for (int v = 0; v <= params.effective_num_voices - 1; ++v) { 
        PatchParams patch_params;
        patch_params.engine_idx = params.engine_index;
        patch_params.note = voice_note_[v];
        patch_params.global_pitch_offset = global_pitch_offset;
        patch_params.harmonics = current_global_harmonics;
        patch_params.timbre = current_global_timbre;
        patch_params.morph = current_global_morph;
        patch_params.arp_on = params.arp_on;
        patch_params.decay = release_value;
        
        UpdatePatchParams(patches_[v], patch_params);

        UpdateModAndEnv(
            modulations_[v],
            voice_envelopes_[v],
            percussive_engine,
            attack_value,
            release_value
        );

        if (!params.poly_mode && !params.arp_on) {
            UpdateMonoTrigger(
                modulations_[v],
                voice_active_[v],
                engine_changed_flag_
            );
        }
        
        voices_[v].Render(patches_[v], modulations_[v], output_buffers_[v], BLOCK_SIZE);

        if (!params.poly_mode && !params.arp_on && (patches_[v].engine > 7) && v == 0) {
            modulations_[v].trigger = 0.0f;
        }
    }
    
    int effective_voices = params.effective_num_voices; 
    for (int v = effective_voices; v < NUM_VOICES; ++v) {
         SilenceVoice(v);
    }

    if(engine_changed_flag_) {
        for(int v = 0; v < effective_voices; ++v) {
            RetriggerVoice(v);
        }
        engine_changed_flag_ = false;
    }
}

void PolyphonyEngine::ProcessEnvelopes(bool poly_mode) {
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

void PolyphonyEngine::UpdatePatchParams(plaits::Patch& patch, const PatchParams& params) {
    patch.note = params.note + params.global_pitch_offset;
    patch.engine = params.engine_idx;
    patch.harmonics = params.harmonics;
    patch.timbre = params.timbre;
    patch.morph = params.morph;
    patch.lpg_colour = 0.0f;
    patch.decay = params.decay;
    patch.frequency_modulation_amount = 0.f;
    patch.timbre_modulation_amount = 0.f;
    patch.morph_modulation_amount = 0.f;
}

void PolyphonyEngine::UpdateModAndEnv(plaits::Modulations& mod, VoiceEnvelope& env, bool percussive_engine, float attack_value, float release_value) {
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

void PolyphonyEngine::UpdateMonoTrigger(plaits::Modulations& mod, bool& active_flag, bool engine_changed_flag_param) {
    if ((engine_changed_flag_param && active_flag) || !active_flag) {
        mod.trigger = 0.0f; 
    } 
}

void PolyphonyEngine::SilenceVoice(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < NUM_VOICES) {
        memset(output_buffers_[voice_idx], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void PolyphonyEngine::RetriggerVoice(int voice_idx) {
    if (voice_idx >= 0 && voice_idx < NUM_VOICES && voice_active_[voice_idx]) {
        bool percussive_engine = (patches_[voice_idx].engine > 7);
        if (!percussive_engine) {
            voice_envelopes_[voice_idx].Reset();
            voice_envelopes_[voice_idx].Trigger();
        }

        modulations_[voice_idx].trigger = 1.0f;
        if(percussive_engine) {
            modulations_[voice_idx].trigger_patched = true;
        }
    }
}

void PolyphonyEngine::ClearVoices() {
    for (int v = 0; v < NUM_VOICES; ++v) {
        voice_envelopes_[v].Reset();
        voice_active_[v] = false;
        modulations_[v].trigger = 0.0f;
        modulations_[v].trigger_patched = false;
        modulations_[v].level_patched = false; 
        memset(output_buffers_[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
    }
}

void PolyphonyEngine::PolyToMono(int source_voice_idx) {
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
        
        voice_envelopes_[0] = voice_envelopes_[source_voice_idx];

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
            PolyToMono(source_voice);
        } else {
            ClearVoices();
        }
    }
    else {
        ClearVoices();
    }
}

uint16_t PolyphonyEngine::GetLastTouchState() const {
    return last_touch_state_member_;
}

void PolyphonyEngine::UpdateLastTouchState(uint16_t current_state) {
    last_touch_state_member_ = current_state;
}

int PolyphonyEngine::FindFreeVoice(int max_voices) {
    for (int i = 0; i < max_voices; ++i) {
        if (!voice_active_[i]) {
            return i;
        }
    }
    
    return -1; 
}

void PolyphonyEngine::AssignMonoNote(float note, bool percussive_engine) {
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

void PolyphonyEngine::TriggerArpVoice(int pad_idx, int current_engine_index_val) {
    if (pad_idx < 0 || pad_idx >= 12) return;

    float note_to_play = kTouchMidiNotes_[pad_idx];
    bool percussive = (current_engine_index_val > 7);

    voice_note_[0] = note_to_play;
    voice_active_[0] = true;

    PatchParams patch_params;
    patch_params.engine_idx = current_engine_index_val;
    patch_params.note = note_to_play;
    patch_params.global_pitch_offset = 0.0f;
    patch_params.harmonics = 0.5f;  // Default values
    patch_params.timbre = 0.5f;
    patch_params.morph = 0.5f;
    patch_params.arp_on = true;
    patch_params.decay = env_release_val;  // wire knob to arp decay
    
    UpdatePatchParams(patches_[0], patch_params);

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
