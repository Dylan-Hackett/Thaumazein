#include "Thaumazein.h"
#include "Arpeggiator.h"

// --- Global hardware variables ---
DaisySeed hw;
Mpr121 touch_sensor;
EchoDelay<48000> delay;
// Arpeggiator instance
Arpeggiator arp;
// Global array for touch pad LEDs
GPIO touch_leds[12];
// Timestamp for each LED when an ARP note triggers (ms)
volatile uint32_t arp_led_timestamps[12] = {0};
// Duration in milliseconds for each LED blink on ARP trigger
const uint32_t ARP_LED_DURATION_MS = 100;

// Hardware controls - Remapped to use ADCs 0-9
Switch button;
AnalogControl delay_time_knob;        // ADC 0 (Pin 15) Delay Time
AnalogControl delay_mix_feedback_knob; // ADC 1 (Pin 16) Delay Mix & Feedback
AnalogControl env_release_knob;       // ADC 2 (Pin 17) Envelope Release
AnalogControl env_attack_knob;        // ADC 3 (Pin 18) Envelope Attack
AnalogControl timbre_knob;            // ADC 4 (Pin 19) Plaits Timbre
AnalogControl harmonics_knob;         // ADC 5 (Pin 20) Plaits Harmonics
AnalogControl morph_knob;             // ADC 6 (Pin 21) Plaits Morph
AnalogControl pitch_knob;             // ADC 7 (Pin 22) Plaits Pitch
AnalogControl arp_pad;               // ADC 8 (Pin 23) Arpeggiator Toggle Pad
AnalogControl model_prev_pad;        // ADC 9 (Pin 24) Model Select Previous Pad
AnalogControl model_next_pad;        // ADC 10 (Pin 25) Model Select Next Pad
AnalogControl mod_wheel;             // ADC 11 (Pin 28) Mod Wheel Control

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
    // --- Configure ADCs (12 Channels) ---
    AdcChannelConfig adc_config[12];
    adc_config[0].InitSingle(hw.GetPin(15)); // ADC 0: Delay Time
    adc_config[1].InitSingle(hw.GetPin(16)); // ADC 1: Delay Mix & Feedback
    adc_config[2].InitSingle(hw.GetPin(17)); // ADC 2: Envelope Release
    adc_config[3].InitSingle(hw.GetPin(18)); // ADC 3: Envelope Attack
    adc_config[4].InitSingle(hw.GetPin(19)); // ADC 4: Plaits Timbre
    adc_config[5].InitSingle(hw.GetPin(20)); // ADC 5: Plaits Harmonics
    adc_config[6].InitSingle(hw.GetPin(21)); // ADC 6: Plaits Morph
    adc_config[7].InitSingle(hw.GetPin(22)); // ADC 7: Plaits Pitch
    adc_config[8].InitSingle(hw.GetPin(23)); // ADC 8: Arpeggiator Toggle Pad
    adc_config[9].InitSingle(hw.GetPin(24)); // ADC 9: Model Select Previous Pad
    adc_config[10].InitSingle(hw.GetPin(25)); // ADC 10: Model Select Next Pad
    adc_config[11].InitSingle(hw.GetPin(28)); // ADC 11: Mod Wheel Control
    hw.adc.Init(adc_config, 12); // Initialize 12 channels
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
    arp_pad.Init(hw.adc.GetPtr(8), sample_rate);               // ADC 8
    model_prev_pad.Init(hw.adc.GetPtr(9), sample_rate);        // ADC 9
    model_next_pad.Init(hw.adc.GetPtr(10), sample_rate);       // ADC 10
    mod_wheel.Init(hw.adc.GetPtr(11), sample_rate);            // ADC 11

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

// Initialize GPIO and light up all touch pad LEDs
void InitializeTouchLEDs() {
    GPIO::Config led_cfg;
    led_cfg.mode  = GPIO::Mode::OUTPUT;
    led_cfg.pull  = GPIO::Pull::NOPULL;
    led_cfg.speed = GPIO::Speed::LOW;

    Pin led_pins[12] = {
        seed::D14, seed::D13, seed::D10, seed::D9,
        seed::D8,  seed::D7,  seed::D6,  seed::D5,
        seed::D4,  seed::D3,  seed::D2,  seed::D1
    };

    for(int i = 0; i < 12; ++i) {
        led_cfg.pin = led_pins[i];
        touch_leds[i].Init(led_cfg);
        touch_leds[i].Write(true);
    }
}

void InitializeSynth() {
    InitializeHardware();
    InitializeVoices(); // Call function from Polyphony.cpp
    InitializeControls();
    InitializeTouchSensor();
    InitializeDelay();
    InitializeTouchLEDs();
    cpu_meter.Init(sample_rate, BLOCK_SIZE); // Initialize the CPU Load Meter
    
    // --- Initialize Arpeggiator ---
    arp.Init(sample_rate);
    // Default arp tempo for audible stepping
    arp.SetMainTempo(8.0f);
    // Route arpeggiator triggers into Plaits voices
    arp.SetNoteTriggerCallback([](int pad_idx) {
        // Monophonic ARP: set AR mode and retrigger envelope without full reset
        voice_envelopes[0].SetMode(VoiceEnvelope::MODE_AR);
        // Assign the new note to voice 0
        float note = kTouchMidiNotes[pad_idx];
        voice_note[0] = note;
        voice_active[0] = true;
        // Start the envelope attack; release phase follows based on knob
        voice_envelopes[0].Trigger();
        // Send patched trigger to Plaits engine only for percussive modes
        if (current_engine_index > 7) {
            modulations[0].trigger = 1.0f;
            modulations[0].trigger_patched = true;
        }
        // Record the timestamp for ARP LED blink (pad 11→led[0], etc.)
        arp_led_timestamps[11 - pad_idx] = hw.system.GetNow();
    });
    
    // --- Start Audio ---
    hw.StartAudio(AudioCallback);
    
    // --- Serial Log (Needs to be started for USB reading) ---
    hw.StartLog(false); // Start log immediately (non-blocking)
    
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
void Bootload() {
    // Bootloader via ADC: pads on ADC 8, 9, and 10 pressed simultaneously at any time
    if (arp_pad.GetRawFloat() > 0.5f &&
        model_prev_pad.GetRawFloat() > 0.5f &&
        model_next_pad.GetRawFloat() > 0.5f) {
        hw.PrintLine("Entering bootloader (ADC combo)...");
        System::Delay(100);
        System::ResetToBootloader();
    }
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