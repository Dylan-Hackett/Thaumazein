#include "Thaumazein.h"
#include "Arpeggiator.h"
#include "Polyphony.h"
#include "SynthStateStorage.h"
#include "plaits/resources.h"
#include <algorithm>

// --- Global hardware variables ---
DaisySeed hw;
CpuLoadMeter cpu_meter;

// Global definition for the touch sensor driver
thaumazein_hal::Mpr121 touch_sensor;

// Arpeggiator instance
Arpeggiator arp;
GPIO touch_leds[12];
// Timestamp for each LED when an ARP note triggers (ms)
volatile uint32_t arp_led_timestamps[12] = {0};
// Duration in milliseconds for each LED blink on ARP trigger
const uint32_t ARP_LED_DURATION_MS = 100;

// ADDED: Global flag for arpeggiator state, managed by Interface.cpp
volatile bool arp_enabled = false;

// Add: flag indicating if the MPR121 touch sensor was successfully initialised
bool touch_sensor_present = true;

// Hardware controls - Remapped to use ADCs 0-9
AnalogControl delay_time_knob;        // ADC 0 (Pin 15) Delay Time
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

// Definition for adc_raw_values moved from AudioProcessor.cpp
volatile float adc_raw_values[12] = {0.0f};

// Global knob values, moved from AudioProcessor.cpp
float pitch_val, harm_knob_val, timbre_knob_val, morph_knob_val;
float env_attack_val, env_release_val;
float delay_time_val;

// Simple diagnostic blink: flashes the Daisy user LED 'count' times rapidly.
static void DebugBlink(int count)
{
    for(int i = 0; i < count; ++i)
    {
        hw.SetLed(true);
        System::Delay(60);
        hw.SetLed(false);
        System::Delay(60);
    }
}

// --- Initialization functions ---
void InitializeHardware() {
    // Initialize Daisy Seed hardware
    hw.Configure();
    hw.Init(); // This calls SystemInit(), which sets VTOR to 0x08000000 by default
    
    // Initialize Plaits resources that require SDRAM
    plaits::PlaitsResourcesInit_C();  // Correctly call namespaced C-linkage version
    // plaits::TestLinkerFunction_C(); // This was removed, keep it removed or commented

    // Set sample rate to 32 kHz for lower CPU load and memory use
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_32KHZ);
    SynthStateStorage::InitMemoryMapped(); // QSPI is configured for memory-mapped mode here

    // Relocate the vector table to the QSPI flash base address
    // This address (0x90040000) must match the QSPIFLASH ORIGIN in the linker script
    // and where the .isr_vector section is actually placed.
    #if defined(__STM32H750xx_H) || defined(STM32H750xx) || defined(STM32H7XX)
        // Ensure SCB is usable (usually through stm32h7xx.h or similar, included by DaisySeed.h)
        SCB->VTOR = 0x90040000UL;
    #endif

    hw.SetAudioBlockSize(BLOCK_SIZE);
    sample_rate = hw.AudioSampleRate(); // now should be 32000
    // hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ); // debug: stick with default SR
    // sample_rate = hw.AudioSampleRate(); // Update sample_rate after setting it
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
}

void InitializeTouchSensor() {
    // Attempt to initialize the MPR121 touch sensor
    thaumazein_hal::Mpr121::Config touch_config;
    touch_config.Defaults(); 
    if (!touch_sensor.Init(touch_config)) {
        // Touch sensor failed to initialise – continue without it
        touch_sensor_present = false;
        hw.PrintLine("[WARN] MPR121 init failed – continuing without touch sensor");
        return; // Skip further configuration
    }
    // Override default thresholds for more sensitivity
    touch_sensor.SetThresholds(6, 3); 
}

