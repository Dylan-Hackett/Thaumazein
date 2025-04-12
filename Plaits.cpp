#include "daisy_seed.h"
#include "daisysp.h"
#include "plaits/dsp/voice.h"
#include "mpr121_daisy.h"
#include "Effects/EchoDelay.h"
#include "Effects/BiquadFilters.h"
#include "Effects/DSPUtils.h"
#include <string>
#include <cmath> // Include for fabsf
#include <cstring> // Include for strcmp
#include <algorithm> // Needed for std::max/min if used for clamping

using namespace daisy;
using namespace daisysp;
using namespace stmlib; 
using namespace infrasonic;

// Simple stereo delay-based reverb 
class SimpleReverb {
public:
    SimpleReverb() {}
    ~SimpleReverb() {}
    
    void Init(float sample_rate) {
        // Clear delay lines
        for (int i = 0; i < kMaxDelaySize; i++) {
            delay_line_left_[i] = 0.0f;
            delay_line_right_[i] = 0.0f;
        }
        
        // Define tap positions for multitap delay (creates early reflections)
        // These prime numbers help create a more diffuse sound
        tap_positions_[0] = 739;   // ~15ms at 48kHz
        tap_positions_[1] = 1861;  // ~39ms at 48kHz
        tap_positions_[2] = 2421;  // ~50ms at 48kHz
        tap_positions_[3] = 4127;  // ~86ms at 48kHz
        
        // Initialize indices
        write_idx_ = 0;
        
        // Set default parameters
        SetDecay(0.92f);
    }
    
    void SetDecay(float decay) {
        decay_ = decay;
        // Ensure decay doesn't exceed 0.99 to prevent instability
        if (decay_ > 0.99f) decay_ = 0.99f;
    }
    
    void Process(float in_left, float in_right, float* out_left, float* out_right) {
        // Save inputs plus feedback from delay lines
        float input_left = in_left;
        float input_right = in_right;
        
        // Write to delay lines
        delay_line_left_[write_idx_] = input_left;
        delay_line_right_[write_idx_] = input_right;
        
        // Read from multiple taps and apply decay for diffusion
        *out_left = 0.0f;
        *out_right = 0.0f;
        
        for (int i = 0; i < kNumTaps; i++) {
            int read_idx = (write_idx_ - tap_positions_[i] + kMaxDelaySize) % kMaxDelaySize;
            
            // Cross-coupled feedback for more stereo spread
            float tap_decay = decay_ * (0.8f + (0.05f * i)); // Slightly different decay per tap
            
            // Mix L/R for cross-feed
            *out_left += delay_line_left_[read_idx] * tap_decay * 0.7f;
            *out_left += delay_line_right_[read_idx] * tap_decay * 0.3f;
            
            *out_right += delay_line_right_[read_idx] * tap_decay * 0.7f;
            *out_right += delay_line_left_[read_idx] * tap_decay * 0.3f;
        }
        
        // Feedback from the output back into the delay lines
        delay_line_left_[write_idx_] += (*out_left) * decay_ * 0.4f;
        delay_line_right_[write_idx_] += (*out_right) * decay_ * 0.4f;
        
        // Increment and wrap delay indices
        write_idx_ = (write_idx_ + 1) % kMaxDelaySize;
    }
    
private:
    static const int kMaxDelaySize = 8192; // ~170ms at 48kHz
    static const int kNumTaps = 4;
    
    float delay_line_left_[kMaxDelaySize];
    float delay_line_right_[kMaxDelaySize];
    int tap_positions_[kNumTaps];
    int write_idx_;
    float decay_;
};

#define NUM_VOICES 4 // Allocate for max polyphony (Tetraphonic)

DaisySeed hw;
Mpr121 touch_sensor;
EchoDelay<48000> delay;

// --- Polyphony Setup ---
plaits::Voice voices[NUM_VOICES];
plaits::Patch patches[NUM_VOICES];
plaits::Modulations modulations[NUM_VOICES];

// Place the shared buffer in SDRAM using the DSY_SDRAM_BSS attribute
// Increase buffer size for 4 voices (256KB)
DSY_SDRAM_BSS char shared_buffer[262144]; 

#define BLOCK_SIZE 32  // Standard block size
plaits::Voice::Frame output_buffers[NUM_VOICES][BLOCK_SIZE]; 

