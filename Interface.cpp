#include "Archein.h"

// --- Global hardware variables ---
DaisySeed hw;
Mpr121 touch_sensor;
EchoDelay<48000> delay;

// Hardware controls - Order matches new ADC mapping
Switch button;
AnalogControl pitch_knob;          // ADC 0 (Pin 15) Plaits Pitch
AnalogControl harmonics_knob;      // ADC 1 (Pin 16) Plaits Harmonics
AnalogControl timbre_knob;         // ADC 2 (Pin 17) Plaits Timbre/Engine
AnalogControl decay_knob;          // ADC 3 (Pin 18) Plaits Decay
AnalogControl morph_knob;           // ADC 4 (Pin 20) Plaits Morph
AnalogControl delay_feedback_knob;  // ADC 5 (Pin 21) Delay Feedback
AnalogControl delay_time_knob;      // ADC 6 (Pin 22) Delay Time
AnalogControl delay_lag_knob;       // ADC 7 (Pin 23) Delay Lag
AnalogControl delay_mix_knob;       // ADC 8 (Pin 19) Delay Wet/Dry Mix
AnalogControl env_attack_knob;      // ADC 9 (Pin 24) Envelope Attack
AnalogControl env_release_knob;     // ADC 10 (Pin 25) Envelope Release

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
    // --- Configure ADCs (11 Channels - New Mapping) ---
    AdcChannelConfig adc_config[11]; 
    adc_config[0].InitSingle(hw.GetPin(15)); // ADC 0: Plaits Pitch
    adc_config[1].InitSingle(hw.GetPin(16)); // ADC 1: Plaits Harmonics
    adc_config[2].InitSingle(hw.GetPin(17)); // ADC 2: Plaits Timbre/Engine
    adc_config[3].InitSingle(hw.GetPin(18)); // ADC 3: Plaits Decay
    adc_config[4].InitSingle(hw.GetPin(20)); // ADC 4: Plaits Morph
    adc_config[5].InitSingle(hw.GetPin(21)); // ADC 5: Delay Feedback
    adc_config[6].InitSingle(hw.GetPin(22)); // ADC 6: Delay Time
    adc_config[7].InitSingle(hw.GetPin(23)); // ADC 7: Delay Lag
    adc_config[8].InitSingle(hw.GetPin(19)); // ADC 8: Delay Wet/Dry Mix
    adc_config[9].InitSingle(hw.GetPin(24)); // ADC 9: Envelope Attack
    adc_config[10].InitSingle(hw.GetPin(25)); // ADC 10: Envelope Release
    hw.adc.Init(adc_config, 11); // Initialize 11 channels
    hw.adc.Start();

    // --- Initialize Controls (Matches new mapping) ---
    pitch_knob.Init(hw.adc.GetPtr(0), sample_rate);          // ADC 0
    harmonics_knob.Init(hw.adc.GetPtr(1), sample_rate);      // ADC 1
    timbre_knob.Init(hw.adc.GetPtr(2), sample_rate);         // ADC 2
    decay_knob.Init(hw.adc.GetPtr(3), sample_rate);          // ADC 3
    morph_knob.Init(hw.adc.GetPtr(4), sample_rate);           // ADC 4
    delay_feedback_knob.Init(hw.adc.GetPtr(5), sample_rate); // ADC 5
    delay_time_knob.Init(hw.adc.GetPtr(6), sample_rate);     // ADC 6
    delay_lag_knob.Init(hw.adc.GetPtr(7), sample_rate);      // ADC 7
    delay_mix_knob.Init(hw.adc.GetPtr(8), sample_rate);       // ADC 8
    env_attack_knob.Init(hw.adc.GetPtr(9), sample_rate);     // ADC 9
    env_release_knob.Init(hw.adc.GetPtr(10), sample_rate);   // ADC 10

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