void InitializeDelay() {
    // Delay initialization removed
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
    DebugBlink(1);

    poly_engine.Init(&hw);
    DebugBlink(2);

    InitializeControls();
    DebugBlink(3);

    InitializeTouchSensor();
    DebugBlink(4);

    // InitializeDelay(); // Delay effect removed
    // DebugBlink(5);

    InitializeTouchLEDs();
    DebugBlink(6);

    cpu_meter.Init(sample_rate, BLOCK_SIZE); // Initialize CPU Load Meter
    DebugBlink(7);

    // --- Initialize Arpeggiator ---
    arp.Init(sample_rate);
    DebugBlink(8);

    // Note trigger callback
    arp.SetNoteTriggerCallback([&](int pad_idx){ 
        poly_engine.TriggerArpVoice(pad_idx, current_engine_index); 
        arp_led_timestamps[11 - pad_idx] = hw.system.GetNow();
    });
    arp.SetDirection(Arpeggiator::AsPlayed);

    hw.StartLog(false); // Start log immediately (non-blocking)
    DebugBlink(9);

    hw.StartAudio(AudioCallback);
    DebugBlink(10);
    
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
    /*
        Bootloader entry via ADC combo (pads 8-10).
        Require the condition to be held for ~1 s to avoid false triggers
        from floating ADC inputs or brief noise at startup.
    */
    static uint16_t hold_cnt = 0;
    const uint16_t kHoldFrames = 500; // 500 * 2 ms ≈ 1 s

    // Skip bootloader combo for first 5 seconds after power-up to avoid
    // false triggers from floating ADC inputs while they settle.
    if(System::GetNow() < 5000)
        return;

    bool combo_pressed = (arp_pad.GetRawFloat()   > 0.5f) &&
                         (model_prev_pad.GetRawFloat() > 0.5f) &&
                         (model_next_pad.GetRawFloat() > 0.5f);

    if(combo_pressed)
    {
        if(++hold_cnt >= kHoldFrames)
        {
            hw.PrintLine("Entering Daisy DFU bootloader (ADC combo)…");
            System::Delay(100);
            // Jump to Daisy bootloader and keep it in DFU until a new
            // image is flashed. This avoids ending up in the STM ROM DFU
            // which cannot load QSPI apps.
            System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
        }
    }
    else
    {
        hold_cnt = 0; // reset counter when combo released
    }
}

void UpdateLED() {
    // LED based on voice activity (any voice active)
    bool any_voice_active = poly_engine.IsAnyVoiceActive(); // NEW Access
    
    bool led_on = any_voice_active;
    if (!any_voice_active && (System::GetNow() % 1000) < 500) { // Blink if inactive
        led_on = true;
    }
    hw.SetLed(led_on);
}

// NEW Function for Arpeggiator Toggle Pad
void UpdateArpeggiatorToggle() {
    constexpr float kOnThreshold  = 0.30f;   // more sensitive press detection
    constexpr float kOffThreshold = 0.20f;   // lower release threshold for fast reset
    static bool pad_pressed = false;          // debounced pad pressed state

    float pad_read = arp_pad.Value();

    // Detect state transitions with hysteresis
    if(!pad_pressed && pad_read > kOnThreshold)
    {
        pad_pressed = true;
        // Rising edge detected -> toggle arp
        arp_enabled = !arp_enabled;
        if(arp_enabled)
        {
            arp.Init(sample_rate);          // restart timing
            arp.SetNoteTriggerCallback([&](int pad_idx){ 
                poly_engine.TriggerArpVoice(pad_idx, current_engine_index); 
                arp_led_timestamps[11 - pad_idx] = hw.system.GetNow();
            });
            arp.SetDirection(Arpeggiator::AsPlayed); // Set default direction to AsPlayed
        }
        else
        {
            // Clearing held notes ensures LEDs revert to touch-indication mode
            arp.ClearNotes();
        }
    }
    else if(pad_pressed && pad_read < kOffThreshold)
    {
        // Consider pad released only when it falls well below off threshold
        pad_pressed = false;
    }
}