bool voice_active[NUM_VOICES] = {false};
float voice_note[NUM_VOICES] = {0.0f};
uint16_t last_touch_state = 0;

// Hardware controls
Switch button;
AnalogControl pitch_knob; 
AnalogControl harmonics_knob;
AnalogControl timbre_knob; 
AnalogControl decay_knob; 
AnalogControl harmonics_input; // Used for Wet/Dry
AnalogControl morph_input;     

// Map touch pads (0-11) to MIDI notes (E Phrygian scale starting at E2)
const float kTouchMidiNotes[12] = {
    40.0f, 41.0f, 43.0f, 45.0f, 47.0f, 48.0f, // E2, F2, G2, A2, B2, C3
    50.0f, 52.0f, 53.0f, 55.0f, 57.0f, 59.0f  // D3, E3, F3, G3, A3, B3
};

// Find the voice playing a specific MIDI note (up to max_voices)
int FindVoice(float note, int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (voice_active[v] && fabsf(voice_note[v] - note) < 0.1f) {
            return v;
        }
    }
    return -1; // Not found
}

// Find the first available (inactive) voice (up to max_voices)
int FindAvailableVoice(int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (!voice_active[v]) {
            return v;
        }
    }
    return -1; // All allowed voices active
}

// Steals voice 0 for mono mode
void AssignMonoNote(float note) {
    if (voice_active[0] && fabsf(voice_note[0] - note) > 0.1f) {
        voice_active[0] = false; // Mark old note for release
    }
    voice_note[0] = note;
    voice_active[0] = true;
    modulations[0].trigger = 1.0f; // Set trigger high for the new note
}

// Add a reverb downsampling factor to reduce CPU usage
#define REVERB_DOWNSAMPLE 2  // Process reverb at half rate

// CPU usage monitoring
float sample_rate = 48000.0f; 
volatile uint32_t avg_elapsed_us = 0; 
volatile bool update_display = false; 

// Engine configuration
// Allow all engines, polyphony determined dynamically
const int MAX_ENGINE_INDEX = 15; 

// Bootloader configuration
const uint32_t BOOTLOADER_HOLD_TIME_MS = 3000; // Hold button for 3 seconds
uint32_t button_held_start_time = 0;
bool button_was_pressed = false;

// --- Shift Input Config ---
const int SHIFT_ADC_CHANNEL = 6; // Use ADC 6 (Pin 22) for Shift

