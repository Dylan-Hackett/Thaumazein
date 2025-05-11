#include "Thaumazein.h"
#include "mpr121_daisy.h"
// #include "DelayEffect.h"
#include "Polyphony.h" // Add include for PolyphonyEngine
#include <cmath>
#include <algorithm>
#include <vector> // Add vector for dynamic list

// const float MASTER_VOLUME = 0.7f; // Master output level scaler // REMOVED - Defined in Thaumazein.h

int  DetermineEngineSettings();
// void ConfigureDelaySettings(); // Ensure this is removed or commented
// void ProcessAudioOutput(AudioHandle::InterleavingOutputBuffer out, size_t size, float dry_level); // Ensure this is removed or commented
void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out);

// New helper function declarations
void ProcessUIAndControls();
void UpdateArpState(int& engineIndex, bool& poly_mode, int& effective_num_voices, bool& arp_on);
void RenderVoices(int engineIndex, bool poly_mode, int effective_num_voices, bool arp_on);
void ApplyEffectsAndOutput(AudioHandle::InterleavingOutputBuffer out, size_t size);

// Global variables for data sharing between decomposed functions

extern VoiceEnvelope voice_envelopes[NUM_VOICES];

extern bool voice_active[NUM_VOICES];

// Define the CpuLoadMeter instance
// CpuLoadMeter cpu_meter; // Removed: Now defined in Interface.cpp


static bool was_arp_on = false; // For ARP state change detection

volatile int current_engine_index = 0; // Global engine index controlled by touch pads

// ADD: flag to indicate engine change so we can retrigger voices even when notes are held
volatile bool engine_changed_flag = false;
// 0 = inactive, 2 = send trigger low this block, 1 = send trigger high next block
volatile int engine_retrigger_phase = 0;

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                 AudioHandle::InterleavingOutputBuffer out,
                 size_t size) {
    // Removed one-off debug print
    // Process UI controls at 1ms intervals inside audio callback
    static uint32_t last_ui_time = 0;
    uint32_t now_ms = hw.system.GetNow();
    if(now_ms - last_ui_time >= 1) {
        last_ui_time = now_ms;
        ProcessUIAndControls();
    }
    cpu_meter.OnBlockStart(); // Mark the beginning of the audio block
    
    // Variables to be passed between helper functions
    int engineIndex;
    bool poly_mode;
    int effective_num_voices;
    bool arp_on;

    // React to engine change flag by delegating voice migration to DSP layer.
    static int prev_engine_index_static = 0;
    if(engine_changed_flag) {
        poly_engine.OnEngineChange(prev_engine_index_static, current_engine_index);
        prev_engine_index_static = current_engine_index;
        engine_changed_flag = false; // Clear flag after handling
    }

    UpdateArpState(engineIndex, poly_mode, effective_num_voices, arp_on);
    RenderVoices(engineIndex, poly_mode, effective_num_voices, arp_on);
    ApplyEffectsAndOutput(out, size);

    cpu_meter.OnBlockEnd(); // Mark the end of the audio block
}

void ProcessUIAndControls() {
    ProcessControls();
    ReadKnobValues();
    
    // Tempo control for arpeggiator via timing knob
    if (arp_enabled) {
        arp.SetMainTempoFromKnob(delay_time_val);
    }

    float touch_control = touch_cv_value; 
    
    float intensity_factor = 0.5f;

    morph_knob_val   = morph_knob_val   * (1.0f - intensity_factor) + touch_control * intensity_factor;
    // delay_feedback_val = delay_feedback_val * (1.0f - intensity_factor) + touch_control * intensity_factor;  // Delay effect removed


}

void UpdateArpState(int& engineIndex, bool& poly_mode, int& effective_num_voices, bool& arp_on_out) {
    engineIndex = DetermineEngineSettings();
    poly_mode = (engineIndex <= 3);
    effective_num_voices = poly_mode ? NUM_VOICES : 1;

    bool current_arp_on = arp_enabled;
    if (!current_arp_on && was_arp_on) {
        poly_engine.ResetVoices();
        for (int v = 0; v < NUM_VOICES; ++v) {
        }
        poly_engine.UpdateLastTouchState(0);
    }
    was_arp_on = current_arp_on;
    arp_on_out = current_arp_on;

    if (current_arp_on) {
        arp.UpdateHeldNotes(current_touch_state, poly_engine.GetLastTouchState());
        arp.Process(BLOCK_SIZE);
    } else {
        poly_engine.HandleTouchInput(current_touch_state, poly_engine.GetLastTouchState(), engineIndex, poly_mode, effective_num_voices);
    }
    
    poly_engine.UpdateLastTouchState(current_touch_state);
}

void RenderVoices(int engineIndex, bool poly_mode, int effective_num_voices, bool arp_on) {
    PolyphonyEngine::RenderParameters params;
    params.engine_index = engineIndex;
    params.poly_mode = poly_mode;
    params.effective_num_voices = effective_num_voices;
    params.arp_on = arp_on;
    params.pitch_val = pitch_val;
    params.harm_knob_val = harm_knob_val;
    params.morph_knob_val = morph_knob_val;
    params.timbre_knob_val = timbre_knob_val;
    params.env_attack_val = env_attack_val;
    params.env_release_val = env_release_val;
    params.delay_mix_val = 0.0f;  // Delay removed, set mix to 0
    params.touch_cv_value = touch_cv_value;
    
    poly_engine.RenderBlock(params);
}

void ApplyEffectsAndOutput(AudioHandle::InterleavingOutputBuffer out, size_t size) {
    // Normalized output: average voices and apply master volume
    const float* buffer = poly_engine.GetMainOutputBuffer();
    const float norm = 1.0f / 32768.0f; // Convert int16 range to [-1,1]
    for (size_t i = 0; i < size; i += 2) {
        float raw = buffer[i/2];
        // Convert to float and average voices
        float sample = (raw * norm) / float(NUM_VOICES);
        // Apply master volume (keep below 1.0)
        sample *= MASTER_VOLUME;
        out[i]   = sample;
        out[i+1] = sample;
    }
    UpdatePerformanceMonitors(size, out);
}

int DetermineEngineSettings() {
    return current_engine_index;
}

void UpdatePerformanceMonitors(size_t size, AudioHandle::InterleavingOutputBuffer out) {
    if (size > 0) {
        float current_level = fabsf(out[0]);
        smoothed_output_level = smoothed_output_level * 0.99f + current_level * 0.01f;
    }

    static uint32_t display_counter = 0;
    static const uint32_t display_interval_blocks = (uint32_t)(sample_rate / BLOCK_SIZE * 3.0f);
    if (++display_counter >= display_interval_blocks) {
        display_counter = 0;
        update_display = true;
    }
} 