// NEW Function for Engine Selection
void UpdateEngineSelection() {
    // --- Model selection logic via touch pads ---
    const float threshold = 0.5f;
    const int kNumEngines = MAX_ENGINE_INDEX + 1; // inclusive range 0..MAX_ENGINE_INDEX
    const int kDebounceCount = 10; // blocks for debounce (~3ms)
    static int model_prev_counter = 0;
    static int model_next_counter = 0;
    static bool debounced_prev = false;
    static bool debounced_next = false;

    // Raw readings
    bool raw_prev = model_prev_pad.Value() > threshold;
    bool raw_next = model_next_pad.Value() > threshold;

    // Update debounce counters
    model_prev_counter = raw_prev
        ? std::min(model_prev_counter + 1, kDebounceCount)
        : std::max(model_prev_counter - 1, 0);
    model_next_counter = raw_next
        ? std::min(model_next_counter + 1, kDebounceCount)
        : std::max(model_next_counter - 1, 0);

    // Determine debounced states (press when counter > half)
    bool new_debounced_prev = model_prev_counter > (kDebounceCount / 2);
    bool new_debounced_next = model_next_counter > (kDebounceCount / 2);

    // On rising edge, update engine index
    if (new_debounced_prev && !debounced_prev) {
        current_engine_index = (current_engine_index + 1) % kNumEngines;
        engine_changed_flag = true;
    }
    if (new_debounced_next && !debounced_next) {
        current_engine_index = (current_engine_index - 1 + kNumEngines) % kNumEngines;
        engine_changed_flag = true;
    }

    debounced_prev = new_debounced_prev;
    debounced_next = new_debounced_next;

    // Voice migration/reset logic has been moved to PolyphonyEngine::OnEngineChange
    // Audio layer (AudioProcessor.cpp) now handles reacting to engine_changed_flag.
}

// Moved from AudioProcessor.cpp
void ProcessControls() {
    delay_time_knob.Process();        // ADC 0
    env_release_knob.Process();       // ADC 2
    env_attack_knob.Process();        // ADC 3
    timbre_knob.Process();            // ADC 4
    harmonics_knob.Process();         // ADC 5
    morph_knob.Process();             // ADC 6
    pitch_knob.Process();             // ADC 7
    // Process the remaining ADC-based controls
    arp_pad.Process();            // Process ADC 8: Arpeggiator Toggle Pad
    model_prev_pad.Process();     // Process ADC 9: Model Select Previous Pad
    model_next_pad.Process();     // Process ADC 10: Model Select Next Pad
    mod_wheel.Process();          // Process ADC 11: Mod Wheel Control

    // Read raw values for ALL 12 ADC channels
    for(int i = 0; i < 12; ++i) {
        adc_raw_values[i] = hw.adc.GetFloat(i);
    }

    // Call the new engine selection function
    UpdateEngineSelection();
    UpdateArpeggiatorToggle(); // Call the new arp toggle function
}

// Moved from AudioProcessor.cpp
void ReadKnobValues() {
    delay_time_val = delay_time_knob.Value();           // ADC 0
    env_release_val = env_release_knob.Value();       // ADC 2
    env_attack_val = env_attack_knob.Value();        // ADC 3
    timbre_knob_val = timbre_knob.Value();            // ADC 4
    harm_knob_val = harmonics_knob.Value();         // ADC 5
    morph_knob_val = morph_knob.Value();             // ADC 6
    pitch_val = pitch_knob.Value();                 // ADC 7

    // Derive separate mix and feedback values from the combined knob
    // Simple approach: use the same value for both
    // delay_mix_val = delay_mix_feedback_val;
    // delay_feedback_val = delay_mix_feedback_val;

    // Restore touch-pad pressure modulation (was previously in audio ISR)
    const float intensity = 0.5f;
    morph_knob_val      = morph_knob_val * (1.0f - intensity) + touch_cv_value * intensity;
    // delay_feedback_val  = delay_feedback_val * (1.0f - intensity) + touch_cv_value * intensity;
} 