// --- Max Delay Definition ---
#define MAX_DELAY_SAMPLES 48000 // 1 second at 48kHz

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                  AudioHandle::InterleavingOutputBuffer out,
                  size_t size) {
    
    // --- Static variables to hold Plaits params when shift is active ---
    static float frozen_harmonics = 0.5f;
    static float frozen_decay = 0.5f;
    static float frozen_morph = 0.5f;
    // Engine is always live from knob
    // Pitch is always live from knob

    uint32_t start_time = System::GetUs();

    // --- Read Sensors & Controls ---
    button.Debounce();
    pitch_knob.Process();
    harmonics_knob.Process();
    timbre_knob.Process();
    decay_knob.Process();
    harmonics_input.Process(); // Default Delay Feedback
    morph_input.Process();     // Default Delay Lag

    float shift_input_val = hw.adc.GetFloat(SHIFT_ADC_CHANNEL);
    bool shift_active = (shift_input_val > 0.5f);

    uint16_t current_touch_state = touch_sensor.Touched();

    // Read knob values AFTER processing
    float pitch_val = pitch_knob.Value();
    float harm_knob_val = harmonics_knob.Value();
    float timbre_knob_val = timbre_knob.Value();
    float decay_knob_val = decay_knob.Value();
    float harm_cv_val = harmonics_input.Value(); 
    float morph_cv_val = morph_input.Value();     

    // --- Determine Dynamic Polyphony & Engine ---
    // Engine is always live from knob
    int engineIndex = static_cast<int>(timbre_knob_val * (MAX_ENGINE_INDEX + 1));
    if (engineIndex > MAX_ENGINE_INDEX) engineIndex = MAX_ENGINE_INDEX;
    bool poly_mode = (engineIndex <= 3); 
    int effective_num_voices = poly_mode ? 4 : 1;
    int max_voice_idx = effective_num_voices - 1; 

    // --- Process Touch Input (Dynamic Polyphony) ---
    for (int i = 0; i < 12; ++i) { 
        bool pad_currently_pressed = (current_touch_state >> i) & 1;
        bool pad_was_pressed = (last_touch_state >> i) & 1;
        float note_for_pad = kTouchMidiNotes[i];

        if (pad_currently_pressed && !pad_was_pressed) { // Note On
            if (poly_mode) {
                int voice_idx = FindAvailableVoice(effective_num_voices); 
                if (voice_idx != -1) {
                    voice_note[voice_idx] = note_for_pad;
                    voice_active[voice_idx] = true;
                    modulations[voice_idx].trigger = 1.0f; // Set persistent trigger
                }
            } else { // Mono Mode
                AssignMonoNote(note_for_pad);
            }
        } else if (!pad_currently_pressed && pad_was_pressed) { // Note Off
            if (poly_mode) {
                 int voice_idx = FindVoice(note_for_pad, effective_num_voices);
                 if (voice_idx != -1) {
                     voice_active[voice_idx] = false; // Mark inactive for release
                 }
            } else { // Mono Mode
                if (voice_active[0] && fabsf(voice_note[0] - note_for_pad) < 0.1f) {
                    voice_active[0] = false; // Mark voice 0 inactive
                }
            }
        }
    }
    last_touch_state = current_touch_state; 

    // --- Initialize Delay Parameters --- 
    float current_delay_feedback;
    float current_delay_time_s;
    float current_delay_lag_s;
    float current_delay_dry;
    float current_delay_wet;

    // --- Apply Shift Logic --- 
    if (shift_active) {
        // SHIFT MODE: Knobs control Delay, Plaits params use frozen values
        current_delay_wet = harm_knob_val;          // ADC 1 -> Wet Amount (0..1)
        current_delay_dry = 1.0f - current_delay_wet;
        current_delay_feedback = timbre_knob_val * 0.98f; // ADC 2 -> Feedback
        current_delay_time_s = 0.01f + decay_knob_val * 0.99f; // ADC 3 -> Time
        current_delay_lag_s = morph_cv_val * 0.2f;          // ADC 5 -> Lag

        // Plaits parameters use static 'frozen' values (updated below in 'else')
    } else {
        // NORMAL MODE: Knobs control Plaits, Delay uses default assignments
        
        // Update frozen Plaits params for next time shift is active
        frozen_harmonics = harm_knob_val;
        frozen_decay = decay_knob_val;
        frozen_morph = morph_cv_val;

        // Assign default delay controls
        current_delay_feedback = harm_cv_val * 0.98f;            // ADC 4 -> Feedback
        current_delay_time_s = 0.01f + decay_knob_val * 0.99f;   // ADC 3 -> Time
        current_delay_lag_s = morph_cv_val * 0.2f;            // ADC 5 -> Lag
        current_delay_dry = 0.6f; // Fixed Wet/Dry
        current_delay_wet = 0.4f;
    }

    // --- Set Delay Parameters --- 
    delay.SetFeedback(current_delay_feedback);
    delay.SetDelayTime(current_delay_time_s);
    delay.SetLagTime(current_delay_lag_s);

    // --- Prepare Global Pitch Offset --- 
    float global_pitch_offset = pitch_val * 24.f - 12.f;

    // --- Process Effective Voices --- 
    for (int v = 0; v <= max_voice_idx; ++v) { 
        // Update Patch using current/frozen values
        patches[v].note = voice_note[v] + global_pitch_offset;
        patches[v].engine = engineIndex; // Engine always live
        // Use frozen values if shift is active, otherwise use live knob values
        patches[v].harmonics = shift_active ? frozen_harmonics : harm_knob_val;
        patches[v].timbre = 0.5f; 
        patches[v].morph = shift_active ? frozen_morph : morph_cv_val;
        patches[v].lpg_colour = 0.0f;
        patches[v].decay = shift_active ? frozen_decay : decay_knob_val;
        
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
        modulations[v].trigger_patched = true; 
        modulations[v].trigger = voice_active[v] ? 1.0f : 0.0f; 
        
        // Render 
        voices[v].Render(patches[v], modulations[v], output_buffers[v], BLOCK_SIZE);
    }
    // Silence unused voices (when switching from poly to mono)
    for (int v = effective_num_voices; v < NUM_VOICES; ++v) {
         memset(output_buffers[v], 0, sizeof(plaits::Voice::Frame) * BLOCK_SIZE);
         // Keep voice_active[v] = false and modulations[v].trigger = 0.0f
    }

    // --- Clear & Mix Output Buffers --- 
    float mix_buffer_out[BLOCK_SIZE] = {0.0f};
    float mix_buffer_aux[BLOCK_SIZE] = {0.0f};
    for (int v = 0; v < NUM_VOICES; ++v) { // Mix all potential voices (inactive ones are silent)
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            mix_buffer_out[i] += output_buffers[v][i].out;
            mix_buffer_aux[i] += output_buffers[v][i].aux;
        }
    }

    // --- Process Echo Delay & Write Output --- 
    // Use calculated Wet/Dry mix based on shift state
    float dry_level = current_delay_dry;
    float wet_level = current_delay_wet;
    float norm_factor = (float)NUM_VOICES * 2.0f; 
    
    for (size_t i = 0; i < size; i += 2) {
        float dry_left  = mix_buffer_out[i/2] / 32768.f / norm_factor;
        float dry_right = mix_buffer_aux[i/2] / 32768.f / norm_factor;
        
        float wet_left = delay.Process(dry_left); 
        float wet_right = delay.Process(dry_right); 
                
        out[i]   = (dry_left * dry_level) + (wet_left * wet_level);
        out[i+1] = (dry_right * dry_level) + (wet_right * wet_level);
    }

    // --- Finish Timing & Update CPU Load --- 
    uint32_t end_time = System::GetUs();
    uint32_t elapsed_us = end_time - start_time;
    avg_elapsed_us = avg_elapsed_us * 0.99f + elapsed_us * 0.01f;
    static uint32_t display_counter = 0;
    if (++display_counter >= 100) { 
        display_counter = 0;
        update_display = true;
    }
}

