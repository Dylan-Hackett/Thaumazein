#ifndef POLYPHONY_H
#define POLYPHONY_H

#include "daisy_seed.h"
#include "plaits/dsp/voice.h"
#include "VoiceEnvelope.h"
#include "Thaumazein.h"
#include "stmlib/utils/buffer_allocator.h"

namespace stmlib {
    class MonoBlockAllocator;
}

namespace plaits {
    class Voice;
}

struct PatchParams {
    int engine_idx;
    float note;
    float global_pitch_offset;
    float harmonics;
    float timbre;
    float morph;
    bool arp_on;
    float decay;
};

class PolyphonyEngine {
public:
    struct RenderParameters {
        int engine_index;
        bool poly_mode;
        int effective_num_voices;
        bool arp_on;
        float pitch_val;
        float harm_knob_val;
        float morph_knob_val;
        float timbre_knob_val;
        float env_attack_val;
        float env_release_val;
        float delay_mix_val;
        float touch_cv_value;
    };

    PolyphonyEngine();
    ~PolyphonyEngine();

    void Init(daisy::DaisySeed* hw);
    void HandleTouchInput(uint16_t current_touch_state, uint16_t last_touch_state, int engine_index, bool poly_mode, int effective_num_voices);
    void RenderBlock(const RenderParameters& params);
    void ResetVoices();
    
    const float* GetMainOutputBuffer() const { return mix_buffer_out_; }
    const float* GetAuxOutputBuffer() const { return mix_buffer_aux_; }

    void TriggerArpVoice(int pad_idx, int current_engine_index_val);
    bool IsAnyVoiceActive() const;
    void PolyToMono(int source_voice_idx);
    void ClearVoices();
    void OnEngineChange(int old_engine_idx, int new_engine_idx);

    uint16_t GetLastTouchState() const;
    void UpdateLastTouchState(uint16_t current_state);

private:
    plaits::Voice voices_[NUM_VOICES];
    plaits::Patch patches_[NUM_VOICES];
    plaits::Modulations modulations_[NUM_VOICES];
    VoiceEnvelope voice_envelopes_[NUM_VOICES];
    bool voice_active_[NUM_VOICES];
    float voice_note_[NUM_VOICES];
    plaits::Voice::Frame output_buffers_[NUM_VOICES][BLOCK_SIZE];

    float mix_buffer_out_[BLOCK_SIZE];
    float mix_buffer_aux_[BLOCK_SIZE];
    
    stmlib::BufferAllocator* allocator_;
    daisy::DaisySeed* hw_ptr_;

    void AllocateVoices();
    void InitVoiceParameters();
    void PrepVoiceParams(const RenderParameters& params);
    void ProcessEnvelopes(bool poly_mode);
    void UpdatePatchParams(plaits::Patch& patch, const PatchParams& params);
    void UpdateModAndEnv(plaits::Modulations& mod, VoiceEnvelope& env, bool percussive_engine, 
                        float attack_value, float release_value);
    void UpdateMonoTrigger(plaits::Modulations& mod, bool& active_flag, bool engine_changed_flag);
    void SilenceVoice(int voice_idx);
    void RetriggerVoice(int voice_idx);

    int FindVoiceForNote(float note, int engine_index, bool poly_mode, int max_voices);
    int FindFreeVoice(int max_voices);
    void AssignMonoNote(float note, bool percussive_engine);

    bool engine_changed_flag_ = false; 
    uint16_t last_touch_state_member_ = 0;

    static const float kTouchMidiNotes_[12];
};

extern PolyphonyEngine poly_engine;

#endif // POLYPHONY_H 