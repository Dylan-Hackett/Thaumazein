#ifndef POLYPHONY_H
#define POLYPHONY_H

#include "daisy_seed.h"
#include "plaits/dsp/voice.h" // For plaits::Voice, plaits::Patch, plaits::Modulations
#include "VoiceEnvelope.h"    // For VoiceEnvelope
#include "Thaumazein.h"       // For NUM_VOICES, BLOCK_SIZE, SAMPLE_RATE
#include "stmlib/utils/buffer_allocator.h" // Trying this again - should be relative to -Ieurorack path
// #include "stmlib/stmlib.h" // Trying this again - should be relative to -Ieurorack path

// Forward declaration if Thaumazein.h is not included directly or for specific types
// class Thaumazein; 

// Forward declare stmlib namespace and MonoBlockAllocator class
namespace stmlib {
    class MonoBlockAllocator;
}

namespace plaits {
    class Voice;
}

class PolyphonyEngine {
public:
    PolyphonyEngine();
    ~PolyphonyEngine();

    void Init(daisy::DaisySeed* hw); // Pass hardware reference if needed for initialization (e.g. sample rate)
    void HandleTouchInput(uint16_t current_touch_state, uint16_t last_touch_state, int engine_index, bool poly_mode, int effective_num_voices);
    void RenderBlock(int engine_index, bool poly_mode, int effective_num_voices, bool arp_on, float pitch_val, float harm_knob_val, float morph_knob_val, float timbre_knob_val, float env_attack_val, float env_release_val, float delay_mix_val, float touch_cv_value);
    void ResetVoices();
    
    // Method for Arpeggiator to trigger notes
    void TriggerArpNote(int voice_index, float note, float strength, int engine_index, bool percussive_engine);

    // Access to mixed output if PolyphonyEngine handles mixing internally
    // Or methods to get individual voice outputs if mixing happens externally
    const float* GetMainOutputBuffer() const { return mix_buffer_out_; }
    const float* GetAuxOutputBuffer() const { return mix_buffer_aux_; }

    // Methods for Interface.cpp interactions
    void TriggerArpCallbackVoice(int pad_idx, int current_engine_index_val);
    bool IsVoiceActive(int voice_index) const;
    bool IsAnyVoiceActive() const;
    float GetVoiceNote(int voice_index) const;
    void TransferPolyToMonoVoice(int source_voice_idx);
    void ClearAllVoicesForEngineSwitch();

    // New: Handle logic when the engine changes (UI -> DSP decoupling)
    void OnEngineChange(int old_engine_idx, int new_engine_idx);

    // Getter and Setter for last_touch_state_member_
    uint16_t GetLastTouchState() const;
    void UpdateLastTouchState(uint16_t current_state);

private:
    // Voice-related data members
    plaits::Voice voices_[NUM_VOICES];
    plaits::Patch patches_[NUM_VOICES];
    plaits::Modulations modulations_[NUM_VOICES];
    VoiceEnvelope voice_envelopes_[NUM_VOICES];
    bool voice_active_[NUM_VOICES];
    float voice_note_[NUM_VOICES]; // Stores the MIDI note number for each voice
    plaits::Voice::Frame output_buffers_[NUM_VOICES][BLOCK_SIZE];

    // Internal mix buffers
    float mix_buffer_out_[BLOCK_SIZE];
    float mix_buffer_aux_[BLOCK_SIZE];
    
    // Other necessary private members or helper functions can be added here
    // For example, a pointer to the allocator if Plaits voices need it
    stmlib::BufferAllocator* allocator_;
    daisy::DaisySeed* hw_ptr_; // Store hardware pointer if needed beyond Init

    // Helper functions (prototypes for now, to be moved from Polyphony.cpp/AudioProcessor.cpp)
    void AllocateVoices(); // From Polyphony.cpp's InitVoices
    void InitVoiceParameters(); // Common initialization for voice params

    // Moved logic from AudioProcessor's PrepareVoiceParameters
    void PrepareVoiceParametersInternal(int engine_index, bool poly_mode, int max_voice_idx, bool arp_on, 
                                       float pitch_val, float harm_knob_val, float morph_knob_val, 
                                       float timbre_knob_val, float env_attack_val, float env_release_val);
    
    // Moved logic from AudioProcessor's ProcessVoiceEnvelopes
    void ProcessVoiceEnvelopesInternal(bool poly_mode);

    // Specific voice update logic, to be refactored from Polyphony.cpp or AudioProcessor.cpp helpers
    void UpdateVoicePatchParamsInternal(plaits::Patch& patch, int engine_index, float note, float global_pitch_offset, float current_global_harmonics, float current_global_timbre, float current_global_morph, bool arp_on, float current_decay);
    void UpdateVoiceModulationAndEnvelopeInternal(plaits::Modulations& mod, VoiceEnvelope& env, bool percussive_engine, float attack_value, float release_value);
    void UpdateMonoNonArpVoiceTriggerInternal(plaits::Modulations& mod, bool& active_flag, bool engine_changed_flag); // Note: engine_changed_flag might need careful handling
    void RenderAndProcessPercussiveArpVoiceInternal(int voice_idx, int engine_index, float global_pitch_offset, float current_global_harmonics, float current_global_morph, float current_global_timbre, float current_decay);
    void SilenceVoiceOutputInternal(int voice_idx);
    void RetriggerActiveVoiceEnvelopeInternal(int voice_idx);


    // Touch handling logic (moved from Polyphony.cpp's HandleTouchInput)
    // This needs careful refactoring as the original HandleTouchInput is quite large
    // And interacts with global knob values.
    int FindVoiceForNote(float note, int engine_index, bool poly_mode, int max_voices);
    void AssignFreeVoice(float note, int engine_index, bool poly_mode, int max_voices);
    void HandleNoteOff(float note, int engine_index, bool poly_mode, int max_voices);
    int FindOldestVoice(bool poly_mode, int max_voices);
    void StealVoice(int voice_to_steal, float new_note, int engine_index);

    // Internal helpers for touch input, ported from global functions
    int FindAvailableVoiceInternal(int max_voices);
    void AssignMonoNoteInternal(float note, int engine_index, bool percussive_engine);

    // Keep track of engine change for retriggering
    // This might be better managed by AudioProcessor and passed in if it affects more than just voices.
    // For now, let's assume PolyphonyEngine might need to know about it for mono voice retriggering.
    bool engine_changed_flag_ = false; 
    uint16_t last_touch_state_member_ = 0;

    static const float kTouchMidiNotes_[12];

};

extern PolyphonyEngine poly_engine;

#endif // POLYPHONY_H 