int main(void) {
	// Initialize Daisy Seed hardware
	hw.Configure();
	hw.Init();
	hw.SetAudioBlockSize(BLOCK_SIZE);
	sample_rate = hw.AudioSampleRate();
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ); // Using 48kHz now
    sample_rate = hw.AudioSampleRate(); // Update sample_rate after setting it

    // Initialize the allocator with the SDRAM buffer
    stmlib::BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));

    // Initialize Plaits Voices 
    for (int i = 0; i < NUM_VOICES; ++i) { // Initialize all 4 voices
        voices[i].Init(&allocator);
        patches[i].engine = 0;      
        modulations[i].engine = 0; 
        modulations[i].trigger = 0.0f;
        voice_active[i] = false;
        voice_note[i] = 0.0f;
    }

	// --- Configure ADCs (Add Channel 6 for Shift) ---
	AdcChannelConfig adc_config[7]; // Increased size to 7
	adc_config[0].InitSingle(hw.GetPin(15)); // Pitch Knob
	adc_config[1].InitSingle(hw.GetPin(16)); // Harmonics Knob
	adc_config[2].InitSingle(hw.GetPin(17)); // Timbre Knob (Engine)
	adc_config[3].InitSingle(hw.GetPin(18)); // Decay Knob
	adc_config[4].InitSingle(hw.GetPin(20)); // Harmonics CV (Delay Feedback Default)
	adc_config[5].InitSingle(hw.GetPin(21)); // Morph CV (Delay Lag Default)
    adc_config[6].InitSingle(hw.GetPin(22)); // Shift Input (ADC6 -> Pin D21/22)
	hw.adc.Init(adc_config, 7); // Initialize 7 channels
	hw.adc.Start();

	// --- Initialize Controls ---
	pitch_knob.Init(hw.adc.GetPtr(0), sample_rate);
	harmonics_knob.Init(hw.adc.GetPtr(1), sample_rate);
	timbre_knob.Init(hw.adc.GetPtr(2), sample_rate);
	decay_knob.Init(hw.adc.GetPtr(3), sample_rate);
	harmonics_input.Init(hw.adc.GetPtr(4), sample_rate); // Default Delay Feedback
	morph_input.Init(hw.adc.GetPtr(5), sample_rate);     // Default Delay Lag

	// --- Initialize Buttons ---
	button.Init(hw.GetPin(27), sample_rate / 48.0f);

	// --- Initialize MPR121 ---
    Mpr121::Config touch_config;
    touch_config.Defaults();
    if (!touch_sensor.Init(touch_config)) {
        while(1) { hw.SetLed(true); System::Delay(50); hw.SetLed(false); System::Delay(50); }
    }

    // --- Initialize Echo Delay --- 
    delay.Init(sample_rate); 
    delay.SetLagTime(0.02f);         // Short lag for time changes
    delay.SetDelayTime(0.3f, true); // 300ms delay, set immediately
    delay.SetFeedback(0.5f);         // 50% feedback

	// --- Start Audio ---
	hw.StartAudio(AudioCallback);

	// --- Serial Log (Needs to be started for USB reading) ---
	hw.StartLog(false); // Start log without waiting (important!)
	hw.PrintLine("Plaits Synth Started - Ready for Bootloader CMD");
	char settings[64];
	// Print Max Voices (compile time)
	sprintf(settings, "Max Voices: %d | Block: %d | SR: %d", NUM_VOICES, BLOCK_SIZE, (int)sample_rate);
	hw.PrintLine(settings);
	sprintf(settings, "Engine Range: 0-%d", MAX_ENGINE_INDEX);
	hw.PrintLine(settings);
    sprintf(settings, "Mode: %s", (MAX_ENGINE_INDEX <=3) ? "Poly (0-3)" : "Dynamic Poly"); // Indicate mode
    hw.PrintLine(settings);
	hw.PrintLine("----------------");

	// --- Main Loop ---
	while (1) {
        // --- Button Check for Bootloader Reset (Keep this) ---
        bool button_pressed_now = button.RawState(); 
        if (button_pressed_now && !button_was_pressed) {
            button_held_start_time = System::GetNow();
        } else if (button_pressed_now && button_was_pressed) {
            uint32_t held_duration = System::GetNow() - button_held_start_time;
            if (held_duration > BOOTLOADER_HOLD_TIME_MS) { 
                hw.PrintLine("Resetting to bootloader (Button Hold)..."); 
                System::Delay(100); 
                System::ResetToBootloader();
            }
        } else if (!button_pressed_now && button_was_pressed) {
            button_held_start_time = 0;
        }
        button_was_pressed = button_pressed_now;

        // LED based on voice activity (any voice active)
        bool any_voice_active = false;
        for(int v=0; v<NUM_VOICES; ++v) { if(voice_active[v]) { any_voice_active = true; break; } }
        
        bool led_on = any_voice_active;
        if (!any_voice_active && (System::GetNow() % 1000) < 500) { // Blink if inactive
            led_on = true;
        }
        hw.SetLed(led_on);
        
        // --- Print CPU Load info ---
        if (update_display) {
            float block_duration_us = (float)BLOCK_SIZE / (float)sample_rate * 1000000.0f; 
            int cpu_percent_int = 0;
            if (block_duration_us > 1.0f) { 
                 cpu_percent_int = static_cast<int>(( (float)avg_elapsed_us / block_duration_us ) * 100.0f);
            }
            cpu_percent_int = cpu_percent_int < 0 ? 0 : (cpu_percent_int > 100 ? 100 : cpu_percent_int);

            char buffer[32]; 
            sprintf(buffer, "CPU: %d", cpu_percent_int); 
            hw.PrintLine(buffer);
            sprintf(buffer, "Avg Time: %lu us", (unsigned long)avg_elapsed_us); 
            hw.PrintLine(buffer);
            // Determine current effective engine index again for display
            int current_engine_idx = static_cast<int>(timbre_knob.Value() * (MAX_ENGINE_INDEX + 1));
             if (current_engine_idx > MAX_ENGINE_INDEX) current_engine_idx = MAX_ENGINE_INDEX;
            sprintf(buffer, "Engine: %d (%s)", current_engine_idx, (current_engine_idx <=3) ? "Poly-4" : "Mono");
            hw.PrintLine(buffer);
            hw.PrintLine("--------"); 
            
            update_display = false; 
        }

		System::Delay(10); // Yield for system tasks
	}
}
