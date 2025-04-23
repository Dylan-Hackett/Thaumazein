#include "Thaumazein.h"

// --- Global hardware variables ---
DaisySeed hw;
Mpr121 touch_sensor;
EchoDelay<48000> delay;

// Hardware controls - Remapped to use ADCs 0-7
Switch button;
AnalogControl delay_time_knob;        // ADC 0 (Pin 15) Delay Time (was Pitch)
AnalogControl delay_mix_feedback_knob; // ADC 1 (Pin 16) Delay Mix & Feedback (was Harmonics)
AnalogControl env_release_knob;       // ADC 2 (Pin 17) Envelope Release (was Timbre)
AnalogControl env_attack_knob;        // ADC 3 (Pin 18) Envelope Attack (was Decay)
AnalogControl timbre_knob;            // ADC 4 (Pin 19) Plaits Timbre/Engine (was Morph)
AnalogControl harmonics_knob;         // ADC 5 (Pin 20) Plaits Harmonics (was Delay Feedback)
AnalogControl morph_knob;             // ADC 6 (Pin 21) Plaits Morph (was Delay Time)
AnalogControl pitch_knob;             // ADC 7 (Pin 22) Plaits Pitch (was Delay Lag)

// CPU usage monitoring
float sample_rate = 48000.0f; 
volatile uint32_t avg_elapsed_us = 0; 
volatile bool update_display = false; 
// Output Level monitoring
volatile float smoothed_output_level = 0.0f; 

// Bootloader configuration
const uint32_t BOOTLOADER_HOLD_TIME_MS = 3000; // Hold button for 3 seconds
uint32_t button_held_start_time = 0;
bool button_was_pressed = false;

// --- Initialization functions ---
void InitializeHardware() {
    // Initialize Daisy Seed hardware
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(BLOCK_SIZE);
    sample_rate = hw.AudioSampleRate();
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ); // Using 48kHz now
    sample_rate = hw.AudioSampleRate(); // Update sample_rate after setting it
}

void InitializeControls() {
    // --- Configure ADCs (8 Channels - New Mapping) ---
    AdcChannelConfig adc_config[8]; 
    adc_config[0].InitSingle(hw.GetPin(15)); // ADC 0: Delay Time
    adc_config[1].InitSingle(hw.GetPin(16)); // ADC 1: Delay Mix & Feedback
    adc_config[2].InitSingle(hw.GetPin(17)); // ADC 2: Envelope Release
    adc_config[3].InitSingle(hw.GetPin(18)); // ADC 3: Envelope Attack
    adc_config[4].InitSingle(hw.GetPin(19)); // ADC 4: Plaits Timbre/Engine
    adc_config[5].InitSingle(hw.GetPin(20)); // ADC 5: Plaits Harmonics
    adc_config[6].InitSingle(hw.GetPin(21)); // ADC 6: Plaits Morph
    adc_config[7].InitSingle(hw.GetPin(22)); // ADC 7: Plaits Pitch
    hw.adc.Init(adc_config, 8); // Initialize 8 channels
    hw.adc.Start();

    // --- Initialize Controls (Matches new mapping) ---
    delay_time_knob.Init(hw.adc.GetPtr(0), sample_rate);        // ADC 0
    delay_mix_feedback_knob.Init(hw.adc.GetPtr(1), sample_rate); // ADC 1
    env_release_knob.Init(hw.adc.GetPtr(2), sample_rate);       // ADC 2
    env_attack_knob.Init(hw.adc.GetPtr(3), sample_rate);        // ADC 3
    timbre_knob.Init(hw.adc.GetPtr(4), sample_rate);            // ADC 4
    harmonics_knob.Init(hw.adc.GetPtr(5), sample_rate);         // ADC 5
    morph_knob.Init(hw.adc.GetPtr(6), sample_rate);             // ADC 6
    pitch_knob.Init(hw.adc.GetPtr(7), sample_rate);             // ADC 7

    // --- Initialize Buttons ---
    button.Init(hw.GetPin(27), sample_rate / 48.0f);
}

void InitializeTouchSensor() {
    // --- Initialize MPR121 ---
    Mpr121::Config touch_config;
    touch_config.Defaults();
    if (!touch_sensor.Init(touch_config)) {
        while(1) { hw.SetLed(true); System::Delay(50); hw.SetLed(false); System::Delay(50); }
    }
    // Override default thresholds for more sensitivity
    touch_sensor.SetThresholds(6, 3); 
}

void InitializeDelay() {
    // --- Initialize Echo Delay --- 
    delay.Init(sample_rate); 
    delay.SetLagTime(0.02f);         // Short lag for time changes
    delay.SetDelayTime(0.3f, true); // 300ms delay, set immediately
    delay.SetFeedback(0.5f);         // 50% feedback
}

void InitializeSynth() {
    InitializeHardware();
    InitializeVoices(); // Call function from Polyphony.cpp
    InitializeControls();
    InitializeTouchSensor();
    InitializeDelay();
    cpu_meter.Init(sample_rate, BLOCK_SIZE); // Initialize the CPU Load Meter
    
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
}

// --- User Interface Functions ---
void HandleButtonInput() {
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
}

void UpdateLED() {
    // LED based on voice activity (any voice active)
    bool any_voice_active = false;
    for(int v=0; v<NUM_VOICES; ++v) { 
        if(voice_active[v]) { 
            any_voice_active = true; 
            break; 
        } 
    }
    
    bool led_on = any_voice_active;
    if (!any_voice_active && (System::GetNow() % 1000) < 500) { // Blink if inactive
        led_on = true;
    }
    hw.SetLed(led_on);
} 